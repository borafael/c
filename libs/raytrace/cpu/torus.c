#include "torus.h"
#include <math.h>

/* Torus: surface of points at distance `r` (minor) from a central circle
 * of radius `R` (major) lying in the plane through `center` with normal
 * `axis` (unit).
 *
 * History: this used to solve the implicit equation
 *     (x^2 + y^2 + z^2 + R^2 - r^2)^2 = 4 R^2 (x^2 + z^2)
 * for P(t) = ro' + t*rd via Ferrari's method in double precision. The
 * closed-form solver missed tangent-near roots (visible as the back half
 * of the donut going invisible from grazing camera angles), even with
 * 64-bit math, because the depressed-quartic coefficients can suffer
 * cancellation that hides shallow sign changes in the resolvent.
 * Sphere-tracing the SDF is bounded-cost and finds those tangents
 * reliably. The GPU backend uses the same approach. */

static inline float torus_sdf(vector P, const scene_torus *t) {
    vector cp = vector_sub(P, t->center);
    float ax = vector_dot(cp, t->axis);
    vector perp = vector_sub(cp, vector_scale(t->axis, ax));
    float plen = sqrtf(vector_dot(perp, perp));
    float dx = plen - t->major_radius;
    return sqrtf(dx*dx + ax*ax) - t->minor_radius;
}

float rt_intersect_torus(vector ro, vector rd, const scene_torus *torus) {
    float R = torus->major_radius;
    float r = torus->minor_radius;
    /* Bound the search by the torus' bounding sphere (radius R + r) so
     * empty space is skipped cheaply. */
    vector oc = vector_sub(ro, torus->center);
    float bd = vector_dot(oc, rd);
    float Rb = R + r;
    float disc = bd*bd - vector_dot(oc, oc) + Rb*Rb;
    if (disc < 0.0f) return -1.0f;
    float sq = sqrtf(disc);
    float t_in  = fmaxf(-bd - sq, 1e-4f);
    float t_out = -bd + sq;
    if (t_out <= 1e-4f) return -1.0f;

    float t = t_in;
    for (int i = 0; i < 96; i++) {
        vector P = vector_add(ro, vector_scale(rd, t));
        float d = torus_sdf(P, torus);
        float eps = 1e-4f * fmaxf(1.0f, t);
        if (d < eps) return t;
        t += fmaxf(d, eps);
        if (t > t_out) return -1.0f;
    }
    return -1.0f;
}

vector rt_normal_torus(vector hp, const scene_torus *torus) {
    /* Normal points from the closest point on the central circle to the
     * hit. The closest point is `center + R * unit(perp)`, where `perp`
     * is the component of (hp - center) perpendicular to `axis`. */
    vector cp = vector_sub(hp, torus->center);
    float ax = vector_dot(cp, torus->axis);
    vector perp = vector_sub(cp, vector_scale(torus->axis, ax));
    float plen = sqrtf(vector_dot(perp, perp));
    if (plen < 1e-8f) {
        /* Hit on the axis (only possible if R == 0, a sphere edge case).
         * Return the axial direction so we still produce a unit normal. */
        return vector_normalize(torus->axis);
    }
    vector closest = vector_add(torus->center,
                                vector_scale(perp, torus->major_radius / plen));
    return vector_normalize(vector_sub(hp, closest));
}
