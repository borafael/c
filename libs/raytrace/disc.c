#include "disc.h"
#include <math.h>

float rt_intersect_disc(vector ro, vector rd, const rt_disc *d) {
    float denom = vector_dot(rd, d->normal);
    if (fabsf(denom) < 1e-6f) return -1.0f;
    float t = vector_dot(vector_sub(d->center, ro), d->normal) / denom;
    if (t < 0.0f) return -1.0f;
    vector hp = vector_add(ro, vector_scale(rd, t));
    vector diff = vector_sub(hp, d->center);
    if (vector_dot(diff, diff) > d->radius * d->radius) return -1.0f;
    return t;
}

vector rt_normal_disc(const rt_disc *d) {
    return d->normal;
}
