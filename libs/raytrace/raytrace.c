#include "raytrace.h"
#include "vector.h"
#include <stdlib.h>
#include <math.h>
#include <float.h>

#define DEFAULT_CAPACITY 64

struct rt_camera {
    vector origin;
    vector forward;
    vector right;
    vector up;
};

struct rt_scene {
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
};

rt_scene *rt_scene_create(void) {
    rt_scene *s = calloc(1, sizeof(rt_scene));
    if (!s) return NULL;

    s->sphere_capacity   = DEFAULT_CAPACITY;
    s->plane_capacity    = DEFAULT_CAPACITY;
    s->disc_capacity     = DEFAULT_CAPACITY;
    s->cylinder_capacity = DEFAULT_CAPACITY;
    s->triangle_capacity = DEFAULT_CAPACITY;
    s->box_capacity      = DEFAULT_CAPACITY;
    s->light_capacity    = DEFAULT_CAPACITY;
    s->sprite_capacity   = DEFAULT_CAPACITY;
    s->ambient           = 0.15f;

    s->spheres   = malloc(sizeof(rt_sphere)   * s->sphere_capacity);
    s->planes    = malloc(sizeof(rt_plane)    * s->plane_capacity);
    s->discs     = malloc(sizeof(rt_disc)     * s->disc_capacity);
    s->cylinders = malloc(sizeof(rt_cylinder) * s->cylinder_capacity);
    s->triangles = malloc(sizeof(rt_triangle) * s->triangle_capacity);
    s->boxes     = malloc(sizeof(rt_box)      * s->box_capacity);
    s->lights    = malloc(sizeof(rt_light)    * s->light_capacity);
    s->sprites   = malloc(sizeof(rt_sprite)  * s->sprite_capacity);

    if (!s->spheres || !s->planes || !s->discs ||
        !s->cylinders || !s->triangles || !s->boxes || !s->lights ||
        !s->sprites) {
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
    scene->sprite_count   = 0;
}

int rt_scene_add_sphere(rt_scene *scene, rt_sphere sphere) {
    if (scene->sphere_count >= scene->sphere_capacity) return -1;
    scene->spheres[scene->sphere_count++] = sphere;
    return 0;
}

int rt_scene_add_plane(rt_scene *scene, rt_plane plane) {
    if (scene->plane_count >= scene->plane_capacity) return -1;
    scene->planes[scene->plane_count++] = plane;
    return 0;
}

int rt_scene_add_disc(rt_scene *scene, rt_disc disc) {
    if (scene->disc_count >= scene->disc_capacity) return -1;
    scene->discs[scene->disc_count++] = disc;
    return 0;
}

int rt_scene_add_cylinder(rt_scene *scene, rt_cylinder cylinder) {
    if (scene->cylinder_count >= scene->cylinder_capacity) return -1;
    scene->cylinders[scene->cylinder_count++] = cylinder;
    return 0;
}

int rt_scene_add_triangle(rt_scene *scene, rt_triangle triangle) {
    if (scene->triangle_count >= scene->triangle_capacity) return -1;
    scene->triangles[scene->triangle_count++] = triangle;
    return 0;
}

int rt_scene_add_box(rt_scene *scene, rt_box box) {
    if (scene->box_count >= scene->box_capacity) return -1;
    scene->boxes[scene->box_count++] = box;
    return 0;
}

void rt_scene_set_ambient(rt_scene *scene, float ambient) {
    scene->ambient = ambient;
}

int rt_scene_add_sprite(rt_scene *scene, rt_sprite sprite) {
    if (scene->sprite_count >= scene->sprite_capacity) return -1;
    scene->sprites[scene->sprite_count++] = sprite;
    return 0;
}

int rt_scene_add_light(rt_scene *scene, rt_light light) {
    if (scene->light_count >= scene->light_capacity) return -1;
    light.direction = vector_normalize(light.direction);
    scene->lights[scene->light_count++] = light;
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
    free(scene);
}

static void camera_update_orientation(rt_camera *cam) {
    cam->forward = vector_normalize(cam->forward);

    vector world_up = {0.0f, 1.0f, 0.0f};
    cam->right = vector_normalize(vector_cross(cam->forward, world_up));
    /* Handle degenerate case when looking straight up/down */
    if (vector_magnitude(cam->right) < 0.001f) {
        cam->right = (vector){1.0f, 0.0f, 0.0f};
    }
    cam->up = vector_cross(cam->right, cam->forward);
}

rt_camera *rt_camera_create(vector position, vector direction) {
    rt_camera *cam = malloc(sizeof(rt_camera));
    if (!cam) return NULL;
    cam->origin = position;
    cam->forward = direction;
    camera_update_orientation(cam);
    return cam;
}

void rt_camera_place(rt_camera *cam, vector position, vector direction) {
    cam->origin = position;
    cam->forward = direction;
    camera_update_orientation(cam);
}

void rt_camera_destroy(rt_camera *cam) {
    if (!cam) return;
    free(cam);
}

/* --- Intersection functions ---
 * All return distance t >= 0, or -1 if no hit.
 * ray_dir must be normalized.
 */

static float intersect_sphere(vector ro, vector rd, const rt_sphere *s) {
    vector oc = vector_sub(ro, s->center);
    float b = 2.0f * vector_dot(oc, rd);
    float c = vector_dot(oc, oc) - s->radius * s->radius;
    float disc = b * b - 4.0f * c;
    if (disc < 0.0f) return -1.0f;
    float t = (-b - sqrtf(disc)) * 0.5f;
    if (t < 0.0f) return -1.0f;
    return t;
}

static float intersect_plane(vector ro, vector rd, const rt_plane *p) {
    float denom = vector_dot(rd, p->normal);
    if (fabsf(denom) < 1e-6f) return -1.0f;
    float t = vector_dot(vector_sub(p->point, ro), p->normal) / denom;
    if (t < 0.0f) return -1.0f;
    return t;
}

static float intersect_disc(vector ro, vector rd, const rt_disc *d) {
    /* Intersect the plane the disc lies on */
    float denom = vector_dot(rd, d->normal);
    if (fabsf(denom) < 1e-6f) return -1.0f;
    float t = vector_dot(vector_sub(d->center, ro), d->normal) / denom;
    if (t < 0.0f) return -1.0f;
    /* Check if hit point is within the disc radius */
    vector hp = vector_add(ro, vector_scale(rd, t));
    vector diff = vector_sub(hp, d->center);
    if (vector_dot(diff, diff) > d->radius * d->radius) return -1.0f;
    return t;
}

static float intersect_cylinder(vector ro, vector rd, const rt_cylinder *cyl) {
    /* Infinite cylinder intersection along axis, then clamp to half_height */
    vector oc = vector_sub(ro, cyl->center);
    float rd_dot_a = vector_dot(rd, cyl->axis);
    float oc_dot_a = vector_dot(oc, cyl->axis);

    /* Coefficients for quadratic in t (ray projected perpendicular to axis) */
    vector rd_perp = vector_sub(rd, vector_scale(cyl->axis, rd_dot_a));
    vector oc_perp = vector_sub(oc, vector_scale(cyl->axis, oc_dot_a));

    float a = vector_dot(rd_perp, rd_perp);
    float b = 2.0f * vector_dot(oc_perp, rd_perp);
    float c = vector_dot(oc_perp, oc_perp) - cyl->radius * cyl->radius;

    float disc = b * b - 4.0f * a * c;
    if (disc < 0.0f) return -1.0f;

    float sqrt_disc = sqrtf(disc);
    float t0 = (-b - sqrt_disc) / (2.0f * a);
    float t1 = (-b + sqrt_disc) / (2.0f * a);

    /* Check both intersections, take nearest valid one */
    for (int i = 0; i < 2; i++) {
        float t = (i == 0) ? t0 : t1;
        if (t < 0.0f) continue;
        /* Check height along axis */
        float h = oc_dot_a + t * rd_dot_a;
        if (h >= -cyl->half_height && h <= cyl->half_height) return t;
    }
    return -1.0f;
}

static float intersect_triangle(vector ro, vector rd, const rt_triangle *tri) {
    /* Möller–Trumbore intersection */
    vector e1 = vector_sub(tri->v1, tri->v0);
    vector e2 = vector_sub(tri->v2, tri->v0);
    vector pvec = vector_cross(rd, e2);
    float det = vector_dot(e1, pvec);
    if (fabsf(det) < 1e-6f) return -1.0f;

    float inv_det = 1.0f / det;
    vector tvec = vector_sub(ro, tri->v0);
    float u = vector_dot(tvec, pvec) * inv_det;
    if (u < 0.0f || u > 1.0f) return -1.0f;

    vector qvec = vector_cross(tvec, e1);
    float v = vector_dot(rd, qvec) * inv_det;
    if (v < 0.0f || u + v > 1.0f) return -1.0f;

    float t = vector_dot(e2, qvec) * inv_det;
    if (t < 0.0f) return -1.0f;
    return t;
}

static float intersect_box(vector ro, vector rd, const rt_box *box) {
    /* Slab method for axis-aligned box */
    float tmin = -FLT_MAX;
    float tmax = FLT_MAX;

    /* X slab */
    if (fabsf(rd.x) > 1e-6f) {
        float t0 = (box->min.x - ro.x) / rd.x;
        float t1 = (box->max.x - ro.x) / rd.x;
        if (t0 > t1) { float tmp = t0; t0 = t1; t1 = tmp; }
        if (t0 > tmin) tmin = t0;
        if (t1 < tmax) tmax = t1;
    } else if (ro.x < box->min.x || ro.x > box->max.x) {
        return -1.0f;
    }

    /* Y slab */
    if (fabsf(rd.y) > 1e-6f) {
        float t0 = (box->min.y - ro.y) / rd.y;
        float t1 = (box->max.y - ro.y) / rd.y;
        if (t0 > t1) { float tmp = t0; t0 = t1; t1 = tmp; }
        if (t0 > tmin) tmin = t0;
        if (t1 < tmax) tmax = t1;
    } else if (ro.y < box->min.y || ro.y > box->max.y) {
        return -1.0f;
    }

    /* Z slab */
    if (fabsf(rd.z) > 1e-6f) {
        float t0 = (box->min.z - ro.z) / rd.z;
        float t1 = (box->max.z - ro.z) / rd.z;
        if (t0 > t1) { float tmp = t0; t0 = t1; t1 = tmp; }
        if (t0 > tmin) tmin = t0;
        if (t1 < tmax) tmax = t1;
    } else if (ro.z < box->min.z || ro.z > box->max.z) {
        return -1.0f;
    }

    if (tmin > tmax) return -1.0f;
    if (tmin > 0.0f) return tmin;
    if (tmax > 0.0f) return tmax;
    return -1.0f;
}

/* --- Normal computation --- */

static vector normal_sphere(vector hp, const rt_sphere *s) {
    return vector_normalize(vector_sub(hp, s->center));
}

static vector normal_disc(const rt_disc *d) {
    return d->normal;
}

static vector normal_cylinder(vector hp, const rt_cylinder *cyl) {
    /* Project hit point onto axis, normal is perpendicular to axis */
    vector diff = vector_sub(hp, cyl->center);
    float proj = vector_dot(diff, cyl->axis);
    vector on_axis = vector_add(cyl->center, vector_scale(cyl->axis, proj));
    return vector_normalize(vector_sub(hp, on_axis));
}

static vector normal_triangle(const rt_triangle *tri) {
    vector e1 = vector_sub(tri->v1, tri->v0);
    vector e2 = vector_sub(tri->v2, tri->v0);
    return vector_normalize(vector_cross(e1, e2));
}

static vector normal_box(vector hp, const rt_box *box) {
    /* Find which face was hit by checking which component is closest to a face */
    float eps = 1e-4f;
    if (fabsf(hp.x - box->min.x) < eps) return (vector){-1, 0, 0};
    if (fabsf(hp.x - box->max.x) < eps) return (vector){ 1, 0, 0};
    if (fabsf(hp.y - box->min.y) < eps) return (vector){ 0,-1, 0};
    if (fabsf(hp.y - box->max.y) < eps) return (vector){ 0, 1, 0};
    if (fabsf(hp.z - box->min.z) < eps) return (vector){ 0, 0,-1};
    return (vector){0, 0, 1};
}

void rt_render_chunk(uint32_t *pixel_buf, const rt_viewport *viewport,
                     int y_start, int y_end,
                     const rt_camera *camera, const rt_scene *scene) {
    int width = viewport->width;
    int height = viewport->height;
    float fov_factor = (float)height / (2.0f * tanf(viewport->fov / 2.0f));

    float half_w = (float)width * 0.5f;
    float half_h = (float)height * 0.5f;

    for (int y = y_start; y < y_end; y++) {
        for (int x = 0; x < width; x++) {
            float sx = ((float)x - half_w) / fov_factor;
            float sy = -((float)y - half_h) / fov_factor;

            vector dir = vector_add(
                vector_add(
                    camera->forward,
                    vector_scale(camera->right, sx)),
                vector_scale(camera->up, sy));
            dir = vector_normalize(dir);

            float closest_t = FLT_MAX;
            vector normal = {0};
            rt_color color = {0};
            int hit = 0;

            /* Spheres */
            for (int i = 0; i < scene->sphere_count; i++) {
                float t = intersect_sphere(camera->origin, dir, &scene->spheres[i]);
                if (t > 0.0f && t < closest_t) {
                    closest_t = t;
                    vector hp = vector_add(camera->origin, vector_scale(dir, t));
                    normal = normal_sphere(hp, &scene->spheres[i]);
                    color = scene->spheres[i].color;
                    hit = 1;
                }
            }

            /* Planes */
            for (int i = 0; i < scene->plane_count; i++) {
                float t = intersect_plane(camera->origin, dir, &scene->planes[i]);
                if (t > 0.0f && t < closest_t) {
                    closest_t = t;
                    normal = scene->planes[i].normal;
                    color = scene->planes[i].color;
                    hit = 1;
                }
            }

            /* Discs */
            for (int i = 0; i < scene->disc_count; i++) {
                float t = intersect_disc(camera->origin, dir, &scene->discs[i]);
                if (t > 0.0f && t < closest_t) {
                    closest_t = t;
                    normal = normal_disc(&scene->discs[i]);
                    color = scene->discs[i].color;
                    hit = 1;
                }
            }

            /* Cylinders */
            for (int i = 0; i < scene->cylinder_count; i++) {
                float t = intersect_cylinder(camera->origin, dir, &scene->cylinders[i]);
                if (t > 0.0f && t < closest_t) {
                    closest_t = t;
                    vector hp = vector_add(camera->origin, vector_scale(dir, t));
                    normal = normal_cylinder(hp, &scene->cylinders[i]);
                    color = scene->cylinders[i].color;
                    hit = 1;
                }
            }

            /* Triangles */
            for (int i = 0; i < scene->triangle_count; i++) {
                float t = intersect_triangle(camera->origin, dir, &scene->triangles[i]);
                if (t > 0.0f && t < closest_t) {
                    closest_t = t;
                    normal = normal_triangle(&scene->triangles[i]);
                    color = scene->triangles[i].color;
                    hit = 1;
                }
            }

            /* Boxes */
            for (int i = 0; i < scene->box_count; i++) {
                float t = intersect_box(camera->origin, dir, &scene->boxes[i]);
                if (t > 0.0f && t < closest_t) {
                    closest_t = t;
                    vector hp = vector_add(camera->origin, vector_scale(dir, t));
                    normal = normal_box(hp, &scene->boxes[i]);
                    color = scene->boxes[i].color;
                    hit = 1;
                }
            }

            if (hit) {
                float shade = scene->ambient;
                for (int i = 0; i < scene->light_count; i++) {
                    float d = vector_dot(normal, scene->lights[i].direction);
                    if (d > 0.0f) shade += d * scene->lights[i].intensity;
                }
                if (shade > 1.0f) shade = 1.0f;

                uint8_t cr = (uint8_t)(color.r * shade > 255.0f ? 255 : color.r * shade);
                uint8_t cg = (uint8_t)(color.g * shade > 255.0f ? 255 : color.g * shade);
                uint8_t cb = (uint8_t)(color.b * shade > 255.0f ? 255 : color.b * shade);

                pixel_buf[y * width + x] = (255u << 24) | (cr << 16) | (cg << 8) | cb;
            } else {
                pixel_buf[y * width + x] = (255u << 24);
            }
        }
    }
}
