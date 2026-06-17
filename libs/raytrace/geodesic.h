#ifndef RT_GEODESIC_H
#define RT_GEODESIC_H

#include <math.h>
#include "vector.h"
#include "scene.h"

/* =========================================================================
 * libs/raytrace/geodesic.h — null-geodesic photon integration.
 *
 * Pure math: no scene traversal, no pixels, no allocation. This is the
 * physics half of the curved-ray (gravitational-lensing) tracer, kept
 * standalone so it can be unit-tested in isolation and mirrored verbatim
 * in a future GLSL port (the same parity discipline libs/raytrace/cpu/
 * torus.c documents for its SDF).
 *
 * A photon is a state (position x, velocity v). In flat space dv/dλ = 0
 * and x(λ) = x0 + λ v0 — exactly the straight ray every other renderer
 * traces. Near a mass it follows a Schwarzschild null geodesic, which in
 * the orbital plane obeys
 *
 *     d²u/dφ² + u = 3 M u²        (u = 1/r,  G = c = 1)
 *
 * The 3 M u² term is the general-relativistic correction; without it
 * (Newtonian gravity acting on light) you get only half the bending.
 * The equivalent Cartesian central-force law that reproduces this orbit
 * is, summed over every black hole i:
 *
 *     a(x, v) = Σ_i  −3 M_i · h_i² · (x − c_i) / r_i⁵ ,
 *               h_i = |(x − c_i) × v|,   r_i = |x − c_i|
 *
 * h_i² is the squared specific angular momentum about hole i. For a
 * single hole (x − c) × v is conserved along the geodesic, so evaluating
 * h² from the current state each step is self-consistent and needs no
 * per-ray precompute; with several holes it is the standard
 * superposition approximation.
 *
 * Weak-field check (the unit test): a ray with impact parameter b ≫ M
 * past a single mass M is deflected by Δφ ≈ 4M/b. The event horizon is
 * r_s = 2M; a path that crosses it is captured (rendered black).
 * ========================================================================= */

/* Photon acceleration at (x, v) from all `count` black holes. */
static inline vector bh_accel(vector x, vector v,
                              const scene_blackhole *bhs, int count) {
    vector a = {0.0f, 0.0f, 0.0f};
    for (int i = 0; i < count; i++) {
        vector d  = vector_sub(x, bhs[i].center);
        float  r2 = vector_dot(d, d);
        if (r2 < 1e-12f) continue;            /* at the singularity — skip */
        vector L  = vector_cross(d, v);
        float  h2 = vector_dot(L, L);
        float  r  = sqrtf(r2);
        float  inv_r5 = 1.0f / (r2 * r2 * r);
        float  k  = -3.0f * bhs[i].mass * h2 * inv_r5;
        a = vector_add(a, vector_scale(d, k));
    }
    return a;
}

/* Advance the photon state (x, v) by one affine step `h` using classic
 * 4th-order Runge-Kutta on dx/dλ = v, dv/dλ = a(x, v). RK4 is a good fit
 * here: the force is smooth away from the horizon and RK4's O(h⁴) error
 * lets the tracer take far larger steps than Euler for the same accuracy,
 * which directly cuts the per-ray closest_hit count. */
static inline void bh_rk4_step(vector *x, vector *v, float h,
                               const scene_blackhole *bhs, int count) {
    vector k1x = *v;
    vector k1v = bh_accel(*x, *v, bhs, count);

    vector x2  = vector_add(*x, vector_scale(k1x, 0.5f * h));
    vector v2  = vector_add(*v, vector_scale(k1v, 0.5f * h));
    vector k2x = v2;
    vector k2v = bh_accel(x2, v2, bhs, count);

    vector x3  = vector_add(*x, vector_scale(k2x, 0.5f * h));
    vector v3  = vector_add(*v, vector_scale(k2v, 0.5f * h));
    vector k3x = v3;
    vector k3v = bh_accel(x3, v3, bhs, count);

    vector x4  = vector_add(*x, vector_scale(k3x, h));
    vector v4  = vector_add(*v, vector_scale(k3v, h));
    vector k4x = v4;
    vector k4v = bh_accel(x4, v4, bhs, count);

    /* x += h/6 (k1 + 2 k2 + 2 k3 + k4), same for v. */
    vector sx = vector_add(vector_add(k1x, vector_scale(k2x, 2.0f)),
                           vector_add(vector_scale(k3x, 2.0f), k4x));
    vector sv = vector_add(vector_add(k1v, vector_scale(k2v, 2.0f)),
                           vector_add(vector_scale(k3v, 2.0f), k4v));
    *x = vector_add(*x, vector_scale(sx, h / 6.0f));
    *v = vector_add(*v, vector_scale(sv, h / 6.0f));
}

/* Return non-zero if `x` is at or inside any black hole's event horizon
 * (r_s = 2M) — the photon is captured and the ray terminates as black. */
static inline int bh_captured(vector x, const scene_blackhole *bhs, int count) {
    for (int i = 0; i < count; i++) {
        vector d  = vector_sub(x, bhs[i].center);
        float  rs = 2.0f * bhs[i].mass;
        if (vector_dot(d, d) <= rs * rs) return 1;
    }
    return 0;
}

#endif /* RT_GEODESIC_H */
