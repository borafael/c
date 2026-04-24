#include "input.h"
#include <stddef.h>
#include <SDL2/SDL.h>

#define ARRAY_LEN(a) (sizeof(a) / sizeof((a)[0]))

typedef struct {
    SDL_Keycode key;
    size_t offset;
} key_binding;

static const key_binding bindings[] = {
    { SDLK_ESCAPE, offsetof(input_events, quit) },
    { SDLK_r,      offsetof(input_events, reset) },
    { SDLK_EQUALS, offsetof(input_events, zoom_in) },
    { SDLK_MINUS,  offsetof(input_events, zoom_out) },
    { SDLK_f,      offsetof(input_events, speed_up) },
    { SDLK_s,      offsetof(input_events, speed_down) },
    { SDLK_UP,     offsetof(input_events, pan_up) },
    { SDLK_DOWN,   offsetof(input_events, pan_down) },
    { SDLK_LEFT,   offsetof(input_events, pan_left) },
    { SDLK_RIGHT,  offsetof(input_events, pan_right) },
    { SDLK_F11,    offsetof(input_events, toggle_fullscreen) },
};

void input_poll(input_events* events) {
    events->quit = 0;
    events->reset = 0;
    events->zoom_in = 0;
    events->zoom_out = 0;
    events->speed_up = 0;
    events->speed_down = 0;
    events->pan_up = 0;
    events->pan_down = 0;
    events->pan_left = 0;
    events->pan_right = 0;
    events->toggle_fullscreen = 0;

    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        if (e.type == SDL_QUIT) {
            events->quit = 1;
        }
        if (e.type == SDL_KEYDOWN) {
            for (size_t i = 0; i < ARRAY_LEN(bindings); i++) {
                if (e.key.keysym.sym == bindings[i].key) {
                    *(int *)((char *)events + bindings[i].offset) = 1;
                }
            }
        }
    }
}
