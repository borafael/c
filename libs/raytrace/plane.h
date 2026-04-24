#ifndef RT_PLANE_H
#define RT_PLANE_H

#include "scene.h"

float  rt_intersect_plane(vector ro, vector rd, const scene_plane *p);
vector rt_normal_plane(const scene_plane *p);

#endif /* RT_PLANE_H */
