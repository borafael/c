/* Lighthouse — the "waking" world. Same primitive vocabulary as R'lyeh,
 * wired to read as calm and human: a reflective sea, a rocky islet, a white
 * tower with a glowing lantern + sweeping beam, a low sun. It is meant to
 * feel right but be subtly wrong from the first frame — the harness's
 * wrongness knob (passed into lighthouse_animate) lerps the whole atmosphere
 * toward the R'lyeh dream as it rises. Pure scene; harness lives in main.c. */

#include "world.h"
#include "renderer.h"   /* RT_OBJ_KIND_* for the fog skip mask */
#include "mesh.h"       /* rt_scene_build_accel */

#include <math.h>
#include <stdint.h>

/* ===== Animated handles ================================================== *
 * Set in lighthouse_build(); -1 means "not in the current scene". The build
 * resets them so a rebuild can't read stale indices into a freed scene. */
static int    BEAM_IDX  = -1;          /* rotating beam cone */
static int    BEAM_MAT  = -1;          /* beam material (recoloured by w) */
static vector BEAM_APEX;               /* lantern position; beam apex pins here */
static int    SUN_MAT   = -1;          /* sun material (recoloured by w) */
static int    SEAM_IDX  = -1;          /* coral stalk that grows with w */
static vector SEAM_BASE;               /* its root on the islet */

