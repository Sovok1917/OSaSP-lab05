Producer-Consumer Demo with POSIX Threads
=========================================

This program demonstrates the producer-consumer problem using POSIX threads (pthreads)
for concurrency and POSIX synchronization primitives. It supports two distinct
synchronization modes:
1.  POSIX Semaphores (`sem_t`)
2.  POSIX Mutexes (`pthread_mutex_t`) and Condition Variables (`pthread_cond_t`)

The main thread manages user commands to dynamically create producer and consumer
threads, which interact via a shared, bounded message queue. The queue size can
also be adjusted dynamically.

Program Components:
-------------------
1.  main (prod_cons_threads): The main control program. It parses command-line
    options to select the synchronization mode (semaphores or condition variables).
    It initializes the shared queue and synchronization primitives, sets up the
    terminal for non-blocking input, and then allows the user to:
    - Create new producer threads ('p').
    - Create new consumer threads ('c').
    - Indicate stopping the creation of new producers ('P') or consumers ('C').
      (Note: This stops *adding* new threads; existing threads continue until quit).
    - Increase queue capacity ('+').
    - Decrease queue capacity ('-').
    - Display the current status of the queue and threads ('s').
    - Quit the application ('q'), which signals all threads to terminate,
      joins them, and cleans up resources.

2.  producer (producer_thread_func): A thread function that generates messages
    (with a type, size, data, and hash) and adds them to the shared queue.
    It uses the selected synchronization mechanism (semaphores or condvars)
    to wait if the queue is full and to protect access to the queue.

3.  consumer (consumer_thread_func): A thread function that retrieves messages
    from the shared queue. It uses the selected synchronization mechanism to wait
    if the queue is empty and to protect access. It then verifies the hash of
    the received message.

4.  queue_manager: Module responsible for managing the shared queue. This includes
    creation, destruction, adding messages, removing messages, and resizing the queue.
    It internally handles the logic for both semaphore-based and
    condition variable-based synchronization.

5.  utils: Utility functions for terminal manipulation (non-blocking, no-echo input),
    message hash calculation, printing formatted info/error messages, and a helper
    for checking pthread function return codes.

Build Instructions:
-------------------
The project uses a Makefile for building. Source code is expected in the src/ directory,
and build artifacts will be placed in the build/ directory (in build/debug or
build/release subdirectories).

1.  Build Debug Version (Default):
    make
    or
    make debug-build
    Executable: build/debug/prod_cons_threads

2.  Build Release Version:
    (Treats warnings as errors and applies optimizations)
    make release-build
    Executable: build/release/prod_cons_threads

3.  Clean Build Artifacts:
    make clean
    This removes the entire build/ directory.

4.  Show Help:
    make help
    This displays available make targets and their descriptions.

Running the Program:
--------------------
The program accepts an optional command-line argument to specify the synchronization mode.

1.  Run with Semaphores (Default):
    make run
    or
    make run-sem
    (Equivalent to: ./build/debug/prod_cons_threads -m sem)

2.  Run with Condition Variables:
    make run-cond
    (Equivalent to: ./build/debug/prod_cons_threads -m cond)

3.  Run Release Version (Default: Semaphores):
    make run-release
    (Equivalent to: ./build/release/prod_cons_threads -m sem)

4.  Run Release Version with Semaphores:
    make run-release-sem
    (Equivalent to: ./build/release/prod_cons_threads -m sem)

5.  Run Release Version with Condition Variables:
    make run-release-cond
    (Equivalent to: ./build/release/prod_cons_threads -m cond)

Manual Execution (Example):
# Assuming you built the debug version
./build/debug/prod_cons_threads -m sem  # For semaphores
./build/debug/prod_cons_threads -m cond # For condition variables

Command-Line Options:
---------------------
  -m mode : Synchronization mode.
            'sem' for POSIX Semaphores (default if -m is omitted).
            'cond' for POSIX Mutexes and Condition Variables.
  -h      : Print help message and exit.

Program Commands (Input single characters):
-------------------------------------------
Once the program is running, it will display a menu and accept the following
single-character commands:

*   p : Add a new Producer thread.
*   c : Add a new Consumer thread.
*   P : Stop *adding* new Producer threads. Existing producers continue to run.
*   C : Stop *adding* new Consumer threads. Existing consumers continue to run.
*   + : Increase the shared queue's capacity.
*   - : Decrease the shared queue's capacity (cannot shrink below current item count
        or minimum capacity).
*   s : Show current status (sync mode, queue details, number of active/created threads).
*   q : Quit the application. This will signal all threads to terminate, wait for them
        to join, and then clean up resources.

Thread and Queue Behavior:
--------------------------
-   Producers generate messages with random data, calculate a hash, and attempt to
    add them to the shared queue. They print a status message upon adding.
-   Consumers attempt to retrieve messages from the queue, recalculate the hash of the
    message data, and compare it with the original hash. They print a status message
    including hash verification (OK/FAIL).
-   Both producers and consumers introduce random delays to simulate work.
-   Threads are designed to check a global termination flag and exit gracefully when
    the main program initiates a shutdown (via 'q' command or SIGINT/SIGTERM).
-   The queue resizing operation is also synchronized.

Resource Cleanup:
-----------------
-   The program attempts to clean up all allocated resources (queue memory, mutexes,
    semaphores, condition variables) upon normal termination ('q' command) or via an
    atexit handler if the main process exits unexpectedly.
-   All created threads are joined during cleanup to ensure they complete their
    final operations.

Notes:
------
-   The program uses non-blocking, no-echo terminal input for interactive commands.
-   Error messages are typically printed to stderr, while informational messages and
    producer/consumer outputs are printed to stdout.
-   The program demonstrates signal handling in the main thread for graceful shutdown.
