/* The interactive game: window, GL display, resolution ladder, the
 * screens (title / play / pause / options / scores) and their input. */

#include "app.h"
#include "bot.h"
#include "highscore.h"
#include "hud.h"
#include "menu.h"
#include "render.h"
#include "sfx.h"
#include "ui.h"

#include <SDL2/SDL.h>

#define GL_GLEXT_PROTOTYPES 1
#include "gl_compat.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define LOOK_SPEED    2.2f   /* arrow-key look, radians per second */

/* Render-resolution ladder, by height; the width follows the window's
 * aspect so nothing stretches windowed or fullscreen. 0 = native window
 * size. Rungs taller than the window are skipped. V cycles, -/= step. */
static const int RES_HEIGHTS[] = {150, 200, 240, 300, 400, 600, 0};
#define RES_N           ((int)(sizeof RES_HEIGHTS / sizeof RES_HEIGHTS[0]))
#define RES_DEFAULT_IDX 0   /* 150 rows: chunky, cheapest; same cost windowed or fullscreen */

static int res_usable(int idx, int window_h) {
    return RES_HEIGHTS[idx] == 0 || RES_HEIGHTS[idx] < window_h;
}

/* Next usable rung in direction dir (+1 / -1). wrap: cycle past the ends. */
static int res_step(int idx, int dir, int wrap, int window_h) {
    for (int k = 0; k < RES_N; k++) {
        int n = idx + dir;
        if (n < 0 || n >= RES_N) {
            if (!wrap) return idx;
            n = (n + RES_N) % RES_N;
        }
        idx = n;
        if (res_usable(idx, window_h)) return idx;
    }
    return idx;
}

static void res_size(int idx, int window_w, int window_h, int *w, int *h) {
    int rh = RES_HEIGHTS[idx];
    if (rh == 0 || rh > window_h) rh = window_h;
    int rw = (int)((float)rh * (float)window_w / (float)window_h + 0.5f);
    *w = rw < 1 ? 1 : rw;
    *h = rh < 1 ? 1 : rh;
}

/* -------------------------------------------------------------- Display */

static void display_pixels(GLuint tex, GLuint fbo, const uint32_t *pixels,
                           int rw, int rh, int ww, int wh) {
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, rw, rh, GL_BGRA, GL_UNSIGNED_BYTE, pixels);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    glBlitFramebuffer(0, 0, rw, rh, 0, wh, ww, 0, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
}

/* ------------------------------------------------------------------ App */

/* Everything the interactive loop owns besides the game itself. */
typedef struct {
    SDL_Window *window;
    int window_w, window_h, fullscreen;
    rt_renderer *cpu, *gpu, *active;
    int res_idx, render_w, render_h;
    float res_toast;            /* seconds left to show the resolution note */
    uint32_t *pixels;
    GLuint tex, fbo;
    fx_state fx;
    int mouse_captured;
    int fire_block;             /* ignore the trigger until it is released */
    screen_id screen;
    screen_id options_from;     /* where BACK returns from OPTIONS */
    int bot;                    /* --bot: the autopilot plays the real game */
    int attract;                /* the bot plays behind the title screen */
    float attract_over;         /* seconds since that match ended */
    float ui_t;
    int quit;
    hs_table scores;
    char scores_path[1024];     /* "" = don't save */
    int last_rank;              /* highlighted on the HIGH SCORES screen */
} app;

static void app_capture_mouse(app *a, int on) {
    a->mouse_captured = on;
    SDL_SetRelativeMouseMode(on ? SDL_TRUE : SDL_FALSE);
}

