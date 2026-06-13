#include "renderer.h"
#include "viewport.h"
#include "scene.h"
#include "scene_accel.h"
#include "mesh.h"
#include <SDL2/SDL.h>
#define GL_GLEXT_PROTOTYPES 1
#include "gl_compat.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* =============================== Noise ==================================== */

static uint32_t hash32(int x, int y, int z) {
    uint32_t n = ((uint32_t)x * 73856093U) ^ ((uint32_t)y * 19349663U) ^ ((uint32_t)z * 83492791U);
    n *= 2654435761U; n ^= n >> 13; n *= 2654435761U;
    return n;
}

static float vnoise(float x, float y, float z) {
    int ix = (int)floorf(x), iy = (int)floorf(y), iz = (int)floorf(z);
    float fx = x - (float)ix, fy = y - (float)iy, fz = z - (float)iz;
    float sx = fx * fx * (3.0f - 2.0f * fx);
    float sy = fy * fy * (3.0f - 2.0f * fy);
    float sz = fz * fz * (3.0f - 2.0f * fz);
    float v000 = (float)(hash32(ix, iy, iz) & 0xFFFFFF) / 16777216.0f;
    float v100 = (float)(hash32(ix+1, iy, iz) & 0xFFFFFF) / 16777216.0f;
    float v010 = (float)(hash32(ix, iy+1, iz) & 0xFFFFFF) / 16777216.0f;
    float v110 = (float)(hash32(ix+1, iy+1, iz) & 0xFFFFFF) / 16777216.0f;
    float v001 = (float)(hash32(ix, iy, iz+1) & 0xFFFFFF) / 16777216.0f;
    float v101 = (float)(hash32(ix+1, iy, iz+1) & 0xFFFFFF) / 16777216.0f;
    float v011 = (float)(hash32(ix, iy+1, iz+1) & 0xFFFFFF) / 16777216.0f;
    float v111 = (float)(hash32(ix+1, iy+1, iz+1) & 0xFFFFFF) / 16777216.0f;
    float nx00 = v000 + (v100 - v000) * sx;
    float nx10 = v010 + (v110 - v010) * sx;
    float nx01 = v001 + (v101 - v001) * sx;
    float nx11 = v011 + (v111 - v011) * sx;
    float nxy0 = nx00 + (nx10 - nx00) * sy;
    float nxy1 = nx01 + (nx11 - nx01) * sy;
    return nxy0 + (nxy1 - nxy0) * sz;
}

static float fbm(float x, float y, int octaves) {
    float sum = 0.0f, amp = 1.0f, freq = 1.0f, max_amp = 0.0f;
    for (int i = 0; i < octaves; i++) {
        sum += amp * vnoise(x * freq, y * freq, 0.5f);
        max_amp += amp;
        amp *= 0.5f;
        freq *= 2.0f;
    }
    return sum / max_amp;
}

/* =========================== Terrain ====================================== */

#define HF_ROWS 80
#define HF_COLS 80
#define HF_WW 60.0f
#define HF_WD 60.0f
#define HF_MAX_H 5.0f

static float   hf_heights[HF_ROWS * HF_COLS];
static float   hf_normals[HF_ROWS * HF_COLS * 3];
static uint8_t hf_colors[(HF_ROWS - 1) * (HF_COLS - 1) * 3];

