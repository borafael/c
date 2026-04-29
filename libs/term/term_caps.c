/* Capability detection: examine env vars and pick the best mode the
 * current terminal seems to handle. Heuristics, not guarantees — apps
 * should expose a --mode override (see term_caps_parse_*). */

#include "term.h"

#include <stdlib.h>
#include <string.h>
#include <strings.h>

static int env_contains(const char *name, const char *needle) {
    const char *v = getenv(name);
    if (!v || !*v) return 0;
    /* Case-insensitive substring search; needle is a static lowercase
     * literal so casefold one side and we're done. */
    size_t nl = strlen(needle);
    for (const char *p = v; *p; p++) {
        if (strncasecmp(p, needle, nl) == 0) return 1;
    }
    return 0;
}

static int env_equals(const char *name, const char *value) {
    const char *v = getenv(name);
    return v && strcasecmp(v, value) == 0;
}

static int detect_utf8(void) {
    /* Locale env vars in priority order per POSIX: LC_ALL > LC_CTYPE > LANG.
     * Any of them containing "UTF-8"/"utf8" means the terminal can render
     * the upper-half block. */
    if (env_contains("LC_ALL",   "utf-8")) return 1;
    if (env_contains("LC_ALL",   "utf8"))  return 1;
    if (env_contains("LC_CTYPE", "utf-8")) return 1;
    if (env_contains("LC_CTYPE", "utf8"))  return 1;
    if (env_contains("LANG",     "utf-8")) return 1;
    if (env_contains("LANG",     "utf8"))  return 1;
    return 0;
}

static term_color_mode detect_color(void) {
    /* COLORTERM is the de-facto truecolor signal. */
    if (env_equals("COLORTERM", "truecolor") ||
        env_equals("COLORTERM", "24bit")) {
        return TERM_COLOR_TRUECOLOR;
    }
    /* TERM containing "256color" or "direct" implies 256-colour at minimum;
     * "direct" hints at truecolor terminfo entries (xterm-direct etc.). */
    if (env_contains("TERM", "direct")) return TERM_COLOR_TRUECOLOR;
    if (env_contains("TERM", "256color")) return TERM_COLOR_PALETTE256;
    /* xterm/screen/tmux/linux without a 256 suffix → assume 16-colour terminal,
     * which means we can't safely send any modern SGR — fall back to mono. */
    const char *term = getenv("TERM");
    if (!term || !*term || strcmp(term, "dumb") == 0) return TERM_COLOR_MONO;
    return TERM_COLOR_MONO;
}

term_caps term_caps_detect(void) {
    term_caps c;
    c.glyph = detect_utf8() ? TERM_GLYPH_HALFBLOCK : TERM_GLYPH_ASCII;
    c.color = detect_color();
    return c;
}

const char *term_caps_glyph_name(term_glyph_mode m) {
    switch (m) {
    case TERM_GLYPH_HALFBLOCK: return "halfblock";
    case TERM_GLYPH_ASCII:     return "ascii";
    }
    return "?";
}

const char *term_caps_color_name(term_color_mode m) {
    switch (m) {
    case TERM_COLOR_TRUECOLOR:  return "truecolor";
    case TERM_COLOR_PALETTE256: return "256";
    case TERM_COLOR_MONO:       return "mono";
    }
    return "?";
}

int term_caps_parse_glyph(const char *s, term_glyph_mode *out) {
    if (!s) return 0;
    if (strcasecmp(s, "auto") == 0) return 1;     /* keep caller default */
    if (strcasecmp(s, "halfblock") == 0 || strcasecmp(s, "block") == 0) {
        *out = TERM_GLYPH_HALFBLOCK; return 1;
    }
    if (strcasecmp(s, "ascii") == 0 || strcasecmp(s, "text") == 0) {
        *out = TERM_GLYPH_ASCII; return 1;
    }
    return 0;
}

int term_caps_parse_color(const char *s, term_color_mode *out) {
    if (!s) return 0;
    if (strcasecmp(s, "auto") == 0) return 1;
    if (strcasecmp(s, "truecolor") == 0 || strcasecmp(s, "24bit") == 0 ||
        strcasecmp(s, "rgb") == 0) {
        *out = TERM_COLOR_TRUECOLOR; return 1;
    }
    if (strcasecmp(s, "256") == 0 || strcasecmp(s, "palette") == 0 ||
        strcasecmp(s, "8bit") == 0) {
        *out = TERM_COLOR_PALETTE256; return 1;
    }
    if (strcasecmp(s, "mono") == 0 || strcasecmp(s, "none") == 0) {
        *out = TERM_COLOR_MONO; return 1;
    }
    return 0;
}
