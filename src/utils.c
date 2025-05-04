#include "common.h"

// --- Static Variables for Terminal Handling ---
static struct termios original_termios;
static int terminal_modified = 0;
static int original_fcntl_flags;

/*
 * Purpose: Prints an error message to stderr, prefixed and including the
 *          current errno value and its string representation.
 * Accepts: prefix - A string prefix (e.g., function name or module).
 *          msg    - The specific error message string.
 * Returns: None.
 */
void print_error(const char *prefix, const char *msg) {
    fprintf(stderr, "ERROR: [%s] %s (errno %d: %s)\r\n", prefix, msg, errno, strerror(errno));
    fflush(stderr);
}

/*
 * Purpose: Prints an informational message to stdout, prefixed.
 * Accepts: prefix - A string prefix (e.g., function name or module).
 *          msg    - The informational message string.
 * Returns: None.
 */
void print_info(const char *prefix, const char *msg) {
    printf("[%s] %s\r\n", prefix, msg);
    fflush(stdout);
}

/*
 * Purpose: Handles errors returned by pthread functions by printing a detailed
 *          error message including the file and line number, and then exiting.
 * Accepts: err_code - The error code returned by the pthread function.
 *          msg      - A descriptive message about the failed operation.
 *          file     - The source file name where the error occurred (__FILE__).
 *          line     - The line number where the error occurred (__LINE__).
 * Returns: None (exits the program).
 */
void handle_pthread_error(int err_code, const char *msg, const char* file, int line) {
    fprintf(stderr, "PTHREAD ERROR: [%s:%d] %s: %s\r\n",
            file, line, msg, strerror(err_code));
    fflush(stderr);
    exit(EXIT_FAILURE);
}

/*
 * Purpose: Configures the terminal connected to standard input for non-blocking,
 *          no-echo input suitable for single-character commands. Exits on failure.
 * Accepts: None.
 * Returns: None.
 */
void setup_terminal_noecho_nonblock(void) {
    if (!isatty(STDIN_FILENO)) {
        print_error("Terminal", "Standard input is not a terminal.");
        return;
    }
    if (tcgetattr(STDIN_FILENO, &original_termios) == -1) {
        print_error("Terminal", "tcgetattr failed");
        exit(EXIT_FAILURE);
    }

    struct termios new_termios = original_termios;
    new_termios.c_lflag &= ~(unsigned long)(ICANON | ECHO);
    new_termios.c_cc[VMIN] = 0;
    new_termios.c_cc[VTIME] = 0;

    if (tcsetattr(STDIN_FILENO, TCSANOW, &new_termios) == -1) {
        print_error("Terminal", "tcsetattr failed");
        exit(EXIT_FAILURE);
    }

    original_fcntl_flags = fcntl(STDIN_FILENO, F_GETFL, 0);
    if (original_fcntl_flags == -1) {
        print_error("Terminal", "fcntl F_GETFL failed");
        tcsetattr(STDIN_FILENO, TCSANOW, &original_termios);
        exit(EXIT_FAILURE);
    }
    if (fcntl(STDIN_FILENO, F_SETFL, original_fcntl_flags | O_NONBLOCK) == -1) {
        print_error("Terminal", "fcntl F_SETFL O_NONBLOCK failed");
        tcsetattr(STDIN_FILENO, TCSANOW, &original_termios);
        exit(EXIT_FAILURE);
    }

    terminal_modified = 1;
}

/*
 * Purpose: Restores the terminal settings to the state they were in before
 *          setup_terminal_noecho_nonblock was called. Does nothing if
 *          setup was not called or stdin is not a TTY.
 * Accepts: None.
 * Returns: None.
 */
void restore_terminal(void) {
    if (terminal_modified && isatty(STDIN_FILENO)) {
        if (fcntl(STDIN_FILENO, F_SETFL, original_fcntl_flags) == -1) {
            fprintf(stderr, "Warning: Failed to restore fcntl flags for stdin.\r\n");
        }
        if (tcsetattr(STDIN_FILENO, TCSANOW, &original_termios) == -1) {
            fprintf(stderr, "Warning: Failed to restore terminal attributes.\r\n");
        }
        printf("\r\n");
        fflush(stdout);
        terminal_modified = 0;
    }
}

/*
 * Purpose: Checks if a key has been pressed on standard input without blocking.
 * Accepts: None.
 * Returns: 1 if a key is available to be read, 0 otherwise.
 */
int kbhit(void) {
    if (!terminal_modified) return 0;
    char c;
    ssize_t n = read(STDIN_FILENO, &c, 1);
    if (n == 1) {
        if (ungetc(c, stdin) == EOF) { print_error("kbhit", "ungetc failed"); return 0; }
        return 1;
    } else if (n == 0) { return 0;
    } else {
        if (errno == EAGAIN || errno == EWOULDBLOCK) { return 0; }
        else { print_error("kbhit", "read failed"); return 0; }
    }
}

/*
 * Purpose: Calculates a simple hash value for a given message structure based
 *          on its type, size, and data content.
 * Accepts: msg - A pointer to the constant message_t structure.
 * Returns: The calculated 16-bit hash value.
 */
unsigned short calculate_message_hash(const message_t *msg) {
    unsigned short hash = 0;
    const unsigned char *byte_ptr;
    size_t i;
    hash = (hash << 5) + hash + msg->type;
    hash = (hash << 5) + hash + msg->size;
    byte_ptr = msg->data;
    for (i = 0; i < msg->size; ++i) {
        hash = (hash << 5) + hash + byte_ptr[i];
    }
    return hash;
}
