#ifndef RT_SPHERE_H
#define RT_SPHERE_H

#include "scene.h"

float  rt_intersect_sphere(vector ro, vector rd, const scene_sphere *s);
vector rt_normal_sphere(vector hp, const scene_sphere *s);

#endif /* RT_SPHERE_H */
