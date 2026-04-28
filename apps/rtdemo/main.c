#include "renderer.h"
#include "viewport.h"
#include "scene.h"
#include "sphere.h"
#include "plane.h"
#include "box.h"
#include "disc.h"
#include "cylinder.h"
#include "triangle.h"
#include "sprite.h"
#include "heightfield.h"
#include <SDL2/SDL.h>

#define GL_GLEXT_PROTOTYPES 1
#include "gl_compat.h"

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <unistd.h>

#define INIT_WINDOW_W 800
#define INIT_WINDOW_H 600
#define FOV (M_PI / 3.0f)
#define RENDER_SCALE_MIN 1
#define RENDER_SCALE_MAX 4


#define S 16  /* sprite frame size */

#define PX(r,g,b) (0xFF000000u | ((r)<<16) | ((g)<<8) | (b))
#define TP 0x00000000u  /* transparent */

static uint32_t frame_data[8][S * S];

static void set(uint32_t *buf, int x, int y, uint32_t c) {
    if (x >= 0 && x < S && y >= 0 && y < S)
        buf[y * S + x] = c;
}

static void fill_circle(uint32_t *buf, int cx, int cy, int r, uint32_t c) {
    for (int y = cy - r; y <= cy + r; y++)
        for (int x = cx - r; x <= cx + r; x++)
            if ((x-cx)*(x-cx) + (y-cy)*(y-cy) <= r*r)
                set(buf, x, y, c);
}

static uint32_t skin  = 0;
static uint32_t hair  = 0;
static uint32_t eye_w = 0;
static uint32_t eye_p = 0;
static uint32_t mouth = 0;

static void clear_frame(uint32_t *buf) {
    for (int i = 0; i < S * S; i++) buf[i] = TP;
}

static void draw_head(uint32_t *buf) {
    fill_circle(buf, 7, 7, 6, skin);
    /* Hair on top */
    for (int x = 2; x <= 12; x++)
        for (int y = 1; y <= 3; y++)
            if ((x-7)*(x-7) + (y-7)*(y-7) <= 36)
                set(buf, x, y, hair);
}

/* Frame 0: Front face — two eyes, smile */
static void draw_front(uint32_t *buf) {
    clear_frame(buf);
    draw_head(buf);
    /* Eyes */
    fill_circle(buf, 5, 6, 1, eye_w);
    set(buf, 5, 6, eye_p);
    fill_circle(buf, 9, 6, 1, eye_w);
    set(buf, 9, 6, eye_p);
    /* Smile */
    set(buf, 5, 10, mouth);
    set(buf, 6, 11, mouth);
    set(buf, 7, 11, mouth);
    set(buf, 8, 11, mouth);
    set(buf, 9, 10, mouth);
}

/* Frame 1: Front-right — two eyes shifted right, 3/4 smile */
static void draw_front_right(uint32_t *buf) {
    clear_frame(buf);
    draw_head(buf);
    fill_circle(buf, 6, 6, 1, eye_w);
    set(buf, 7, 6, eye_p);
    fill_circle(buf, 10, 6, 1, eye_w);
    set(buf, 11, 6, eye_p);
    set(buf, 7, 10, mouth);
    set(buf, 8, 11, mouth);
    set(buf, 9, 11, mouth);
    set(buf, 10, 10, mouth);
}

/* Frame 2: Right profile — one eye, nose bump */
static void draw_right(uint32_t *buf) {
    clear_frame(buf);
    draw_head(buf);
    fill_circle(buf, 9, 6, 1, eye_w);
    set(buf, 10, 6, eye_p);
    /* Nose */
    set(buf, 12, 7, skin);
    set(buf, 13, 8, skin);
    /* Mouth */
    set(buf, 9, 10, mouth);
    set(buf, 10, 11, mouth);
    set(buf, 11, 10, mouth);
}

/* Frame 3: Back-right — no features, ear hint */
static void draw_back_right(uint32_t *buf) {
    clear_frame(buf);
    draw_head(buf);
    /* Ear on left side (viewer's left = character's right from behind) */
    set(buf, 2, 7, skin);
    set(buf, 1, 7, skin);
    set(buf, 1, 8, skin);
}

/* Frame 4: Back — no face, just hair and head */
static void draw_back(uint32_t *buf) {
    clear_frame(buf);
    draw_head(buf);
    /* More hair coverage on back */
    for (int x = 3; x <= 11; x++)
        for (int y = 2; y <= 6; y++)
            if ((x-7)*(x-7) + (y-7)*(y-7) <= 36)
                set(buf, x, y, hair);
}