static void generate_terrain(void) {
    float ox = -HF_WW * 0.5f, oz = -HF_WD * 0.5f;
    float cell_w = HF_WW / (float)(HF_COLS - 1);
    float cell_d = HF_WD / (float)(HF_ROWS - 1);

    for (int r = 0; r < HF_ROWS; r++) {
        for (int c = 0; c < HF_COLS; c++) {
            float wx = ox + c * cell_w;
            float wz = oz + r * cell_d;
            float h = fbm(wx * 0.3f, wz * 0.3f, 4);
            h = h * HF_MAX_H * 0.6f + 0.5f;
            hf_heights[r * HF_COLS + c] = h;
        }
    }

    for (int r = 0; r < HF_ROWS; r++) {
        for (int c = 0; c < HF_COLS; c++) {
            int cl = c > 0 ? c - 1 : c;
            int cr = c < HF_COLS - 1 ? c + 1 : c;
            int ru = r > 0 ? r - 1 : r;
            int rd = r < HF_ROWS - 1 ? r + 1 : r;
            float dx = (hf_heights[r * HF_COLS + cr] - hf_heights[r * HF_COLS + cl]) / (2.0f * cell_w);
            float dz = (hf_heights[rd * HF_COLS + c] - hf_heights[ru * HF_COLS + c]) / (2.0f * cell_d);
            vector n = vector_normalize((vector){-dx, 1.0f, -dz});
            int i = (r * HF_COLS + c) * 3;
            hf_normals[i]     = n.x;
            hf_normals[i + 1] = n.y;
            hf_normals[i + 2] = n.z;
        }
    }

    for (int r = 0; r < HF_ROWS - 1; r++) {
        for (int c = 0; c < HF_COLS - 1; c++) {
            float h = hf_heights[r * HF_COLS + c];
            float t = h / HF_MAX_H;
            uint8_t r_col, g_col, b_col;
            if (t < 0.25f) {
                r_col = 50;  g_col = 110; b_col = 40;
            } else if (t < 0.55f) {
                r_col = 80;  g_col = 140; b_col = 55;
            } else if (t < 0.75f) {
                r_col = 130; g_col = 125; b_col = 75;
            } else {
                r_col = 180; g_col = 170; b_col = 150;
            }
            int i = (r * (HF_COLS - 1) + c) * 3;
            hf_colors[i]     = r_col;
            hf_colors[i + 1] = g_col;
            hf_colors[i + 2] = b_col;
        }
    }
}

static float height_at(float x, float z) {
    float ox = -HF_WW * 0.5f, oz = -HF_WD * 0.5f;
    float cell_w = HF_WW / (float)(HF_COLS - 1);
    float cell_d = HF_WD / (float)(HF_ROWS - 1);
    float gx = (x - ox) / cell_w;
    float gz = (z - oz) / cell_d;
    if (gx < 0.0f) gx = 0.0f; else if (gx > (float)(HF_COLS - 2)) gx = (float)(HF_COLS - 2);
    if (gz < 0.0f) gz = 0.0f; else if (gz > (float)(HF_ROWS - 2)) gz = (float)(HF_ROWS - 2);
    int c0 = (int)floorf(gx), r0 = (int)floorf(gz);
    if (c0 >= HF_COLS - 1) c0 = HF_COLS - 2;
    if (r0 >= HF_ROWS - 1) r0 = HF_ROWS - 2;
    float fx = gx - (float)c0, fz = gz - (float)r0;
    float h00 = hf_heights[r0 * HF_COLS + c0];
    float h10 = hf_heights[r0 * HF_COLS + c0 + 1];
    float h01 = hf_heights[(r0 + 1) * HF_COLS + c0];
    float h11 = hf_heights[(r0 + 1) * HF_COLS + c0 + 1];
    float h = h00 * (1.0f - fx) * (1.0f - fz)
            + h10 * fx * (1.0f - fz)
            + h01 * (1.0f - fx) * fz
            + h11 * fx * fz;
    return h;
}

/* ========================== Axis-Angle Rotation ============================ */

static inline mat4 mat4_rotate_axis(float angle, vector axis) {
    float c = cosf(angle), s = sinf(angle), t = 1.0f - c;
    float x = axis.x, y = axis.y, z = axis.z;
    mat4 r = mat4_identity();
    r.m[ 0] = t * x * x + c;
    r.m[ 1] = t * x * y - s * z;
    r.m[ 2] = t * x * z + s * y;
    r.m[ 4] = t * x * y + s * z;
    r.m[ 5] = t * y * y + c;
    r.m[ 6] = t * y * z - s * x;
    r.m[ 8] = t * x * z - s * y;
    r.m[ 9] = t * y * z + s * x;
    r.m[10] = t * z * z + c;
    return r;
}

