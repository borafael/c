/* Terminal lifecycle:
 *   - termios switched to raw, no-echo, non-blocking on stdin
 *   - alternate screen buffer entered (so we don't trash scrollback)
 *   - cursor hidden
 *   - SIGWINCH handler installs a flag (no I/O in the handler)
 *
 * Symmetric teardown reverses every step. We keep the original termios
 * and stdin flags so quitting the app (or crashing through atexit) lands
 * the user back in a sane shell. */

#include "term.h"

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

#define ESC_ALT_SCREEN_ENTER  "\x1b[?1049h"
#define ESC_ALT_SCREEN_LEAVE  "\x1b[?1049l"
#define ESC_CURSOR_HIDE       "\x1b[?25l"
#define ESC_CURSOR_SHOW       "\x1b[?25h"
#define ESC_CLEAR_SCREEN      "\x1b[2J"
#define ESC_CURSOR_HOME       "\x1b[H"
#define ESC_RESET_ATTR        "\x1b[0m"

struct term_screen {
    struct termios saved_tio;
    int            tio_saved;
    struct sigaction saved_sigwinch;
    int              sigaction_saved;
};

/* Module-level so the signal handler can flip it; reset by
 * term_screen_consume_resize. The screen pointer used by the handler is
 * stored here too so we don't need a sigqueue payload. */
static volatile sig_atomic_t g_winch_flag = 0;

static void winch_handler(int signo) {
    (void)signo;
    g_winch_flag = 1;
}

static int write_all(int fd, const char *s, size_t n) {
    while (n) {
        ssize_t w = write(fd, s, n);
        if (w < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        s += w;
        n -= (size_t)w;
    }
    return 0;
}

static void write_str(int fd, const char *s) {
    write_all(fd, s, strlen(s));
}

term_screen *term_screen_open(void) {
    if (!isatty(STDIN_FILENO) || !isatty(STDOUT_FILENO)) {
        /* Refuse to mangle a non-tty. Pipes/CI logs would just see noise. */
        return NULL;
    }

    term_screen *s = calloc(1, sizeof(*s));
    if (!s) return NULL;

    if (tcgetattr(STDIN_FILENO, &s->saved_tio) != 0) goto fail;
    s->tio_saved = 1;

    struct termios raw = s->saved_tio;
    /* cfmakeraw equivalent, but keeping ONLCR off is enough — we still
     * want isig OFF so Ctrl+C arrives as a byte the app can choose to
     * handle. Apps that prefer the kernel to handle SIGINT can override. */
    raw.c_lflag &= ~(ICANON | ECHO | ISIG | IEXTEN);
    raw.c_iflag &= ~(IXON | ICRNL | INPCK | ISTRIP | BRKINT);
    raw.c_oflag &= ~(OPOST);
    raw.c_cflag |= CS8;
    /* VMIN=0/VTIME=0 makes read() return 0 immediately when no input
     * is queued — equivalent to non-blocking behaviour for stdin
     * without needing O_NONBLOCK on the fd. We deliberately avoid
     * O_NONBLOCK because under a pty (script, ssh, tmux) stdin and
     * stdout often share a file-table entry, so flipping O_NONBLOCK on
     * stdin also makes write() to stdout fail with EAGAIN — which silently
     * truncates any frame larger than the pty buffer. */
    raw.c_cc[VMIN]  = 0;
    raw.c_cc[VTIME] = 0;
    if (tcsetattr(STDIN_FILENO, TCSANOW, &raw) != 0) goto fail;

    struct sigaction sa = {0};
    sa.sa_handler = winch_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    if (sigaction(SIGWINCH, &sa, &s->saved_sigwinch) != 0) goto fail;
    s->sigaction_saved = 1;
    g_winch_flag = 0;

    write_str(STDOUT_FILENO, ESC_ALT_SCREEN_ENTER);
    write_str(STDOUT_FILENO, ESC_CURSOR_HIDE);
    write_str(STDOUT_FILENO, ESC_RESET_ATTR);
    write_str(STDOUT_FILENO, ESC_CLEAR_SCREEN);
    write_str(STDOUT_FILENO, ESC_CURSOR_HOME);
    return s;

fail:
    term_screen_close(s);
    return NULL;
}

void term_screen_close(term_screen *s) {
    if (!s) return;
    /* Reverse order: cursor + alt screen first (terminal-side), then
     * sigaction, then stdin flags, then termios. */
    write_str(STDOUT_FILENO, ESC_RESET_ATTR);
    write_str(STDOUT_FILENO, ESC_CURSOR_SHOW);
    write_str(STDOUT_FILENO, ESC_ALT_SCREEN_LEAVE);

    if (s->sigaction_saved) {
        sigaction(SIGWINCH, &s->saved_sigwinch, NULL);
    }
    if (s->tio_saved) {
        tcsetattr(STDIN_FILENO, TCSANOW, &s->saved_tio);
    }
    free(s);
}

void term_screen_size(int *cols, int *rows) {
    struct winsize w = {0};
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &w) == 0 && w.ws_col && w.ws_row) {
        *cols = w.ws_col;
        *rows = w.ws_row;
    } else {
        *cols = 80;
        *rows = 24;
    }
}

int term_screen_consume_resize(term_screen *s) {
    (void)s;
    if (g_winch_flag) {
        g_winch_flag = 0;
        return 1;
    }
    return 0;
}

void term_screen_present(term_screen *s, const char *buf, size_t len) {
    (void)s;
    /* CUP-home before the body so the renderer's cell positions land at
     * the right place even if some other code (or a stray printf) moved
     * the cursor between frames. */
    write_str(STDOUT_FILENO, ESC_CURSOR_HOME);
    write_all(STDOUT_FILENO, buf, len);
}
