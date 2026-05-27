#ifndef RLYEH_AUDIO_H
#define RLYEH_AUDIO_H

/* Streaming ambient-loop player.
 *
 * audio_init() decodes the MP3 at `mp3_path` once into a PCM buffer,
 * opens an SDL audio device, and starts a looping playback callback.
 * Failure is non-fatal — the caller should still continue, just silent.
 *
 * audio_shutdown() stops playback and frees the buffer. Safe to call
 * even if init failed. */

int  audio_init(const char *mp3_path);
void audio_shutdown(void);

#endif
