#ifndef COMMON_H
#define COMMON_H

#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <pthread.h>
#include <semaphore.h>
#include <termios.h>
#include <fcntl.h>
#include <stdbool.h>
#include <time.h>
#include <errno.h>
#include <stdint.h>

// --- Constants ---
#define INITIAL_QUEUE_CAPACITY 10
#define MIN_QUEUE_CAPACITY 1
#define MAX_QUEUE_CAPACITY 100
#define MAX_DATA_SIZE 256
#define MAX_PRODUCERS 10
#define MAX_CONSUMERS 10
#define RESIZE_STEP 1 // Adjust queue size by 1

// --- Synchronization Mode ---
typedef enum {
    SYNC_MODE_SEM,
    SYNC_MODE_CONDVAR
} sync_mode_t;

// --- Message Structure ---
typedef struct message_s {
    unsigned char type;
    unsigned short hash;
    unsigned char size;
    unsigned char data[MAX_DATA_SIZE];
} message_t;

// --- Shared Queue Structure ---
typedef struct queue_s {
    message_t *messages;
    size_t capacity;
    size_t count;
    int head_idx;
    int tail_idx;
    pthread_mutex_t mutex;
    // For SYNC_MODE_SEM
    sem_t empty_slots;
    sem_t full_slots;
    // For SYNC_MODE_CONDVAR
    pthread_cond_t not_empty;
    pthread_cond_t not_full;
    // Stats
    unsigned long added_count_total;
    unsigned long extracted_count_total;
} queue_t;

// --- Thread Argument Structure ---
typedef struct thread_args_s {
    int id;
    queue_t *queue;
    sync_mode_t sync_mode;
} thread_args_t;

// --- Global Flags ---
extern volatile sig_atomic_t g_terminate_flag;
extern sync_mode_t g_sync_mode;

// --- Utility Function Declarations ---

/*
 * Purpose: Checks if a key has been pressed on standard input without blocking.
 * Accepts: None.
 * Returns: 1 if a key is available to be read, 0 otherwise.
 */
int kbhit(void);

/*
 * Purpose: Restores the terminal settings to the state they were in before
 *          setup_terminal_noecho_nonblock was called. Does nothing if
 *          setup was not called or stdin is not a TTY.
 * Accepts: None.
 * Returns: None.
 */
void restore_terminal(void);

/*
 * Purpose: Configures the terminal connected to standard input for non-blocking,
 *          no-echo input suitable for single-character commands. Exits on failure.
 * Accepts: None.
 * Returns: None.
 */
void setup_terminal_noecho_nonblock(void);

/*
 * Purpose: Calculates a simple hash value for a given message structure based
 *          on its type, size, and data content.
 * Accepts: msg - A pointer to the constant message_t structure.
 * Returns: The calculated 16-bit hash value.
 */
unsigned short calculate_message_hash(const message_t *msg);

/*
 * Purpose: Prints an error message to stderr, prefixed and including the
 *          current errno value and its string representation.
 * Accepts: prefix - A string prefix (e.g., function name or module).
 *          msg    - The specific error message string.
 * Returns: None.
 */
void print_error(const char *prefix, const char *msg);

/*
 * Purpose: Prints an informational message to stdout, prefixed.
 * Accepts: prefix - A string prefix (e.g., function name or module).
 *          msg    - The informational message string.
 * Returns: None.
 */
void print_info(const char *prefix, const char *msg);

/*
 * Purpose: Handles errors returned by pthread functions by printing a detailed
 *          error message including the file and line number, and then exiting.
 * Accepts: err_code - The error code returned by the pthread function.
 *          msg      - A descriptive message about the failed operation.
 *          file     - The source file name where the error occurred (__FILE__).
 *          line     - The line number where the error occurred (__LINE__).
 * Returns: None (exits the program).
 */
void handle_pthread_error(int err_code, const char *msg, const char* file, int line);

// Macro to simplify pthread error checking
#define PTHREAD_CHECK(err, msg) \
do { if ((err) != 0) handle_pthread_error(err, msg, __FILE__, __LINE__); } while (0)

    #endif // COMMON_H