/* Frame 5: Back-left — mirror of back-right */
static void draw_back_left(uint32_t *buf) {
    clear_frame(buf);
    draw_head(buf);
    set(buf, 12, 7, skin);
    set(buf, 13, 7, skin);
    set(buf, 13, 8, skin);
}

/* Frame 6: Left profile — mirror of right */
static void draw_left(uint32_t *buf) {
    clear_frame(buf);
    draw_head(buf);
    fill_circle(buf, 5, 6, 1, eye_w);
    set(buf, 4, 6, eye_p);
    set(buf, 2, 7, skin);
    set(buf, 1, 8, skin);
    set(buf, 3, 10, mouth);
    set(buf, 4, 11, mouth);
    set(buf, 5, 10, mouth);
}

/* Frame 7: Front-left — mirror of front-right */
static void draw_front_left(uint32_t *buf) {
    clear_frame(buf);
    draw_head(buf);
    fill_circle(buf, 4, 6, 1, eye_w);
    set(buf, 3, 6, eye_p);
    fill_circle(buf, 8, 6, 1, eye_w);
    set(buf, 7, 6, eye_p);
    set(buf, 4, 10, mouth);
    set(buf, 5, 11, mouth);
    set(buf, 6, 11, mouth);
    set(buf, 7, 10, mouth);
}

static void init_sprite_frames(void) {
    skin  = PX(255, 200, 150);
    hair  = PX(100,  60,  20);
    eye_w = PX(255, 255, 255);
    eye_p = PX( 30,  30,  30);
    mouth = PX(200,  60,  60);

    draw_front(frame_data[0]);
    draw_front_right(frame_data[1]);
    draw_right(frame_data[2]);
    draw_back_right(frame_data[3]);
    draw_back(frame_data[4]);
    draw_back_left(frame_data[5]);
    draw_left(frame_data[6]);
    draw_front_left(frame_data[7]);
}

#define RTDEMO_TEX_SIZE 64
static uint32_t rtdemo_test_texture_pixels[RTDEMO_TEX_SIZE * RTDEMO_TEX_SIZE];

static void build_test_texture(void) {
    for (int y = 0; y < RTDEMO_TEX_SIZE; y++) {
        for (int x = 0; x < RTDEMO_TEX_SIZE; x++) {
            uint8_t r, g, b;
            if ((x % 16 == 0) || (y % 16 == 0)) {
                r = g = b = 20;
            } else {
                r = (uint8_t)((x * 4) & 0xFF);
                g = (uint8_t)((y * 4) & 0xFF);
                b = (uint8_t)(((x + y) * 2) & 0xFF);
            }
            rtdemo_test_texture_pixels[y * RTDEMO_TEX_SIZE + x] =
                (0xFFu << 24) | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
        }
    }
}

