/* K-Means: Lloyd's algorithm animated in 3D on the raytracer.
 *
 * A cloud of points floats over a mirror-checker floor. By default it is
 * a random number of wide, overlapping blobs buried in uniform noise (the
 * hidden structure is independent of k); D switches to pure uniform noise
 * or to k clean, separated blobs. The demo then runs k-means one
 * half-step at a time so each phase is visible:
 *
 *   ASSIGN  every point recolors to its nearest centroid (colors fade),
 *   UPDATE  every centroid glides to the mean of its members.
 *
 * Thin cylinders link each point to its centroid so the Voronoi
 * partition reads at a glance. When an ASSIGN pass changes nothing the
 * run has converged; after a short hold a fresh dataset is drawn.
 *
 * Controls
 *   SPACE pause / resume      N  single half-step (while paused)
 *   R     new dataset         J / K  fewer / more clusters (2..8)
 *   D     data: mixed (overlapping blobs + noise) / uniform / clean blobs
 *   I     init: k-means++ / random   L  toggle link lines
 *   arrows orbit manually     wheel  zoom      O  toggle auto-orbit
 *   TAB   CPU / OpenGL        - / =  render scale    F11 fullscreen
 *
 * Headless: --shot out.ppm renders one converged frame on the CPU
 * backend without opening a window (--seed N, --k N, --res WxH). */

#include "renderer.h"
#include "viewport.h"
#include "scene.h"
#include <SDL2/SDL.h>

#define GL_GLEXT_PROTOTYPES 1
#include "gl_compat.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>

#define INIT_WINDOW_W 960
#define INIT_WINDOW_H 600
#define FOV (M_PI / 3.2f)
#define RENDER_SCALE_MIN 1
#define RENDER_SCALE_MAX 4

#define K_MIN 2
#define K_MAX 8
#define K_DEFAULT 4
#define POINTS_PER_BLOB 40
#define MAX_POINTS (K_MAX * POINTS_PER_BLOB)
#define DATA_POINTS 220          /* target count for mixed / uniform data */
#define DATA_HALF_XZ 3.8f
#define DATA_Y_MIN  -1.6f
#define DATA_Y_MAX   2.2f

typedef enum { DATA_MIXED, DATA_UNIFORM, DATA_BLOBS, DATA_MODE_COUNT } data_mode;

#define POINT_RADIUS     0.11f
#define CENTROID_RADIUS  0.30f
#define LINK_RADIUS      0.018f
#define FLOOR_Y         -2.2f

#define STEP_INTERVAL    0.85f   /* seconds between half-steps */
#define CONVERGED_HOLD   3.0f    /* seconds to admire the result */
#define MAX_ITERATIONS   40

#define ORBIT_SPEED      0.18f

/* ------------------------------------------------------------------ RNG */

static uint64_t rng_state = 0x9E3779B97F4A7C15ull;

static uint32_t rng_u32(void) {
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 7;
    rng_state ^= rng_state << 17;
    return (uint32_t)(rng_state >> 32);
}

static float rng_unit(void) { return (rng_u32() >> 8) * (1.0f / 16777216.0f); }
static float rng_range(float a, float b) { return a + (b - a) * rng_unit(); }

static float rng_gauss(void) {
    float u1 = rng_unit();
    float u2 = rng_unit();
    if (u1 < 1e-7f) u1 = 1e-7f;
    return sqrtf(-2.0f * logf(u1)) * cosf(2.0f * (float)M_PI * u2);
}

/* -------------------------------------------------------------- Palette */

static const scene_color PALETTE[K_MAX] = {
    {255,  80,  90},   /* coral   */
    { 70, 190, 255},   /* sky     */
    {120, 230,  90},   /* lime    */
    {255, 200,  60},   /* amber   */
    {200, 110, 255},   /* violet  */
    { 60, 230, 200},   /* teal    */
    {255, 130, 200},   /* pink    */
    {240, 240, 240},   /* white   */
};
static const float UNASSIGNED_RGB[3] = {110.0f, 110.0f, 120.0f};

/* ------------------------------------------------------------ K-means */

typedef enum { PHASE_ASSIGN, PHASE_UPDATE, PHASE_CONVERGED } km_phase;

