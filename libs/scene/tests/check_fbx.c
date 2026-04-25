#include <check.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "fbx.h"
#include "scene.h"

/* Fixture: a 21 KB Blender-3.4 FBX containing 4 flat planes at the
 * corners of a 2x2 square on the XZ plane, sharing one white material,
 * no animation. Originally authored for ufbx's tangent-sign regression
 * test. */
static const char *fixture_path(void) {
    static char buf[4096];
    const char *srcdir = getenv("srcdir");
    if (srcdir) snprintf(buf, sizeof(buf), "%s/tests/cube.fbx", srcdir);
    else        snprintf(buf, sizeof(buf), "tests/cube.fbx");
    return buf;
}

/* -------------------------------------------------------------------------
 * These tests cover the parts of the FBX importer we own: the public API
 * contract and scene_anim_sample's keyframe interpolation / wrap / clamp.
 * ufbx's own parsing is out of scope — it has its own test suite upstream.
 * ------------------------------------------------------------------------- */

/* --- scene_load_fbx on a missing file must zero the result and return 0. */
START_TEST(test_load_nonexistent)
{
    scene_fbx_result r;
    /* Pre-poison so we can verify the loader clears it. */
    memset(&r, 0xAB, sizeof(r));
    int ok = scene_load_fbx("/nonexistent/path/does/not/exist.fbx",
                            SCENE_FBX_DEFAULT, &r);
    ck_assert_int_eq(ok, 0);
    ck_assert_ptr_null(r.meshes);
    ck_assert_ptr_null(r.nodes);
    ck_assert_ptr_null(r.materials);
    ck_assert_ptr_null(r.animations);
    ck_assert_int_eq(r.mesh_count, 0);
    ck_assert_int_eq(r.node_count, 0);
    ck_assert_int_eq(r.material_count, 0);
    ck_assert_int_eq(r.animation_count, 0);
    /* Safe on a zeroed result. */
    scene_fbx_result_free(&r);
}
END_TEST

/* --- scene_add_fbx on a missing file must return -1 and leave scene empty. */
START_TEST(test_add_fbx_nonexistent)
{
    scene *s = scene_create();
    ck_assert_ptr_nonnull(s);
    int first = -999;
    int rc = scene_add_fbx(s, "/no/such/file.fbx", SCENE_FBX_DEFAULT, &first);
    ck_assert_int_eq(rc, -1);
    ck_assert_int_eq(s->node_count, 0);
    ck_assert_int_eq(s->mesh_count, 0);
    ck_assert_int_eq(s->animation_count, 0);
    scene_destroy(s);
}
END_TEST

/* Build a scene with one node and a single-channel animation track
 * (pos.x) sampled at three key times. Used by the interpolation tests. */
static scene *build_scene_with_ramp(float t0, float v0,
                                    float t1, float v1,
                                    float t2, float v2) {
    scene *s = scene_create();
    scene_node n;
    n.transform    = scene_transform_identity();
    n.parent_index = -1;
    n.mesh_index   = -1;
    scene_add_node(s, n);

    scene_anim_key *keys = malloc(sizeof(*keys) * 3);
    keys[0] = (scene_anim_key){ t0, v0 };
    keys[1] = (scene_anim_key){ t1, v1 };
    keys[2] = (scene_anim_key){ t2, v2 };

    scene_anim_track *tracks = malloc(sizeof(*tracks));
    tracks[0].node_index = 0;
    tracks[0].channel    = SCENE_ANIM_POS_X;
    tracks[0].keys       = keys;
    tracks[0].key_count  = 3;

    scene_animation anim;
    memset(&anim, 0, sizeof(anim));
    strcpy(anim.name, "ramp");
    anim.duration    = t2;
    anim.tracks      = tracks;
    anim.track_count = 1;
    scene_add_animation(s, anim);
    return s;
}

/* --- linear interp between two keys --- */
START_TEST(test_anim_sample_lerp)
{
    scene *s = build_scene_with_ramp(0.0f, 0.0f, 1.0f, 10.0f, 2.0f, 20.0f);
    const scene_animation *anim = &s->animations[0];

    scene_anim_sample(s, anim, 0.0f, 0);
    ck_assert_float_eq_tol(s->nodes[0].transform.position.x, 0.0f, 1e-5);

    scene_anim_sample(s, anim, 0.5f, 0);
    ck_assert_float_eq_tol(s->nodes[0].transform.position.x, 5.0f, 1e-5);

    scene_anim_sample(s, anim, 1.0f, 0);
    ck_assert_float_eq_tol(s->nodes[0].transform.position.x, 10.0f, 1e-5);

    scene_anim_sample(s, anim, 1.75f, 0);
    ck_assert_float_eq_tol(s->nodes[0].transform.position.x, 17.5f, 1e-5);

    scene_destroy(s);
}
END_TEST

