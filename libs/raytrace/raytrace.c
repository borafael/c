#include "raytrace.h"
#include "vector.h"
#include <stdlib.h>
#include <math.h>
#include <float.h>

struct rt_scene {
    rt_sphere *spheres;
    int count;
    int capacity;
};

rt_scene *rt_scene_create(int max_spheres) {
    rt_scene *s = malloc(sizeof(rt_scene));
    if (!s) return NULL;
    s->spheres = malloc(sizeof(rt_sphere) * max_spheres);
    if (!s->spheres) {
        free(s);
        return NULL;
    }
    s->count = 0;
    s->capacity = max_spheres;
    return s;
}

void rt_scene_clear(rt_scene *scene) {
    scene->count = 0;
}

int rt_scene_add_sphere(rt_scene *scene, rt_sphere sphere) {
    if (scene->count >= scene->capacity) return -1;
    scene->spheres[scene->count++] = sphere;
    return 0;
}

void rt_scene_destroy(rt_scene *scene) {
    if (!scene) return;
    free(scene->spheres);
    free(scene);
}

/**
 * Ray-sphere intersection. Returns distance t >= 0, or -1 if no hit.
 * ray_origin + t * ray_dir = hit point
 * ray_dir must be normalized (so a = 1).
 */
static float intersect_sphere(vector ray_origin, vector ray_dir,
                              const rt_sphere *sphere) {
    vector oc = vector_sub(ray_origin, sphere->center);
    float b = 2.0f * vector_dot(oc, ray_dir);
    float c = vector_dot(oc, oc) - sphere->radius * sphere->radius;
    float disc = b * b - 4.0f * c;
    if (disc < 0.0f) return -1.0f;
    float t = (-b - sqrtf(disc)) * 0.5f;
    if (t < 0.0f) return -1.0f;
    return t;
}

void rt_render_chunk(uint32_t *pixel_buf, int width, int height,
                     int y_start, int y_end,
                     const rt_camera *camera, const rt_scene *scene) {
    /* Fixed directional light */
    vector light_dir = vector_normalize((vector){1.0f, 1.0f, -1.0f});
    float ambient = 0.15f;

    float half_w = (float)width * 0.5f;
    float half_h = (float)height * 0.5f;

    for (int y = y_start; y < y_end; y++) {
        for (int x = 0; x < width; x++) {
            /* Map pixel to normalized screen coords */
            float sx = ((float)x - half_w) / camera->fov_factor;
            float sy = -((float)y - half_h) / camera->fov_factor;

            /* Construct ray direction in world space */
            vector dir = vector_add(
                vector_add(
                    camera->forward,
                    vector_scale(camera->right, sx)),
                vector_scale(camera->up, sy));
            dir = vector_normalize(dir);

            /* Find closest sphere hit */
            float closest_t = FLT_MAX;
            int hit_idx = -1;

            for (int i = 0; i < scene->count; i++) {
                float t = intersect_sphere(camera->origin, dir,
                                           &scene->spheres[i]);
                if (t > 0.0f && t < closest_t) {
                    closest_t = t;
                    hit_idx = i;
                }
            }

            if (hit_idx >= 0) {
                /* Compute hit point and normal */
                const rt_sphere *sp = &scene->spheres[hit_idx];
                vector hit = vector_add(camera->origin,
                                        vector_scale(dir, closest_t));
                vector normal = vector_normalize(
                    vector_sub(hit, sp->center));

                /* Lambertian diffuse shading */
                float diffuse = vector_dot(normal, light_dir);
                if (diffuse < 0.0f) diffuse = 0.0f;
                float shade = ambient + 0.85f * diffuse;

                uint8_t cr = (uint8_t)(sp->r * shade > 255.0f ? 255 : sp->r * shade);
                uint8_t cg = (uint8_t)(sp->g * shade > 255.0f ? 255 : sp->g * shade);
                uint8_t cb = (uint8_t)(sp->b * shade > 255.0f ? 255 : sp->b * shade);

                pixel_buf[y * width + x] = (255u << 24) | (cr << 16) | (cg << 8) | cb;
            } else {
                /* Background: black */
                pixel_buf[y * width + x] = (255u << 24);
            }
        }
    }
}
