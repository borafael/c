#ifndef RT_BOX_H
#define RT_BOX_H

#include "vector.h"

typedef struct {
    vector min;
    vector max;
    int material;
} rt_box;

float rt_intersect_box(vector ro, vector rd, const rt_box *box);
vector rt_normal_box(vector hp, const rt_box *box);

#endif /* RT_BOX_H */
