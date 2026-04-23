/* Orb: a textured inner sphere enclosed inside a bigger mirror sphere.
 * The camera orbits inside the mirror, so every ray eventually bounces
 * and you see the inner orb smeared across the curved reflections. */

#include "renderer.h"
#include "viewport.h"
#include "scene.h"
#include "camera.h"
#include "sphere.h"
#include <SDL2/SDL.h>

#define GL_GLEXT_PROTOTYPES 1
#include <GL/gl.h>
#include <GL/glext.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define WINDOW_W 960
#define WINDOW_H 600
#define FOV (M_PI / 2.8f)
#define RENDER_SCALE_MIN 1
#define RENDER_SCALE_MAX 4

/* ---- Scene knobs — tweak to taste -------------------------------------- */

/* Inner (textured) sphere. Pick any RT_TEX_* from material.h: NOISE,
 * MARBLE, WOOD, CELLS, CRACKS, SPOTS, CLOUDS all look interesting on
 * a ball. tex_scale controls feature size (world units per feature). */
#define INNER_RADIUS        1.4f
#define INNER_TEX_KIND      RT_TEX_NONE
#define INNER_TEX_SCALE     0.35f
#define INNER_ALBEDO_A      {40,  90, 180}
#define INNER_ALBEDO_B      {230, 220, 180}
#define INNER_REFLECTIVITY  1.0f   /* set > 0 to also make it glossy */

/* Outer (mirror) sphere. Almost-black albedo + reflectivity 1.0 gives
 * the cleanest hall-of-reflections look; raise the albedo to tint the
 * room, lower the reflectivity to wash it out. Set OUTER_TEX_KIND to
 * any RT_TEX_* (same options as the inner sphere) to pattern the
 * mirror — with full reflectivity the pattern is barely visible, so
 * drop OUTER_REFLECTIVITY (e.g. 0.6) to let the texture show through. */
#define OUTER_RADIUS        7.0f
#define OUTER_TEX_KIND      RT_TEX_CELLS
#define OUTER_TEX_SCALE     1.0f
#define OUTER_ALBEDO_A      {250,  8,  12}
#define OUTER_ALBEDO_B      {40, 40, 60}
#define OUTER_REFLECTIVITY  0.0f

/* Camera orbit inside the mirror sphere. */
#define ORBIT_RADIUS        4.2f
#define ORBIT_HEIGHT_AMP    1.1f
#define ORBIT_SPEED         0.35f

/* ------------------------------------------------------------------------ */

static void build_scene(rt_scene **scene_out, rt_camera **camera_out) {
    rt_scene *scene = rt_scene_create();

    int m_inner = rt_scene_add_material(scene, (rt_material){
        .albedo       = INNER_ALBEDO_A,
        .albedo2      = INNER_ALBEDO_B,
        .tex_kind     = INNER_TEX_KIND,
        .tex_scale    = INNER_TEX_SCALE,
        .reflectivity = INNER_REFLECTIVITY,
    });
    int m_outer = rt_scene_add_material(scene, (rt_material){
        .albedo       = OUTER_ALBEDO_A,
        .albedo2      = OUTER_ALBEDO_B,
        .tex_kind     = OUTER_TEX_KIND,
        .tex_scale    = OUTER_TEX_SCALE,
        .reflectivity = OUTER_REFLECTIVITY,
    });

    rt_scene_add_sphere(scene, (rt_sphere){
        .center   = {0.0f, 0.0f, 0.0f},
        .radius   = INNER_RADIUS,
        .material = m_inner,
    });
    rt_scene_add_sphere(scene, (rt_sphere){
        .center   = {0.0f, 0.0f, 0.0f},
        .radius   = OUTER_RADIUS,
        .material = m_outer,
    });

    rt_scene_set_ambient(scene, 0.25f);
    rt_scene_add_light(scene, (rt_light){
        .direction = {0.4f, 0.9f, 0.3f},
        .intensity = 0.8f,
    });
    rt_scene_add_light(scene, (rt_light){
        .direction = {-0.6f, 0.3f, -0.5f},
        .intensity = 0.4f,
    });

    *scene_out = scene;
    *camera_out = rt_camera_create(
        (vector){ORBIT_RADIUS, 0.5f, 0.0f},
        (vector){-1.0f, 0.0f, 0.0f}
    );
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

int main(int argc, char *argv[]) {
    rt_backend preferred = RT_BACKEND_CPU;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-G") == 0 || strcmp(argv[i], "--gpu") == 0) {
            preferred = RT_BACKEND_OPENGL;
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            printf("Usage: %s [options]\n", argv[0]);
            printf("  -G, --gpu    Start with OpenGL raytrace backend (falls back to CPU)\n");
            printf("  -h, --help   Show this help\n");
            printf("\nTAB toggles backend, SPACE toggles auto-orbit, WASD/arrows fly.\n");
            return 0;
        }
    }

    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        fprintf(stderr, "SDL init failed: %s\n", SDL_GetError());
        return 1;
    }

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

    SDL_Window *window = SDL_CreateWindow("Orb",
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
    rt_renderer *active = (preferred == RT_BACKEND_OPENGL && gpu_rnd) ? gpu_rnd
                        : (cpu_rnd ? cpu_rnd : gpu_rnd);
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

    vector cam_pos = {ORBIT_RADIUS, 0.5f, 0.0f};
    float cam_yaw = -(float)M_PI_2;
    float cam_pitch = 0.0f;
    float move_speed = 3.5f;
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
            float a = t * ORBIT_SPEED;
            cam_pos.x = cosf(a) * ORBIT_RADIUS;
            cam_pos.z = sinf(a) * ORBIT_RADIUS;
            cam_pos.y = sinf(t * 0.55f) * ORBIT_HEIGHT_AMP;
            vector look_at = {0.0f, 0.0f, 0.0f};
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
            if (keys[SDL_SCANCODE_LSHIFT] || keys[SDL_SCANCODE_RSHIFT]) cam_pos.y -= move_speed * dt;

            /* Keep the camera between the two spheres so we always see
             * the reflective room from the inside. */
            float d2 = cam_pos.x * cam_pos.x + cam_pos.y * cam_pos.y + cam_pos.z * cam_pos.z;
            float d = sqrtf(d2);
            float inner_limit = INNER_RADIUS + 0.2f;
            float outer_limit = OUTER_RADIUS - 0.3f;
            if (d < inner_limit && d > 1e-4f) {
                float s = inner_limit / d;
                cam_pos.x *= s; cam_pos.y *= s; cam_pos.z *= s;
            } else if (d > outer_limit) {
                float s = outer_limit / d;
                cam_pos.x *= s; cam_pos.y *= s; cam_pos.z *= s;
            }
        }

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
                     "Orb - %s %d FPS (%.2f ms, %dx%d, 1/%d) %s",
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
