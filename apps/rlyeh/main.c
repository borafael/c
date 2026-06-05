/* R'lyeh — Lovecraftian POV demo (harness).
 *
 * Two worlds you can cycle between (O), each defined in its own file behind
 * the world_api in world.h:
 *   - R'lyeh (world_rlyeh.c): a first-person stroll across a drowned alien
 *     plain. Bruised teal-purple sky, two pale teal moons, far-off obsidian
 *     mountains, leaning coral stalks. The world is finished without you.
 *   - Lighthouse (world_lighthouse.c): the "waking" world — a calm dawn over
 *     a reflective sea, a white tower with a sweeping beam, a low sun. Meant
 *     to feel right, but subtly wrong from the first frame: a wrongness knob
 *     ([ / ]) curdles the dawn toward the dream (sky, fog, beam, sun, stalk).
 *
 * This file owns the window, renderer backends, postfx stack, input, the
 * wake-fade, the whisper overlay, and the wrongness knob; the worlds own only
 * their scenes. Player can only walk and look — no jumping, flying, shooting.
 *
 *   WASD / arrows    walk / turn
 *   mouse            look (relative-mouse mode)
 *   O                cycle world (Lighthouse <-> R'lyeh)
 *   [ / ]            wrongness down / up (Lighthouse)
 *   R                cycle render resolution
 *   TAB              cycle CPU / OpenGL backend
 *   I                toggle interlace (CPU)
 *   M                toggle mouse capture
 *   P                toggle postfx
 *   F11              fullscreen
 *   ESC              quit
 */

#include "renderer.h"
#include "viewport.h"
#include "scene.h"
#include "postfx.h"
#include "audio.h"
#include "world.h"
#include "whisper.h"
#include "bench.h"
#include <SDL2/SDL.h>

#define GL_GLEXT_PROTOTYPES 1
#include "gl_compat.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#ifdef _WIN32
/* MinGW lacks POSIX setenv; _putenv_s has the same effect for our uses. */
#define setenv(k, v, ow) _putenv_s((k), (v))
#endif

#define INIT_WINDOW_W   960
#define INIT_WINDOW_H   600

/* Render-resolution ladder (R cycles). Every rung is 16:10 like the
 * window, so the nearest-neighbour upscale in display_pixels() stays an
 * integer multiple and the chunky-pixel look never shimmers. */
static const struct { int w, h; } RES_LADDER[] = {
    { 120,  75 },
    { 240, 150 },
    { 480, 300 },
    { 960, 600 },
};
#define RES_LADDER_N    ((int)(sizeof(RES_LADDER) / sizeof(RES_LADDER[0])))
#define RES_DEFAULT_IDX 1   /* 240x150 */
#define FOV             (M_PI / 2.6f)
#define WALK_SPEED      4.0f
#define LOOK_SPEED      1.6f          /* keyboard fallback */
#define MOUSE_SENS      0.0025f
#define PITCH_LIMIT     1.45f         /* ~83°, prevents gimbal flip */
#define WAKE_FADE_SEC   4.5f          /* black -> full brightness on startup */

/* Selectable worlds; O cycles. Index 0 is the default — you wake in it. */
static const world_api *const WORLDS[] = {
    &world_lighthouse,      /* the shore */
    &world_lighthouse_top,  /* up in the lantern room */
    &world_rlyeh,           /* the dream */
};
#define WORLD_COUNT ((int)(sizeof(WORLDS) / sizeof(WORLDS[0])))

/* Wrongness ∈ [0,1]. Default is small but non-zero: subtly wrong from the
 * first frame. [ / ] nudge it live; 0 = fully "right", 1 = fully the dream.
 * Only the Lighthouse reads it; harness owns it and passes it to animate(). */
static float g_wrongness = 0.15f;

/* Whispers — faint Lovecraftian text overlay — live in whisper.c
 * (declared in whisper.h); the harness just inits/updates/renders. */

/* ===== POV helpers ======================================================= */
static vector cam_dir_from_yaw_pitch(float yaw, float pitch) {
    return (vector){
        cosf(pitch) * sinf(yaw),
        sinf(pitch),
        cosf(pitch) * cosf(yaw)
    };
}

/* (Re)allocate the CPU framebuffer + G-buffer and resize the display
 * texture for a new render resolution. Frees any previous buffers, so it
 * doubles as the initial allocator when called with *pixels == NULL and a
 * zeroed gbuffer (free(NULL) is a no-op). The postfx contexts are left
 * untouched — bloom/chromatic auto-resize their scratch on the next apply
 * when they see the new width/height. */
