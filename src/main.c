// /home/sovok/My Programs/Code Studies/OSaSP/Везенков М.Ю./lab05/src/main.c
#include "common.h"
#include "queue_manager.h"
#include "producer.h"
#include "consumer.h"
#include "utils.h"
#include <getopt.h>

// --- Global Variables ---
volatile sig_atomic_t g_terminate_flag = 0;
sync_mode_t g_sync_mode = SYNC_MODE_SEM; // Default

// Thread tracking
static pthread_t producer_threads[MAX_PRODUCERS];
static int producer_created_count = 0; // Number of currently active/joinable producers

static pthread_t consumer_threads[MAX_CONSUMERS];
static int consumer_created_count = 0; // Number of currently active/joinable consumers

static queue_t *g_queue = NULL;

// --- Static Function Declarations ---
/*
 * Purpose: Signal handler for SIGINT and SIGTERM in the main thread.
 *          Sets the global termination flag. Async-signal-safe.
 * Accepts: sig - The signal number received.
 * Returns: None.
 */
static void main_signal_handler(int sig);

/*
 * Purpose: Registers the signal handler (main_signal_handler) for
 *          SIGINT and SIGTERM signals for the main process. Exits on failure.
 * Accepts: None.
 * Returns: None.
 */
static void register_main_signal_handlers(void);

/*
 * Purpose: Cleanup routine registered with atexit. Signals termination to
 *          threads, attempts to unblock them, joins all created threads,
 *          destroys the queue, and restores the terminal.
 * Accepts: None.
 * Returns: None.
 */
static void cleanup_threads(void);

/*
 * Purpose: Prints command-line usage instructions to stderr.
 * Accepts: prog_name - The name of the executable (argv[0]).
 * Returns: None.
 */
static void print_usage(const char *prog_name);

/*
 * Purpose: Main entry point of the application. Parses command-line arguments,
 *          initializes resources (terminal, queue, signals, cleanup handler),
 *          runs the main command loop creating/managing threads, and handles
 *          program termination.
 * Accepts: argc - Argument count.
 *          argv - Argument vector.
 * Returns: EXIT_SUCCESS on normal completion, EXIT_FAILURE on error.
 */
