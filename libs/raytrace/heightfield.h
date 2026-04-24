#ifndef RT_HEIGHTFIELD_H
#define RT_HEIGHTFIELD_H

#include "scene.h"

int rt_intersect_heightfield(const scene_heightfield *hf, vector origin, vector dir,
                             float *out_t, vector *out_normal,
                             int *out_cell_r, int *out_cell_c);

#endif /* RT_HEIGHTFIELD_H */
