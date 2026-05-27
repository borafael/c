#include "audio.h"

#define DR_MP3_IMPLEMENTATION
#include "vendor/dr_mp3.h"

#include <SDL2/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

static SDL_AudioDeviceID g_dev         = 0;
static int16_t          *g_pcm         = NULL;   /* interleaved frames */
static uint64_t          g_pcm_frames  = 0;
static uint64_t          g_pos         = 0;      /* frame cursor */
static int               g_channels    = 2;

/* SDL audio callback — runs on a thread SDL spawns internally. The mix
 * loop wraps around to frame 0 so the clip plays forever; no extra
 * thread of our own. */
static void audio_cb(void *user, Uint8 *stream, int len) {
    (void)user;
    int16_t *out = (int16_t *)stream;
    int frames_wanted = (int)(len / (sizeof(int16_t) * (size_t)g_channels));

    while (frames_wanted > 0) {
        uint64_t remaining = g_pcm_frames - g_pos;
        uint64_t take = (uint64_t)frames_wanted < remaining
                      ? (uint64_t)frames_wanted : remaining;
        memcpy(out, g_pcm + g_pos * g_channels,
               (size_t)take * g_channels * sizeof(int16_t));
        out += take * g_channels;
        g_pos += take;
        frames_wanted -= (int)take;
        if (g_pos >= g_pcm_frames) g_pos = 0;
    }
}

int audio_init(const char *mp3_path) {
    /* Probe silently — callers may try multiple candidate paths. */
    FILE *probe = fopen(mp3_path, "rb");
    if (!probe) return -1;
    fclose(probe);

    if (SDL_InitSubSystem(SDL_INIT_AUDIO) < 0) {
        fprintf(stderr, "audio: SDL_INIT_AUDIO: %s\n", SDL_GetError());
        return -1;
    }

    drmp3_config cfg = {0};
    drmp3_uint64 frame_count = 0;
    drmp3_int16 *pcm = drmp3_open_file_and_read_pcm_frames_s16(
        mp3_path, &cfg, &frame_count, NULL);
    if (!pcm) {
        fprintf(stderr, "audio: failed to decode %s\n", mp3_path);
        return -1;
    }

    SDL_AudioSpec want = {0}, have = {0};
    want.freq     = (int)cfg.sampleRate;
    want.format   = AUDIO_S16SYS;
    want.channels = (Uint8)cfg.channels;
    want.samples  = 2048;
    want.callback = audio_cb;

    SDL_AudioDeviceID dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if (dev == 0) {
        fprintf(stderr, "audio: SDL_OpenAudioDevice: %s\n", SDL_GetError());
        drmp3_free(pcm, NULL);
        return -1;
    }

    g_pcm         = pcm;
    g_pcm_frames  = frame_count;
    g_pos         = 0;
    g_channels    = have.channels;
    g_dev         = dev;

    SDL_PauseAudioDevice(dev, 0);
    fprintf(stderr, "audio: %s — %u Hz, %d ch, %.1f s loop\n",
            mp3_path, (unsigned)cfg.sampleRate, g_channels,
            (double)frame_count / (double)cfg.sampleRate);
    return 0;
}

void audio_shutdown(void) {
    if (g_dev) {
        SDL_CloseAudioDevice(g_dev);
        g_dev = 0;
    }
    if (g_pcm) {
        drmp3_free(g_pcm, NULL);
        g_pcm = NULL;
    }
    g_pcm_frames = 0;
    g_pos        = 0;
}