typedef struct {
    int      n, k;
    vector   pts[MAX_POINTS];
    int      assign[MAX_POINTS];       /* -1 = not yet assigned */
    float    pt_rgb[MAX_POINTS][3];    /* displayed (smoothed) color */
    vector   cent[K_MAX];              /* algorithmic centroid */
    vector   cent_disp[K_MAX];         /* displayed (smoothed) centroid */
    int      iteration;
    km_phase phase;
    float    inertia;                  /* sum of squared distances */
    int      changed;                  /* points moved in last ASSIGN */
    int      use_pp;                   /* k-means++ init vs random */
    data_mode data_mode;               /* how the dataset is sampled */
} kmeans;

static float dist2(vector a, vector b) {
    float dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
    return dx * dx + dy * dy + dz * dz;
}

static const char *DATA_MODE_NAME[DATA_MODE_COUNT] = {"mixed", "uniform", "blobs"};

static vector uniform_point(void) {
    return (vector){rng_range(-DATA_HALF_XZ, DATA_HALF_XZ),
                    rng_range(DATA_Y_MIN, DATA_Y_MAX),
                    rng_range(-DATA_HALF_XZ, DATA_HALF_XZ)};
}

static void add_point(kmeans *km, vector p) {
    if (km->n >= MAX_POINTS) return;
    float lo = FLOOR_Y + POINT_RADIUS + 0.05f;
    if (p.y < lo) p.y = lo;
    km->pts[km->n++] = p;
}

/* Gaussian blobs, `blobs` of them, `per_blob` points each on average.
 * `min_sep` keeps centers apart (0 = anywhere), sigma in [s_lo, s_hi]. */
static void add_blobs(kmeans *km, int blobs, int per_blob, float min_sep,
                      float s_lo, float s_hi) {
    vector centers[K_MAX];
    for (int b = 0; b < blobs; b++) {
        for (int tries = 0; tries < 200; tries++) {
            centers[b] = (vector){rng_range(-3.0f, 3.0f),
                                  rng_range(-0.8f, 1.6f),
                                  rng_range(-3.0f, 3.0f)};
            int ok = 1;
            for (int j = 0; j < b; j++)
                if (dist2(centers[b], centers[j]) < min_sep * min_sep) { ok = 0; break; }
            if (ok) break;
        }
    }
    for (int b = 0; b < blobs; b++) {
        /* Anisotropic sigma: blobs are stretched ellipsoids, not balls. */
        float sx = rng_range(s_lo, s_hi);
        float sy = rng_range(s_lo, s_hi) * 0.7f;
        float sz = rng_range(s_lo, s_hi);
        int count = per_blob / 2 + (int)(rng_u32() % (uint32_t)per_blob);
        for (int i = 0; i < count; i++)
            add_point(km, (vector){centers[b].x + rng_gauss() * sx,
                                   centers[b].y + rng_gauss() * sy,
                                   centers[b].z + rng_gauss() * sz});
    }
}

static void km_generate(kmeans *km) {
    km->n = 0;
    switch (km->data_mode) {
    case DATA_MIXED: {
        /* The hidden structure is NOT tied to k: a random number of wide,
         * overlapping, unevenly sized blobs, buried in uniform noise. */
        int blobs = 2 + (int)(rng_u32() % 5);           /* 2..6 */
        add_blobs(km, blobs, 26, 1.2f, 0.55f, 1.25f);
        int noise = DATA_POINTS - km->n;
        if (noise < DATA_POINTS / 3) noise = DATA_POINTS / 3;
        for (int i = 0; i < noise; i++) add_point(km, uniform_point());
        break;
    }
    case DATA_UNIFORM:
        /* No structure at all: k-means still partitions the box into k
         * Voronoi cells, which is a good reminder that it always finds
         * "clusters" whether or not any exist. */
        for (int i = 0; i < DATA_POINTS; i++) add_point(km, uniform_point());
        break;
    case DATA_BLOBS:
    default:
        /* Textbook case: k tight, well-separated blobs. */
        add_blobs(km, km->k, POINTS_PER_BLOB, 2.8f, 0.35f, 0.75f);
        break;
    }
}

