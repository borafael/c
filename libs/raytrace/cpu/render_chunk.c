#include "render_chunk.h"
#include "sphere.h"
#include "plane.h"
#include "disc.h"
#include "cylinder.h"
#include "triangle.h"
#include "box.h"
#include "sprite.h"
#include "heightfield.h"
#include "material.h"
#include "texture.h"
#include <math.h>
#include <float.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static inline void tangent_basis(vector n, vector *t, vector *b) {
    vector up = (fabsf(n.y) < 0.999f) ? (vector){0,1,0} : (vector){1,0,0};
    *t = vector_normalize(vector_cross(up, n));
    *b = vector_cross(n, *t);
}

static inline void uv_sphere(vector hp, vector center, float *u, float *v) {
    vector n = vector_normalize(vector_sub(hp, center));
    *u = atan2f(n.z, n.x) / (2.0f * (float)M_PI) + 0.5f;
    float ny = n.y < -1.0f ? -1.0f : (n.y > 1.0f ? 1.0f : n.y);
    *v = acosf(ny) / (float)M_PI;
}

static inline void uv_planar(vector hp, vector anchor, vector normal,
                             float *u, float *v) {
    vector t, b;
    tangent_basis(normal, &t, &b);
    vector d = vector_sub(hp, anchor);
    *u = vector_dot(d, t);
    *v = vector_dot(d, b);
}

static inline void uv_cylinder(vector hp, const rt_cylinder *cyl,
                               float *u, float *v) {
    vector axis = vector_normalize(cyl->axis);
    vector ohp = vector_sub(hp, cyl->center);
    float h = vector_dot(ohp, axis);
    vector radial = vector_sub(ohp, vector_scale(axis, h));
    vector tan_a, tan_b;
    tangent_basis(axis, &tan_a, &tan_b);
    float x = vector_dot(radial, tan_a);
    float z = vector_dot(radial, tan_b);
    *u = atan2f(z, x) / (2.0f * (float)M_PI) + 0.5f;
    *v = (h + cyl->half_height) / (2.0f * cyl->half_height);
}

static inline void uv_box(vector hp, vector normal, float *u, float *v) {
    float ax = fabsf(normal.x), ay = fabsf(normal.y), az = fabsf(normal.z);
    if (ax >= ay && ax >= az)      { *u = hp.z; *v = hp.y; }
    else if (ay >= ax && ay >= az) { *u = hp.x; *v = hp.z; }
    else                           { *u = hp.x; *v = hp.y; }
}

static inline rt_color material_sample(const rt_material *m,
                                       const rt_texture *textures,
                                       vector p, float u, float v) {
    if (m->tex_kind == RT_TEX_CHECKER) {
        float s = m->tex_scale > 0.0f ? m->tex_scale : 1.0f;
        /* Bias by a tiny epsilon so that tile boundaries (especially hits
         * on an axis-aligned plane where one coordinate is algebraically
         * zero) don't flip parity from float rounding noise. */
        const float eps = 1e-4f;
        int ix = (int)floorf(p.x / s + eps);
        int iy = (int)floorf(p.y / s + eps);
        int iz = (int)floorf(p.z / s + eps);
        return ((ix + iy + iz) & 1) ? m->albedo2 : m->albedo;
    }
    if (m->tex_kind == RT_TEX_IMAGE && m->tex_index >= 0) {
        const rt_texture *t = &textures[m->tex_index];
        float s = m->tex_scale > 0.0f ? m->tex_scale : 1.0f;
        float uu = u / s; uu -= floorf(uu);
        float vv = v / s; vv -= floorf(vv);
        int ix = (int)(uu * (float)t->width);
        int iy = (int)(vv * (float)t->height);
        if (ix < 0) ix = 0; else if (ix >= t->width)  ix = t->width  - 1;
        if (iy < 0) iy = 0; else if (iy >= t->height) iy = t->height - 1;
        uint32_t pixel = t->pixels[iy * t->width + ix];
        rt_color c;
        c.r = (pixel >> 16) & 0xFF;
        c.g = (pixel >>  8) & 0xFF;
        c.b =  pixel        & 0xFF;
        return c;
    }
    return m->albedo;
}

#define RT_MAX_BOUNCES 4
#define RT_REFLECT_EPSILON 1e-4f

