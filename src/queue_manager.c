#include "queue_manager.h"

// Global variable indicating sync mode (defined in main.c)
extern sync_mode_t g_sync_mode;

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
        if (sem_init(&q->full_slots, 0, 0) == -1) {
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
    pthread_mutex_destroy(&q->mutex);
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

    if (mode == SYNC_MODE_SEM) {
        if (sem_destroy(&q->empty_slots) == -1) print_error("Queue Destroy", "sem_destroy(empty_slots) failed");
        if (sem_destroy(&q->full_slots) == -1) print_error("Queue Destroy", "sem_destroy(full_slots) failed");
    } else { // SYNC_MODE_CONDVAR
        int ret = pthread_cond_destroy(&q->not_empty);
        if (ret != 0) { errno = ret; print_error("Queue Destroy", "pthread_cond_destroy(not_empty) failed"); }
        ret = pthread_cond_destroy(&q->not_full);
        if (ret != 0) { errno = ret; print_error("Queue Destroy", "pthread_cond_destroy(not_full) failed"); }
    }

    int ret = pthread_mutex_destroy(&q->mutex);
    if (ret != 0) { errno = ret; print_error("Queue Destroy", "pthread_mutex_destroy failed"); }

    if (q->messages) free(q->messages);
    free(q);
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
    if (!q || !msg) return -1;
    while (sem_wait(&q->empty_slots) == -1) {
        if (errno == EINTR) { if (g_terminate_flag) { print_info(caller_prefix, "Terminating during wait for empty slot (EINTR)."); return -1; } continue; }
        else { print_error(caller_prefix, "sem_wait(empty_slots) failed"); return -1; }
    }
    if (g_terminate_flag) { sem_post(&q->empty_slots); print_info(caller_prefix, "Terminating after wait for empty slot."); return -1; }

    int ret = pthread_mutex_lock(&q->mutex); PTHREAD_CHECK(ret, "AddSem: Lock Mutex");
    if (q->count >= q->capacity) {
        pthread_mutex_unlock(&q->mutex); sem_post(&q->empty_slots);
        print_error(caller_prefix, "Queue full after acquiring mutex (sem logic error?)"); return -1;
    }
    memcpy(&q->messages[q->tail_idx], msg, sizeof(message_t));
    q->tail_idx = (q->tail_idx + 1) % q->capacity;
    q->count++;
    q->added_count_total++;
    ret = pthread_mutex_unlock(&q->mutex); PTHREAD_CHECK(ret, "AddSem: Unlock Mutex");

    if (sem_post(&q->full_slots) == -1) print_error(caller_prefix, "sem_post(full_slots) failed");
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
    if (!q || !msg) return -1;
    while (sem_wait(&q->full_slots) == -1) {
        if (errno == EINTR) { if (g_terminate_flag) { print_info(caller_prefix, "Terminating during wait for full slot (EINTR)."); return -1; } continue; }
        else { print_error(caller_prefix, "sem_wait(full_slots) failed"); return -1; }
    }
    if (g_terminate_flag) { sem_post(&q->full_slots); print_info(caller_prefix, "Terminating after wait for full slot."); return -1; }

    int ret = pthread_mutex_lock(&q->mutex); PTHREAD_CHECK(ret, "RemoveSem: Lock Mutex");
    if (q->count == 0) {
        pthread_mutex_unlock(&q->mutex); sem_post(&q->full_slots);
        print_error(caller_prefix, "Queue empty after acquiring mutex (sem logic error?)"); return -1;
    }
    memcpy(msg, &q->messages[q->head_idx], sizeof(message_t));
    q->head_idx = (q->head_idx + 1) % q->capacity;
    q->count--;
    q->extracted_count_total++;
    ret = pthread_mutex_unlock(&q->mutex); PTHREAD_CHECK(ret, "RemoveSem: Unlock Mutex");

    if (sem_post(&q->empty_slots) == -1) print_error(caller_prefix, "sem_post(empty_slots) failed");
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
    if (!q || !msg) return -1;
    int ret;

    ret = pthread_mutex_lock(&q->mutex); PTHREAD_CHECK(ret, "AddCond: Lock Mutex");

    while (q->count == q->capacity && !g_terminate_flag) {
        print_info(caller_prefix, "Queue full, waiting...");
        ret = pthread_cond_wait(&q->not_full, &q->mutex);
        if (ret != 0) {
            errno = ret; print_error(caller_prefix, "pthread_cond_wait(not_full) failed");
            pthread_mutex_unlock(&q->mutex);
            return -1;
        }
    }

    if (g_terminate_flag) {
        print_info(caller_prefix, "Terminating while waiting to add.");
        pthread_mutex_unlock(&q->mutex);
        return -1;
    }

    memcpy(&q->messages[q->tail_idx], msg, sizeof(message_t));
    q->tail_idx = (q->tail_idx + 1) % q->capacity;
    q->count++;
    q->added_count_total++;

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
    if (!q || !msg) return -1;
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
        print_info(caller_prefix, "Terminating while waiting to remove.");
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
 * Accepts: q      - Pointer to the shared queue.
 *          change - The amount to change the capacity by (positive to increase,
 *                   negative to decrease).
 * Returns: 0 on success, -1 on failure.
 */
int queue_resize(queue_t *q, int change) {
    if (!q || change == 0) return -1;

    char prefix[32];
    snprintf(prefix, sizeof(prefix), "Queue Resize (%s)", change > 0 ? "Increase" : "Decrease");
    print_info(prefix, "Resize requested.");

    int ret = pthread_mutex_lock(&q->mutex); PTHREAD_CHECK(ret, "Resize: Lock Mutex");

    size_t current_capacity = q->capacity;
    size_t current_count = q->count;
    size_t new_capacity;

    if (change > 0) {
        new_capacity = current_capacity + (size_t)change;
        if (new_capacity > MAX_QUEUE_CAPACITY) new_capacity = MAX_QUEUE_CAPACITY;
    } else {
        size_t decrease_amount = (size_t)(-change);
        if (decrease_amount >= current_capacity) new_capacity = MIN_QUEUE_CAPACITY;
        else new_capacity = current_capacity - decrease_amount;
        if (new_capacity < MIN_QUEUE_CAPACITY) new_capacity = MIN_QUEUE_CAPACITY;
    }

    if (new_capacity == current_capacity) {
        print_info(prefix, "No change in capacity needed/possible.");
        pthread_mutex_unlock(&q->mutex);
        return 0;
    }

    if (new_capacity < current_capacity && new_capacity < current_count) {
        print_info(prefix, "Cannot shrink queue: new capacity smaller than current item count.");
        pthread_mutex_unlock(&q->mutex);
        return -1;
    }

    print_info(prefix, "Attempting realloc...");

    // --- Reallocate Buffer ---
    // Warning: Simple realloc without element shifting can corrupt ring buffer logic
    // if head > tail. A robust solution is more complex. This is simplified for lab.
    message_t *new_buffer = realloc(q->messages, new_capacity * sizeof(message_t));
    if (!new_buffer) {
        print_error(prefix, "realloc failed");
        pthread_mutex_unlock(&q->mutex);
        return -1;
    }
    q->messages = new_buffer;
    size_t old_capacity = q->capacity;
    q->capacity = new_capacity;

    // Adjust indices (simple modulo, assumes elements weren't shifted)
    q->head_idx %= q->capacity;
    q->tail_idx %= q->capacity;

    printf("[%s] Realloc successful. New capacity: %zu\r\n", prefix, q->capacity);

    // --- Adjust Synchronization Primitives ---
    if (g_sync_mode == SYNC_MODE_SEM) {
        if (new_capacity > old_capacity) { // Increased size
            size_t added_slots = new_capacity - old_capacity;
            print_info(prefix, "Posting new empty semaphore slots...");
            for (size_t i = 0; i < added_slots; ++i) {
                if (sem_post(&q->empty_slots) == -1) print_error(prefix, "sem_post(empty_slots) failed during grow");
            }
        } else { // Decreased size
            size_t removed_slots = old_capacity - new_capacity;
            print_info(prefix, "Waiting to acquire removed empty semaphore slots...");
            for (size_t i = 0; i < removed_slots; ++i) {
                while (sem_wait(&q->empty_slots) == -1) {
                    if (errno == EINTR) { if (g_terminate_flag) { /* Handle termination? */ } continue; }
                    else { print_error(prefix, "sem_wait(empty_slots) failed during shrink"); goto unlock_and_fail; }
                }
            }
        }
    } else { // SYNC_MODE_CONDVAR
        print_info(prefix, "Broadcasting condition variables after resize...");
        pthread_cond_broadcast(&q->not_empty);
        pthread_cond_broadcast(&q->not_full);
    }

    ret = pthread_mutex_unlock(&q->mutex); PTHREAD_CHECK(ret, "Resize: Unlock Mutex");
    print_info(prefix, "Resize complete.");
    return 0;

    unlock_and_fail:
    pthread_mutex_unlock(&q->mutex);
    return -1;
}


/*
 * Purpose: Safely gets the current number of items in the queue.
 * Accepts: q - Pointer to the shared queue.
 * Returns: The number of items currently in the queue, or 0 if q is NULL.
 */
size_t queue_get_count(queue_t *q) {
    if (!q) {
        return 0;
    }
    size_t count = 0;
    pthread_mutex_lock(&q->mutex);
    count = q->count;
    pthread_mutex_unlock(&q->mutex);
    return count;
}

/*
 * Purpose: Safely gets the current capacity of the queue buffer.
 * Accepts: q - Pointer to the shared queue.
 * Returns: The current capacity of the queue, or 0 if q is NULL.
 */
size_t queue_get_capacity(queue_t *q) {
    if (!q) {
        return 0;
    }
    size_t cap = 0;
    pthread_mutex_lock(&q->mutex);
    cap = q->capacity;
    pthread_mutex_unlock(&q->mutex);
    return cap;
}

/*
 * Purpose: Safely gets the total number of messages ever added to the queue.
 * Accepts: q - Pointer to the shared queue.
 * Returns: The total added count, or 0 if q is NULL.
 */
unsigned long queue_get_added_total(queue_t *q) {
    if (!q) {
        return 0;
    }
    unsigned long added = 0;
    pthread_mutex_lock(&q->mutex);
    added = q->added_count_total;
    pthread_mutex_unlock(&q->mutex);
    return added;
}

/*
 * Purpose: Safely gets the total number of messages ever extracted from the queue.
 * Accepts: q - Pointer to the shared queue.
 * Returns: The total extracted count, or 0 if q is NULL.
 */
unsigned long queue_get_extracted_total(queue_t *q) {
    if (!q) {
        return 0;
    }
    unsigned long extracted = 0;
    pthread_mutex_lock(&q->mutex);
    extracted = q->extracted_count_total;
    pthread_mutex_unlock(&q->mutex);
    return extracted;
}
