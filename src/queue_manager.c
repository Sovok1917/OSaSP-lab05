#include "queue_manager.h"

// Global variable indicating sync mode (defined in main.c)
extern sync_mode_t g_sync_mode; // Used by queue_add/remove dispatchers and resize
extern volatile sig_atomic_t g_terminate_flag; // Used for graceful exit during waits

// --- Internal Helper Function Declarations ---
static int queue_add_sem(queue_t *q, const message_t *msg, const char* caller_prefix);
static int queue_remove_sem(queue_t *q, message_t *msg, const char* caller_prefix);
static int queue_add_condvar(queue_t *q, const message_t *msg, const char* caller_prefix);
static int queue_remove_condvar(queue_t *q, message_t *msg, const char* caller_prefix);

/*
 * Purpose: Allocates and initializes a new shared queue structure, including
 *          memory for the message buffer and the appropriate synchronization
 *          primitives (semaphores or condition variables) based on the mode.
 * Accepts: initial_capacity - The desired initial size of the queue buffer.
 *          mode             - The synchronization mode (SYNC_MODE_SEM or SYNC_MODE_CONDVAR).
 * Returns: A pointer to the newly created queue_t structure on success,
 *          NULL on failure (prints error message).
 */
queue_t* queue_create(size_t initial_capacity, sync_mode_t mode) {
    if (initial_capacity == 0) initial_capacity = INITIAL_QUEUE_CAPACITY;
    if (initial_capacity < MIN_QUEUE_CAPACITY) initial_capacity = MIN_QUEUE_CAPACITY;
    if (initial_capacity > MAX_QUEUE_CAPACITY) initial_capacity = MAX_QUEUE_CAPACITY;

    queue_t *q = malloc(sizeof(queue_t));
    if (!q) { print_error("Queue Create", "Failed to allocate queue structure"); return NULL; }

    q->messages = malloc(initial_capacity * sizeof(message_t));
    if (!q->messages) { print_error("Queue Create", "Failed to allocate message buffer"); free(q); return NULL; }

    q->capacity = initial_capacity;
    q->count = 0;
    q->head_idx = 0;
    q->tail_idx = 0;
    q->added_count_total = 0;
    q->extracted_count_total = 0;

    int ret = pthread_mutex_init(&q->mutex, NULL);
    if (ret != 0) { errno = ret; print_error("Queue Create", "pthread_mutex_init failed"); free(q->messages); free(q); return NULL; }

    if (mode == SYNC_MODE_SEM) {
        if (sem_init(&q->empty_slots, 0, (unsigned int)initial_capacity) == -1) {
            print_error("Queue Create", "sem_init(empty_slots) failed"); goto cleanup_mutex;
        }
        if (sem_init(&q->full_slots, 0, 0) == -1) { // Initially 0 full slots
            print_error("Queue Create", "sem_init(full_slots) failed"); sem_destroy(&q->empty_slots); goto cleanup_mutex;
        }
        print_info("Queue Create", "Queue initialized successfully (Semaphore Mode).");
    } else { // SYNC_MODE_CONDVAR
        ret = pthread_cond_init(&q->not_empty, NULL);
        if (ret != 0) { errno = ret; print_error("Queue Create", "pthread_cond_init(not_empty) failed"); goto cleanup_mutex; }
        ret = pthread_cond_init(&q->not_full, NULL);
        if (ret != 0) { errno = ret; print_error("Queue Create", "pthread_cond_init(not_full) failed"); pthread_cond_destroy(&q->not_empty); goto cleanup_mutex; }
        print_info("Queue Create", "Queue initialized successfully (CondVar Mode).");
    }

    return q;

    cleanup_mutex:
    pthread_mutex_destroy(&q->mutex); // Ensure mutex is destroyed on error path
    free(q->messages);
    free(q);
    return NULL;
}

/*
 * Purpose: Destroys the synchronization primitives (semaphores or condition
 *          variables and mutex) and frees the memory associated with the queue.
 * Accepts: q    - A pointer to the queue_t structure to destroy.
 *          mode - The synchronization mode the queue was created with.
 * Returns: None.
 */
