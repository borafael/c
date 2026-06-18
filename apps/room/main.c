/* Room — first-person walk-through of a small furnished room.
 *
 * A raytraced room with walls, floor, ceiling, a door, a window,
 * a bed, a desk, and a rug. WASD + mouse look for first-person
 * exploration of a simple game-like interior scene.
 *
 * Controls:
 *   ESC        quit
 *   WASD       walk (yaw-aligned, no pitch)
 *   Mouse      look around (click to capture)
 *   M          toggle mouse capture
 *   B          spawn / collapse the black hole (grows in, then settles)
 *   [ ]        shrink / grow the black hole's mass (r_s)
 *   TAB        toggle CPU / OpenGL backend (OpenGL ignores the hole)
 *   1..4       resolution preset
 *   F11        fullscreen
 */

#include "renderer.h"
#include "viewport.h"
#include "scene.h"
#include "box.h"
#include "sphere.h"
#include "mesh.h"
#include "postfx.h"
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
#define WALK_SPEED 3.0f
#define MOUSE_SENS 0.003f
#define PITCH_LIMIT 1.4f

static const struct { int w, h; const char *name; } PRESETS[] = {
    { 160,  100, "160x100" },
    { 320,  200, "320x200" },
    { 480,  300, "480x300" },
    { 960,  600, "960x600" },
};
#define PRESET_COUNT ((int)(sizeof(PRESETS) / sizeof(PRESETS[0])))
#define PRESET_DEFAULT 1

static vector cam_dir_from_yaw_pitch(float yaw, float pitch) {
    return (vector){
        cosf(pitch) * sinf(yaw),
        sinf(pitch),
        cosf(pitch) * cosf(yaw),
    };
}

