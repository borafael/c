/* Ricochet: a first-person shooter where everything is a sphere.
 *
 * Enemies are rolling spheres with one glowing eye that hunt you across a
 * walled arena. Your machine gun sprays little glowing spheres that really
 * collide: they hop off enemies (shoving them), off the floor, walls,
 * pillars and each other, then settle. You only own --ammo of them (40), so once
 * they're spent you must walk over them to pick them back up. A ball
 * at rest pulses brighter and dimmer so you can find it.
 *
 * Every sound is synthesized at startup (see sfx.c) and panned and
 * faded by distance relative to the player:
 *   shoot   fired          hit     ball strikes an enemy hard enough to hurt
 *   bounce  ball on floor  wall    ball on wall / pillar / other ball
 *   pickup, pop (enemy destroyed), hurt, dry (empty trigger)
 *
 * Controls
 *   WASD move   mouse look (arrows fallback)   hold LMB / SPACE to fire
 *   SHIFT sprint   M toggle mouse capture   R restart
 *   V cycle render resolution (- / = step down / up)   B bloom
 *   TAB CPU/OpenGL   F11 fullscreen   ESC / P pause menu
 *
 * Menus: a title screen over a quiet bot match (START / HIGH SCORES /
 * OPTIONS / QUIT), a pause menu, and options for the magazine, starting
 * ammo, starting wave, mouse speed, reflections, bloom, renderer,
 * resolution and fullscreen. Losing window focus pauses.
 *
 * Where things live:
 *   main.c      command line, then hands off to app or headless
 *   app.c       window, display, screens and their input, main loop
 *   game.c      rules and state: player, enemies, waves, ammo, scoring
 *   bot.c       the autopilot
 *   render.c    scene building, gun viewmodel, post effects, HUD
 *   ui.c        menu contents, options, high-score screen
 *   level.c     level file format (see level.h)   levels.c  the level list
 *   nav.c       grid paths around walls for enemies and the bot
 *   menu.c      generic list-menu widget     hud.c  HUD font + shapes
 *   highscore.c local top-10 table           sfx.c  synthesized sounds
 *   headless.c  --shot / --bench
 *
 * Starts on the OpenGL compute backend when the driver has GL 4.3
 * (Linux / Windows), otherwise on the CPU tracer; --cpu forces the CPU.
 *
 * Flags: --fullscreen (start fullscreen), --no-reflections (matte surfaces,
 * roughly 40% cheaper on the CPU tracer).
 *
 * Flags: --bot (autopilot plays), --shot out.ppm [--sim N] (headless:
 * the bot plays N seconds, renders one frame and prints sound-event
 * counts), --dump-sfx DIR (write every sound as a WAV), --seed N. */

#include "app.h"
#include "game.h"
#include "headless.h"
#include "level.h"
#include "render.h"
#include "sfx.h"

#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static void usage(const char *argv0) {
    printf("Usage: %s [options]\n", argv0);
    printf("  --cpu              Start on the CPU tracer (default: OpenGL when available)\n");
    printf("  -G, --gpu          Start on the OpenGL backend (the default; kept for scripts)\n");
    printf("  --ammo N           Magazine size: spheres that exist (default %d, max %d)\n",
           DEFAULT_AMMO, AMMO_CAP);
    printf("  --start-ammo N     Spheres in hand at start (default: full); the rest\n"
           "                     lie scattered on the floor to collect\n");
    printf("  --wave N           Start at wave N (default 1, max %d): more, bigger,\n"
           "                     faster enemies from the first fight\n", MAX_START_WAVE);
    printf("  --level FILE       Play this level file (default: the arena; the title\n"
           "                     menu lists every levels/NAME.lvl)\n");
    printf("  --bot              Let the autopilot play (skips the title screen)\n");
    printf("  --fullscreen       Start in desktop fullscreen (F11 toggles)\n");
    printf("  --no-reflections   Matte surfaces only: much cheaper on the CPU tracer\n");
    printf("  --seed N           RNG seed\n");
    printf("  --shot out.ppm     Headless: bot plays --sim seconds, render one frame\n");
    printf("  --sim N            With --shot: seconds to simulate (default 6)\n");
    printf("  --shot-menu NAME   With --shot: draw the title, pause, options or scores screen\n");
    printf("  --scores FILE      High-score file (default: scores.txt in the per-user\n"
           "                     data folder SDL picks for this system)\n");
    printf("  --res WxH          With --shot: output resolution (default 960x600)\n");
    printf("  --bench N          Headless: bot plays --sim seconds, then time N CPU\n"
           "                     renders per variant (features toggled off one by one)\n");
    printf("  --dump-sfx DIR     Write every synthesized sound as DIR/<name>.wav\n");
    printf("\nWASD move, mouse look, hold LMB/SPACE to fire, SHIFT sprint, walk over spheres to\n");
    printf("recollect them. ESC / P pause menu, M mouse capture, R restart,\n");
    printf("V cycle resolution (-/= step), TAB backend, B bloom, F11 fullscreen,\n");
    printf("F5 reload the level file (for editing levels).\n");
    printf("Menus: arrows / WASD or the mouse, Enter select, left / right change, ESC back.\n");
}

