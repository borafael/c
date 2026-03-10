#include "raytrace.h"
#include <SDL2/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#define WINDOW_W 800
#define WINDOW_H 600
#define FOV (M_PI / 3.0f)

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

    *camera = rt_camera_create(
        (vector){5.0f, 3.0f, 5.0f},
        (vector){-1.0f, -0.4f, -1.0f}
    );
}

static void update_scene(rt_camera *camera, float angle,
                         float cam_dist, float cam_height) {
    vector cam_pos = {
        cam_dist * cosf(angle),
        cam_height,
        cam_dist * sinf(angle)
    };
    /* Look at the origin */
    vector cam_dir = vector_sub((vector){0.0f, 0.5f, 0.0f}, cam_pos);
    rt_camera_place(camera, cam_pos, cam_dir);
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
    SDL_Texture *texture = SDL_CreateTexture(renderer,
        SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING,
        WINDOW_W, WINDOW_H);

    uint32_t *pixels = calloc(WINDOW_W * WINDOW_H, sizeof(uint32_t));

    rt_scene *scene;
    rt_camera *camera;
    build_scene(&scene, &camera);

    rt_viewport viewport = { WINDOW_W, WINDOW_H, FOV };

    float angle = 0.0f;
    float cam_dist = 10.0f;
    float cam_height = 3.0f;
    int running = 1;

    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) running = 0;
            if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE)
                running = 0;
        }

        angle += 0.01f;
        update_scene(camera, angle, cam_dist, cam_height);

        rt_render_chunk(pixels, &viewport, 0, WINDOW_H, camera, scene);

        SDL_UpdateTexture(texture, NULL, pixels, WINDOW_W * sizeof(uint32_t));
        SDL_RenderCopy(renderer, texture, NULL, NULL);
        SDL_RenderPresent(renderer);
        SDL_Delay(16);
    }

    free(pixels);
    rt_camera_destroy(camera);
    rt_scene_destroy(scene);
    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
