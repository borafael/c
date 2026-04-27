#include "render.h"
#include <SDL2/SDL.h>
#include "gl_compat.h"
#include <stdio.h>
#include <stdlib.h>

static SDL_Window* window = NULL;
static SDL_GLContext gl_ctx = NULL;
static GLuint display_fbo = 0;
static int screen_width = 800;
static int screen_height = 400;

typedef struct {
    GLuint tex;
    int w;
    int h;
} render_texture;

int render_init(void) {
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        fprintf(stderr, "SDL init failed: %s\n", SDL_GetError());
        return -1;
    }

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

    window = SDL_CreateWindow("N-Body Simulation",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        0, 0, SDL_WINDOW_FULLSCREEN_DESKTOP | SDL_WINDOW_OPENGL);
    if (!window) {
        fprintf(stderr, "Window creation failed: %s\n", SDL_GetError());
        return -1;
    }

    SDL_GetWindowSize(window, &screen_width, &screen_height);

    gl_ctx = SDL_GL_CreateContext(window);
    if (!gl_ctx) {
        fprintf(stderr, "GL context creation failed: %s\n", SDL_GetError());
        return -1;
    }
    SDL_GL_SetSwapInterval(1);
    gl_compat_init((gl_compat_loader_fn)SDL_GL_GetProcAddress);

    glGenFramebuffers(1, &display_fbo);
    return 0;
}

void render_get_size(int* width, int* height) {
    *width = screen_width;
    *height = screen_height;
}

void render_toggle_fullscreen(void) {
    if (!window) return;
    Uint32 flags = SDL_GetWindowFlags(window);
    int is_fullscreen = (flags & SDL_WINDOW_FULLSCREEN_DESKTOP) != 0;
    if (is_fullscreen) {
        SDL_SetWindowFullscreen(window, 0);
        /* The window was created with 0x0 for fullscreen-desktop; give it
         * a sane windowed size on first toggle-out. */
        SDL_SetWindowSize(window, 1280, 720);
        SDL_SetWindowPosition(window,
            SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
    } else {
        SDL_SetWindowFullscreen(window, SDL_WINDOW_FULLSCREEN_DESKTOP);
    }
    SDL_GetWindowSize(window, &screen_width, &screen_height);
}

void render_clear(void) {
    glViewport(0, 0, screen_width, screen_height);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
}

void render_present(void) {
    SDL_GL_SwapWindow(window);
}

void render_delay(int ms) {
    SDL_Delay(ms);
}

void *render_create_texture(int width, int height) {
    render_texture *rt = malloc(sizeof(render_texture));
    if (!rt) return NULL;
    rt->w = width;
    rt->h = height;
    glGenTextures(1, &rt->tex);
    glBindTexture(GL_TEXTURE_2D, rt->tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0,
                 GL_BGRA, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    return rt;
}

void render_texture_update(void *texture, const void *pixels, int pitch) {
    render_texture *rt = (render_texture *)texture;
    glBindTexture(GL_TEXTURE_2D, rt->tex);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, pitch / 4);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, rt->w, rt->h,
                    GL_BGRA, GL_UNSIGNED_BYTE, pixels);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);

    glBindFramebuffer(GL_READ_FRAMEBUFFER, display_fbo);
    glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_2D, rt->tex, 0);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    glBlitFramebuffer(0, 0, rt->w, rt->h,
                      0, screen_height, screen_width, 0,
                      GL_COLOR_BUFFER_BIT, GL_NEAREST);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
}

void render_destroy_texture(void *texture) {
    if (!texture) return;
    render_texture *rt = (render_texture *)texture;
    glDeleteTextures(1, &rt->tex);
    free(rt);
}

void render_cleanup(void) {
    if (display_fbo) {
        glDeleteFramebuffers(1, &display_fbo);
        display_fbo = 0;
    }
    if (gl_ctx) SDL_GL_DeleteContext(gl_ctx);
    if (window) SDL_DestroyWindow(window);
    SDL_Quit();
}
