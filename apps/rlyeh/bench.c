/* Headless benchmark (RLYEH_BENCH=1) — see bench.h.
 *
 * Measures the cost of adding a fully-reflective roaming sphere to the real
 * R'lyeh scene, with no SDL/GL (so it runs anywhere). Three conditions on the
 * identical scene + camera:
 *   baseline        — scene as shipped
 *   +occluder(r=0)  — one extra unlit sphere in the sky (isolates the
 *                     "+1 primitive in every scan" cost; no bounce)
 *   +mirror(r=1)    — same sphere flipped to a perfect mirror (adds the
 *                     reflection-bounce cost on its covered pixels)
 * Each is timed full-frame and again with the app's even-rows interlace. The
 * sphere's real screen coverage is counted from the G-buffer so we can report
 * a per-covered-pixel marginal cost. Pins its own reference resolution/FOV so
 * results stay comparable across runs even if the live app's config changes. */

#include "bench.h"
#include "renderer.h"
#include "viewport.h"
#include "scene.h"
#include "postfx.h"
#include "world.h"      /* world_rlyeh, EYE_HEIGHT */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <math.h>       /* M_PI for the reference FOV */
#include <time.h>       /* clock_gettime — timing */
#ifdef _WIN32
#include <windows.h>    /* GetSystemInfo — host core count */
#else
#include <unistd.h>     /* sysconf — host core count */
#endif

#define BENCH_W   240
#define BENCH_H   150
#define BENCH_FOV (M_PI / 2.6f)   /* mirrors the app's default FOV */

static long bench_host_cores(void) {
#ifdef _WIN32
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    return (long)si.dwNumberOfProcessors;
#else
    return sysconf(_SC_NPROCESSORS_ONLN);
#endif
}

static double bench_now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1.0e6;
}

static double bench_measure(rt_renderer *rnd, scene *s, scene_camera *cam,
                            rt_viewport *vp, uint32_t *px, rt_gbuffer *gb,
                            int n, double *out_min, double *out_max) {
    for (int i = 0; i < 5; i++)                 /* warm caches / clocks */
        rt_renderer_render(rnd, s, cam, vp, px, gb);
    double total = 0.0, mn = 1e30, mx = 0.0;
    for (int i = 0; i < n; i++) {
        double t0 = bench_now_ms();
        rt_renderer_render(rnd, s, cam, vp, px, gb);
        double dt = bench_now_ms() - t0;
        total += dt;
        if (dt < mn) mn = dt;
        if (dt > mx) mx = dt;
    }
    if (out_min) *out_min = mn;
    if (out_max) *out_max = mx;
    return total / (double)n;
}

