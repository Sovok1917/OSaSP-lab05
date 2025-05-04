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
static int producer_created_count = 0;
static int producer_active_count = 0;

static pthread_t consumer_threads[MAX_CONSUMERS];
static int consumer_created_count = 0;
static int consumer_active_count = 0;

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

    // Parse Command Line Options
    while ((opt = getopt(argc, argv, "m:h")) != -1) {
        switch (opt) {
            case 'm': mode_str = optarg; break;
            case 'h': print_usage(argv[0]); return EXIT_SUCCESS;
            default: print_usage(argv[0]); return EXIT_FAILURE;
        }
    }

    // Determine synchronization mode
    if (strcmp(mode_str, "sem") == 0) { g_sync_mode = SYNC_MODE_SEM; print_info("Main", "Using POSIX Semaphores."); }
    else if (strcmp(mode_str, "cond") == 0) { g_sync_mode = SYNC_MODE_CONDVAR; print_info("Main", "Using Condition Variables."); }
    else { fprintf(stderr, "Error: Invalid mode '%s'.\n", mode_str); print_usage(argv[0]); return EXIT_FAILURE; }

    srand((unsigned int)time(NULL) ^ (unsigned int)getpid());
    print_info("Main", "Initializing system...");
    setup_terminal_noecho_nonblock();

    // Create queue
    g_queue = queue_create(INITIAL_QUEUE_CAPACITY, g_sync_mode);
    if (!g_queue) { restore_terminal(); return EXIT_FAILURE; }

    register_main_signal_handlers();
    if (atexit(cleanup_threads) != 0) { print_error("Main", "atexit failed"); queue_destroy(g_queue, g_sync_mode); restore_terminal(); return EXIT_FAILURE; }

    // Print usage instructions
    printf("\r\n--- Producer/Consumer Control (Mode: %s) ---\r\n", mode_str);
    printf("  p: Add Producer   c: Add Consumer\r\n");
    printf("  P: Remove Last Producer (stops adding)   C: Remove Last Consumer (stops adding)\r\n");
    printf("  +: Increase Queue Capacity   -: Decrease Queue Capacity\r\n");
    printf("  s: Show Status    q: Quit\r\n");
    printf("--------------------------------------------------\r\n");
    printf("Enter command: ");
    fflush(stdout);

    char command = 0;
    int ret;

    // Main command loop
    while (!g_terminate_flag) {
        if (kbhit()) {
            command = (char)getchar();
            printf("\r\n");

            switch (command) {
                case 'p':
                    if (producer_active_count < MAX_PRODUCERS) {
                        thread_args_t *args = malloc(sizeof(thread_args_t));
                        if (!args) { print_error("Main", "Failed to allocate args"); break; }
                        args->id = producer_created_count + 1;
                        args->queue = g_queue;
                        args->sync_mode = g_sync_mode;
                        ret = pthread_create(&producer_threads[producer_created_count], NULL, producer_thread_func, args);
                        if (ret == 0) { producer_created_count++; producer_active_count++; print_info("Main", "Producer thread created."); }
                        else { errno = ret; print_error("Main", "pthread_create (producer) failed"); free(args); }
                    } else { print_info("Main", "Maximum producer threads reached."); }
                    break;
                case 'c':
                    if (consumer_active_count < MAX_CONSUMERS) {
                        thread_args_t *args = malloc(sizeof(thread_args_t));
                        if (!args) { print_error("Main", "Failed to allocate args"); break; }
                        args->id = consumer_created_count + 1;
                        args->queue = g_queue;
                        args->sync_mode = g_sync_mode;
                        ret = pthread_create(&consumer_threads[consumer_created_count], NULL, consumer_thread_func, args);
                        if (ret == 0) { consumer_created_count++; consumer_active_count++; print_info("Main", "Consumer thread created."); }
                        else { errno = ret; print_error("Main", "pthread_create (consumer) failed"); free(args); }
                    } else { print_info("Main", "Maximum consumer threads reached."); }
                    break;
                case 'P':
                    if (producer_active_count > 0) {
                        producer_active_count--;
                        printf("[Main] Stopped adding producer %d. Thread continues until quit.\r\n", producer_active_count + 1);
                    } else { print_info("Main", "No active producers to stop adding."); }
                    break;
                case 'C':
                    if (consumer_active_count > 0) {
                        consumer_active_count--;
                        printf("[Main] Stopped adding consumer %d. Thread continues until quit.\r\n", consumer_active_count + 1);
                    } else { print_info("Main", "No active consumers to stop adding."); }
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
                    printf("Mode:           %s\r\n", g_sync_mode == SYNC_MODE_SEM ? "Semaphores" : "CondVars");
                    printf("Queue Capacity: %zu\r\n", cap);
                    printf("Queue Occupied: %zu\r\n", count);
                    printf("Queue Free:     %zu\r\n", cap > count ? cap - count : 0);
                    printf("Total Added:    %lu\r\n", added);
                    printf("Total Extracted:%lu\r\n", extracted);
                    printf("Producers:      %d / %d (Created: %d)\r\n", producer_active_count, MAX_PRODUCERS, producer_created_count);
                    printf("Consumers:      %d / %d (Created: %d)\r\n", consumer_active_count, MAX_CONSUMERS, consumer_created_count);
                    printf("---------------------\r\n");
                    fflush(stdout);
                }
                break;
                case 'q': print_info("Main", "Quit command received..."); g_terminate_flag = 1; break;
                default: printf("[Main] Unknown command: '%c'\r\n", command); break;
            }
            if (!g_terminate_flag) { printf("Enter command: "); fflush(stdout); }
        }

        // Pause briefly
        struct timespec loop_delay = {0, 100000000L}; // 100ms
        nanosleep(&loop_delay, NULL);

    } // end while

    print_info("Main", "Exiting.");
    return EXIT_SUCCESS;
}

