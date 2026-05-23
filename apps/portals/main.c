/* Portals: paired-PARAMETRIC mismatched-shape demo.
 *
 * A disc on one side, a sphere on the other, paired with
 * SCENE_PORTAL_PAIRED_PARAMETRIC. The correspondence is "same normalized
 * (u, v) on the partner" — disc (u, v) ∈ [-1, 1] over its diameter maps
 * to sphere (phi/PI, 2*theta/PI - 1) over its lat/long. Looking at the
 * disc, you see the world *through the sphere's surface* — every point
 * on the flat disc is a window onto a different angular direction from
 * the sphere's exit point. Looking at the sphere, you see the disc's
 * flat scene wrapped around the sphere's curvature.
 *
 * Visually unsettling because the mapping isn't a rigid motion — it's a
 * topological reparameterization. Two shapes with completely different
 * surface structures, glued together at their UV parameterizations.
 *
 * Camera orbits in the corridor between them.
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

static void build_scene(scene **scene_out, scene_camera **camera_out) {
    scene *sc = scene_create();

    /* Floor (low plane just below origin). */
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

    /* Corridor furniture. Each recursive portal bounce passes a ray
     * through the corridor between A and B; without scene content along
     * those paths the center of each portal goes black once
     * RT_MAX_BOUNCES is exhausted (every iteration is a portal hit, no
     * non-portal contribution ever accumulates). These objects break up
     * that dark tunnel by giving off-axis recursive rays something to
     * hit. They sit outside the camera's auto-orbit radius and below
     * camera height, so they don't obstruct the direct view of either
     * portal. */
    int m_red    = scene_add_material(sc, (scene_material){
        .albedo = {220,  70,  70}, .portal_index = -1,
    });
    int m_blue   = scene_add_material(sc, (scene_material){
        .albedo = { 70, 120, 220}, .portal_index = -1,
    });
    int m_green  = scene_add_material(sc, (scene_material){
        .albedo = { 80, 200, 100}, .portal_index = -1,
    });
    int m_orange = scene_add_material(sc, (scene_material){
        .albedo = {240, 160,  60}, .portal_index = -1,
    });
    int m_violet = scene_add_material(sc, (scene_material){
        .albedo = {170,  90, 220}, .portal_index = -1,
    });

    /* Two reference balls on the corridor floor, z-offset from the
     * portal axis so they read as "depth markers" without blocking
     * the central portal pixels. */
    scene_add_sphere(sc, (scene_sphere){
        .center = { 1.5f, -0.15f, -3.2f}, .radius = 0.35f, .material = m_red,
    });
    scene_add_sphere(sc, (scene_sphere){
        .center = {-1.5f, -0.15f, -0.8f}, .radius = 0.35f, .material = m_blue,
    });

    /* Three smaller balls at portal height between the discs, also
     * z-offset so they don't sit on the direct A↔B line. Recursive
     * rays that drift in z catch one of these and stop the recursion
     * with a coloured contribution instead of black. */
    scene_add_sphere(sc, (scene_sphere){
        .center = { 0.6f, 0.5f, -3.5f}, .radius = 0.22f, .material = m_green,
    });
    scene_add_sphere(sc, (scene_sphere){
        .center = {-0.6f, 0.5f, -0.5f}, .radius = 0.22f, .material = m_orange,
    });
    scene_add_sphere(sc, (scene_sphere){
        .center = { 0.0f, 1.2f, -2.0f}, .radius = 0.22f, .material = m_violet,
    });

    /* ---- Disc (left) ↔ Sphere (right), paired parametrically. ----
     *
     * Ordering note: portal records reference partners BY INDEX. The
     * corridor furniture spheres above occupy sphere indices 0..4; the
     * portal sphere will get sphere index 5. The portal disc will get
     * disc index 0 (no other discs in this scene). We assert those
     * indices ahead of time so the portal records can name each other.
     */
    int disc_A_idx   = 0;
    int sphere_B_idx = 5;

    /* Portal on the disc points at the sphere; portal on the sphere
     * points back at the disc — a two-way mismatched pair. */
    int portal_A = scene_add_portal(sc, (scene_portal){
        .kind          = SCENE_PORTAL_PAIRED_PARAMETRIC,
        .partner_kind  = SCENE_PRIM_SPHERE,
        .partner_index = sphere_B_idx,
    });
    int portal_B = scene_add_portal(sc, (scene_portal){
        .kind          = SCENE_PORTAL_PAIRED_PARAMETRIC,
        .partner_kind  = SCENE_PRIM_DISC,
        .partner_index = disc_A_idx,
    });

    int m_portal_A = scene_add_material(sc, (scene_material){
        .albedo = {255, 0, 255}, .portal_index = portal_A,
    });
    int m_portal_B = scene_add_material(sc, (scene_material){
        .albedo = {255, 0, 255}, .portal_index = portal_B,
    });

    /* Disc A on the left, normal facing right toward the sphere. */
    scene_add_disc(sc, (scene_disc){      /* index disc_A_idx (0) */
        .center   = {-3.0f, 0.5f, -2.0f},
        .normal   = { 1.0f, 0.0f,  0.0f},
        .radius   = 1.0f,
        .material = m_portal_A,
    });
    /* Sphere B on the right. */
    scene_add_sphere(sc, (scene_sphere){      /* index sphere_B_idx (5) */
        .center   = { 3.0f, 0.5f, -2.0f},
        .radius   = 1.0f,
        .material = m_portal_B,
    });

    /* Lights. */
    scene_set_ambient(sc, 0.3f);
    scene_add_light(sc, (scene_light){
        .direction = {0.4f, 0.9f, 0.3f},
        .intensity = 0.8f,
    });

    *scene_out  = sc;
    *camera_out = scene_camera_create(
        (vector){0.0f, 0.8f, 0.5f},
        (vector){-1.0f, 0.0f, -0.4f}
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

    vector cam_pos = {0.5f, 0.7f, -2.0f};
    float cam_yaw = -(float)M_PI_2;     /* facing -X toward portal A */
    float cam_pitch = 0.0f;
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
            /* Small orbit in the corridor between the two portals, so we
             * alternately look at A (when on the +X side) and B (when on
             * the -X side). Pan-target also swings slightly off the
             * midpoint so we get oblique angles, not just straight-on. */
            float a = t * 0.45f;
            float orbit_r = 1.2f;
            cam_pos.x = sinf(a) * orbit_r;
            cam_pos.z = -2.0f + cosf(a) * orbit_r * 0.6f;
            cam_pos.y = 0.7f + sinf(t * 0.6f) * 0.2f;
            /* Look toward whichever portal is "far" along X. */
            vector look_at = { -copysignf(3.0f, cam_pos.x), 0.5f, -2.0f };
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
