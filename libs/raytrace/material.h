#ifndef RT_MATERIAL_H
#define RT_MATERIAL_H

#include "rt_color.h"

typedef enum {
    RT_TEX_NONE = 0,
    RT_TEX_CHECKER = 1,
    RT_TEX_IMAGE = 2,
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