int main(int argc, char *argv[]) {
    int opt;
    const char *mode_str = "sem";

    // Check for arguments if program requires them (example, not strictly needed by this program's current design if defaults are fine)
    // if (argc < MIN_EXPECTED_ARGS_IF_ANY && strcmp(argv[1], "-h") != 0 && strcmp(argv[1], "--help") != 0) {
    //     print_usage(argv[0]);
    //     return EXIT_FAILURE;
    // }


    // Parse Command Line Options
    while ((opt = getopt(argc, argv, "m:h")) != -1) {
        switch (opt) {
            case 'm': mode_str = optarg; break;
            case 'h': print_usage(argv[0]); return EXIT_SUCCESS;
            default: print_usage(argv[0]); return EXIT_FAILURE;
        }
    }
    if (optind < argc) { // Check for non-option arguments if they are not input files
        fprintf(stderr, "Error: Unexpected non-option arguments.\n");
        print_usage(argv[0]);
        return EXIT_FAILURE;
    }


    // Determine synchronization mode
    if (strcmp(mode_str, "sem") == 0) { g_sync_mode = SYNC_MODE_SEM; print_info("Main", "Using POSIX Semaphores."); }
    else if (strcmp(mode_str, "cond") == 0) { g_sync_mode = SYNC_MODE_CONDVAR; print_info("Main", "Using Condition Variables."); }
    else { fprintf(stderr, "Error: Invalid mode '%s'.\n", mode_str); print_usage(argv[0]); return EXIT_FAILURE; }

    // Initialize static memory (example, if any static memory needed runtime init)
    // initialize_static_data(); // Placeholder for explicit static memory initialization

    srand((unsigned int)time(NULL) ^ (unsigned int)getpid());
    print_info("Main", "Initializing system...");
    setup_terminal_noecho_nonblock();

    // Create queue
    g_queue = queue_create(INITIAL_QUEUE_CAPACITY, g_sync_mode);
    if (!g_queue) {
        restore_terminal(); // Ensure terminal is restored on early exit
        return EXIT_FAILURE;
    }

    register_main_signal_handlers();
    if (atexit(cleanup_threads) != 0) {
        print_error("Main", "atexit registration failed");
        // Perform manual cleanup as atexit handler won't run
        if (g_queue) queue_destroy(g_queue, g_sync_mode);
        restore_terminal();
        return EXIT_FAILURE;
    }

    // Print usage instructions
    printf("\r\n--- Producer/Consumer Control (Mode: %s) ---\r\n", mode_str);
    printf("  p: Add Producer        c: Add Consumer\r\n");
    printf("  P: Remove Last Producer  C: Remove Last Consumer\r\n");
    printf("  +: Increase Queue Cap.  -: Decrease Queue Cap.\r\n");
    printf("  s: Show Status         q: Quit\r\n");
    printf("--------------------------------------------------\r\n");
    printf("Enter command: ");
    fflush(stdout);

    char command = 0;
    int ret;

    // Main command loop
    while (!g_terminate_flag) {
        if (kbhit()) {
            command = (char)getchar();
            printf("\r\n"); // Echo command for clarity, then proceed

            switch (command) {
                case 'p':
                    if (producer_created_count < MAX_PRODUCERS) {
                        thread_args_t *args = malloc(sizeof(thread_args_t));
                        if (!args) { print_error("Main", "Failed to allocate args for producer"); abort(); }
                        args->id = producer_created_count + 1; // User-friendly 1-based ID
                        args->queue = g_queue;
                        args->sync_mode = g_sync_mode; // Pass current sync mode
                        ret = pthread_create(&producer_threads[producer_created_count], NULL, producer_thread_func, args);
                        if (ret == 0) {
                            producer_created_count++;
                            print_info("Main", "Producer thread created.");
                        } else {
                            errno = ret; print_error("Main", "pthread_create (producer) failed");
                            free(args); // Free args if thread creation failed
                        }
                    } else { print_info("Main", "Maximum producer threads reached."); }
                    break;
                case 'c':
                    if (consumer_created_count < MAX_CONSUMERS) {
                        thread_args_t *args = malloc(sizeof(thread_args_t));
                        if (!args) { print_error("Main", "Failed to allocate args for consumer"); abort(); }
                        args->id = consumer_created_count + 1; // User-friendly 1-based ID
                        args->queue = g_queue;
                        args->sync_mode = g_sync_mode; // Pass current sync mode
                        ret = pthread_create(&consumer_threads[consumer_created_count], NULL, consumer_thread_func, args);
                        if (ret == 0) {
                            consumer_created_count++;
                            print_info("Main", "Consumer thread created.");
                        } else {
                            errno = ret; print_error("Main", "pthread_create (consumer) failed");
                            free(args); // Free args if thread creation failed
                        }
                    } else { print_info("Main", "Maximum consumer threads reached."); }
                    break;
                case 'P':
                    if (producer_created_count > 0) {
                        int target_idx = producer_created_count - 1;
                        pthread_t thread_to_cancel = producer_threads[target_idx];
                        // The ID passed to the thread was `target_idx + 1`
                        printf("[Main] Attempting to cancel producer thread (ID %d)...\r\n", target_idx + 1);

                        int cancel_ret = pthread_cancel(thread_to_cancel);
                        if (cancel_ret == 0) {
                            // print_info("Main", "Cancellation request sent to producer thread.");
                            void *join_res;
                            int join_ret = pthread_join(thread_to_cancel, &join_res);
                            if (join_ret == 0) {
                                if (join_res == PTHREAD_CANCELED) {
                                    printf("[Main] Producer thread (ID %d) successfully canceled and joined.\r\n", target_idx + 1);
                                } else {
                                    printf("[Main] Producer thread (ID %d) joined (exited normally, value: %p).\r\n", target_idx + 1, join_res);
                                }
                                producer_created_count--; // Successfully removed
                                // The slot producer_threads[target_idx] can now be reused by a new thread.
                            } else {
                                errno = join_ret;
                                print_error("Main", "pthread_join failed for canceled producer");
                            }
                        } else {
                            errno = cancel_ret;
                            print_error("Main", "pthread_cancel failed for last producer");
                        }
                    } else { print_info("Main", "No active producers to remove."); }
                    break;
                case 'C':
                    if (consumer_created_count > 0) {
                        int target_idx = consumer_created_count - 1;
                        pthread_t thread_to_cancel = consumer_threads[target_idx];
                        printf("[Main] Attempting to cancel consumer thread (ID %d)...\r\n", target_idx + 1);

                        int cancel_ret = pthread_cancel(thread_to_cancel);
                        if (cancel_ret == 0) {
                            // print_info("Main", "Cancellation request sent to consumer thread.");
                            void *join_res;
                            int join_ret = pthread_join(thread_to_cancel, &join_res);
                            if (join_ret == 0) {
                                if (join_res == PTHREAD_CANCELED) {
                                    printf("[Main] Consumer thread (ID %d) successfully canceled and joined.\r\n", target_idx + 1);
                                } else {
                                    printf("[Main] Consumer thread (ID %d) joined (exited normally, value: %p).\r\n", target_idx + 1, join_res);
                                }
                                consumer_created_count--; // Successfully removed
                            } else {
                                errno = join_ret;
                                print_error("Main", "pthread_join failed for canceled consumer");
                            }
                        } else {
                            errno = cancel_ret;
                            print_error("Main", "pthread_cancel failed for last consumer");
                        }
                    } else { print_info("Main", "No active consumers to remove."); }
                    break;
                case '+': queue_resize(g_queue, RESIZE_STEP); break;
                case '-': queue_resize(g_queue, -RESIZE_STEP); break;
                case 's':
                {
                    size_t cap = queue_get_capacity(g_queue);
                    size_t count = queue_get_count(g_queue);
                    unsigned long added = queue_get_added_total(g_queue);
                    unsigned long extracted = queue_get_extracted_total(g_queue);
                    printf("\n--- System Status ---\r\n");
                    printf("Mode:                %s\r\n", g_sync_mode == SYNC_MODE_SEM ? "Semaphores" : "CondVars");
                    printf("Queue Capacity:      %zu\r\n", cap);
                    printf("Queue Occupied:      %zu\r\n", count);
                    printf("Queue Free:          %zu\r\n", cap > count ? cap - count : 0);
                    printf("Total Added:         %lu\r\n", added);
                    printf("Total Extracted:     %lu\r\n", extracted);
                    printf("Active Producers:    %d / %d\r\n", producer_created_count, MAX_PRODUCERS);
                    printf("Active Consumers:    %d / %d\r\n", consumer_created_count, MAX_CONSUMERS);
                    printf("---------------------\r\n");
                    fflush(stdout);
                }
                break;
                case 'q': print_info("Main", "Quit command received..."); g_terminate_flag = 1; break;
                default: printf("[Main] Unknown command: '%c'\r\n", command); break;
            }
            if (!g_terminate_flag) { printf("Enter command: "); fflush(stdout); }
        }

        struct timespec loop_delay = {0, 100000000L}; // 100ms
        if (nanosleep(&loop_delay, NULL) == -1 && errno != EINTR) {
            // Non-critical error, can be ignored or logged lightly
            // print_error("Main", "nanosleep in main loop failed");
        }


    } // end while

    print_info("Main", "Exiting main loop. Cleanup will be handled by atexit.");
    // cleanup_threads() will be called by atexit
    return EXIT_SUCCESS;
}

