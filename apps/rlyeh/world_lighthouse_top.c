/* Lantern Room — the keeper's vantage at the top of the lighthouse. You stand
 * on the lantern-room floor, the great lamp turning just behind you, its beam
 * raking out over the sea far below; a cage of glazing bars frames the view,
 * the dawn sun sits low on the horizon. Same "right but subtly wrong" contract
 * as the shore: the wrongness knob (passed to animate) curdles sky/fog/lamp/
 * sun toward the dream and pushes a coral stalk up through the floor.
 *
 * The camera spawns at the world origin (0, EYE_HEIGHT, 0), so the room is
 * built around the origin and the sea is dropped far below (y = -33) to read
 * as height — you look out and down over the water. Pure scene; harness in
 * main.c. */

#include "world.h"
#include "renderer.h"   /* RT_OBJ_KIND_* for the fog skip mask */
#include "mesh.h"       /* rt_scene_build_accel */

#include <math.h>
#include <stdint.h>

#define SEA_Y   (-33.0f)               /* sea surface, far below the room */

/* ===== Animated handles ================================================== */
static int    BEAM_IDX  = -1;          /* rotating beam cone */
static int    BEAM_MAT  = -1;          /* beam/lamp material (recoloured by w) */
static vector BEAM_APEX;               /* lamp position; beam apex pins here */
static int    SUN_MAT   = -1;          /* sun material (recoloured by w) */
static int    SEAM_IDX  = -1;          /* coral stalk that grows with w */
static vector SEAM_BASE;               /* its root on the lantern floor */

