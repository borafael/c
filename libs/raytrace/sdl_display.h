#ifndef RT_SDL_DISPLAY_H
#define RT_SDL_DISPLAY_H

/*
 * Header-only helpers shared by the SDL demo apps' display path. Kept
 * inline so libraytrace itself still has no SDL2 dependency — only the
 * apps that include this header (and already link SDL2) see it.
 */

#include <SDL2/SDL.h>
#include <stdio.h>

/* Request the GL context the apps blit through. Display itself only
 * needs FBO blits (GL 3.0); 4.3 is asked for so the OpenGL compute
 * backend can share the context where it's built. macOS caps core
 * contexts at 4.1 and refuses anything higher — and never has the
 * compute backend anyway — so ask for 4.1 there. Call before
 * SDL_CreateWindow. */
static inline void sdl_display_request_context(void) {
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
#ifdef __APPLE__
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS,
                        SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
#else
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
#endif
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,
                        SDL_GL_CONTEXT_PROFILE_CORE);
}

/* What TAB says when there is no second backend to toggle to — rather
 * than silently doing nothing. configure drops the OpenGL compute
 * backend wherever GL 4.3 is missing, which includes every Mac. */
static inline void sdl_display_note_single_backend(const char *active_name) {
    fprintf(stderr, "Only the %s backend was built; nothing to toggle to "
                    "(OpenGL compute needs GL 4.3).\n", active_name);
}

#endif
