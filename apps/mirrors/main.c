/* Hall-of-mirrors demo: two parallel mirror walls, a reflective floor,
 * and a ring of colored spheres animated between them. The opposing
 * mirrors bounce ray paths back and forth, producing a receding
 * corridor of reflected copies. */

#include "renderer.h"
#include "viewport.h"
#include "scene.h"
#include "camera.h"
#include "sphere.h"
#include "plane.h"
#include "box.h"
#include <SDL2/SDL.h>

#define GL_GLEXT_PROTOTYPES 1
#include <GL/gl.h>
#include <GL/glext.h>

#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#define WINDOW_W 960
#define WINDOW_H 600
#define FOV (M_PI / 2.8f)
#define RENDER_SCALE_MIN 1
#define RENDER_SCALE_MAX 4

#define HALL_HALF_LEN 8.0f   /* along Z */
#define HALL_HALF_W   3.0f   /* along X — mirror-to-mirror half distance */
#define WALL_HEIGHT   6.0f
#define WALL_THICK    0.4f
#define FLOOR_Y       0.0f

#define ORB_COUNT 7
#define ORB_RING_R 1.8f

static int orb_material_ids[ORB_COUNT];
static int orb_sphere_ids[ORB_COUNT];

static rt_color hsv_to_rgb(float h, float s, float v) {
    float r = 0.0f, g = 0.0f, b = 0.0f;
    int i = (int)floorf(h * 6.0f);
    float f = h * 6.0f - (float)i;
    float p = v * (1.0f - s);
    float q = v * (1.0f - f * s);
    float t = v * (1.0f - (1.0f - f) * s);
    switch (i % 6) {
        case 0: r = v; g = t; b = p; break;
        case 1: r = q; g = v; b = p; break;
        case 2: r = p; g = v; b = t; break;
        case 3: r = p; g = q; b = v; break;
        case 4: r = t; g = p; b = v; break;
        case 5: r = v; g = p; b = q; break;
    }
    rt_color c;
    c.r = (uint8_t)(r * 255.0f);
    c.g = (uint8_t)(g * 255.0f);
    c.b = (uint8_t)(b * 255.0f);
    return c;
}