static void build_scene(scene **scene, scene_camera **camera) {
    *scene = scene_create();

    build_test_texture();
    int t_grid = scene_add_texture(*scene, (scene_texture){
        .pixels = rtdemo_test_texture_pixels,
        .width  = RTDEMO_TEX_SIZE,
        .height = RTDEMO_TEX_SIZE,
    });

    int m_red    = scene_add_material(*scene, (scene_material){
        .albedo    = {200,  40,  40},  /* deep red stone */
        .albedo2   = {255, 200, 160},  /* pale highlight */
        .tex_kind  = SCENE_TEX_CELLS,
        .tex_scale = 0.4f,
    });
    int m_green  = scene_add_material(*scene, (scene_material){
        .albedo    = { 80, 180,  80},  /* moss surface */
        .albedo2   = { 10,  25,  10},  /* crack line */
        .tex_kind  = SCENE_TEX_CRACKS,
        .tex_scale = 0.3f,
    });
    int m_blue   = scene_add_material(*scene, (scene_material){
        .albedo    = { 40,  90, 180},  /* sky */
        .albedo2   = {245, 245, 250},  /* cloud */
        .tex_kind  = SCENE_TEX_CLOUDS,
        .tex_scale = 0.6f,
    });
    int m_yellow = scene_add_material(*scene, (scene_material){
        .tex_kind  = SCENE_TEX_IMAGE,
        .tex_index = t_grid,
        .tex_scale = 1.0f,
    });
    int m_grey   = scene_add_material(*scene, (scene_material){
        .albedo    = { 90,  90,  90},
        .albedo2   = {160, 160, 160},
        .tex_kind  = SCENE_TEX_CHECKER,
        .tex_scale = 1.0f,
    });
    int m_orange = scene_add_material(*scene, (scene_material){
        .albedo    = {255, 180,  60},  /* bottom */
        .albedo2   = {160,  20, 120},  /* top */
        .tex_kind  = SCENE_TEX_GRADIENT,
        .tex_scale = 1.2f,
    });
    int m_purple = scene_add_material(*scene, (scene_material){
        .albedo    = {200, 160, 100},  /* light wood */
        .albedo2   = { 70,  35,  15},  /* dark grain */
        .tex_kind  = SCENE_TEX_WOOD,
        .tex_scale = 0.25f,
    });
    int m_cyan   = scene_add_material(*scene, (scene_material){
        .albedo    = {230, 230, 235},  /* stone */
        .albedo2   = { 40,  60,  90},  /* vein */
        .tex_kind  = SCENE_TEX_MARBLE,
        .tex_scale = 0.5f,
    });
    int m_coral  = scene_add_material(*scene, (scene_material){
        .albedo    = {255, 120,  60},
        .albedo2   = {120,  30,  10},
        .tex_kind  = SCENE_TEX_NOISE,
        .tex_scale = 0.35f,
    });
    int m_mirror = scene_add_material(*scene, (scene_material){
        .reflectivity = 1.0f,
    });

    scene_add_sphere(*scene, (scene_sphere){
        .center = {0.0f, 1.0f, 0.0f},
        .radius = 1.0f,
        .material = m_red
    });
    scene_add_sphere(*scene, (scene_sphere){
        .center = {-2.5f, 0.6f, -1.0f},
        .radius = 0.6f,
        .material = m_green
    });
    scene_add_sphere(*scene, (scene_sphere){
        .center = {2.0f, 0.8f, -0.5f},
        .radius = 0.8f,
        .material = m_blue
    });
    scene_add_sphere(*scene, (scene_sphere){
        .center = {0.5f, 0.4f, 2.0f},
        .radius = 0.4f,
        .material = m_yellow
    });
    scene_add_sphere(*scene, (scene_sphere){
        .center = {0.0f, 2.5f, -1.0f},
        .radius = 1.0f,
        .material = m_mirror
    });

    scene_add_plane(*scene, (scene_plane){
        .point = {0.0f, -1.0f, 0.0f},
        .normal = {0.0f, 0.96f, 0.29f},
        .material = m_grey
    });

    scene_add_disc(*scene, (scene_disc){
        .center = {-3.0f, 0.0f, 2.0f},
        .normal = {0.0f, 1.0f, 0.0f},
        .radius = 1.2f,
        .material = m_orange
    });

    scene_add_cylinder(*scene, (scene_cylinder){
        .center = {3.0f, 0.5f, -2.0f},
        .axis = {0.0f, 1.0f, 0.0f},
        .radius = 0.5f,
        .half_height = 1.0f,
        .material = m_purple
    });

    scene_add_triangle(*scene, (scene_triangle){
        .v0 = {-1.0f, 0.0f, -3.0f},
        .v1 = { 1.0f, 0.0f, -3.0f},
        .v2 = { 0.0f, 2.0f, -3.0f},
        .material = m_cyan
    });

    scene_add_box(*scene, (scene_box){
        .min = {-4.0f, -0.5f, -1.5f},
        .max = {-3.0f,  0.5f, -0.5f},
        .material = m_coral
    });

    scene_set_ambient(*scene, 0.15f);
    scene_add_light(*scene, (scene_light){
        .direction = {1.0f, 1.0f, -1.0f},
        .intensity = 0.85f
    });

    static scene_frame sprite_frames[8];
    init_sprite_frames();
    for (int i = 0; i < 8; i++)
        sprite_frames[i] = (scene_frame){ frame_data[i], S, S };

    scene_add_sprite(*scene, (scene_sprite){
        .position = {0.0f, 1.0f, 3.0f},
        .direction = {0.0f, 0.0f, 1.0f},
        .width = 2.0f,
        .height = 2.0f,
        .frame_count = 8,
        .frames = sprite_frames
    });

    *camera = scene_camera_create(
        (vector){5.0f, 3.0f, 5.0f},
        (vector){-1.0f, -0.4f, -1.0f}
    );
}

static vector cam_dir_from_yaw_pitch(float yaw, float pitch) {
    return (vector){
        cosf(pitch) * sinf(yaw),
        sinf(pitch),
        cosf(pitch) * cosf(yaw)
    };
}

