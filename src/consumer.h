#ifndef CONSUMER_H
#define CONSUMER_H

#include "common.h"

// --- Function Declarations ---

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
void* consumer_thread_func(void *arg);

#endif // CONSUMER_H
