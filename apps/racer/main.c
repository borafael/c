/* Anti-grav racer prototype.
 *
 * Hi Octane / Wipeout 2097 vibe on the raytracer:
 *   - Long reflective track strip (wet-road look via reflectivity 0.55)
 *   - Mirror-still "water" plane below the track
 *   - Ship = body sphere + two wing boxes + canopy sphere
 *   - Glowing pickup rings (torus SDF, emissive)
 *   - Neon track-edge barriers (unlit boxes)
 *   - Cloud skybox sphere + distant sun
 *   - Chase cam, auto forward motion, A/D strafe, W/S boost/brake
 *
 * Renders 320x240, scaled to a 960x720 window.
 *
 * Interlace is forced on via RT_CPU_INTERLACE=0 set in main() before
 * the renderer is created — every other row is left untouched in the
 * framebuffer. Because we never memset the pixel buffer between frames,
 * the skipped rows hold the previous frame's content, which reads as
 * CRT phosphor persistence during motion. No scanline postfx needed.
 *
 * Postfx stack: chromatic → vignette → grain (no scanlines — interlace
 * already provides the banding).
 *
 * Controls:
 *   ESC          quit
 *   F11          toggle fullscreen
 *   TAB          toggle CPU/OpenGL backend (OpenGL has no interlace)
 *   1..6         resolution preset (160x120 / 240x180 / 320x240 / 480x360 / 640x480 / 960x720)
 *   I            toggle interlacing (CPU backend only)
 *   R            toggle reflections (off state swaps in procedural textures)
 *   P            toggle postfx stack (chromatic / vignette / grain)
 *   H            toggle on-screen key legend
 *   - / =        zoom camera out / in
 *   A / D        strafe
 *   W / S        boost / brake
 *   SPACE        reset to start (resets lap counter too)
 *
 * HUD: lap counter in the top-left, speed (km/h) in the top-right.
 * Press H to overlay the full key legend.
 */

#include "renderer.h"
#include "viewport.h"
#include "scene.h"
#include "sphere.h"
#include "plane.h"
#include "box.h"
#include "torus.h"
#include "cylinder.h"
#include "mesh.h"
#include "postfx.h"
#include <SDL2/SDL.h>

#define GL_GLEXT_PROTOTYPES 1
#include "gl_compat.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define INIT_WINDOW_W   960
#define INIT_WINDOW_H   720
#define FOV             (M_PI / 3.0f)

typedef struct { int w, h; const char *name; } pixel_preset;
static const pixel_preset PRESETS[] = {
    { 160, 120, "160x120" },   /* chunky pixel — pair with interlace off for legibility */
    { 240, 180, "240x180" },   /* low */
    { 320, 240, "320x240" },   /* default — CRT-era authentic */
    { 480, 360, "480x360" },   /* medium */
    { 640, 480, "640x480" },   /* sharp */
    { 960, 720, "960x720" },   /* native window */
};
#define PRESET_COUNT  ((int)(sizeof(PRESETS) / sizeof(PRESETS[0])))
#define PRESET_DEFAULT 2

#define TRACK_WIDTH         6.0f
#define TRACK_Y             0.0f
#define SHIP_HOVER_Y        0.9f
#define WORLD_GROUND_Y      -1.5f

/* Track laid out as a closed loop:
 *
 *   0  straight 1  (20m)            entry runway
 *   1  banked turn (90° left, R=15m, 30° peak)   tunnel wraps this
 *   2  straight 2  (30m)
 *   3  corkscrew   (55m, 360° barrel roll)
 *   4  straight 3  (30m)
 *   5  hairpin     (180° left, R=25m, 60° peak)  open-air wall-of-death
 *   6  straight 4  (115m)            back stretch
 *   7  closing arc (90° left, R=15m, 30° peak)   lands at origin facing +Z
 *
 * Total angular change = 90 + 180 + 90 = 360° (one full lap).
 * The straight-4 length (115m) is the only free knob — chosen so the
 * closing turn lands at (0,0,0) with heading +Z. See init_track() for
 * the position/heading propagation that proves closure. */
#define TRACK_SEG_LEN       2.5f   /* arc-segment target length */

/* Tunnel wraps the first banked turn. */
#define TUNNEL_RADIUS       3.5f
#define TUNNEL_CENTER_OFF   3.0f    /* up-offset of cylinder center from track */
#define TUNNEL_LIGHT_SPACE  4.0f    /* emissive strip every Nm along each side */

#define BASE_SPEED      24.0f
#define MAX_BOOST       1.6f
#define MIN_BOOST       0.5f
#define BOOST_RATE      1.4f
#define STRAFE_ACCEL    40.0f
#define STRAFE_DAMP     6.0f
#define MAX_STRAFE_V    14.0f
#define STRAFE_CLAMP    (TRACK_WIDTH * 0.40f)

/* ----- HUD font (5x7 bitmap) ---------------------------------------------
 *
 * Each glyph is 7 rows × 5 cols, stored as 7 bytes (low 5 bits used,
 * MSB = leftmost pixel). hud_draw_text scales per-pixel for the
 * current resolution. */
