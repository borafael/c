#ifndef RT_SPHERE_H
#define RT_SPHERE_H

#include "vector.h"

typedef struct {
    vector center;
    float radius;
    int material;
} rt_sphere;

float rt_intersect_sphere(vector ro, vector rd, const rt_sphere *s);
vector rt_normal_sphere(vector hp, const rt_sphere *s);

#endif /* RT_SPHERE_H */
