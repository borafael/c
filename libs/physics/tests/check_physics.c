#include <stdlib.h>
#include <math.h>
#include <check.h>
#include "physics.h"

#define TOL 1e-3f

/* ---------- construction / body lifecycle ---------- */

START_TEST(test_default_config)
{
    physics_config c = physics_default_config();
    ck_assert_int_gt(c.max_bodies, 0);
    ck_assert_int_gt(c.num_threads, 0);
    ck_assert_float_gt(c.gravity, 0.0f);
    ck_assert_float_gt(c.dt, 0.0f);
}
END_TEST

START_TEST(test_create_destroy)
{
    physics_config c = physics_default_config();
    c.max_bodies = 4;
    c.num_threads = 2;
    physics_world *w = physics_world_create(&c);
    ck_assert_ptr_nonnull(w);
    ck_assert_int_eq(physics_world_capacity(w), 4);
    ck_assert_int_eq(physics_world_body_count(w), 0);
    physics_world_destroy(w);
}
END_TEST

START_TEST(test_add_until_full)
{
    physics_config c = physics_default_config();
    c.max_bodies = 3;
    c.num_threads = 1;
    physics_world *w = physics_world_create(&c);

    int a = physics_world_add_body(w, (vector){0,0,0}, (vector){0,0,0}, 1.0f);
    int b = physics_world_add_body(w, (vector){1,0,0}, (vector){0,0,0}, 1.0f);
    int c2 = physics_world_add_body(w, (vector){2,0,0}, (vector){0,0,0}, 1.0f);
    int overflow = physics_world_add_body(w, (vector){3,0,0}, (vector){0,0,0}, 1.0f);

    ck_assert_int_ge(a, 0);
    ck_assert_int_ge(b, 0);
    ck_assert_int_ge(c2, 0);
    ck_assert_int_eq(overflow, -1);
    ck_assert_int_eq(physics_world_body_count(w), 3);
    physics_world_destroy(w);
}
END_TEST

START_TEST(test_remove_frees_slot)
{
    physics_config c = physics_default_config();
    c.max_bodies = 2;
    c.num_threads = 1;
    physics_world *w = physics_world_create(&c);

    int a = physics_world_add_body(w, (vector){0,0,0}, (vector){0,0,0}, 1.0f);
    physics_world_add_body(w, (vector){1,0,0}, (vector){0,0,0}, 1.0f);
    ck_assert_int_eq(physics_world_add_body(w, (vector){2,0,0}, (vector){0,0,0}, 1.0f), -1);

    physics_world_remove_body(w, a);
    ck_assert_int_eq(physics_world_body_alive(w, a), 0);
    ck_assert_int_eq(physics_world_body_count(w), 1);

    int reused = physics_world_add_body(w, (vector){9,9,9}, (vector){0,0,0}, 2.0f);
    ck_assert_int_ge(reused, 0);
    ck_assert_int_eq(physics_world_body_count(w), 2);
    physics_world_destroy(w);
}
END_TEST

START_TEST(test_clear)
{
    physics_config c = physics_default_config();
    c.max_bodies = 4;
    c.num_threads = 1;
    physics_world *w = physics_world_create(&c);
    for (int i = 0; i < 4; i++)
        physics_world_add_body(w, (vector){(float)i,0,0}, (vector){0,0,0}, 1.0f);
    ck_assert_int_eq(physics_world_body_count(w), 4);

    physics_world_clear(w);
    ck_assert_int_eq(physics_world_body_count(w), 0);
    for (int i = 0; i < 4; i++) ck_assert_int_eq(physics_world_body_alive(w, i), 0);

    /* Can add fresh bodies after clear. */
    ck_assert_int_ge(physics_world_add_body(w, (vector){0,0,0}, (vector){0,0,0}, 1.0f), 0);
    physics_world_destroy(w);
}
END_TEST

/* ---------- physics behaviour ---------- */

/* Two equal masses at x=+/-10 should attract along x.
 * After a short step, body0.x should increase and body1.x should decrease. */
START_TEST(test_two_body_attraction)
{
    physics_config c = physics_default_config();
    c.max_bodies = 2;
    c.num_threads = 1;
    c.gravity = 1.0f;
    c.dt = 0.01f;
    c.softening = 0.1f;
    c.merge_on_contact = 0;
    c.bounded = 0;
    physics_world *w = physics_world_create(&c);

    int a = physics_world_add_body(w, (vector){-10,0,0}, (vector){0,0,0}, 100.0f);
    int b = physics_world_add_body(w, (vector){ 10,0,0}, (vector){0,0,0}, 100.0f);

    physics_world_step(w);

    ck_assert_float_gt(physics_world_body_position(w, a).x, -10.0f);
    ck_assert_float_lt(physics_world_body_position(w, b).x,  10.0f);

    /* Symmetric motion — magnitudes should match. */
    float da = physics_world_body_position(w, a).x - (-10.0f);
    float db = 10.0f - physics_world_body_position(w, b).x;
    ck_assert_float_eq_tol(da, db, TOL);
    physics_world_destroy(w);
}
END_TEST

