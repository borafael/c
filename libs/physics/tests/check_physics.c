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


/* ---------- falling + collisions (sphere worlds) ---------- */

/* A world with no mutual gravity: things fall, hit the ground, walls,
 * columns and each other. */
static physics_config arena_config(void) {
    physics_config c = physics_default_config();
    c.max_bodies = 64;
    c.gravity = 0.0f;
    c.merge_on_contact = 0;
    c.fall_gravity = 10.0f;
    c.collisions = 1;
    c.ground = 1;
    c.ground_restitution = 0.5f;
    c.ground_grip = 1.0f;
    c.ground_settle_speed = 0.5f;
    c.roll_friction = 2.0f;
    c.sleep_speed = 0.1f;
    c.walls = 1;
    c.wall_min_x = c.wall_min_z = -10.0f;
    c.wall_max_x = c.wall_max_z =  10.0f;
    c.wall_restitution = 0.5f;
    c.max_substep_dist = 0.05f;
    c.min_substeps = 1;
    c.max_substeps = 256;
    return c;
}

static int add_ball(physics_world *w, vector p, vector v) {
    physics_body_desc d = {.type = PHYSICS_DYNAMIC, .position = p, .velocity = v,
                           .mass = 1.0f, .radius = 0.1f, .restitution = 1.0f};
    return physics_world_add(w, &d);
}

typedef struct { int n[4]; float last_speed; int last_other; } contact_log;
static void log_contact(void *user, physics_world *w, const physics_contact *c) {
    (void)w;
    contact_log *l = user;
    l->n[c->kind]++;
    l->last_speed = c->speed;
    l->last_other = c->other;
}

START_TEST(test_ground_bounce_restitution)
{
    physics_config c = arena_config();
    contact_log log = {0};
    c.on_contact = log_contact;
    c.contact_user = &log;
    physics_world *w = physics_world_create(&c);
    int b = add_ball(w, (vector){0, 0.1f, 0}, (vector){0, -8.0f, 0});
    physics_world_advance(w, 1.0f / 60.0f);
    ck_assert_int_eq(log.n[PHYSICS_CONTACT_GROUND], 1);
    ck_assert_float_ge(physics_world_body_position(w, b).y, 0.1f);
    /* Bounces back up with half the impact speed (restitution 0.5). */
    float vy = physics_world_body_velocity(w, b).y;
    ck_assert_float_eq_tol(vy, 0.5f * log.last_speed, 0.2f);
    ck_assert_float_gt(vy, 3.5f);
    physics_world_destroy(w);
}
END_TEST

START_TEST(test_ball_comes_to_rest_and_stays)
{
    physics_config c = arena_config();
    physics_world *w = physics_world_create(&c);
    int b = add_ball(w, (vector){1, 3, 2}, (vector){2, 0, -1});
    for (int i = 0; i < 600; i++) physics_world_advance(w, 1.0f / 60.0f);
    physics_body s = physics_world_body(w, b);
    ck_assert(s.sleeping);
    ck_assert_float_eq_tol(s.position.y, 0.1f, 1e-3f);
    /* Asleep: another minute changes nothing at all. */
    for (int i = 0; i < 3600; i++) physics_world_advance(w, 1.0f / 60.0f);
    physics_body t = physics_world_body(w, b);
    ck_assert(t.sleeping);
    ck_assert_mem_eq(&s.position, &t.position, sizeof s.position);
    physics_world_destroy(w);
}
END_TEST

/* 110 m/s at a wall, a column and a kinematic sphere: never through. */
START_TEST(test_fast_body_never_tunnels)
{
    physics_column col = {0.0f, 5.0f, 0.3f, 0.5f};
    physics_config c = arena_config();
    c.fall_gravity = 0.0f;
    c.columns = &col;
    c.column_count = 1;
    physics_world *w = physics_world_create(&c);
    int wall = add_ball(w, (vector){0, 1, 0}, (vector){110, 0, 0});
    int column = add_ball(w, (vector){0, 1, 0}, (vector){0, 0, 110});
    physics_body_desc k = {.type = PHYSICS_KINEMATIC, .position = {0, 1, -5},
                           .mass = 10.0f, .radius = 0.3f, .restitution = 0.5f};
    int kin = physics_world_add(w, &k);
    int at_kin = add_ball(w, (vector){0, 1, 0}, (vector){0, 0, -110});
    /* 8 frames: long enough for each to hit its target once (~0.09 s at
     * most), short enough that none has come back to hit something else. */
    for (int i = 0; i < 8; i++) {
        physics_world_advance(w, 1.0f / 60.0f);
        ck_assert_float_le(physics_world_body_position(w, wall).x, 10.0f - 0.1f + 1e-4f);
        ck_assert_float_lt(physics_world_body_position(w, column).z, 5.0f);
        ck_assert_float_gt(physics_world_body_position(w, at_kin).z, -5.0f);
    }
    ck_assert_float_lt(physics_world_body_velocity(w, wall).x, 0.0f);
    ck_assert_float_lt(physics_world_body_velocity(w, column).z, 0.0f);
    ck_assert_float_gt(physics_world_body_velocity(w, at_kin).z, 0.0f);
    /* The world never moves a kinematic body. */
    vector kp = physics_world_body_position(w, kin);
    ck_assert_float_eq(kp.z, -5.0f);
    physics_world_destroy(w);
}
END_TEST

