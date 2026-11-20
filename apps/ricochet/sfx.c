#include "sfx.h"

#include <SDL2/SDL.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define SR 44100
#define TAU (2.0f * (float)M_PI)

static float *g_buf[SFX_COUNT];
static int    g_len[SFX_COUNT];
static int    g_counts[SFX_COUNT];

static const char *NAMES[SFX_COUNT] = {
    "shoot", "hit", "bounce", "wall", "pickup", "pop", "hurt", "dry",
};

typedef struct {
    int   active;
    int   id;
    float pos;      /* fractional sample cursor */
    float rate;
    float gl, gr;
} voice;

static voice             g_voices[SFX_MAX_VOICES];
static SDL_AudioDeviceID g_dev;
static int               g_out_rate = SR;

/* ------------------------------------------------------------ synthesis */

static uint32_t noise_state = 0x1234567u;
static float noise(void) {
    noise_state ^= noise_state << 13;
    noise_state ^= noise_state >> 17;
    noise_state ^= noise_state << 5;
    return (float)(noise_state & 0xFFFFFF) / (float)0x7FFFFF - 1.0f;
}

static float *alloc_sound(sfx_id id, float seconds) {
    g_len[id] = (int)(seconds * SR);
    g_buf[id] = calloc((size_t)g_len[id], sizeof(float));
    return g_buf[id];
}

/* Scale to a target peak and apply a short fade-out so nothing clicks. */
static void finish(sfx_id id, float peak) {
    float *b = g_buf[id];
    int n = g_len[id];
    float m = 0.0f;
    for (int i = 0; i < n; i++) if (fabsf(b[i]) > m) m = fabsf(b[i]);
    float k = m > 0.0f ? peak / m : 0.0f;
    int fade = SR / 200;            /* 5 ms */
    for (int i = 0; i < n; i++) {
        b[i] *= k;
        int left = n - 1 - i;
        if (left < fade) b[i] *= (float)left / (float)fade;
    }
}

/* Pneumatic launch: a pitch-dropping sine thump, a puff of low-passed
 * noise for the air, and a bright click on the release valve. */
static void synth_shoot(void) {
    float *b = alloc_sound(SFX_SHOOT, 0.28f);
    float ph = 0.0f, lp = 0.0f, hp_prev = 0.0f, hp = 0.0f;
    for (int i = 0; i < g_len[SFX_SHOOT]; i++) {
        float t = (float)i / SR;
        float f = 70.0f + 320.0f * expf(-t * 35.0f);
        ph += TAU * f / SR;
        float body = sinf(ph) * expf(-t * 16.0f);
        float n = noise();
        lp += (n - lp) * 0.18f;                 /* air puff */
        hp = 0.85f * (hp + n - hp_prev);        /* valve click */
        hp_prev = n;
        b[i] = body * 0.9f
             + lp * expf(-t * 28.0f) * 1.4f
             + hp * expf(-t * 260.0f) * 0.35f;
    }
    finish(SFX_SHOOT, 0.85f);
}

/* Metallic clang: inharmonic partials (struck-plate ratios) each with
 * its own decay, over a sharp noise transient. */
static void synth_hit(void) {
    static const float ratio[] = {1.0f, 2.76f, 5.40f, 8.93f, 13.34f};
    static const float amp[]   = {1.0f, 0.70f, 0.45f, 0.28f, 0.15f};
    static const float decay[] = {7.0f, 11.0f, 17.0f, 26.0f, 40.0f};
    const float base = 540.0f;
    float *b = alloc_sound(SFX_HIT, 0.7f);
    for (int i = 0; i < g_len[SFX_HIT]; i++) {
        float t = (float)i / SR;
        float s = 0.0f;
        for (int p = 0; p < 5; p++)
            s += amp[p] * expf(-t * decay[p]) * sinf(TAU * base * ratio[p] * t + p);
        s += noise() * expf(-t * 300.0f) * 0.8f;
        b[i] = s;
    }
    finish(SFX_HIT, 0.8f);
}

/* Rubber-ball "tok": a short sine with a fast pitch blip at contact and
 * a little filtered noise for the slap on the floor. */