static void set_render_res(int w, int h, uint32_t **pixels, rt_gbuffer *gb,
                           rt_viewport *vp, GLuint tex) {
    free(*pixels);
    free(gb->object_id);
    free(gb->depth);
    free(gb->normal);
    *pixels       = calloc((size_t)(w * h), sizeof(uint32_t));
    gb->object_id = calloc((size_t)(w * h), sizeof(uint32_t));
    gb->depth     = calloc((size_t)(w * h), sizeof(float));
    gb->normal    = calloc((size_t)(w * h) * 3, sizeof(float));
    *vp = (rt_viewport){ w, h, FOV };
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0,
                 GL_BGRA, GL_UNSIGNED_BYTE, NULL);
}

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

/* The headless benchmark (RLYEH_BENCH=1) lives in bench.c / bench.h. */

int main(int argc, char *argv[]) {
    (void)argc; (void)argv;

    if (getenv("RLYEH_BENCH")) return run_bench();

    /* CPU raytrace; start interlaced (even rows only) for ~2x frame rate.
     * This only sets the CPU backend's initial field at create time — the
     * I key flips it live at runtime via rt_renderer_set_interlace(). */
    setenv("RT_CPU_INTERLACE", "0", 1);

    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        fprintf(stderr, "SDL init failed: %s\n", SDL_GetError());
        return 1;
    }

    /* Ambient loop — failure is non-fatal, the demo just runs silent.
     * Two probe paths so the same binary works from the project root
     * (dev) and from a staged dir next to its `assets/` (Win64 build). */
    if (audio_init("apps/rlyeh/assets/ambient.mp3") != 0)
        audio_init("assets/ambient.mp3");
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

    int window_w = INIT_WINDOW_W, window_h = INIT_WINDOW_H;
    int fullscreen = 0;
    SDL_Window *window = SDL_CreateWindow("R'lyeh",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        window_w, window_h, SDL_WINDOW_OPENGL);
    if (!window) {
        fprintf(stderr, "Window: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }
    SDL_GLContext gl_ctx = SDL_GL_CreateContext(window);
    if (!gl_ctx) {
        fprintf(stderr, "GL ctx: %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    SDL_GL_SetSwapInterval(0);
    gl_compat_init((gl_compat_loader_fn)SDL_GL_GetProcAddress);

    /* Build every backend this configuration supports. CPU goes first so
     * it stays the default — its even-rows interlace (RT_CPU_INTERLACE=0,
     * set above) drives the line-doubling persistence below; the OpenGL
     * compute path renders full-frame instead. Both fill the G-buffer, so
     * the fog pass works either way. TAB cycles. */
    rt_renderer *backends[2] = {0};
    rt_backend   backend_kind[2] = {0};
    int backend_count = 0;
    const rt_backend try_order[] = { RT_BACKEND_CPU, RT_BACKEND_OPENGL };
    for (size_t i = 0; i < sizeof(try_order) / sizeof(try_order[0]); i++) {
        if (!rt_renderer_available(try_order[i])) continue;
        rt_renderer *r = rt_renderer_create(try_order[i]);
        if (r) { backend_kind[backend_count] = try_order[i]; backends[backend_count++] = r; }
    }
    if (backend_count == 0) {
        fprintf(stderr, "No renderer backends available\n");
        SDL_GL_DeleteContext(gl_ctx);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    int backend_idx = 0;
    rt_renderer *rnd = backends[backend_idx];
    int active_is_cpu = (backend_kind[backend_idx] == RT_BACKEND_CPU);
    fprintf(stderr, "Renderer: %s (TAB cycles %d backend%s)\n",
            rt_renderer_name(rnd), backend_count, backend_count == 1 ? "" : "s");

    /* The active world drives scene build + per-frame animation through its
     * world_api; the harness below is world-agnostic. */
    int world_idx = 0;
    const world_api *world = WORLDS[world_idx];
    scene *scn = NULL;
    scene_camera *cam = NULL;
    int sky_mat = -1;
    postfx_fog fog_cfg;                  /* filled per-world by world->build */
    world->build(&scn, &cam, &sky_mat, &fog_cfg);
    fprintf(stderr, "World: %s (O cycles %d worlds; [ / ] wrongness, start %.2f)\n",
            world->name, WORLD_COUNT, g_wrongness);

    /* Buffers + viewport are sized by set_render_res() below (and again
     * whenever R cycles the ladder); the G-buffer drives the distance-fog
     * pass, so depth and object_id are needed (normal is unused but the
     * renderer fills all three). */
    int res_idx  = RES_DEFAULT_IDX;
    int render_w = RES_LADDER[res_idx].w, render_h = RES_LADDER[res_idx].h;
    rt_viewport viewport = { render_w, render_h, FOV };
    uint32_t *pixels = NULL;
    rt_gbuffer gbuf  = {0};

    /* Postfx stack — fog first (so bloom blooms the foggy frame and
     * bright glints spill over hazed mountains), then chromatic +
     * vignette + grain on top. The fog config is owned by the active world
     * (filled by build, optionally rewritten each frame by animate). */
    postfx_chromatic_ctx *chrom = postfx_chromatic_create(render_w, render_h);
    postfx_bloom_ctx     *bloom = postfx_bloom_create(render_w, render_h);
    postfx_chromatic chrom_cfg = { .enabled = 1, .shift_pixels = 1 };
    postfx_vignette  vig_cfg   = { .enabled = 1, .intensity = 0.35f, .softness = 0.50f };
    postfx_grain     grain_cfg = { .enabled = 1, .strength = 0.10f, .seed = 0 };
    postfx_bloom     bloom_cfg = {
        .enabled = 1, .threshold = 0.55f, .knee = 0.30f,
        .intensity = 0.55f, .radius = 6, .iterations = 2,
    };

    GLuint display_tex, display_fbo;
    glGenTextures(1, &display_tex);
    glBindTexture(GL_TEXTURE_2D, display_tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glGenFramebuffers(1, &display_fbo);

    /* Allocate the frame/G-buffers and size the display texture for the
     * starting rung. */
    set_render_res(render_w, render_h, &pixels, &gbuf, &viewport, display_tex);

    /* Player state — at origin, looking down +Z across the world. */
    vector cam_pos   = {0.0f, EYE_HEIGHT, 0.0f};
    float  cam_yaw   = 0.0f;     /* 0 = +Z */
    float  cam_pitch = 0.0f;
    int    mouse_captured = 1;
    int    postfx_on = 1;
    int    interlace_on = 1;     /* CPU even-rows interlace (matches the setenv above); I toggles live */

    /* Whisper state — starts idle, first fire is between WHISPER_GAP_MIN
     * and WHISPER_GAP_MAX seconds after the wake fade finishes. */
    whisper_state whisper;
    whisper_init(&whisper, WAKE_FADE_SEC);

    SDL_SetRelativeMouseMode(SDL_TRUE);

    int running = 1;
    Uint32 start_ticks = SDL_GetTicks();
    Uint32 frame_last  = start_ticks;
    Uint32 fps_last    = start_ticks;
    int    fps_frames = 0;
    Uint32 r_ms = 0, fx_ms = 0;
    char title_buf[200];

    while (running) {
        Uint32 frame_now = SDL_GetTicks();
        float dt = (frame_now - frame_last) / 1000.0f;
        if (dt > 0.1f) dt = 0.1f;
        frame_last = frame_now;

        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) running = 0;
            if (e.type == SDL_KEYDOWN) {
                SDL_Keycode k = e.key.keysym.sym;
                if (k == SDLK_ESCAPE) running = 0;
                if (k == SDLK_m) {
                    mouse_captured = !mouse_captured;
                    SDL_SetRelativeMouseMode(mouse_captured ? SDL_TRUE : SDL_FALSE);
                }
                if (k == SDLK_p) postfx_on = !postfx_on;
                if (k == SDLK_o) {
                    /* Cycle worlds: tear down the current scene/camera, build
                     * the next, respawn at the origin, and re-trigger the
                     * wake fade so you "wake" into it. */
                    world_idx = (world_idx + 1) % WORLD_COUNT;
                    world = WORLDS[world_idx];
                    scene_camera_destroy(cam);
                    scene_destroy(scn);
                    world->build(&scn, &cam, &sky_mat, &fog_cfg);
                    cam_pos   = (vector){0.0f, EYE_HEIGHT, 0.0f};
                    cam_yaw   = 0.0f;
                    cam_pitch = 0.0f;
                    start_ticks = SDL_GetTicks();
                }
                if (k == SDLK_LEFTBRACKET) {
                    g_wrongness -= 0.05f;
                    if (g_wrongness < 0.0f) g_wrongness = 0.0f;
                }
                if (k == SDLK_RIGHTBRACKET) {
                    g_wrongness += 0.05f;
                    if (g_wrongness > 1.0f) g_wrongness = 1.0f;
                }
                if (k == SDLK_i) {
                    /* Field 0 = even rows only (interlaced), -1 = full
                     * frame. No-op on the OpenGL backend, which always
                     * renders every row. */
                    interlace_on = !interlace_on;
                    rt_renderer_set_interlace(rnd, interlace_on ? 0 : -1);
                }
                if (k == SDLK_r) {
                    res_idx  = (res_idx + 1) % RES_LADDER_N;
                    render_w = RES_LADDER[res_idx].w;
                    render_h = RES_LADDER[res_idx].h;
                    set_render_res(render_w, render_h, &pixels, &gbuf,
                                   &viewport, display_tex);
                }
                if (k == SDLK_TAB && backend_count > 1) {
                    backend_idx   = (backend_idx + 1) % backend_count;
                    rnd           = backends[backend_idx];
                    active_is_cpu = (backend_kind[backend_idx] == RT_BACKEND_CPU);
                    /* Re-assert the interlace choice on the newly-active
                     * renderer (the CPU backend keeps its own field). */
                    rt_renderer_set_interlace(rnd, interlace_on ? 0 : -1);
                }
                if (k == SDLK_F11) {
                    fullscreen = !fullscreen;
                    SDL_SetWindowFullscreen(window,
                        fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
                    SDL_GetWindowSize(window, &window_w, &window_h);
                }
            }
            if (e.type == SDL_MOUSEMOTION && mouse_captured) {
                cam_yaw   += e.motion.xrel * MOUSE_SENS;
                cam_pitch -= e.motion.yrel * MOUSE_SENS;
            }
        }

        const Uint8 *keys = SDL_GetKeyboardState(NULL);
        /* Arrow keys = look (fallback when mouse capture is off). */
        if (keys[SDL_SCANCODE_LEFT])  cam_yaw   -= LOOK_SPEED * dt;
        if (keys[SDL_SCANCODE_RIGHT]) cam_yaw   += LOOK_SPEED * dt;
        if (keys[SDL_SCANCODE_UP])    cam_pitch += LOOK_SPEED * dt;
        if (keys[SDL_SCANCODE_DOWN])  cam_pitch -= LOOK_SPEED * dt;
        if (cam_pitch >  PITCH_LIMIT) cam_pitch =  PITCH_LIMIT;
        if (cam_pitch < -PITCH_LIMIT) cam_pitch = -PITCH_LIMIT;

        /* WASD = walk on the plain. Movement is yaw-aligned, no pitch
         * (so looking up doesn't levitate you). Y is locked to eye height. */
        vector fwd   = { sinf(cam_yaw), 0.0f, cosf(cam_yaw) };
        vector right = { cosf(cam_yaw), 0.0f, -sinf(cam_yaw) };
        float v = WALK_SPEED * dt;
        if (keys[SDL_SCANCODE_W]) cam_pos = vector_add(cam_pos, vector_scale(fwd,    v));
        if (keys[SDL_SCANCODE_S]) cam_pos = vector_add(cam_pos, vector_scale(fwd,   -v));
        if (keys[SDL_SCANCODE_D]) cam_pos = vector_add(cam_pos, vector_scale(right,  v));
        if (keys[SDL_SCANCODE_A]) cam_pos = vector_add(cam_pos, vector_scale(right, -v));
        cam_pos.y = EYE_HEIGHT;

        vector cam_dir = cam_dir_from_yaw_pitch(cam_yaw, cam_pitch);
        scene_camera_place(cam, cam_pos, cam_dir);

        /* Hand the frame to the active world. It owns all per-frame scene
         * mutation (sky pulse, drift, beam sweep, the wrongness response);
         * the harness only reads the results back when it renders. */
        float t_sec = (frame_now - start_ticks) / 1000.0f;
        world->animate(scn, sky_mat, t_sec, dt, cam_pos, g_wrongness, &fog_cfg);

        Uint32 r0 = SDL_GetTicks();
        rt_renderer_render(rnd, scn, cam, &viewport, pixels, &gbuf);
        Uint32 r1 = SDL_GetTicks();

        /* Interlace leaves odd rows holding prior-frame content. Without
         * intervention, chromatic + grain compound on those stale rows
         * each frame and they drift into red/green static. Line-double
         * the rendered (even) rows down into the odd rows so postfx
         * operates on a fully-coherent image. The G-buffer needs the
         * same treatment because fog reads odd-row depth. Halves
         * vertical detail but kills the artifact entirely. Only the CPU
         * backend interlaces; the OpenGL path renders every row, so skip
         * the doubling there and keep its full vertical detail. */
        for (int y = 1; active_is_cpu && interlace_on && y < render_h; y += 2) {
            size_t row_px   = (size_t)render_w;
            memcpy(&pixels[y * render_w],         &pixels[(y - 1) * render_w],
                   row_px * sizeof(uint32_t));
            memcpy(&gbuf.depth[y * render_w],     &gbuf.depth[(y - 1) * render_w],
                   row_px * sizeof(float));
            memcpy(&gbuf.object_id[y * render_w], &gbuf.object_id[(y - 1) * render_w],
                   row_px * sizeof(uint32_t));
        }

        if (postfx_on) {
            postfx_fog_apply      (pixels, &(postfx_gbuffer){
                                       .object_id = gbuf.object_id,
                                       .depth     = gbuf.depth,
                                       .normal    = gbuf.normal,
                                   }, render_w, render_h, &fog_cfg);
            postfx_bloom_apply    (bloom, pixels, render_w, render_h, &bloom_cfg);
            postfx_chromatic_apply(chrom, pixels, render_w, render_h, &chrom_cfg);
            postfx_vignette_apply (pixels, render_w, render_h, &vig_cfg);
            grain_cfg.seed = frame_now;
            postfx_grain_apply    (pixels, render_w, render_h, &grain_cfg);
        }
        /* Wake-up fade: smooth ramp from pitch black on the first frame
         * to full brightness over WAKE_FADE_SEC. Smoothstep gives an
         * eyelids-opening feel — slow start, slow finish. Applied last
         * so postfx (grain, vignette) fades in too. Re-triggered on every
         * world switch so you "wake" into the new world. */
        float wake_t = (frame_now - start_ticks) / 1000.0f / WAKE_FADE_SEC;
        if (wake_t < 1.0f) {
            if (wake_t < 0.0f) wake_t = 0.0f;
            float fade = wake_t * wake_t * (3.0f - 2.0f * wake_t);
            uint32_t mul = (uint32_t)(fade * 256.0f);
            int n = render_w * render_h;
            for (int i = 0; i < n; i++) {
                uint32_t p = pixels[i];
                uint32_t b = ((p        & 0xFF) * mul) >> 8;
                uint32_t g = (((p >> 8) & 0xFF) * mul) >> 8;
                uint32_t r = (((p >> 16) & 0xFF) * mul) >> 8;
                pixels[i] = (p & 0xFF000000) | (r << 16) | (g << 8) | b;
            }
        }

        /* Whispers — overlay last, so they fade in on top of the
         * already-postfx'd frame (and survive the wake fade naturally
         * because the first whisper fires well after it ends). */
        whisper_update(&whisper, dt, frame_now);
        whisper_render(&whisper, pixels, render_w, render_h);

        Uint32 fx1 = SDL_GetTicks();
        r_ms  += r1  - r0;
        fx_ms += fx1 - r1;

        display_pixels(display_tex, display_fbo, pixels,
                       render_w, render_h, window_w, window_h);
        SDL_GL_SwapWindow(window);

        fps_frames++;
        Uint32 now = SDL_GetTicks();
        if (now - fps_last >= 1000) {
            float ar  = fps_frames ? (float)r_ms  / fps_frames : 0.0f;
            float afx = fps_frames ? (float)fx_ms / fps_frames : 0.0f;
            const char *lace = (active_is_cpu && interlace_on) ? "il" : "full";
            snprintf(title_buf, sizeof(title_buf),
                     "%s - %d FPS (rt=%.1fms fx=%.1fms) %dx%d %s %s %s w=%.2f",
                     world->name, fps_frames, ar, afx, render_w, render_h,
                     rt_renderer_name(rnd), lace, postfx_on ? "[postfx]" : "",
                     g_wrongness);
            SDL_SetWindowTitle(window, title_buf);
            fps_frames = 0; r_ms = 0; fx_ms = 0;
            fps_last = now;
        }
    }

    glDeleteFramebuffers(1, &display_fbo);
    glDeleteTextures(1, &display_tex);
    postfx_chromatic_destroy(chrom);
    postfx_bloom_destroy(bloom);
    free(gbuf.normal);
    free(gbuf.depth);
    free(gbuf.object_id);
    free(pixels);
    scene_camera_destroy(cam);
    scene_destroy(scn);
    for (int i = 0; i < backend_count; i++) rt_renderer_destroy(backends[i]);
    SDL_GL_DeleteContext(gl_ctx);
    SDL_DestroyWindow(window);
    audio_shutdown();
    SDL_Quit();
    return 0;
}