int main(int argc, char *argv[]) {
    rt_backend preferred = RT_BACKEND_OPENGL;   /* falls back to CPU */
    const char *shot_path = NULL, *dump_dir = NULL, *shot_menu = NULL, *scores_path = NULL;
    const char *level_path = NULL;
    int shot_w = INIT_WINDOW_W, shot_h = INIT_WINDOW_H, bot = 0, start_fullscreen = 0;
    float sim_seconds = 6.0f;
    int bench_frames = 0;
    uint64_t seed = (uint64_t)time(NULL);

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-G") || !strcmp(argv[i], "--gpu")) preferred = RT_BACKEND_OPENGL;
        else if (!strcmp(argv[i], "--cpu")) preferred = RT_BACKEND_CPU;
        else if (!strcmp(argv[i], "--ammo") && i + 1 < argc) game_cfg.mag = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--start-ammo") && i + 1 < argc) {
            game_cfg.start_ammo = atoi(argv[++i]);
            if (game_cfg.start_ammo < 0) game_cfg.start_ammo = INT_MAX;   /* rejected below */
        }
        else if (!strcmp(argv[i], "--wave") && i + 1 < argc) game_cfg.start_wave = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--bot")) bot = 1;
        else if (!strcmp(argv[i], "--fullscreen")) start_fullscreen = 1;
        else if (!strcmp(argv[i], "--no-reflections")) render_opts.no_reflections = 1;
        else if (!strcmp(argv[i], "--seed") && i + 1 < argc) seed = strtoull(argv[++i], NULL, 10);
        else if (!strcmp(argv[i], "--shot") && i + 1 < argc) shot_path = argv[++i];
        else if (!strcmp(argv[i], "--shot-menu") && i + 1 < argc) {
            shot_menu = argv[++i];
            if (strcmp(shot_menu, "title") && strcmp(shot_menu, "pause") &&
                strcmp(shot_menu, "options") && strcmp(shot_menu, "scores")) {
                fprintf(stderr, "--shot-menu must be title, pause, options or scores\n");
                return 1;
            }
        }
        else if (!strcmp(argv[i], "--scores") && i + 1 < argc) scores_path = argv[++i];
        else if (!strcmp(argv[i], "--level") && i + 1 < argc) level_path = argv[++i];
        else if (!strcmp(argv[i], "--sim") && i + 1 < argc) sim_seconds = (float)atof(argv[++i]);
        else if (!strcmp(argv[i], "--dump-sfx") && i + 1 < argc) dump_dir = argv[++i];
        else if (!strcmp(argv[i], "--bench") && i + 1 < argc) bench_frames = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--res") && i + 1 < argc) {
            if (sscanf(argv[++i], "%dx%d", &shot_w, &shot_h) != 2 || shot_w <= 0 || shot_h <= 0) {
                fprintf(stderr, "Bad --res, expected WxH\n");
                return 1;
            }
        } else if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) { usage(argv[0]); return 0; }
        else { fprintf(stderr, "Unknown option: %s\n", argv[i]); usage(argv[0]); return 1; }
    }
    if (game_cfg.mag < 1 || game_cfg.mag > AMMO_CAP) {
        fprintf(stderr, "--ammo must be 1..%d\n", AMMO_CAP);
        return 1;
    }
    if (game_cfg.start_ammo > game_cfg.mag) {
        fprintf(stderr, "--start-ammo must be 0..%d (the magazine size)\n", game_cfg.mag);
        return 1;
    }
    if (game_cfg.start_wave < 1 || game_cfg.start_wave > MAX_START_WAVE) {
        fprintf(stderr, "--wave must be 1..%d\n", MAX_START_WAVE);
        return 1;
    }
    rng_seed(seed);

    if (level_path) {   /* checked up front, so a typo fails fast */
        static level lv;
        char err[256];
        if (level_load(&lv, level_path, err, sizeof err) != 0) {
            fprintf(stderr, "%s\n", err);
            return 1;
        }
        game_cfg.level = &lv;
    }

    static game g;
    game_reset(&g);

    if (dump_dir) {
        sfx_init(0);
        int rc = sfx_dump_wav(dump_dir);
        sfx_shutdown();
        return rc == 0 ? 0 : 1;
    }
    if (bench_frames > 0) {
        sfx_init(0);
        int rc = run_bench(&g, sim_seconds, shot_w, shot_h, bench_frames);
        sfx_shutdown();
        return rc;
    }
    if (shot_path) {
        sfx_init(0);
        int rc = run_shot(&g, shot_path, sim_seconds, shot_w, shot_h, shot_menu, scores_path);
        sfx_shutdown();
        return rc;
    }

    app_config cfg = {preferred, bot, start_fullscreen, scores_path, level_path};
    return app_run(&g, &cfg);
}
