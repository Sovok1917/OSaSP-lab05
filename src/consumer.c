#include "consumer.h"
#include "queue_manager.h"
#include "utils.h"

/*
 * Purpose: The entry point function for consumer threads. Runs a loop that
 *          removes messages from the shared queue (blocking if empty),
 *          verifies the message hash, prints status, and delays. Checks the
 *          global termination flag to exit gracefully.
 * Accepts: arg - A void pointer, expected to be a pointer to a dynamically
 *                allocated thread_args_t structure containing the thread ID
 *                and a pointer to the shared queue. The function takes
 *                ownership of and frees this argument structure.
 * Returns: Always returns NULL upon completion or termination.
 */
void* consumer_thread_func(void *arg) {
    if (!arg) {
        print_error("Consumer", "Invalid thread arguments received.");
        return NULL;
    }
    thread_args_t *args = (thread_args_t *)arg;
    queue_t *q = args->queue;
    int id = args->id;
    free(arg);

    unsigned int seed = (unsigned int)time(NULL) ^ (unsigned int)pthread_self();
    srand(seed);

    char info_prefix[32];
    snprintf(info_prefix, sizeof(info_prefix), "Consumer %d", id);
    print_info(info_prefix, "Started.");

    while (!g_terminate_flag) {
        message_t msg;
        unsigned short original_hash;
        unsigned short calculated_hash;

        // Remove from Queue (blocks if empty)
        if (queue_remove(q, &msg, info_prefix) == -1) {
            if (g_terminate_flag) { /* Normal termination */ }
            else { print_error(info_prefix, "Failed to remove message from queue."); }
            break;
        }

        // Process Message (Verify Hash)
        original_hash = msg.hash;
        msg.hash = 0;
        calculated_hash = calculate_message_hash(&msg);
        bool hash_ok = (original_hash == calculated_hash);

        // Print status
        unsigned long total_extracted = queue_get_extracted_total(q);
        printf("[%s] Extracted msg (Type:%u Size:%u Hash:%u -> %s). Total Extracted: %lu\r\n",
               info_prefix, msg.type, msg.size, original_hash,
               hash_ok ? "OK" : "FAIL", total_extracted);
        fflush(stdout);
        if (!hash_ok) {
            fprintf(stderr, "WARNING: [%s] Hash mismatch! Expected %u, Calculated %u\r\n",
                    info_prefix, original_hash, calculated_hash);
            fflush(stderr);
        }

        // Delay
        struct timespec delay_req = {0, 0};
        struct timespec delay_rem;
        long delay_us = (rand_r(&seed) % 400000L) + 200000L;
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