static void synth_tok(sfx_id id, float base, float noise_amt, float len) {
    float *b = alloc_sound(id, len);
    float ph = 0.0f, lp = 0.0f;
    for (int i = 0; i < g_len[id]; i++) {
        float t = (float)i / SR;
        float f = base * (1.0f + 0.7f * expf(-t * 110.0f));
        ph += TAU * f / SR;
        lp += (noise() - lp) * 0.3f;
        b[i] = sinf(ph) * expf(-t * 34.0f)
             + lp * expf(-t * 90.0f) * noise_amt;
    }
    finish(id, 0.8f);
}

static void synth_pickup(void) {
    float *b = alloc_sound(SFX_PICKUP, 0.20f);
    float ph = 0.0f;
    for (int i = 0; i < g_len[SFX_PICKUP]; i++) {
        float t = (float)i / SR;
        float f = t < 0.07f ? 880.0f : 1320.0f;
        float local = t < 0.07f ? t : t - 0.07f;
        ph += TAU * f / SR;
        float env = (1.0f - expf(-local * 400.0f)) * expf(-local * 18.0f);
        b[i] = (sinf(ph) + 0.25f * sinf(3.0f * ph)) * env;
    }
    finish(SFX_PICKUP, 0.55f);
}

static void synth_pop(void) {
    float *b = alloc_sound(SFX_POP, 0.55f);
    float ph = 0.0f, lp = 0.0f;
    for (int i = 0; i < g_len[SFX_POP]; i++) {
        float t = (float)i / SR;
        float f = 60.0f + 700.0f * expf(-t * 9.0f);
        ph += TAU * f / SR;
        lp += (noise() - lp) * 0.08f;
        float crackle = noise() > 0.92f ? noise() : 0.0f;
        b[i] = sinf(ph) * expf(-t * 6.0f)
             + lp * expf(-t * 7.0f) * 2.5f
             + crackle * expf(-t * 10.0f) * 0.6f;
    }
    finish(SFX_POP, 0.85f);
}

static void synth_hurt(void) {
    float *b = alloc_sound(SFX_HURT, 0.35f);
    float ph = 0.0f;
    for (int i = 0; i < g_len[SFX_HURT]; i++) {
        float t = (float)i / SR;
        ph += 85.0f / SR;
        float saw = 2.0f * (ph - floorf(ph)) - 1.0f;
        float trem = 0.6f + 0.4f * sinf(TAU * 22.0f * t);
        b[i] = saw * trem * expf(-t * 7.0f);
    }
    finish(SFX_HURT, 0.6f);
}

static void synth_dry(void) {
    float *b = alloc_sound(SFX_DRY, 0.05f);
    for (int i = 0; i < g_len[SFX_DRY]; i++) {
        float t = (float)i / SR;
        b[i] = (sinf(TAU * 2400.0f * t) * 0.5f + noise() * 0.5f) * expf(-t * 160.0f);
    }
    finish(SFX_DRY, 0.4f);
}

/* ---------------------------------------------------------------- mixer */

static void audio_cb(void *user, Uint8 *stream, int len) {
    (void)user;
    float *out = (float *)stream;
    int frames = len / (int)(2 * sizeof(float));
    memset(out, 0, (size_t)len);
    float step_scale = (float)SR / (float)g_out_rate;

    for (int v = 0; v < SFX_MAX_VOICES; v++) {
        voice *vc = &g_voices[v];
        if (!vc->active) continue;
        const float *b = g_buf[vc->id];
        int n = g_len[vc->id];
        float step = vc->rate * step_scale;
        for (int i = 0; i < frames; i++) {
            int i0 = (int)vc->pos;
            if (i0 >= n - 1) { vc->active = 0; break; }
            float fr = vc->pos - (float)i0;
            float s = b[i0] + (b[i0 + 1] - b[i0]) * fr;
            out[2 * i]     += s * vc->gl;
            out[2 * i + 1] += s * vc->gr;
            vc->pos += step;
        }
    }
    /* Gentle limiter: linear below 0.6, soft knee above. */
    for (int i = 0; i < frames * 2; i++) {
        float x = out[i] * 0.8f, a = fabsf(x);
        if (a > 0.6f) {
            float y = 0.6f + 0.4f * tanhf((a - 0.6f) / 0.4f);
            x = x < 0 ? -y : y;
        }
        out[i] = x;
    }
}

