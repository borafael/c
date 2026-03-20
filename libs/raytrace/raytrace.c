#include "raytrace.h"
#include <math.h>
#include <float.h>

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
                float t = rt_intersect_sphere(camera->origin, dir, &scene->spheres[i]);
                if (t > 0.0f && t < closest_t) {
                    closest_t = t;
                    vector hp = vector_add(camera->origin, vector_scale(dir, t));
                    normal = rt_normal_sphere(hp, &scene->spheres[i]);
                    color = scene->spheres[i].color;
                    hit = 1;
                }
            }

            /* Planes */
            for (int i = 0; i < scene->plane_count; i++) {
                float t = rt_intersect_plane(camera->origin, dir, &scene->planes[i]);
                if (t > 0.0f && t < closest_t) {
                    closest_t = t;
                    normal = rt_normal_plane(&scene->planes[i]);
                    color = scene->planes[i].color;
                    hit = 1;
                }
            }

            /* Discs */
            for (int i = 0; i < scene->disc_count; i++) {
                float t = rt_intersect_disc(camera->origin, dir, &scene->discs[i]);
                if (t > 0.0f && t < closest_t) {
                    closest_t = t;
                    normal = rt_normal_disc(&scene->discs[i]);
                    color = scene->discs[i].color;
                    hit = 1;
                }
            }

            /* Cylinders */
            for (int i = 0; i < scene->cylinder_count; i++) {
                float t = rt_intersect_cylinder(camera->origin, dir, &scene->cylinders[i]);
                if (t > 0.0f && t < closest_t) {
                    closest_t = t;
                    vector hp = vector_add(camera->origin, vector_scale(dir, t));
                    normal = rt_normal_cylinder(hp, &scene->cylinders[i]);
                    color = scene->cylinders[i].color;
                    hit = 1;
                }
            }

            /* Triangles */
            for (int i = 0; i < scene->triangle_count; i++) {
                float t = rt_intersect_triangle(camera->origin, dir, &scene->triangles[i]);
                if (t > 0.0f && t < closest_t) {
                    closest_t = t;
                    normal = rt_normal_triangle(&scene->triangles[i]);
                    color = scene->triangles[i].color;
                    hit = 1;
                }
            }

            /* Boxes */
            for (int i = 0; i < scene->box_count; i++) {
                float t = rt_intersect_box(camera->origin, dir, &scene->boxes[i]);
                if (t > 0.0f && t < closest_t) {
                    closest_t = t;
                    vector hp = vector_add(camera->origin, vector_scale(dir, t));
                    normal = rt_normal_box(hp, &scene->boxes[i]);
                    color = scene->boxes[i].color;
                    hit = 1;
                }
            }

            /* Sprites */
            for (int i = 0; i < scene->sprite_count; i++) {
                vector spr_right, spr_up, spr_normal;
                float t = rt_intersect_sprite(camera->origin, dir,
                                               &scene->sprites[i], camera->origin,
                                               &spr_right, &spr_up, &spr_normal);
                if (t > 0.0f && t < closest_t) {
                    int frame_idx = rt_sprite_select_frame(&scene->sprites[i],
                                                            camera->origin);
                    const rt_frame *frame = &scene->sprites[i].frames[frame_idx];
                    vector hp = vector_add(camera->origin, vector_scale(dir, t));
                    uint32_t pixel = rt_sprite_sample(&scene->sprites[i], frame,
                                                       hp, spr_right, spr_up);
                    uint8_t alpha = (pixel >> 24) & 0xFF;
                    if (alpha == 0) continue; /* transparent — skip */

                    closest_t = t;
                    normal = spr_normal;
                    color.r = (pixel >> 16) & 0xFF;
                    color.g = (pixel >>  8) & 0xFF;
                    color.b =  pixel        & 0xFF;
                    hit = 1;
                }
            }

            /* Heightfields */
            for (int i = 0; i < scene->heightfield_count; i++) {
                const rt_heightfield *hf = &scene->heightfields[i];
                float t;
                vector hn;
                int cell_r, cell_c;
                if (rt_intersect_heightfield(hf, camera->origin, dir,
                                              &t, &hn, &cell_r, &cell_c)) {
                    if (t > 0.0f && t < closest_t) {
                        closest_t = t;
                        normal = hn;
                        int cells_per_row = hf->cols - 1;
                        int ci = (cell_r * cells_per_row + cell_c) * 3;
                        color.r = hf->colors[ci];
                        color.g = hf->colors[ci + 1];
                        color.b = hf->colors[ci + 2];
                        hit = 1;
                    }
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
