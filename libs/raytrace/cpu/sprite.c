#include "sprite.h"
#define _USE_MATH_DEFINES
#include <math.h>

float rt_intersect_sprite(vector ro, vector rd, const rt_sprite *spr,
                           vector cam_origin, vector *out_right,
                           vector *out_up, vector *out_normal) {
    /* Billboard normal: from sprite toward camera */
    vector to_cam = vector_sub(cam_origin, spr->position);
    vector normal = vector_normalize(to_cam);

    /* Ray-plane intersection */
    float denom = vector_dot(rd, normal);
    if (fabsf(denom) < 1e-6f) return -1.0f;
    float t = vector_dot(vector_sub(spr->position, ro), normal) / denom;
    if (t < 0.0f) return -1.0f;

    /* Billboard axes */
    vector world_up = {0.0f, 1.0f, 0.0f};
    vector right = vector_normalize(vector_cross(normal, world_up));
    if (vector_magnitude(right) < 0.001f) {
        vector world_fwd = {0.0f, 0.0f, 1.0f};
        right = vector_normalize(vector_cross(normal, world_fwd));
    }
    vector up = vector_cross(right, normal);

    /* Check if hit is within quad bounds */
    vector hp = vector_add(ro, vector_scale(rd, t));
    vector diff = vector_sub(hp, spr->position);
    float local_x = vector_dot(diff, right);
    float local_y = vector_dot(diff, up);

    float half_w = spr->width * 0.5f;
    float half_h = spr->height * 0.5f;
    if (local_x < -half_w || local_x > half_w ||
        local_y < -half_h || local_y > half_h)
        return -1.0f;

    *out_right = right;
    *out_up = up;
    *out_normal = normal;
    return t;
}

int rt_sprite_select_frame(const rt_sprite *spr, vector cam_origin) {
    if (spr->frame_count <= 1) return 0;

    vector to_cam = vector_sub(cam_origin, spr->position);
    float xz_mag = sqrtf(to_cam.x * to_cam.x + to_cam.z * to_cam.z);
    if (xz_mag < 1e-4f) return 0;

    float angle = atan2f(to_cam.x, to_cam.z) - atan2f(spr->direction.x, spr->direction.z);
    if (angle < 0.0f) angle += 2.0f * (float)M_PI;
    if (angle >= 2.0f * (float)M_PI) angle -= 2.0f * (float)M_PI;

    float sector = 2.0f * (float)M_PI / spr->frame_count;
    int index = (int)roundf(angle / sector) % spr->frame_count;
    return index;
}

uint32_t rt_sprite_sample(const rt_sprite *spr, const rt_frame *frame,
                            vector hp, vector right, vector up) {
    vector diff = vector_sub(hp, spr->position);
    float local_x = vector_dot(diff, right);
    float local_y = vector_dot(diff, up);

    float u = (local_x / spr->width) + 0.5f;
    float v = 0.5f - (local_y / spr->height);

    int px = (int)(u * frame->width);
    int py = (int)(v * frame->height);
    if (px < 0) px = 0;
    if (px >= frame->width) px = frame->width - 1;
    if (py < 0) py = 0;
    if (py >= frame->height) py = frame->height - 1;

    return frame->pixels[py * frame->width + px];
}

float rt_pick_sprite(vector ray_origin, vector ray_dir,
                     const rt_sprite *sprite, vector camera_origin,
                     vector *hit_point) {
    vector spr_right, spr_up, spr_normal;
    float t = rt_intersect_sprite(ray_origin, ray_dir, sprite, camera_origin,
                                   &spr_right, &spr_up, &spr_normal);
    if (t < 0.0f) return -1.0f;

    vector hp = vector_add(ray_origin, vector_scale(ray_dir, t));
    int frame_idx = rt_sprite_select_frame(sprite, camera_origin);
    const rt_frame *frame = &sprite->frames[frame_idx];
    uint32_t pixel = rt_sprite_sample(sprite, frame, hp, spr_right, spr_up);
    uint8_t alpha = (pixel >> 24) & 0xFF;
    if (alpha == 0) return -1.0f;

    if (hit_point) *hit_point = hp;
    return t;
}
