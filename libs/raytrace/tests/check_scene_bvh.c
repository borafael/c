/* The CPU backend's per-frame scene BVH must be a pure speed-up: for any
 * scene, rendering with it (RT_CPU_BVH=1, the default) and without it
 * (RT_CPU_BVH=0, every primitive tested linearly) must produce identical
 * pixels AND an identical G-buffer (object ids, depth, normals) — including
 * exact-distance ties, which the BVH breaks in the linear loop's order. */

#include <check.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "renderer.h"
#include "scene.h"
#include "viewport.h"

#define W 160
#define H 120

static uint32_t rng = 1;
static float frand(void) {
    rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
    return (float)(rng & 0xFFFFFF) / (float)0xFFFFFF;
}
static float frange(float a, float b) { return a + (b - a) * frand(); }
static vector rand_unit(void) {
    vector v = {frange(-1, 1), frange(-1, 1), frange(-1, 1)};
    float m = vector_magnitude(v);
    return m > 1e-3f ? vector_scale(v, 1.0f / m) : (vector){0, 1, 0};
}
static vector rand_pos(float r) { return (vector){frange(-r, r), frange(0.2f, r), frange(-r, r)}; }

static int rand_material(scene *s) {
    scene_material m = scene_material_default();
    m.albedo  = (scene_color){(uint8_t)(frand() * 255), (uint8_t)(frand() * 255), (uint8_t)(frand() * 255)};
    m.albedo2 = (scene_color){(uint8_t)(frand() * 255), (uint8_t)(frand() * 255), (uint8_t)(frand() * 255)};
    float k = frand();
    if (k < 0.3f) { m.tex_kind = SCENE_TEX_CHECKER; m.tex_scale = 0.5f; }
    m.reflectivity = frand() < 0.35f ? frange(0.2f, 0.9f) : 0.0f;
    m.unlit = frand() < 0.1f;
    return scene_add_material(s, m);
}

/* A dense random mix of every BVH-managed primitive kind, plus deliberate
 * exact ties: duplicated spheres / boxes (same geometry, different
 * material, different index) and boxes flush with the floor plane. */
static void build_mix(scene *s, int n, float extent) {
    scene_set_background(s, (scene_color){20, 30, 50}, -1);
    scene_set_ambient(s, 0.3f);
    scene_add_light(s, (scene_light){.direction = {0.4f, 0.8f, 0.3f}, .intensity = 0.8f});
    scene_add_plane(s, (scene_plane){.normal = {0, 1, 0}, .point = {0, 0, 0}, .material = rand_material(s)});

    for (int i = 0; i < n; i++) {
        int kind = (int)(frand() * 7.0f);
        int m = rand_material(s);
        vector p = rand_pos(extent);
        switch (kind) {
        case 0: {
            scene_sphere sp = {.center = p, .radius = frange(0.1f, 1.2f), .material = m};
            scene_add_sphere(s, sp);
            if (frand() < 0.2f) { sp.material = rand_material(s); scene_add_sphere(s, sp); }
            break;
        }
        case 1:
            scene_add_disc(s, (scene_disc){.center = p, .normal = rand_unit(),
                                           .radius = frange(0.2f, 1.5f), .material = m});
            break;
        case 2:
            scene_add_cylinder(s, (scene_cylinder){.center = p, .axis = rand_unit(),
                .radius = frange(0.05f, 0.6f), .half_height = frange(0.2f, 2.0f), .material = m});
            break;
        case 3:
            scene_add_cone(s, (scene_cone){.apex = p, .axis = rand_unit(),
                .height = frange(0.3f, 2.0f), .radius = frange(0.1f, 0.8f), .material = m});
            break;
        case 4: {
            float R = frange(0.2f, 1.0f);
            scene_add_torus(s, (scene_torus){.center = p, .axis = rand_unit(),
                .major_radius = R, .minor_radius = R * frange(0.1f, 0.4f), .material = m});
            break;
        }
        case 5:
            scene_add_triangle(s, (scene_triangle){.v0 = p,
                .v1 = vector_add(p, vector_scale(rand_unit(), frange(0.3f, 2.0f))),
                .v2 = vector_add(p, vector_scale(rand_unit(), frange(0.3f, 2.0f))), .material = m});
            break;
        default: {
            vector he = {frange(0.1f, 1.0f), frange(0.1f, 1.0f), frange(0.1f, 1.0f)};
            if (frand() < 0.3f) {
                /* Flush with the floor: top face ties the plane exactly. */
                p.y = -he.y;
                scene_add_box(s, scene_box_aabb(vector_sub(p, he), vector_add(p, he), m));
            } else {
                scene_box b = scene_box_obb(p, he, (vector){frange(0, 3), frange(0, 3), frange(0, 3)}, m);
                scene_add_box(s, b);
                if (frand() < 0.2f) { b.material = rand_material(s); scene_add_box(s, b); }
            }
            break;
        }
        }
    }
}

typedef struct {
    uint32_t px[W * H];
    uint32_t id[W * H];
    float    depth[W * H];
    float    normal[3 * W * H];
} frame;