int run_bench(void) {
    /* Bench the R'lyeh scene specifically (the heavier of the two). It ships
     * with no reflective spheres, so the baseline is clean: the occluder->
     * mirror deltas isolate exactly the one sphere this benchmark adds. */
    scene *s = NULL; scene_camera *cam = NULL; int sky = -1;
    postfx_fog fog_unused;
    world_rlyeh.build(&s, &cam, &sky, &fog_unused);

    /* Fixed representative pose: spawn point, level, looking +Z. The
     * roaming sphere (placed below) hangs in the upper third of this view. */
    scene_camera_place(cam, (vector){0.0f, EYE_HEIGHT, 0.0f},
                            (vector){0.0f, 0.0f, 1.0f});

    rt_renderer *rnd = rt_renderer_create(RT_BACKEND_CPU);
    if (!rnd) { fprintf(stderr, "CPU renderer unavailable\n"); return 1; }

    int W = BENCH_W, H = BENCH_H;
    rt_viewport vp = { W, H, BENCH_FOV };
    uint32_t *px = calloc((size_t)(W * H), sizeof(uint32_t));
    rt_gbuffer gb = {
        .object_id = calloc((size_t)(W * H), sizeof(uint32_t)),
        .depth     = calloc((size_t)(W * H), sizeof(float)),
        .normal    = calloc((size_t)(W * H) * 3, sizeof(float)),
    };

    const char *n_env     = getenv("RLYEH_BENCH_FRAMES");
    const int   N         = n_env ? atoi(n_env) : 120;
    const int   fields[2] = { -1, 0 };                 /* full frame, app interlace */
    const char *fname[2]  = { "full-frame    ", "interlaced(app)" };

    /* Deltas are computed off MIN, not AVG: at these frame times the
     * thread pool's scheduling jitter (tens of %) dwarfs the sphere's
     * sub-ms cost, and the least-contended run is the cleanest estimate
     * of the actual compute. AVG/MAX are printed for context only. */
    long nthreads = bench_host_cores();
    const char *thr_env = getenv("RT_CPU_THREADS");
    printf("R'lyeh reflective-sphere benchmark\n");
    printf("  res=%dx%d  bounce_budget=4  frames/measure=%d  host_cores=%ld  RT_CPU_THREADS=%s\n",
           W, H, N, nthreads, thr_env ? thr_env : "(default=all)");
    printf("  base scene: %d spheres, %d cones, %d cylinders, %d heightfields\n\n",
           s->sphere_count, s->cone_count, s->cylinder_count,
           s->heightfield_count);

    double base_ms[2], occ_ms[2], mir_ms[2];
    double base_mn[2], occ_mn[2], mir_mn[2], mx;

    /* --- baseline (no extra sphere) --- */
    for (int f = 0; f < 2; f++) {
        rt_renderer_set_interlace(rnd, fields[f]);
        base_ms[f] = bench_measure(rnd, s, cam, &vp, px, &gb, N, &base_mn[f], &mx);
        printf("  baseline        %s : min %6.3f ms  (avg %6.3f / max %6.3f)\n",
               fname[f], base_mn[f], base_ms[f], mx);
    }

    /* --- add the roaming sphere as an unlit occluder (reflectivity 0) --- */
    int mat = scene_add_material(s, (scene_material){
        .albedo = {40, 40, 55}, .unlit = 1,
    });
    int sidx = scene_add_sphere(s, (scene_sphere){
        .center = {0.0f, 233.0f, 500.0f}, .radius = 80.0f, .material = mat,
    });
    uint32_t want = ((uint32_t)RT_OBJ_KIND_SPHERE << 24)
                  | ((uint32_t)sidx & 0x00FFFFFFu);
    for (int f = 0; f < 2; f++) {
        rt_renderer_set_interlace(rnd, fields[f]);
        occ_ms[f] = bench_measure(rnd, s, cam, &vp, px, &gb, N, &occ_mn[f], &mx);
        printf("  +occluder(r=0)  %s : min %6.3f ms  (avg %6.3f / max %6.3f)\n",
               fname[f], occ_mn[f], occ_ms[f], mx);
    }
    /* Coverage from the last (interlaced) occluder pass would only count
     * even rows; re-render once full-frame so the count is the true
     * on-screen footprint. */
    rt_renderer_set_interlace(rnd, -1);
    rt_renderer_render(rnd, s, cam, &vp, px, &gb);
    int cover = 0;
    for (int i = 0; i < W * H; i++) if (gb.object_id[i] == want) cover++;

    /* --- flip the same sphere to a perfect mirror --- */
    s->materials[mat].unlit        = 0;
    s->materials[mat].reflectivity = 1.0f;
    for (int f = 0; f < 2; f++) {
        rt_renderer_set_interlace(rnd, fields[f]);
        mir_ms[f] = bench_measure(rnd, s, cam, &vp, px, &gb, N, &mir_mn[f], &mx);
        printf("  +mirror(r=1)    %s : min %6.3f ms  (avg %6.3f / max %6.3f)\n",
               fname[f], mir_mn[f], mir_ms[f], mx);
    }

    double cov_pct = 100.0 * (double)cover / (double)(W * H);
    printf("\n  sphere screen coverage: %d / %d px (%.2f%% of frame)\n",
           cover, W * H, cov_pct);
    printf("  --- deltas vs baseline, from MIN (full-frame) ---\n");
    printf("    occluder (+1 sphere in scan): %+6.3f ms (%+.1f%%)\n",
           occ_mn[0] - base_mn[0],
           100.0 * (occ_mn[0] - base_mn[0]) / base_mn[0]);
    printf("    mirror   (occluder + bounce): %+6.3f ms (%+.1f%%)\n",
           mir_mn[0] - base_mn[0],
           100.0 * (mir_mn[0] - base_mn[0]) / base_mn[0]);
    if (cover > 0) {
        double per_px_us = 1000.0 * (mir_mn[0] - occ_mn[0]) / (double)cover;
        printf("    reflection cost: %+6.3f ms over %d px = %.3f us/covered px\n",
               mir_mn[0] - occ_mn[0], cover, per_px_us);
        printf("    extrapolated if it filled the whole frame: %.2f ms "
               "(+%.0f%% over baseline)\n",
               per_px_us * (double)(W * H) / 1000.0,
               100.0 * (per_px_us * (double)(W * H) / 1000.0) / base_mn[0]);
    }

    free(gb.normal); free(gb.depth); free(gb.object_id); free(px);
    scene_camera_destroy(cam);
    scene_destroy(s);
    rt_renderer_destroy(rnd);
    return 0;
}