int sfx_init(int open_device) {
    synth_shoot();
    synth_hit();
    synth_tok(SFX_BOUNCE, 230.0f, 0.5f, 0.16f);
    synth_tok(SFX_WALL, 120.0f, 1.2f, 0.20f);
    synth_pickup();
    synth_pop();
    synth_hurt();
    synth_dry();

    if (!open_device) return -1;
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) < 0) {
        fprintf(stderr, "sfx: SDL_INIT_AUDIO: %s (running silent)\n", SDL_GetError());
        return -1;
    }
    SDL_AudioSpec want = {0}, have;
    want.freq     = SR;
    want.format   = AUDIO_F32SYS;
    want.channels = 2;
    want.samples  = 512;     /* ~12 ms: low enough that shots feel instant */
    want.callback = audio_cb;
    g_dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, SDL_AUDIO_ALLOW_FREQUENCY_CHANGE);
    if (!g_dev) {
        fprintf(stderr, "sfx: SDL_OpenAudioDevice: %s (running silent)\n", SDL_GetError());
        return -1;
    }
    g_out_rate = have.freq;
    SDL_PauseAudioDevice(g_dev, 0);
    fprintf(stderr, "sfx: %d Hz stereo, %d voices\n", have.freq, SFX_MAX_VOICES);
    return 0;
}

void sfx_shutdown(void) {
    if (g_dev) { SDL_CloseAudioDevice(g_dev); g_dev = 0; }
    for (int i = 0; i < SFX_COUNT; i++) { free(g_buf[i]); g_buf[i] = NULL; }
}

void sfx_play(sfx_id id, float gain, float pan, float rate) {
    if (id < 0 || id >= SFX_COUNT) return;
    g_counts[id]++;
    if (!g_dev || gain < 0.01f) return;
    if (pan < -1.0f) pan = -1.0f;
    if (pan >  1.0f) pan =  1.0f;
    /* Constant-power pan. */
    float a = (pan + 1.0f) * 0.25f * (float)M_PI;

    SDL_LockAudioDevice(g_dev);
    /* Free slot, else steal the voice closest to finishing. */
    int slot = -1;
    float best = -1.0f;
    for (int v = 0; v < SFX_MAX_VOICES; v++) {
        if (!g_voices[v].active) { slot = v; break; }
        float done = g_voices[v].pos / (float)g_len[g_voices[v].id];
        if (done > best) { best = done; slot = v; }
    }
    g_voices[slot] = (voice){1, id, 0.0f, rate, gain * cosf(a), gain * sinf(a)};
    SDL_UnlockAudioDevice(g_dev);
}

int sfx_count(sfx_id id) { return (id >= 0 && id < SFX_COUNT) ? g_counts[id] : 0; }
const char *sfx_name(sfx_id id) { return (id >= 0 && id < SFX_COUNT) ? NAMES[id] : "?"; }

static void put_u32(FILE *f, uint32_t v) { fwrite(&v, 4, 1, f); }
static void put_u16(FILE *f, uint16_t v) { fwrite(&v, 2, 1, f); }

int sfx_dump_wav(const char *dir) {
    for (int id = 0; id < SFX_COUNT; id++) {
        char path[1024];
        snprintf(path, sizeof path, "%s/%s.wav", dir, NAMES[id]);
        FILE *f = fopen(path, "wb");
        if (!f) { fprintf(stderr, "sfx: cannot write %s\n", path); return -1; }
        uint32_t data = (uint32_t)g_len[id] * 2;
        fwrite("RIFF", 1, 4, f); put_u32(f, 36 + data);
        fwrite("WAVEfmt ", 1, 8, f); put_u32(f, 16);
        put_u16(f, 1); put_u16(f, 1); put_u32(f, SR); put_u32(f, SR * 2);
        put_u16(f, 2); put_u16(f, 16);
        fwrite("data", 1, 4, f); put_u32(f, data);
        for (int i = 0; i < g_len[id]; i++) {
            float s = g_buf[id][i];
            if (s > 1.0f) s = 1.0f;
            if (s < -1.0f) s = -1.0f;
            put_u16(f, (uint16_t)(int16_t)(s * 32767.0f));
        }
        fclose(f);
        fprintf(stderr, "sfx: wrote %s (%.2f s)\n", path, (float)g_len[id] / SR);
    }
    return 0;
}