static void build_scene(rt_scene **scene_out, rt_camera **camera_out) {
    rt_scene *scene = rt_scene_create();

    int m_mirror = rt_scene_add_material(scene, (rt_material){
        .albedo = {10, 10, 14},
        .reflectivity = 1.0f,
    });
    int m_floor = rt_scene_add_material(scene, (rt_material){
        .albedo       = {20, 20, 28},
        .albedo2      = {60, 60, 80},
        .tex_kind     = RT_TEX_CHECKER,
        .tex_scale    = 1.0f,
        .reflectivity = 0.55f,
    });
    int m_ceiling = rt_scene_add_material(scene, (rt_material){
        .albedo = {30, 30, 40},
    });
    int m_endwall = rt_scene_add_material(scene, (rt_material){
        .albedo = {45, 20, 60},
    });

    /* Floor */
    rt_scene_add_plane(scene, (rt_plane){
        .point    = {0.0f, FLOOR_Y, 0.0f},
        .normal   = {0.0f, 1.0f, 0.0f},
        .material = m_floor,
    });
    /* Ceiling */
    rt_scene_add_plane(scene, (rt_plane){
        .point    = {0.0f, WALL_HEIGHT, 0.0f},
        .normal   = {0.0f, -1.0f, 0.0f},
        .material = m_ceiling,
    });

    /* Left mirror wall (facing +X) */
    rt_scene_add_box(scene, (rt_box){
        .min = {-HALL_HALF_W - WALL_THICK, FLOOR_Y,       -HALL_HALF_LEN},
        .max = {-HALL_HALF_W,               WALL_HEIGHT,   HALL_HALF_LEN},
        .material = m_mirror,
    });
    /* Right mirror wall (facing -X) */
    rt_scene_add_box(scene, (rt_box){
        .min = { HALL_HALF_W,               FLOOR_Y,       -HALL_HALF_LEN},
        .max = { HALL_HALF_W + WALL_THICK,  WALL_HEIGHT,   HALL_HALF_LEN},
        .material = m_mirror,
    });
    /* End caps (non-mirror, so you can see into the hall) */
    rt_scene_add_box(scene, (rt_box){
        .min = {-HALL_HALF_W - WALL_THICK, FLOOR_Y,      -HALL_HALF_LEN - WALL_THICK},
        .max = { HALL_HALF_W + WALL_THICK, WALL_HEIGHT, -HALL_HALF_LEN},
        .material = m_endwall,
    });
    rt_scene_add_box(scene, (rt_box){
        .min = {-HALL_HALF_W - WALL_THICK, FLOOR_Y,      HALL_HALF_LEN},
        .max = { HALL_HALF_W + WALL_THICK, WALL_HEIGHT, HALL_HALF_LEN + WALL_THICK},
        .material = m_endwall,
    });

    /* Ring of colored orbs — stored indices let us animate positions per frame. */
    for (int i = 0; i < ORB_COUNT; i++) {
        float hue = (float)i / (float)ORB_COUNT;
        rt_color c = hsv_to_rgb(hue, 0.85f, 1.0f);
        orb_material_ids[i] = rt_scene_add_material(scene, (rt_material){
            .albedo = c,
        });
        orb_sphere_ids[i] = rt_scene_add_sphere(scene, (rt_sphere){
            .center   = {0.0f, 1.6f, 0.0f},
            .radius   = 0.55f,
            .material = orb_material_ids[i],
        });
    }

    /* A single chrome sphere at the center — its reflection drives the eye
     * inward and multiplies via the side mirrors. */
    int m_chrome = rt_scene_add_material(scene, (rt_material){
        .albedo       = {200, 200, 210},
        .reflectivity = 0.9f,
    });
    rt_scene_add_sphere(scene, (rt_sphere){
        .center   = {0.0f, 1.6f, 0.0f},
        .radius   = 0.9f,
        .material = m_chrome,
    });

    rt_scene_set_ambient(scene, 0.18f);
    rt_scene_add_light(scene, (rt_light){
        .direction = {0.3f, 1.0f, 0.2f},
        .intensity = 0.7f,
    });
    rt_scene_add_light(scene, (rt_light){
        .direction = {-0.5f, 0.4f, -0.8f},
        .intensity = 0.35f,
    });

    *scene_out = scene;
    *camera_out = rt_camera_create(
        (vector){0.0f, 2.2f, 6.0f},
        (vector){0.0f, -0.1f, -1.0f}
    );
}

static void animate_orbs(rt_scene *scene, float t) {
    for (int i = 0; i < ORB_COUNT; i++) {
        float phase = (float)i / (float)ORB_COUNT * (float)(2.0 * M_PI);
        float angle = t * 0.6f + phase;
        /* Orbit about the central chrome sphere, bobbing vertically. */
        float x = cosf(angle) * ORB_RING_R * 0.6f;
        float z = sinf(angle) * ORB_RING_R;
        float y = 1.6f + sinf(t * 1.4f + phase * 2.0f) * 0.6f;
        scene->spheres[orb_sphere_ids[i]].center = (vector){x, y, z};
    }
}

static vector cam_dir_from_yaw_pitch(float yaw, float pitch) {
    return (vector){
        cosf(pitch) * sinf(yaw),
        sinf(pitch),
        cosf(pitch) * cosf(yaw)
    };
}

static void display_pixels(GLuint tex, GLuint fbo, const uint32_t *pixels,
                           int render_w, int render_h,
                           int window_w, int window_h) {
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, render_w, render_h,
                    GL_BGRA, GL_UNSIGNED_BYTE, pixels);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_2D, tex, 0);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    glBlitFramebuffer(0, 0, render_w, render_h,
                      0, window_h, window_w, 0,
                      GL_COLOR_BUFFER_BIT, GL_NEAREST);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
}

