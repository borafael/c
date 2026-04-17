#ifndef RT_CYLINDER_H
#define RT_CYLINDER_H

#include "vector.h"

typedef struct {
    vector center;
    vector axis;
    float radius;
    float half_height;
    int material;
} rt_cylinder;

float rt_intersect_cylinder(vector ro, vector rd, const rt_cylinder *cyl);
vector rt_normal_cylinder(vector hp, const rt_cylinder *cyl);

#endif /* RT_CYLINDER_H */
