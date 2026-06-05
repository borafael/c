#ifndef RLYEH_WHISPER_H
#define RLYEH_WHISPER_H

#include <stdint.h>

/* Whispers — faint Lovecraftian text that fades in over the framebuffer at
 * long random intervals. Bottom-centred, drawn with a tiny embedded 5x7 font;
 * ~80% intelligible English phrases, ~20% random glyph noise so the brain
 * still tries to parse alien strings. A pure overlay: it reads no scene state,
 * only the elapsed time the harness feeds it, and composites onto a finished
 * ARGB frame. The state struct is exposed so the caller can hold one by value;
 * all the font/timing constants stay private to whisper.c. */

#define WHISPER_MAX_LEN 32

typedef enum {
    WHISPER_IDLE,
    WHISPER_FADE_IN,
    WHISPER_HOLD,
    WHISPER_FADE_OUT,
} whisper_phase;

typedef struct {
    whisper_phase phase;
    float    phase_t;     /* elapsed in current phase */
    float    next_gap;    /* idle duration before next whisper fires */
    char     text[WHISPER_MAX_LEN + 1];
    int      text_len;
    int      is_noise;    /* 1 = random glyph stream, 0 = English phrase */
    uint32_t noise_seed;  /* stable across the whole whisper lifetime */
} whisper_state;

/* Arm a fresh whisper state in the idle phase. first_delay_sec is extra idle
 * time before the first whisper may fire, on top of the normal minimum gap —
 * pass the wake-fade duration so whispers hold off until you've "woken". */
void whisper_init(whisper_state *w, float first_delay_sec);

/* Advance the state machine by dt seconds. now_ms seeds phrase/noise choice. */
void whisper_update(whisper_state *w, float dt, uint32_t now_ms);

/* Composite the current whisper (if any) onto a W x H ARGB framebuffer. */
void whisper_render(const whisper_state *w, uint32_t *pixels, int W, int H);

#endif /* RLYEH_WHISPER_H */