static const uint8_t HUD_FONT[][7] = {
    /* digits 0..9 */
    {0x0E,0x11,0x13,0x15,0x19,0x11,0x0E}, /* 0 */
    {0x04,0x0C,0x04,0x04,0x04,0x04,0x0E}, /* 1 */
    {0x0E,0x11,0x01,0x02,0x04,0x08,0x1F}, /* 2 */
    {0x1F,0x02,0x04,0x02,0x01,0x11,0x0E}, /* 3 */
    {0x02,0x06,0x0A,0x12,0x1F,0x02,0x02}, /* 4 */
    {0x1F,0x10,0x1E,0x01,0x01,0x11,0x0E}, /* 5 */
    {0x06,0x08,0x10,0x1E,0x11,0x11,0x0E}, /* 6 */
    {0x1F,0x01,0x02,0x04,0x08,0x08,0x08}, /* 7 */
    {0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E}, /* 8 */
    {0x0E,0x11,0x11,0x0F,0x01,0x02,0x0C}, /* 9 */
    /* letters — ordered alphabetically to match hud_font_index */
    {0x0E,0x11,0x11,0x1F,0x11,0x11,0x11}, /* A */
    {0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E}, /* B */
    {0x0F,0x10,0x10,0x10,0x10,0x10,0x0F}, /* C */
    {0x1E,0x11,0x11,0x11,0x11,0x11,0x1E}, /* D */
    {0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F}, /* E */
    {0x1F,0x10,0x10,0x1E,0x10,0x10,0x10}, /* F */
    {0x11,0x11,0x11,0x1F,0x11,0x11,0x11}, /* H */
    {0x1F,0x04,0x04,0x04,0x04,0x04,0x1F}, /* I */
    {0x11,0x12,0x14,0x18,0x14,0x12,0x11}, /* K */
    {0x10,0x10,0x10,0x10,0x10,0x10,0x1F}, /* L */
    {0x11,0x1B,0x15,0x15,0x11,0x11,0x11}, /* M */
    {0x11,0x19,0x19,0x15,0x13,0x13,0x11}, /* N */
    {0x0E,0x11,0x11,0x11,0x11,0x11,0x0E}, /* O */
    {0x1E,0x11,0x11,0x1E,0x10,0x10,0x10}, /* P */
    {0x0E,0x11,0x11,0x11,0x15,0x13,0x0F}, /* Q */
    {0x1E,0x11,0x11,0x1E,0x14,0x12,0x11}, /* R */
    {0x0F,0x10,0x10,0x0E,0x01,0x01,0x1E}, /* S */
    {0x1F,0x04,0x04,0x04,0x04,0x04,0x04}, /* T */
    {0x11,0x11,0x11,0x11,0x11,0x11,0x0E}, /* U */
    {0x11,0x11,0x11,0x15,0x15,0x1B,0x11}, /* W */
    {0x11,0x11,0x0A,0x04,0x0A,0x11,0x11}, /* X */
    {0x1F,0x01,0x02,0x04,0x08,0x10,0x1F}, /* Z */
    /* punctuation */
    {0x01,0x02,0x02,0x04,0x08,0x08,0x10}, /* / */
    {0x00,0x04,0x04,0x00,0x04,0x04,0x00}, /* : */
    {0x00,0x00,0x00,0x1F,0x00,0x00,0x00}, /* - */
    {0x00,0x1F,0x00,0x1F,0x00,0x00,0x00}, /* = */
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00}, /* space */
};

static int hud_font_index(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    switch (c) {
        case 'A': return 10;
        case 'B': return 11;
        case 'C': return 12;
        case 'D': return 13;
        case 'E': return 14;
        case 'F': return 15;
        case 'H': return 16;
        case 'I': return 17;
        case 'K': return 18;
        case 'L': return 19;
        case 'M': return 20;
        case 'N': return 21;
        case 'O': return 22;
        case 'P': return 23;
        case 'Q': return 24;
        case 'R': return 25;
        case 'S': return 26;
        case 'T': return 27;
        case 'U': return 28;
        case 'W': return 29;
        case 'X': return 30;
        case 'Z': return 31;
        case '/': return 32;
        case ':': return 33;
        case '-': return 34;
        case '=': return 35;
        case ' ': return 36;
        default:  return -1;
    }
}

static void hud_draw_text(uint32_t *pixels, int W, int H,
                          int x, int y, const char *str,
                          int scale, uint32_t color) {
    for (const char *p = str; *p; p++) {
        int idx = hud_font_index(*p);
        if (idx >= 0) {
            const uint8_t *glyph = HUD_FONT[idx];
            for (int row = 0; row < 7; row++) {
                for (int col = 0; col < 5; col++) {
                    if (!(glyph[row] & (1u << (4 - col)))) continue;
                    int px0 = x + col * scale;
                    int py0 = y + row * scale;
                    for (int dy = 0; dy < scale; dy++) {
                        int py = py0 + dy;
                        if (py < 0 || py >= H) continue;
                        for (int dx = 0; dx < scale; dx++) {
                            int px = px0 + dx;
                            if (px < 0 || px >= W) continue;
                            pixels[py * W + px] = color;
                        }
                    }
                }
            }
        }
        x += 6 * scale;     /* 5 cols + 1 spacing */
    }
}

/* Ship is a primitive composite expressed in a local frame:
 *   +z = forward (tangent), +x = right, +y = up.
 * update_ship_geometry transforms each part into world space using the
 * (right, up, tangent) frame from the track spline. */
typedef struct {
    /* primitive indices into the scene arrays */
    int hull;            /* OBB elongated body */
    int nose;            /* cone, apex forward */
    int canopy;          /* sphere on top, near front */
    int tail_fin;        /* OBB vertical fin at the back */
    int wing_l, wing_r;  /* OBBs, swept slightly outward */
    int engine_l, engine_r;   /* cylinders along tangent at the back */
    int glow_l, glow_r;       /* emissive spheres at the engine exhausts */

    /* local-frame offsets */
    vector hull_off, canopy_off, tail_fin_off;
    vector wing_l_off, wing_r_off;
    vector engine_l_off, engine_r_off;
    vector glow_l_off, glow_r_off;
    vector nose_apex_off;

    /* shape sizes */
    vector hull_he, tail_fin_he, wing_he;
    float  canopy_r, glow_r_size;
    float  nose_height, nose_radius;
    float  engine_radius, engine_half_h;
} ship_rig;

static ship_rig SHIP;

/* Reflections toggle: each reflective material has an "off" state
 * with reflectivity zeroed and a procedural texture taking the place
 * of the mirror sheen. Toggling R swaps materials between the two
 * states in-place. */
#define RACER_MAX_SWAPS 8
typedef struct {
    int idx;
    scene_material on_state, off_state;
} mat_swap;
static mat_swap SWAPS[RACER_MAX_SWAPS];
static int SWAP_COUNT = 0;

static void register_swap(scene *s, int mat_idx, scene_material off_state) {
    if (SWAP_COUNT >= RACER_MAX_SWAPS) return;
    SWAPS[SWAP_COUNT].idx = mat_idx;
    SWAPS[SWAP_COUNT].on_state = s->materials[mat_idx];
    SWAPS[SWAP_COUNT].off_state = off_state;
    SWAP_COUNT++;
}

static void apply_reflections(scene *s, int on) {
    for (int i = 0; i < SWAP_COUNT; i++) {
        s->materials[SWAPS[i].idx] = on ? SWAPS[i].on_state : SWAPS[i].off_state;
    }
}

