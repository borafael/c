/* Portals: object-traversal demo (step 1).
 *
 * Two PAIRED_RIGID disc portals A and B, with a tagged sphere oscillating
 * back and forth through A. Step 1 of the "object coming out of a portal"
 * upgrade ladder: prove that one primitive (sphere) can be split across
 * the entry/exit of one paired-rigid disc portal.
 *
 * Mechanism: the sphere carries a `portal_disc1` tag pointing at disc A.
 * The renderer:
 *   - Clips the original sphere to A's front half-space.
 *   - Renders a virtual copy at B, transformed by the rigid-disc portal
 *     map (tangent offset preserved, normal-axis component flipped), and
 *     clipped to B's front half-space.
 * Together those two halves reproduce "the object is poking through the
 * portal" — front half visible at the entry, back half emerging from
 * the exit.
 *
 * The previous PARAMETRIC mismatched-shape demo lives in git history.
 *
 * ESC quits, SPACE toggles auto-orbit, WASD/arrows fly. */

#include "renderer.h"
#include "viewport.h"
#include "scene.h"
#include "sphere.h"
#include <SDL2/SDL.h>

#define GL_GLEXT_PROTOTYPES 1
#include "gl_compat.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define INIT_WINDOW_W 960
#define INIT_WINDOW_H 600
#define FOV (M_PI / 2.8f)

/* Animation knobs for the traversing sphere. Sphere center oscillates
 * along X through disc A, sweeping from "fully behind" to "fully in
 * front" so the viewer can watch the split appear, slide along, and
 * vanish. Both extents lie outside A's disc so the transitions are
 * clearly readable. */
#define TRAVELER_Y         0.5f
#define TRAVELER_Z        -2.0f
#define TRAVELER_RADIUS    0.5f
#define TRAVELER_MIN_X    -3.6f   /* fully behind disc A (A.x = -2) */
#define TRAVELER_MAX_X    -0.4f   /* fully in front of disc A         */
#define TRAVELER_PERIOD    7.0f   /* seconds per back-and-forth */

static int traveler_sphere_idx = -1;