static void km_init_centroids(kmeans *km) {
    int n = km->n;
    if (km->use_pp) {
        /* k-means++: first center uniform, the rest D^2-weighted. */
        static float d2[MAX_POINTS];
        km->cent[0] = km->pts[rng_u32() % (uint32_t)n];
        for (int c = 1; c < km->k; c++) {
            float total = 0.0f;
            for (int i = 0; i < n; i++) {
                float best = INFINITY;
                for (int j = 0; j < c; j++) {
                    float d = dist2(km->pts[i], km->cent[j]);
                    if (d < best) best = d;
                }
                d2[i] = best;
                total += best;
            }
            float r = rng_unit() * total;
            int pick = n - 1;
            for (int i = 0; i < n; i++) {
                r -= d2[i];
                if (r <= 0.0f) { pick = i; break; }
            }
            km->cent[c] = km->pts[pick];
        }
    } else {
        /* Forgy: k distinct random points. */
        int chosen[K_MAX];
        for (int c = 0; c < km->k; c++) {
            int idx, dup;
            do {
                idx = (int)(rng_u32() % (uint32_t)n);
                dup = 0;
                for (int j = 0; j < c; j++) if (chosen[j] == idx) dup = 1;
            } while (dup);
            chosen[c] = idx;
            km->cent[c] = km->pts[idx];
        }
    }
    /* Centroids "drop in" from above so the initial placement is visible. */
    for (int c = 0; c < km->k; c++) {
        km->cent_disp[c] = km->cent[c];
        km->cent_disp[c].y += 3.0f;
    }
}

static void km_reset_run(kmeans *km) {
    for (int i = 0; i < km->n; i++) {
        km->assign[i] = -1;
        memcpy(km->pt_rgb[i], UNASSIGNED_RGB, sizeof UNASSIGNED_RGB);
    }
    km->iteration = 0;
    km->phase     = PHASE_ASSIGN;
    km->inertia   = 0.0f;
    km->changed   = 0;
    km_init_centroids(km);
}

static void km_new_dataset(kmeans *km) {
    km_generate(km);
    km_reset_run(km);
}

static void km_assign(kmeans *km) {
    km->changed = 0;
    km->inertia = 0.0f;
    for (int i = 0; i < km->n; i++) {
        int best = 0;
        float bd = INFINITY;
        for (int c = 0; c < km->k; c++) {
            float d = dist2(km->pts[i], km->cent[c]);
            if (d < bd) { bd = d; best = c; }
        }
        if (km->assign[i] != best) km->changed++;
        km->assign[i] = best;
        km->inertia += bd;
    }
}

static void km_update(kmeans *km) {
    vector sum[K_MAX] = {{0}};
    int count[K_MAX] = {0};
    for (int i = 0; i < km->n; i++) {
        int c = km->assign[i];
        sum[c] = vector_add(sum[c], km->pts[i]);
        count[c]++;
    }
    for (int c = 0; c < km->k; c++) {
        if (count[c] > 0) {
            km->cent[c] = vector_scale(sum[c], 1.0f / (float)count[c]);
        } else {
            /* Empty cluster: re-seed on the point farthest from its
             * centroid, the usual fix-up. */
            /* Not named `far`: <windows.h> (via SDL on MinGW) #defines
             * far/near as empty macros. */
            int farthest = 0;
            float fd = -1.0f;
            for (int i = 0; i < km->n; i++) {
                float d = dist2(km->pts[i], km->cent[km->assign[i]]);
                if (d > fd) { fd = d; farthest = i; }
            }
            km->cent[c] = km->pts[farthest];
        }
    }
    km->iteration++;
}

/* Advance one visible half-step. Returns 1 if the run just converged. */
static int km_step(kmeans *km) {
    switch (km->phase) {
    case PHASE_ASSIGN:
        km_assign(km);
        if ((km->changed == 0 && km->iteration > 0) ||
            km->iteration >= MAX_ITERATIONS) {
            km->phase = PHASE_CONVERGED;
            return 1;
        }
        km->phase = PHASE_UPDATE;
        return 0;
    case PHASE_UPDATE:
        km_update(km);
        km->phase = PHASE_ASSIGN;
        return 0;
    case PHASE_CONVERGED:
        return 0;
    }
    return 0;
}