void queue_destroy(queue_t *q, sync_mode_t mode) {
    if (!q) return;
    print_info("Queue Destroy", "Destroying queue resources...");

    // Destroy synchronization primitives first
    if (mode == SYNC_MODE_SEM) {
        if (sem_destroy(&q->empty_slots) == -1 && errno != EINVAL) print_error("Queue Destroy", "sem_destroy(empty_slots) failed");
        if (sem_destroy(&q->full_slots) == -1 && errno != EINVAL) print_error("Queue Destroy", "sem_destroy(full_slots) failed");
    } else { // SYNC_MODE_CONDVAR
        int ret_cond_ne = pthread_cond_destroy(&q->not_empty);
        if (ret_cond_ne != 0 && ret_cond_ne != EINVAL) { errno = ret_cond_ne; print_error("Queue Destroy", "pthread_cond_destroy(not_empty) failed"); }
        int ret_cond_nf = pthread_cond_destroy(&q->not_full);
        if (ret_cond_nf != 0 && ret_cond_nf != EINVAL) { errno = ret_cond_nf; print_error("Queue Destroy", "pthread_cond_destroy(not_full) failed"); }
    }

    int ret_mutex = pthread_mutex_destroy(&q->mutex);
    if (ret_mutex != 0 && ret_mutex != EINVAL) { errno = ret_mutex; print_error("Queue Destroy", "pthread_mutex_destroy failed"); }

    // Free memory
    if (q->messages) {
        free(q->messages);
        q->messages = NULL;
    }
    free(q);
    q = NULL; // Good practice, though q is local to caller
    print_info("Queue Destroy", "Queue resources destroyed.");
}

/*
 * Purpose: Adds a message to the shared queue. This function acts as a dispatcher,
 *          calling the appropriate internal implementation based on the global
 *          g_sync_mode. Blocks if the queue is full. Handles EINTR.
 * Accepts: q             - Pointer to the shared queue.
 *          msg           - Pointer to the message to add.
 *          caller_prefix - String prefix for logging messages (e.g., "Producer N").
 * Returns: 0 on success, -1 on error or if termination is requested during wait.
 */
int queue_add(queue_t *q, const message_t *msg, const char* caller_prefix) {
    if (!q || !msg) {
        print_error(caller_prefix ? caller_prefix : "Queue Add", "NULL queue or message pointer.");
        return -1;
    }
    if (g_sync_mode == SYNC_MODE_SEM) {
        return queue_add_sem(q, msg, caller_prefix);
    } else {
        return queue_add_condvar(q, msg, caller_prefix);
    }
}

/*
 * Purpose: Removes a message from the shared queue. This function acts as a dispatcher,
 *          calling the appropriate internal implementation based on the global
 *          g_sync_mode. Blocks if the queue is empty. Handles EINTR.
 * Accepts: q             - Pointer to the shared queue.
 *          msg           - Pointer to a message_t structure to store the removed message.
 *          caller_prefix - String prefix for logging messages (e.g., "Consumer N").
 * Returns: 0 on success, -1 on error or if termination is requested during wait.
 */
int queue_remove(queue_t *q, message_t *msg, const char* caller_prefix) {
    if (!q || !msg) {
        print_error(caller_prefix ? caller_prefix : "Queue Remove", "NULL queue or message pointer.");
        return -1;
    }
    if (g_sync_mode == SYNC_MODE_SEM) {
        return queue_remove_sem(q, msg, caller_prefix);
    } else {
        return queue_remove_condvar(q, msg, caller_prefix);
    }
}

/*
 * Purpose: Internal implementation to add a message using POSIX semaphores.
 *          Waits for an empty slot, locks mutex, adds message, unlocks mutex,
 *          signals full slot. Handles EINTR during wait.
 * Accepts: q             - Pointer to the shared queue.
 *          msg           - Pointer to the message to add.
 *          caller_prefix - String prefix for logging messages.
 * Returns: 0 on success, -1 on error or termination request.
 */
