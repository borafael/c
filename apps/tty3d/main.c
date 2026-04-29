/* tty3d — orbit camera around a textured orb inside a mirror sphere,
 * rendered into the terminal. CPU raytracer, no SDL, no X. The point
 * is "ssh into a 3D rendered scene": works over a normal interactive
 * shell using ANSI escapes.
 *
 * Capabilities are auto-detected from $LANG/$COLORTERM/$TERM but every
 * mode can be forced via --glyph and --color, which is also how you
 * see the graceful-degradation path on a modern terminal.
 *
 * Controls (key events, not held-down state — terminals don't deliver
 * keyup, so each press nudges by a fixed amount):
 *   WASD / arrows  — orbit / move
 *   shift+W/S      — pitch (also: I/K)
 *   space          — toggle auto-orbit
 *   q / esc        — quit
 *   r              — force a full redraw (handy after window resize) */

#include "term.h"
#include "renderer.h"
#include "viewport.h"
#include "scene.h"
#include "sphere.h"
#include "vector.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define FOV (M_PI / 2.8f)

/* Same scene as apps/orb — textured inner sphere inside a tinted mirror. */
#define INNER_RADIUS        1.4f
#define INNER_TEX_KIND      SCENE_TEX_MARBLE
#define INNER_TEX_SCALE     0.35f
#define INNER_ALBEDO_A      {40,  90, 180}
#define INNER_ALBEDO_B      {230, 220, 180}
#define INNER_REFLECTIVITY  0.4f

#define OUTER_RADIUS        7.0f
#define OUTER_TEX_KIND      SCENE_TEX_CELLS
#define OUTER_TEX_SCALE     1.0f
#define OUTER_ALBEDO_A      {250,  8,  12}
#define OUTER_ALBEDO_B      {40, 40, 60}
#define OUTER_REFLECTIVITY  0.0f

#define ORBIT_RADIUS        4.2f
#define ORBIT_HEIGHT_AMP    1.1f
#define ORBIT_SPEED         0.35f

static void build_scene(scene **scene_out, scene_camera **camera_out) {
    scene *sc = scene_create();

    int m_inner = scene_add_material(sc, (scene_material){
        .albedo = INNER_ALBEDO_A, .albedo2 = INNER_ALBEDO_B,
        .tex_kind = INNER_TEX_KIND, .tex_scale = INNER_TEX_SCALE,
        .reflectivity = INNER_REFLECTIVITY,
    });
    int m_outer = scene_add_material(sc, (scene_material){
        .albedo = OUTER_ALBEDO_A, .albedo2 = OUTER_ALBEDO_B,
        .tex_kind = OUTER_TEX_KIND, .tex_scale = OUTER_TEX_SCALE,
        .reflectivity = OUTER_REFLECTIVITY,
    });
    scene_add_sphere(sc, (scene_sphere){{0,0,0}, INNER_RADIUS, m_inner});
    scene_add_sphere(sc, (scene_sphere){{0,0,0}, OUTER_RADIUS, m_outer});

    scene_set_ambient(sc, 0.25f);
    scene_add_light(sc, (scene_light){.direction={0.4f,0.9f,0.3f}, .intensity=0.8f});
    scene_add_light(sc, (scene_light){.direction={-0.6f,0.3f,-0.5f}, .intensity=0.4f});

    *scene_out = sc;
    *camera_out = scene_camera_create((vector){ORBIT_RADIUS,0.5f,0}, (vector){-1,0,0});
}

static vector cam_dir_from_yaw_pitch(float yaw, float pitch) {
    return (vector){
        cosf(pitch) * sinf(yaw),
        sinf(pitch),
        cosf(pitch) * cosf(yaw),
    };
}