/* ----- Track spline -------------------------------------------------------
 *
 * The track is a sequence of sections; each section has a kind
 * (straight / arc / corkscrew), a length, and (for arcs) a turn angle
 * + radius + peak bank. init_track() walks the table once and fills
 * in each section's s_start + start_pos + start_tangent + start_right.
 * Then track_frame_at(s) locates the section containing s and computes
 * the frame relative to that section's start. */
typedef struct { vector pos, tangent, right, up; } track_frame;

typedef enum {
    SEC_STRAIGHT,
    SEC_ARC,         /* horizontal left turn, optionally banked (sine bell) */
    SEC_CORKSCREW,   /* straight centerline + 360° roll around tangent + bell lift */
} sec_kind;

typedef struct {
    sec_kind kind;
    float length;       /* arc-length (for arcs, computed = radius * angle) */
    float angle;        /* SEC_ARC: turn angle in radians, CCW (always left) */
    float radius;       /* SEC_ARC: turn radius */
    float bank_peak;    /* SEC_ARC: peak bank angle in radians */
    float cork_lift;    /* SEC_CORKSCREW: centerline lift at apex */

    /* Filled in by init_track() — DO NOT initialize manually. */
    float s_start;
    vector start_pos, start_tangent, start_right;
} track_section;

enum {
    SEC_S1 = 0,        /* straight 1 (entry) */
    SEC_TURN1,         /* banked 90° left + tunnel */
    SEC_S2,
    SEC_CORK,
    SEC_S3,
    SEC_HAIRPIN,       /* 180° left, steep bank */
    SEC_S4,            /* back-stretch */
    SEC_TURN2,         /* 90° left closing turn */
    SEC_COUNT
};

static track_section SECTIONS[SEC_COUNT] = {
    [SEC_S1]      = { .kind = SEC_STRAIGHT,  .length = 20.0f },
    [SEC_TURN1]   = { .kind = SEC_ARC,       .angle = (float)(M_PI / 2.0), .radius = 15.0f, .bank_peak = (float)(M_PI / 6.0) },
    [SEC_S2]      = { .kind = SEC_STRAIGHT,  .length = 30.0f },
    [SEC_CORK]    = { .kind = SEC_CORKSCREW, .length = 55.0f, .cork_lift = 3.5f },
    [SEC_S3]      = { .kind = SEC_STRAIGHT,  .length = 30.0f },
    [SEC_HAIRPIN] = { .kind = SEC_ARC,       .angle = (float)M_PI,         .radius = 25.0f, .bank_peak = (float)(M_PI / 3.0) },
    [SEC_S4]      = { .kind = SEC_STRAIGHT,  .length = 115.0f },
    [SEC_TURN2]   = { .kind = SEC_ARC,       .angle = (float)(M_PI / 2.0), .radius = 15.0f, .bank_peak = (float)(M_PI / 6.0) },
};

static float TRACK_TOTAL_LEN = 0.0f;     /* set by init_track */

/* Rotate (x, z) CCW by angle (around +Y). */
static inline vector rot_y(vector v, float ca, float sa) {
    return (vector){ v.x * ca - v.z * sa, v.y, v.x * sa + v.z * ca };
}

static void init_track(void) {
    vector pos = {0, TRACK_Y, 0};
    vector tangent = {0, 0, 1};
    vector right = {1, 0, 0};
    float s = 0.0f;
    for (int i = 0; i < SEC_COUNT; i++) {
        track_section *sec = &SECTIONS[i];
        if (sec->kind == SEC_ARC) sec->length = sec->radius * sec->angle;

        sec->s_start       = s;
        sec->start_pos     = pos;
        sec->start_tangent = tangent;
        sec->start_right   = right;
        s += sec->length;

        if (sec->kind == SEC_STRAIGHT || sec->kind == SEC_CORKSCREW) {
            pos = vector_add(pos, vector_scale(tangent, sec->length));
        } else { /* SEC_ARC */
            float ca = cosf(sec->angle), sa = sinf(sec->angle);
            vector new_tangent = rot_y(tangent, ca, sa);
            vector new_right   = rot_y(right,   ca, sa);
            /* Left turn: arc center is at start_pos - R*right. End is at
             * center + R*new_right (the new "right" at section's end). */
            vector arc_center = vector_sub(pos, vector_scale(right, sec->radius));
            pos = vector_add(arc_center, vector_scale(new_right, sec->radius));
            tangent = new_tangent;
            right = new_right;
        }
    }
    TRACK_TOTAL_LEN = s;
}

static float track_wrap_s(float s) {
    while (s <  0.0f)             s += TRACK_TOTAL_LEN;
    while (s >= TRACK_TOTAL_LEN)  s -= TRACK_TOTAL_LEN;
    return s;
}

static track_frame track_frame_at(float s) {
    s = track_wrap_s(s);

    /* Find the section containing s. Linear scan over ~8 sections. */
    int idx = SEC_COUNT - 1;
    for (int i = 0; i < SEC_COUNT; i++) {
        if (s < SECTIONS[i].s_start + SECTIONS[i].length) { idx = i; break; }
    }
    const track_section *sec = &SECTIONS[idx];
    float local_s = s - sec->s_start;

    track_frame f;
    f.up = (vector){0, 1, 0};

    if (sec->kind == SEC_STRAIGHT) {
        f.pos     = vector_add(sec->start_pos, vector_scale(sec->start_tangent, local_s));
        f.tangent = sec->start_tangent;
        f.right   = sec->start_right;
        return f;
    }

    if (sec->kind == SEC_CORKSCREW) {
        float t       = local_s / sec->length;
        float roll    = 2.0f * (float)M_PI * t;
        float cr = cosf(roll), sr = sinf(roll);
        float lift    = sec->cork_lift * sinf((float)M_PI * t);

        f.pos = vector_add(sec->start_pos, vector_scale(sec->start_tangent, local_s));
        f.pos.y += lift;
        f.tangent = sec->start_tangent;
        f.right = vector_add(vector_scale(sec->start_right,  cr),
                             vector_scale(f.up,              sr));
        f.up    = vector_add(vector_scale(sec->start_right, -sr),
                             vector_scale(f.up,              cr));
        return f;
    }

    /* SEC_ARC */
    float a  = local_s / sec->radius;
    float ca = cosf(a), sa = sinf(a);
    vector new_tangent = rot_y(sec->start_tangent, ca, sa);
    vector new_right   = rot_y(sec->start_right,   ca, sa);
    vector arc_center  = vector_sub(sec->start_pos,
                                    vector_scale(sec->start_right, sec->radius));
    f.pos     = vector_add(arc_center, vector_scale(new_right, sec->radius));
    f.tangent = new_tangent;

    /* Banking: sine-bell, 0 at ends, peak at midpoint. */
    float t    = a / sec->angle;
    float bank = sec->bank_peak * sinf((float)M_PI * t);
    float cb = cosf(bank), sb = sinf(bank);
    vector flat_up = (vector){0, 1, 0};
    f.right = vector_add(vector_scale(new_right,  cb),
                         vector_scale(flat_up,    sb));
    f.up    = vector_add(vector_scale(new_right, -sb),
                         vector_scale(flat_up,    cb));
    return f;
}