/* Bodies closer than softening merge and conserve momentum / mass. */
START_TEST(test_merge_on_contact)
{
    physics_config c = physics_default_config();
    c.max_bodies = 2;
    c.num_threads = 1;
    c.gravity = 0.0f; /* disable force so we isolate merge behavior */
    c.dt = 0.001f;
    c.softening = 1.0f;
    c.merge_on_contact = 1;
    physics_world *w = physics_world_create(&c);

    int a = physics_world_add_body(w, (vector){ 0,0,0}, (vector){ 1,0,0}, 2.0f);
    int b = physics_world_add_body(w, (vector){0.5f,0,0}, (vector){-1,0,0}, 2.0f);

    physics_world_step(w);

    ck_assert_int_eq(physics_world_body_count(w), 1);
    /* One of the two slots survives; the survivor carries total mass. */
    int survivor = physics_world_body_alive(w, a) ? a : b;
    ck_assert_float_eq_tol(physics_world_body_mass(w, survivor), 4.0f, TOL);
    /* Momentum is 2*1 + 2*(-1) = 0 → survivor velocity.x ~ 0. */
    ck_assert_float_eq_tol(physics_world_body_velocity(w, survivor).x, 0.0f, TOL);
    physics_world_destroy(w);
}
END_TEST

/* Without merge_on_contact, close bodies skip force but stay alive. */
START_TEST(test_softening_without_merge)
{
    physics_config c = physics_default_config();
    c.max_bodies = 2;
    c.num_threads = 1;
    c.gravity = 1.0f;
    c.dt = 0.01f;
    c.softening = 100.0f;
    c.merge_on_contact = 0;
    physics_world *w = physics_world_create(&c);

    physics_world_add_body(w, (vector){ 0,0,0}, (vector){0,0,0}, 1.0f);
    physics_world_add_body(w, (vector){ 1,0,0}, (vector){0,0,0}, 1.0f);

    physics_world_step(w);

    /* No forces → velocities remain zero, positions unchanged. */
    ck_assert_int_eq(physics_world_body_count(w), 2);
    ck_assert_float_eq_tol(physics_world_body_velocity(w, 0).x, 0.0f, TOL);
    ck_assert_float_eq_tol(physics_world_body_velocity(w, 1).x, 0.0f, TOL);
    physics_world_destroy(w);
}
END_TEST

/* A body shot outward past the boundary should be pulled back and have
 * its outward velocity flipped + damped. */
START_TEST(test_boundary_reflection)
{
    physics_config c = physics_default_config();
    c.max_bodies = 1;
    c.num_threads = 1;
    c.gravity = 0.0f;
    c.dt = 1.0f;
    c.softening = 0.01f;
    c.merge_on_contact = 0;
    c.bounded = 1;
    c.world_radius = 10.0f;
    physics_world *w = physics_world_create(&c);

    int id = physics_world_add_body(w, (vector){9, 0, 0}, (vector){4, 0, 0}, 1.0f);

    physics_world_step(w);

    /* After one step, position should be clamped at the boundary and
     * velocity should have flipped sign (outward became inward) and halved. */
    vector p = physics_world_body_position(w, id);
    vector v = physics_world_body_velocity(w, id);
    ck_assert_float_eq_tol(vector_magnitude(p), 10.0f, TOL);
    ck_assert_float_lt(v.x, 0.0f);
    physics_world_destroy(w);
}
END_TEST

/* ---------- suite ---------- */

Suite *physics_suite(void) {
    Suite *s = suite_create("Physics");

    TCase *tc_life = tcase_create("Lifecycle");
    tcase_add_test(tc_life, test_default_config);
    tcase_add_test(tc_life, test_create_destroy);
    tcase_add_test(tc_life, test_add_until_full);
    tcase_add_test(tc_life, test_remove_frees_slot);
    tcase_add_test(tc_life, test_clear);
    suite_add_tcase(s, tc_life);

    TCase *tc_sim = tcase_create("Simulation");
    tcase_add_test(tc_sim, test_two_body_attraction);
    tcase_add_test(tc_sim, test_merge_on_contact);
    tcase_add_test(tc_sim, test_softening_without_merge);
    tcase_add_test(tc_sim, test_boundary_reflection);
    suite_add_tcase(s, tc_sim);

    return s;
}

int main(void) {
    SRunner *sr = srunner_create(physics_suite());
    srunner_run_all(sr, CK_NORMAL);
    int nf = srunner_ntests_failed(sr);
    srunner_free(sr);
    return (nf == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
