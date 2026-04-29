/* Non-blocking keystroke parser.
 *
 * Stdin is in raw, non-blocking mode (term_screen_open). We read into
 * a small ring buffer and pop one logical key at a time. The hard part
 * is escape sequences:
 *   - bare ESC          → TERM_KEY_ESC
 *   - ESC [ A/B/C/D     → arrows
 *   - ESC O A/B/C/D     → arrows in "application cursor keys" mode
 *   - other CSI         → consumed and discarded (safe default)
 *
 * Bare-ESC vs CSI is ambiguous in raw mode without a timer. We use a
 * one-character lookahead: after seeing ESC, if no follow-up byte is
 * available right now we report TERM_KEY_ESC. This loses perfect
 * fidelity if the user is typing very fast, but for game-style input
 * it's the right tradeoff (instant Esc → quit, no delay). */

#include "term.h"

#include <errno.h>
#include <unistd.h>

static int read_byte_nonblocking(unsigned char *out) {
    /* Returns 1 on a byte, 0 if none available, -1 on real error. */
    ssize_t n = read(STDIN_FILENO, out, 1);
    if (n == 1) return 1;
    if (n == 0) return 0;
    if (errno == EAGAIN || errno == EWOULDBLOCK) return 0;
    return -1;
}

static term_key_kind decode_csi_final(unsigned char c) {
    switch (c) {
    case 'A': return TERM_KEY_UP;
    case 'B': return TERM_KEY_DOWN;
    case 'C': return TERM_KEY_RIGHT;
    case 'D': return TERM_KEY_LEFT;
    default:  return TERM_KEY_NONE;     /* unknown CSI; consumed silently */
    }
}

int term_input_poll(term_key *out) {
    out->kind = TERM_KEY_NONE;
    out->ch   = 0;

    unsigned char b;
    int r = read_byte_nonblocking(&b);
    if (r <= 0) return 0;

    if (b == 0x1b) {
        /* Possibly an escape sequence — peek one more byte. */
        unsigned char b2;
        if (read_byte_nonblocking(&b2) <= 0) {
            out->kind = TERM_KEY_ESC;
            return 1;
        }
        if (b2 == '[' || b2 == 'O') {
            /* CSI / SS3. Read parameter bytes (digits + ;) until a
             * final byte in 0x40..0x7e. */
            unsigned char final = 0;
            for (int i = 0; i < 16; i++) {
                unsigned char c;
                if (read_byte_nonblocking(&c) <= 0) break;
                if (c >= 0x40 && c <= 0x7e) { final = c; break; }
            }
            if (final) {
                term_key_kind k = decode_csi_final(final);
                if (k != TERM_KEY_NONE) { out->kind = k; return 1; }
            }
            return 0;     /* unknown sequence; swallowed */
        }
        /* ESC followed by some other byte — treat as plain ESC and
         * discard the second byte (likely Alt+key, which we don't model). */
        out->kind = TERM_KEY_ESC;
        return 1;
    }

    switch (b) {
    case '\r': case '\n':
        out->kind = TERM_KEY_ENTER; return 1;
    case '\t':
        out->kind = TERM_KEY_TAB; return 1;
    case 0x7f: case 0x08:
        out->kind = TERM_KEY_BACKSPACE; return 1;
    default:
        if (b >= 0x20 && b < 0x7f) {
            out->kind = TERM_KEY_CHAR;
            out->ch   = (int)b;
            return 1;
        }
        /* Other control bytes (Ctrl+letter etc.) — pass through as char
         * so apps can match them numerically (e.g. 0x03 = Ctrl+C). */
        out->kind = TERM_KEY_CHAR;
        out->ch   = (int)b;
        return 1;
    }
}