START_TEST(test_sphere_collision_conserves_momentum)
{
    physics_config c = arena_config();
    c.fall_gravity = 0.0f;
    c.ground = 0;
    physics_world *w = physics_world_create(&c);
    physics_body_desc a = {.type = PHYSICS_DYNAMIC, .position = {-1, 2, 0}, .velocity = {4, 0.5f, 0},
                           .mass = 1.0f, .radius = 0.2f, .restitution = 0.8f};
    physics_body_desc b = {.type = PHYSICS_DYNAMIC, .position = {1, 2, 0.05f}, .velocity = {-2, 0, 0},
                           .mass = 3.0f, .radius = 0.2f, .restitution = 0.8f};
    int ia = physics_world_add(w, &a), ib = physics_world_add(w, &b);
    vector p0 = vector_add(vector_scale(a.velocity, a.mass), vector_scale(b.velocity, b.mass));
    for (int i = 0; i < 30; i++) physics_world_advance(w, 1.0f / 60.0f);
    vector va = physics_world_body_velocity(w, ia), vb = physics_world_body_velocity(w, ib);
    vector p1 = vector_add(vector_scale(va, a.mass), vector_scale(vb, b.mass));
    ck_assert_float_eq_tol(p1.x, p0.x, 1e-4f);
    ck_assert_float_eq_tol(p1.y, p0.y, 1e-4f);
    ck_assert_float_eq_tol(p1.z, p0.z, 1e-4f);
    ck_assert_float_lt(va.x, 0.0f);   /* the light one bounced back */
    physics_world_destroy(w);
}
END_TEST

START_TEST(test_contacts_reported)
{
    physics_column col = {3.0f, 0.0f, 0.5f, 0.5f};
    physics_config c = arena_config();
    contact_log log = {0};
    c.columns = &col;
    c.column_count = 1;
    c.on_contact = log_contact;
    c.contact_user = &log;
    physics_world *w = physics_world_create(&c);
    add_ball(w, (vector){0, 0.5f, 0}, (vector){20, 0, 0});    /* column */
    add_ball(w, (vector){0, 0.5f, 3}, (vector){0, 0, 30});    /* +z wall */
    for (int i = 0; i < 20; i++) physics_world_advance(w, 1.0f / 60.0f);
    ck_assert_int_ge(log.n[PHYSICS_CONTACT_COLUMN], 1);
    ck_assert_int_ge(log.n[PHYSICS_CONTACT_WALL], 1);
    ck_assert_int_ge(log.n[PHYSICS_CONTACT_GROUND], 1);
    physics_world_destroy(w);
}
END_TEST

START_TEST(test_disabled_body_is_ignored)
{
    physics_config c = arena_config();
    physics_world *w = physics_world_create(&c);
    int b = add_ball(w, (vector){0, 5, 0}, (vector){1, 0, 0});
    physics_world_set_enabled(w, b, 0);
    for (int i = 0; i < 60; i++) physics_world_advance(w, 1.0f / 60.0f);
    physics_body s = physics_world_body(w, b);
    ck_assert(s.alive && !s.enabled);
    ck_assert_float_eq(s.position.y, 5.0f);
    physics_world_set_enabled(w, b, 1);
    ck_assert_float_eq(physics_world_body(w, b).age, 0.0f);
    physics_world_advance(w, 1.0f / 60.0f);
    ck_assert_float_lt(physics_world_body_position(w, b).y, 5.0f);
    physics_world_destroy(w);
}
END_TEST

/* Same calls, same world: bit-identical state. */
START_TEST(test_deterministic)
{
    physics_world *w[2];
    for (int k = 0; k < 2; k++) {
        physics_config c = arena_config();
        w[k] = physics_world_create(&c);
        for (int i = 0; i < 40; i++)
            add_ball(w[k], (vector){(float)(i % 7) - 3, 1.0f + (float)i * 0.3f, (float)(i % 5) - 2},
                     (vector){(float)(i % 3) * 4 - 4, 0, (float)(i % 4) * 3 - 4});
        for (int s = 0; s < 300; s++) physics_world_advance(w[k], 1.0f / 60.0f);
    }
    for (int i = 0; i < 40; i++) {
        physics_body a = physics_world_body(w[0], i), b = physics_world_body(w[1], i);
        ck_assert_mem_eq(&a, &b, sizeof a);
    }
    physics_world_destroy(w[0]);
    physics_world_destroy(w[1]);
}
END_TEST

START_TEST(test_push_out_columns)
{
    physics_column col = {0.0f, 0.0f, 1.0f, 0.5f};
    physics_config c = arena_config();
    c.columns = &col;
    c.column_count = 1;
    physics_world *w = physics_world_create(&c);
    vector p = {0.5f, 0, 0};
    vector n = physics_world_push_out_columns(w, &p, 0.25f);
    ck_assert_float_eq_tol(p.x, 1.25f, 1e-5f);
    ck_assert_float_eq_tol(n.x, 1.0f, 1e-5f);
    vector q = {5, 0, 5};
    n = physics_world_push_out_columns(w, &q, 0.25f);
    ck_assert_float_eq(n.x, 0.0f);
    ck_assert_float_eq(q.x, 5.0f);
    physics_world_destroy(w);
}
END_TEST

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

    TCase *tc_col = tcase_create("Collisions");
    tcase_add_test(tc_col, test_ground_bounce_restitution);
    tcase_add_test(tc_col, test_ball_comes_to_rest_and_stays);
    tcase_add_test(tc_col, test_fast_body_never_tunnels);
    tcase_add_test(tc_col, test_sphere_collision_conserves_momentum);
    tcase_add_test(tc_col, test_contacts_reported);
    tcase_add_test(tc_col, test_disabled_body_is_ignored);
    tcase_add_test(tc_col, test_deterministic);
    tcase_add_test(tc_col, test_push_out_columns);
    suite_add_tcase(s, tc_col);

    return s;
}

int main(void) {
    SRunner *sr = srunner_create(physics_suite());
    srunner_run_all(sr, CK_NORMAL);
    int nf = srunner_ntests_failed(sr);
    srunner_free(sr);
    return (nf == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