/* Upload a uint32 pixel buffer to a GL texture and blit it to the window.
 * Source rows run top-to-bottom (y=0 is top). Destination flips Y so
 * the image displays right-side-up. */
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
    SDL_Window *window = SDL_CreateWindow("Raytrace Demo",
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
    SDL_GL_SetSwapInterval(0);  /* no vsync — measure raw throughput */
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
    fprintf(stderr, "Active: %s (press TAB to toggle)\n",
            rt_renderer_name(active));

    scene *scene;
    scene_camera *camera;
    build_scene(&scene, &camera);

    int render_scale = 1;
    int render_w = window_w / render_scale;
    int render_h = window_h / render_scale;
    uint32_t *pixels = calloc((size_t)(render_w * render_h), sizeof(uint32_t));
    rt_viewport viewport = { render_w, render_h, FOV };

    /* Display texture + framebuffer for blitting */
    GLuint display_tex, display_fbo;
    glGenTextures(1, &display_tex);
    glBindTexture(GL_TEXTURE_2D, display_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, render_w, render_h, 0,
                 GL_BGRA, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glGenFramebuffers(1, &display_fbo);

    vector cam_pos = {5.0f, 3.0f, 5.0f};
    float cam_yaw = -2.356f;
    float cam_pitch = -0.3f;
    float move_speed = 5.0f;
    float look_speed = 2.0f;
    int running = 1;

    Uint32 fps_last = SDL_GetTicks();
    Uint32 frame_last = SDL_GetTicks();
    int fps_frames = 0;
    Uint32 render_ms_accum = 0;
    char title_buf[160];

    while (running) {
        Uint32 frame_now = SDL_GetTicks();
        float dt = (frame_now - frame_last) / 1000.0f;
        frame_last = frame_now;
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
                if (e.key.keysym.sym == SDLK_F11) {
                    fullscreen = !fullscreen;
                    SDL_SetWindowFullscreen(window,
                        fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
                    SDL_GetWindowSize(window, &window_w, &window_h);
                    goto recreate_buffers;
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

        if (keys[SDL_SCANCODE_LEFT])  cam_yaw   -= look_speed * dt;
        if (keys[SDL_SCANCODE_RIGHT]) cam_yaw   += look_speed * dt;
        if (keys[SDL_SCANCODE_UP])    cam_pitch += look_speed * dt;
        if (keys[SDL_SCANCODE_DOWN])  cam_pitch -= look_speed * dt;
        if (cam_pitch >  1.4f) cam_pitch =  1.4f;
        if (cam_pitch < -1.4f) cam_pitch = -1.4f;

        vector forward = { sinf(cam_yaw), 0.0f, cosf(cam_yaw) };
        vector right   = { cosf(cam_yaw), 0.0f, -sinf(cam_yaw) };
        if (keys[SDL_SCANCODE_W]) cam_pos = vector_add(cam_pos, vector_scale(forward, move_speed * dt));
        if (keys[SDL_SCANCODE_S]) cam_pos = vector_add(cam_pos, vector_scale(forward, -move_speed * dt));
        if (keys[SDL_SCANCODE_D]) cam_pos = vector_add(cam_pos, vector_scale(right, move_speed * dt));
        if (keys[SDL_SCANCODE_A]) cam_pos = vector_add(cam_pos, vector_scale(right, -move_speed * dt));
        if (keys[SDL_SCANCODE_SPACE])  cam_pos.y += move_speed * dt;
        if (keys[SDL_SCANCODE_LSHIFT]) cam_pos.y -= move_speed * dt;

        vector cam_dir = cam_dir_from_yaw_pitch(cam_yaw, cam_pitch);
        scene_camera_place(camera, cam_pos, cam_dir);

        Uint32 r_start = SDL_GetTicks();
        rt_renderer_render(active, scene, camera, &viewport, pixels, NULL);
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
                     "Raytrace Demo - %s %d FPS (%.2f ms/frame, %dx%d, 1/%d)",
                     rt_renderer_name(active), fps_frames, avg_ms,
                     render_w, render_h, render_scale);
            SDL_SetWindowTitle(window, title_buf);
            fprintf(stderr, "[%s] %d FPS, %.2f ms/frame\n",
                    rt_renderer_name(active), fps_frames, avg_ms);
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
    scene_camera_destroy(camera);
    scene_destroy(scene);
    SDL_GL_DeleteContext(gl_ctx);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
