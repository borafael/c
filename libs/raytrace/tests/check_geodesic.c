#include <stdlib.h>
#include <math.h>
#include <check.h>
#include "vector.h"
#include "scene.h"
#include "geodesic.h"

/* --- flat space: with no black holes a photon travels in a straight
 * line, i.e. the curved tracer degenerates exactly to the straight-ray
 * path the rest of the engine uses. --- */
START_TEST(test_flat_space_is_straight)
{
    vector x = {0, 0, 0};
    vector v = {1, 0, 0};
    for (int i = 0; i < 10; i++)
        bh_rk4_step(&x, &v, 2.0f, NULL, 0);
    /* 10 steps of length 2 along +x. */
    ck_assert_float_eq_tol(x.x, 20.0f, 1e-4f);
    ck_assert_float_eq_tol(x.y, 0.0f, 1e-5f);
    ck_assert_float_eq_tol(x.z, 0.0f, 1e-5f);
    /* Velocity untouched. */
    ck_assert_float_eq_tol(v.x, 1.0f, 1e-6f);
    ck_assert_float_eq_tol(v.y, 0.0f, 1e-6f);
    ck_assert_float_eq_tol(v.z, 0.0f, 1e-6f);
}
END_TEST

/* --- angular momentum L = r x v is conserved along a geodesic about a
 * single mass (the property the h² force law relies on). --- */
START_TEST(test_angular_momentum_conserved)
{
    scene_blackhole bh = { .center = {0, 0, 0}, .mass = 1.0f };
    vector x = {-2000, 50, 0};
    vector v = {1, 0, 0};

    vector L0 = vector_cross(x, v);
    float  m0 = vector_magnitude(L0);

    for (int i = 0; i < 4000; i++)
        bh_rk4_step(&x, &v, 1.0f, &bh, 1);

    vector L1 = vector_cross(x, v);
    float  m1 = vector_magnitude(L1);

    /* |L| stays put to well under a percent. */
    ck_assert_float_eq_tol(m1, m0, m0 * 0.01f);
}
END_TEST

/* Integrate a photon with impact parameter b past a mass M and return
 * the total deflection angle (radians). Photon starts far on -x at
 * offset +y = b moving +x; the mass at the origin bends it toward -y. */
static float integrate_deflection(float M, float b, float X0, float h)
{
    scene_blackhole bh = { .center = {0, 0, 0}, .mass = M };
    vector x = {-X0, b, 0};
    vector v = {1, 0, 0};
    int guard = 0;
    while (x.x < X0 && guard < 10000000) {
        bh_rk4_step(&x, &v, h, &bh, 1);
        guard++;
    }
    /* Deflection = angle of the outgoing velocity below the +x axis. */
    return atan2f(-v.y, v.x);
}

/* --- weak-field light bending: Δφ ≈ 4M/b for b ≫ M. This is THE
 * general-relativistic result; Newtonian gravity on a corpuscle gives
 * only 2M/b. We check both that we land near 4M/b and that we are
 * unambiguously above the Newtonian value. --- */
START_TEST(test_weak_field_deflection)
{
    float M = 1.0f, b = 200.0f;
    float expected_gr  = 4.0f * M / b;   /* 0.020 rad */
    float newtonian    = 2.0f * M / b;   /* 0.010 rad */

    float got = integrate_deflection(M, b, 8000.0f, 1.0f);

    /* Within 5% of the GR prediction (finite domain + higher-order
     * terms account for the few-percent residual). */
    ck_assert_float_eq_tol(got, expected_gr, expected_gr * 0.05f);
    /* And clearly the GR value, not the Newtonian half. */
    ck_assert(got > 2.5f * M / b);
    (void)newtonian;
}
END_TEST

/* --- deflection scales like 1/b: doubling the impact parameter halves
 * the bend. --- */
START_TEST(test_deflection_scales_inverse_b)
{
    float M = 1.0f;
    float near = integrate_deflection(M, 200.0f, 8000.0f, 1.0f);
    float far  = integrate_deflection(M, 400.0f, 8000.0f, 1.0f);
    /* near / far ≈ 2. */
    ck_assert_float_eq_tol(near / far, 2.0f, 0.1f);
}
END_TEST

/* --- horizon capture: r_s = 2M. --- */
START_TEST(test_horizon_capture)
{
    scene_blackhole bh = { .center = {0, 0, 0}, .mass = 1.0f }; /* r_s = 2 */
    ck_assert_int_eq(bh_captured((vector){1.5f, 0, 0}, &bh, 1), 1); /* inside */
    ck_assert_int_eq(bh_captured((vector){0, 1.9f, 0}, &bh, 1), 1); /* inside */
    ck_assert_int_eq(bh_captured((vector){5.0f, 0, 0}, &bh, 1), 0); /* outside */
    ck_assert_int_eq(bh_captured((vector){5.0f, 0, 0}, NULL, 0), 0);/* no holes */
}
END_TEST

Suite *geodesic_suite(void)
{
    Suite *s = suite_create("Geodesic");

    TCase *tc = tcase_create("Integration");
    tcase_add_test(tc, test_flat_space_is_straight);
    tcase_add_test(tc, test_angular_momentum_conserved);
    tcase_add_test(tc, test_weak_field_deflection);
    tcase_add_test(tc, test_deflection_scales_inverse_b);
    tcase_add_test(tc, test_horizon_capture);
    suite_add_tcase(s, tc);

    return s;
}

int main(void)
{
    SRunner *sr = srunner_create(geodesic_suite());
    srunner_run_all(sr, CK_NORMAL);
    int nf = srunner_ntests_failed(sr);
    srunner_free(sr);
    return (nf == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
