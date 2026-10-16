/* Bestiary: a gallery of eldritch entities, each built from a handful of
 * raytracer primitives and displayed on a stone plinth under a sickly
 * sky. The camera orbits whichever creature is summoned; eyes follow it.
 *
 * Each creature is a pure builder (see creatures.c) — the scene is
 * cleared and rebuilt every frame from (time, camera position). */

#include "renderer.h"
#include "viewport.h"
#include "scene.h"
#include "postfx.h"
#include "kit.h"
#include "creatures.h"
#include <SDL2/SDL.h>

#define GL_GLEXT_PROTOTYPES 1
#include "gl_compat.h"
#include "sdl_display.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define INIT_WINDOW_W 960
#define INIT_WINDOW_H 600
#define FOV (M_PI / 3.0f)
#define RENDER_SCALE_MIN 1
#define RENDER_SCALE_MAX 4
#define RENDER_SCALE_DEFAULT 2

#define ORBIT_SPEED   0.22f
#define PITCH_MIN    -0.15f
#define PITCH_MAX     1.2f
#define DIST_MIN      3.0f
#define DIST_MAX     16.0f

/* ---- Stage -------------------------------------------------------------- */

static void build_stage(scene *s, const bst_creature *cr, float t) {
    /* Sky dome: a sickly green horizon fading to violet-black overhead. */
    scene_material sky = kit_tex((scene_color){ 38, 52, 44},
                                 (scene_color){  6,  4, 12},
                                 SCENE_TEX_GRADIENT, 55.0f);
    sky.unlit = 1;
    kit_sphere(s, (vector){0, 0, 0}, 120.0f, kit_mat(s, sky));

    /* Wet black flagstones. */
    scene_material ground = kit_tex((scene_color){ 30, 32, 34},
                                    (scene_color){ 10, 10, 12},
                                    SCENE_TEX_CRACKS, 1.2f);
    ground.reflectivity = 0.2f;
    scene_add_plane(s, (scene_plane){ .normal = {0, 1, 0},
                                      .point  = {0, 0, 0},
                                      .material = kit_mat(s, ground) });

    /* The plinth, with a rune-ring inlay glowing in the creature's hue. */
    int stone = kit_mat(s, kit_tex((scene_color){ 72, 70, 64},
                                   (scene_color){ 34, 32, 30},
                                   SCENE_TEX_CRACKS, 0.4f));
    scene_add_cylinder(s, (scene_cylinder){
        .center = {0, BST_PLINTH_TOP * 0.5f, 0}, .axis = {0, 1, 0},
        .radius = 2.3f, .half_height = BST_PLINTH_TOP * 0.5f,
        .material = stone });
    scene_add_disc(s, (scene_disc){ .center = {0, BST_PLINTH_TOP, 0},
                                    .normal = {0, 1, 0}, .radius = 2.3f,
                                    .material = stone });
    float pulse = 0.65f + 0.35f * sinf(t * 1.5f);
    scene_color ac = cr->accent;
    int ring = kit_mat(s, kit_glow((scene_color){
        (uint8_t)(ac.r * pulse), (uint8_t)(ac.g * pulse),
        (uint8_t)(ac.b * pulse) }));
    scene_add_torus(s, (scene_torus){ .center = {0, BST_PLINTH_TOP, 0},
                                      .axis = {0, 1, 0},
                                      .major_radius = 2.05f,
                                      .minor_radius = 0.035f,
                                      .material = ring });

    scene_set_ambient(s, 0.22f);
    scene_add_light(s, (scene_light){ .direction = {0.4f, 0.9f, 0.5f},
                                      .intensity = 0.8f });
    scene_add_light(s, (scene_light){ .direction = {-0.6f, 0.3f, -0.5f},
                                      .intensity = 0.35f });
}

/* ---- Display ------------------------------------------------------------ */

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

static void announce(int idx) {
    const bst_creature *cr = &bst_creatures[idx];
    fprintf(stderr, "\n[%d/%d] %s — %s\n  %s\n", idx + 1, bst_creature_count,
            cr->name, cr->epithet, cr->lore);
    if (strcmp(cr->name, "Azathoth") == 0)
        fprintf(stderr, "  (Gravitational lensing is CPU-heavy: L toggles it, "
                        "- lowers the resolution.)\n");
}

