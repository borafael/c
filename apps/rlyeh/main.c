/* R'lyeh — Lovecraftian POV demo.
 *
 * First-person stroll across a drowned alien plain. Bruised teal-purple sky,
 * one swollen red sun on the horizon, two pale teal moons. Far-off jagged
 * obsidian mountains close the world in on every side. Coral-stalk
 * vegetation scattered across the plain.
 *
 * Player can only walk and look — no jumping, no flying, no shooting.
 * The whole point is that nothing happens. The world is finished without you.
 *
 *   WASD / arrows    walk / turn
 *   mouse            look (relative-mouse mode)
 *   M                toggle mouse capture
 *   P                toggle postfx
 *   F11              fullscreen
 *   ESC              quit
 */

#include "renderer.h"
#include "viewport.h"
#include "scene.h"
#include "sphere.h"
#include "plane.h"
#include "cone.h"
#include "cylinder.h"
#include "heightfield.h"
#include "mesh.h"        /* rt_scene_build_accel */
#include "postfx.h"
#include <SDL2/SDL.h>

#define GL_GLEXT_PROTOTYPES 1
#include "gl_compat.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define INIT_WINDOW_W   960
#define INIT_WINDOW_H   600
#define RENDER_W        480
#define RENDER_H        300
#define FOV             (M_PI / 2.6f)
#define EYE_HEIGHT      1.7f
#define WALK_SPEED      4.0f
#define LOOK_SPEED      1.6f          /* keyboard fallback */
#define MOUSE_SENS      0.0025f
#define PITCH_LIMIT     1.45f         /* ~83°, prevents gimbal flip */

/* ===== Mountain heightfield (ring around player) ========================== */
#define HF_ROWS         96
#define HF_COLS         96
#define HF_WORLD_W      600.0f
#define HF_WORLD_D      600.0f
#define HF_INNER        70.0f        /* flat playable area radius */
#define HF_OUTER        280.0f       /* mountains reach max height here */
#define HF_MAX_H        85.0f

/* Borrowed by the scene; storage lives here. */
static float   HF_HEIGHTS[HF_ROWS * HF_COLS];
static float   HF_NORMALS[HF_ROWS * HF_COLS * 3];
static uint8_t HF_COLORS [(HF_ROWS - 1) * (HF_COLS - 1) * 3];

/* ===== Pseudo-noise (small, deterministic, no library) ==================== */
static float hash01(int x, int y) {
    uint32_t h = (uint32_t)(x * 374761393u) ^ (uint32_t)(y * 668265263u);
    h ^= h >> 13;
    h *= 1274126177u;
    h ^= h >> 16;
    return (float)(h & 0xFFFFFFu) / (float)0xFFFFFFu;
}

static float smooth_noise(float x, float y) {
    int ix = (int)floorf(x), iy = (int)floorf(y);
    float fx = x - ix, fy = y - iy;
    float u = fx * fx * (3.0f - 2.0f * fx);
    float v = fy * fy * (3.0f - 2.0f * fy);
    float a = hash01(ix,     iy);
    float b = hash01(ix + 1, iy);
    float c = hash01(ix,     iy + 1);
    float d = hash01(ix + 1, iy + 1);
    return a * (1 - u) * (1 - v) + b * u * (1 - v)
         + c * (1 - u) * v       + d * u * v;
}

static float fbm(float x, float y) {
    float s = 0, amp = 1, freq = 1, sum = 0;
    for (int i = 0; i < 4; i++) {
        s += amp * smooth_noise(x * freq, y * freq);
        sum += amp;
        amp *= 0.5f;
        freq *= 2.0f;
    }
    return s / sum;
}

