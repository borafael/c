#ifndef RT_HEIGHTFIELD_H
#define RT_HEIGHTFIELD_H

#include "vector.h"
#include <stdint.h>

typedef struct {
    float *heights;           /* rows * cols vertex heights (borrowed) */
    uint8_t *colors;          /* (rows-1)*(cols-1)*3 RGB per cell (borrowed) */
    float *normals;           /* rows * cols * 3 vertex normals (borrowed) */
    int rows, cols;           /* vertex grid dimensions */
    float world_width;        /* X extent in world units */
    float world_depth;        /* Z extent in world units */
    float origin_x, origin_z; /* world position of grid corner (0,0) */
    float max_height;         /* for AABB early-out */
} rt_heightfield;

int rt_intersect_heightfield(const rt_heightfield *hf, vector origin, vector dir,
                              float *out_t, vector *out_normal,
                              int *out_cell_r, int *out_cell_c);

#endif /* RT_HEIGHTFIELD_H */