int main(int argc, char *argv[]) {
    rt_backend preferred = RT_BACKEND_CPU;
    int start = 0;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-G") == 0 || strcmp(argv[i], "--gpu") == 0) {
            preferred = RT_BACKEND_OPENGL;
        } else if ((strcmp(argv[i], "-c") == 0 ||
                    strcmp(argv[i], "--creature") == 0) && i + 1 < argc) {
            start = atoi(argv[++i]) - 1;
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            printf("Usage: %s [options]\n", argv[0]);
            printf("  -G, --gpu          Start with OpenGL raytrace backend (falls back to CPU)\n");
            printf("  -c, --creature N   Start on creature N (1..%d)\n", bst_creature_count);
            printf("  -h, --help         Show this help\n");
            printf("\nN/P next/previous creature, 1..9/0 jump to 1..10, SPACE auto-orbit,\n"
                   "arrows orbit, W/S zoom, F postfx, L lensing, -/= render scale,\n"
                   "TAB backend, F11 fullscreen, ESC quit.\n");
            printf("\nThe bestiary:\n");
            for (int k = 0; k < bst_creature_count; k++)
                printf("  %d. %s — %s\n", k + 1, bst_creatures[k].name,
                       bst_creatures[k].epithet);
            return 0;
        }
    }
    if (start < 0 || start >= bst_creature_count) start = 0;

    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        fprintf(stderr, "SDL init failed: %s\n", SDL_GetError());
        return 1;
    }

    sdl_display_request_context();
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

    int window_w = INIT_WINDOW_W;
    int window_h = INIT_WINDOW_H;
    int fullscreen = 0;
    SDL_Window *window = SDL_CreateWindow("Bestiary",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        window_w, window_h, SDL_WINDOW_OPENGL);
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
    gl_compat_init((gl_compat_loader_fn)SDL_GL_GetProcAddress);

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
    fprintf(stderr, "Active: %s%s\n", rt_renderer_name(active),
            (cpu_rnd && gpu_rnd) ? " (TAB to toggle)" : " (only backend built)");

    scene *scene = scene_create();
    scene_camera *camera = scene_camera_create((vector){0, 2, 7},
                                               (vector){0, 0, -1});

    int render_scale = RENDER_SCALE_DEFAULT;
    int render_w = window_w / render_scale;
    int render_h = window_h / render_scale;
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

    postfx_bloom_ctx *bloom_ctx = postfx_bloom_create(render_w, render_h);
    postfx_bloom    bloom    = { 1, 0.62f, 0.2f, 0.9f, 6, 2 };
    postfx_vignette vignette = { 1, 0.75f, 0.35f };
    postfx_grain    grain    = { 1, 0.06f, 0 };
    int postfx_on = 1;
    int lensing   = 1;

    int   current   = start;
    float cam_yaw   = 0.0f;
    float cam_pitch = 0.18f;
    float cam_dist  = bst_creatures[current].view_dist;
    int   auto_orbit = 1;
    int   running    = 1;
    announce(current);

    Uint32 fps_last = SDL_GetTicks();
    Uint32 frame_last = SDL_GetTicks();
    Uint32 summon_ticks = SDL_GetTicks();
    int fps_frames = 0;
    Uint32 render_ms_accum = 0;
    char title_buf[200];

    while (running) {
        Uint32 frame_now = SDL_GetTicks();
        float dt = (frame_now - frame_last) / 1000.0f;
        frame_last = frame_now;

        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) running = 0;
            if (e.type != SDL_KEYDOWN) continue;
            SDL_Keycode k = e.key.keysym.sym;
            int next = -1;
            if (k == SDLK_ESCAPE) running = 0;
            if (k == SDLK_n) next = (current + 1) % bst_creature_count;
            if (k == SDLK_p) next = (current + bst_creature_count - 1)
                                  % bst_creature_count;
            if (k >= SDLK_1 && k <= SDLK_9 && k - SDLK_1 < bst_creature_count)
                next = k - SDLK_1;
            if (k == SDLK_0 && bst_creature_count >= 10) next = 9;
            if (next >= 0 && next != current) {
                current = next;
                cam_dist = bst_creatures[current].view_dist;
                summon_ticks = frame_now;
                announce(current);
            }
            if (k == SDLK_SPACE) auto_orbit = !auto_orbit;
            if (k == SDLK_l) {
                lensing = !lensing;
                fprintf(stderr, "Lensing: %s\n", lensing ? "on" : "off");
            }
            if (k == SDLK_f) {
                postfx_on = !postfx_on;
                fprintf(stderr, "Postfx: %s\n", postfx_on ? "on" : "off");
            }
            if (k == SDLK_TAB) {
                if (!cpu_rnd || !gpu_rnd) {
                    sdl_display_note_single_backend(rt_renderer_name(active));
                } else {
                    active = (active == cpu_rnd) ? gpu_rnd : cpu_rnd;
                    fprintf(stderr, "Active: %s\n", rt_renderer_name(active));
                }
            }
            int resize = 0;
            if (k == SDLK_F11) {
                fullscreen = !fullscreen;
                SDL_SetWindowFullscreen(window,
                    fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
                SDL_GetWindowSize(window, &window_w, &window_h);
                resize = 1;
            }
            if ((k == SDLK_MINUS || k == SDLK_KP_MINUS) &&
                render_scale < RENDER_SCALE_MAX) {
                render_scale++;
                resize = 1;
            }
            if ((k == SDLK_EQUALS || k == SDLK_KP_PLUS) &&
                render_scale > RENDER_SCALE_MIN) {
                render_scale--;
                resize = 1;
            }
            if (resize) {
                render_w = window_w / render_scale;
                render_h = window_h / render_scale;
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
        if (keys[SDL_SCANCODE_LEFT])  { cam_yaw   -= 1.4f * dt; auto_orbit = 0; }
        if (keys[SDL_SCANCODE_RIGHT]) { cam_yaw   += 1.4f * dt; auto_orbit = 0; }
        if (keys[SDL_SCANCODE_UP])    cam_pitch += 0.9f * dt;
        if (keys[SDL_SCANCODE_DOWN])  cam_pitch -= 0.9f * dt;
        if (keys[SDL_SCANCODE_W])     cam_dist  -= 4.0f * dt;
        if (keys[SDL_SCANCODE_S])     cam_dist  += 4.0f * dt;
        if (auto_orbit) cam_yaw += ORBIT_SPEED * dt;
        if (cam_pitch < PITCH_MIN) cam_pitch = PITCH_MIN;
        if (cam_pitch > PITCH_MAX) cam_pitch = PITCH_MAX;
        if (cam_dist  < DIST_MIN)  cam_dist  = DIST_MIN;
        if (cam_dist  > DIST_MAX)  cam_dist  = DIST_MAX;

        const bst_creature *cr = &bst_creatures[current];
        vector focus = { 0.0f, cr->focus_y, 0.0f };
        vector cam_pos = {
            focus.x + cam_dist * cosf(cam_pitch) * sinf(cam_yaw),
            focus.y + cam_dist * sinf(cam_pitch),
            focus.z + cam_dist * cosf(cam_pitch) * cosf(cam_yaw),
        };
        if (cam_pos.y < 0.3f) cam_pos.y = 0.3f;
        scene_camera_place(camera, cam_pos,
                           vector_normalize(vector_sub(focus, cam_pos)));

        float t = (frame_now - summon_ticks) / 1000.0f;
        bst_ctx ctx = { .t = t, .cam = cam_pos, .lensing = lensing };
        scene_clear(scene);
        kit_frame_begin();
        build_stage(scene, cr, t);
        cr->build(scene, &ctx);

        Uint32 r_start = SDL_GetTicks();
        rt_renderer_render(active, scene, camera, &viewport, pixels, NULL);
        if (postfx_on) {
            postfx_bloom_apply(bloom_ctx, pixels, render_w, render_h, &bloom);
            postfx_vignette_apply(pixels, render_w, render_h, &vignette);
            grain.seed = frame_now;
            postfx_grain_apply(pixels, render_w, render_h, &grain);
        }
        render_ms_accum += SDL_GetTicks() - r_start;

        display_pixels(display_tex, display_fbo, pixels,
                       render_w, render_h, window_w, window_h);
        SDL_GL_SwapWindow(window);

        fps_frames++;
        Uint32 now = SDL_GetTicks();
        if (now - fps_last >= 1000) {
            float avg_ms = (fps_frames > 0)
                ? (float)render_ms_accum / (float)fps_frames : 0.0f;
            snprintf(title_buf, sizeof(title_buf),
                     "Bestiary [%d/%d] %s — %s | %s %d FPS (%.1f ms, %dx%d)",
                     current + 1, bst_creature_count, cr->name, cr->epithet,
                     rt_renderer_name(active), fps_frames, avg_ms,
                     render_w, render_h);
            SDL_SetWindowTitle(window, title_buf);
            fps_frames = 0;
            render_ms_accum = 0;
            fps_last = now;
        }
    }

    postfx_bloom_destroy(bloom_ctx);
    glDeleteFramebuffers(1, &display_fbo);
    glDeleteTextures(1, &display_tex);
    if (cpu_rnd) rt_renderer_destroy(cpu_rnd);
    if (gpu_rnd) rt_renderer_destroy(gpu_rnd);
    free(pixels);
    scene_camera_destroy(camera);
    scene_destroy(scene);
    SDL_GL_DeleteContext(gl_ctx);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
