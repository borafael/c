#include "render.h"
#include <SDL2/SDL.h>
#include <stdio.h>

static SDL_Window* window = NULL;
static SDL_Renderer* renderer = NULL;
static int screen_width = 800;
static int screen_height = 400;

int render_init(void) {
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        fprintf(stderr, "SDL init failed: %s\n", SDL_GetError());
        return -1;
    }

    window = SDL_CreateWindow("N-Body Simulation",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        0, 0, SDL_WINDOW_FULLSCREEN_DESKTOP);

    if (!window) {
        fprintf(stderr, "Window creation failed: %s\n", SDL_GetError());
        return -1;
    }

    SDL_GetWindowSize(window, &screen_width, &screen_height);

    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    if (!renderer) {
        fprintf(stderr, "Renderer creation failed: %s\n", SDL_GetError());
        return -1;
    }

    return 0;
}

void render_get_size(int* width, int* height) {
    *width = screen_width;
    *height = screen_height;
}

void render_clear(void) {
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);
}

void render_circle(int cx, int cy, int radius, uint8_t r, uint8_t g, uint8_t b) {
    SDL_SetRenderDrawColor(renderer, r, g, b, 255);
    for (int y = -radius; y <= radius; y++) {
        for (int x = -radius; x <= radius; x++) {
            if (x*x + y*y <= radius*radius) {
                SDL_RenderDrawPoint(renderer, cx + x, cy + y);
            }
        }
    }
}

void render_present(void) {
    SDL_RenderPresent(renderer);
}

void render_delay(int ms) {
    SDL_Delay(ms);
}

void *render_create_texture(int width, int height) {
    SDL_Texture *tex = SDL_CreateTexture(renderer,
        SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING,
        width, height);
    if (!tex) {
        fprintf(stderr, "Texture creation failed: %s\n", SDL_GetError());
    }
    return tex;
}

void render_texture_update(void *texture, const void *pixels, int pitch) {
    SDL_Texture *tex = (SDL_Texture *)texture;
    SDL_UpdateTexture(tex, NULL, pixels, pitch);
    SDL_RenderCopy(renderer, tex, NULL, NULL);
}

void render_destroy_texture(void *texture) {
    if (texture) SDL_DestroyTexture((SDL_Texture *)texture);
}

void render_cleanup(void) {
    if (renderer) SDL_DestroyRenderer(renderer);
    if (window) SDL_DestroyWindow(window);
    SDL_Quit();
}