static int queue_add_sem(queue_t *q, const message_t *msg, const char* caller_prefix) {
    // Wait for an empty slot
    while (sem_wait(&q->empty_slots) == -1) {
        if (errno == EINTR) {
            if (g_terminate_flag) { print_info(caller_prefix, "Terminating during wait for empty slot (EINTR)."); return -1; }
            continue; // Retry if interrupted but not terminating
        } else {
            print_error(caller_prefix, "sem_wait(empty_slots) failed"); return -1;
        }
    }

    // Check termination flag *after* acquiring semaphore, before locking mutex
    if (g_terminate_flag) {
        sem_post(&q->empty_slots); // Release the acquired slot if terminating
        print_info(caller_prefix, "Terminating after wait for empty slot.");
        return -1;
    }

    int ret_lock = pthread_mutex_lock(&q->mutex); PTHREAD_CHECK(ret_lock, "AddSem: Lock Mutex");

    // Critical section: Add message to queue
    // This check should ideally not fail if semaphore logic is correct
    if (q->count >= q->capacity) {
        pthread_mutex_unlock(&q->mutex);
        sem_post(&q->empty_slots); // Give back the slot if something is wrong
        print_error(caller_prefix, "Queue full after acquiring mutex (sem logic error?)");
        return -1;
    }
    memcpy(&q->messages[q->tail_idx], msg, sizeof(message_t));
    q->tail_idx = (q->tail_idx + 1) % q->capacity;
    q->count++;
    q->added_count_total++;

    int ret_unlock = pthread_mutex_unlock(&q->mutex); PTHREAD_CHECK(ret_unlock, "AddSem: Unlock Mutex");

    // Signal that a slot is now full
    if (sem_post(&q->full_slots) == -1) {
        print_error(caller_prefix, "sem_post(full_slots) failed");
        // This is a non-fatal error for the current operation but indicates a problem
    }
    return 0;
}

/*
 * Purpose: Internal implementation to remove a message using POSIX semaphores.
 *          Waits for a full slot, locks mutex, removes message, unlocks mutex,
 *          signals empty slot. Handles EINTR during wait.
 * Accepts: q             - Pointer to the shared queue.
 *          msg           - Pointer to store the removed message.
 *          caller_prefix - String prefix for logging messages.
 * Returns: 0 on success, -1 on error or termination request.
 */
static int queue_remove_sem(queue_t *q, message_t *msg, const char* caller_prefix) {
    // Wait for a full slot
    while (sem_wait(&q->full_slots) == -1) {
        if (errno == EINTR) {
            if (g_terminate_flag) { print_info(caller_prefix, "Terminating during wait for full slot (EINTR)."); return -1; }
            continue; // Retry
        } else {
            print_error(caller_prefix, "sem_wait(full_slots) failed"); return -1;
        }
    }

    if (g_terminate_flag) {
        sem_post(&q->full_slots); // Release acquired slot
        print_info(caller_prefix, "Terminating after wait for full slot.");
        return -1;
    }

    int ret_lock = pthread_mutex_lock(&q->mutex); PTHREAD_CHECK(ret_lock, "RemoveSem: Lock Mutex");

    if (q->count == 0) { // Should not happen if semaphores are correct
        pthread_mutex_unlock(&q->mutex);
        sem_post(&q->full_slots); // Give back slot
        print_error(caller_prefix, "Queue empty after acquiring mutex (sem logic error?)");
        return -1;
    }
    memcpy(msg, &q->messages[q->head_idx], sizeof(message_t));
    q->head_idx = (q->head_idx + 1) % q->capacity;
    q->count--;
    q->extracted_count_total++;

    int ret_unlock = pthread_mutex_unlock(&q->mutex); PTHREAD_CHECK(ret_unlock, "RemoveSem: Unlock Mutex");

    if (sem_post(&q->empty_slots) == -1) {
        print_error(caller_prefix, "sem_post(empty_slots) failed");
    }
    return 0;
}

/*
 * Purpose: Internal implementation to add a message using mutex and condition variables.
 *          Locks mutex, waits on 'not_full' condition if queue is full, adds message,
 *          signals 'not_empty' condition, unlocks mutex. Handles termination checks.
 * Accepts: q             - Pointer to the shared queue.
 *          msg           - Pointer to the message to add.
 *          caller_prefix - String prefix for logging messages.
 * Returns: 0 on success, -1 on error or termination request.
 */
