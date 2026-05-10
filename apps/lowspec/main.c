/* lowspec: headless raytracer benchmark for "how slow can the host be
 * and still hit a target framerate" experiments. No SDL, no GL — just
 * the CPU backend, a fixed scene, a frame loop, and a PPM dump.
 *
 * The three knobs that matter:
 *   --res WxH        render resolution (default 160x120)
 *   --threads N      pin the thread pool size (default 1 — single-threaded
 *                    is the meaningful "old hardware" number)
 *   --interlace      render every other row, skipping the rest (the
 *                    skipped rows are copied from their neighbor for the
 *                    PPM dump so it doesn't look like dropouts)
 *
 * Output: ms/frame average, FPS, rays-traced/sec, and optionally a PPM
 * of the last rendered frame to --out.
 */

#include "renderer.h"
#include "viewport.h"
#include "scene.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

typedef enum { SCENE_SIMPLE, SCENE_MIRROR } scene_kind;

static double now_seconds(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

static void build_scene_simple(scene **scene_out, scene_camera **cam_out) {
    scene *s = scene_create();
    int m_red = scene_add_material(s, (scene_material){
        .albedo = {220, 60, 60}, .tex_kind = SCENE_TEX_NONE,
    });
    int m_blue = scene_add_material(s, (scene_material){
        .albedo = {60, 100, 220}, .tex_kind = SCENE_TEX_NONE,
    });
    int m_floor = scene_add_material(s, (scene_material){
        .albedo  = {180, 180, 180}, .albedo2 = {120, 120, 120},
        .tex_kind = SCENE_TEX_CHECKER, .tex_scale = 1.0f,
    });

    scene_add_sphere(s, (scene_sphere){
        .center = {-1.1f, 0.0f, 0.0f}, .radius = 0.9f, .material = m_red,
    });
    scene_add_sphere(s, (scene_sphere){
        .center = { 1.1f, 0.0f, 0.0f}, .radius = 0.9f, .material = m_blue,
    });
    scene_add_plane(s, (scene_plane){
        .normal = {0, 1, 0}, .point = {0, -1.0f, 0}, .material = m_floor,
    });

    scene_set_ambient(s, 0.25f);
    scene_add_light(s, (scene_light){
        .direction = {0.5f, 0.8f, 0.3f}, .intensity = 0.9f,
    });

    *scene_out = s;
    *cam_out = scene_camera_create((vector){0.0f, 1.0f, 4.0f},
                                   (vector){0.0f, -0.2f, -1.0f});
}

static void build_scene_mirror(scene **scene_out, scene_camera **cam_out) {
    scene *s = scene_create();
    int m_inner = scene_add_material(s, (scene_material){
        .albedo = {40, 90, 180}, .albedo2 = {230, 220, 180},
        .tex_kind = SCENE_TEX_NOISE, .tex_scale = 0.35f,
        .reflectivity = 0.2f,
    });
    int m_outer = scene_add_material(s, (scene_material){
        .albedo = {10, 10, 14}, .tex_kind = SCENE_TEX_NONE,
        .reflectivity = 1.0f,
    });
    scene_add_sphere(s, (scene_sphere){
        .center = {0, 0, 0}, .radius = 1.4f, .material = m_inner,
    });
    scene_add_sphere(s, (scene_sphere){
        .center = {0, 0, 0}, .radius = 7.0f, .material = m_outer,
    });
    scene_set_ambient(s, 0.25f);
    scene_add_light(s, (scene_light){
        .direction = {0.4f, 0.9f, 0.3f}, .intensity = 0.8f,
    });
    scene_add_light(s, (scene_light){
        .direction = {-0.6f, 0.3f, -0.5f}, .intensity = 0.4f,
    });

    *scene_out = s;
    *cam_out = scene_camera_create((vector){4.2f, 0.5f, 0.0f},
                                   (vector){-1.0f, 0.0f, 0.0f});
}

/* Duplicate every other row into its blank neighbor so the PPM dump of
 * an interlaced render reads as chunky rather than as black gaps. */
static void fill_interlace_gaps(uint32_t *pixels, int w, int h, int field) {
    for (int y = 0; y < h; y++) {
        if ((y & 1) == field) continue;
        int src = (y > 0) ? (y - 1) : (y + 1);
        if (src >= h) src = y;
        memcpy(&pixels[y * w], &pixels[src * w], (size_t)w * sizeof(uint32_t));
    }
}

static int write_ppm(const char *path, const uint32_t *pixels, int w, int h) {
    FILE *f = fopen(path, "wb");
    if (!f) {
        fprintf(stderr, "lowspec: cannot open %s for writing\n", path);
        return 1;
    }
    fprintf(f, "P6\n%d %d\n255\n", w, h);
    for (int i = 0; i < w * h; i++) {
        uint32_t p = pixels[i];
        unsigned char rgb[3] = {
            (unsigned char)((p >> 16) & 0xFF),
            (unsigned char)((p >>  8) & 0xFF),
            (unsigned char)( p        & 0xFF),
        };
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
    return 0;
}

static void usage(const char *prog) {
    fprintf(stderr,
"Usage: %s [--res WxH] [--threads N] [--frames N] [--interlace]\n"
"          [--scene simple|mirror] [--out frame.ppm] [--quiet]\n"
"\n"
"  --res WxH       render resolution (default 160x120)\n"
"  --threads N     pin CPU thread-pool size (default 1)\n"
"  --frames N      number of frames to render and time (default 60)\n"
"  --interlace     render only even rows (skipped rows duplicated for output)\n"
"  --scene KIND    'simple' (2 spheres + checker plane) or 'mirror' (orb)\n"
"  --out PATH      write the last frame as a PPM to PATH\n"
"  --quiet         suppress the per-frame trace\n"
"\n"
"Reports total time, ms/frame average, FPS, and rays/sec\n"
"(rays = pixels actually rendered, not counting reflection bounces).\n",
        prog);
}

int main(int argc, char *argv[]) {
    int width = 160, height = 120;
    int threads = 1;
    int frames = 60;
    int interlace = 0;
    int quiet = 0;
    scene_kind kind = SCENE_SIMPLE;
    const char *out_path = NULL;

    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (!strcmp(a, "-h") || !strcmp(a, "--help")) {
            usage(argv[0]); return 0;
        } else if (!strcmp(a, "--res") && i + 1 < argc) {
            if (sscanf(argv[++i], "%dx%d", &width, &height) != 2 ||
                width < 1 || height < 1) {
                fprintf(stderr, "lowspec: bad --res value\n");
                return 1;
            }
        } else if (!strcmp(a, "--threads") && i + 1 < argc) {
            threads = atoi(argv[++i]);
            if (threads < 1) threads = 1;
        } else if (!strcmp(a, "--frames") && i + 1 < argc) {
            frames = atoi(argv[++i]);
            if (frames < 1) frames = 1;
        } else if (!strcmp(a, "--interlace")) {
            interlace = 1;
        } else if (!strcmp(a, "--scene") && i + 1 < argc) {
            const char *v = argv[++i];
            if      (!strcmp(v, "simple")) kind = SCENE_SIMPLE;
            else if (!strcmp(v, "mirror")) kind = SCENE_MIRROR;
            else { fprintf(stderr, "lowspec: unknown --scene %s\n", v); return 1; }
        } else if (!strcmp(a, "--out") && i + 1 < argc) {
            out_path = argv[++i];
        } else if (!strcmp(a, "--quiet")) {
            quiet = 1;
        } else {
            fprintf(stderr, "lowspec: unknown arg %s\n", a);
            usage(argv[0]);
            return 1;
        }
    }

    /* The CPU backend reads these at create time. setenv before
     * rt_renderer_create — the public renderer API has no thread/
     * interlace knob, and we want to keep it that way. */
    char buf[32];
    snprintf(buf, sizeof(buf), "%d", threads);
    setenv("RT_CPU_THREADS", buf, 1);
    setenv("RT_CPU_INTERLACE", interlace ? "0" : "-1", 1);

    if (!rt_renderer_available(RT_BACKEND_CPU)) {
        fprintf(stderr, "lowspec: CPU backend not available\n");
        return 1;
    }
    rt_renderer *r = rt_renderer_create(RT_BACKEND_CPU);
    if (!r) {
        fprintf(stderr, "lowspec: rt_renderer_create failed\n");
        return 1;
    }

    scene *scn;
    scene_camera *cam;
    if (kind == SCENE_MIRROR) build_scene_mirror(&scn, &cam);
    else                      build_scene_simple(&scn, &cam);

    rt_viewport vp = { width, height, (float)(M_PI / 2.8) };
    uint32_t *pixels = calloc((size_t)width * (size_t)height, sizeof(uint32_t));
    if (!pixels) {
        fprintf(stderr, "lowspec: out of memory for %dx%d framebuffer\n",
                width, height);
        return 1;
    }

    if (!quiet) {
        fprintf(stderr,
            "lowspec: %dx%d, %d thread%s, %s, scene=%s, frames=%d\n",
            width, height, threads, threads == 1 ? "" : "s",
            interlace ? "interlaced (even rows)" : "full",
            kind == SCENE_MIRROR ? "mirror" : "simple",
            frames);
    }

    double total = 0.0;
    double worst = 0.0;
    double best  = 1e9;
    for (int f = 0; f < frames; f++) {
        double t0 = now_seconds();
        rt_renderer_render(r, scn, cam, &vp, pixels, NULL);
        double dt = now_seconds() - t0;
        total += dt;
        if (dt > worst) worst = dt;
        if (dt < best)  best  = dt;
        if (!quiet) {
            fprintf(stderr, "  frame %3d  %7.2f ms\n", f, dt * 1000.0);
        }
    }

    /* Pixels actually traced per frame. Interlace halves the count, with
     * a tiny rounding wrinkle when height is odd. */
    long rendered_per_frame = (long)width *
        (interlace ? ((long)height + 1) / 2 : (long)height);
    double avg = total / (double)frames;
    double rays_per_sec = (double)rendered_per_frame / avg;

    printf("frames        %d\n", frames);
    printf("total_time    %.3f s\n", total);
    printf("avg_frame     %.3f ms\n", avg * 1000.0);
    printf("best_frame    %.3f ms\n", best * 1000.0);
    printf("worst_frame   %.3f ms\n", worst * 1000.0);
    printf("fps_avg       %.2f\n", 1.0 / avg);
    printf("rays_per_sec  %.0f\n", rays_per_sec);
    printf("rays_per_frame %ld\n", rendered_per_frame);

    if (out_path) {
        if (interlace) fill_interlace_gaps(pixels, width, height, 0);
        if (write_ppm(out_path, pixels, width, height) != 0) {
            fprintf(stderr, "lowspec: failed to write %s\n", out_path);
        } else if (!quiet) {
            fprintf(stderr, "lowspec: wrote %s (%dx%d PPM)\n",
                    out_path, width, height);
        }
    }

    free(pixels);
    scene_camera_destroy(cam);
    scene_destroy(scn);
    rt_renderer_destroy(r);
    return 0;
}