/* Transform a point from ship-local frame (right, up, tangent) to world. */
static vector local_to_world(vector origin, vector right, vector up,
                             vector tangent, vector local) {
    return vector_add(origin,
           vector_add(vector_scale(right,   local.x),
           vector_add(vector_scale(up,      local.y),
                      vector_scale(tangent, local.z))));
}

static void build_scene(scene **scn_out, scene_camera **cam_out) {
    scene *s = scene_create();

    int m_track = scene_add_material(s, (scene_material){
        .albedo = {30, 35, 45}, .albedo2 = {18, 20, 28},
        .tex_kind = SCENE_TEX_STRIPES, .tex_scale = 4.0f,
        .reflectivity = 0.55f,
    });
    int m_water = scene_add_material(s, (scene_material){
        .albedo = {8, 14, 22}, .reflectivity = 0.78f,
    });
    int m_ship = scene_add_material(s, (scene_material){
        .albedo = {210, 35, 45}, .reflectivity = 0.30f,
    });
    int m_canopy = scene_add_material(s, (scene_material){
        .albedo = {25, 30, 60}, .reflectivity = 0.85f,
    });
    int m_wing = scene_add_material(s, (scene_material){
        .albedo = {180, 180, 195}, .reflectivity = 0.45f,
    });
    int m_sky = scene_add_material(s, (scene_material){
        .albedo = {35, 30, 75}, .albedo2 = {200, 110, 70},
        .tex_kind = SCENE_TEX_CLOUDS, .tex_scale = 30.0f,
        .unlit = 1,
    });
    int m_sun = scene_add_material(s, (scene_material){
        .albedo = {255, 230, 180}, .unlit = 1,
    });
    int m_tunnel = scene_add_material(s, (scene_material){
        /* unlit so the wall shows its albedo at full brightness even
         * when no directional light reaches the interior. Combined with
         * reflectivity this reads as "softly lit reflective panels". */
        .albedo = {55, 65, 95}, .reflectivity = 0.55f, .unlit = 1,
    });
    int m_strip = scene_add_material(s, (scene_material){
        .albedo = {255, 180, 90}, .unlit = 1,
    });

    /* Register "reflections-off" replacements for each reflective
     * material — reflectivity drops to 0 and a procedural texture
     * fills in for the mirror sheen. */
    register_swap(s, m_track, (scene_material){
        .albedo = {30, 35, 45}, .albedo2 = {18, 20, 28},
        .tex_kind = SCENE_TEX_STRIPES, .tex_scale = 4.0f,
        .reflectivity = 0.0f,
    });
    register_swap(s, m_water, (scene_material){
        .albedo = {30, 60, 95}, .albedo2 = {10, 20, 35},
        .tex_kind = SCENE_TEX_NOISE, .tex_scale = 5.0f,
        .reflectivity = 0.0f,
    });
    register_swap(s, m_ship, (scene_material){
        .albedo = {210, 35, 45}, .albedo2 = {130, 25, 30},
        .tex_kind = SCENE_TEX_SPOTS, .tex_scale = 0.3f,
        .reflectivity = 0.0f,
    });
    register_swap(s, m_canopy, (scene_material){
        .albedo = {25, 30, 60}, .albedo2 = {110, 150, 220},
        .tex_kind = SCENE_TEX_CELLS, .tex_scale = 0.3f,
        .reflectivity = 0.0f,
    });
    register_swap(s, m_wing, (scene_material){
        .albedo = {180, 180, 195}, .albedo2 = {130, 130, 150},
        .tex_kind = SCENE_TEX_STRIPES, .tex_scale = 0.6f,
        .reflectivity = 0.0f,
    });
    register_swap(s, m_tunnel, (scene_material){
        .albedo = {55, 65, 95}, .albedo2 = {30, 40, 65},
        .tex_kind = SCENE_TEX_BRICKS, .tex_scale = 2.0f,
        .reflectivity = 0.0f, .unlit = 1,
    });

    /* World water plane below the track */
    scene_add_plane(s, (scene_plane){
        .point = {0, WORLD_GROUND_Y, 0},
        .normal = {0, 1, 0},
        .material = m_water,
    });

    /* Track surface — walk the section table. Straights emit a single
     * long OBB; arcs and corkscrews emit a chain of small OBBs so the
     * varying frame is sampled at intervals. Tunnel cylinders are
     * attached to SEC_TURN1 only. */
    for (int si = 0; si < SEC_COUNT; si++) {
        const track_section *sec = &SECTIONS[si];
        int is_tunnel = (si == SEC_TURN1);

        if (sec->kind == SEC_STRAIGHT) {
            float s_mid = sec->s_start + sec->length * 0.5f;
            track_frame f = track_frame_at(s_mid);
            scene_box b = {
                .center = vector_add(f.pos, vector_scale(f.up, -0.25f)),
                .half_extents = {TRACK_WIDTH * 0.5f, 0.25f, sec->length * 0.5f},
                .ux = f.right, .uy = f.up, .uz = f.tangent,
                .material = m_track,
            };
            scene_add_box(s, b);
            continue;
        }

        int nseg = (int)(sec->length / TRACK_SEG_LEN) + 1;
        float seg_len = sec->length / (float)nseg;
        float seg_half_z = seg_len * 0.55f;
        for (int j = 0; j < nseg; j++) {
            float s_mid = sec->s_start + (j + 0.5f) * seg_len;
            track_frame f = track_frame_at(s_mid);
            scene_box surface = {
                .center = vector_add(f.pos, vector_scale(f.up, -0.25f)),
                .half_extents = {TRACK_WIDTH * 0.5f, 0.25f, seg_half_z},
                .ux = f.right, .uy = f.up, .uz = f.tangent,
                .material = m_track,
            };
            scene_add_box(s, surface);
            if (is_tunnel) {
                scene_add_cylinder(s, (scene_cylinder){
                    .center = vector_add(f.pos, vector_scale(f.up, TUNNEL_CENTER_OFF)),
                    .axis = f.tangent,
                    .radius = TUNNEL_RADIUS,
                    .half_height = seg_half_z,
                    .material = m_tunnel,
                });
            }
        }
    }

    /* Emissive strips on the upper-side of the tunnel interior.
     * Reflective cylinder bounces them into the rest of the tube so it
     * doesn't feel dead. */
    float tunnel_s_start = SECTIONS[SEC_TURN1].s_start;
    float tunnel_s_end   = tunnel_s_start + SECTIONS[SEC_TURN1].length;
    for (float sp = tunnel_s_start; sp <= tunnel_s_end; sp += TUNNEL_LIGHT_SPACE) {
        track_frame f = track_frame_at(sp);
        vector strip_he = {0.05f, 0.18f, 0.30f};
        /* Place strips at ~60° up-and-out along the cylinder interior. */
        float strip_lat = TUNNEL_RADIUS * 0.78f;     /* sin(60°) ≈ 0.866 — clear of the wall */
        float strip_up  = TUNNEL_CENTER_OFF + TUNNEL_RADIUS * 0.55f;
        scene_box left_strip = {
            .center = local_to_world(f.pos, f.right, f.up, f.tangent,
                      (vector){-strip_lat, strip_up, 0}),
            .half_extents = strip_he,
            .ux = f.right, .uy = f.up, .uz = f.tangent,
            .material = m_strip,
        };
        scene_box right_strip = {
            .center = local_to_world(f.pos, f.right, f.up, f.tangent,
                      (vector){ strip_lat, strip_up, 0}),
            .half_extents = strip_he,
            .ux = f.right, .uy = f.up, .uz = f.tangent,
            .material = m_strip,
        };
        scene_add_box(s, left_strip);
        scene_add_box(s, right_strip);
    }

    /* Engine-exhaust glow — bright cyan, unlit so it always reads as a
     * light source regardless of orientation. */
    int m_glow = scene_add_material(s, (scene_material){
        .albedo = {120, 230, 255}, .unlit = 1,
    });
    /* Engine pods — dark metallic. Reused canopy material's dark blue
     * keeps the palette tight. */
    int m_engine = scene_add_material(s, (scene_material){
        .albedo = {35, 40, 55}, .reflectivity = 0.50f,
    });
    register_swap(s, m_engine, (scene_material){
        .albedo = {35, 40, 55}, .albedo2 = {18, 22, 32},
        .tex_kind = SCENE_TEX_STRIPES, .tex_scale = 0.25f,
        .reflectivity = 0.0f,
    });

    /* Ship parts — record indices and local offsets so we can
     * translate them by the ship position each frame.
     *
     * Local frame: +z = forward (tangent), +x = right, +y = up. */
    vector zero_pos = {0, TRACK_Y + SHIP_HOVER_Y, 0};

    SHIP.hull_he       = (vector){0.35f, 0.20f, 0.85f};
    SHIP.hull_off      = (vector){0, 0, 0};

    SHIP.nose_height   = 0.40f;
    SHIP.nose_radius   = 0.32f;
    SHIP.nose_apex_off = (vector){0, 0, 1.15f};

    SHIP.canopy_r      = 0.30f;
    SHIP.canopy_off    = (vector){0, 0.26f, 0.20f};

    SHIP.tail_fin_he   = (vector){0.04f, 0.28f, 0.18f};
    SHIP.tail_fin_off  = (vector){0, 0.42f, -0.55f};

    SHIP.wing_he       = (vector){0.45f, 0.07f, 0.42f};
    SHIP.wing_l_off    = (vector){-0.72f, -0.05f, -0.10f};
    SHIP.wing_r_off    = (vector){ 0.72f, -0.05f, -0.10f};

    SHIP.engine_radius = 0.16f;
    SHIP.engine_half_h = 0.30f;
    SHIP.engine_l_off  = (vector){-0.45f, -0.05f, -0.78f};
    SHIP.engine_r_off  = (vector){ 0.45f, -0.05f, -0.78f};

    SHIP.glow_r_size   = 0.13f;
    SHIP.glow_l_off    = (vector){-0.45f, -0.05f, -1.12f};
    SHIP.glow_r_off    = (vector){ 0.45f, -0.05f, -1.12f};

    SHIP.hull = scene_add_box(s,
        scene_box_obb(zero_pos, SHIP.hull_he, (vector){0,0,0}, m_ship));
    SHIP.nose = scene_add_cone(s, (scene_cone){
        .apex = zero_pos, .axis = {0, 0, -1},
        .height = SHIP.nose_height, .radius = SHIP.nose_radius,
        .material = m_ship,
    });
    SHIP.canopy = scene_add_sphere(s, (scene_sphere){
        .center = zero_pos, .radius = SHIP.canopy_r, .material = m_canopy,
    });
    SHIP.tail_fin = scene_add_box(s,
        scene_box_obb(zero_pos, SHIP.tail_fin_he, (vector){0,0,0}, m_wing));
    SHIP.wing_l = scene_add_box(s,
        scene_box_obb(zero_pos, SHIP.wing_he, (vector){0,0,0}, m_wing));
    SHIP.wing_r = scene_add_box(s,
        scene_box_obb(zero_pos, SHIP.wing_he, (vector){0,0,0}, m_wing));
    SHIP.engine_l = scene_add_cylinder(s, (scene_cylinder){
        .center = zero_pos, .axis = {0, 0, 1},
        .radius = SHIP.engine_radius, .half_height = SHIP.engine_half_h,
        .material = m_engine,
    });
    SHIP.engine_r = scene_add_cylinder(s, (scene_cylinder){
        .center = zero_pos, .axis = {0, 0, 1},
        .radius = SHIP.engine_radius, .half_height = SHIP.engine_half_h,
        .material = m_engine,
    });
    SHIP.glow_l = scene_add_sphere(s, (scene_sphere){
        .center = zero_pos, .radius = SHIP.glow_r_size, .material = m_glow,
    });
    SHIP.glow_r = scene_add_sphere(s, (scene_sphere){
        .center = zero_pos, .radius = SHIP.glow_r_size, .material = m_glow,
    });

    /* Skybox + sun */
    scene_add_sphere(s, (scene_sphere){
        .center = {0, 0, TRACK_TOTAL_LEN * 0.5f},
        .radius = 500.0f, .material = m_sky,
    });
    scene_add_sphere(s, (scene_sphere){
        .center = {-80.0f, 70.0f, 280.0f},
        .radius = 14.0f, .material = m_sun,
    });

    scene_set_ambient(s, 0.25f);
    scene_add_light(s, (scene_light){
        .direction = {-0.4f, 0.85f, -0.35f},
        .intensity = 0.95f,
    });

    rt_scene_build_accel(s);

    *scn_out = s;
    *cam_out = scene_camera_create(
        (vector){0, TRACK_Y + 3.0f, -5.0f},
        (vector){0, -0.15f, 1.0f});
}

