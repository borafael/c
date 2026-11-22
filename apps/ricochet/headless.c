#include "headless.h"
#include "bot.h"
#include "render.h"
#include "sfx.h"
#include "ui.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ------------------------------------------------------------- Headless */

static int write_ppm(const char *path, const uint32_t *px, int w, int h) {
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    fprintf(f, "P6\n%d %d\n255\n", w, h);
    for (int i = 0; i < w * h; i++) {
        uint8_t rgb[3] = {(uint8_t)(px[i] >> 16), (uint8_t)(px[i] >> 8), (uint8_t)px[i]};
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
    return 0;
}

/* menu_name: NULL, or "title" / "pause" / "options" / "scores" drawn over
 * the frame ("scores" shows the --scores file, read-only). */
int run_shot(game *g, const char *path, float sim_seconds, int w, int h,
             const char *menu_name, const char *scores_path) {
    rt_renderer *rnd = rt_renderer_create(RT_BACKEND_CPU);
    if (!rnd) { fprintf(stderr, "CPU renderer unavailable\n"); return 1; }
    scene *s = scene_create();
    scene_camera *cam = scene_camera_create((vector){0, EYE_H, 0}, (vector){0, 0, 1});
    fx_state fx = {postfx_bloom_create(w, h), 1};

    const float dt = 1.0f / 60.0f;
    int frames = (int)(sim_seconds / dt);
    for (int f = 0; f < frames; f++) {
        scene_camera_place(cam, eye_pos(g), cam_forward(g->pl.yaw, g->pl.pitch));
        g->ear = eye_pos(g);
        g->ear_right = cam->right;
        input in;
        bot_input(g, &in, dt);
        game_update(g, &in, dt);
    }

    uint32_t *px = calloc((size_t)w * (size_t)h, sizeof *px);
    int title = menu_name && !strcmp(menu_name, "title");
    render_frame(g, rnd, s, cam, px, w, h, &fx, !menu_name);
    if (menu_name && !strcmp(menu_name, "scores")) {
        hs_table tab;
        if (!scores_path || hs_load(&tab, scores_path) != 0) hs_clear(&tab);
        draw_scores((hud_target){px, w, h}, &tab, game_cfg.level->name, 0, 0.0f);
    } else if (menu_name) {
        title_set_level(game_cfg.level->name, 1);
        int bloom = fx.bloom_on, fullscreen = 0;
        option_items[O_BLOOM].value = &bloom;
        option_items[O_FULLSCREEN].value = &fullscreen;
        options_init();
        options_refresh(0, 0, h, !title);
        menu *m = title ? &title_menu : !strcmp(menu_name, "pause") ? &pause_menu : &options_menu;
        menu_draw(m, (hud_target){px, w, h}, 0.0f);
        option_items[O_BLOOM].value = option_items[O_FULLSCREEN].value = NULL;
    }
    int rc = write_ppm(path, px, w, h);

    int in_world = 0, resting = 0;
    for (int i = 0; i < g->mag; i++) {
        physics_body b = ball_get(g, i);
        in_world += b.enabled;
        resting += b.enabled && b.sleeping;
    }
    fprintf(stderr, "sim %.1fs: wave %d, kills %d, score %d, hp %.0f, shots %d, hits %d, "
                    "ammo %d in hand / %d in world (%d resting)%s\n",
            sim_seconds, g->wave, g->kills, g->score, g->pl.hp, g->shots, g->hits,
            g->ammo, in_world, resting, g->game_over ? ", GAME OVER" : "");
    {
        float sum = 0; int n = 0;
        for (int i = 0; i < g->mag; i++) {
            physics_body b = ball_get(g, i);
            if (!b.enabled) continue;
            sum += hypotf(b.position.x - g->pl.p.x, b.position.z - g->pl.p.z); n++; }
        if (n) fprintf(stderr, "balls in world: mean distance from player %.1f m\n", sum / n);
    }
    fprintf(stderr, "sound events:");
    for (int i = 0; i < SFX_COUNT; i++) fprintf(stderr, " %s=%d", sfx_name(i), sfx_count(i));
    fprintf(stderr, "\n-> %s\n", path);

    free(px);
    postfx_bloom_destroy(fx.bloom);
    scene_camera_destroy(cam);
    scene_destroy(s);
    rt_renderer_destroy(rnd);
    return rc == 0 ? 0 : 1;
}

static double now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
}

/* Plays the bot for sim_seconds, then renders that frozen state `frames`
 * times per variant, each variant switching one feature off. */
int run_bench(game *g, float sim_seconds, int w, int h, int frames) {
    rt_renderer *rnd = rt_renderer_create(RT_BACKEND_CPU);
    if (!rnd) { fprintf(stderr, "CPU renderer unavailable\n"); return 1; }
    scene *s = scene_create();
    scene_camera *cam = scene_camera_create((vector){0, EYE_H, 0}, (vector){0, 0, 1});
    const float dt = 1.0f / 60.0f;
    for (int f = 0; f < (int)(sim_seconds / dt); f++) {
        scene_camera_place(cam, eye_pos(g), cam_forward(g->pl.yaw, g->pl.pitch));
        g->ear = eye_pos(g);
        g->ear_right = cam->right;
        input in;
        bot_input(g, &in, dt);
        game_update(g, &in, dt);
    }
    uint32_t *px = calloc((size_t)w * (size_t)h, sizeof *px);
    fx_state fx = {postfx_bloom_create(w, h), 1};

    static const char *NAMES[] = {"full", "no reflections", "no rifle",
                                  "no particles", "no bloom", "bare (all off)"};
    double base = 0.0;
    for (int v = 0; v < 6; v++) {
        render_opts.no_reflections = v == 1 || v == 5;
        render_opts.no_gun   = v == 2 || v == 5;
        render_opts.no_parts = v == 3 || v == 5;
        fx.bloom_on    = !(v == 4 || v == 5);
        render_frame(g, rnd, s, cam, px, w, h, &fx, 1);   /* warm-up */
        double t0 = now_ms();
        for (int f = 0; f < frames; f++) render_frame(g, rnd, s, cam, px, w, h, &fx, 1);
        double ms = (now_ms() - t0) / frames;
        if (v == 0) {
            base = ms;
            int balls = 0;
            for (int i = 0; i < g->mag; i++) balls += ball_get(g, i).enabled;
            fprintf(stderr, "%dx%d, %d balls out, primitives: %d spheres, %d boxes, "
                            "%d cylinders, %d tori, %d discs\n",
                    w, h, balls, s->sphere_count, s->box_count, s->cylinder_count,
                    s->torus_count, s->disc_count);
        }
        fprintf(stderr, "  %-16s %7.2f ms/frame  (%5.1f FPS)  %+6.1f%%\n",
                NAMES[v], ms, 1000.0 / ms, v ? (ms - base) / base * 100.0 : 0.0);
    }
    free(px);
    postfx_bloom_destroy(fx.bloom);
    scene_camera_destroy(cam);
    scene_destroy(s);
    rt_renderer_destroy(rnd);
    return 0;
}