static void build_scene(scene **scene_out, scene_camera **camera_out) {
    scene *sc = scene_create();

    int m_floor = scene_add_material(sc, (scene_material){
        .albedo       = {180, 180, 180},
        .albedo2      = {60,  60,  60},
        .tex_kind     = SCENE_TEX_CHECKER,
        .tex_scale    = 1.0f,
        .portal_index = -1,
    });
    scene_add_plane(sc, (scene_plane){
        .point    = {0.0f, -0.5f, 0.0f},
        .normal   = {0.0f, 1.0f, 0.0f},
        .material = m_floor,
    });

    /* Background reference balls — give the camera something to anchor
     * to so the traveler's motion reads against a stable scene. */
    int m_red   = scene_add_material(sc, (scene_material){
        .albedo = {220,  70,  70}, .portal_index = -1});
    int m_blue  = scene_add_material(sc, (scene_material){
        .albedo = { 70, 120, 220}, .portal_index = -1});
    int m_green = scene_add_material(sc, (scene_material){
        .albedo = { 80, 200, 100}, .portal_index = -1});
    scene_add_sphere(sc, (scene_sphere){
        .center = { 0.8f, -0.1f,  3.5f}, .radius = 0.35f, .material = m_red});
    scene_add_sphere(sc, (scene_sphere){
        .center = { 1.2f,  0.4f, -4.5f}, .radius = 0.30f, .material = m_blue});
    scene_add_sphere(sc, (scene_sphere){
        .center = {-4.0f,  0.2f,  0.5f}, .radius = 0.30f, .material = m_green});

    /* ---- Paired-rigid disc portals A and B. ----
     *
     * Both face +X. Placed at the same X but different Z so:
     *   - The camera can see both discs in a single view (orbit XZ).
     *   - A ray exiting one portal doesn't aim straight at the other
     *     (no degenerate infinite-portal loop).
     *
     * The traveling sphere is aligned with A (same z), oscillating in x.
     * When fully behind A its virtual copy emerges fully in front of B;
     * when fully in front of A the virtual copy is fully behind B and
     * thus clipped away. */
    int disc_A_idx = 0;     /* first disc added below */
    int disc_B_idx = 1;     /* second disc            */

    int portal_A = scene_add_portal(sc, (scene_portal){
        .kind          = SCENE_PORTAL_PAIRED_RIGID,
        .partner_kind  = SCENE_PRIM_DISC,
        .partner_index = disc_B_idx,
    });
    int portal_B = scene_add_portal(sc, (scene_portal){
        .kind          = SCENE_PORTAL_PAIRED_RIGID,
        .partner_kind  = SCENE_PRIM_DISC,
        .partner_index = disc_A_idx,
    });

    int m_portal_A = scene_add_material(sc, (scene_material){
        .albedo = {200,  80, 230}, .portal_index = portal_A});
    int m_portal_B = scene_add_material(sc, (scene_material){
        .albedo = { 80, 200, 230}, .portal_index = portal_B});

    scene_add_disc(sc, (scene_disc){    /* disc index 0 = A */
        .center   = {-2.0f, 0.5f, -2.0f},
        .normal   = { 1.0f, 0.0f,  0.0f},
        .radius   = 1.0f,
        .material = m_portal_A,
    });
    scene_add_disc(sc, (scene_disc){    /* disc index 1 = B */
        .center   = {-2.0f, 0.5f,  2.0f},
        .normal   = { 1.0f, 0.0f,  0.0f},
        .radius   = 1.0f,
        .material = m_portal_B,
    });

    /* Traveling sphere — the star of step 1. Tagged with portal_disc1
     * so the renderer clips it to disc A's front half-space and emits
     * a virtual copy at disc B. */
    int m_traveler = scene_add_material(sc, (scene_material){
        .albedo       = {255, 215,  60},
        .portal_index = -1,
    });
    traveler_sphere_idx = scene_add_sphere(sc, (scene_sphere){
        .center       = {TRAVELER_MIN_X, TRAVELER_Y, TRAVELER_Z},
        .radius       = TRAVELER_RADIUS,
        .material     = m_traveler,
        .portal_disc1 = disc_A_idx + 1,    /* 1-based: 0 = none */
    });

    scene_set_ambient(sc, 0.3f);
    scene_add_light(sc, (scene_light){
        .direction = {0.4f, 0.9f, 0.3f},
        .intensity = 0.8f,
    });

    *scene_out  = sc;
    *camera_out = scene_camera_create(
        (vector){4.5f, 1.4f, 1.0f},
        (vector){-1.0f, -0.2f, -0.3f}
    );
}

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

    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        fprintf(stderr, "SDL init failed: %s\n", SDL_GetError());
        return 1;
    }

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

    int window_w = INIT_WINDOW_W;
    int window_h = INIT_WINDOW_H;
    SDL_Window *window = SDL_CreateWindow("Portals",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        window_w, window_h, SDL_WINDOW_OPENGL);
    if (!window) {
        fprintf(stderr, "Window creation failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    SDL_GLContext gl_ctx = SDL_GL_CreateContext(window);
    if (!gl_ctx) {
        fprintf(stderr, "GL context creation failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    SDL_GL_SetSwapInterval(1);
    gl_compat_init((gl_compat_loader_fn)SDL_GL_GetProcAddress);

    rt_renderer *cpu_rnd = rt_renderer_create(RT_BACKEND_CPU);
    if (!cpu_rnd) {
        fprintf(stderr, "CPU renderer unavailable\n");
        SDL_GL_DeleteContext(gl_ctx);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    scene *sc;
    scene_camera *camera;
    build_scene(&sc, &camera);

    int render_scale = 2;
    int render_w = window_w / render_scale;
    int render_h = window_h / render_scale;
    uint32_t *pixels = calloc((size_t)(render_w * render_h), sizeof(uint32_t));
    rt_viewport viewport = { render_w, render_h, FOV };

    GLuint display_tex, display_fbo;
    glGenTextures(1, &display_tex);
    glBindTexture(GL_TEXTURE_2D, display_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, render_w, render_h, 0,
                 GL_BGRA, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glGenFramebuffers(1, &display_fbo);

    vector cam_pos = {4.5f, 1.4f, 1.0f};
    float cam_yaw = -1.7f;     /* facing -X-ish, toward the portals */
    float cam_pitch = -0.15f;
    float move_speed = 3.0f;
    float look_speed = 1.6f;
    int auto_orbit = 1;
    int running = 1;

    Uint32 fps_last = SDL_GetTicks();
    Uint32 frame_last = SDL_GetTicks();
    Uint32 start_ticks = SDL_GetTicks();
    int fps_frames = 0;
    char title_buf[160];

    while (running) {
        Uint32 frame_now = SDL_GetTicks();
        float dt = (frame_now - frame_last) / 1000.0f;
        frame_last = frame_now;
        float t = (frame_now - start_ticks) / 1000.0f;

        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) running = 0;
            if (e.type == SDL_KEYDOWN) {
                if (e.key.keysym.sym == SDLK_ESCAPE) running = 0;
                if (e.key.keysym.sym == SDLK_SPACE) auto_orbit = !auto_orbit;
            }
        }

        const Uint8 *keys = SDL_GetKeyboardState(NULL);
        int manual_input = keys[SDL_SCANCODE_W] || keys[SDL_SCANCODE_S] ||
                           keys[SDL_SCANCODE_A] || keys[SDL_SCANCODE_D] ||
                           keys[SDL_SCANCODE_LEFT] || keys[SDL_SCANCODE_RIGHT] ||
                           keys[SDL_SCANCODE_UP] || keys[SDL_SCANCODE_DOWN];
        if (manual_input) auto_orbit = 0;

        if (auto_orbit) {
            /* Slow orbit around the portal pair. The orbit center is
             * offset toward +X so the camera spends most of its time
             * on the front side of A/B and we get a clear view of the
             * traversing sphere splitting across A. */
            float a = t * 0.22f;
            float orbit_r = 5.0f;
            cam_pos.x = -1.0f + sinf(a) * orbit_r;
            cam_pos.z =          cosf(a) * orbit_r;
            cam_pos.y = 1.4f + sinf(t * 0.4f) * 0.25f;
            vector look_at = { -2.0f, 0.5f, 0.0f };
            vector dir = vector_normalize(vector_sub(look_at, cam_pos));
            cam_yaw   = atan2f(dir.x, dir.z);
            cam_pitch = asinf(dir.y);
        } else {
            if (keys[SDL_SCANCODE_LEFT])  cam_yaw   -= look_speed * dt;
            if (keys[SDL_SCANCODE_RIGHT]) cam_yaw   += look_speed * dt;
            if (keys[SDL_SCANCODE_UP])    cam_pitch += look_speed * dt;
            if (keys[SDL_SCANCODE_DOWN])  cam_pitch -= look_speed * dt;
            if (cam_pitch >  1.4f) cam_pitch =  1.4f;
            if (cam_pitch < -1.4f) cam_pitch = -1.4f;

            vector forward = { sinf(cam_yaw), 0.0f, cosf(cam_yaw) };
            vector right   = { cosf(cam_yaw), 0.0f, -sinf(cam_yaw) };
            if (keys[SDL_SCANCODE_W]) cam_pos = vector_add(cam_pos, vector_scale(forward,  move_speed * dt));
            if (keys[SDL_SCANCODE_S]) cam_pos = vector_add(cam_pos, vector_scale(forward, -move_speed * dt));
            if (keys[SDL_SCANCODE_D]) cam_pos = vector_add(cam_pos, vector_scale(right,    move_speed * dt));
            if (keys[SDL_SCANCODE_A]) cam_pos = vector_add(cam_pos, vector_scale(right,   -move_speed * dt));
        }

        vector cam_dir = cam_dir_from_yaw_pitch(cam_yaw, cam_pitch);
        scene_camera_place(camera, cam_pos, cam_dir);

        /* Animate the traversing sphere through disc A. Smooth back-and-
         * forth so the viewer can watch the split appear, slide, vanish. */
        if (traveler_sphere_idx >= 0) {
            float omega = 2.0f * (float)M_PI / TRAVELER_PERIOD;
            float u = (sinf(t * omega) + 1.0f) * 0.5f;   /* [0, 1] */
            sc->spheres[traveler_sphere_idx].center.x =
                TRAVELER_MIN_X + (TRAVELER_MAX_X - TRAVELER_MIN_X) * u;
        }

        rt_renderer_render(cpu_rnd, sc, camera, &viewport, pixels, NULL);

        display_pixels(display_tex, display_fbo, pixels,
                       render_w, render_h, window_w, window_h);
        SDL_GL_SwapWindow(window);

        fps_frames++;
        Uint32 now = SDL_GetTicks();
        if (now - fps_last >= 1000) {
            snprintf(title_buf, sizeof(title_buf),
                     "Portals - %d FPS (%dx%d) %s",
                     fps_frames, render_w, render_h,
                     auto_orbit ? "[orbit]" : "[manual]");
            SDL_SetWindowTitle(window, title_buf);
            fps_frames = 0;
            fps_last = now;
        }
    }

    glDeleteFramebuffers(1, &display_fbo);
    glDeleteTextures(1, &display_tex);
    rt_renderer_destroy(cpu_rnd);
    free(pixels);
    scene_camera_destroy(camera);
    scene_destroy(sc);
    SDL_GL_DeleteContext(gl_ctx);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
