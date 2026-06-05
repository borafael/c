#ifndef RLYEH_WORLD_H
#define RLYEH_WORLD_H

/* A "world" is one self-contained environment the demo can be in. Each lives
 * in its own translation unit (world_rlyeh.c, world_lighthouse.c) and exposes
 * itself as a `world_api` the harness in main.c drives. Adding a world is:
 * write world_foo.c, declare `extern const world_api world_foo;` here, and add
 * it to the WORLDS[] table in main.c. The harness owns the window, renderer,
 * postfx stack, input and the wrongness knob; a world owns only its scene. */

#include "scene.h"     /* scene, scene_camera, vector (via vector.h) */
#include "postfx.h"    /* postfx_fog */

/* Eye height — shared by the world builders (camera spawn) and the harness
 * (movement clamp), so it lives here rather than in either. */
#define EYE_HEIGHT 1.7f

/* ===== Shared atmosphere palette =========================================
 * R'lyeh's night sky and its slow breathing pulse double as the Lighthouse's
 * "fully wrong" lerp target (wrongness=1 lands here), so both worlds need
 * them. The Lighthouse's own dawn anchors sit alongside. */
#define SKY_HORIZON_R   8
#define SKY_HORIZON_G  32
#define SKY_HORIZON_B  48
#define SKY_ZENITH_R   58
#define SKY_ZENITH_G   24
#define SKY_ZENITH_B   64
#define SKY_PULSE_RATE  0.30f          /* radians/sec */
#define SKY_PULSE_BIAS  0.92f
#define SKY_PULSE_AMP   0.08f

#define LH_SKY_HORIZON_R 168           /* warm peach dawn horizon */
#define LH_SKY_HORIZON_G 132
#define LH_SKY_HORIZON_B 120
#define LH_SKY_ZENITH_R   78           /* soft blue zenith */
#define LH_SKY_ZENITH_G  104
#define LH_SKY_ZENITH_B  150
#define LH_FOG_R         150           /* pale dawn haze */
#define LH_FOG_G         138
#define LH_FOG_B         132

/* ===== World interface ===================================================
 * build():   lay down the scene/camera once, hand back the sky material index
 *            and the fog config this world wants. Resets its own per-frame
 *            animation handles, so it is safe to call repeatedly (on every O).
 * animate(): advance the world one frame. Gets the full frame context; a
 *            world ignores what it doesn't use (R'lyeh ignores wrongness/fog;
 *            the Lighthouse ignores dt/cam_pos). The fog pointer is the live
 *            config the harness will apply this frame, so a world may rewrite
 *            it (the Lighthouse drives fog from the wrongness knob). */
typedef struct {
    const char *name;
    void (*build)(scene **out_s, scene_camera **out_cam,
                  int *out_sky_mat, postfx_fog *out_fog);
    void (*animate)(scene *s, int sky_mat, float t_sec, float dt,
                    vector cam_pos, float wrongness, postfx_fog *fog);
} world_api;

extern const world_api world_rlyeh;
extern const world_api world_lighthouse;

#endif /* RLYEH_WORLD_H */