static int build_room(scene *s) {
    /* ---- Materials ---- */
    int m_wall = scene_add_material(s, (scene_material){
        .albedo = {210, 205, 195} });
    int m_floor = scene_add_material(s, (scene_material){
        .albedo  = {170, 135, 85},
        .albedo2 = {130, 95,  55},
        .tex_kind = SCENE_TEX_CHECKER,
        .tex_scale = 0.5f });
    int m_ceiling = scene_add_material(s, (scene_material){
        .albedo = {235, 233, 228} });
    int m_door = scene_add_material(s, (scene_material){
        .albedo = {145, 85, 45} });
    int m_doorknob = scene_add_material(s, (scene_material){
        .albedo  = {220, 185, 95} });
    int m_window_glass = scene_add_material(s, (scene_material){
        .albedo  = {30, 30, 32},
        .reflectivity = 0.9f });
    int m_bed_frame = scene_add_material(s, (scene_material){
        .albedo = {95, 60, 30} });
    int m_mattress = scene_add_material(s, (scene_material){
        .albedo = {235, 228, 218} });
    int m_pillow = scene_add_material(s, (scene_material){
        .albedo  = {245, 240, 235},
        .albedo2 = {235, 228, 220},
        .tex_kind = SCENE_TEX_CHECKER,
        .tex_scale = 0.08f });
    int m_desk_top = scene_add_material(s, (scene_material){
        .albedo  = {175, 145, 95},
        .albedo2 = {155, 125, 80},
        .tex_kind = SCENE_TEX_STRIPES,
        .tex_scale = 0.08f });
    int m_desk_leg = scene_add_material(s, (scene_material){
        .albedo = {75, 75, 80} });
    int m_rug = scene_add_material(s, (scene_material){
        .albedo = {170, 60, 45} });
    int m_painting = scene_add_material(s, (scene_material){
        .albedo  = {205, 110, 65},
        .albedo2 = {60, 130, 180},
        .tex_kind = SCENE_TEX_BRICKS,
        .tex_scale = 0.08f });
    int m_lamp_base = scene_add_material(s, (scene_material){
        .albedo = {55, 55, 60} });
    int m_light_bulb = scene_add_material(s, (scene_material){
        .albedo = {255, 245, 220},
        .unlit  = 1 });
    if (m_wall < 0 || m_floor < 0 || m_ceiling < 0 || m_door < 0 ||
        m_doorknob < 0 || m_window_glass < 0 || m_bed_frame < 0 ||
        m_mattress < 0 || m_pillow < 0 || m_desk_top < 0 ||
        m_desk_leg < 0 || m_rug < 0 || m_painting < 0 ||
        m_lamp_base < 0 || m_light_bulb < 0)
        return -1;

    /* Room: 6 x 3 x 6, centered at origin. Walls are 0.1 thick AABBs. */
    /* Floor */
    scene_add_box(s, scene_box_aabb((vector){-3, 0, -3},
                                    (vector){ 3, 0.1f,  3}, m_floor));
    /* Ceiling */
    scene_add_box(s, scene_box_aabb((vector){-3, 3, -3},
                                    (vector){ 3, 3.1f,  3}, m_ceiling));
    /* Back wall (z = -3) */
    scene_add_box(s, scene_box_aabb((vector){-3, 0, -3.1f},
                                    (vector){ 3, 3, -2.9f}, m_wall));
    /* Front wall (z = 3) */
    scene_add_box(s, scene_box_aabb((vector){-3, 0, 2.9f},
                                    (vector){ 3, 3, 3.1f}, m_wall));
    /* Left wall (x = -3) */
    scene_add_box(s, scene_box_aabb((vector){-3.1f, 0, -3},
                                    (vector){-2.9f, 3,  3}, m_wall));
    /* Right wall (x = 3) */
    scene_add_box(s, scene_box_aabb((vector){2.9f, 0, -3},
                                    (vector){3.1f, 3,  3}, m_wall));

    /* Door — centered on the back wall */
    scene_add_box(s, scene_box_aabb((vector){-0.4f, 0, -2.88f},
                                    (vector){ 0.4f, 2.1f, -2.78f}, m_door));
    scene_add_sphere(s, (scene_sphere){
        .center   = {0.4f, 0.95f, -2.83f},
        .radius   = 0.055f,
        .material = m_doorknob });

    /* Window — on the right wall, now a mirror */
    scene_add_box(s, scene_box_aabb((vector){2.88f, 0.8f, -0.2f},
                                    (vector){2.98f, 2.4f,  1.6f}, m_window_glass));

    /* Rug — centre of the floor */
    scene_add_box(s, scene_box_aabb((vector){-1.0f, 0.1f, -1.2f},
                                    (vector){ 1.0f, 0.13f, 1.2f}, m_rug));

    /* Bed — along the left wall, head at back wall */
    scene_add_box(s, scene_box_aabb((vector){-2.85f, 0.1f, -2.5f},
                                    (vector){-1.2f,  0.25f, -0.5f}, m_bed_frame));
    scene_add_box(s, scene_box_aabb((vector){-2.8f, 0.25f, -2.45f},
                                    (vector){-1.25f, 0.55f, -0.55f}, m_mattress));
    scene_add_box(s, scene_box_aabb((vector){-2.7f, 0.55f, -2.35f},
                                    (vector){-2.15f, 0.68f, -1.85f}, m_pillow));

    /* Desk — against the right wall at the back */
    scene_add_box(s, scene_box_aabb((vector){ 1.2f, 1.0f, -2.6f},
                                    (vector){ 2.85f, 1.05f, -1.2f}, m_desk_top));
    scene_add_box(s, scene_box_aabb((vector){ 1.3f, 0.1f, -2.5f},
                                    (vector){ 1.4f, 1.0f, -2.4f}, m_desk_leg));
    scene_add_box(s, scene_box_aabb((vector){ 2.65f, 0.1f, -2.5f},
                                    (vector){ 2.75f, 1.0f, -2.4f}, m_desk_leg));
    scene_add_box(s, scene_box_aabb((vector){ 1.3f, 0.1f, -1.4f},
                                    (vector){ 1.4f, 1.0f, -1.3f}, m_desk_leg));
    scene_add_box(s, scene_box_aabb((vector){ 2.65f, 0.1f, -1.4f},
                                    (vector){ 2.75f, 1.0f, -1.3f}, m_desk_leg));

    /* Painting — on the back wall above the door */
    scene_add_box(s, scene_box_aabb((vector){-0.5f, 1.6f, -2.88f},
                                    (vector){ 0.5f, 2.3f, -2.85f}, m_painting));

    /* Ceiling lamp */
    scene_add_box(s, scene_box_aabb((vector){-0.08f, 2.9f, -0.08f},
                                    (vector){ 0.08f, 2.98f, 0.08f}, m_lamp_base));
    scene_add_sphere(s, (scene_sphere){
        .center   = {0.0f, 2.85f, 0.0f},
        .radius   = 0.12f,
        .material = m_light_bulb });

    /* A black hole hovering in the middle of the room. It starts hidden;
     * main() drives its mass live (B spawns/collapses it, [ ] resize it),
     * easing mass up from 0 so it grows in. Curved-ray tracing is
     * CPU-only — the OpenGL backend ignores black holes (TAB to compare).
     * The initial mass here is just a placeholder; main() overwrites it
     * each frame. */
    scene_add_blackhole(s, (scene_blackhole){
        .center = {0.0f, 1.4f, 0.0f}, .mass = 0.0f });

    /* Lighting */
    scene_set_ambient(s, 0.25f);
    scene_add_light(s, (scene_light){
        .direction = {1.5f, 1.0f, 1.0f},
        .intensity = 0.6f });
    scene_add_light(s, (scene_light){
        .direction = {-0.5f, 0.8f, -0.3f},
        .intensity = 0.3f });

    rt_scene_build_accel(s);
    return 0;
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
    rt_backend preferred = RT_BACKEND_CPU;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-G") == 0 || strcmp(argv[i], "--gpu") == 0)
            preferred = RT_BACKEND_OPENGL;
        else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            printf("Usage: %s [options]\n", argv[0]);
            printf("  -G, --gpu    Start with OpenGL raytrace backend\n");
            printf("  -h, --help   Show this help\n");
            printf("\nWASD walk, mouse look, TAB toggle backend, M toggle mouse capture.\n");
            printf("B spawn/collapse the black hole, [ ] shrink/grow it.\n");
            return 0;
        }
    }

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
    SDL_Window *window = SDL_CreateWindow("Room",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        window_w, window_h, SDL_WINDOW_OPENGL);
    if (!window) {
        fprintf(stderr, "Window: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }
    SDL_GLContext gl_ctx = SDL_GL_CreateContext(window);
    if (!gl_ctx) {
        fprintf(stderr, "GL context: %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    SDL_GL_SetSwapInterval(0);
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
    fprintf(stderr, "Renderer: %s (TAB to toggle)\n", rt_renderer_name(active));

    scene *scn = scene_create();
    if (!scn || build_room(scn) < 0) {
        fprintf(stderr, "Failed to build room scene\n");
        scene_destroy(scn);
        if (cpu_rnd) rt_renderer_destroy(cpu_rnd);
        if (gpu_rnd) rt_renderer_destroy(gpu_rnd);
        SDL_GL_DeleteContext(gl_ctx);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    int preset = PRESET_DEFAULT;
    int render_w = PRESETS[preset].w;
    int render_h = PRESETS[preset].h;
    rt_viewport viewport = { render_w, render_h, FOV };
    uint32_t *pixels = calloc((size_t)(render_w * render_h), sizeof(uint32_t));

    postfx_vignette vig_cfg = { .enabled = 1, .intensity = 0.30f, .softness = 0.55f };

    GLuint display_tex, display_fbo;
    glGenTextures(1, &display_tex);
    glBindTexture(GL_TEXTURE_2D, display_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, render_w, render_h, 0,
                 GL_BGRA, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glGenFramebuffers(1, &display_fbo);

    scene_camera *cam = scene_camera_create(
        (vector){0.0f, 1.2f, 2.0f},
        (vector){0.0f, 0.0f, -1.0f});
    vector cam_pos = {0.0f, 1.2f, 2.0f};
    float cam_yaw = -(float)M_PI_2;
    float cam_pitch = 0.0f;
    int mouse_captured = 1;
    SDL_SetRelativeMouseMode(SDL_TRUE);

    /* Black hole growth animation. mass is eased toward bh_target each
     * frame, so toggling/dialing the target makes the hole grow or
     * shrink smoothly (mass == 0 → zero deflection, i.e. "not there").
     * build_room added one hole; we drive its mass live and flip
     * blackhole_count off whenever the mass eases back to ~0 so the
     * straight path is taken (no marching cost) when the hole is gone. */
    int   saved_blackhole_count = scn->blackhole_count;
    float bh_mass   = 0.0f;        /* current, eased */
    float bh_target = 0.0f;        /* commanded; B/[/] change it */
    const float BH_FULL = 0.12f;   /* default "fully grown" mass */
    const float BH_EASE = 2.5f;    /* growth rate (per second) */
    scn->blackhole_count = 0;      /* starts hidden until spawned */

    int running = 1;
    Uint32 start_ticks = SDL_GetTicks();
    Uint32 frame_last = start_ticks;
    Uint32 fps_last = start_ticks;
    int fps_frames = 0;
    Uint32 render_ms_accum = 0;
    char title_buf[160];

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
                if (k == SDLK_b) {
                    /* Spawn (grow in) if gone, else collapse (shrink out). */
                    bh_target = (bh_target > 0.0f) ? 0.0f : BH_FULL;
                    fprintf(stderr, "Black hole: %s%s\n",
                            bh_target > 0.0f ? "growing" : "collapsing",
                            (bh_target > 0.0f && active == gpu_rnd)
                                ? " (CPU backend only — TAB to lens)" : "");
                }
                if (k == SDLK_RIGHTBRACKET) bh_target += 0.03f;  /* bigger */
                if (k == SDLK_LEFTBRACKET) {                     /* smaller */
                    bh_target -= 0.03f;
                    if (bh_target < 0.0f) bh_target = 0.0f;
                }
                if (bh_target > 0.5f) bh_target = 0.5f;          /* r_s = 1.0 */
                if (k == SDLK_TAB) {
                    if (active == cpu_rnd && gpu_rnd) active = gpu_rnd;
                    else if (active == gpu_rnd && cpu_rnd) active = cpu_rnd;
                    fprintf(stderr, "Renderer: %s\n", rt_renderer_name(active));
                }
                if (k == SDLK_F11) {
                    fullscreen = !fullscreen;
                    SDL_SetWindowFullscreen(window,
                        fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
                    SDL_GetWindowSize(window, &window_w, &window_h);
                }
                if (k >= SDLK_1 && k <= SDLK_4) {
                    int idx = k - SDLK_1;
                    if (idx < PRESET_COUNT) {
                        preset = idx;
                        render_w = PRESETS[preset].w;
                        render_h = PRESETS[preset].h;
                        free(pixels);
                        pixels = calloc((size_t)(render_w * render_h), sizeof(uint32_t));
                        viewport = (rt_viewport){ render_w, render_h, FOV };
                        glBindTexture(GL_TEXTURE_2D, display_tex);
                        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8,
                                     render_w, render_h, 0,
                                     GL_BGRA, GL_UNSIGNED_BYTE, NULL);
                        fprintf(stderr, "Preset: %s (%dx%d)\n",
                                PRESETS[preset].name, render_w, render_h);
                    }
                }
            }
            if (e.type == SDL_MOUSEMOTION && mouse_captured) {
                cam_yaw   += e.motion.xrel * MOUSE_SENS;
                cam_pitch -= e.motion.yrel * MOUSE_SENS;
            }
        }

        const Uint8 *keys = SDL_GetKeyboardState(NULL);
        if (cam_pitch >  PITCH_LIMIT) cam_pitch =  PITCH_LIMIT;
        if (cam_pitch < -PITCH_LIMIT) cam_pitch = -PITCH_LIMIT;

        vector fwd   = { sinf(cam_yaw), 0.0f, cosf(cam_yaw) };
        vector right = { cosf(cam_yaw), 0.0f, -sinf(cam_yaw) };
        float v = WALK_SPEED * dt;
        if (keys[SDL_SCANCODE_W]) cam_pos = vector_add(cam_pos, vector_scale(fwd,    v));
        if (keys[SDL_SCANCODE_S]) cam_pos = vector_add(cam_pos, vector_scale(fwd,   -v));
        if (keys[SDL_SCANCODE_D]) cam_pos = vector_add(cam_pos, vector_scale(right,  v));
        if (keys[SDL_SCANCODE_A]) cam_pos = vector_add(cam_pos, vector_scale(right, -v));

        /* Clamp to room bounds so the camera can't walk through walls */
        float margin = 0.4f;
        if (cam_pos.x < -3.0f + margin) cam_pos.x = -3.0f + margin;
        if (cam_pos.x >  3.0f - margin) cam_pos.x =  3.0f - margin;
        if (cam_pos.z < -3.0f + margin) cam_pos.z = -3.0f + margin;
        if (cam_pos.z >  3.0f - margin) cam_pos.z =  3.0f - margin;
        cam_pos.y = 1.2f;

        vector cam_dir = cam_dir_from_yaw_pitch(cam_yaw, cam_pitch);
        scene_camera_place(cam, cam_pos, cam_dir);

        /* Ease the hole's mass toward the commanded target (exponential,
         * so it pops in fast then settles), then update the live scene. */
        float ease = dt * BH_EASE;
        if (ease > 1.0f) ease = 1.0f;
        bh_mass += (bh_target - bh_mass) * ease;
        if (bh_target <= 0.0f && bh_mass < 1e-4f) bh_mass = 0.0f;
        scn->blackholes[0].mass = bh_mass;
        scn->blackhole_count = (bh_mass > 1e-3f) ? saved_blackhole_count : 0;

        Uint32 r_start = SDL_GetTicks();
        rt_renderer_render(active, scn, cam, &viewport, pixels, NULL);
        render_ms_accum += SDL_GetTicks() - r_start;

        postfx_vignette_apply(pixels, render_w, render_h, &vig_cfg);

        display_pixels(display_tex, display_fbo, pixels,
                       render_w, render_h, window_w, window_h);
        SDL_GL_SwapWindow(window);

        fps_frames++;
        Uint32 now = SDL_GetTicks();
        if (now - fps_last >= 1000) {
            float avg_ms = fps_frames ? (float)render_ms_accum / (float)fps_frames : 0.0f;
            snprintf(title_buf, sizeof(title_buf),
                     "Room - %s %dx%d  %d FPS (%.1fms)  BH r_s=%.2f %s",
                     rt_renderer_name(active), render_w, render_h,
                     fps_frames, avg_ms, 2.0f * bh_mass,
                     mouse_captured ? "[m]" : "");
            SDL_SetWindowTitle(window, title_buf);
            fps_frames = 0;
            render_ms_accum = 0;
            fps_last = now;
        }
    }

    glDeleteFramebuffers(1, &display_fbo);
    glDeleteTextures(1, &display_tex);
    if (cpu_rnd) rt_renderer_destroy(cpu_rnd);
    if (gpu_rnd) rt_renderer_destroy(gpu_rnd);
    free(pixels);
    scene_camera_destroy(cam);
    scene_destroy(scn);
    SDL_GL_DeleteContext(gl_ctx);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