/* =========================== Scene ======================================== */

#define MARBLE_RADIUS 0.4f

typedef struct {
    vector pos;
    float  yaw;
    mat4   rot;             /* accumulated world-space orientation */
} marble_state;

static int build_scene(scene *s, int *out_marble_mat) {
    int m_ground = scene_add_material(s, (scene_material){
        .albedo    = {255, 255, 255},
        .albedo2   = {0, 0, 0},
        .tex_kind  = SCENE_TEX_NONE,
        .tex_scale = 1.0f,
        .reflectivity = 0.0f,
    });

    scene_add_heightfield(s, &(scene_heightfield){
        .heights     = hf_heights,
        .colors      = hf_colors,
        .normals     = hf_normals,
        .rows        = HF_ROWS,
        .cols        = HF_COLS,
        .world_width = HF_WW,
        .world_depth = HF_WD,
        .origin_x    = -HF_WW * 0.5f,
        .origin_z    = -HF_WD * 0.5f,
        .max_height  = HF_MAX_H + 0.5f,
        .material    = m_ground,
    });

    int m_marble = scene_add_material(s, (scene_material){
        .albedo       = {30, 50, 90},
        .albedo2      = {220, 210, 180},
        .tex_kind     = SCENE_TEX_CHECKER,
        .tex_scale    = 0.25f,
        .reflectivity = 0.2f,
        .world_to_obj = mat4_identity(),
    });

    *out_marble_mat = m_marble;
    return m_marble;
}

/* ============================ Main ======================================== */

#define INIT_W 960
#define INIT_H 600
#define FOV (M_PI / 2.8f)
#define MOVE_SPEED 4.0f

static void place_camera(scene_camera *cam, vector target, float dist, float yaw, float pitch) {
    vector dir = {
        cosf(pitch) * sinf(yaw),
        sinf(pitch),
        cosf(pitch) * cosf(yaw),
    };
    vector pos = vector_add(target, vector_scale(dir, -dist));
    scene_camera_place(cam, pos, dir);
}

