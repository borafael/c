#include "cone.h"
#include <math.h>

/* Finite cone: lateral surface only.
 *
 * Implicit equation for the infinite double cone with apex at `apex`,
 * axis `a` (unit), half-angle alpha:
 *     ((P - apex) . a)^2 = cos^2(alpha) * |P - apex|^2
 * with cos^2(alpha) = h^2 / (h^2 + r^2) for a cone with height h and
 * base radius r (similar triangles).
 *
 * Substituting P = ro + t*rd gives a quadratic in t. The infinite cone
 * has two nappes; the height filter `0 <= proj <= height` clips us to
 * the finite forward nappe. */
float rt_intersect_cone(vector ro, vector rd, const scene_cone *cone) {
    vector oa = vector_sub(ro, cone->apex);
    float k1 = vector_dot(oa, cone->axis);     /* axial component of origin */
    float k2 = vector_dot(rd, cone->axis);     /* axial component of dir    */

    float h2 = cone->height * cone->height;
    float r2 = cone->radius * cone->radius;
    float cosa2 = h2 / (h2 + r2);

    float rd_dot_oa = vector_dot(rd, oa);
    float rd2 = vector_dot(rd, rd);
    float oa2 = vector_dot(oa, oa);

    float a = k2 * k2 - cosa2 * rd2;
    float b = 2.0f * (k1 * k2 - cosa2 * rd_dot_oa);
    float c = k1 * k1 - cosa2 * oa2;

    /* Degenerate case (ray parallel to cone surface) — fall through with
     * a near-zero `a` and let the discriminant reject if needed. */
    if (fabsf(a) < 1e-8f) {
        if (fabsf(b) < 1e-8f) return -1.0f;
        float t = -c / b;
        if (t < 0.0f) return -1.0f;
        float proj = k1 + t * k2;
        if (proj < 0.0f || proj > cone->height) return -1.0f;
        return t;
    }

    float disc = b * b - 4.0f * a * c;
    if (disc < 0.0f) return -1.0f;

    float sqrt_disc = sqrtf(disc);
    float inv2a = 1.0f / (2.0f * a);
    float t0 = (-b - sqrt_disc) * inv2a;
    float t1 = (-b + sqrt_disc) * inv2a;
    if (t0 > t1) { float tmp = t0; t0 = t1; t1 = tmp; }

    for (int i = 0; i < 2; i++) {
        float t = (i == 0) ? t0 : t1;
        if (t < 0.0f) continue;
        float proj = k1 + t * k2;
        if (proj >= 0.0f && proj <= cone->height) return t;
    }
    return -1.0f;
}

vector rt_normal_cone(vector hp, const scene_cone *cone) {
    /* Outward normal of the implicit cone is parallel to
     * (cos^2(alpha) * cp - axis * (cp . axis)), where cp = hp - apex.
     * Same algebra as -gradient of the F above. */
    vector cp = vector_sub(hp, cone->apex);
    float h2 = cone->height * cone->height;
    float r2 = cone->radius * cone->radius;
    float cosa2 = h2 / (h2 + r2);
    float m = vector_dot(cp, cone->axis);
    vector n = vector_sub(vector_scale(cp, cosa2),
                          vector_scale(cone->axis, m));
    return vector_normalize(n);
}