static void build_mountains(void) {
    for (int r = 0; r < HF_ROWS; r++) {
        for (int c = 0; c < HF_COLS; c++) {
            float u = (float)c / (HF_COLS - 1) - 0.5f;
            float v = (float)r / (HF_ROWS - 1) - 0.5f;
            float wx = u * HF_WORLD_W;
            float wz = v * HF_WORLD_D;
            float d  = sqrtf(wx * wx + wz * wz);

            float ramp = 0.0f;
            if (d > HF_INNER) {
                float t = (d - HF_INNER) / (HF_OUTER - HF_INNER);
                if (t > 1.0f) t = 1.0f;
                /* Squared ramp = gentle near, jagged far. */
                ramp = t * t;
            }
            float n1 = fbm(c * 0.16f, r * 0.16f);
            float n2 = fbm(c * 0.50f, r * 0.50f);
            float h  = ramp * HF_MAX_H * (0.35f + 1.10f * n1) * (0.55f + 0.85f * n2);

            /* Subtle ripple in the inner playable area — a hair of tide
             * pools so the floor doesn't read as perfectly mathematical. */
            if (d < HF_INNER) {
                h = 0.08f * (fbm(c * 0.6f, r * 0.6f) - 0.5f);
            }
            HF_HEIGHTS[r * HF_COLS + c] = h;
        }
    }
    float dx = HF_WORLD_W / (HF_COLS - 1);
    float dz = HF_WORLD_D / (HF_ROWS - 1);
    for (int r = 0; r < HF_ROWS; r++) {
        for (int c = 0; c < HF_COLS; c++) {
            int cl = c > 0           ? c - 1 : c;
            int cr = c < HF_COLS - 1 ? c + 1 : c;
            int rt = r > 0           ? r - 1 : r;
            int rb = r < HF_ROWS - 1 ? r + 1 : r;
            float hl = HF_HEIGHTS[r  * HF_COLS + cl];
            float hr = HF_HEIGHTS[r  * HF_COLS + cr];
            float ht = HF_HEIGHTS[rt * HF_COLS + c];
            float hb = HF_HEIGHTS[rb * HF_COLS + c];
            vector n = {
                -(hr - hl) / ((cr - cl) * dx),
                 1.0f,
                -(hb - ht) / ((rb - rt) * dz)
            };
            n = vector_normalize(n);
            HF_NORMALS[(r * HF_COLS + c) * 3 + 0] = n.x;
            HF_NORMALS[(r * HF_COLS + c) * 3 + 1] = n.y;
            HF_NORMALS[(r * HF_COLS + c) * 3 + 2] = n.z;
        }
    }
    /* Per-cell colors — wet obsidian, slightly teal-pooled in the valleys,
     * fading to near-black at the peaks. */
    for (int r = 0; r < HF_ROWS - 1; r++) {
        for (int c = 0; c < HF_COLS - 1; c++) {
            float h = HF_HEIGHTS[r * HF_COLS + c];
            float t = h / HF_MAX_H;
            if (t < 0) t = 0;
            if (t > 1) t = 1;
            /* Low ground: teal-tinged dark; high peaks: bluer black. */
            float rch = 6.0f  * (1.0f - t) +  3.0f * t;
            float gch = 18.0f * (1.0f - t) +  5.0f * t;
            float bch = 28.0f * (1.0f - t) + 10.0f * t;
            int ci = (r * (HF_COLS - 1) + c) * 3;
            HF_COLORS[ci + 0] = (uint8_t)rch;
            HF_COLORS[ci + 1] = (uint8_t)gch;
            HF_COLORS[ci + 2] = (uint8_t)bch;
        }
    }
}

/* ===== Scene construction ================================================ */
static void add_star_field(scene *s, int mat, int count) {
    /* Stars on a hemisphere; positions are pseudo-random but deterministic. */
    for (int i = 0; i < count; i++) {
        float u = hash01(i, 11);
        float v = hash01(i, 47);
        float theta = u * 2.0f * (float)M_PI;
        /* Bias toward upper hemisphere — clamp v so phi stays > 30° above horizon. */
        float phi = (0.18f + 0.55f * v) * (float)M_PI;   /* polar angle from +Y */
        float r = 600.0f + 60.0f * hash01(i, 73);
        float x = r * sinf(phi) * cosf(theta);
        float y = r * cosf(phi);
        float z = r * sinf(phi) * sinf(theta);
        float radius = 0.6f + 1.6f * hash01(i, 19);
        scene_add_sphere(s, (scene_sphere){
            .center = {x, y, z}, .radius = radius, .material = mat,
        });
    }
}

static void add_vegetation(scene *s, int stalk_mat) {
    /* Coral / polyp clumps scattered across the playable area. Each clump
     * is a tall asymmetric cone tilted slightly off-vertical. Positions
     * are seeded but pseudo-random; orientation is varied. */
    static const struct { float x, z, h, r, tilt; } clumps[] = {
        { -18.0f,  12.0f, 5.2f, 0.55f, 0.05f },
        {  22.0f, -14.0f, 3.8f, 0.45f, 0.12f },
        { -32.0f, -25.0f, 6.5f, 0.65f, 0.08f },
        {  35.0f,  28.0f, 4.4f, 0.40f, 0.15f },
        {   8.0f,  44.0f, 7.2f, 0.70f, 0.04f },
        { -12.0f, -48.0f, 5.8f, 0.55f, 0.11f },
        {  48.0f,  -4.0f, 4.1f, 0.42f, 0.18f },
    };
    int n = (int)(sizeof(clumps) / sizeof(clumps[0]));
    for (int i = 0; i < n; i++) {
        float th = (float)i * 0.7f;
        vector apex = { clumps[i].x, clumps[i].h, clumps[i].z };
        vector axis = vector_normalize((vector){
            clumps[i].tilt * cosf(th), -1.0f, clumps[i].tilt * sinf(th)
        });
        scene_add_cone(s, (scene_cone){
            .apex     = apex,
            .axis     = axis,
            .height   = clumps[i].h,
            .radius   = clumps[i].r,
            .material = stalk_mat,
        });
    }
}