typedef struct {
    int hit;
    vector point;
    vector normal;
    rt_color albedo;
    float reflectivity;
} hit_info;

static hit_info closest_hit(vector ro, vector rd, const rt_scene *scene,
                            vector camera_origin) {
    hit_info h = {0};
    float closest_t = FLT_MAX;

    for (int i = 0; i < scene->sphere_count; i++) {
        float t = rt_intersect_sphere(ro, rd, &scene->spheres[i]);
        if (t > 0.0f && t < closest_t) {
            closest_t = t;
            vector hp = vector_add(ro, vector_scale(rd, t));
            h.point = hp;
            h.normal = rt_normal_sphere(hp, &scene->spheres[i]);
            float u, v;
            uv_sphere(hp, scene->spheres[i].center, &u, &v);
            const rt_material *m = &scene->materials[scene->spheres[i].material];
            h.albedo = material_sample(m, scene->textures, hp, u, v);
            h.reflectivity = m->reflectivity;
            h.hit = 1;
        }
    }

    for (int i = 0; i < scene->plane_count; i++) {
        float t = rt_intersect_plane(ro, rd, &scene->planes[i]);
        if (t > 0.0f && t < closest_t) {
            closest_t = t;
            vector hp = vector_add(ro, vector_scale(rd, t));
            h.point = hp;
            h.normal = rt_normal_plane(&scene->planes[i]);
            float u, v;
            uv_planar(hp, scene->planes[i].point, h.normal, &u, &v);
            const rt_material *m = &scene->materials[scene->planes[i].material];
            h.albedo = material_sample(m, scene->textures, hp, u, v);
            h.reflectivity = m->reflectivity;
            h.hit = 1;
        }
    }

    for (int i = 0; i < scene->disc_count; i++) {
        float t = rt_intersect_disc(ro, rd, &scene->discs[i]);
        if (t > 0.0f && t < closest_t) {
            closest_t = t;
            vector hp = vector_add(ro, vector_scale(rd, t));
            h.point = hp;
            h.normal = rt_normal_disc(&scene->discs[i]);
            float u, v;
            uv_planar(hp, scene->discs[i].center, h.normal, &u, &v);
            const rt_material *m = &scene->materials[scene->discs[i].material];
            h.albedo = material_sample(m, scene->textures, hp, u, v);
            h.reflectivity = m->reflectivity;
            h.hit = 1;
        }
    }

    for (int i = 0; i < scene->cylinder_count; i++) {
        float t = rt_intersect_cylinder(ro, rd, &scene->cylinders[i]);
        if (t > 0.0f && t < closest_t) {
            closest_t = t;
            vector hp = vector_add(ro, vector_scale(rd, t));
            h.point = hp;
            h.normal = rt_normal_cylinder(hp, &scene->cylinders[i]);
            float u, v;
            uv_cylinder(hp, &scene->cylinders[i], &u, &v);
            const rt_material *m = &scene->materials[scene->cylinders[i].material];
            h.albedo = material_sample(m, scene->textures, hp, u, v);
            h.reflectivity = m->reflectivity;
            h.hit = 1;
        }
    }

    for (int i = 0; i < scene->triangle_count; i++) {
        float t = rt_intersect_triangle(ro, rd, &scene->triangles[i]);
        if (t > 0.0f && t < closest_t) {
            closest_t = t;
            vector hp = vector_add(ro, vector_scale(rd, t));
            h.point = hp;
            h.normal = rt_normal_triangle(&scene->triangles[i]);
            float u, v;
            uv_planar(hp, scene->triangles[i].v0, h.normal, &u, &v);
            const rt_material *m = &scene->materials[scene->triangles[i].material];
            h.albedo = material_sample(m, scene->textures, hp, u, v);
            h.reflectivity = m->reflectivity;
            h.hit = 1;
        }
    }

    for (int i = 0; i < scene->box_count; i++) {
        float t = rt_intersect_box(ro, rd, &scene->boxes[i]);
        if (t > 0.0f && t < closest_t) {
            closest_t = t;
            vector hp = vector_add(ro, vector_scale(rd, t));
            h.point = hp;
            h.normal = rt_normal_box(hp, &scene->boxes[i]);
            float u, v;
            uv_box(hp, h.normal, &u, &v);
            const rt_material *m = &scene->materials[scene->boxes[i].material];
            h.albedo = material_sample(m, scene->textures, hp, u, v);
            h.reflectivity = m->reflectivity;
            h.hit = 1;
        }
    }

    for (int i = 0; i < scene->sprite_count; i++) {
        vector spr_right, spr_up, spr_normal;
        float t = rt_intersect_sprite(ro, rd, &scene->sprites[i],
                                       camera_origin, &spr_right, &spr_up, &spr_normal);
        if (t > 0.0f && t < closest_t) {
            int frame_idx = rt_sprite_select_frame(&scene->sprites[i], camera_origin);
            const rt_frame *frame = &scene->sprites[i].frames[frame_idx];
            vector hp = vector_add(ro, vector_scale(rd, t));
            uint32_t pixel = rt_sprite_sample(&scene->sprites[i], frame,
                                               hp, spr_right, spr_up);
            uint8_t alpha = (pixel >> 24) & 0xFF;
            if (alpha == 0) continue;

            closest_t = t;
            h.point = hp;
            h.normal = spr_normal;
            h.albedo.r = (pixel >> 16) & 0xFF;
            h.albedo.g = (pixel >>  8) & 0xFF;
            h.albedo.b =  pixel        & 0xFF;
            h.reflectivity = 0.0f;
            h.hit = 1;
        }
    }

    for (int i = 0; i < scene->heightfield_count; i++) {
        const rt_heightfield *hf = &scene->heightfields[i];
        float t;
        vector hn;
        int cell_r, cell_c;
        if (rt_intersect_heightfield(hf, ro, rd, &t, &hn, &cell_r, &cell_c)) {
            if (t > 0.0f && t < closest_t) {
                closest_t = t;
                h.point = vector_add(ro, vector_scale(rd, t));
                h.normal = hn;
                int cells_per_row = hf->cols - 1;
                int ci = (cell_r * cells_per_row + cell_c) * 3;
                h.albedo.r = hf->colors[ci];
                h.albedo.g = hf->colors[ci + 1];
                h.albedo.b = hf->colors[ci + 2];
                h.reflectivity = 0.0f;
                h.hit = 1;
            }
        }
    }

    return h;
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

            float result_r = 0.0f, result_g = 0.0f, result_b = 0.0f;
            float thr_r = 1.0f, thr_g = 1.0f, thr_b = 1.0f;
            vector ro = camera->origin;
            vector rd = dir;

            for (int bounce = 0; bounce < RT_MAX_BOUNCES; bounce++) {
                hit_info h = closest_hit(ro, rd, scene, camera->origin);
                if (!h.hit) break;

                float shade = scene->ambient;
                for (int i = 0; i < scene->light_count; i++) {
                    float d = vector_dot(h.normal, scene->lights[i].direction);
                    if (d > 0.0f) shade += d * scene->lights[i].intensity;
                }
                if (shade > 1.0f) shade = 1.0f;

                float dw = 1.0f - h.reflectivity;
                result_r += thr_r * dw * (float)h.albedo.r * shade;
                result_g += thr_g * dw * (float)h.albedo.g * shade;
                result_b += thr_b * dw * (float)h.albedo.b * shade;

                if (h.reflectivity <= 0.0f) break;

                thr_r *= h.reflectivity;
                thr_g *= h.reflectivity;
                thr_b *= h.reflectivity;

                float ndotrd = vector_dot(h.normal, rd);
                rd = vector_normalize(vector_sub(rd, vector_scale(h.normal, 2.0f * ndotrd)));
                ro = vector_add(h.point, vector_scale(rd, RT_REFLECT_EPSILON));
            }

            uint8_t cr = result_r > 255.0f ? 255 : (result_r < 0.0f ? 0 : (uint8_t)result_r);
            uint8_t cg = result_g > 255.0f ? 255 : (result_g < 0.0f ? 0 : (uint8_t)result_g);
            uint8_t cb = result_b > 255.0f ? 255 : (result_b < 0.0f ? 0 : (uint8_t)result_b);

            pixel_buf[y * width + x] = (255u << 24) | (cr << 16) | (cg << 8) | cb;
        }
    }
}
