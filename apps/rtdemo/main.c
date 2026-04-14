#include "renderer.h"
#include "viewport.h"
#include "scene.h"
#include "camera.h"
#include "sphere.h"
#include "plane.h"
#include "box.h"
#include "disc.h"
#include "cylinder.h"
#include "triangle.h"
#include "sprite.h"
#include "heightfield.h"
#include "rt_color.h"
#include <SDL2/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <unistd.h>

#define WINDOW_W 800
#define WINDOW_H 600
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

static void build_scene(rt_scene **scene, rt_camera **camera) {
    *scene = rt_scene_create();

    rt_scene_add_sphere(*scene, (rt_sphere){
        .center = {0.0f, 1.0f, 0.0f},
        .radius = 1.0f,
        .color = {255, 80, 80}
    });
    rt_scene_add_sphere(*scene, (rt_sphere){
        .center = {-2.5f, 0.6f, -1.0f},
        .radius = 0.6f,
        .color = {80, 255, 80}
    });
    rt_scene_add_sphere(*scene, (rt_sphere){
        .center = {2.0f, 0.8f, -0.5f},
        .radius = 0.8f,
        .color = {80, 80, 255}
    });
    rt_scene_add_sphere(*scene, (rt_sphere){
        .center = {0.5f, 0.4f, 2.0f},
        .radius = 0.4f,
        .color = {255, 255, 80}
    });

    rt_scene_add_plane(*scene, (rt_plane){
        .point = {0.0f, -1.0f, 0.0f},
        .normal = {0.0f, 0.96f, 0.29f},
        .color = {120, 120, 120}
    });

    rt_scene_add_disc(*scene, (rt_disc){
        .center = {-3.0f, 0.0f, 2.0f},
        .normal = {0.0f, 1.0f, 0.0f},
        .radius = 1.2f,
        .color = {255, 160, 0}
    });

    rt_scene_add_cylinder(*scene, (rt_cylinder){
        .center = {3.0f, 0.5f, -2.0f},
        .axis = {0.0f, 1.0f, 0.0f},
        .radius = 0.5f,
        .half_height = 1.0f,
        .color = {200, 50, 200}
    });

    rt_scene_add_triangle(*scene, (rt_triangle){
        .v0 = {-1.0f, 0.0f, -3.0f},
        .v1 = { 1.0f, 0.0f, -3.0f},
        .v2 = { 0.0f, 2.0f, -3.0f},
        .color = {0, 200, 200}
    });

    rt_scene_add_box(*scene, (rt_box){
        .min = {-4.0f, -0.5f, -1.5f},
        .max = {-3.0f,  0.5f, -0.5f},
        .color = {255, 120, 60}
    });

    rt_scene_set_ambient(*scene, 0.15f);
    rt_scene_add_light(*scene, (rt_light){
        .direction = {1.0f, 1.0f, -1.0f},
        .intensity = 0.85f
    });

    static rt_frame sprite_frames[8];
    init_sprite_frames();
    for (int i = 0; i < 8; i++)
        sprite_frames[i] = (rt_frame){ frame_data[i], S, S };

    rt_scene_add_sprite(*scene, (rt_sprite){
        .position = {0.0f, 1.0f, 3.0f},
        .direction = {0.0f, 0.0f, 1.0f},
        .width = 2.0f,
        .height = 2.0f,
        .frame_count = 8,
        .frames = sprite_frames
    });

    *camera = rt_camera_create(
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

int main(void) {
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        fprintf(stderr, "SDL init failed: %s\n", SDL_GetError());
        return 1;
    }

    SDL_Window *window = SDL_CreateWindow("Raytrace Demo",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        WINDOW_W, WINDOW_H, 0);
    if (!window) {
        fprintf(stderr, "Window creation failed: %s\n", SDL_GetError());
        return 1;
    }

    SDL_Renderer *renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);

    rt_renderer *rnd = rt_renderer_create();
    if (!rnd) {
        fprintf(stderr, "Failed to create renderer\n");
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    fprintf(stderr, "Using renderer: %s\n", rt_renderer_name(rnd));

    rt_scene *scene;
    rt_camera *camera;
    build_scene(&scene, &camera);

    int render_scale = 1;
    int render_w = WINDOW_W / render_scale;
    int render_h = WINDOW_H / render_scale;
    uint32_t *pixels = calloc(render_w * render_h, sizeof(uint32_t));
    rt_viewport viewport = { render_w, render_h, FOV };
    SDL_Texture *texture = SDL_CreateTexture(renderer,
        SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING,
        render_w, render_h);

    vector cam_pos = {5.0f, 3.0f, 5.0f};
    float cam_yaw = -2.356f;   /* ~-135 deg, facing toward origin */
    float cam_pitch = -0.3f;
    float move_speed = 5.0f;
    float look_speed = 2.0f;
    int running = 1;

    Uint32 fps_last = SDL_GetTicks();
    Uint32 frame_last = SDL_GetTicks();
    int fps_frames = 0;
    char title_buf[128];

    while (running) {
        Uint32 frame_now = SDL_GetTicks();
        float dt = (frame_now - frame_last) / 1000.0f;
        frame_last = frame_now;
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) running = 0;
            if (e.type == SDL_KEYDOWN) {
                if (e.key.keysym.sym == SDLK_ESCAPE) running = 0;
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
                    pixels = calloc(render_w * render_h, sizeof(uint32_t));
                    viewport = (rt_viewport){ render_w, render_h, FOV };
                    SDL_DestroyTexture(texture);
                    texture = SDL_CreateTexture(renderer,
                        SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING,
                        render_w, render_h);
                    fprintf(stderr, "Render scale: 1/%d (%dx%d)\n",
                            render_scale, render_w, render_h);
            }
        }

        const Uint8 *keys = SDL_GetKeyboardState(NULL);

        /* Look: arrow keys */
        if (keys[SDL_SCANCODE_LEFT])  cam_yaw   -= look_speed * dt;
        if (keys[SDL_SCANCODE_RIGHT]) cam_yaw   += look_speed * dt;
        if (keys[SDL_SCANCODE_UP])    cam_pitch += look_speed * dt;
        if (keys[SDL_SCANCODE_DOWN])  cam_pitch -= look_speed * dt;
        if (cam_pitch >  1.4f) cam_pitch =  1.4f;
        if (cam_pitch < -1.4f) cam_pitch = -1.4f;

        /* Movement: WASD + Space/Shift for vertical */
        vector forward = { sinf(cam_yaw), 0.0f, cosf(cam_yaw) };
        vector right   = { cosf(cam_yaw), 0.0f, -sinf(cam_yaw) };
        if (keys[SDL_SCANCODE_W]) cam_pos = vector_add(cam_pos, vector_scale(forward, move_speed * dt));
        if (keys[SDL_SCANCODE_S]) cam_pos = vector_add(cam_pos, vector_scale(forward, -move_speed * dt));
        if (keys[SDL_SCANCODE_D]) cam_pos = vector_add(cam_pos, vector_scale(right, move_speed * dt));
        if (keys[SDL_SCANCODE_A]) cam_pos = vector_add(cam_pos, vector_scale(right, -move_speed * dt));
        if (keys[SDL_SCANCODE_SPACE])  cam_pos.y += move_speed * dt;
        if (keys[SDL_SCANCODE_LSHIFT]) cam_pos.y -= move_speed * dt;

        vector cam_dir = cam_dir_from_yaw_pitch(cam_yaw, cam_pitch);
        rt_camera_place(camera, cam_pos, cam_dir);

        rt_renderer_render(rnd, scene, camera, &viewport, pixels);

        SDL_UpdateTexture(texture, NULL, pixels, render_w * sizeof(uint32_t));
        SDL_RenderCopy(renderer, texture, NULL, NULL);
        SDL_RenderPresent(renderer);

        fps_frames++;
        Uint32 now = SDL_GetTicks();
        if (now - fps_last >= 1000) {
            snprintf(title_buf, sizeof(title_buf),
                     "Raytrace Demo - %d FPS (%dx%d, 1/%d scale, %s)",
                     fps_frames, render_w, render_h, render_scale, rt_renderer_name(rnd));
            SDL_SetWindowTitle(window, title_buf);
            fprintf(stderr, "%d FPS\n", fps_frames);
            fps_frames = 0;
            fps_last = now;
        }
    }

    rt_renderer_destroy(rnd);
    free(pixels);
    rt_camera_destroy(camera);
    rt_scene_destroy(scene);
    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
