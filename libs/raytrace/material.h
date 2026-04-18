#ifndef RT_MATERIAL_H
#define RT_MATERIAL_H

#include "rt_color.h"

typedef enum {
    RT_TEX_NONE     = 0,
    RT_TEX_CHECKER  = 1,
    RT_TEX_IMAGE    = 2,
    RT_TEX_GRADIENT = 3,   /* albedo→albedo2 along +Y over tex_scale units */
    RT_TEX_NOISE    = 4,   /* value-noise lerp, tex_scale = world/cell */
    RT_TEX_WOOD     = 5,   /* turbulent rings in XZ, tex_scale = ring width */
    RT_TEX_MARBLE   = 6,   /* turbulent veins along X, tex_scale = band width */
    RT_TEX_CELLS    = 7,   /* Voronoi F1: smooth cell interiors, tex_scale = cell size */
    RT_TEX_CRACKS   = 8,   /* Voronoi F2-F1: cracks at cell borders */
    RT_TEX_STRIPES  = 9,   /* alternating bands along X, tex_scale = band width */
    RT_TEX_DOTS     = 10,  /* polka dots in XZ grid, tex_scale = cell size */
    RT_TEX_BRICKS   = 11,  /* 2:1 staggered bricks in XZ, tex_scale = brick height */
    RT_TEX_CLOUDS   = 12,  /* soft fBm clouds, tex_scale = world units per octave */
    RT_TEX_SPOTS    = 13,  /* thresholded noise (leopard), tex_scale = spot scale */
} rt_tex_kind;

typedef struct {
    rt_color albedo;       /* base color; checker: tile A */
    rt_color albedo2;      /* checker: tile B */
    rt_tex_kind tex_kind;
    float tex_scale;       /* checker: world units per tile; image: UV repeat */
    int tex_index;         /* image: index into scene->textures */
    float reflectivity;    /* 0 = matte, 1 = perfect mirror */
} rt_material;

#endif /* RT_MATERIAL_H */
