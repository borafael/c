#include "input.h"
#include <SDL2/SDL.h>

void input_poll(input_events* events) {
    events->quit = 0;
    events->reset = 0;

    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        if (e.type == SDL_QUIT) {
            events->quit = 1;
        }
        if (e.type == SDL_KEYDOWN) {
            if (e.key.keysym.sym == SDLK_ESCAPE) {
                events->quit = 1;
            }
            if (e.key.keysym.sym == SDLK_r) {
                events->reset = 1;
            }
        }
    }
}