/* ===== Build ============================================================= */
static void lighthouse_build(scene **out_s, scene_camera **out_cam,
                             int *out_sky_mat, postfx_fog *out_fog) {
    BEAM_IDX = BEAM_MAT = SUN_MAT = SEAM_IDX = -1;

    scene *s = scene_create();

    /* ===== Materials ===== */
    int m_sky = scene_add_material(s, (scene_material){
        .albedo   = { LH_SKY_HORIZON_R, LH_SKY_HORIZON_G, LH_SKY_HORIZON_B },
        .albedo2  = { LH_SKY_ZENITH_R,  LH_SKY_ZENITH_G,  LH_SKY_ZENITH_B  },
        .tex_kind = SCENE_TEX_GRADIENT,
        .tex_scale = 1400.0f,
        .unlit = 1,
    });
    int m_sea = scene_add_material(s, (scene_material){
        .albedo = { 26, 38, 52 }, .reflectivity = 0.55f,   /* wet dark mirror */
    });
    int m_rock = scene_add_material(s, (scene_material){
        .albedo = { 44, 46, 50 }, .albedo2 = { 24, 26, 30 },
        .tex_kind = SCENE_TEX_MARBLE, .tex_scale = 3.0f,
    });
    int m_tower = scene_add_material(s, (scene_material){
        .albedo = { 232, 230, 230 },                       /* whitewashed */
    });
    int m_gallery = scene_add_material(s, (scene_material){
        .albedo = { 120, 30, 34 },                         /* dark-red cap */
    });
    int m_lantern = scene_add_material(s, (scene_material){
        .albedo = { 255, 238, 200 }, .unlit = 1,           /* the lamp glows */
    });
    int m_beam = scene_add_material(s, (scene_material){
        .albedo = { 255, 240, 205 }, .unlit = 1,
    });
    int m_sun = scene_add_material(s, (scene_material){
        .albedo = { 255, 200, 140 }, .unlit = 1,
    });
    int m_stalk = scene_add_material(s, (scene_material){   /* same coral as R'lyeh */
        .albedo   = { 18, 35, 45 }, .albedo2 = { 70, 140, 150 },
        .tex_kind = SCENE_TEX_MARBLE, .tex_scale = 1.2f,
    });

    /* ===== Sky + sun ===== */
    scene_add_sphere(s, (scene_sphere){
        .center = {0, 0, 0}, .radius = 1700.0f, .material = m_sky,
    });
    SUN_MAT = m_sun;
    scene_add_sphere(s, (scene_sphere){
        .center = { 70.0f, 26.0f, 300.0f }, .radius = 18.0f, .material = m_sun,
    });

    /* ===== Sea — infinite reflective plane, set a little below the rock so
     * the shelf you stand on rises out of it; fog melts it into the horizon. */
    scene_add_plane(s, (scene_plane){
        .point = {0, -1.0f, 0}, .normal = {0, 1, 0}, .material = m_sea,
    });

    /* ===== Near shelf — the wave-cut rock you wake standing on. A flat-top
     * slab whose top sits at y=0 (where the camera's feet are), with a couple
     * of rounded boulders to break the straight edge. It has to be a box, not
     * a cylinder: cylinders here are lateral-only (no end cap), so you'd see
     * straight through the top of a cylinder you stood on. */
    scene_add_box(s, scene_box_aabb((vector){-16.0f, -4.0f, -14.0f},
                                    (vector){ 16.0f,  0.0f,  18.0f}, m_rock));
    scene_add_sphere(s, (scene_sphere){
        .center = { 13.0f, -0.5f, 13.0f }, .radius = 3.0f, .material = m_rock,
    });
    scene_add_sphere(s, (scene_sphere){
        .center = { -12.0f, -1.0f, 8.0f }, .radius = 4.0f, .material = m_rock,
    });

    /* ===== Lighthouse island — rounded rock across the water that the tower
     * stands on; you look out to it over the reflective channel. */
    scene_add_sphere(s, (scene_sphere){
        .center = { 0.0f, -9.0f, 80.0f }, .radius = 13.0f, .material = m_rock,
    });
    scene_add_sphere(s, (scene_sphere){
        .center = { 9.0f, -8.0f, 86.0f }, .radius = 8.0f, .material = m_rock,
    });

    /* ===== Lighthouse — tower + gallery + lantern + roof on its island
     * across the water, far enough that the whole silhouette frames up. */
    scene_add_cylinder(s, (scene_cylinder){
        .center = {0, 16.0f, 80.0f}, .axis = {0, 1, 0},
        .radius = 3.4f, .half_height = 16.0f, .material = m_tower,
    });
    scene_add_cylinder(s, (scene_cylinder){
        .center = {0, 32.6f, 80.0f}, .axis = {0, 1, 0},
        .radius = 4.3f, .half_height = 1.1f, .material = m_gallery,
    });
    vector lantern = { 0.0f, 34.4f, 80.0f };
    scene_add_sphere(s, (scene_sphere){
        .center = lantern, .radius = 2.2f, .material = m_lantern,
    });
    scene_add_cone(s, (scene_cone){                 /* roof: apex up, opens down */
        .apex = {0, 38.4f, 80.0f}, .axis = {0, -1, 0},
        .height = 3.4f, .radius = 4.3f, .material = m_gallery,
    });

    /* ===== Beam — bright unlit shaft from the lantern; lighthouse_animate
     * pins the apex here and sweeps the axis (bloom does the glowing). */
    BEAM_MAT  = m_beam;
    BEAM_APEX = lantern;
    BEAM_IDX  = scene_add_cone(s, (scene_cone){
        .apex = lantern, .axis = vector_normalize((vector){0, -0.16f, 1}),
        .height = 200.0f, .radius = 8.0f, .material = m_beam,
    });

    /* ===== Seam stalk — one coral cone that grows with wrongness, planted on
     * the shelf right beside you so the dream's intrusion is in your own
     * space. A sub-unit nub at w=0.15; taller as it curdles. */
    SEAM_BASE = (vector){ 6.0f, 0.0f, 8.0f };
    SEAM_IDX  = scene_add_cone(s, (scene_cone){
        .apex = { SEAM_BASE.x, SEAM_BASE.y + 0.25f, SEAM_BASE.z },
        .axis = {0, -1, 0}, .height = 0.25f, .radius = 0.08f, .material = m_stalk,
    });

    /* ===== Lights ===== (directional-only engine; warmth is carried by the
     * sky gradient, the sun disc, and the fog — not the light colour.) */
    scene_set_ambient(s, 0.32f);
    scene_add_light(s, (scene_light){               /* key, roughly from the sun */
        .direction = vector_normalize((vector){0.18f, 0.30f, 1.0f}),
        .intensity = 0.80f,
    });
    scene_add_light(s, (scene_light){               /* camera-side fill so the
                                                     * tower's near face reads
                                                     * white, not backlit grey */
        .direction = vector_normalize((vector){-0.2f, 0.45f, -1.0f}),
        .intensity = 0.45f,
    });

    rt_scene_build_accel(s);

    *out_s   = s;
    *out_cam = scene_camera_create(
        (vector){0.0f, EYE_HEIGHT, 0.0f}, (vector){0.0f, 0.0f, 1.0f});
    if (out_sky_mat) *out_sky_mat = m_sky;
    if (out_fog) *out_fog = (postfx_fog){
        .enabled = 1,
        .color   = { LH_FOG_R, LH_FOG_G, LH_FOG_B },
        .start = 70.0f, .end = 430.0f, .max_strength = 0.80f,
        .skip_kinds_mask = (1u << RT_OBJ_KIND_SKY) | (1u << RT_OBJ_KIND_SPHERE),
    };
}

