#ifndef QUEUE_MANAGER_H
#define QUEUE_MANAGER_H

#include "common.h"

// --- Function Declarations ---

/*
 * Purpose: Allocates and initializes a new shared queue structure, including
 *          memory for the message buffer and the appropriate synchronization
 *          primitives (semaphores or condition variables) based on the mode.
 * Accepts: initial_capacity - The desired initial size of the queue buffer.
 *          mode             - The synchronization mode (SYNC_MODE_SEM or SYNC_MODE_CONDVAR).
 * Returns: A pointer to the newly created queue_t structure on success,
 *          NULL on failure (prints error message).
 */
queue_t* queue_create(size_t initial_capacity, sync_mode_t mode);

/*
 * Purpose: Destroys the synchronization primitives (semaphores or condition
 *          variables and mutex) and frees the memory associated with the queue.
 * Accepts: q    - A pointer to the queue_t structure to destroy.
 *          mode - The synchronization mode the queue was created with.
 * Returns: None.
 */
void queue_destroy(queue_t *q, sync_mode_t mode);

/*
 * Purpose: Adds a message to the shared queue. This function acts as a dispatcher,
 *          calling the appropriate internal implementation based on the global
 *          g_sync_mode. Blocks if the queue is full. Handles EINTR.
 * Accepts: q             - Pointer to the shared queue.
 *          msg           - Pointer to the message to add.
 *          caller_prefix - String prefix for logging messages (e.g., "Producer N").
 * Returns: 0 on success, -1 on error or if termination is requested during wait.
 */
int queue_add(queue_t *q, const message_t *msg, const char* caller_prefix);

/*
 * Purpose: Removes a message from the shared queue. This function acts as a dispatcher,
 *          calling the appropriate internal implementation based on the global
 *          g_sync_mode. Blocks if the queue is empty. Handles EINTR.
 * Accepts: q             - Pointer to the shared queue.
 *          msg           - Pointer to a message_t structure to store the removed message.
 *          caller_prefix - String prefix for logging messages (e.g., "Consumer N").
 * Returns: 0 on success, -1 on error or if termination is requested during wait.
 */
int queue_remove(queue_t *q, message_t *msg, const char* caller_prefix);

/*
 * Purpose: Attempts to resize the queue's message buffer and adjust associated
 *          synchronization primitives. Handles both increasing and decreasing size.
 *          Shrinking requires waiting for enough empty slots.
 * Accepts: q      - Pointer to the shared queue.
 *          change - The amount to change the capacity by (positive to increase,
 *                   negative to decrease).
 * Returns: 0 on success, -1 on failure (e.g., realloc fails, cannot shrink below
 *          current count, error adjusting semaphores).
 */
int queue_resize(queue_t *q, int change);

/*
 * Purpose: Safely gets the current number of items in the queue.
 * Accepts: q - Pointer to the shared queue.
 * Returns: The number of items currently in the queue, or 0 if q is NULL.
 */
size_t queue_get_count(queue_t *q);

/*
 * Purpose: Safely gets the current capacity of the queue buffer.
 * Accepts: q - Pointer to the shared queue.
 * Returns: The current capacity of the queue, or 0 if q is NULL.
 */
size_t queue_get_capacity(queue_t *q);

/*
 * Purpose: Safely gets the total number of messages ever added to the queue.
 * Accepts: q - Pointer to the shared queue.
 * Returns: The total added count, or 0 if q is NULL.
 */
unsigned long queue_get_added_total(queue_t *q);

/*
 * Purpose: Safely gets the total number of messages ever extracted from the queue.
 * Accepts: q - Pointer to the shared queue.
 * Returns: The total extracted count, or 0 if q is NULL.
 */
unsigned long queue_get_extracted_total(queue_t *q);

#endif // QUEUE_MANAGER_H
