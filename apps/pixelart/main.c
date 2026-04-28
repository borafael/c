/* Pixel-art raytrace demo.
 *
 * Renders the scene at a fixed low internal resolution and lets the
 * GL_NEAREST blit do the upscale, so every traced pixel becomes one
 * chunky on-screen pixel. Optional palette quantization snaps the
 * output to a small fixed palette for an even more deliberate look.
 *
 * Controls:
 *   ESC       quit
 *   TAB       toggle CPU / OpenGL backend (if both available)
 *   1..4      resolution preset: 160x90 / 240x135 / 320x180 / 480x270
 *   P         toggle palette quantization
 *   F11       fullscreen
 *   WASD/space/shift  fly camera; arrows look around
 */

#include "renderer.h"
#include "viewport.h"
#include "scene.h"
#include "sphere.h"
#include "plane.h"
#include "box.h"
#include "cylinder.h"
#include <SDL2/SDL.h>

#define GL_GLEXT_PROTOTYPES 1
#include "gl_compat.h"

#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#define INIT_WINDOW_W 960
#define INIT_WINDOW_H 540
#define FOV (M_PI / 3.0f)

/* Resolution presets. Order matters: keys 1..N pick presets[i]. */
typedef struct { int w, h; const char *name; } pixel_preset;
static const pixel_preset PRESETS[] = {
    { 160,  90, "160x90"  },
    { 240, 135, "240x135" },
    { 320, 180, "320x180" },
    { 480, 270, "480x270" },
};
#define PRESET_COUNT ((int)(sizeof(PRESETS) / sizeof(PRESETS[0])))
#define PRESET_DEFAULT 1  /* 240x135 */

/* 16-colour palette. Loosely EGA-flavoured plus a couple of warm tones
 * to flatter the test scene. The quantizer just picks the nearest in
 * sRGB-space, which is crude but fine for prototype. */
static const uint8_t PALETTE[][3] = {
    {  0,   0,   0}, { 30,  30,  30}, { 90,  90,  90}, {180, 180, 180},
    {255, 255, 255}, {120,  20,  20}, {220,  60,  60}, {255, 160, 100},
    { 60, 110,  40}, {120, 200,  80}, { 40,  80, 160}, {110, 170, 230},
    {200, 200,  60}, {220, 130,  40}, {120,  60, 140}, { 60,  35,  20},
};
#define PALETTE_COUNT ((int)(sizeof(PALETTE) / sizeof(PALETTE[0])))

static inline uint32_t palette_quantize(uint32_t argb) {
    int r = (argb >> 16) & 0xFF;
    int g = (argb >>  8) & 0xFF;
    int b =  argb        & 0xFF;
    int best = 0;
    int best_d = 1 << 30;
    for (int i = 0; i < PALETTE_COUNT; i++) {
        int dr = r - PALETTE[i][0];
        int dg = g - PALETTE[i][1];
        int db = b - PALETTE[i][2];
        int d  = dr*dr + dg*dg + db*db;
        if (d < best_d) { best_d = d; best = i; }
    }
    return 0xFF000000u
         | ((uint32_t)PALETTE[best][0] << 16)
         | ((uint32_t)PALETTE[best][1] <<  8)
         |  (uint32_t)PALETTE[best][2];
}

static void quantize_buffer(uint32_t *pixels, int count) {
    for (int i = 0; i < count; i++)
        pixels[i] = palette_quantize(pixels[i]);
}

static void build_scene(scene **scn, scene_camera **cam) {
    *scn = scene_create();

    /* A handful of materials chosen to read clearly at low res. The
     * pixel-art look benefits from saturated, chunky blocks of colour;
     * heavy procedural detail just turns to noise once you blit. */
    int m_red = scene_add_material(*scn, (scene_material){
        .albedo  = {220,  60,  60},
        .albedo2 = {120,  20,  20},
        .tex_kind = SCENE_TEX_CHECKER,
        .tex_scale = 0.6f,
    });
    int m_green = scene_add_material(*scn, (scene_material){
        .albedo  = {120, 200,  80},
    });
    int m_blue = scene_add_material(*scn, (scene_material){
        .albedo  = { 60, 110, 200},
    });
    int m_yellow = scene_add_material(*scn, (scene_material){
        .albedo  = {230, 200,  60},
    });
    int m_floor = scene_add_material(*scn, (scene_material){
        .albedo  = {180, 180, 180},
        .albedo2 = { 60,  60,  60},
        .tex_kind = SCENE_TEX_CHECKER,
        .tex_scale = 1.0f,
    });
    int m_mirror = scene_add_material(*scn, (scene_material){
        .reflectivity = 0.7f,
        .albedo  = {200, 200, 220},
    });

    scene_add_sphere(*scn, (scene_sphere){
        .center = {0.0f, 1.0f, 0.0f}, .radius = 1.0f, .material = m_red });
    scene_add_sphere(*scn, (scene_sphere){
        .center = {-2.4f, 0.6f, -0.5f}, .radius = 0.6f, .material = m_green });
    scene_add_sphere(*scn, (scene_sphere){
        .center = {2.0f, 0.8f, -1.0f}, .radius = 0.8f, .material = m_blue });
    scene_add_sphere(*scn, (scene_sphere){
        .center = {0.0f, 2.4f, -1.5f}, .radius = 0.9f, .material = m_mirror });

    scene_add_box(*scn, (scene_box){
        .min = {-3.5f, -0.5f, 1.5f}, .max = {-2.5f, 0.8f, 2.5f},
        .material = m_yellow });

    scene_add_cylinder(*scn, (scene_cylinder){
        .center = {2.5f, 0.5f, 2.0f}, .axis = {0.0f, 1.0f, 0.0f},
        .radius = 0.45f, .half_height = 1.0f, .material = m_yellow });

    scene_add_plane(*scn, (scene_plane){
        .point = {0.0f, -0.5f, 0.0f}, .normal = {0.0f, 1.0f, 0.0f},
        .material = m_floor });

    scene_set_ambient(*scn, 0.2f);
    scene_add_light(*scn, (scene_light){
        .direction = {1.0f, 1.2f, -0.8f}, .intensity = 0.9f });

    *cam = scene_camera_create(
        (vector){4.5f, 2.5f, 5.0f},
        (vector){-1.0f, -0.4f, -1.0f});
}

