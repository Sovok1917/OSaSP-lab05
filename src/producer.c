#include "producer.h"
#include "queue_manager.h"
#include "utils.h"

/*
 * Purpose: The entry point function for producer threads. Runs a loop that
 *          generates messages, adds them to the shared queue (blocking if full),
 *          prints status, and delays. Checks the global termination flag
 *          to exit gracefully.
 * Accepts: arg - A void pointer, expected to be a pointer to a dynamically
 *                allocated thread_args_t structure containing the thread ID
 *                and a pointer to the shared queue. The function takes
 *                ownership of and frees this argument structure.
 * Returns: Always returns NULL upon completion or termination.
 */
void* producer_thread_func(void *arg) {
    if (!arg) {
        print_error("Producer", "Invalid thread arguments received.");
        return NULL;
    }
    thread_args_t *args = (thread_args_t *)arg;
    queue_t *q = args->queue;
    int id = args->id;
    free(arg); // Free the args structure allocated in main

    unsigned int seed = (unsigned int)time(NULL) ^ (unsigned int)pthread_self();
    srand(seed);

    char info_prefix[32];
    snprintf(info_prefix, sizeof(info_prefix), "Producer %d", id);
    print_info(info_prefix, "Started.");

    while (!g_terminate_flag) {
        message_t msg;

        // Create Message
        msg.type = (unsigned char)(rand_r(&seed) % 256);
        msg.size = (unsigned char)(rand_r(&seed) % MAX_DATA_SIZE);
        for (int i = 0; i < msg.size; ++i) {
            msg.data[i] = (unsigned char)(rand_r(&seed) % 256);
        }
        msg.hash = 0;
        msg.hash = calculate_message_hash(&msg);

        // Add to Queue (blocks if full)
        if (queue_add(q, &msg, info_prefix) == -1) {
            if (g_terminate_flag) { /* Normal termination */ }
            else { print_error(info_prefix, "Failed to add message to queue."); }
            break;
        }

        // Print status
        unsigned long total_added = queue_get_added_total(q);
        printf("[%s] Added msg (Type:%u Size:%u Hash:%u). Total Added: %lu\r\n",
               info_prefix, msg.type, msg.size, msg.hash, total_added);
        fflush(stdout);

        // Delay
        struct timespec delay_req = {0, 0};
        struct timespec delay_rem;
        long delay_us = (rand_r(&seed) % 400000L) + 100000L;
        delay_req.tv_sec = delay_us / 1000000L;
        delay_req.tv_nsec = (delay_us % 1000000L) * 1000L;
        while (nanosleep(&delay_req, &delay_rem) == -1) {
            if (errno == EINTR) {
                if (g_terminate_flag) break;
                delay_req = delay_rem;
            } else {
                print_error(info_prefix, "nanosleep failed"); break;
            }
        }
    }

    print_info(info_prefix, "Terminating.");
    return NULL;
}
