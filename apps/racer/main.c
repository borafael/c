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
 *   - / =        zoom camera out / in
 *   A / D        strafe
 *   W / S        boost / brake
 *   SPACE        reset to start
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

/* Track layout: straight → banked left turn → straight. */
#define TRACK_STRAIGHT1     70.0f
#define TRACK_TURN_RADIUS   28.0f
#define TRACK_TURN_ANGLE    ((float)(M_PI * 0.55))   /* ~99° */
#define TRACK_TURN_BANK     ((float)(M_PI / 6.0))    /* 30° peak */
#define TRACK_STRAIGHT2     50.0f
#define TRACK_CORK_LEN      55.0f       /* full 360° barrel-roll over this arc length */
#define TRACK_STRAIGHT3     50.0f
#define TRACK_ARC_LEN       (TRACK_TURN_RADIUS * TRACK_TURN_ANGLE)
#define TRACK_TOTAL         (TRACK_STRAIGHT1 + TRACK_ARC_LEN + TRACK_STRAIGHT2 + \
                             TRACK_CORK_LEN + TRACK_STRAIGHT3)

/* Useful segment-start offsets along the spline. */
#define TRACK_S_TURN_START  (TRACK_STRAIGHT1)
#define TRACK_S_STR2_START  (TRACK_STRAIGHT1 + TRACK_ARC_LEN)
#define TRACK_S_CORK_START  (TRACK_S_STR2_START + TRACK_STRAIGHT2)
#define TRACK_S_STR3_START  (TRACK_S_CORK_START + TRACK_CORK_LEN)

#define TRACK_SEG_LEN       2.5f   /* approximate; segments overlap slightly */

/* Tunnel wraps the banked turn for the photogenic reflection moment.
 * Built from one cylinder per segment (axis = tangent) so the cross-
 * section is a true circle that follows the spline + banking. */
#define TUNNEL_S_START      TRACK_S_TURN_START
#define TUNNEL_S_END        TRACK_S_STR2_START
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