static inline void set_obb_frame(scene_box *b, vector right, vector up, vector tangent) {
    b->ux = right; b->uy = up; b->uz = tangent;
}

static void update_ship_geometry(scene *s, vector ship_pos,
                                 vector right, vector up, vector tangent) {
    /* Hull (OBB) */
    s->boxes[SHIP.hull].center =
        local_to_world(ship_pos, right, up, tangent, SHIP.hull_off);
    set_obb_frame(&s->boxes[SHIP.hull], right, up, tangent);

    /* Nose cone — apex forward, axis points back toward base. */
    s->cones[SHIP.nose].apex =
        local_to_world(ship_pos, right, up, tangent, SHIP.nose_apex_off);
    s->cones[SHIP.nose].axis = vector_scale(tangent, -1.0f);

    /* Canopy (sphere) */
    s->spheres[SHIP.canopy].center =
        local_to_world(ship_pos, right, up, tangent, SHIP.canopy_off);

    /* Tail fin (OBB) */
    s->boxes[SHIP.tail_fin].center =
        local_to_world(ship_pos, right, up, tangent, SHIP.tail_fin_off);
    set_obb_frame(&s->boxes[SHIP.tail_fin], right, up, tangent);

    /* Wings (OBBs) */
    s->boxes[SHIP.wing_l].center =
        local_to_world(ship_pos, right, up, tangent, SHIP.wing_l_off);
    set_obb_frame(&s->boxes[SHIP.wing_l], right, up, tangent);
    s->boxes[SHIP.wing_r].center =
        local_to_world(ship_pos, right, up, tangent, SHIP.wing_r_off);
    set_obb_frame(&s->boxes[SHIP.wing_r], right, up, tangent);

    /* Engine pods (cylinders) — axis along tangent. */
    s->cylinders[SHIP.engine_l].center =
        local_to_world(ship_pos, right, up, tangent, SHIP.engine_l_off);
    s->cylinders[SHIP.engine_l].axis = tangent;
    s->cylinders[SHIP.engine_r].center =
        local_to_world(ship_pos, right, up, tangent, SHIP.engine_r_off);
    s->cylinders[SHIP.engine_r].axis = tangent;

    /* Engine glows (emissive spheres) */
    s->spheres[SHIP.glow_l].center =
        local_to_world(ship_pos, right, up, tangent, SHIP.glow_l_off);
    s->spheres[SHIP.glow_r].center =
        local_to_world(ship_pos, right, up, tangent, SHIP.glow_r_off);
}