static void km_animate(kmeans *km, float dt) {
    float a_col  = 1.0f - expf(-dt * 7.0f);
    float a_cent = 1.0f - expf(-dt * 4.5f);
    for (int i = 0; i < km->n; i++) {
        const float *tgt = UNASSIGNED_RGB;
        float pal[3];
        if (km->assign[i] >= 0) {
            scene_color c = PALETTE[km->assign[i]];
            pal[0] = c.r; pal[1] = c.g; pal[2] = c.b;
            tgt = pal;
        }
        for (int ch = 0; ch < 3; ch++)
            km->pt_rgb[i][ch] += (tgt[ch] - km->pt_rgb[i][ch]) * a_col;
    }
    for (int c = 0; c < km->k; c++) {
        vector d = vector_sub(km->cent[c], km->cent_disp[c]);
        km->cent_disp[c] = vector_add(km->cent_disp[c], vector_scale(d, a_cent));
    }
}

static void km_snap(kmeans *km) {
    km_animate(km, 100.0f);
}

/* -------------------------------------------------------------- Scene */

static scene_color scale_color(scene_color c, float s) {
    float r = c.r * s, g = c.g * s, b = c.b * s;
    return (scene_color){(uint8_t)(r > 255 ? 255 : r),
                         (uint8_t)(g > 255 ? 255 : g),
                         (uint8_t)(b > 255 ? 255 : b)};
}

static void build_scene(scene *s, const kmeans *km, int show_links, float t) {
    scene_clear(s);
    scene_set_background(s, (scene_color){8, 10, 22}, -1);
    scene_set_ambient(s, 0.28f);
    scene_add_light(s, (scene_light){.direction = {0.35f, 0.9f, 0.25f}, .intensity = 0.75f});
    scene_add_light(s, (scene_light){.direction = {-0.6f, 0.4f, -0.5f}, .intensity = 0.30f});

    scene_material floor = scene_material_default();
    floor.albedo       = (scene_color){28, 30, 44};
    floor.albedo2      = (scene_color){14, 15, 24};
    floor.tex_kind     = SCENE_TEX_CHECKER;
    floor.tex_scale    = 1.0f;
    floor.reflectivity = 0.35f;
    int m_floor = scene_add_material(s, floor);
    scene_add_plane(s, (scene_plane){.normal = {0, 1, 0},
                                     .point  = {0, FLOOR_Y, 0},
                                     .material = m_floor});

    /* Per-cluster materials: links (unlit, dim), centroid core (mirror-ish),
     * halo ring (unlit, bright). */
    int m_link[K_MAX], m_core[K_MAX], m_halo[K_MAX];
    for (int c = 0; c < km->k; c++) {
        scene_material link = scene_material_default();
        link.albedo = scale_color(PALETTE[c], 0.55f);
        link.unlit  = 1;
        m_link[c] = scene_add_material(s, link);

        scene_material core = scene_material_default();
        core.albedo       = scale_color(PALETTE[c], 0.9f);
        core.reflectivity = 0.55f;
        m_core[c] = scene_add_material(s, core);

        scene_material halo = scene_material_default();
        halo.albedo = scale_color(PALETTE[c], 1.15f);
        halo.unlit  = 1;
        m_halo[c] = scene_add_material(s, halo);
    }

    for (int i = 0; i < km->n; i++) {
        scene_material pm = scene_material_default();
        pm.albedo = (scene_color){(uint8_t)km->pt_rgb[i][0],
                                  (uint8_t)km->pt_rgb[i][1],
                                  (uint8_t)km->pt_rgb[i][2]};
        pm.reflectivity = 0.12f;
        int m = scene_add_material(s, pm);
        scene_add_sphere(s, (scene_sphere){.center = km->pts[i],
                                           .radius = POINT_RADIUS,
                                           .material = m});

        int c = km->assign[i];
        if (show_links && c >= 0) {
            vector a = km->pts[i], b = km->cent_disp[c];
            vector d = vector_sub(b, a);
            float len = vector_magnitude(d);
            float gap = POINT_RADIUS + CENTROID_RADIUS;
            if (len > gap + 0.05f) {
                vector axis = vector_scale(d, 1.0f / len);
                /* Span only the gap between the two sphere surfaces. */
                vector p0 = vector_add(a, vector_scale(axis, POINT_RADIUS));
                vector p1 = vector_sub(b, vector_scale(axis, CENTROID_RADIUS));
                scene_add_cylinder(s, (scene_cylinder){
                    .center      = vector_scale(vector_add(p0, p1), 0.5f),
                    .axis        = axis,
                    .radius      = LINK_RADIUS,
                    .half_height = 0.5f * (len - gap),
                    .material    = m_link[c],
                });
            }
        }
    }

    for (int c = 0; c < km->k; c++) {
        vector p = km->cent_disp[c];
        scene_add_sphere(s, (scene_sphere){.center = p,
                                           .radius = CENTROID_RADIUS,
                                           .material = m_core[c]});
        float a = t * 1.3f + (float)c * 0.9f;
        vector axis = vector_normalize((vector){sinf(a), 1.4f, cosf(a)});
        scene_add_torus(s, (scene_torus){.center = p,
                                         .axis = axis,
                                         .major_radius = CENTROID_RADIUS + 0.17f,
                                         .minor_radius = 0.035f,
                                         .material = m_halo[c]});
    }
}