static void render_with(int use_bvh, const scene *s, const scene_camera *cam, frame *f) {
    setenv("RT_CPU_BVH", use_bvh ? "1" : "0", 1);
    setenv("RT_CPU_THREADS", "4", 1);
    rt_renderer *r = rt_renderer_create(RT_BACKEND_CPU);
    ck_assert_ptr_nonnull(r);
    memset(f, 0, sizeof *f);
    rt_gbuffer g = {f->id, f->depth, f->normal};
    rt_viewport vp = {W, H, (float)M_PI / 3.0f};
    rt_renderer_render(r, s, cam, &vp, f->px, &g);
    rt_renderer_destroy(r);
}

static void assert_same(const scene *s, vector eye, vector look_at) {
    static frame a, b;
    scene_camera *cam = scene_camera_create(eye, vector_normalize(vector_sub(look_at, eye)));
    render_with(0, s, cam, &a);
    render_with(1, s, cam, &b);
    scene_camera_destroy(cam);

    int bad_px = 0, bad_id = 0, first = -1;
    for (int i = 0; i < W * H; i++) {
        if (a.px[i] != b.px[i]) { bad_px++; if (first < 0) first = i; }
        if (a.id[i] != b.id[i]) bad_id++;
    }
    ck_assert_msg(bad_px == 0 && bad_id == 0,
                  "BVH differs from linear: %d pixels, %d object ids (first at %d,%d)",
                  bad_px, bad_id, first % W, first / W);
    ck_assert_int_eq(memcmp(a.depth, b.depth, sizeof a.depth), 0);
    ck_assert_int_eq(memcmp(a.normal, b.normal, sizeof a.normal), 0);
}

START_TEST(test_random_mix_outside)
{
    for (int seed = 1; seed <= 8; seed++) {
        rng = 0x9E3779B9u * (uint32_t)seed;
        scene *s = scene_create();
        build_mix(s, 300, 8.0f);
        assert_same(s, (vector){14, 9, 16}, (vector){0, 2, 0});
        scene_destroy(s);
    }
}
END_TEST

/* Camera inside the cluster: many rays start inside primitives and
 * their bounding boxes. */
START_TEST(test_random_mix_inside)
{
    for (int seed = 11; seed <= 16; seed++) {
        rng = 0x85EBCA6Bu * (uint32_t)seed;
        scene *s = scene_create();
        build_mix(s, 400, 6.0f);
        assert_same(s, (vector){0.3f, 2.5f, -0.4f}, (vector){3, 1, 5});
        scene_destroy(s);
    }
}
END_TEST

/* Eye just above the floor, looking along it: grazing rays and the
 * plane-vs-flush-box ties dominate. */
START_TEST(test_grazing_floor)
{
    rng = 4242;
    scene *s = scene_create();
    build_mix(s, 250, 10.0f);
    assert_same(s, (vector){-12, 0.05f, -12}, (vector){10, 0.3f, 9});
    scene_destroy(s);
}
END_TEST

/* Thousands of small spheres: deep tree, stack depth, leaf packing. */
START_TEST(test_many_spheres)
{
    rng = 777;
    scene *s = scene_create();
    scene_set_ambient(s, 0.3f);
    scene_add_light(s, (scene_light){.direction = {0.3f, 1, 0.2f}, .intensity = 0.8f});
    scene_add_plane(s, (scene_plane){.normal = {0, 1, 0}, .point = {0, 0, 0}, .material = rand_material(s)});
    for (int i = 0; i < 4000; i++)
        scene_add_sphere(s, (scene_sphere){.center = rand_pos(15.0f),
                                           .radius = frange(0.05f, 0.4f), .material = rand_material(s)});
    /* All spheres at one point: the degenerate split path. */
    for (int i = 0; i < 50; i++)
        scene_add_sphere(s, (scene_sphere){.center = {0, 3, 0}, .radius = 0.5f + 0.01f * (float)i,
                                           .material = rand_material(s)});
    assert_same(s, (vector){20, 12, 22}, (vector){0, 3, 0});
    scene_destroy(s);
}
END_TEST

/* No BVH-managed primitives at all (just a plane): must still render. */
START_TEST(test_plane_only)
{
    rng = 5;
    scene *s = scene_create();
    scene_add_plane(s, (scene_plane){.normal = {0, 1, 0}, .point = {0, 0, 0}, .material = rand_material(s)});
    assert_same(s, (vector){0, 2, -5}, (vector){0, 0, 0});
    scene_destroy(s);
}
END_TEST

Suite *scene_bvh_suite(void)
{
    Suite *s = suite_create("SceneBVH");
    TCase *tc = tcase_create("MatchesLinear");
    tcase_set_timeout(tc, 120);
    tcase_add_test(tc, test_random_mix_outside);
    tcase_add_test(tc, test_random_mix_inside);
    tcase_add_test(tc, test_grazing_floor);
    tcase_add_test(tc, test_many_spheres);
    tcase_add_test(tc, test_plane_only);
    suite_add_tcase(s, tc);
    return s;
}

int main(void)
{
    SRunner *sr = srunner_create(scene_bvh_suite());
    srunner_run_all(sr, CK_NORMAL);
    int nf = srunner_ntests_failed(sr);
    srunner_free(sr);
    return (nf == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
