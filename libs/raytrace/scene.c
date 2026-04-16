#include "scene.h"
#include <stdlib.h>

#define GROW_IF_NEEDED(arr, count, cap, type) do { \
    if ((count) >= (cap)) { \
        int _new_cap = (cap) * 2; \
        type *_new_arr = realloc((arr), sizeof(type) * _new_cap); \
        if (!_new_arr) return -1; \
        (arr) = _new_arr; \
        (cap) = _new_cap; \
    } \
} while (0)

rt_scene *rt_scene_create(void) {
    rt_scene *s = calloc(1, sizeof(rt_scene));
    if (!s) return NULL;

    s->sphere_capacity   = RT_DEFAULT_CAPACITY;
    s->plane_capacity    = RT_DEFAULT_CAPACITY;
    s->disc_capacity     = RT_DEFAULT_CAPACITY;
    s->cylinder_capacity = RT_DEFAULT_CAPACITY;
    s->triangle_capacity = RT_DEFAULT_CAPACITY;
    s->box_capacity      = RT_DEFAULT_CAPACITY;
    s->light_capacity    = RT_DEFAULT_CAPACITY;
    s->sprite_capacity       = RT_DEFAULT_CAPACITY;
    s->heightfield_capacity  = RT_DEFAULT_CAPACITY;
    s->ambient               = 0.15f;

    s->spheres   = malloc(sizeof(rt_sphere)   * s->sphere_capacity);
    s->planes    = malloc(sizeof(rt_plane)    * s->plane_capacity);
    s->discs     = malloc(sizeof(rt_disc)     * s->disc_capacity);
    s->cylinders = malloc(sizeof(rt_cylinder) * s->cylinder_capacity);
    s->triangles = malloc(sizeof(rt_triangle) * s->triangle_capacity);
    s->boxes     = malloc(sizeof(rt_box)      * s->box_capacity);
    s->lights    = malloc(sizeof(rt_light)    * s->light_capacity);
    s->sprites      = malloc(sizeof(rt_sprite)       * s->sprite_capacity);
    s->heightfields = malloc(sizeof(rt_heightfield) * s->heightfield_capacity);

    if (!s->spheres || !s->planes || !s->discs ||
        !s->cylinders || !s->triangles || !s->boxes || !s->lights ||
        !s->sprites || !s->heightfields) {
        rt_scene_destroy(s);
        return NULL;
    }
    return s;
}

void rt_scene_clear(rt_scene *scene) {
    scene->sphere_count   = 0;
    scene->plane_count    = 0;
    scene->disc_count     = 0;
    scene->cylinder_count = 0;
    scene->triangle_count = 0;
    scene->box_count      = 0;
    scene->sprite_count      = 0;
    scene->heightfield_count = 0;
    scene->light_count       = 0;
}

int rt_scene_add_sphere(rt_scene *scene, rt_sphere sphere) {
    GROW_IF_NEEDED(scene->spheres, scene->sphere_count, scene->sphere_capacity, rt_sphere);
    scene->spheres[scene->sphere_count++] = sphere;
    return 0;
}

int rt_scene_add_plane(rt_scene *scene, rt_plane plane) {
    GROW_IF_NEEDED(scene->planes, scene->plane_count, scene->plane_capacity, rt_plane);
    scene->planes[scene->plane_count++] = plane;
    return 0;
}

int rt_scene_add_disc(rt_scene *scene, rt_disc disc) {
    GROW_IF_NEEDED(scene->discs, scene->disc_count, scene->disc_capacity, rt_disc);
    scene->discs[scene->disc_count++] = disc;
    return 0;
}

int rt_scene_add_cylinder(rt_scene *scene, rt_cylinder cylinder) {
    GROW_IF_NEEDED(scene->cylinders, scene->cylinder_count, scene->cylinder_capacity, rt_cylinder);
    scene->cylinders[scene->cylinder_count++] = cylinder;
    return 0;
}

int rt_scene_add_triangle(rt_scene *scene, rt_triangle triangle) {
    GROW_IF_NEEDED(scene->triangles, scene->triangle_count, scene->triangle_capacity, rt_triangle);
    scene->triangles[scene->triangle_count++] = triangle;
    return 0;
}

int rt_scene_add_box(rt_scene *scene, rt_box box) {
    GROW_IF_NEEDED(scene->boxes, scene->box_count, scene->box_capacity, rt_box);
    scene->boxes[scene->box_count++] = box;
    return 0;
}

void rt_scene_set_ambient(rt_scene *scene, float ambient) {
    scene->ambient = ambient;
}

int rt_scene_add_sprite(rt_scene *scene, rt_sprite sprite) {
    GROW_IF_NEEDED(scene->sprites, scene->sprite_count, scene->sprite_capacity, rt_sprite);
    scene->sprites[scene->sprite_count++] = sprite;
    return 0;
}

int rt_scene_add_light(rt_scene *scene, rt_light light) {
    GROW_IF_NEEDED(scene->lights, scene->light_count, scene->light_capacity, rt_light);
    light.direction = vector_normalize(light.direction);
    scene->lights[scene->light_count++] = light;
    return 0;
}

int rt_scene_add_heightfield(rt_scene *scene, const rt_heightfield *hf) {
    GROW_IF_NEEDED(scene->heightfields, scene->heightfield_count, scene->heightfield_capacity, rt_heightfield);
    scene->heightfields[scene->heightfield_count++] = *hf;
    return 0;
}

void rt_scene_destroy(rt_scene *scene) {
    if (!scene) return;
    free(scene->spheres);
    free(scene->planes);
    free(scene->discs);
    free(scene->cylinders);
    free(scene->triangles);
    free(scene->boxes);
    free(scene->lights);
    free(scene->sprites);
    free(scene->heightfields);
    free(scene);
}
