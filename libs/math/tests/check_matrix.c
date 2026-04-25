#include <stdlib.h>
#include <math.h>
#include <check.h>
#include "matrix.h"

#define TOL 1e-4f

static void assert_vec(vector v, float x, float y, float z) {
    ck_assert_float_eq_tol(v.x, x, TOL);
    ck_assert_float_eq_tol(v.y, y, TOL);
    ck_assert_float_eq_tol(v.z, z, TOL);
}

/* --- identity --- */

START_TEST(test_identity_point)
{
    mat4 m = mat4_identity();
    vector p = {3, -7, 2};
    assert_vec(mat4_transform_point(m, p), 3, -7, 2);
}
END_TEST

/* --- translate --- */

START_TEST(test_translate_point_and_dir)
{
    mat4 m = mat4_translate((vector){10, 20, 30});
    assert_vec(mat4_transform_point(m, (vector){1, 2, 3}), 11, 22, 33);
    /* Direction is unaffected by translation. */
    assert_vec(mat4_transform_dir(m, (vector){1, 2, 3}), 1, 2, 3);
}
END_TEST

/* --- rotation: single-axis sanity checks against simple expectations --- */

START_TEST(test_rotate_z_90)
{
    mat4 m = mat4_rotate_xyz((vector){0, 0, (float)M_PI / 2});
    /* +X rotates to +Y under a +90° spin around Z. */
    assert_vec(mat4_transform_point(m, (vector){1, 0, 0}), 0, 1, 0);
    assert_vec(mat4_transform_point(m, (vector){0, 1, 0}), -1, 0, 0);
}
END_TEST

START_TEST(test_rotate_y_90)
{
    mat4 m = mat4_rotate_xyz((vector){0, (float)M_PI / 2, 0});
    /* +X rotates to -Z under +90° around Y. */
    assert_vec(mat4_transform_point(m, (vector){1, 0, 0}), 0, 0, -1);
    assert_vec(mat4_transform_point(m, (vector){0, 0, 1}), 1, 0, 0);
}
END_TEST

START_TEST(test_rotate_x_90)
{
    mat4 m = mat4_rotate_xyz((vector){(float)M_PI / 2, 0, 0});
    assert_vec(mat4_transform_point(m, (vector){0, 1, 0}), 0, 0, 1);
    assert_vec(mat4_transform_point(m, (vector){0, 0, 1}), 0, -1, 0);
}
END_TEST

/* --- composition: combined rotation matches Rx * Ry * Rz --- */

START_TEST(test_rotate_xyz_order)
{
    /* When all three axes get the same angle, the result should match
     * the explicit composition Rx * Ry * Rz. */
    vector e = {0.3f, -0.7f, 1.1f};
    mat4 combined = mat4_rotate_xyz(e);
    mat4 rx = mat4_rotate_xyz((vector){e.x, 0, 0});
    mat4 ry = mat4_rotate_xyz((vector){0, e.y, 0});
    mat4 rz = mat4_rotate_xyz((vector){0, 0, e.z});
    mat4 expected = mat4_mul(rx, mat4_mul(ry, rz));

    vector p = {0.5f, -1.2f, 0.8f};
    vector a = mat4_transform_point(combined, p);
    vector b = mat4_transform_point(expected, p);
    assert_vec(a, b.x, b.y, b.z);
}
END_TEST

/* --- TRS composition --- */

START_TEST(test_trs_applies_scale_then_rotate_then_translate)
{
    vector t = {5, 6, 7};
    vector e = {0, 0, (float)M_PI / 2};
    vector s = {2, 3, 4};
    mat4 m = mat4_trs(t, e, s);

    /* p = (1,0,0). Scale -> (2,0,0). Rotate Z 90° -> (0,2,0). Translate -> (5,8,7). */
    assert_vec(mat4_transform_point(m, (vector){1, 0, 0}), 5, 8, 7);
    /* p = (0,1,0). Scale -> (0,3,0). Rotate Z 90° -> (-3,0,0). Translate -> (2,6,7). */
    assert_vec(mat4_transform_point(m, (vector){0, 1, 0}), 2, 6, 7);
}
END_TEST

/* --- inverse round-trips a TRS --- */

START_TEST(test_affine_inverse_round_trip)
{
    mat4 m = mat4_trs((vector){2.5f, -1, 4},
                      (vector){0.4f, -0.9f, 1.2f},
                      (vector){1.5f, 0.8f, 2.0f});
    mat4 inv = mat4_affine_inverse(m);

    vector p = {1.1f, -0.3f, 2.7f};
    vector roundtrip = mat4_transform_point(inv, mat4_transform_point(m, p));
    assert_vec(roundtrip, p.x, p.y, p.z);

    /* Direction round-trip too — translation must drop out. */
    vector d = {-0.5f, 0.7f, 0.3f};
    vector dr = mat4_transform_dir(inv, mat4_transform_dir(m, d));
    assert_vec(dr, d.x, d.y, d.z);
}
END_TEST

/* --- normal transform: uniform scale matches inverse-transpose --- */

START_TEST(test_transform_normal_through_rotation)
{
    mat4 m = mat4_rotate_xyz((vector){0, 0, (float)M_PI / 2});
    mat4 inv = mat4_affine_inverse(m);
    vector n = {1, 0, 0};
    /* For a pure rotation, transform_normal(M^-1, n) == M * n. */
    vector got = mat4_transform_normal(inv, n);
    /* Normalize and compare. */
    float mag = sqrtf(got.x*got.x + got.y*got.y + got.z*got.z);
    got.x /= mag; got.y /= mag; got.z /= mag;
    assert_vec(got, 0, 1, 0);
}
END_TEST

static Suite *matrix_suite(void) {
    Suite *s = suite_create("matrix");
    TCase *tc = tcase_create("core");
    tcase_add_test(tc, test_identity_point);
    tcase_add_test(tc, test_translate_point_and_dir);
    tcase_add_test(tc, test_rotate_z_90);
    tcase_add_test(tc, test_rotate_y_90);
    tcase_add_test(tc, test_rotate_x_90);
    tcase_add_test(tc, test_rotate_xyz_order);
    tcase_add_test(tc, test_trs_applies_scale_then_rotate_then_translate);
    tcase_add_test(tc, test_affine_inverse_round_trip);
    tcase_add_test(tc, test_transform_normal_through_rotation);
    suite_add_tcase(s, tc);
    return s;
}

int main(void) {
    SRunner *sr = srunner_create(matrix_suite());
    srunner_run_all(sr, CK_NORMAL);
    int failed = srunner_ntests_failed(sr);
    srunner_free(sr);
    return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