/* --- sampling past duration with loop=0 clamps to last key --- */
START_TEST(test_anim_sample_clamp)
{
    scene *s = build_scene_with_ramp(0.0f, 0.0f, 1.0f, 10.0f, 2.0f, 20.0f);
    const scene_animation *anim = &s->animations[0];

    scene_anim_sample(s, anim, 5.0f, 0);
    ck_assert_float_eq_tol(s->nodes[0].transform.position.x, 20.0f, 1e-5);

    scene_anim_sample(s, anim, -3.0f, 0);
    ck_assert_float_eq_tol(s->nodes[0].transform.position.x, 0.0f, 1e-5);

    scene_destroy(s);
}
END_TEST

/* --- sampling past duration with loop=1 wraps modulo duration --- */
START_TEST(test_anim_sample_loop)
{
    scene *s = build_scene_with_ramp(0.0f, 0.0f, 1.0f, 10.0f, 2.0f, 20.0f);
    const scene_animation *anim = &s->animations[0];

    /* t=2.5 with duration=2 → effective t=0.5 → value=5. */
    scene_anim_sample(s, anim, 2.5f, 1);
    ck_assert_float_eq_tol(s->nodes[0].transform.position.x, 5.0f, 1e-5);

    /* Negative t wraps too: t=-0.5 → effective t=1.5 → value=15. */
    scene_anim_sample(s, anim, -0.5f, 1);
    ck_assert_float_eq_tol(s->nodes[0].transform.position.x, 15.0f, 1e-5);

    scene_destroy(s);
}
END_TEST

/* --- scene_anim_sample leaves untracked channels alone --- */
START_TEST(test_anim_sample_leaves_untracked_channels)
{
    scene *s = build_scene_with_ramp(0.0f, 0.0f, 1.0f, 10.0f, 2.0f, 20.0f);
    const scene_animation *anim = &s->animations[0];

    /* Pre-seed the unrelated channels to a recognizable sentinel. */
    s->nodes[0].transform.position.y = 42.0f;
    s->nodes[0].transform.rotation.z = 1.23f;

    scene_anim_sample(s, anim, 0.5f, 0);

    ck_assert_float_eq_tol(s->nodes[0].transform.position.y, 42.0f, 1e-5);
    ck_assert_float_eq_tol(s->nodes[0].transform.rotation.z, 1.23f, 1e-5);
    ck_assert_float_eq_tol(s->nodes[0].transform.position.x,  5.0f, 1e-5);

    scene_destroy(s);
}
END_TEST

/* --- scene_load_fbx on the real fixture produces expected shape ----- */
START_TEST(test_load_fixture)
{
    scene_fbx_result r;
    int ok = scene_load_fbx(fixture_path(), SCENE_FBX_DEFAULT, &r);
    ck_assert_int_eq(ok, 1);
    ck_assert_int_eq(r.mesh_count, 4);
    ck_assert_int_eq(r.node_count, 4);
    ck_assert_int_eq(r.material_count, 1);
    ck_assert_int_eq(r.animation_count, 0);

    /* Every mesh is a single quad: 2 triangles × 3 verts = 6. */
    for (int i = 0; i < r.mesh_count; i++) {
        ck_assert_int_eq(r.meshes[i].vertex_count, 6);
        ck_assert_int_eq(r.meshes[i].index_count,  6);
        ck_assert_int_eq(r.meshes[i].material_index, 0);
        ck_assert(r.meshes[i].vertices != NULL);
        ck_assert(r.meshes[i].indices  != NULL);
    }
    /* All four planes are top-level (parented to FBX root, which we strip). */
    for (int i = 0; i < r.node_count; i++) {
        ck_assert_int_eq(r.nodes[i].parent_index, -1);
        ck_assert_int_ge(r.nodes[i].mesh_index,    0);
    }
    /* Nodes sit at the four corners of a 2x2 square on the XZ plane. */
    int corners_seen = 0;
    for (int i = 0; i < r.node_count; i++) {
        float x = r.nodes[i].transform.position.x;
        float z = r.nodes[i].transform.position.z;
        if (fabsf(fabsf(x) - 2.0f) < 0.01f &&
            fabsf(fabsf(z) - 2.0f) < 0.01f) corners_seen++;
    }
    ck_assert_int_eq(corners_seen, 4);

    /* Names come through verbatim from the FBX. */
    const char *expected[] = { "Positive", "FlipX", "FlipY", "FlipXY" };
    for (int k = 0; k < 4; k++) {
        int found = 0;
        for (int i = 0; i < r.node_count; i++) {
            if (strcmp(r.nodes[i].name, expected[k]) == 0) { found = 1; break; }
        }
        ck_assert_msg(found, "expected node name \"%s\" not in fixture",
                      expected[k]);
    }

    scene_fbx_result_free(&r);
}
END_TEST