static int queue_add_condvar(queue_t *q, const message_t *msg, const char* caller_prefix) {
    int ret;
    ret = pthread_mutex_lock(&q->mutex); PTHREAD_CHECK(ret, "AddCond: Lock Mutex");

    while (q->count == q->capacity && !g_terminate_flag) {
        print_info(caller_prefix, "Queue full, waiting...");
        ret = pthread_cond_wait(&q->not_full, &q->mutex); // Unlocks mutex, waits, re-locks on wake
        if (ret != 0) {
            errno = ret; print_error(caller_prefix, "pthread_cond_wait(not_full) failed");
            pthread_mutex_unlock(&q->mutex); // Ensure mutex is unlocked on error
            return -1;
        }
        // Spurious wakeup or actual signal, re-check condition
    }

    if (g_terminate_flag) { // Check termination after potential wait
        print_info(caller_prefix, "Terminating while waiting to add (or after wake-up).");
        pthread_mutex_unlock(&q->mutex);
        return -1;
    }

    // At this point, q->count < q->capacity (or terminate flag was set and handled)
    if (q->count >= q->capacity) { // Should not happen if logic is correct and not terminating
        print_error(caller_prefix, "Queue still full after cond_wait (logic error or race).");
        pthread_mutex_unlock(&q->mutex);
        return -1;
    }


    memcpy(&q->messages[q->tail_idx], msg, sizeof(message_t));
    q->tail_idx = (q->tail_idx + 1) % q->capacity;
    q->count++;
    q->added_count_total++;

    // Signal one waiting consumer (if any) that queue is no longer empty
    ret = pthread_cond_signal(&q->not_empty);
    if (ret != 0) { errno = ret; print_error(caller_prefix, "pthread_cond_signal(not_empty) failed"); }

    ret = pthread_mutex_unlock(&q->mutex); PTHREAD_CHECK(ret, "AddCond: Unlock Mutex");
    return 0;
}

/*
 * Purpose: Internal implementation to remove a message using mutex and condition variables.
 *          Locks mutex, waits on 'not_empty' condition if queue is empty, removes message,
 *          signals 'not_full' condition, unlocks mutex. Handles termination checks.
 * Accepts: q             - Pointer to the shared queue.
 *          msg           - Pointer to store the removed message.
 *          caller_prefix - String prefix for logging messages.
 * Returns: 0 on success, -1 on error or termination request.
 */
static int queue_remove_condvar(queue_t *q, message_t *msg, const char* caller_prefix) {
    int ret;
    ret = pthread_mutex_lock(&q->mutex); PTHREAD_CHECK(ret, "RemoveCond: Lock Mutex");

    while (q->count == 0 && !g_terminate_flag) {
        print_info(caller_prefix, "Queue empty, waiting...");
        ret = pthread_cond_wait(&q->not_empty, &q->mutex);
        if (ret != 0) {
            errno = ret; print_error(caller_prefix, "pthread_cond_wait(not_empty) failed");
            pthread_mutex_unlock(&q->mutex);
            return -1;
        }
    }

    if (g_terminate_flag) {
        print_info(caller_prefix, "Terminating while waiting to remove (or after wake-up).");
        pthread_mutex_unlock(&q->mutex);
        return -1;
    }

    if (q->count == 0) { // Should not happen if logic is correct and not terminating
        print_error(caller_prefix, "Queue still empty after cond_wait (logic error or race).");
        pthread_mutex_unlock(&q->mutex);
        return -1;
    }

    memcpy(msg, &q->messages[q->head_idx], sizeof(message_t));
    q->head_idx = (q->head_idx + 1) % q->capacity;
    q->count--;
    q->extracted_count_total++;

    ret = pthread_cond_signal(&q->not_full);
    if (ret != 0) { errno = ret; print_error(caller_prefix, "pthread_cond_signal(not_full) failed"); }

    ret = pthread_mutex_unlock(&q->mutex); PTHREAD_CHECK(ret, "RemoveCond: Unlock Mutex");
    return 0;
}


/*
 * Purpose: Attempts to resize the queue's message buffer and adjust associated
 *          synchronization primitives. Handles both increasing and decreasing size.
 *          Shrinking requires waiting for enough empty slots (semaphore mode).
 *          Correctly handles ring buffer data by linearizing it.
 * Accepts: q      - Pointer to the shared queue.
 *          change - The amount to change the capacity by (positive to increase,
 *                   negative to decrease).
 * Returns: 0 on success, -1 on failure.
 */