/* ------------------------------------------------------------- Camera */

typedef struct {
    float yaw, pitch, dist;
} orbit_cam;

static void orbit_place(scene_camera *cam, const orbit_cam *o) {
    vector target = {0.0f, -0.2f, 0.0f};
    vector pos = {target.x + o->dist * cosf(o->pitch) * sinf(o->yaw),
                  target.y + o->dist * sinf(o->pitch),
                  target.z + o->dist * cosf(o->pitch) * cosf(o->yaw)};
    scene_camera_place(cam, pos, vector_normalize(vector_sub(target, pos)));
}

/* ----------------------------------------------------------- Headless */

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

static int run_shot(kmeans *km, const char *path, int w, int h, int max_steps) {
    rt_renderer *rnd = rt_renderer_create(RT_BACKEND_CPU);
    if (!rnd) { fprintf(stderr, "CPU renderer unavailable\n"); return 1; }

    int steps = 0;
    while (km->phase != PHASE_CONVERGED && (max_steps < 0 || steps < max_steps)) {
        km_step(km);
        steps++;
    }
    km_snap(km);

    scene *s = scene_create();
    scene_camera *cam = scene_camera_create((vector){0, 0, 10}, (vector){0, 0, -1});
    orbit_cam o = {0.6f, 0.50f, 9.0f};
    orbit_place(cam, &o);
    build_scene(s, km, 1, 0.0f);

    uint32_t *px = calloc((size_t)w * (size_t)h, sizeof *px);
    rt_viewport vp = {w, h, FOV};
    rt_renderer_render(rnd, s, cam, &vp, px, NULL);
    int rc = write_ppm(path, px, w, h);
    fprintf(stderr, "k=%d n=%d iter=%d inertia=%.2f %s -> %s\n",
            km->k, km->n, km->iteration, km->inertia,
            km->phase == PHASE_CONVERGED ? "converged" : "in progress", path);

    free(px);
    scene_camera_destroy(cam);
    scene_destroy(s);
    rt_renderer_destroy(rnd);
    return rc == 0 ? 0 : 1;
}

/* -------------------------------------------------------------- Display */

static void display_pixels(GLuint tex, GLuint fbo, const uint32_t *pixels,
                           int render_w, int render_h,
                           int window_w, int window_h) {
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, render_w, render_h,
                    GL_BGRA, GL_UNSIGNED_BYTE, pixels);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_2D, tex, 0);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    glBlitFramebuffer(0, 0, render_w, render_h,
                      0, window_h, window_w, 0,
                      GL_COLOR_BUFFER_BIT, GL_NEAREST);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
}

static void usage(const char *argv0) {
    printf("Usage: %s [options]\n", argv0);
    printf("  -G, --gpu          Start on the OpenGL backend (falls back to CPU)\n");
    printf("  --k N              Number of clusters (%d..%d, default %d)\n", K_MIN, K_MAX, K_DEFAULT);
    printf("  --seed N           RNG seed for the dataset\n");
    printf("  --data MODE        mixed (default: overlapping blobs + noise),\n"
           "                     uniform (no structure), blobs (k clean blobs)\n");
    printf("  --random-init      Use random (Forgy) init instead of k-means++\n");
    printf("  --shot out.ppm     Render one converged frame headless and exit\n");
    printf("  --steps N          With --shot: stop after N half-steps instead\n");
    printf("  --res WxH          With --shot: output resolution (default 960x600)\n");
    printf("\nSPACE pause, N step, R new data, D data mode, J/K clusters, I init, L links,\n");
    printf("arrows/wheel orbit+zoom, O auto-orbit, TAB backend, -/= scale, F11, ESC.\n");
}

