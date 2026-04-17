#include "sphere.h"
#include <math.h>

float rt_intersect_sphere(vector ro, vector rd, const rt_sphere *s) {
    vector oc = vector_sub(ro, s->center);
    float b = 2.0f * vector_dot(oc, rd);
    float c = vector_dot(oc, oc) - s->radius * s->radius;
    float disc = b * b - 4.0f * c;
    if (disc < 0.0f) return -1.0f;
    float sq = sqrtf(disc);
    float t0 = (-b - sq) * 0.5f;
    if (t0 > 0.0f) return t0;
    /* Origin is inside the sphere: return the exit-point hit so the
     * surface doesn't disappear when the camera walks into it. */
    float t1 = (-b + sq) * 0.5f;
    if (t1 > 0.0f) return t1;
    return -1.0f;
}

vector rt_normal_sphere(vector hp, const rt_sphere *s) {
    return vector_normalize(vector_sub(hp, s->center));
}