/* --- scene_find_node_by_name covers hit/miss/empty/NULL paths ------- */
START_TEST(test_find_node_by_name)
{
    scene *s = scene_create();
    scene_add_fbx(s, fixture_path(), SCENE_FBX_DEFAULT, NULL);

    ck_assert_int_ge(scene_find_node_by_name(s, "Positive"), 0);
    ck_assert_int_ge(scene_find_node_by_name(s, "FlipXY"),   0);
    ck_assert_int_eq(scene_find_node_by_name(s, "NoSuch"),  -1);
    ck_assert_int_eq(scene_find_node_by_name(s, ""),        -1);
    ck_assert_int_eq(scene_find_node_by_name(s, NULL),      -1);

    scene_destroy(s);
}
END_TEST

/* --- ANIMATION_ONLY on a clip-less fixture is a no-op (not an error). */
START_TEST(test_animation_only_noop)
{
    scene *s = scene_create();
    /* First load the rig so names exist. */
    int added_rig = scene_add_fbx(s, fixture_path(), SCENE_FBX_DEFAULT, NULL);
    ck_assert_int_eq(added_rig, 4);
    int pre_anim = s->animation_count;

    /* Same file re-opened in animation-only mode. Fixture has no clips,
     * so we expect 0 animations added and the rest of the scene unchanged. */
    int added_anim = scene_add_fbx(s, fixture_path(),
                                   SCENE_FBX_ANIMATION_ONLY, NULL);
    ck_assert_int_eq(added_anim, 0);
    ck_assert_int_eq(s->animation_count, pre_anim);
    ck_assert_int_eq(s->node_count, 4);  /* no new nodes */
    ck_assert_int_eq(s->mesh_count, 4);  /* no new meshes */

    scene_destroy(s);
}
END_TEST

/* --- ANIMATION_ONLY rejected by scene_load_fbx (wrong entry point). */
START_TEST(test_animation_only_raw_rejected)
{
    scene_fbx_result r;
    memset(&r, 0xAB, sizeof(r));
    int ok = scene_load_fbx(fixture_path(), SCENE_FBX_ANIMATION_ONLY, &r);
    ck_assert_int_eq(ok, 0);
    ck_assert_ptr_null(r.meshes);
    ck_assert_ptr_null(r.nodes);
    scene_fbx_result_free(&r);
}
END_TEST

/* --- scene_add_fbx appends and returns correct first-node index ------ */
START_TEST(test_add_fbx_fixture)
{
    scene *s = scene_create();
    /* Pre-seed a node so first_node_index_out > 0. */
    scene_node seed;
    seed.transform    = scene_transform_identity();
    seed.parent_index = -1;
    seed.mesh_index   = -1;
    scene_add_node(s, seed);

    int first = -1;
    int added = scene_add_fbx(s, fixture_path(), SCENE_FBX_DEFAULT, &first);
    ck_assert_int_eq(added, 4);
    ck_assert_int_eq(first, 1);
    ck_assert_int_eq(s->node_count, 5);
    ck_assert_int_eq(s->mesh_count, 4);
    ck_assert_int_eq(s->material_count, 1);

    /* Imported nodes' mesh_index must have been shifted by mesh_base (0). */
    for (int i = first; i < s->node_count; i++) {
        ck_assert_int_ge(s->nodes[i].mesh_index, 0);
        ck_assert_int_lt(s->nodes[i].mesh_index, s->mesh_count);
    }
    scene_destroy(s);
}
END_TEST

/* --- scene_destroy frees tracks/keys without leaking (ASAN-bait smoke). */
START_TEST(test_scene_destroy_with_animation)
{
    scene *s = build_scene_with_ramp(0.0f, 1.0f, 1.0f, 2.0f, 2.0f, 3.0f);
    scene_destroy(s);  /* just needs to not crash / leak */
}
END_TEST

static Suite *fbx_suite(void) {
    Suite *suite = suite_create("fbx");

    TCase *tc_api = tcase_create("api");
    tcase_add_test(tc_api, test_load_nonexistent);
    tcase_add_test(tc_api, test_add_fbx_nonexistent);
    suite_add_tcase(suite, tc_api);

    TCase *tc_fixture = tcase_create("fixture");
    tcase_add_test(tc_fixture, test_load_fixture);
    tcase_add_test(tc_fixture, test_add_fbx_fixture);
    tcase_add_test(tc_fixture, test_find_node_by_name);
    tcase_add_test(tc_fixture, test_animation_only_noop);
    tcase_add_test(tc_fixture, test_animation_only_raw_rejected);
    suite_add_tcase(suite, tc_fixture);

    TCase *tc_anim = tcase_create("anim_sample");
    tcase_add_test(tc_anim, test_anim_sample_lerp);
    tcase_add_test(tc_anim, test_anim_sample_clamp);
    tcase_add_test(tc_anim, test_anim_sample_loop);
    tcase_add_test(tc_anim, test_anim_sample_leaves_untracked_channels);
    tcase_add_test(tc_anim, test_scene_destroy_with_animation);
    suite_add_tcase(suite, tc_anim);

    return suite;
}

int main(void) {
    SRunner *runner = srunner_create(fbx_suite());
    srunner_run_all(runner, CK_NORMAL);
    int failed = srunner_ntests_failed(runner);
    srunner_free(runner);
    return failed == 0 ? 0 : 1;
}
