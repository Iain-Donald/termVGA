#include <stdio.h>
#include <stdint.h>
#include <signal.h>
#include <unistd.h>
#include <termios.h>
#include <errno.h>
#include <string.h>

typedef enum : int8_t {
    S_OK = 0,
    S_ERROR = 1,
    S_TCGETATTR = 10,
    S_TCSETATTR
} status;

typedef struct {
    uint8_t id;
    uint16_t h, v;
    uint8_t r, g, b;
} screen;

static const char seq_enter[] = "\033[?1049h\033[2J\033[H\033[?25l"; // alt buffer on, clear, cursor home, hide cursor.
static const char seq_leave[] = "\033[?25h\033[?1049l"; // show cursor, alt buffer off.
static struct termios saved_termios; // original terminal settings, restored on exit.
static volatile sig_atomic_t got_signal = 0; // set by on_signal, checked by the input loop.

static void on_signal(int sig) {
    (void)sig;
    got_signal = 1;
}

static status restore_terminal(void) {
    write(STDOUT_FILENO, seq_leave, sizeof(seq_leave) - 1); // leave alt buffer before restoring termios.
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &saved_termios); // back to original echo and line buffering.
    return S_OK;
}

status printError(status termvga_rc) {
    int saved_errno = errno; // grab before any other libc call.
    fprintf(stderr, "termVGA error: ");
    switch (termvga_rc) {
        case S_TCGETATTR:
            fprintf(stderr, "tcgetattr: %s\n", strerror(saved_errno));
            break;
        case S_TCSETATTR:
            fprintf(stderr, "tcsetattr: %s\n", strerror(saved_errno));
            break;
        default:
            fprintf(stderr, "unspecified.\n");
            break;
    }
    return S_OK;
}

status screen_connect(screen *s) {
    (void)s; // screen values unused for now.
    // get term attributes.
    // status rc = S_OK;
    int rc_libc = tcgetattr(STDIN_FILENO, &saved_termios);
    if (rc_libc == -1) return S_TCGETATTR; // failed to get attrs.

    struct termios raw = saved_termios;
    raw.c_lflag &= ~(ECHO | ICANON); // no echo, no line buffering (ISIG kept so Ctrl-C still works).
    raw.c_cc[VMIN] = 1; // block until 1 byte is available.
    raw.c_cc[VTIME] = 0; // no read timeout.
    signal(SIGINT, on_signal); // Ctrl-C exits the loop cleanly.
    signal(SIGTERM, on_signal); // kill exits the loop cleanly.
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) return S_TCSETATTR;

    write(STDOUT_FILENO, seq_enter, sizeof(seq_enter) - 1);  // blank "window" on the alt buffer.

    while (!got_signal) {
        char c;
        ssize_t n = read(STDIN_FILENO, &c, 1); // -1 when interrupted by a signal, loop condition handles it.
        if (n == 1 && c == 'q') break; // q quit.
        if (n == 0) break; // stdin closed.
    }
    restore_terminal();
    return S_OK;
}

int main(void) {
    screen s = { .id = 0, .r = 0, .g = 0, .b = 0, .h = 40, .v = 40 };
    status rc = screen_connect(&s);
    if (rc != S_OK) {
        printError(rc);
        return rc;
    }
    printf("termVGA clean exit.\n");
    return S_OK;
}
