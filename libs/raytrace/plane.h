#ifndef RT_PLANE_H
#define RT_PLANE_H

#include "vector.h"
#include "rt_color.h"

typedef struct {
    vector normal;
    vector point;
    rt_color color;
} rt_plane;

float rt_intersect_plane(vector ro, vector rd, const rt_plane *p);
vector rt_normal_plane(const rt_plane *p);

#endif /* RT_PLANE_H */