/*
 * Purpose: Prints command-line usage instructions to stderr.
 * Accepts: prog_name - The name of the executable (argv[0]).
 * Returns: None.
 */
static void print_usage(const char *prog_name) {
    fprintf(stderr, "Usage: %s [-m mode] [-h]\n", prog_name);
    fprintf(stderr, "  -m mode : Synchronization mode ('sem' for semaphores (default), 'cond' for condition variables).\n");
    fprintf(stderr, "            Default is 'sem'.\n");
    fprintf(stderr, "  -h      : Print this help message and exit.\n");
}

/*
 * Purpose: Signal handler for SIGINT and SIGTERM in the main thread.
 *          Sets the global termination flag. Async-signal-safe.
 * Accepts: sig - The signal number received.
 * Returns: None.
 */
static void main_signal_handler(int sig) {
    // This is a signal handler, keep it simple and async-signal-safe.
    // Avoid functions like printf, malloc, etc. Use write().
    if (sig == SIGINT || sig == SIGTERM) {
        g_terminate_flag = 1; // Set the flag for other threads to see
        // Optionally, write a message. ssize_t is used to potentially check write's return.
        const char msg[] = "\n[Main Signal Handler] Termination signal received. Shutting down...\n";
        ssize_t bytes_written __attribute__((unused)) = write(STDERR_FILENO, msg, sizeof(msg) - 1);
        // The __attribute__((unused)) suppresses warnings if bytes_written is not checked.
    }
}

/*
 * Purpose: Registers the signal handler (main_signal_handler) for
 *          SIGINT and SIGTERM signals for the main process. Exits on failure.
 * Accepts: None.
 * Returns: None.
 */
static void register_main_signal_handlers(void) {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa)); // Initialize sa structure
    sa.sa_handler = main_signal_handler;
    if (sigemptyset(&sa.sa_mask) == -1) { // Initialize and empty signal set
        print_error("Signal", "sigemptyset failed");
        exit(EXIT_FAILURE); // Critical failure
    }
    // sa.sa_flags = 0; // No SA_RESTART, syscalls interrupted by this signal will return EINTR.
    // This is often desired for responsiveness.
    // For more robust restart behavior of some syscalls, SA_RESTART could be used,
    // but for this app, EINTR handling in loops is generally preferred.

    if (sigaction(SIGINT, &sa, NULL) == -1) {
        print_error("Signal", "Failed to register SIGINT handler");
        exit(EXIT_FAILURE); // Critical failure
    }
    if (sigaction(SIGTERM, &sa, NULL) == -1) {
        print_error("Signal", "Failed to register SIGTERM handler");
        exit(EXIT_FAILURE); // Critical failure
    }
}

