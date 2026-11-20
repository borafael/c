#ifndef RICOCHET_SFX_H
#define RICOCHET_SFX_H

/* Procedural sound effects + a tiny voice mixer.
 *
 * Every sound is synthesized into a float buffer at sfx_init() time
 * (no asset files). sfx_play() starts a voice with a gain, a stereo pan
 * and a playback-rate multiplier (pitch); the SDL audio callback mixes
 * up to SFX_MAX_VOICES of them. Audio failure is non-fatal: sfx_play()
 * still counts events so headless runs can verify triggers. */

typedef enum {
    SFX_SHOOT,     /* pneumatic "pfoomp" of the sphere gun          */
    SFX_HIT,       /* metallic clang: bullet strikes an enemy       */
    SFX_BOUNCE,    /* rubbery "tok": bullet bounces off the floor   */
    SFX_WALL,      /* duller thud: bullet hits a wall / pillar      */
    SFX_PICKUP,    /* rising two-tone blip: ammo recollected        */
    SFX_POP,       /* descending pop: enemy destroyed               */
    SFX_HURT,      /* low buzz: player touched by an enemy          */
    SFX_DRY,       /* tiny click: trigger pulled with no ammo       */
    SFX_COUNT
} sfx_id;

#define SFX_MAX_VOICES 32

/* Synthesizes all buffers. If open_device is nonzero also opens the SDL
 * audio device and starts playback. Returns 0 if audio is live, -1 if
 * running silent (buffers still exist, so sfx_dump_wav works). */
int  sfx_init(int open_device);
void sfx_shutdown(void);

/* gain 0..1+, pan -1 (left) .. +1 (right), rate 1 = native pitch. */
void sfx_play(sfx_id id, float gain, float pan, float rate);

/* How many times each sound was triggered (for headless checks). */
int  sfx_count(sfx_id id);
const char *sfx_name(sfx_id id);

/* Writes every synthesized sound as a 16-bit mono WAV into dir. */
int  sfx_dump_wav(const char *dir);

#endif
