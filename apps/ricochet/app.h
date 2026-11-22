#ifndef RICOCHET_APP_H
#define RICOCHET_APP_H

#include "game.h"
#include "renderer.h"

#define INIT_WINDOW_W 960
#define INIT_WINDOW_H 600

typedef struct {
    rt_backend preferred;      /* OpenGL falls back to CPU */
    int bot;                   /* autopilot plays; skips the title */
    int fullscreen;
    const char *scores_path;   /* NULL: scores.txt in SDL's per-user folder */
    const char *level_path;    /* --level FILE: start on it (NULL: the arena) */
} app_config;

/* Open the window and run until the player quits. Returns the exit code. */
int app_run(game *g, const app_config *cfg);

#endif