static void build_scene(scene **out_s, scene_camera **out_cam) {
    scene *s = scene_create();

    /* ===== Materials ===== */
    /* Sky: dark teal at horizon, bleeding to bruised purple at zenith via
     * GRADIENT (albedo at bottom -> albedo2 at top), unlit. The sphere
     * is huge so the gradient stretches across the whole field of view. */
    int m_sky = scene_add_material(s, (scene_material){
        .albedo   = {  8, 32, 48},   /* horizon: deep teal */
        .albedo2  = { 58, 24, 64},   /* zenith: bruised purple */
        .tex_kind = SCENE_TEX_GRADIENT,
        .tex_scale = 1400.0f,        /* span = sky-sphere diameter */
        .unlit = 1,
    });
    int m_sun = scene_add_material(s, (scene_material){
        .albedo = {180, 28, 22}, .unlit = 1,
    });
    int m_moon = scene_add_material(s, (scene_material){
        .albedo = {130, 170, 175}, .unlit = 1,
    });
    int m_moon2 = scene_add_material(s, (scene_material){
        .albedo = {95, 145, 155}, .unlit = 1,
    });
    int m_star = scene_add_material(s, (scene_material){
        .albedo = {220, 230, 235}, .unlit = 1,
    });
    /* Heightfield uses the per-cell colors baked into HF_COLORS, no
     * material needed. Pass -1 for the heightfield material to skip the
     * material-modulation path entirely. */

    /* Coral stalks — dark with cyan/teal marble veins, very low
     * reflectivity (wet but not mirror). */
    int m_stalk = scene_add_material(s, (scene_material){
        .albedo   = { 18,  35,  45},
        .albedo2  = { 70, 140, 150},
        .tex_kind = SCENE_TEX_MARBLE,
        .tex_scale = 1.2f,
        .reflectivity = 0.08f,
    });
    /* ===== Geometry ===== */
    /* Sky sphere — huge, centered on origin, gradient is along +Y. */
    scene_add_sphere(s, (scene_sphere){
        .center = {0, 0, 0}, .radius = 700.0f, .material = m_sky,
    });

    /* Sun — low, swollen, on the +Z horizon to invite walking toward it. */
    scene_add_sphere(s, (scene_sphere){
        .center = {0.0f, 38.0f, 500.0f}, .radius = 55.0f, .material = m_sun,
    });

    /* Two pale teal moons — high and oblique. */
    scene_add_sphere(s, (scene_sphere){
        .center = {-280.0f, 200.0f, 80.0f}, .radius = 24.0f, .material = m_moon,
    });
    scene_add_sphere(s, (scene_sphere){
        .center = {220.0f, 240.0f, -120.0f}, .radius = 18.0f, .material = m_moon2,
    });

    /* Stars — deterministic pseudo-random hemisphere. */
    add_star_field(s, m_star, 90);

    /* Mountains — heightfield ring around the player. */
    build_mountains();
    scene_heightfield hf = {
        .heights     = HF_HEIGHTS,
        .normals     = HF_NORMALS,
        .colors      = HF_COLORS,
        .rows        = HF_ROWS,
        .cols        = HF_COLS,
        .world_width = HF_WORLD_W,
        .world_depth = HF_WORLD_D,
        .origin_x    = -HF_WORLD_W * 0.5f,
        .origin_z    = -HF_WORLD_D * 0.5f,
        .max_height  = HF_MAX_H,
        .material    = -1,         /* raw cell colors, no material modulation */
    };
    scene_add_heightfield(s, &hf);

    /* Vegetation. */
    add_vegetation(s, m_stalk);

    /* ===== Lights ===== */
    /* Two directionals: one from the sun's direction (warmer red), one
     * faint fill from the opposite zenith (cold teal-violet). Low ambient
     * keeps shadows heavy without going pitch-black. */
    scene_set_ambient(s, 0.18f);
    scene_add_light(s, (scene_light){
        .direction = vector_normalize((vector){0.0f, 0.30f, 1.0f}),
        .intensity = 0.70f,
    });
    scene_add_light(s, (scene_light){
        .direction = vector_normalize((vector){-0.4f, 0.55f, -0.6f}),
        .intensity = 0.28f,
    });

    rt_scene_build_accel(s);

    *out_s   = s;
    *out_cam = scene_camera_create(
        (vector){0.0f, EYE_HEIGHT, 0.0f},
        (vector){0.0f, 0.0f, 1.0f}
    );
}