int main(int argc, char *argv[]) {
    rt_backend preferred = RT_BACKEND_CPU;
    const char *shot_path = NULL;
    int shot_w = INIT_WINDOW_W, shot_h = INIT_WINDOW_H, shot_steps = -1;
    uint64_t seed = 0;
    int have_seed = 0;

    static kmeans km;
    km.k = K_DEFAULT;
    km.use_pp = 1;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-G") || !strcmp(argv[i], "--gpu")) {
            preferred = RT_BACKEND_OPENGL;
        } else if (!strcmp(argv[i], "--k") && i + 1 < argc) {
            km.k = atoi(argv[++i]);
            if (km.k < K_MIN) km.k = K_MIN;
            if (km.k > K_MAX) km.k = K_MAX;
        } else if (!strcmp(argv[i], "--seed") && i + 1 < argc) {
            seed = strtoull(argv[++i], NULL, 10);
            have_seed = 1;
        } else if (!strcmp(argv[i], "--data") && i + 1 < argc) {
            const char *m = argv[++i];
            int found = 0;
            for (int d = 0; d < DATA_MODE_COUNT; d++)
                if (!strcmp(m, DATA_MODE_NAME[d])) { km.data_mode = d; found = 1; }
            if (!found) {
                fprintf(stderr, "Bad --data, expected mixed|uniform|blobs\n");
                return 1;
            }
        } else if (!strcmp(argv[i], "--random-init")) {
            km.use_pp = 0;
        } else if (!strcmp(argv[i], "--shot") && i + 1 < argc) {
            shot_path = argv[++i];
        } else if (!strcmp(argv[i], "--steps") && i + 1 < argc) {
            shot_steps = atoi(argv[++i]);
        } else if (!strcmp(argv[i], "--res") && i + 1 < argc) {
            if (sscanf(argv[++i], "%dx%d", &shot_w, &shot_h) != 2 ||
                shot_w <= 0 || shot_h <= 0) {
                fprintf(stderr, "Bad --res, expected WxH\n");
                return 1;
            }
        } else if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
            usage(argv[0]);
            return 0;
        } else {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            usage(argv[0]);
            return 1;
        }
    }

    if (!have_seed) seed = (uint64_t)time(NULL);
    rng_state ^= seed * 0x2545F4914F6CDD1Dull;
    if (rng_state == 0) rng_state = 1;
    km_new_dataset(&km);

    if (shot_path) return run_shot(&km, shot_path, shot_w, shot_h, shot_steps);

    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        fprintf(stderr, "SDL init failed: %s\n", SDL_GetError());
        return 1;
    }
    int window_w = INIT_WINDOW_W, window_h = INIT_WINDOW_H;
    int fullscreen = 0;
    SDL_Window *window = NULL;
    SDL_GLContext gl_ctx = NULL;
    /* GL 4.3 for the compute backend; fall back to 4.1 core (macOS max)
     * or 3.3, which is plenty for the CPU backend's FBO blit. */
    static const int GL_VERSIONS[][2] = { {4, 3}, {4, 1}, {3, 3} };
    for (int v = 0; v < 3 && !gl_ctx; v++) {
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, GL_VERSIONS[v][0]);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, GL_VERSIONS[v][1]);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
        SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
        if (window) SDL_DestroyWindow(window);
        window = SDL_CreateWindow("K-Means",
            SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
            window_w, window_h, SDL_WINDOW_OPENGL);
        if (!window) break;
        gl_ctx = SDL_GL_CreateContext(window);
    }
    if (!window || !gl_ctx) {
        fprintf(stderr, "Window/GL context: %s\n", SDL_GetError());
        if (window) SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    SDL_GL_SetSwapInterval(1);
    gl_compat_init((gl_compat_loader_fn)SDL_GL_GetProcAddress);

    rt_renderer *cpu_rnd = rt_renderer_available(RT_BACKEND_CPU)
                         ? rt_renderer_create(RT_BACKEND_CPU) : NULL;
    rt_renderer *gpu_rnd = rt_renderer_available(RT_BACKEND_OPENGL)
                         ? rt_renderer_create(RT_BACKEND_OPENGL) : NULL;
    if (!cpu_rnd && !gpu_rnd) {
        fprintf(stderr, "No renderers available\n");
        SDL_GL_DeleteContext(gl_ctx);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    rt_renderer *active = (preferred == RT_BACKEND_OPENGL && gpu_rnd) ? gpu_rnd
                        : (cpu_rnd ? cpu_rnd : gpu_rnd);
    fprintf(stderr, "Active: %s\n", rt_renderer_name(active));

    scene *s = scene_create();
    scene_camera *camera = scene_camera_create((vector){0, 0, 10}, (vector){0, 0, -1});

    /* Start at half resolution: a few hundred spheres + cylinders is a
     * real workload for the CPU tracer at full window size. */
    int render_scale = 2;
    int render_w = window_w / render_scale;
    int render_h = window_h / render_scale;
    uint32_t *pixels = calloc((size_t)render_w * (size_t)render_h, sizeof(uint32_t));
    rt_viewport viewport = {render_w, render_h, FOV};

    GLuint display_tex, display_fbo;
    glGenTextures(1, &display_tex);
    glBindTexture(GL_TEXTURE_2D, display_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, render_w, render_h, 0,
                 GL_BGRA, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glGenFramebuffers(1, &display_fbo);

    orbit_cam orbit = {0.6f, 0.50f, 9.0f};
    int auto_orbit = 1, paused = 0, show_links = 1, running = 1;
    float step_timer = 0.0f, hold_timer = 0.0f;

    Uint32 start_ticks = SDL_GetTicks();
    Uint32 frame_last = start_ticks, fps_last = start_ticks;
    int fps_frames = 0;
    Uint32 render_ms_accum = 0;
    char title[200];

    while (running) {
        Uint32 now_ticks = SDL_GetTicks();
        float dt = (now_ticks - frame_last) / 1000.0f;
        if (dt > 0.1f) dt = 0.1f;
        frame_last = now_ticks;
        float t = (now_ticks - start_ticks) / 1000.0f;
        int need_resize = 0;

        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) running = 0;
            if (e.type == SDL_MOUSEWHEEL) {
                orbit.dist -= (float)e.wheel.y * 0.6f;
                if (orbit.dist < 5.0f)  orbit.dist = 5.0f;
                if (orbit.dist > 25.0f) orbit.dist = 25.0f;
            }
            if (e.type != SDL_KEYDOWN) continue;
            switch (e.key.keysym.sym) {
            case SDLK_ESCAPE: running = 0; break;
            case SDLK_TAB:
                if (active == cpu_rnd && gpu_rnd) active = gpu_rnd;
                else if (active == gpu_rnd && cpu_rnd) active = cpu_rnd;
                fprintf(stderr, "Active: %s\n", rt_renderer_name(active));
                break;
            case SDLK_SPACE: paused = !paused; break;
            case SDLK_n:
                if (paused) km_step(&km);
                break;
            case SDLK_r:
                km_new_dataset(&km);
                step_timer = hold_timer = 0.0f;
                break;
            case SDLK_j: case SDLK_k: {
                int nk = km.k + (e.key.keysym.sym == SDLK_k ? 1 : -1);
                if (nk >= K_MIN && nk <= K_MAX) {
                    km.k = nk;
                    km_reset_run(&km);   /* same data, new k: see over/under-fit */
                    step_timer = hold_timer = 0.0f;
                }
                fprintf(stderr, "k = %d\n", km.k);
                break;
            }
            case SDLK_i:
                km.use_pp = !km.use_pp;
                km_reset_run(&km);
                step_timer = hold_timer = 0.0f;
                fprintf(stderr, "Init: %s\n", km.use_pp ? "k-means++" : "random");
                break;
            case SDLK_d:
                km.data_mode = (km.data_mode + 1) % DATA_MODE_COUNT;
                km_new_dataset(&km);
                step_timer = hold_timer = 0.0f;
                fprintf(stderr, "Data: %s\n", DATA_MODE_NAME[km.data_mode]);
                break;
            case SDLK_l: show_links = !show_links; break;
            case SDLK_o: auto_orbit = !auto_orbit; break;
            case SDLK_F11:
                fullscreen = !fullscreen;
                SDL_SetWindowFullscreen(window, fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
                SDL_GetWindowSize(window, &window_w, &window_h);
                need_resize = 1;
                break;
            case SDLK_MINUS: case SDLK_KP_MINUS:
                if (render_scale < RENDER_SCALE_MAX) { render_scale++; need_resize = 1; }
                break;
            case SDLK_EQUALS: case SDLK_KP_PLUS:
                if (render_scale > RENDER_SCALE_MIN) { render_scale--; need_resize = 1; }
                break;
            default: break;
            }
        }

        if (need_resize) {
            render_w = window_w / render_scale;
            render_h = window_h / render_scale;
            free(pixels);
            pixels = calloc((size_t)render_w * (size_t)render_h, sizeof(uint32_t));
            viewport = (rt_viewport){render_w, render_h, FOV};
            glBindTexture(GL_TEXTURE_2D, display_tex);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, render_w, render_h, 0,
                         GL_BGRA, GL_UNSIGNED_BYTE, NULL);
            fprintf(stderr, "Render scale: 1/%d (%dx%d)\n", render_scale, render_w, render_h);
        }

        /* Algorithm clock. */
        if (!paused) {
            if (km.phase == PHASE_CONVERGED) {
                hold_timer += dt;
                if (hold_timer >= CONVERGED_HOLD) {
                    km_new_dataset(&km);
                    hold_timer = step_timer = 0.0f;
                }
            } else {
                step_timer += dt;
                if (step_timer >= STEP_INTERVAL) {
                    step_timer -= STEP_INTERVAL;
                    km_step(&km);
                }
            }
        }
        km_animate(&km, dt);

        /* Camera. */
        const Uint8 *keys = SDL_GetKeyboardState(NULL);
        if (auto_orbit) orbit.yaw += ORBIT_SPEED * dt;
        if (keys[SDL_SCANCODE_LEFT])  orbit.yaw   -= 1.4f * dt;
        if (keys[SDL_SCANCODE_RIGHT]) orbit.yaw   += 1.4f * dt;
        if (keys[SDL_SCANCODE_UP])    orbit.pitch += 1.0f * dt;
        if (keys[SDL_SCANCODE_DOWN])  orbit.pitch -= 1.0f * dt;
        if (orbit.pitch > 1.35f) orbit.pitch = 1.35f;
        if (orbit.pitch < 0.05f) orbit.pitch = 0.05f;
        orbit_place(camera, &orbit);

        build_scene(s, &km, show_links, t);

        Uint32 r_start = SDL_GetTicks();
        rt_renderer_render(active, s, camera, &viewport, pixels, NULL);
        render_ms_accum += SDL_GetTicks() - r_start;

        display_pixels(display_tex, display_fbo, pixels,
                       render_w, render_h, window_w, window_h);
        SDL_GL_SwapWindow(window);

        fps_frames++;
        if (now_ticks - fps_last >= 1000) {
            static const char *phase_name[] = {"assign", "update", "converged"};
            snprintf(title, sizeof title,
                     "K-Means  k=%d  n=%d  %s data  iter %d  [%s]  SSE %.1f  %s%s | %s %d FPS (%.1f ms, %dx%d)",
                     km.k, km.n, DATA_MODE_NAME[km.data_mode], km.iteration, phase_name[km.phase], km.inertia,
                     km.use_pp ? "k-means++" : "random",
                     paused ? "  PAUSED" : "",
                     rt_renderer_name(active), fps_frames,
                     fps_frames ? (float)render_ms_accum / fps_frames : 0.0f,
                     render_w, render_h);
            SDL_SetWindowTitle(window, title);
            fps_frames = 0;
            render_ms_accum = 0;
            fps_last = now_ticks;
        }
    }

    glDeleteFramebuffers(1, &display_fbo);
    glDeleteTextures(1, &display_tex);
    if (cpu_rnd) rt_renderer_destroy(cpu_rnd);
    if (gpu_rnd) rt_renderer_destroy(gpu_rnd);
    free(pixels);
    scene_camera_destroy(camera);
    scene_destroy(s);
    SDL_GL_DeleteContext(gl_ctx);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