/* ===== Build ============================================================= */
static void lighthouse_top_build(scene **out_s, scene_camera **out_cam,
                                 int *out_sky_mat, postfx_fog *out_fog) {
    BEAM_IDX = BEAM_MAT = SUN_MAT = SEAM_IDX = -1;

    scene *s = scene_create();

    /* ===== Materials ===== */
    int m_sky = scene_add_material(s, (scene_material){
        .albedo   = { LH_SKY_HORIZON_R, LH_SKY_HORIZON_G, LH_SKY_HORIZON_B },
        .albedo2  = { LH_SKY_ZENITH_R,  LH_SKY_ZENITH_G,  LH_SKY_ZENITH_B  },
        .tex_kind = SCENE_TEX_GRADIENT, .tex_scale = 1400.0f, .unlit = 1,
    });
    int m_sea = scene_add_material(s, (scene_material){
        .albedo = { 26, 38, 52 }, .reflectivity = 0.55f,
    });
    int m_sun = scene_add_material(s, (scene_material){
        .albedo = { 255, 200, 140 }, .unlit = 1,
    });
    int m_lamp = scene_add_material(s, (scene_material){    /* the glowing lens */
        .albedo = { 255, 240, 205 }, .unlit = 1,
    });
    int m_frame = scene_add_material(s, (scene_material){   /* floor + glazing bars */
        .albedo = { 70, 72, 80 },
    });
    int m_roof = scene_add_material(s, (scene_material){    /* dark-red cap */
        .albedo = { 120, 30, 34 },
    });
    int m_stalk = scene_add_material(s, (scene_material){
        .albedo = { 18, 35, 45 }, .albedo2 = { 70, 140, 150 },
        .tex_kind = SCENE_TEX_MARBLE, .tex_scale = 1.2f,
    });

    /* ===== Sky + sun + sea ===== */
    scene_add_sphere(s, (scene_sphere){
        .center = {0, 0, 0}, .radius = 1700.0f, .material = m_sky,
    });
    SUN_MAT = m_sun;
    scene_add_sphere(s, (scene_sphere){                     /* low on the horizon */
        .center = { 50.0f, 7.0f, 320.0f }, .radius = 18.0f, .material = m_sun,
    });
    scene_add_plane(s, (scene_plane){
        .point = {0, SEA_Y, 0}, .normal = {0, 1, 0}, .material = m_sea,
    });

    /* ===== Lantern room ===== floor box (you stand on its top at y=0), a ring
     * of vertical glazing bars, and the roof cap. Bars are offset 22.5° so a
     * pair straddles the forward (+Z) view instead of blocking its centre. */
    scene_add_box(s, scene_box_aabb((vector){-5.0f, -1.5f, -5.0f},
                                    (vector){ 5.0f,  0.0f,  5.0f}, m_frame));
    for (int i = 0; i < 8; i++) {
        float th = 0.3927f + (float)i * 0.7854f;            /* 22.5° + i*45° */
        float px = 4.5f * cosf(th), pz = 4.5f * sinf(th);
        scene_add_box(s, scene_box_aabb(
            (vector){ px - 0.15f, 0.0f, pz - 0.15f },
            (vector){ px + 0.15f, 4.2f, pz + 0.15f }, m_frame));
    }
    scene_add_cone(s, (scene_cone){                         /* roof: apex up */
        .apex = {0, 6.0f, 0}, .axis = {0, -1, 0},
        .height = 2.0f, .radius = 5.0f, .material = m_roof,
    });

    /* ===== The lamp — a glowing lens just behind you; its beam shoots out the
     * front window. Bloom does the glow (lights are directional-only). */
    vector lamp = { 0.0f, 2.4f, -2.2f };
    scene_add_sphere(s, (scene_sphere){
        .center = lamp, .radius = 1.3f, .material = m_lamp,
    });
    BEAM_MAT  = m_lamp;
    BEAM_APEX = lamp;
    BEAM_IDX  = scene_add_cone(s, (scene_cone){
        .apex = lamp, .axis = vector_normalize((vector){0, -0.12f, 1}),
        .height = 220.0f, .radius = 9.0f, .material = m_lamp,
    });

    /* ===== Seam stalk — coral pushing up through the lantern floor beside you;
     * grows with wrongness (sub-unit nub at w=0.15). */
    SEAM_BASE = (vector){ 2.5f, 0.0f, 1.5f };
    SEAM_IDX  = scene_add_cone(s, (scene_cone){
        .apex = { SEAM_BASE.x, SEAM_BASE.y + 0.25f, SEAM_BASE.z },
        .axis = {0, -1, 0}, .height = 0.25f, .radius = 0.08f, .material = m_stalk,
    });

    /* ===== Lights ===== directional-only; warmth comes from sky/sun/fog. */
    scene_set_ambient(s, 0.34f);
    scene_add_light(s, (scene_light){
        .direction = vector_normalize((vector){0.2f, 0.25f, 1.0f}),
        .intensity = 0.80f,
    });
    scene_add_light(s, (scene_light){               /* camera-side fill */
        .direction = vector_normalize((vector){-0.2f, 0.5f, -1.0f}),
        .intensity = 0.40f,
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
 * Same wrongness response as the shore (sky/fog lerp, lamp + sun cool, beam
 * sweep, seam grows) — see world_lighthouse.c. Kept as its own copy for now;
 * worth folding into a shared lighthouse-atmosphere helper if a third
 * lighthouse view appears. */
static void lighthouse_top_animate(scene *s, int sky_mat, float t_sec, float dt,
                                   vector cam_pos, float w, postfx_fog *fog) {
    (void)dt;
    (void)cam_pos;

    float pulse = SKY_PULSE_BIAS + SKY_PULSE_AMP * sinf(t_sec * SKY_PULSE_RATE);

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

    if (fog) {
        fog->color.r      = (uint8_t)(LH_FOG_R + (14 - LH_FOG_R) * w);
        fog->color.g      = (uint8_t)(LH_FOG_G + (30 - LH_FOG_G) * w);
        fog->color.b      = (uint8_t)(LH_FOG_B + (44 - LH_FOG_B) * w);
        fog->start        = 70.0f * (1.0f - 0.45f * w);
        fog->end          = 430.0f - 150.0f * w;
        fog->max_strength  = 0.80f + 0.20f * w;
    }

    /* Beam sweep — steady at w=0; wrongness adds a wobble + hitch. */
    if (BEAM_IDX >= 0 && BEAM_IDX < s->cone_count) {
        float a = t_sec * 0.6f
                + w * 0.9f * sinf(t_sec * 1.7f)
                + w * 0.4f * sinf(t_sec * 5.3f);
        s->cones[BEAM_IDX].apex = BEAM_APEX;
        s->cones[BEAM_IDX].axis =
            vector_normalize((vector){ sinf(a), -0.12f, cosf(a) });
    }
    /* Lamp/beam warm -> sickly pale as it curdles. */
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
    /* Coral pushing up through the lantern floor. */
    if (SEAM_IDX >= 0 && SEAM_IDX < s->cone_count) {
        float h = 0.25f + w * 5.0f;
        s->cones[SEAM_IDX].apex   = (vector){ SEAM_BASE.x, SEAM_BASE.y + h,
                                              SEAM_BASE.z };
        s->cones[SEAM_IDX].axis   = (vector){ 0.0f, -1.0f, 0.0f };
        s->cones[SEAM_IDX].height = h;
        s->cones[SEAM_IDX].radius = 0.12f * h + 0.05f;
    }
}

const world_api world_lighthouse_top = { "Lantern Room", lighthouse_top_build,
                                         lighthouse_top_animate };