/* ===== POV helpers ======================================================= */
static vector cam_dir_from_yaw_pitch(float yaw, float pitch) {
    return (vector){
        cosf(pitch) * sinf(yaw),
        sinf(pitch),
        cosf(pitch) * cosf(yaw)
    };
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

int main(int argc, char *argv[]) {
    (void)argc; (void)argv;

    /* CPU raytrace; interlace on for ~2x frame rate. The dropped rows
     * just hold the previous frame and read as crawling persistence on
     * motion, which fits the mood. */
    setenv("RT_CPU_INTERLACE", "0", 1);

    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        fprintf(stderr, "SDL init failed: %s\n", SDL_GetError());
        return 1;
    }
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

    rt_renderer *rnd = rt_renderer_create(RT_BACKEND_CPU);
    if (!rnd) {
        fprintf(stderr, "CPU renderer unavailable\n");
        SDL_GL_DeleteContext(gl_ctx);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    scene *scn = NULL;
    scene_camera *cam = NULL;
    build_scene(&scn, &cam);

    int render_w = RENDER_W, render_h = RENDER_H;
    rt_viewport viewport = { render_w, render_h, FOV };
    uint32_t *pixels = calloc((size_t)(render_w * render_h), sizeof(uint32_t));

    /* Postfx stack — vignette tightens the frame, chromatic shifts the
     * sun's red into the sky, bloom blooms the sun, grain crawls. */
    postfx_chromatic_ctx *chrom = postfx_chromatic_create(render_w, render_h);
    postfx_bloom_ctx     *bloom = postfx_bloom_create(render_w, render_h);
    postfx_chromatic chrom_cfg = { .enabled = 1, .shift_pixels = 1 };
    postfx_vignette  vig_cfg   = { .enabled = 1, .intensity = 0.75f, .softness = 0.30f };
    postfx_grain     grain_cfg = { .enabled = 1, .strength = 0.10f, .seed = 0 };
    postfx_bloom     bloom_cfg = {
        .enabled = 1, .threshold = 0.55f, .knee = 0.30f,
        .intensity = 0.55f, .radius = 6, .iterations = 2,
    };

    GLuint display_tex, display_fbo;
    glGenTextures(1, &display_tex);
    glBindTexture(GL_TEXTURE_2D, display_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, render_w, render_h, 0,
                 GL_BGRA, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glGenFramebuffers(1, &display_fbo);

    /* Player state — at origin, looking down +Z toward the sun. */
    vector cam_pos   = {0.0f, EYE_HEIGHT, 0.0f};
    float  cam_yaw   = 0.0f;     /* 0 = +Z */
    float  cam_pitch = 0.0f;
    int    mouse_captured = 1;
    int    postfx_on = 1;

    SDL_SetRelativeMouseMode(SDL_TRUE);

    int running = 1;
    Uint32 frame_last = SDL_GetTicks();
    Uint32 fps_last   = SDL_GetTicks();
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

        Uint32 r0 = SDL_GetTicks();
        rt_renderer_render(rnd, scn, cam, &viewport, pixels, NULL);
        Uint32 r1 = SDL_GetTicks();

        /* Interlace leaves odd rows holding prior-frame content. Without
         * intervention, chromatic + grain compound on those stale rows
         * each frame and they drift into red/green static. Line-double
         * the rendered (even) rows down into the odd rows so postfx
         * operates on a fully-coherent image. Halves vertical detail
         * but kills the artifact entirely. */
        for (int y = 1; y < render_h; y += 2) {
            memcpy(&pixels[y * render_w], &pixels[(y - 1) * render_w],
                   (size_t)render_w * sizeof(uint32_t));
        }

        if (postfx_on) {
            postfx_bloom_apply    (bloom, pixels, render_w, render_h, &bloom_cfg);
            postfx_chromatic_apply(chrom, pixels, render_w, render_h, &chrom_cfg);
            postfx_vignette_apply (pixels, render_w, render_h, &vig_cfg);
            grain_cfg.seed = frame_now;
            postfx_grain_apply    (pixels, render_w, render_h, &grain_cfg);
        }
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
            snprintf(title_buf, sizeof(title_buf),
                     "R'lyeh - %d FPS (rt=%.1fms fx=%.1fms) %dx%d %s",
                     fps_frames, ar, afx, render_w, render_h,
                     postfx_on ? "[postfx]" : "");
            SDL_SetWindowTitle(window, title_buf);
            fps_frames = 0; r_ms = 0; fx_ms = 0;
            fps_last = now;
        }
    }

    glDeleteFramebuffers(1, &display_fbo);
    glDeleteTextures(1, &display_tex);
    postfx_chromatic_destroy(chrom);
    postfx_bloom_destroy(bloom);
    free(pixels);
    scene_camera_destroy(cam);
    scene_destroy(scn);
    rt_renderer_destroy(rnd);
    SDL_GL_DeleteContext(gl_ctx);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