/*
 * Purpose: Prints command-line usage instructions to stderr.
 * Accepts: prog_name - The name of the executable (argv[0]).
 * Returns: None.
 */
static void print_usage(const char *prog_name) {
    fprintf(stderr, "Usage: %s [-m mode]\n", prog_name);
    fprintf(stderr, "  -m mode : Synchronization mode ('sem' for semaphores (default), 'cond' for condition variables)\n");
    fprintf(stderr, "  -h      : Print this help message\n");
}

/*
 * Purpose: Signal handler for SIGINT and SIGTERM in the main thread.
 *          Sets the global termination flag. Async-signal-safe.
 * Accepts: sig - The signal number received.
 * Returns: None.
 */
static void main_signal_handler(int sig) {
    if (sig == SIGINT || sig == SIGTERM) {
        g_terminate_flag = 1;
        const char msg[] = "\n[Main] Termination signal received. Shutting down...\n";
        // Use write for safety in signal handler
        ssize_t written __attribute__((unused)) = write(STDERR_FILENO, msg, sizeof(msg) - 1);
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
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = main_signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0; // No SA_RESTART
    if (sigaction(SIGINT, &sa, NULL) == -1 || sigaction(SIGTERM, &sa, NULL) == -1) {
        print_error("Signal", "Failed to register main signal handlers"); exit(EXIT_FAILURE);
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
    print_info("Cleanup", "Starting cleanup...");
    restore_terminal();
    g_terminate_flag = 1; // Ensure flag is set

    // Wake up potentially blocked threads
    if (g_queue) {
        print_info("Cleanup", "Signaling sync primitives to unblock threads...");
        if (g_sync_mode == SYNC_MODE_SEM) {
            // Post semaphores generously
            for (int i = 0; i < MAX_PRODUCERS + MAX_CONSUMERS + 1; ++i) {
                // Ignore errors here, sem might be destroyed already if cleanup runs twice
                sem_post(&g_queue->empty_slots);
                sem_post(&g_queue->full_slots);
            }
        } else { // SYNC_MODE_CONDVAR
            // Broadcast conditions
            pthread_mutex_lock(&g_queue->mutex); // Lock needed for broadcast
            pthread_cond_broadcast(&g_queue->not_empty);
            pthread_cond_broadcast(&g_queue->not_full);
            pthread_mutex_unlock(&g_queue->mutex);
        }
    }

    // Join all created threads
    print_info("Cleanup", "Joining producer threads...");
    for (int i = 0; i < producer_created_count; ++i) {
        int ret = pthread_join(producer_threads[i], NULL);
        if (ret != 0) { errno = ret; fprintf(stderr, "Warning: Failed to join producer thread %d: %s\r\n", i + 1, strerror(errno)); }
        else { printf("[Cleanup] Joined producer thread %d.\r\n", i + 1); fflush(stdout); }
    }
    producer_created_count = 0;
    producer_active_count = 0;

    print_info("Cleanup", "Joining consumer threads...");
    for (int i = 0; i < consumer_created_count; ++i) {
        int ret = pthread_join(consumer_threads[i], NULL);
        if (ret != 0) { errno = ret; fprintf(stderr, "Warning: Failed to join consumer thread %d: %s\r\n", i + 1, strerror(errno)); }
        else { printf("[Cleanup] Joined consumer thread %d.\r\n", i + 1); fflush(stdout); }
    }
    consumer_created_count = 0;
    consumer_active_count = 0;

    // Destroy the queue
    if (g_queue) {
        queue_destroy(g_queue, g_sync_mode);
        g_queue = NULL;
    }

    print_info("Cleanup", "Cleanup complete.");
}
