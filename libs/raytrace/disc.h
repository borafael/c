#ifndef RT_DISC_H
#define RT_DISC_H

#include "vector.h"

typedef struct {
    vector center;
    vector normal;
    float radius;
    int material;
} rt_disc;

float rt_intersect_disc(vector ro, vector rd, const rt_disc *d);
vector rt_normal_disc(const rt_disc *d);

#endif /* RT_DISC_H */
