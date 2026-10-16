/* kit — small building blocks for assembling eldritch creatures out of
 * raytracer primitives: material shorthands, a vector rotation helper,
 * capsule-chain tentacles, and camera-tracking eye textures.
 *
 * Every creature is rebuilt from scratch each frame (scene_clear +
 * re-add), so nothing here keeps per-creature state; the only state is
 * the eye-texture pool, reset by kit_frame_begin(). */

#ifndef BESTIARY_KIT_H
#define BESTIARY_KIT_H

#include "scene.h"
#include "vector.h"

/* Per-frame inputs every creature builder receives. */
typedef struct {
    float  t;     /* seconds since the creature was summoned */
    vector cam;   /* camera position, for eyes that follow the viewer */
    int    lensing; /* allow black holes (CPU-heavy geodesic marching) */
} bst_ctx;

/* ---- Materials ---------------------------------------------------------- */

scene_color    kit_hsv(float h, float s, float v);   /* h wraps, s/v in 0..1 */
scene_material kit_matte(scene_color c);
scene_material kit_glow(scene_color c);              /* unlit — blooms */
scene_material kit_tex(scene_color a, scene_color b,
                       scene_tex_kind kind, float scale);
int            kit_mat(scene *s, scene_material m);  /* scene_add_material */

/* ---- Geometry ----------------------------------------------------------- */

vector kit_rotate(vector v, vector axis, float angle);  /* axis must be unit */
void   kit_basis(vector n, vector *e1, vector *e2);     /* ⟂ to unit n */
void   kit_sphere(scene *s, vector c, float r, int mat);

/* A tentacle is a chain of spheres joined by cylinders (a capsule chain).
 * The heading starts at `dir` and bends a little at every joint: `curl`
 * is a constant total bend (radians, curls toward e1 of dir), and `sway`
 * is a time-varying total bend split across two perpendicular axes so
 * the tip traces a 3D figure rather than a flat arc. */
typedef struct {
    vector root, dir;
    float  length;
    int    segments;
    float  r_root, r_tip;
    float  curl;
    float  sway, sway_freq, phase;
    int    mat;
} kit_tentacle;

typedef struct { vector tip, dir; } kit_limb_end;

kit_limb_end kit_add_tentacle(scene *s, const kit_tentacle *d, float t);

/* ---- Eyes ---------------------------------------------------------------
 *
 * Sphere UVs in the renderer are fixed in world space (lat/lon around
 * the sphere's center), and image textures ignore world_to_obj, so a
 * sphere can't be "rotated" to aim a painted eye. Instead each eye gets
 * its own small equirectangular texture repainted every frame with the
 * iris centered on the look direction. Painting in angular space (a dot
 * product per texel) keeps the iris round everywhere on the sphere,
 * poles included. */

typedef struct {
    uint32_t sclera, iris, pupil;   /* ARGB */
    float    iris_angle;            /* iris angular radius, radians */
    float    pupil_frac;            /* pupil radius as fraction of iris */
    float    slit;                  /* 0 = round pupil; >0 squeezes it
                                     * horizontally into a vertical slit */
} kit_eye_style;

extern const kit_eye_style KIT_EYE_GOLD;     /* the ophan's staring eye */
extern const kit_eye_style KIT_EYE_SLIT;     /* yellow, goat/cat slit */
extern const kit_eye_style KIT_EYE_SHOGGOTH; /* green, glassy, all pupil */
extern const kit_eye_style KIT_EYE_YITH;     /* black eyes on a yellow globe */
extern const kit_eye_style KIT_EYE_LENS;     /* camera lens: red ring, hot core */

void kit_frame_begin(void);   /* recycle the eye-texture pool */

/* Paint a texture with up to 4 eyes (one per look direction) and add it
 * to the scene. Returns the texture index, or -1 if the pool is spent. */
int kit_eye_texture(scene *s, const kit_eye_style *st,
                    const vector *looks, int n_looks);

/* Convenience: a sphere at `c` with one eye that tracks `target`.
 * `jitter` adds a small per-eye saccade so a crowd of eyes doesn't move
 * in perfect lockstep. Returns the material index used. */
int kit_eye(scene *s, vector c, float r, vector target,
            const kit_eye_style *st, int unlit, float t, float jitter);

/* The ophan's ring tapestry: a static 4x2 grid of small eyes for torus
 * UVs (around-axis × around-tube). Returns a texture index. */
int kit_tapestry_texture(scene *s);

#endif /* BESTIARY_KIT_H */