/* ===== Animate =========================================================== *
 * Everything wrongness touches lives here. At w=0 the world is the calm dawn
 * laid down by build(); as w rises, sky + fog lerp toward the R'lyeh murk, the
 * beam wobbles and cools, the sun cools, and the seam stalk pushes up. */
static void lighthouse_animate(scene *s, int sky_mat, float t_sec, float dt,
                               vector cam_pos, float w, postfx_fog *fog) {
    (void)dt;          /* the lighthouse animates off absolute time, not dt */
    (void)cam_pos;     /* nothing here tracks the player */

    float pulse = SKY_PULSE_BIAS + SKY_PULSE_AMP * sinf(t_sec * SKY_PULSE_RATE);

    /* Sky: dawn breathes, and wrongness lerps the gradient toward the dream's
     * night palette. */
    if (sky_mat >= 0) {
        #define LH_LERP8(a, b) \
            (uint8_t)(((float)(a) + ((float)(b) - (float)(a)) * w) * pulse)
        s->materials[sky_mat].albedo.r  = LH_LERP8(LH_SKY_HORIZON_R, SKY_HORIZON_R);
        s->materials[sky_mat].albedo.g  = LH_LERP8(LH_SKY_HORIZON_G, SKY_HORIZON_G);
        s->materials[sky_mat].albedo.b  = LH_LERP8(LH_SKY_HORIZON_B, SKY_HORIZON_B);
        s->materials[sky_mat].albedo2.r = LH_LERP8(LH_SKY_ZENITH_R,  SKY_ZENITH_R);
        s->materials[sky_mat].albedo2.g = LH_LERP8(LH_SKY_ZENITH_G,  SKY_ZENITH_G);
        s->materials[sky_mat].albedo2.b = LH_LERP8(LH_SKY_ZENITH_B,  SKY_ZENITH_B);
        #undef LH_LERP8
    }

    /* Fog: warm dawn haze -> teal murk, pulling in and thickening. */
    if (fog) {
        fog->color.r      = (uint8_t)(LH_FOG_R + (14 - LH_FOG_R) * w);
        fog->color.g      = (uint8_t)(LH_FOG_G + (30 - LH_FOG_G) * w);
        fog->color.b      = (uint8_t)(LH_FOG_B + (44 - LH_FOG_B) * w);
        fog->start        = 70.0f * (1.0f - 0.45f * w);
        fog->end          = 430.0f - 150.0f * w;
        fog->max_strength = 0.80f + 0.20f * w;
    }

    /* Beam sweep — a steady turn at w=0; wrongness layers on a slow wobble and
     * a faster hitch so the cadence stops being a reassuring clock. */
    if (BEAM_IDX >= 0 && BEAM_IDX < s->cone_count) {
        float a = t_sec * 0.6f
                + w * 0.9f * sinf(t_sec * 1.7f)
                + w * 0.4f * sinf(t_sec * 5.3f);
        s->cones[BEAM_IDX].apex = BEAM_APEX;
        s->cones[BEAM_IDX].axis =
            vector_normalize((vector){ sinf(a), -0.16f, cosf(a) });
    }
    /* Warm lamp light cools toward a sickly pale as it curdles. */
    if (BEAM_MAT >= 0) {
        s->materials[BEAM_MAT].albedo.r = (uint8_t)(255 + (200 - 255) * w);
        s->materials[BEAM_MAT].albedo.g = (uint8_t)(240 + (232 - 240) * w);
        s->materials[BEAM_MAT].albedo.b = (uint8_t)(205 + (255 - 205) * w);
    }
    if (SUN_MAT >= 0) {
        s->materials[SUN_MAT].albedo.r = (uint8_t)(255 + (170 - 255) * w);
        s->materials[SUN_MAT].albedo.g = (uint8_t)(200 + (196 - 200) * w);
        s->materials[SUN_MAT].albedo.b = (uint8_t)(140 + (214 - 140) * w);
    }
    /* A coral stalk pushes up through the safe ground — the dream bleeding in.
     * Sub-unit nub at low w, several units tall at w=1. */
    if (SEAM_IDX >= 0 && SEAM_IDX < s->cone_count) {
        float h = 0.25f + w * 6.0f;
        s->cones[SEAM_IDX].apex   = (vector){ SEAM_BASE.x, SEAM_BASE.y + h,
                                              SEAM_BASE.z };
        s->cones[SEAM_IDX].axis   = (vector){ 0.0f, -1.0f, 0.0f };
        s->cones[SEAM_IDX].height = h;
        s->cones[SEAM_IDX].radius = 0.12f * h + 0.05f;
    }
}

const world_api world_lighthouse = { "Lighthouse", lighthouse_build,
                                     lighthouse_animate };
