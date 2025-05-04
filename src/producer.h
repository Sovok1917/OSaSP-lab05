#ifndef PRODUCER_H
#define PRODUCER_H

#include "common.h"

// --- Function Declarations ---

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
void* producer_thread_func(void *arg);

#endif // PRODUCER_H