int main(int argc, char *argv[]) {
    (void)argc; (void)argv;

    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

    int window_w = INIT_W, window_h = INIT_H;
    SDL_Window *window = SDL_CreateWindow("Marble",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        window_w, window_h, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
    if (!window) {
        fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    SDL_GLContext gl_ctx = SDL_GL_CreateContext(window);
    SDL_GL_SetSwapInterval(0);
    gl_compat_init((gl_compat_loader_fn)SDL_GL_GetProcAddress);

    rt_renderer *cpu_rnd = rt_renderer_available(RT_BACKEND_CPU)
                         ? rt_renderer_create(RT_BACKEND_CPU) : NULL;
    rt_renderer *gpu_rnd = rt_renderer_available(RT_BACKEND_OPENGL)
                         ? rt_renderer_create(RT_BACKEND_OPENGL) : NULL;
    if (!cpu_rnd && !gpu_rnd) {
        fprintf(stderr, "No renderer backend available\n");
        SDL_GL_DeleteContext(gl_ctx);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    rt_renderer *active = cpu_rnd ? cpu_rnd : gpu_rnd;

    generate_terrain();

    float cam_dist = 8.0f, cam_yaw = 0.0f, cam_pitch = -0.4f;

    marble_state marble;
    marble.pos      = (vector){0.0f, height_at(0.0f, 0.0f) + MARBLE_RADIUS, 0.0f};
    marble.yaw      = 0.0f;
    marble.rot      = mat4_identity();

    scene *s = scene_create();
    scene_set_ambient(s, 0.18f);
    scene_add_light(s, (scene_light){
        .direction = {1.0f, 1.2f, -0.8f}, .intensity = 0.55f });
    int marble_mat;
    build_scene(s, &marble_mat);

    vector cam_dir = { sinf(cam_yaw) * cosf(cam_pitch), sinf(cam_pitch), cosf(cam_yaw) * cosf(cam_pitch) };
    vector cam_pos = vector_add(marble.pos, vector_scale(cam_dir, -cam_dist));
    scene_camera *cam = scene_camera_create(cam_pos, cam_dir);
    rt_scene_build_accel(s);

    int render_w = 640, render_h = 400;
    rt_viewport viewport = { render_w, render_h, FOV };
    uint32_t *pixels = calloc((size_t)(render_w * render_h), sizeof(uint32_t));

    GLuint display_tex;
    glGenTextures(1, &display_tex);
    glBindTexture(GL_TEXTURE_2D, display_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, render_w, render_h, 0,
                 GL_BGRA, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    GLuint display_fbo;
    glGenFramebuffers(1, &display_fbo);

    int running = 1;
    int mouse_captured = 0;
    Uint32 frame_last = SDL_GetTicks();
    Uint32 fps_last = SDL_GetTicks();
    int fps_frames = 0;
    Uint32 render_ms_accum = 0;
    char title_buf[200];

    while (running) {
        Uint32 frame_now = SDL_GetTicks();
        float dt = (float)(frame_now - frame_last) / 1000.0f;
        if (dt > 0.1f) dt = 0.1f;
        frame_last = frame_now;

        /* ---- Events ---- */
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) { running = 0; }
            else if (e.type == SDL_KEYDOWN) {
                SDL_Keycode k = e.key.keysym.sym;
                if (k == SDLK_ESCAPE) { running = 0; }
                if (k == SDLK_TAB) {
                    if (active == cpu_rnd && gpu_rnd) active = gpu_rnd;
                    else if (active == gpu_rnd && cpu_rnd) active = cpu_rnd;
                }
                if (k == SDLK_m && !mouse_captured) {
                    SDL_SetRelativeMouseMode(SDL_TRUE);
                    mouse_captured = 1;
                }
                if (k == SDLK_F11) {
                    int flags = SDL_GetWindowFlags(window);
                    int fs = (flags & SDL_WINDOW_FULLSCREEN_DESKTOP) ? 1 : 0;
                    SDL_SetWindowFullscreen(window, fs ? 0 : SDL_WINDOW_FULLSCREEN_DESKTOP);
                }
            }
            else if (e.type == SDL_KEYUP) {
                if (e.key.keysym.sym == SDLK_ESCAPE && mouse_captured) {
                    SDL_SetRelativeMouseMode(SDL_FALSE);
                    mouse_captured = 0;
                }
            }
            else if (e.type == SDL_WINDOWEVENT) {
                if (e.window.event == SDL_WINDOWEVENT_RESIZED) {
                    window_w = e.window.data1; window_h = e.window.data2;
                }
            }
            else if (e.type == SDL_MOUSEMOTION && mouse_captured) {
                cam_yaw   -= (float)e.motion.xrel * 0.005f;
                cam_pitch += (float)e.motion.yrel * 0.005f;
                if (cam_pitch >  1.3f) cam_pitch =  1.3f;
                if (cam_pitch < -1.3f) cam_pitch = -1.3f;
            }
        }

        /* ---- Marble movement ---- */
        const Uint8 *keys = SDL_GetKeyboardState(NULL);
        float fwd = 0.0f, turn = 0.0f;
        if (keys[SDL_SCANCODE_W] || keys[SDL_SCANCODE_UP])    fwd  =  1.0f;
        if (keys[SDL_SCANCODE_S] || keys[SDL_SCANCODE_DOWN])  fwd  = -1.0f;
        if (keys[SDL_SCANCODE_D] || keys[SDL_SCANCODE_RIGHT]) turn =  1.0f;
        if (keys[SDL_SCANCODE_A] || keys[SDL_SCANCODE_LEFT])  turn = -1.0f;

        marble.yaw += turn * 2.0f * dt;

        float speed = MOVE_SPEED * dt;
        float dx = fwd * sinf(marble.yaw) * speed;
        float dz = fwd * cosf(marble.yaw) * speed;

        vector new_pos = marble.pos;
        new_pos.x += dx;
        new_pos.z += dz;

        float half = HF_WW * 0.4f;
        if (new_pos.x < -half) new_pos.x = -half;
        if (new_pos.x >  half) new_pos.x =  half;
        if (new_pos.z < -half) new_pos.z = -half;
        if (new_pos.z >  half) new_pos.z =  half;

        new_pos.y = height_at(new_pos.x, new_pos.z) + MARBLE_RADIUS;
        marble.pos = new_pos;

        /* ---- Accumulate rolling rotation ---- */
        float dist = sqrtf(dx * dx + dz * dz);
        if (dist > 0.0001f) {
            float angle = dist / MARBLE_RADIUS;
            vector axis = {dz / dist, 0.0f, -dx / dist};
            mat4 Rf = mat4_rotate_axis(angle, axis);
            marble.rot = mat4_mul(Rf, marble.rot);
        }

        /* ---- Update marble world_to_obj ---- */
        mat4 invR = mat4_affine_inverse(marble.rot);
        mat4 T = mat4_translate((vector){-marble.pos.x, -marble.pos.y, -marble.pos.z});
        s->materials[marble_mat].world_to_obj = mat4_mul(invR, T);

        /* ---- Update marble sphere position ---- */
        if (s->sphere_count > 0) {
            s->spheres[0].center = marble.pos;
        } else {
            scene_add_sphere(s, (scene_sphere){
                .center  = marble.pos,
                .radius  = MARBLE_RADIUS,
                .material = marble_mat,
            });
        }

        /* ---- Camera follows marble yaw ---- */
        float yaw_diff = marble.yaw - cam_yaw;
        while (yaw_diff > M_PI) yaw_diff -= 2.0f * M_PI;
        while (yaw_diff < -M_PI) yaw_diff += 2.0f * M_PI;
        cam_yaw += yaw_diff * 3.0f * dt;

        /* ---- Camera ---- */
        place_camera(cam, marble.pos, cam_dist, cam_yaw, cam_pitch);

        /* ---- Render ---- */
        Uint32 r_start = SDL_GetTicks();
        rt_renderer_render(active, s, cam, &viewport, pixels, NULL);
        render_ms_accum += SDL_GetTicks() - r_start;

        /* ---- Display ---- */
        glBindTexture(GL_TEXTURE_2D, display_tex);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, render_w, render_h,
                        GL_BGRA, GL_UNSIGNED_BYTE, pixels);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, display_fbo);
        glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                               GL_TEXTURE_2D, display_tex, 0);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
        glBlitFramebuffer(0, 0, render_w, render_h,
                          0, window_h, window_w, 0,
                          GL_COLOR_BUFFER_BIT, GL_NEAREST);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
        SDL_GL_SwapWindow(window);

        /* ---- FPS ---- */
        fps_frames++;
        Uint32 now = SDL_GetTicks();
        if (now - fps_last >= 1000) {
            float avg_ms = (float)render_ms_accum / (float)fps_frames;
            snprintf(title_buf, sizeof(title_buf),
                     "Marble  |  %s  %d FPS  %.1f ms",
                     rt_renderer_name(active), fps_frames, avg_ms);
            SDL_SetWindowTitle(window, title_buf);
            fps_frames = 0;
            render_ms_accum = 0;
            fps_last = now;
        }
    }

    /* ---- Cleanup ---- */
    glDeleteFramebuffers(1, &display_fbo);
    glDeleteTextures(1, &display_tex);
    if (cpu_rnd) rt_renderer_destroy(cpu_rnd);
    if (gpu_rnd) rt_renderer_destroy(gpu_rnd);
    free(pixels);
    scene_camera_destroy(cam);
    scene_destroy(s);
    SDL_GL_DeleteContext(gl_ctx);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