int queue_resize(queue_t *q, int change) {
    if (!q || change == 0) return -1;

    char prefix[64];
    snprintf(prefix, sizeof(prefix), "Queue Resize (%s by %d)", change > 0 ? "Increase" : "Decrease", change > 0 ? change : -change);
    print_info(prefix, "Resize requested.");

    int ret_lock = pthread_mutex_lock(&q->mutex); PTHREAD_CHECK(ret_lock, "Resize: Lock Mutex");

    size_t old_capacity = q->capacity;
    size_t current_count = q->count;
    size_t new_capacity;

    if (change > 0) {
        if ((size_t)change > MAX_QUEUE_CAPACITY - old_capacity) { // Check for overflow before addition
            new_capacity = MAX_QUEUE_CAPACITY;
        } else {
            new_capacity = old_capacity + (size_t)change;
        }
        if (new_capacity > MAX_QUEUE_CAPACITY) new_capacity = MAX_QUEUE_CAPACITY;
    } else { // change < 0
        size_t decrease_amount = (size_t)(-change);
        if (decrease_amount >= old_capacity) { // Prevent underflow to 0 or negative
            new_capacity = MIN_QUEUE_CAPACITY;
        } else {
            new_capacity = old_capacity - decrease_amount;
        }
        if (new_capacity < MIN_QUEUE_CAPACITY) new_capacity = MIN_QUEUE_CAPACITY;
    }

    if (new_capacity == old_capacity) {
        print_info(prefix, "No change in capacity needed/possible (already at min/max or no effective change).");
        pthread_mutex_unlock(&q->mutex);
        return 0;
    }

    if (new_capacity < current_count) {
        printf("[%s] Cannot shrink queue: new capacity %zu is smaller than current item count %zu.\r\n", prefix, new_capacity, current_count);
        pthread_mutex_unlock(&q->mutex);
        return -1;
    }

    printf("[%s] Attempting to change capacity from %zu to %zu (current items: %zu).\r\n", prefix, old_capacity, new_capacity, current_count);

    message_t *new_messages_buffer = malloc(new_capacity * sizeof(message_t));
    if (!new_messages_buffer) {
        print_error(prefix, "malloc for new message buffer failed");
        pthread_mutex_unlock(&q->mutex);
        return -1; // Malloc failure, abort not appropriate here, return error
    }

    // Linearly copy existing messages to the new buffer
    if (current_count > 0) {
        for (size_t i = 0; i < current_count; ++i) {
            memcpy(&new_messages_buffer[i], &q->messages[(q->head_idx + i) % old_capacity], sizeof(message_t));
        }
    }

    free(q->messages);
    q->messages = new_messages_buffer;
    q->capacity = new_capacity;
    q->head_idx = 0;
    q->tail_idx = current_count; // Next item will be inserted at index 'current_count'
    if ((size_t)q->tail_idx == q->capacity && q->capacity > 0) { // If queue is full after resize
        q->tail_idx = 0; // Wrap around for circular buffer logic
    }
    // If count is 0, tail_idx is 0. If count > 0 and count < capacity, tail_idx is count.
    // If count == capacity, tail_idx should be 0 (next write would be messages[0] if it were allowed)
    // The logic `q->tail_idx = (q->tail_idx + 1) % q->capacity;` in add handles this.
    // So, `q->tail_idx = current_count;` is fine if current_count < new_capacity.
    // If current_count == new_capacity, then tail_idx is new_capacity.
    // Let's simplify: if current_count == new_capacity, tail_idx is 0. Else tail_idx is current_count.
    q->tail_idx = (current_count == new_capacity && new_capacity > 0) ? 0 : current_count;


    printf("[%s] Buffer reallocated. New capacity: %zu, head: %d, tail: %d, count: %zu\r\n",
           prefix, q->capacity, q->head_idx, q->tail_idx, q->count);

    // Adjust Synchronization Primitives
    if (g_sync_mode == SYNC_MODE_SEM) {
        if (new_capacity > old_capacity) { // Increased size
            size_t added_slots = new_capacity - old_capacity;
            printf("[%s] Posting %zu new empty semaphore slots...\r\n", prefix, added_slots);
            for (size_t i = 0; i < added_slots; ++i) {
                if (sem_post(&q->empty_slots) == -1) print_error(prefix, "sem_post(empty_slots) failed during grow");
            }
        } else { // Decreased size (new_capacity < old_capacity)
            size_t removed_slots = old_capacity - new_capacity;
            printf("[%s] Waiting to acquire %zu removed empty semaphore slots...\r\n", prefix, removed_slots);
            for (size_t i = 0; i < removed_slots; ++i) {
                // This loop attempts to decrement the empty_slots semaphore.
                // It effectively "takes back" the slots that are no longer part of the queue.
                // If these slots are currently "empty" (sem_wait succeeds), they are reclaimed.
                // If they are notionally "full" (sem_wait would block), this call will block
                // until a consumer makes them empty, or until termination.
                while (sem_wait(&q->empty_slots) == -1) {
                    if (errno == EINTR) {
                        if (g_terminate_flag) {
                            print_info(prefix, "Terminating during sem_wait for shrink.");
                            // Unlock and return error, as resize cannot complete.
                            // The queue might be in an inconsistent state regarding semaphore counts.
                            pthread_mutex_unlock(&q->mutex);
                            return -1;
                        }
                        continue; // Retry if interrupted
                    } else {
                        print_error(prefix, "sem_wait(empty_slots) failed during shrink");
                        pthread_mutex_unlock(&q->mutex); // Unlock before failing
                        return -1;
                    }
                }
            }
            printf("[%s] Acquired %zu empty slots for shrinking.\r\n", prefix, removed_slots);
        }
    } else { // SYNC_MODE_CONDVAR
        // After resize, conditions for not_empty or not_full might have changed.
        // Broadcast to wake up any waiting threads so they can re-evaluate.
        print_info(prefix, "Broadcasting condition variables after resize...");
        pthread_cond_broadcast(&q->not_empty);
        pthread_cond_broadcast(&q->not_full);
    }

    int ret_unlock = pthread_mutex_unlock(&q->mutex); PTHREAD_CHECK(ret_unlock, "Resize: Unlock Mutex");
    print_info(prefix, "Resize complete.");
    return 0;
}