/* Reallocate the render buffer after the window or the rung changed. */
static void app_resize(app *a) {
    /* After a window change the current rung may be too tall. */
    if (!res_usable(a->res_idx, a->window_h)) a->res_idx = res_step(a->res_idx, -1, 0, a->window_h);
    res_size(a->res_idx, a->window_w, a->window_h, &a->render_w, &a->render_h);
    a->res_toast = 1.5f;
    free(a->pixels);
    a->pixels = calloc((size_t)a->render_w * (size_t)a->render_h, sizeof(uint32_t));
    glBindTexture(GL_TEXTURE_2D, a->tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, a->render_w, a->render_h, 0,
                 GL_BGRA, GL_UNSIGNED_BYTE, NULL);
    fprintf(stderr, "Render resolution: %dx%d%s\n", a->render_w, a->render_h,
            RES_HEIGHTS[a->res_idx] == 0 ? " (native)" : "");
}

static void app_step_res(app *a, int dir, int wrap) {
    a->res_idx = res_step(a->res_idx, dir, wrap, a->window_h);
    app_resize(a);
}

static void app_set_fullscreen(app *a, int on) {
    a->fullscreen = on;
    SDL_SetWindowFullscreen(a->window, on ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
    SDL_GetWindowSize(a->window, &a->window_w, &a->window_h);
    app_resize(a);
}

static void app_toggle_backend(app *a) {
    if (a->active == a->cpu && a->gpu) a->active = a->gpu;
    else if (a->active == a->gpu && a->cpu) a->active = a->cpu;
    fprintf(stderr, "Active: %s\n", rt_renderer_name(a->active));
}

/* ------------------------------------------------------------ Screens */

/* A game you played is over (died, restarted, left or quit): put it on
 * the table if it made it. Bot games never count. Runs once per game. */
static void finish_game(app *a, game *g) {
    if (a->attract || a->bot || g->hs_rank != HS_UNRECORDED) return;
    hs_entry e = {g->score, g->wave, g->kills, g->shots, g->hits, g->mag,
                  g->start_wave, g->start_ammo, (long long)time(NULL)};
    g->hs_rank = hs_insert(&a->scores, &e);
    if (g->hs_rank < 0) return;
    a->last_rank = g->hs_rank;
    if (a->scores_path[0] && hs_save(&a->scores, a->scores_path) != 0)
        fprintf(stderr, "Could not save high scores to %s\n", a->scores_path);
}

static void start_game(app *a, game *g) {
    finish_game(a, g);
    game_reset(g);
    a->attract = 0;
    game_volume = 1.0f;
    a->screen = SCREEN_PLAY;
    app_capture_mouse(a, 1);
    a->fire_block = 1;
}

static void enter_title(app *a, game *g) {
    finish_game(a, g);
    game_reset(g);
    a->attract = 1;
    a->attract_over = 0.0f;
    game_volume = 0.25f;
    a->screen = SCREEN_TITLE;
    title_menu.sel = T_START;
    app_capture_mouse(a, 0);
}

static void pause_game(app *a) {
    a->screen = SCREEN_PAUSE;
    pause_menu.sel = P_RESUME;
    app_capture_mouse(a, 0);
}

static void resume_game(app *a) {
    a->screen = SCREEN_PLAY;
    app_capture_mouse(a, 1);
    a->fire_block = 1;
}

static void open_options(app *a) {
    a->options_from = a->screen;
    a->screen = SCREEN_OPTIONS;
    options_menu.sel = O_MAG;
}

static menu *current_menu(const app *a) {
    switch (a->screen) {
    case SCREEN_TITLE:   return &title_menu;
    case SCREEN_PAUSE:   return &pause_menu;
    case SCREEN_OPTIONS: return &options_menu;
    default:             return NULL;
    }
}

static void on_menu_event(app *a, game *g, menu_event ev) {
    menu_feedback(ev);
    switch (a->screen) {
    case SCREEN_TITLE:
        if (ev.kind == MENU_BACK) title_menu.sel = T_QUIT;   /* ESC again + Enter quits */
        if (ev.kind != MENU_ACTIVATE) break;
        if (ev.item == T_START) start_game(a, g);
        else if (ev.item == T_SCORES) a->screen = SCREEN_SCORES;
        else if (ev.item == T_OPTIONS) open_options(a);
        else a->quit = 1;
        break;
    case SCREEN_PAUSE:
        if (ev.kind == MENU_BACK) { resume_game(a); break; }
        if (ev.kind != MENU_ACTIVATE) break;
        switch (ev.item) {
        case P_RESUME:  resume_game(a); break;
        case P_RESTART: start_game(a, g); break;
        case P_OPTIONS: open_options(a); break;
        case P_TITLE:   enter_title(a, g); break;
        case P_QUIT:    a->quit = 1; break;
        }
        break;
    case SCREEN_OPTIONS:
        if (ev.kind == MENU_BACK || (ev.kind == MENU_ACTIVATE && ev.item == O_BACK)) {
            a->screen = a->options_from;
            break;
        }
        if (ev.kind != MENU_CHANGED) break;
        switch (ev.item) {
        case O_MAG:
            options_ammo_changed(ev.item);
            /* In a game it applies now; the kill multiplier follows. */
            if (a->options_from == SCREEN_PAUSE) game_set_magazine(g, game_cfg.mag);
            break;
        case O_START: options_ammo_changed(ev.item); break;
        case O_REFLECT:    render_opts.no_reflections = !opt_reflect; break;
        case O_RENDERER:   app_toggle_backend(a); break;
        case O_RES:        app_step_res(a, ev.dir, 0); break;
        case O_FULLSCREEN: app_set_fullscreen(a, a->fullscreen); break;
        default: break;   /* wave, mouse speed, bloom: the new value is enough */
        }
        break;
    default:
        break;
    }
}

/* ---------------------------------------------------------------- Input */

static void menu_input(app *a, game *g, menu *m, const SDL_Event *e) {
    menu_event ev = {MENU_NONE, -1, 0};
    /* Window pixels -> render-buffer pixels (the blit stretches). */
    int mx = 0, my = 0;
    if (e->type == SDL_MOUSEMOTION) { mx = e->motion.x; my = e->motion.y; }
    else if (e->type == SDL_MOUSEBUTTONDOWN) { mx = e->button.x; my = e->button.y; }
    else if (e->type == SDL_MOUSEWHEEL) SDL_GetMouseState(&mx, &my);
    mx = a->window_w > 0 ? mx * a->render_w / a->window_w : 0;
    my = a->window_h > 0 ? my * a->render_h / a->window_h : 0;

    if (e->type == SDL_KEYDOWN) {
        switch (e->key.keysym.sym) {
        case SDLK_UP:    case SDLK_w: ev = menu_on_key(m, MENU_KEY_UP); break;
        case SDLK_DOWN:  case SDLK_s: ev = menu_on_key(m, MENU_KEY_DOWN); break;
        case SDLK_LEFT:  case SDLK_a: ev = menu_on_key(m, MENU_KEY_LEFT); break;
        case SDLK_RIGHT: case SDLK_d: ev = menu_on_key(m, MENU_KEY_RIGHT); break;
        case SDLK_RETURN: case SDLK_KP_ENTER: case SDLK_SPACE:
            if (!e->key.repeat) ev = menu_on_key(m, MENU_KEY_OK);
            break;
        case SDLK_ESCAPE: case SDLK_BACKSPACE:
            if (!e->key.repeat) ev = menu_on_key(m, MENU_KEY_BACK);
            break;
        default: break;
        }
    } else if (e->type == SDL_MOUSEMOTION) {
        ev = menu_on_motion(m, mx, my);
    } else if (e->type == SDL_MOUSEBUTTONDOWN) {
        int b = e->button.button == SDL_BUTTON_LEFT ? 1 : e->button.button == SDL_BUTTON_RIGHT ? 3 : 0;
        ev = menu_on_click(m, mx, my, b);
    } else if (e->type == SDL_MOUSEWHEEL) {
        ev = menu_on_wheel(m, mx, my, e->wheel.y);
    }
    if (ev.kind != MENU_NONE) on_menu_event(a, g, ev);
}

static void play_input(app *a, game *g, const SDL_Event *e, input *in) {
    if (e->type == SDL_MOUSEMOTION && a->mouse_captured) {
        in->look_dx += e->motion.xrel * mouse_sens();
        in->look_dy -= e->motion.yrel * mouse_sens();
    } else if (e->type == SDL_MOUSEBUTTONDOWN && e->button.button == SDL_BUTTON_LEFT) {
        if (!a->mouse_captured) {
            /* This click only grabs the mouse; don't start firing until
             * the button is released and pressed again. */
            app_capture_mouse(a, 1);
            a->fire_block = 1;
        }
    } else if (e->type == SDL_KEYDOWN && !e->key.repeat) {
        switch (e->key.keysym.sym) {
        case SDLK_ESCAPE: case SDLK_p: pause_game(a); break;
        case SDLK_r: start_game(a, g); break;
        case SDLK_b: a->fx.bloom_on = !a->fx.bloom_on; break;
        case SDLK_m: app_capture_mouse(a, !a->mouse_captured); break;
        case SDLK_TAB: app_toggle_backend(a); break;
        case SDLK_v: app_step_res(a, +1, 1); break;
        case SDLK_MINUS: case SDLK_KP_MINUS: app_step_res(a, -1, 0); break;
        case SDLK_EQUALS: case SDLK_KP_PLUS: app_step_res(a, +1, 0); break;
        default: break;
        }
    }
}

/* Held keys / buttons for this frame. */
static void play_controls(app *a, const game *g, input *in, float dt) {
    const Uint8 *keys = SDL_GetKeyboardState(NULL);
    int lmb = (SDL_GetMouseState(NULL, NULL) & SDL_BUTTON_LMASK) != 0;
    int space = keys[SDL_SCANCODE_SPACE];
    /* A click or Enter/Space that closed a menu must not also fire. */
    if (a->fire_block && !lmb && !space) a->fire_block = 0;
    int trigger = !a->fire_block && (space || (a->mouse_captured && lmb));
    if (a->bot) {
        bot_input(g, in, dt);
        in->fire |= trigger;
        return;
    }
    in->fire = trigger;
    in->move_f = (float)(keys[SDL_SCANCODE_W] - keys[SDL_SCANCODE_S]);
    in->move_s = (float)(keys[SDL_SCANCODE_D] - keys[SDL_SCANCODE_A]);
    in->sprint = keys[SDL_SCANCODE_LSHIFT] || keys[SDL_SCANCODE_RSHIFT];
    in->look_dx += (float)(keys[SDL_SCANCODE_RIGHT] - keys[SDL_SCANCODE_LEFT]) * LOOK_SPEED * dt;
    in->look_dy += (float)(keys[SDL_SCANCODE_UP] - keys[SDL_SCANCODE_DOWN]) * LOOK_SPEED * dt;
}

/* ----------------------------------------------------------------- Run */

int app_run(game *g, const app_config *cfg) {
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        fprintf(stderr, "SDL init failed: %s\n", SDL_GetError());
        return 1;
    }
    sfx_init(1);

    static app a;
    a.window_w = INIT_WINDOW_W;
    a.window_h = INIT_WINDOW_H;
    a.fullscreen = cfg->fullscreen;
    a.bot = cfg->bot;
    a.last_rank = -1;
    if (cfg->scores_path) {
        snprintf(a.scores_path, sizeof a.scores_path, "%s", cfg->scores_path);
    } else {
        char *dir = SDL_GetPrefPath("ricochet", "ricochet");   /* per-user data folder */
        if (dir) snprintf(a.scores_path, sizeof a.scores_path, "%sscores.txt", dir);
        SDL_free(dir);
    }
    if (a.scores_path[0] && hs_load(&a.scores, a.scores_path) != 0)
        fprintf(stderr, "Ignoring unreadable high scores in %s\n", a.scores_path);
    SDL_GLContext gl_ctx = NULL;
    static const int GL_VERSIONS[][2] = {{4, 3}, {4, 1}, {3, 3}};
    for (int v = 0; v < 3 && !gl_ctx; v++) {
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, GL_VERSIONS[v][0]);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, GL_VERSIONS[v][1]);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
        SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
        if (a.window) SDL_DestroyWindow(a.window);
        a.window = SDL_CreateWindow("Ricochet", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                    a.window_w, a.window_h,
                                    SDL_WINDOW_OPENGL | (a.fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0));
        if (!a.window) break;
        gl_ctx = SDL_GL_CreateContext(a.window);
    }
    if (!a.window || !gl_ctx) {
        fprintf(stderr, "Window/GL context: %s\n", SDL_GetError());
        if (a.window) SDL_DestroyWindow(a.window);
        sfx_shutdown();
        SDL_Quit();
        return 1;
    }
    SDL_GL_SetSwapInterval(1);
    gl_compat_init((gl_compat_loader_fn)SDL_GL_GetProcAddress);
    /* Fullscreen-at-start: size the render buffer to the real screen. */
    SDL_GetWindowSize(a.window, &a.window_w, &a.window_h);

    a.cpu = rt_renderer_available(RT_BACKEND_CPU) ? rt_renderer_create(RT_BACKEND_CPU) : NULL;
    a.gpu = rt_renderer_available(RT_BACKEND_OPENGL) ? rt_renderer_create(RT_BACKEND_OPENGL) : NULL;
    if (!a.cpu && !a.gpu) {
        fprintf(stderr, "No renderers available\n");
        SDL_GL_DeleteContext(gl_ctx);
        SDL_DestroyWindow(a.window);
        sfx_shutdown();
        SDL_Quit();
        return 1;
    }
    a.active = (cfg->preferred == RT_BACKEND_OPENGL && a.gpu) ? a.gpu : (a.cpu ? a.cpu : a.gpu);
    fprintf(stderr, "Active: %s\n", rt_renderer_name(a.active));

    scene *s = scene_create();
    scene_camera *cam = scene_camera_create((vector){0, EYE_H, 0}, (vector){0, 0, 1});

    a.res_idx = RES_DEFAULT_IDX;
    glGenTextures(1, &a.tex);
    glBindTexture(GL_TEXTURE_2D, a.tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glGenFramebuffers(1, &a.fbo);
    app_resize(&a);
    a.res_toast = 0.0f;
    a.fx = (fx_state){postfx_bloom_create(a.render_w, a.render_h), 1};
    option_items[O_BLOOM].value = &a.fx.bloom_on;
    option_items[O_FULLSCREEN].value = &a.fullscreen;
    options_init();

    if (cfg->bot) start_game(&a, g);   /* scripted runs skip the title */
    else enter_title(&a, g);

    Uint32 frame_last = SDL_GetTicks(), fps_last = frame_last;
    int fps_frames = 0;
    Uint32 render_ms = 0;
    char title[200];

    while (!a.quit) {
        Uint32 now = SDL_GetTicks();
        float dt = (now - frame_last) / 1000.0f;
        if (dt > 0.05f) dt = 0.05f;
        frame_last = now;
        a.ui_t += dt;
        input in = {0};
        options_refresh(a.active == a.gpu, a.cpu && a.gpu,
                        RES_HEIGHTS[a.res_idx] ? a.render_h : 0, a.options_from == SCREEN_PAUSE);

        SDL_Event e;
        while (SDL_PollEvent(&e) && !a.quit) {
            if (e.type == SDL_QUIT) { a.quit = 1; break; }
            if (e.type == SDL_WINDOWEVENT && e.window.event == SDL_WINDOWEVENT_FOCUS_LOST &&
                a.screen == SCREEN_PLAY && !a.bot) {
                pause_game(&a);
                continue;
            }
            if (e.type == SDL_KEYDOWN && !e.key.repeat && e.key.keysym.sym == SDLK_F11) {
                app_set_fullscreen(&a, !a.fullscreen);
                continue;
            }
            if (a.screen == SCREEN_SCORES) {   /* any key or click goes back */
                if ((e.type == SDL_KEYDOWN && !e.key.repeat) || e.type == SDL_MOUSEBUTTONDOWN) {
                    menu_feedback((menu_event){MENU_BACK, -1, 0});
                    a.screen = SCREEN_TITLE;
                }
                continue;
            }
            menu *m = current_menu(&a);   /* may change between events */
            if (m) menu_input(&a, g, m, &e);
            else play_input(&a, g, &e, &in);
        }
        if (a.quit) break;

        /* The game runs while playing and behind the title; pause and
         * options-from-pause freeze it. */
        if (a.screen == SCREEN_PLAY) {
            play_controls(&a, g, &in, dt);
            game_update(g, &in, dt);
            if (g->game_over) finish_game(&a, g);   /* once; shows the rank */
        } else if (a.attract) {
            bot_input(g, &in, dt);
            game_update(g, &in, dt);
            a.attract_over = g->game_over ? a.attract_over + dt : 0.0f;
            if (a.attract_over > 4.0f) {   /* next match; stay on this screen */
                game_reset(g);
                a.attract_over = 0.0f;
            }
        }

        hud_target t = {a.pixels, a.render_w, a.render_h};
        Uint32 r0 = SDL_GetTicks();
        render_frame(g, a.active, s, cam, a.pixels, a.render_w, a.render_h, &a.fx,
                     a.screen == SCREEN_PLAY);
        render_ms += SDL_GetTicks() - r0;
        menu *m = current_menu(&a);
        if (m) menu_draw(m, t, a.ui_t);
        if (a.screen == SCREEN_SCORES) draw_scores(t, &a.scores, a.last_rank, a.ui_t);
        if (a.res_toast > 0.0f) {
            a.res_toast -= dt;
            if (!m) {
                char note[48];
                snprintf(note, sizeof note, "RENDER %dX%d%s", a.render_w, a.render_h,
                         RES_HEIGHTS[a.res_idx] == 0 ? " NATIVE" : "");
                int sc = a.render_h / 150;
                hud_text_center(t, a.render_h * 2 / 3, note, sc < 1 ? 1 : sc, 0xFFB0F0FFu);
            }
        }
        display_pixels(a.tex, a.fbo, a.pixels, a.render_w, a.render_h, a.window_w, a.window_h);
        SDL_GL_SwapWindow(a.window);

        fps_frames++;
        if (now - fps_last >= 1000) {
            snprintf(title, sizeof title, "Ricochet  wave %d  ammo %d/%d  hp %.0f | %s %d FPS (%.1f ms, %dx%d)%s",
                     g->wave, g->ammo, g->mag, g->pl.hp, rt_renderer_name(a.active), fps_frames,
                     fps_frames ? (float)render_ms / fps_frames : 0.0f, a.render_w, a.render_h,
                     a.bot ? "  [BOT]" : "");
            SDL_SetWindowTitle(a.window, title);
            fps_frames = 0;
            render_ms = 0;
            fps_last = now;
        }
    }

    finish_game(&a, g);   /* quitting mid-game still records it */
    glDeleteFramebuffers(1, &a.fbo);
    glDeleteTextures(1, &a.tex);
    if (a.cpu) rt_renderer_destroy(a.cpu);
    if (a.gpu) rt_renderer_destroy(a.gpu);
    postfx_bloom_destroy(a.fx.bloom);
    free(a.pixels);
    scene_camera_destroy(cam);
    scene_destroy(s);
    sfx_shutdown();
    SDL_GL_DeleteContext(gl_ctx);
    SDL_DestroyWindow(a.window);
    SDL_Quit();
    return 0;
}