static vector cam_dir_from_yaw_pitch(float yaw, float pitch) {
    return (vector){
        cosf(pitch) * sinf(yaw),
        sinf(pitch),
        cosf(pitch) * cosf(yaw),
    };
}

/* Mirror of rtdemo's display_pixels: nearest-neighbour blit, Y-flipped
 * so y=0 of the trace appears at the top of the window. */
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
    (void)argc; (void)argv;
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        fprintf(stderr, "SDL init failed: %s\n", SDL_GetError());
        return 1;
    }

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

    int window_w = INIT_WINDOW_W;
    int window_h = INIT_WINDOW_H;
    int fullscreen = 0;
    SDL_Window *window = SDL_CreateWindow("Pixel-Art Raytrace",
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
    SDL_GL_SetSwapInterval(0);
    gl_compat_init((gl_compat_loader_fn)SDL_GL_GetProcAddress);

    fprintf(stderr, "GL version: %s\n", (const char *)glGetString(GL_VERSION));

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
    fprintf(stderr, "Active: %s (TAB to toggle)\n", rt_renderer_name(active));

    scene *scn;
    scene_camera *cam;
    build_scene(&scn, &cam);

    int preset = PRESET_DEFAULT;
    int render_w = PRESETS[preset].w;
    int render_h = PRESETS[preset].h;
    int palette_on = 1;
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

    vector cam_pos = {4.5f, 2.5f, 5.0f};
    float cam_yaw = -2.356f;
    float cam_pitch = -0.3f;
    float move_speed = 5.0f;
    float look_speed = 2.0f;
    int running = 1;

    Uint32 fps_last = SDL_GetTicks();
    Uint32 frame_last = SDL_GetTicks();
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
            if (e.type == SDL_KEYDOWN) {
                SDL_Keycode k = e.key.keysym.sym;
                if (k == SDLK_ESCAPE) running = 0;
                if (k == SDLK_TAB) {
                    if (active == cpu_rnd && gpu_rnd) active = gpu_rnd;
                    else if (active == gpu_rnd && cpu_rnd) active = cpu_rnd;
                    fprintf(stderr, "Active: %s\n", rt_renderer_name(active));
                }
                if (k == SDLK_p) {
                    palette_on = !palette_on;
                    fprintf(stderr, "Palette: %s\n", palette_on ? "on" : "off");
                }
                if (k >= SDLK_1 && k <= SDLK_4) {
                    int idx = k - SDLK_1;
                    if (idx < PRESET_COUNT) {
                        preset = idx;
                        goto recreate_buffers;
                    }
                }
                if (k == SDLK_F11) {
                    fullscreen = !fullscreen;
                    SDL_SetWindowFullscreen(window,
                        fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
                    SDL_GetWindowSize(window, &window_w, &window_h);
                }
                continue;
                recreate_buffers:
                    render_w = PRESETS[preset].w;
                    render_h = PRESETS[preset].h;
                    free(pixels);
                    pixels = calloc((size_t)(render_w * render_h), sizeof(uint32_t));
                    viewport = (rt_viewport){ render_w, render_h, FOV };
                    glBindTexture(GL_TEXTURE_2D, display_tex);
                    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, render_w, render_h, 0,
                                 GL_BGRA, GL_UNSIGNED_BYTE, NULL);
                    fprintf(stderr, "Preset: %s (%dx%d)\n",
                            PRESETS[preset].name, render_w, render_h);
            }
        }

        const Uint8 *keys = SDL_GetKeyboardState(NULL);
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
        if (keys[SDL_SCANCODE_SPACE])  cam_pos.y += move_speed * dt;
        if (keys[SDL_SCANCODE_LSHIFT]) cam_pos.y -= move_speed * dt;

        scene_camera_place(cam, cam_pos, cam_dir_from_yaw_pitch(cam_yaw, cam_pitch));

        Uint32 r_start = SDL_GetTicks();
        rt_renderer_render(active, scn, cam, &viewport, pixels);
        if (palette_on) quantize_buffer(pixels, render_w * render_h);
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
                     "Pixel-Art Raytrace - %s %s palette=%s %d FPS (%.2f ms)",
                     rt_renderer_name(active), PRESETS[preset].name,
                     palette_on ? "on" : "off", fps_frames, avg_ms);
            SDL_SetWindowTitle(window, title_buf);
            fprintf(stderr, "[%s %s pal=%d] %d FPS, %.2f ms\n",
                    rt_renderer_name(active), PRESETS[preset].name,
                    palette_on, fps_frames, avg_ms);
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
    scene_camera_destroy(cam);
    scene_destroy(scn);
    SDL_GL_DeleteContext(gl_ctx);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