static double now_sec(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

static void usage(const char *argv0) {
    fprintf(stderr,
        "Usage: %s [--glyph halfblock|ascii|auto] [--color truecolor|256|mono|auto]\n"
        "          [--fps N]\n"
        "Controls:\n"
        "  WASD/arrows    orbit / move\n"
        "  I/K            pitch\n"
        "  SPACE          toggle auto-orbit\n"
        "  G              cycle glyph mode (halfblock <-> ascii)\n"
        "  C              cycle colour mode (truecolor -> 256 -> mono)\n"
        "  R              force full redraw\n"
        "  Q / ESC        quit\n",
        argv0);
}

int main(int argc, char *argv[]) {
    term_caps caps = term_caps_detect();
    int target_fps = 30;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--glyph") == 0 && i + 1 < argc) {
            if (!term_caps_parse_glyph(argv[++i], &caps.glyph)) {
                fprintf(stderr, "unknown glyph mode: %s\n", argv[i]);
                return 2;
            }
        } else if (strcmp(argv[i], "--color") == 0 && i + 1 < argc) {
            if (!term_caps_parse_color(argv[++i], &caps.color)) {
                fprintf(stderr, "unknown color mode: %s\n", argv[i]);
                return 2;
            }
        } else if (strcmp(argv[i], "--fps") == 0 && i + 1 < argc) {
            target_fps = atoi(argv[++i]);
            if (target_fps < 5)  target_fps = 5;
            if (target_fps > 60) target_fps = 60;
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            usage(argv[0]);
            return 0;
        } else {
            fprintf(stderr, "unknown arg: %s\n", argv[i]);
            usage(argv[0]);
            return 2;
        }
    }

    if (!rt_renderer_available(RT_BACKEND_CPU)) {
        fprintf(stderr, "CPU raytrace backend not built\n");
        return 1;
    }
    rt_renderer *rnd = rt_renderer_create(RT_BACKEND_CPU);
    if (!rnd) { fprintf(stderr, "renderer create failed\n"); return 1; }

    term_screen *screen = term_screen_open();
    if (!screen) {
        fprintf(stderr, "term_screen_open failed (not a tty?)\n");
        rt_renderer_destroy(rnd);
        return 1;
    }
    term_render_ctx *r = term_render_create();

    scene *sc; scene_camera *camera;
    build_scene(&sc, &camera);

    int cols = 0, rows = 0;
    term_screen_size(&cols, &rows);
    /* Reserve the bottom row for a status line. */
    int draw_rows = rows > 1 ? rows - 1 : rows;

    int fb_w = 0, fb_h = 0;
    term_render_pixel_size(&caps, cols, draw_rows, &fb_w, &fb_h);
    uint32_t *pixels = calloc((size_t)fb_w * (size_t)fb_h, sizeof(uint32_t));
    rt_viewport vp = { fb_w, fb_h, FOV };

    vector cam_pos = {ORBIT_RADIUS, 0.5f, 0.0f};
    float cam_yaw   = -(float)M_PI_2;
    float cam_pitch = 0.0f;
    int   auto_orbit = 1;
    int   running = 1;

    /* Movement step per keystroke. Tuned so a few presses noticeably
     * rotate the camera but a single press doesn't teleport. */
    const float yaw_step   = 0.10f;
    const float pitch_step = 0.07f;
    const float move_step  = 0.25f;

    double t0 = now_sec();
    double frame_target = 1.0 / (double)target_fps;
    double next_frame = now_sec();
    double last_fps_t = now_sec();
    int    fps_frames = 0;
    double render_ms_acc = 0.0;
    int    last_fps = 0;
    double last_ms = 0.0;
    size_t last_bytes = 0;

    /* Reconfigure the framebuffer + viewport for the current cell grid
     * and capability set, and clear the alt screen. Called on SIGWINCH,
     * 'r' (manual redraw), and the live mode toggles 'g' / 'c'. */
    #define RECONFIGURE() do {                                              \
        term_screen_size(&cols, &rows);                                     \
        draw_rows = rows > 1 ? rows - 1 : rows;                             \
        term_render_pixel_size(&caps, cols, draw_rows, &fb_w, &fb_h);       \
        free(pixels);                                                       \
        pixels = calloc((size_t)fb_w * (size_t)fb_h, sizeof(uint32_t));     \
        vp = (rt_viewport){ fb_w, fb_h, FOV };                              \
        term_render_force_full_redraw(r);                                   \
        const char _clr[] = "\x1b[2J\x1b[H";                                \
        term_screen_present(screen, _clr, sizeof(_clr) - 1);                \
    } while (0)

    while (running) {
        if (term_screen_consume_resize(screen)) RECONFIGURE();

        /* Drain all pending input — terminals deliver bursts (especially
         * for autorepeat); reacting to one key per frame feels mushy. */
        term_key key;
        while (term_input_poll(&key)) {
            if (key.kind == TERM_KEY_ESC) { running = 0; break; }
            if (key.kind == TERM_KEY_CHAR) {
                if (key.ch == 'q' || key.ch == 'Q' || key.ch == 0x03) {
                    running = 0; break;
                }
                if (key.ch == ' ') auto_orbit = !auto_orbit;
                if (key.ch == 'r' || key.ch == 'R') RECONFIGURE();
                if (key.ch == 'g' || key.ch == 'G') {
                    /* Cycle glyph: halfblock <-> ascii. Halfblock packs
                     * two pixels per cell, so the framebuffer height
                     * doubles when entering it and halves when leaving;
                     * RECONFIGURE handles the realloc. */
                    caps.glyph = (caps.glyph == TERM_GLYPH_HALFBLOCK)
                                ? TERM_GLYPH_ASCII : TERM_GLYPH_HALFBLOCK;
                    RECONFIGURE();
                }
                if (key.ch == 'c' || key.ch == 'C') {
                    /* Cycle colour: truecolor -> 256 -> mono -> truecolor.
                     * No fb resize needed but we still force a full
                     * redraw so the new mode's bytes overwrite what the
                     * previous mode left on screen. */
                    caps.color = (caps.color == TERM_COLOR_TRUECOLOR)
                                ? TERM_COLOR_PALETTE256
                                : (caps.color == TERM_COLOR_PALETTE256)
                                ? TERM_COLOR_MONO
                                : TERM_COLOR_TRUECOLOR;
                    RECONFIGURE();
                }
                if (key.ch == 'a' || key.ch == 'A') { auto_orbit = 0; cam_yaw -= yaw_step; }
                if (key.ch == 'd' || key.ch == 'D') { auto_orbit = 0; cam_yaw += yaw_step; }
                if (key.ch == 'i' || key.ch == 'I') { auto_orbit = 0; cam_pitch += pitch_step; }
                if (key.ch == 'k' || key.ch == 'K') { auto_orbit = 0; cam_pitch -= pitch_step; }
                if (key.ch == 'w' || key.ch == 'W') {
                    auto_orbit = 0;
                    vector fwd = { sinf(cam_yaw), 0, cosf(cam_yaw) };
                    cam_pos = vector_add(cam_pos, vector_scale(fwd,  move_step));
                }
                if (key.ch == 's' || key.ch == 'S') {
                    auto_orbit = 0;
                    vector fwd = { sinf(cam_yaw), 0, cosf(cam_yaw) };
                    cam_pos = vector_add(cam_pos, vector_scale(fwd, -move_step));
                }
            } else if (key.kind == TERM_KEY_LEFT)  { auto_orbit = 0; cam_yaw   -= yaw_step; }
              else if (key.kind == TERM_KEY_RIGHT) { auto_orbit = 0; cam_yaw   += yaw_step; }
              else if (key.kind == TERM_KEY_UP)    { auto_orbit = 0; cam_pitch += pitch_step; }
              else if (key.kind == TERM_KEY_DOWN)  { auto_orbit = 0; cam_pitch -= pitch_step; }
        }
        if (!running) break;

        if (cam_pitch >  1.4f) cam_pitch =  1.4f;
        if (cam_pitch < -1.4f) cam_pitch = -1.4f;

        if (auto_orbit) {
            float t = (float)(now_sec() - t0);
            float a = t * ORBIT_SPEED;
            cam_pos.x = cosf(a) * ORBIT_RADIUS;
            cam_pos.z = sinf(a) * ORBIT_RADIUS;
            cam_pos.y = sinf(t * 0.55f) * ORBIT_HEIGHT_AMP;
            vector look_at = {0,0,0};
            vector dir = vector_normalize(vector_sub(look_at, cam_pos));
            cam_yaw   = atan2f(dir.x, dir.z);
            cam_pitch = asinf(dir.y);
        } else {
            /* Clamp position to the annulus between the two spheres. */
            float d2 = cam_pos.x*cam_pos.x + cam_pos.y*cam_pos.y + cam_pos.z*cam_pos.z;
            float d = sqrtf(d2);
            float inner = INNER_RADIUS + 0.2f;
            float outer = OUTER_RADIUS - 0.3f;
            if (d < inner && d > 1e-4f) {
                float s = inner / d; cam_pos.x*=s; cam_pos.y*=s; cam_pos.z*=s;
            } else if (d > outer) {
                float s = outer / d; cam_pos.x*=s; cam_pos.y*=s; cam_pos.z*=s;
            }
        }

        vector cam_dir = cam_dir_from_yaw_pitch(cam_yaw, cam_pitch);
        scene_camera_place(camera, cam_pos, cam_dir);

        double r_start = now_sec();
        rt_renderer_render(rnd, sc, camera, &vp, pixels, NULL);
        render_ms_acc += (now_sec() - r_start) * 1000.0;

        size_t n = term_render_frame(r, &caps, pixels, fb_w, fb_h, cols, draw_rows);
        last_bytes = n;
        term_screen_present(screen, term_render_buffer(r), n);

        /* Status line in the reserved row. Use a fresh CUP + SGR so it's
         * never affected by the renderer's leftover state. */
        char status[256];
        int sl = snprintf(status, sizeof(status),
            "\x1b[%d;1H\x1b[0m\x1b[2K"
            "%dx%d cells (fb %dx%d) | %s+%s | %d fps | %.1f ms render | %zu B/frame | "
            "%s | q=quit space=auto WASD/arrows i/k=pitch g=glyph c=color r=redraw",
            rows, cols, draw_rows, fb_w, fb_h,
            term_caps_glyph_name(caps.glyph),
            term_caps_color_name(caps.color),
            last_fps, last_ms, last_bytes,
            auto_orbit ? "auto" : "manual");
        if (sl > 0) term_screen_present(screen, status, (size_t)sl);

        /* Frame pacing. Don't burn CPU faster than target_fps; keeps
         * things polite for the ssh peer too. */
        next_frame += frame_target;
        double slack = next_frame - now_sec();
        if (slack > 0) {
            struct timespec ts = {
                .tv_sec  = (time_t)slack,
                .tv_nsec = (long)((slack - (double)(time_t)slack) * 1e9),
            };
            nanosleep(&ts, NULL);
        } else if (slack < -0.5) {
            /* Way behind — resync rather than spiral into the past. */
            next_frame = now_sec();
        }

        fps_frames++;
        double tnow = now_sec();
        if (tnow - last_fps_t >= 1.0) {
            last_fps = fps_frames;
            last_ms  = render_ms_acc / (double)(fps_frames > 0 ? fps_frames : 1);
            fps_frames = 0;
            render_ms_acc = 0.0;
            last_fps_t = tnow;
        }
    }

    term_render_destroy(r);
    term_screen_close(screen);
    free(pixels);
    scene_camera_destroy(camera);
    scene_destroy(sc);
    rt_renderer_destroy(rnd);
    return 0;
}