/*
 * Purpose: Cleanup routine registered with atexit. Signals termination to
 *          threads, attempts to unblock them, joins all created threads,
 *          destroys the queue, and restores the terminal.
 * Accepts: None.
 * Returns: None.
 */
static void cleanup_threads(void) {
    print_info("Cleanup", "Starting cleanup via atexit...");
    restore_terminal(); // Restore terminal settings first, important for user visibility
    g_terminate_flag = 1; // Ensure flag is globally set for all threads

    if (g_queue) {
        print_info("Cleanup", "Signaling sync primitives to unblock any waiting threads...");
        if (g_sync_mode == SYNC_MODE_CONDVAR) {
            // Best effort to lock, broadcast, and unlock.
            // Mutex might be in an inconsistent state if program crashed badly,
            // but pthread_cond_broadcast is the correct way to wake waiters.
            int ret_lock = pthread_mutex_trylock(&g_queue->mutex); // Use trylock to avoid deadlock if mutex is held
            if (ret_lock == 0) {
                pthread_cond_broadcast(&g_queue->not_empty);
                pthread_cond_broadcast(&g_queue->not_full);
                pthread_mutex_unlock(&g_queue->mutex);
            } else if (ret_lock == EBUSY) {
                print_error("Cleanup", "Queue mutex busy during cond_broadcast attempt. Threads might not wake immediately.");
            } else {
                errno = ret_lock; print_error("Cleanup", "Failed to trylock queue mutex for cond_broadcast");
            }
        } else { // SYNC_MODE_SEM
            // Post semaphores generously. sem_post is safe to call multiple times.
            // This helps unblock any threads stuck in sem_wait.
            for (int i = 0; i < (MAX_PRODUCERS + MAX_CONSUMERS + 5); ++i) { // A few extra posts
                if (sem_post(&g_queue->empty_slots) == -1 && errno != EINVAL) {
                    // EINVAL means semaphore is already destroyed, which is possible if cleanup runs multiple times
                    // or after an error. Other errors are more problematic.
                    // print_error("Cleanup", "sem_post(empty_slots) failed");
                }
                if (sem_post(&g_queue->full_slots) == -1 && errno != EINVAL) {
                    // print_error("Cleanup", "sem_post(full_slots) failed");
                }
            }
        }
    }

    // Join all *remaining* created threads
    // producer_created_count and consumer_created_count reflect threads
    // that were started and not yet explicitly removed by 'P'/'C'.
    print_info("Cleanup", "Joining remaining producer threads...");
    for (int i = 0; i < producer_created_count; ++i) {
        // It's possible a thread was already joined if 'P' was used.
        // However, pthread_join on an already joined thread ID is undefined behavior.
        // The current logic: 'P'/'C' decrements count, so this loop only hits live ones.
        int ret = pthread_join(producer_threads[i], NULL);
        if (ret != 0) {
            errno = ret;
            fprintf(stderr, "Warning: [Cleanup] Failed to join producer thread (Array Index %d, Orig ID %d): %s\r\n", i, i + 1, strerror(errno));
        } else {
            printf("[Cleanup] Joined producer thread (Array Index %d, Orig ID %d).\r\n", i, i + 1);
            fflush(stdout);
        }
    }
    producer_created_count = 0; // All joined or attempted

    print_info("Cleanup", "Joining remaining consumer threads...");
    for (int i = 0; i < consumer_created_count; ++i) {
        int ret = pthread_join(consumer_threads[i], NULL);
        if (ret != 0) {
            errno = ret;
            fprintf(stderr, "Warning: [Cleanup] Failed to join consumer thread (Array Index %d, Orig ID %d): %s\r\n", i, i + 1, strerror(errno));
        } else {
            printf("[Cleanup] Joined consumer thread (Array Index %d, Orig ID %d).\r\n", i, i + 1);
            fflush(stdout);
        }
    }
    consumer_created_count = 0; // All joined or attempted

    if (g_queue) {
        queue_destroy(g_queue, g_sync_mode);
        g_queue = NULL;
    }

    print_info("Cleanup", "Cleanup complete.");
    fflush(stdout); // Ensure all messages are printed
    fflush(stderr);
}
