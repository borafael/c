#ifndef RT_BOX_H
#define RT_BOX_H

#include "scene.h"

float  rt_intersect_box(vector ro, vector rd, const scene_box *box);
vector rt_normal_box(vector hp, const scene_box *box);
void   rt_box_uv(vector hp, const scene_box *box, float *u, float *v);

#endif /* RT_BOX_H */