int main(void) {
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        fprintf(stderr, "SDL init failed: %s\n", SDL_GetError());
        return 1;
    }

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

    SDL_Window *window = SDL_CreateWindow("Hall of Mirrors",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        WINDOW_W, WINDOW_H, SDL_WINDOW_OPENGL);
    if (!window) {
        fprintf(stderr, "Window creation failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    SDL_GLContext gl_ctx = SDL_GL_CreateContext(window);
    if (!gl_ctx) {
        fprintf(stderr, "GL context creation failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    SDL_GL_SetSwapInterval(1);

    rt_renderer *cpu_rnd = rt_renderer_available(RT_BACKEND_CPU)
                         ? rt_renderer_create(RT_BACKEND_CPU) : NULL;
    rt_renderer *gpu_rnd = rt_renderer_available(RT_BACKEND_OPENGL)
                         ? rt_renderer_create(RT_BACKEND_OPENGL) : NULL;
    if (!cpu_rnd && !gpu_rnd) {
        fprintf(stderr, "No renderers available\n");
        SDL_GL_DeleteContext(gl_ctx);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    rt_renderer *active = gpu_rnd ? gpu_rnd : cpu_rnd;
    fprintf(stderr, "Active: %s (TAB to toggle, SPACE auto-orbit, WASD/arrows manual)\n",
            rt_renderer_name(active));

    rt_scene *scene;
    rt_camera *camera;
    build_scene(&scene, &camera);

    int render_scale = 1;
    int render_w = WINDOW_W / render_scale;
    int render_h = WINDOW_H / render_scale;
    uint32_t *pixels = calloc((size_t)(render_w * render_h), sizeof(uint32_t));
    rt_viewport viewport = { render_w, render_h, FOV };

    GLuint display_tex, display_fbo;
    glGenTextures(1, &display_tex);
    glBindTexture(GL_TEXTURE_2D, display_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, render_w, render_h, 0,
                 GL_BGRA, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glGenFramebuffers(1, &display_fbo);

    vector cam_pos = {0.0f, 2.2f, 6.0f};
    float cam_yaw = 0.0f;    /* looking toward -Z */
    float cam_pitch = -0.1f;
    float move_speed = 4.0f;
    float look_speed = 1.8f;
    int auto_orbit = 1;
    int running = 1;

    Uint32 fps_last = SDL_GetTicks();
    Uint32 frame_last = SDL_GetTicks();
    Uint32 start_ticks = SDL_GetTicks();
    int fps_frames = 0;
    Uint32 render_ms_accum = 0;
    char title_buf[160];

    while (running) {
        Uint32 frame_now = SDL_GetTicks();
        float dt = (frame_now - frame_last) / 1000.0f;
        frame_last = frame_now;
        float t = (frame_now - start_ticks) / 1000.0f;

        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) running = 0;
            if (e.type == SDL_KEYDOWN) {
                if (e.key.keysym.sym == SDLK_ESCAPE) running = 0;
                if (e.key.keysym.sym == SDLK_TAB) {
                    if (active == cpu_rnd && gpu_rnd) active = gpu_rnd;
                    else if (active == gpu_rnd && cpu_rnd) active = cpu_rnd;
                    fprintf(stderr, "Active: %s\n", rt_renderer_name(active));
                }
                if (e.key.keysym.sym == SDLK_SPACE) {
                    auto_orbit = !auto_orbit;
                    fprintf(stderr, "Auto-orbit: %s\n", auto_orbit ? "on" : "off");
                }
                if (e.key.keysym.sym == SDLK_MINUS ||
                    e.key.keysym.sym == SDLK_KP_MINUS) {
                    if (render_scale < RENDER_SCALE_MAX) {
                        render_scale++;
                        goto recreate_buffers;
                    }
                }
                if (e.key.keysym.sym == SDLK_EQUALS ||
                    e.key.keysym.sym == SDLK_KP_PLUS) {
                    if (render_scale > RENDER_SCALE_MIN) {
                        render_scale--;
                        goto recreate_buffers;
                    }
                }
                continue;
                recreate_buffers:
                    render_w = WINDOW_W / render_scale;
                    render_h = WINDOW_H / render_scale;
                    free(pixels);
                    pixels = calloc((size_t)(render_w * render_h), sizeof(uint32_t));
                    viewport = (rt_viewport){ render_w, render_h, FOV };
                    glBindTexture(GL_TEXTURE_2D, display_tex);
                    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, render_w, render_h, 0,
                                 GL_BGRA, GL_UNSIGNED_BYTE, NULL);
                    fprintf(stderr, "Render scale: 1/%d (%dx%d)\n",
                            render_scale, render_w, render_h);
            }
        }

        const Uint8 *keys = SDL_GetKeyboardState(NULL);
        int manual_input = keys[SDL_SCANCODE_W] || keys[SDL_SCANCODE_S] ||
                           keys[SDL_SCANCODE_A] || keys[SDL_SCANCODE_D] ||
                           keys[SDL_SCANCODE_LEFT] || keys[SDL_SCANCODE_RIGHT] ||
                           keys[SDL_SCANCODE_UP] || keys[SDL_SCANCODE_DOWN] ||
                           keys[SDL_SCANCODE_LSHIFT] || keys[SDL_SCANCODE_RSHIFT];
        if (manual_input) auto_orbit = 0;

        if (auto_orbit) {
            /* Slow orbit inside the hall — stays within the mirror channel. */
            float r = 4.5f;
            cam_pos.x = sinf(t * 0.25f) * (HALL_HALF_W - 0.8f) * 0.35f;
            cam_pos.y = 2.2f + sinf(t * 0.4f) * 0.3f;
            cam_pos.z = cosf(t * 0.18f) * r;
            vector look_at = {0.0f, 1.8f, 0.0f};
            vector dir = vector_normalize(vector_sub(look_at, cam_pos));
            cam_yaw = atan2f(dir.x, dir.z);
            cam_pitch = asinf(dir.y);
        } else {
            if (keys[SDL_SCANCODE_LEFT])  cam_yaw   -= look_speed * dt;
            if (keys[SDL_SCANCODE_RIGHT]) cam_yaw   += look_speed * dt;
            if (keys[SDL_SCANCODE_UP])    cam_pitch += look_speed * dt;
            if (keys[SDL_SCANCODE_DOWN])  cam_pitch -= look_speed * dt;
            if (cam_pitch >  1.4f) cam_pitch =  1.4f;
            if (cam_pitch < -1.4f) cam_pitch = -1.4f;

            vector forward = { sinf(cam_yaw), 0.0f, cosf(cam_yaw) };
            vector right   = { cosf(cam_yaw), 0.0f, -sinf(cam_yaw) };
            if (keys[SDL_SCANCODE_W]) cam_pos = vector_add(cam_pos, vector_scale(forward,  move_speed * dt));
            if (keys[SDL_SCANCODE_S]) cam_pos = vector_add(cam_pos, vector_scale(forward, -move_speed * dt));
            if (keys[SDL_SCANCODE_D]) cam_pos = vector_add(cam_pos, vector_scale(right,    move_speed * dt));
            if (keys[SDL_SCANCODE_A]) cam_pos = vector_add(cam_pos, vector_scale(right,   -move_speed * dt));
            if (keys[SDL_SCANCODE_RSHIFT] || keys[SDL_SCANCODE_LSHIFT]) cam_pos.y -= move_speed * dt;
        }

        animate_orbs(scene, t);

        vector cam_dir = cam_dir_from_yaw_pitch(cam_yaw, cam_pitch);
        rt_camera_place(camera, cam_pos, cam_dir);

        Uint32 r_start = SDL_GetTicks();
        rt_renderer_render(active, scene, camera, &viewport, pixels);
        render_ms_accum += SDL_GetTicks() - r_start;

        display_pixels(display_tex, display_fbo, pixels,
                       render_w, render_h, WINDOW_W, WINDOW_H);
        SDL_GL_SwapWindow(window);

        fps_frames++;
        Uint32 now = SDL_GetTicks();
        if (now - fps_last >= 1000) {
            float avg_ms = (fps_frames > 0)
                ? (float)render_ms_accum / (float)fps_frames : 0.0f;
            snprintf(title_buf, sizeof(title_buf),
                     "Hall of Mirrors - %s %d FPS (%.2f ms, %dx%d, 1/%d) %s",
                     rt_renderer_name(active), fps_frames, avg_ms,
                     render_w, render_h, render_scale,
                     auto_orbit ? "[orbit]" : "[manual]");
            SDL_SetWindowTitle(window, title_buf);
            fps_frames = 0;
            render_ms_accum = 0;
            fps_last = now;
        }
    }

    glDeleteFramebuffers(1, &display_fbo);
    glDeleteTextures(1, &display_tex);
    if (cpu_rnd) rt_renderer_destroy(cpu_rnd);
    if (gpu_rnd) rt_renderer_destroy(gpu_rnd);
    free(pixels);
    rt_camera_destroy(camera);
    rt_scene_destroy(scene);
    SDL_GL_DeleteContext(gl_ctx);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