/*
 * Purpose: Safely gets the current number of items in the queue.
 * Accepts: q - Pointer to the shared queue.
 * Returns: The number of items currently in the queue, or 0 if q is NULL.
 */
size_t queue_get_count(queue_t *q) {
    if (!q) return 0;
    size_t count_val = 0;
    int ret_lock = pthread_mutex_lock(&q->mutex);
    if (ret_lock != 0) { errno = ret_lock; print_error("QueueGetCount", "Failed to lock mutex"); return 0; /* Or some error indicator */ }
    count_val = q->count;
    pthread_mutex_unlock(&q->mutex);
    return count_val;
}

/*
 * Purpose: Safely gets the current capacity of the queue buffer.
 * Accepts: q - Pointer to the shared queue.
 * Returns: The current capacity of the queue, or 0 if q is NULL.
 */
size_t queue_get_capacity(queue_t *q) {
    if (!q) return 0;
    size_t cap_val = 0;
    int ret_lock = pthread_mutex_lock(&q->mutex);
    if (ret_lock != 0) { errno = ret_lock; print_error("QueueGetCapacity", "Failed to lock mutex"); return 0; }
    cap_val = q->capacity;
    pthread_mutex_unlock(&q->mutex);
    return cap_val;
}

/*
 * Purpose: Safely gets the total number of messages ever added to the queue.
 * Accepts: q - Pointer to the shared queue.
 * Returns: The total added count, or 0 if q is NULL.
 */
unsigned long queue_get_added_total(queue_t *q) {
    if (!q) return 0;
    unsigned long added_val = 0;
    int ret_lock = pthread_mutex_lock(&q->mutex);
    if (ret_lock != 0) { errno = ret_lock; print_error("QueueGetAdded", "Failed to lock mutex"); return 0; }
    added_val = q->added_count_total;
    pthread_mutex_unlock(&q->mutex);
    return added_val;
}

/*
 * Purpose: Safely gets the total number of messages ever extracted from the queue.
 * Accepts: q - Pointer to the shared queue.
 * Returns: The total extracted count, or 0 if q is NULL.
 */
unsigned long queue_get_extracted_total(queue_t *q) {
    if (!q) return 0;
    unsigned long extracted_val = 0;
    int ret_lock = pthread_mutex_lock(&q->mutex);
    if (ret_lock != 0) { errno = ret_lock; print_error("QueueGetExtracted", "Failed to lock mutex"); return 0; }
    extracted_val = q->extracted_count_total;
    pthread_mutex_unlock(&q->mutex);
    return extracted_val;
}
