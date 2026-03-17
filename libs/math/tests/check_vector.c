#include <stdlib.h>
#include <check.h>
#include "vector.h"

#define TOL 1e-5f

/* --- add / sub --- */

START_TEST(test_add)
{
    vector a = {1, 2, 3};
    vector b = {4, 5, 6};
    vector c = vector_add(a, b);
    ck_assert_float_eq_tol(c.x, 5.0f, TOL);
    ck_assert_float_eq_tol(c.y, 7.0f, TOL);
    ck_assert_float_eq_tol(c.z, 9.0f, TOL);
}
END_TEST

START_TEST(test_sub)
{
    vector a = {5, 7, 9};
    vector b = {1, 2, 3};
    vector c = vector_sub(a, b);
    ck_assert_float_eq_tol(c.x, 4.0f, TOL);
    ck_assert_float_eq_tol(c.y, 5.0f, TOL);
    ck_assert_float_eq_tol(c.z, 6.0f, TOL);
}
END_TEST

/* --- dot / cross --- */

START_TEST(test_dot_perpendicular)
{
    vector a = {1, 0, 0};
    vector b = {0, 1, 0};
    ck_assert_float_eq_tol(vector_dot(a, b), 0.0f, TOL);
}
END_TEST

START_TEST(test_dot_parallel)
{
    vector a = {2, 0, 0};
    vector b = {3, 0, 0};
    ck_assert_float_eq_tol(vector_dot(a, b), 6.0f, TOL);
}
END_TEST

START_TEST(test_cross_basis)
{
    vector x = {1, 0, 0};
    vector y = {0, 1, 0};
    vector z = vector_cross(x, y);
    ck_assert_float_eq_tol(z.x, 0.0f, TOL);
    ck_assert_float_eq_tol(z.y, 0.0f, TOL);
    ck_assert_float_eq_tol(z.z, 1.0f, TOL);
}
END_TEST

START_TEST(test_cross_anticommutative)
{
    vector a = {1, 2, 3};
    vector b = {4, 5, 6};
    vector ab = vector_cross(a, b);
    vector ba = vector_cross(b, a);
    ck_assert_float_eq_tol(ab.x, -ba.x, TOL);
    ck_assert_float_eq_tol(ab.y, -ba.y, TOL);
    ck_assert_float_eq_tol(ab.z, -ba.z, TOL);
}
END_TEST

/* --- magnitude / scale / normalize --- */

START_TEST(test_magnitude)
{
    vector v = {3, 4, 0};
    ck_assert_float_eq_tol(vector_magnitude(v), 5.0f, TOL);
}
END_TEST

START_TEST(test_scale)
{
    vector v = {1, 2, 3};
    vector s = vector_scale(v, 2.0f);
    ck_assert_float_eq_tol(s.x, 2.0f, TOL);
    ck_assert_float_eq_tol(s.y, 4.0f, TOL);
    ck_assert_float_eq_tol(s.z, 6.0f, TOL);
}
END_TEST

START_TEST(test_normalize)
{
    vector v = vector_normalize((vector){3, 0, 0});
    ck_assert_float_eq_tol(v.x, 1.0f, TOL);
    ck_assert_float_eq_tol(v.y, 0.0f, TOL);
    ck_assert_float_eq_tol(v.z, 0.0f, TOL);
}
END_TEST

START_TEST(test_normalize_unit_length)
{
    vector v = vector_normalize((vector){1, 2, 3});
    ck_assert_float_eq_tol(vector_magnitude(v), 1.0f, TOL);
}
END_TEST

START_TEST(test_normalize_zero)
{
    vector v = vector_normalize((vector){0, 0, 0});
    ck_assert_float_eq_tol(vector_magnitude(v), 0.0f, TOL);
}
END_TEST

/* --- suite --- */

Suite *vector_suite(void)
{
    Suite *s = suite_create("Vector");

    TCase *tc_arith = tcase_create("Arithmetic");
    tcase_add_test(tc_arith, test_add);
    tcase_add_test(tc_arith, test_sub);
    tcase_add_test(tc_arith, test_dot_perpendicular);
    tcase_add_test(tc_arith, test_dot_parallel);
    tcase_add_test(tc_arith, test_cross_basis);
    tcase_add_test(tc_arith, test_cross_anticommutative);
    suite_add_tcase(s, tc_arith);

    TCase *tc_geom = tcase_create("Geometry");
    tcase_add_test(tc_geom, test_magnitude);
    tcase_add_test(tc_geom, test_scale);
    tcase_add_test(tc_geom, test_normalize);
    tcase_add_test(tc_geom, test_normalize_unit_length);
    tcase_add_test(tc_geom, test_normalize_zero);
    suite_add_tcase(s, tc_geom);

    return s;
}

int main(void)
{
    SRunner *sr = srunner_create(vector_suite());
    srunner_run_all(sr, CK_NORMAL);
    int nf = srunner_ntests_failed(sr);
    srunner_free(sr);
    return (nf == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
