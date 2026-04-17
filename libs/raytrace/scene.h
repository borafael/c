#ifndef RT_SCENE_H
#define RT_SCENE_H

#include "material.h"
#include "texture.h"
#include "sphere.h"
#include "plane.h"
#include "disc.h"
#include "cylinder.h"
#include "triangle.h"
#include "box.h"
#include "sprite.h"
#include "heightfield.h"

typedef struct {
    vector direction;
    float intensity;
} rt_light;

#define RT_DEFAULT_CAPACITY 64

typedef struct {
    rt_sphere *spheres;
    int sphere_count;
    int sphere_capacity;
    rt_plane *planes;
    int plane_count;
    int plane_capacity;
    rt_disc *discs;
    int disc_count;
    int disc_capacity;
    rt_cylinder *cylinders;
    int cylinder_count;
    int cylinder_capacity;
    rt_triangle *triangles;
    int triangle_count;
    int triangle_capacity;
    rt_box *boxes;
    int box_count;
    int box_capacity;
    rt_light *lights;
    int light_count;
    int light_capacity;
    float ambient;
    rt_sprite *sprites;
    int sprite_count;
    int sprite_capacity;
    rt_heightfield *heightfields;
    int heightfield_count;
    int heightfield_capacity;
    rt_material *materials;
    int material_count;
    int material_capacity;
    rt_texture *textures;
    int texture_count;
    int texture_capacity;
} rt_scene;

rt_scene *rt_scene_create(void);
void rt_scene_clear(rt_scene *scene);
void rt_scene_set_ambient(rt_scene *scene, float ambient);
int rt_scene_add_light(rt_scene *scene, rt_light light);
int rt_scene_add_sphere(rt_scene *scene, rt_sphere sphere);
int rt_scene_add_plane(rt_scene *scene, rt_plane plane);
int rt_scene_add_disc(rt_scene *scene, rt_disc disc);
int rt_scene_add_cylinder(rt_scene *scene, rt_cylinder cylinder);
int rt_scene_add_triangle(rt_scene *scene, rt_triangle triangle);
int rt_scene_add_box(rt_scene *scene, rt_box box);
int rt_scene_add_sprite(rt_scene *scene, rt_sprite sprite);
int rt_scene_add_heightfield(rt_scene *scene, const rt_heightfield *hf);
int rt_scene_add_material(rt_scene *scene, rt_material material);
int rt_scene_add_texture(rt_scene *scene, rt_texture texture);
void rt_scene_destroy(rt_scene *scene);

#endif /* RT_SCENE_H */