typedef struct {
    int body, wing_l, wing_r, canopy;
    vector body_off, wing_l_off, wing_r_off, canopy_off;
    float body_r, canopy_r;
    vector wing_he;
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
 * A track frame at arc-length s gives world position + an orthonormal
 * (tangent, right, up) basis. Bank is a roll around the tangent axis,
 * which tilts (right, up) but leaves the centerline path itself flat in
 * world space. The arc is a horizontal left turn; banking peaks in the
 * middle of the arc and eases off at the ends.
 */
typedef struct { vector pos, tangent, right, up; } track_frame;

static float track_wrap_s(float s) {
    while (s <  0.0f)        s += TRACK_TOTAL;
    while (s >= TRACK_TOTAL) s -= TRACK_TOTAL;
    return s;
}

static track_frame track_frame_at(float s) {
    /* No wrap: extend the endpoints linearly so the chase cam (which
     * samples s±offset) gets a sensible frame even just before the
     * starting line or just past the finish. Lap wrapping is handled
     * by track_wrap_s at the ship-state level. */
    track_frame f;
    f.up = (vector){0, 1, 0};

    if (s < TRACK_STRAIGHT1) {
        f.pos     = (vector){0, TRACK_Y, s};       /* s may be < 0 — extends straight 1 backwards */
        f.tangent = (vector){0, 0, 1};
        f.right   = (vector){1, 0, 0};
        return f;
    }

    if (s < TRACK_STRAIGHT1 + TRACK_ARC_LEN) {
        float local_s = s - TRACK_STRAIGHT1;
        float a  = local_s / TRACK_TURN_RADIUS;
        float ca = cosf(a), sa = sinf(a);

        /* Curve center sits TRACK_TURN_RADIUS to the left of the start
         * of the arc (i.e., at -X). Going CCW from above gives a left
         * turn that initially still heads +Z. */
        f.pos = (vector){
            -TRACK_TURN_RADIUS + TRACK_TURN_RADIUS * ca,
             TRACK_Y,
             TRACK_STRAIGHT1 + TRACK_TURN_RADIUS * sa
        };
        f.tangent = (vector){-sa, 0, ca};
        vector flat_right = (vector){ ca, 0, sa};

        /* Smooth bell-shaped bank: 0 at arc ends, peak in middle. */
        float t    = a / TRACK_TURN_ANGLE;
        float bank = TRACK_TURN_BANK * sinf((float)M_PI * t);
        float cb = cosf(bank), sb = sinf(bank);

        vector flat_up = (vector){0, 1, 0};
        /* Roll (right, up) by `bank` around tangent. For a left turn,
         * positive bank raises the right edge (banking into the turn). */
        f.right = vector_add(vector_scale(flat_right,  cb),
                             vector_scale(flat_up,     sb));
        f.up    = vector_add(vector_scale(flat_right, -sb),
                             vector_scale(flat_up,     cb));
        return f;
    }

    /* Past the arc — compute the arc-exit frame once, then place the
     * remaining straight-2 / corkscrew / straight-3 sections relative
     * to it. The exit frame is back to flat (no bank) since we eased
     * banking down across the arc. */
    float ca = cosf(TRACK_TURN_ANGLE), sa = sinf(TRACK_TURN_ANGLE);
    vector arc_end_pos = {
        -TRACK_TURN_RADIUS + TRACK_TURN_RADIUS * ca,
         TRACK_Y,
         TRACK_STRAIGHT1 + TRACK_TURN_RADIUS * sa
    };
    vector arc_end_tan   = {-sa, 0, ca};
    vector arc_end_right = { ca, 0, sa};
    vector flat_up       = {0, 1, 0};

    if (s < TRACK_S_CORK_START) {
        /* Straight 2: flat run from the arc exit. */
        float local_s = s - TRACK_S_STR2_START;
        f.pos     = vector_add(arc_end_pos, vector_scale(arc_end_tan, local_s));
        f.tangent = arc_end_tan;
        f.right   = arc_end_right;
        return f;
    }

    /* Corkscrew start position is at the end of straight 2. */
    vector cork_start_pos = vector_add(arc_end_pos,
                                       vector_scale(arc_end_tan, TRACK_STRAIGHT2));

    if (s < TRACK_S_STR3_START) {
        /* Corkscrew: centerline keeps going along arc_end_tan, but
         * (right, up) rolls 360° around the tangent over CORK_LEN.
         * Lift the centerline slightly in the middle of the roll so
         * the ship has clearance when the track is overhead. */
        float local_s = s - TRACK_S_CORK_START;
        float t       = local_s / TRACK_CORK_LEN;
        float roll    = 2.0f * (float)M_PI * t;
        float cr = cosf(roll), sr = sinf(roll);

        /* Raise centerline in a bell curve so the ship rolls under a
         * track that's lifted above its original height when inverted. */
        float lift = 3.5f * sinf((float)M_PI * t);

        f.pos = vector_add(cork_start_pos, vector_scale(arc_end_tan, local_s));
        f.pos.y += lift;
        f.tangent = arc_end_tan;
        /* Roll the (right, up) basis around the tangent. Start with
         * the arc-exit (right, up) and rotate by `roll`. */
        f.right = vector_add(vector_scale(arc_end_right,  cr),
                             vector_scale(flat_up,        sr));
        f.up    = vector_add(vector_scale(arc_end_right, -sr),
                             vector_scale(flat_up,        cr));
        return f;
    }

    /* Straight 3: continues from the end of the corkscrew (back to
     * flat orientation since roll = 2π = 0 mod 2π). */
    vector str3_start_pos = vector_add(cork_start_pos,
                                       vector_scale(arc_end_tan, TRACK_CORK_LEN));
    float local_s = s - TRACK_S_STR3_START;
    f.pos     = vector_add(str3_start_pos, vector_scale(arc_end_tan, local_s));
    f.tangent = arc_end_tan;
    f.right   = arc_end_right;
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

    /* Track surface — straights are single long OBBs (frame is constant
     * along their length, so no segmentation needed); the arc and the
     * corkscrew are segmented because their frames vary continuously.
     * Tunnel cylinders ride the arc segments. */

    /* Helper macro for the straight sections. */
    #define ADD_STRAIGHT(SMID, LEN) do {                                    \
        track_frame f = track_frame_at(SMID);                               \
        scene_box b = {                                                     \
            .center = vector_add(f.pos, vector_scale(f.up, -0.25f)),        \
            .half_extents = {TRACK_WIDTH * 0.5f, 0.25f, (LEN) * 0.5f},      \
            .ux = f.right, .uy = f.up, .uz = f.tangent,                     \
            .material = m_track,                                            \
        };                                                                  \
        scene_add_box(s, b);                                                \
    } while (0)

    ADD_STRAIGHT(TRACK_STRAIGHT1 * 0.5f, TRACK_STRAIGHT1);

    int arc_segs = (int)(TRACK_ARC_LEN / TRACK_SEG_LEN) + 1;
    float arc_seg_len = TRACK_ARC_LEN / (float)arc_segs;
    float arc_seg_half_z = arc_seg_len * 0.55f;
    for (int i = 0; i < arc_segs; i++) {
        float s_mid = TRACK_S_TURN_START + (i + 0.5f) * arc_seg_len;
        track_frame f = track_frame_at(s_mid);
        scene_box surface = {
            .center = vector_add(f.pos, vector_scale(f.up, -0.25f)),
            .half_extents = {TRACK_WIDTH * 0.5f, 0.25f, arc_seg_half_z},
            .ux = f.right, .uy = f.up, .uz = f.tangent,
            .material = m_track,
        };
        scene_add_box(s, surface);
        scene_add_cylinder(s, (scene_cylinder){
            .center = vector_add(f.pos, vector_scale(f.up, TUNNEL_CENTER_OFF)),
            .axis = f.tangent,
            .radius = TUNNEL_RADIUS,
            .half_height = arc_seg_half_z,
            .material = m_tunnel,
        });
    }

    ADD_STRAIGHT(TRACK_S_STR2_START + TRACK_STRAIGHT2 * 0.5f, TRACK_STRAIGHT2);

    int cork_segs = (int)(TRACK_CORK_LEN / TRACK_SEG_LEN) + 1;
    float cork_seg_len = TRACK_CORK_LEN / (float)cork_segs;
    float cork_seg_half_z = cork_seg_len * 0.55f;
    for (int i = 0; i < cork_segs; i++) {
        float s_mid = TRACK_S_CORK_START + (i + 0.5f) * cork_seg_len;
        track_frame f = track_frame_at(s_mid);
        scene_box surface = {
            .center = vector_add(f.pos, vector_scale(f.up, -0.25f)),
            .half_extents = {TRACK_WIDTH * 0.5f, 0.25f, cork_seg_half_z},
            .ux = f.right, .uy = f.up, .uz = f.tangent,
            .material = m_track,
        };
        scene_add_box(s, surface);
    }

    ADD_STRAIGHT(TRACK_S_STR3_START + TRACK_STRAIGHT3 * 0.5f, TRACK_STRAIGHT3);
    #undef ADD_STRAIGHT

    /* Emissive strips on the upper-side of the tunnel interior.
     * Reflective cylinder bounces them into the rest of the tube so it
     * doesn't feel dead. */
    for (float sp = TUNNEL_S_START; sp <= TUNNEL_S_END; sp += TUNNEL_LIGHT_SPACE) {
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

    /* Ship parts — record indices and local offsets so we can
     * translate them by the ship position each frame. */
    SHIP.body_r   = 0.55f;
    SHIP.canopy_r = 0.32f;
    SHIP.wing_he  = (vector){0.45f, 0.08f, 0.40f};
    SHIP.body_off     = (vector){0, 0, 0};
    SHIP.canopy_off   = (vector){0, 0.35f, -0.05f};
    SHIP.wing_l_off   = (vector){-0.80f, -0.05f,  0.05f};
    SHIP.wing_r_off   = (vector){ 0.80f, -0.05f,  0.05f};

    SHIP.body = scene_add_sphere(s, (scene_sphere){
        .center = {0, TRACK_Y + SHIP_HOVER_Y, 0},
        .radius = SHIP.body_r, .material = m_ship,
    });
    SHIP.canopy = scene_add_sphere(s, (scene_sphere){
        .center = {0, TRACK_Y + SHIP_HOVER_Y, 0},
        .radius = SHIP.canopy_r, .material = m_canopy,
    });
    SHIP.wing_l = scene_add_box(s, scene_box_obb(
        (vector){0, TRACK_Y + SHIP_HOVER_Y, 0},
        SHIP.wing_he, (vector){0, 0, 0}, m_wing));
    SHIP.wing_r = scene_add_box(s, scene_box_obb(
        (vector){0, TRACK_Y + SHIP_HOVER_Y, 0},
        SHIP.wing_he, (vector){0, 0, 0}, m_wing));

    /* Skybox + sun */
    scene_add_sphere(s, (scene_sphere){
        .center = {0, 0, TRACK_TOTAL * 0.5f},
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

static void update_ship_geometry(scene *s, vector ship_pos,
                                 vector right, vector up, vector tangent) {
    s->spheres[SHIP.body].center =
        local_to_world(ship_pos, right, up, tangent, SHIP.body_off);
    s->spheres[SHIP.canopy].center =
        local_to_world(ship_pos, right, up, tangent, SHIP.canopy_off);

    s->boxes[SHIP.wing_l].center =
        local_to_world(ship_pos, right, up, tangent, SHIP.wing_l_off);
    s->boxes[SHIP.wing_l].ux = right;
    s->boxes[SHIP.wing_l].uy = up;
    s->boxes[SHIP.wing_l].uz = tangent;

    s->boxes[SHIP.wing_r].center =
        local_to_world(ship_pos, right, up, tangent, SHIP.wing_r_off);
    s->boxes[SHIP.wing_r].ux = right;
    s->boxes[SHIP.wing_r].uy = up;
    s->boxes[SHIP.wing_r].uz = tangent;
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
    int    interlace_on  = 1;   /* I toggles; CPU only */
    int    reflections_on = 1;  /* R toggles; swap to procedural tex when off */

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
        ship_s = track_wrap_s(ship_s);

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

        postfx_chromatic_apply(chrom, pixels, render_w, render_h, &chrom_cfg);
        postfx_vignette_apply (pixels, render_w, render_h, &vig_cfg);
        grain_cfg.seed = frame_now;
        postfx_grain_apply    (pixels, render_w, render_h, &grain_cfg);
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
                     boost, ship_s, (float)TRACK_TOTAL,
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