static void display_pixels(GLuint tex, GLuint fbo, const uint32_t *pixels,
                           int render_w, int render_h,
                           int window_w, int window_h) {
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, render_w, render_h,
                    GL_BGRA, GL_UNSIGNED_BYTE, pixels);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_2D, tex, 0);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    glBlitFramebuffer(0, 0, render_w, render_h,
                      0, window_h, window_w, 0,
                      GL_COLOR_BUFFER_BIT, GL_NEAREST);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
}

int main(int argc, char *argv[]) {
    (void)argc; (void)argv;

    /* Force interlace on the CPU backend before creating the renderer. */
    setenv("RT_CPU_INTERLACE", "0", 1);

    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        fprintf(stderr, "SDL init failed: %s\n", SDL_GetError());
        return 1;
    }

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

    int window_w = INIT_WINDOW_W, window_h = INIT_WINDOW_H;
    int fullscreen = 0;
    SDL_Window *window = SDL_CreateWindow("Racer",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        window_w, window_h, SDL_WINDOW_OPENGL);
    if (!window) {
        fprintf(stderr, "Window creation failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }
    SDL_GLContext gl_ctx = SDL_GL_CreateContext(window);
    if (!gl_ctx) {
        fprintf(stderr, "GL context creation failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(window); SDL_Quit();
        return 1;
    }
    SDL_GL_SetSwapInterval(0);
    gl_compat_init((gl_compat_loader_fn)SDL_GL_GetProcAddress);

    rt_renderer *cpu_rnd = rt_renderer_available(RT_BACKEND_CPU)
                         ? rt_renderer_create(RT_BACKEND_CPU) : NULL;
    rt_renderer *gpu_rnd = rt_renderer_available(RT_BACKEND_OPENGL)
                         ? rt_renderer_create(RT_BACKEND_OPENGL) : NULL;
    if (!cpu_rnd && !gpu_rnd) {
        fprintf(stderr, "No renderers available\n");
        SDL_GL_DeleteContext(gl_ctx);
        SDL_DestroyWindow(window); SDL_Quit();
        return 1;
    }
    rt_renderer *active = cpu_rnd ? cpu_rnd : gpu_rnd;
    fprintf(stderr, "Active: %s (TAB to toggle)\n", rt_renderer_name(active));

    scene *scn;
    scene_camera *cam;
    init_track();
    build_scene(&scn, &cam);

    int preset = PRESET_DEFAULT;
    int render_w = PRESETS[preset].w, render_h = PRESETS[preset].h;
    rt_viewport viewport = { render_w, render_h, FOV };
    uint32_t *pixels = calloc((size_t)(render_w * render_h), sizeof(uint32_t));

    postfx_chromatic chrom_cfg = { .enabled = 1, .shift_pixels = 2 };
    postfx_vignette  vig_cfg   = { .enabled = 1, .intensity = 0.55f, .softness = 0.4f };
    postfx_grain     grain_cfg = { .enabled = 1, .strength = 0.16f, .seed = 0 };
    postfx_chromatic_ctx *chrom = postfx_chromatic_create(render_w, render_h);

    GLuint display_tex, display_fbo;
    glGenTextures(1, &display_tex);
    glBindTexture(GL_TEXTURE_2D, display_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, render_w, render_h, 0,
                 GL_BGRA, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glGenFramebuffers(1, &display_fbo);

    /* Game state — track-relative: (arc-length along centerline, lateral offset). */
    float  ship_s   = 0.0f;
    float  ship_lat = 0.0f;
    float  strafe_v = 0.0f;
    float  boost    = 1.0f;
    float  cam_zoom = 1.0f;     /* scales chase distance + height; -/= adjust */
    int    lap      = 1;        /* incremented each time ship_s wraps */
    int    interlace_on  = 1;   /* I toggles; CPU only */
    int    reflections_on = 1;  /* R toggles; swap to procedural tex when off */
    int    postfx_on = 1;       /* P toggles chromatic+vignette+grain stack */
    int    legend_on = 0;       /* H toggles the key-legend overlay */

    int running = 1;
    Uint32 fps_last = SDL_GetTicks();
    Uint32 frame_last = SDL_GetTicks();
    int fps_frames = 0;
    Uint32 render_ms_accum = 0, fx_ms_accum = 0;
    char title_buf[200];

    while (running) {
        Uint32 frame_now = SDL_GetTicks();
        float dt = (frame_now - frame_last) / 1000.0f;
        if (dt > 0.05f) dt = 0.05f;
        frame_last = frame_now;

        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) running = 0;
            if (e.type == SDL_KEYDOWN) {
                SDL_Keycode k = e.key.keysym.sym;
                if (k == SDLK_ESCAPE) running = 0;
                if (k == SDLK_SPACE) {
                    ship_s = 0.0f; ship_lat = 0.0f;
                    strafe_v = 0.0f; boost = 1.0f;
                    lap = 1;
                }
                if (k == SDLK_TAB) {
                    if (active == cpu_rnd && gpu_rnd) active = gpu_rnd;
                    else if (active == gpu_rnd && cpu_rnd) active = cpu_rnd;
                    fprintf(stderr, "Active: %s\n", rt_renderer_name(active));
                }
                if (k == SDLK_F11) {
                    fullscreen = !fullscreen;
                    SDL_SetWindowFullscreen(window,
                        fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
                    SDL_GetWindowSize(window, &window_w, &window_h);
                }
                if (k == SDLK_MINUS || k == SDLK_KP_MINUS) {
                    cam_zoom += 0.25f;
                    if (cam_zoom > 3.5f) cam_zoom = 3.5f;
                    fprintf(stderr, "Camera zoom: %.2fx\n", cam_zoom);
                }
                if (k == SDLK_EQUALS || k == SDLK_KP_PLUS) {
                    cam_zoom -= 0.25f;
                    if (cam_zoom < 0.5f) cam_zoom = 0.5f;
                    fprintf(stderr, "Camera zoom: %.2fx\n", cam_zoom);
                }
                if (k == SDLK_i) {
                    interlace_on = !interlace_on;
                    if (cpu_rnd) rt_renderer_set_interlace(cpu_rnd, interlace_on ? 0 : -1);
                    /* Clear the framebuffer so the half-rows from the
                     * prior mode don't ghost when switching. */
                    memset(pixels, 0, (size_t)(render_w * render_h) * sizeof(uint32_t));
                    fprintf(stderr, "Interlace: %s\n", interlace_on ? "on" : "off");
                }
                if (k == SDLK_r) {
                    reflections_on = !reflections_on;
                    apply_reflections(scn, reflections_on);
                    fprintf(stderr, "Reflections: %s\n",
                            reflections_on ? "on" : "off");
                }
                if (k == SDLK_p) {
                    postfx_on = !postfx_on;
                    fprintf(stderr, "Postfx: %s\n", postfx_on ? "on" : "off");
                }
                if (k == SDLK_h) {
                    legend_on = !legend_on;
                }
                if (k >= SDLK_1 && k <= SDLK_6) {
                    int idx = k - SDLK_1;
                    if (idx < PRESET_COUNT && idx != preset) {
                        preset = idx;
                        render_w = PRESETS[preset].w;
                        render_h = PRESETS[preset].h;
                        free(pixels);
                        pixels = calloc((size_t)(render_w * render_h),
                                        sizeof(uint32_t));
                        viewport = (rt_viewport){ render_w, render_h, FOV };
                        postfx_chromatic_destroy(chrom);
                        chrom = postfx_chromatic_create(render_w, render_h);
                        glBindTexture(GL_TEXTURE_2D, display_tex);
                        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8,
                                     render_w, render_h, 0,
                                     GL_BGRA, GL_UNSIGNED_BYTE, NULL);
                        fprintf(stderr, "Preset: %s\n", PRESETS[preset].name);
                    }
                }
            }
        }

        const Uint8 *keys = SDL_GetKeyboardState(NULL);

        /* Strafe */
        float strafe_input = 0.0f;
        if (keys[SDL_SCANCODE_A]) strafe_input -= 1.0f;
        if (keys[SDL_SCANCODE_D]) strafe_input += 1.0f;
        strafe_v += strafe_input * STRAFE_ACCEL * dt;
        strafe_v -= strafe_v * STRAFE_DAMP * dt;
        if (strafe_v >  MAX_STRAFE_V) strafe_v =  MAX_STRAFE_V;
        if (strafe_v < -MAX_STRAFE_V) strafe_v = -MAX_STRAFE_V;

        /* Boost / brake */
        float boost_input = 0.0f;
        if (keys[SDL_SCANCODE_W]) boost_input += 1.0f;
        if (keys[SDL_SCANCODE_S]) boost_input -= 1.0f;
        boost += boost_input * BOOST_RATE * dt;
        if (boost > MAX_BOOST) boost = MAX_BOOST;
        if (boost < MIN_BOOST) boost = MIN_BOOST;
        if (boost_input == 0.0f) {
            float toward_one = (1.0f - boost) * 1.5f * dt;
            boost += toward_one;
        }

        /* Integrate along the track */
        ship_lat += strafe_v * dt;
        ship_s   += BASE_SPEED * boost * dt;
        if (ship_lat >  STRAFE_CLAMP) { ship_lat =  STRAFE_CLAMP; strafe_v = 0; }
        if (ship_lat < -STRAFE_CLAMP) { ship_lat = -STRAFE_CLAMP; strafe_v = 0; }
        while (ship_s >= TRACK_TOTAL_LEN) {
            ship_s -= TRACK_TOTAL_LEN;
            lap++;
        }
        while (ship_s < 0.0f) ship_s += TRACK_TOTAL_LEN;

        /* Resolve to world */
        track_frame sf = track_frame_at(ship_s);
        vector ship_world = local_to_world(sf.pos, sf.right, sf.up, sf.tangent,
                                           (vector){ship_lat, SHIP_HOVER_Y, 0});
        update_ship_geometry(scn, ship_world, sf.right, sf.up, sf.tangent);

        /* Chase cam: trailing distance + height scaled by cam_zoom.
         * Pulled slightly toward the ship's lateral side, looks 6m ahead. */
        float cam_back = 4.5f * cam_zoom;
        float cam_high = 1.6f * cam_zoom;
        track_frame cf = track_frame_at(ship_s - cam_back);
        vector cam_pos = local_to_world(cf.pos, cf.right, cf.up, cf.tangent,
                                        (vector){ship_lat * 0.6f, cam_high, 0});
        track_frame lf = track_frame_at(ship_s + 6.0f);
        vector look_at = local_to_world(lf.pos, lf.right, lf.up, lf.tangent,
                                        (vector){ship_lat, 0.4f, 0});
        vector cam_dir = vector_normalize(vector_sub(look_at, cam_pos));
        scene_camera_place(cam, cam_pos, cam_dir);

        Uint32 r_start = SDL_GetTicks();
        rt_renderer_render(active, scn, cam, &viewport, pixels, NULL);
        Uint32 r_done = SDL_GetTicks();

        /* When interlace is on, the skipped rows hold content from
         * earlier frames. Without clearing them, every postfx pass
         * (chromatic, vignette, grain) compounds on those stale rows
         * each frame — they progressively darken, shift colors, and
         * pick up grain. Zero them here so postfx sees clean
         * "content + black" alternating rows (true scanline look,
         * no accumulation). */
        if (interlace_on) {
            for (int y = 1; y < render_h; y += 2) {
                memset(&pixels[y * render_w], 0,
                       (size_t)render_w * sizeof(uint32_t));
            }
        }

        if (postfx_on) {
            postfx_chromatic_apply(chrom, pixels, render_w, render_h, &chrom_cfg);
            postfx_vignette_apply (pixels, render_w, render_h, &vig_cfg);
            grain_cfg.seed = frame_now;
            postfx_grain_apply    (pixels, render_w, render_h, &grain_cfg);
        }

        /* HUD: drawn after postfx so digits stay crisp. Scale with
         * render height so glyphs are legible at every preset. Color
         * is the same warm amber used by the tunnel light strips. */
        {
            int hud_scale = render_h / 120;
            if (hud_scale < 1) hud_scale = 1;
            uint32_t hud_color = 0xFFFFB450u;  /* amber, ARGB */
            int speed_kmh = (int)(BASE_SPEED * boost * 3.6f + 0.5f);
            char lap_buf[16], spd_buf[16];
            snprintf(lap_buf, sizeof(lap_buf), "LAP %d", lap);
            snprintf(spd_buf, sizeof(spd_buf), "%d KM/H", speed_kmh);
            int pad = 4 * hud_scale;
            int spd_w = (int)strlen(spd_buf) * 6 * hud_scale;
            hud_draw_text(pixels, render_w, render_h,
                          pad, pad, lap_buf, hud_scale, hud_color);
            hud_draw_text(pixels, render_w, render_h,
                          render_w - spd_w - pad, pad, spd_buf,
                          hud_scale, hud_color);

            if (legend_on) {
                static const char *legend[] = {
                    "A/D STRAFE",
                    "W/S BOOST/BRAKE",
                    "SPACE RESET",
                    "1-6 RES",
                    "-/= ZOOM",
                    "I INTERLACE",
                    "R REFLECTIONS",
                    "P POSTFX",
                    "H HELP",
                    "F11 FULLSCREEN",
                    "TAB BACKEND",
                    "ESC QUIT",
                };
                int n_lines = (int)(sizeof(legend) / sizeof(legend[0]));
                int line_h = 8 * hud_scale;       /* 7 row + 1 spacing */
                int y0 = pad + line_h * 2;        /* below LAP */
                for (int i = 0; i < n_lines; i++) {
                    int y = y0 + i * line_h;
                    if (y + 7 * hud_scale >= render_h) break;
                    hud_draw_text(pixels, render_w, render_h,
                                  pad, y, legend[i], hud_scale, hud_color);
                }
            }
        }
        Uint32 fx_done = SDL_GetTicks();

        render_ms_accum += r_done  - r_start;
        fx_ms_accum     += fx_done - r_done;

        display_pixels(display_tex, display_fbo, pixels,
                       render_w, render_h, window_w, window_h);
        SDL_GL_SwapWindow(window);

        fps_frames++;
        Uint32 now = SDL_GetTicks();
        if (now - fps_last >= 1000) {
            float avg_r  = fps_frames ? (float)render_ms_accum / fps_frames : 0.0f;
            float avg_fx = fps_frames ? (float)fx_ms_accum     / fps_frames : 0.0f;
            snprintf(title_buf, sizeof(title_buf),
                     "Racer - %s %dx%d boost=%.2f s=%.0f/%.0f %d FPS (rt=%.1fms fx=%.1fms)",
                     rt_renderer_name(active), render_w, render_h,
                     boost, ship_s, TRACK_TOTAL_LEN,
                     fps_frames, avg_r, avg_fx);
            SDL_SetWindowTitle(window, title_buf);
            fps_frames = 0; render_ms_accum = 0; fx_ms_accum = 0;
            fps_last = now;
        }
    }

    glDeleteFramebuffers(1, &display_fbo);
    glDeleteTextures(1, &display_tex);
    if (cpu_rnd) rt_renderer_destroy(cpu_rnd);
    if (gpu_rnd) rt_renderer_destroy(gpu_rnd);
    postfx_chromatic_destroy(chrom);
    free(pixels);
    scene_camera_destroy(cam);
    scene_destroy(scn);
    SDL_GL_DeleteContext(gl_ctx);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
