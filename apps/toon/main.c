/* Toon (cel-shading) raytrace demo.
 *
 * Stacks two postfx passes on top of the raytracer:
 *   1. postfx_toon         - quantize lighting bands using G-buffer normals
 *   2. postfx_apply_edges  - black outlines from the same G-buffer
 *
 * Together they recreate the Borderlands / Wind Waker / Jet Set Radio
 * look. Materials are diffuse-only, no reflective surfaces, so the
 * banded multiplier doesn't fight any specular contribution.
 *
 * The toon light direction is wired to the same vector as the scene
 * key light, so the bands line up with the geometric shading.
 *
 * Controls:
 *   ESC          quit
 *   TAB          toggle CPU / OpenGL backend
 *   F11          fullscreen
 *   1..4         resolution preset
 *   T            toggle toon banding
 *   O            toggle outlines
 *   - / =        fewer / more lighting bands (2..6)
 *   [ / ]        thinner / thicker outlines (4 vs 8 connected)
 *   R            toggle rim light
 *   WASD/space/shift  fly camera; arrows look around
 */

#include "renderer.h"
#include "viewport.h"
#include "scene.h"
#include "sphere.h"
#include "plane.h"
#include "box.h"
#include "cylinder.h"
#include "obj.h"
#include "mesh.h"
#include "postfx.h"
#include <SDL2/SDL.h>

#define GL_GLEXT_PROTOTYPES 1
#include "gl_compat.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <float.h>

#define INIT_WINDOW_W 960
#define INIT_WINDOW_H 540
#define FOV (M_PI / 3.0f)

/* The same direction used for the scene's key light and for the toon
 * banding — keeping these in sync makes the bands feel "right". */
static const vector LIGHT_DIR = { 1.0f, 1.2f, -0.8f };

typedef struct { int w, h; const char *name; } pixel_preset;
static const pixel_preset PRESETS[] = {
    { 320, 180, "320x180" },
    { 480, 270, "480x270" },
    { 640, 360, "640x360" },
    { 960, 540, "960x540" },
};
#define PRESET_COUNT ((int)(sizeof(PRESETS) / sizeof(PRESETS[0])))
#define PRESET_DEFAULT 2

static const char *VALKYRIE_OBJ_CANDIDATES[] = {
    "apps/mech/assets/valkyrie.obj",
    "../mech/assets/valkyrie.obj",
    "./valkyrie.obj",
};
static const char *VALKYRIE_MTL_CANDIDATES[] = {
    "apps/mech/assets/valkyrie.mtl",
    "../mech/assets/valkyrie.mtl",
    "./valkyrie.mtl",
};

static int try_load_valkyrie(scene *s, int default_mat,
                             vector offset, float uniform_scale) {
    int n = (int)(sizeof(VALKYRIE_OBJ_CANDIDATES) /
                  sizeof(VALKYRIE_OBJ_CANDIDATES[0]));
    for (int i = 0; i < n; i++) {
        FILE *probe = fopen(VALKYRIE_OBJ_CANDIDATES[i], "rb");
        if (!probe) continue;
        fclose(probe);
        scene_mtl_entry *mtl = NULL;
        int mtl_n = scene_load_mtl(VALKYRIE_MTL_CANDIDATES[i], &mtl);
        if (mtl_n < 0) { mtl_n = 0; mtl = NULL; }
        int first = 0;
        int added = scene_add_meshes_from_obj(s, VALKYRIE_OBJ_CANDIDATES[i],
                                              mtl, mtl_n, default_mat, &first);
        free(mtl);
        if (added <= 0) return 0;
        for (int k = 0; k < added; k++) {
            scene_mesh *m = &s->meshes[first + k];
            for (int v = 0; v < m->vertex_count; v++) {
                m->vertices[v].position.x = m->vertices[v].position.x * uniform_scale + offset.x;
                m->vertices[v].position.y = m->vertices[v].position.y * uniform_scale + offset.y;
                m->vertices[v].position.z = m->vertices[v].position.z * uniform_scale + offset.z;
            }
            scene_mesh_compute_bounds(m);
        }
        fprintf(stderr, "Loaded Valkyrie from %s (%d mesh groups)\n",
                VALKYRIE_OBJ_CANDIDATES[i], added);
        return 1;
    }
    fprintf(stderr, "warning: valkyrie.obj not found in any candidate path\n");
    return 0;
}

static void build_scene(scene **scn, scene_camera **cam) {
    *scn = scene_create();

    /* Diffuse-only materials: no reflectivity, no procedural noise.
     * Toon banding looks crisp on flat colour and turns to mush on
     * noisy textures. */
    int m_red    = scene_add_material(*scn, (scene_material){ .albedo = {220,  60,  60} });
    int m_green  = scene_add_material(*scn, (scene_material){ .albedo = { 80, 180,  80} });
    int m_blue   = scene_add_material(*scn, (scene_material){ .albedo = { 60, 110, 200} });
    int m_yellow = scene_add_material(*scn, (scene_material){ .albedo = {230, 200,  60} });
    int m_floor  = scene_add_material(*scn, (scene_material){
        .albedo  = {180, 180, 200},
        .albedo2 = {120, 120, 150},
        .tex_kind = SCENE_TEX_CHECKER,
        .tex_scale = 1.0f,
    });
    int m_paint  = scene_add_material(*scn, (scene_material){
        .albedo = {200, 200, 215},
    });

    scene_add_sphere(*scn, (scene_sphere){
        .center = {0.0f, 1.0f, 0.0f}, .radius = 1.0f, .material = m_red });
    scene_add_sphere(*scn, (scene_sphere){
        .center = {-2.4f, 0.6f, -0.5f}, .radius = 0.6f, .material = m_green });
    scene_add_sphere(*scn, (scene_sphere){
        .center = {2.0f, 0.8f, -1.0f}, .radius = 0.8f, .material = m_blue });

    scene_add_box(*scn, (scene_box){
        .min = {-3.5f, -0.5f, 1.5f}, .max = {-2.5f, 0.8f, 2.5f},
        .material = m_yellow });
    scene_add_cylinder(*scn, (scene_cylinder){
        .center = {2.5f, 0.5f, 2.0f}, .axis = {0.0f, 1.0f, 0.0f},
        .radius = 0.45f, .half_height = 1.0f, .material = m_yellow });

    scene_add_plane(*scn, (scene_plane){
        .point = {0.0f, -0.5f, 0.0f}, .normal = {0.0f, 1.0f, 0.0f},
        .material = m_floor });

    try_load_valkyrie(*scn, m_paint,
                      (vector){0.0f, 0.07f, -2.5f}, 3.0f);

    scene_set_ambient(*scn, 0.25f);
    scene_add_light(*scn, (scene_light){
        .direction = LIGHT_DIR, .intensity = 0.85f });

    rt_scene_build_accel(*scn);

    *cam = scene_camera_create(
        (vector){4.5f, 2.5f, 5.0f},
        (vector){-1.0f, -0.4f, -1.0f});
}

static vector cam_dir_from_yaw_pitch(float yaw, float pitch) {
    return (vector){
        cosf(pitch) * sinf(yaw),
        sinf(pitch),
        cosf(pitch) * cosf(yaw),
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
    int fullscreen = 0;
    SDL_Window *window = SDL_CreateWindow("Toon Raytrace",
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
    SDL_GL_SetSwapInterval(0);
    gl_compat_init((gl_compat_loader_fn)SDL_GL_GetProcAddress);

    fprintf(stderr, "GL version: %s\n", (const char *)glGetString(GL_VERSION));

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
    rt_renderer *active = gpu_rnd ? gpu_rnd : cpu_rnd;
    fprintf(stderr, "Active: %s (TAB to toggle)\n", rt_renderer_name(active));

    scene *scn;
    scene_camera *cam;
    build_scene(&scn, &cam);

    int preset = PRESET_DEFAULT;
    int render_w = PRESETS[preset].w;
    int render_h = PRESETS[preset].h;

    postfx_toon toon_cfg = {
        .enabled      = 1,
        .bands        = 3,
        .light_x      = LIGHT_DIR.x,
        .light_y      = LIGHT_DIR.y,
        .light_z      = LIGHT_DIR.z,
        .ambient      = 0.35f,
        .rim_strength = 0.0f,
    };
    postfx_edges edges_cfg = {
        .use_object_id    = 1,
        .use_depth        = 1,
        .use_normal       = 1,
        .eight_connected  = 0,
        .depth_threshold  = 1.0f,
        .normal_threshold = 0.65f,
    };
    int outlines_on = 1;

    uint32_t *pixels = calloc((size_t)(render_w * render_h), sizeof(uint32_t));
    rt_gbuffer gbuf = {
        .object_id = calloc((size_t)(render_w * render_h), sizeof(uint32_t)),
        .depth     = calloc((size_t)(render_w * render_h), sizeof(float)),
        .normal    = calloc((size_t)(render_w * render_h * 3), sizeof(float)),
    };
    rt_viewport viewport = { render_w, render_h, FOV };

    GLuint display_tex, display_fbo;
    glGenTextures(1, &display_tex);
    glBindTexture(GL_TEXTURE_2D, display_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, render_w, render_h, 0,
                 GL_BGRA, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glGenFramebuffers(1, &display_fbo);

    vector cam_pos = {4.5f, 2.5f, 5.0f};
    float cam_yaw = -2.356f;
    float cam_pitch = -0.3f;
    float move_speed = 5.0f;
    float look_speed = 2.0f;
    int running = 1;

    Uint32 fps_last = SDL_GetTicks();
    Uint32 frame_last = SDL_GetTicks();
    int fps_frames = 0;
    Uint32 render_ms_accum = 0;
    Uint32 toon_ms_accum = 0;
    Uint32 edge_ms_accum = 0;
    char title_buf[200];

    postfx_gbuffer pg = {
        .object_id = gbuf.object_id,
        .depth     = gbuf.depth,
        .normal    = gbuf.normal,
    };

    while (running) {
        Uint32 frame_now = SDL_GetTicks();
        float dt = (frame_now - frame_last) / 1000.0f;
        frame_last = frame_now;

        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) running = 0;
            if (e.type == SDL_KEYDOWN) {
                SDL_Keycode k = e.key.keysym.sym;
                if (k == SDLK_ESCAPE) running = 0;
                if (k == SDLK_TAB) {
                    if (active == cpu_rnd && gpu_rnd) active = gpu_rnd;
                    else if (active == gpu_rnd && cpu_rnd) active = cpu_rnd;
                    fprintf(stderr, "Active: %s\n", rt_renderer_name(active));
                }
                if (k == SDLK_t) {
                    toon_cfg.enabled = !toon_cfg.enabled;
                    fprintf(stderr, "Toon: %s\n", toon_cfg.enabled ? "on" : "off");
                }
                if (k == SDLK_o) {
                    outlines_on = !outlines_on;
                    fprintf(stderr, "Outlines: %s\n", outlines_on ? "on" : "off");
                }
                if (k == SDLK_MINUS || k == SDLK_KP_MINUS) {
                    if (toon_cfg.bands > 2) toon_cfg.bands--;
                    fprintf(stderr, "Bands: %d\n", toon_cfg.bands);
                }
                if (k == SDLK_EQUALS || k == SDLK_KP_PLUS) {
                    if (toon_cfg.bands < 6) toon_cfg.bands++;
                    fprintf(stderr, "Bands: %d\n", toon_cfg.bands);
                }
                if (k == SDLK_LEFTBRACKET) {
                    edges_cfg.eight_connected = 0;
                    fprintf(stderr, "Outlines: 4-connected\n");
                }
                if (k == SDLK_RIGHTBRACKET) {
                    edges_cfg.eight_connected = 1;
                    fprintf(stderr, "Outlines: 8-connected\n");
                }
                if (k == SDLK_r) {
                    toon_cfg.rim_strength = (toon_cfg.rim_strength > 0.01f) ? 0.0f : 0.4f;
                    fprintf(stderr, "Rim light: %.2f\n", toon_cfg.rim_strength);
                }
                if (k >= SDLK_1 && k <= SDLK_4) {
                    int idx = k - SDLK_1;
                    if (idx < PRESET_COUNT) {
                        preset = idx;
                        goto recreate_buffers;
                    }
                }
                if (k == SDLK_F11) {
                    fullscreen = !fullscreen;
                    SDL_SetWindowFullscreen(window,
                        fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
                    SDL_GetWindowSize(window, &window_w, &window_h);
                }
                continue;
                recreate_buffers:
                    render_w = PRESETS[preset].w;
                    render_h = PRESETS[preset].h;
                    free(pixels);
                    free(gbuf.object_id);
                    free(gbuf.depth);
                    free(gbuf.normal);
                    pixels         = calloc((size_t)(render_w * render_h),     sizeof(uint32_t));
                    gbuf.object_id = calloc((size_t)(render_w * render_h),     sizeof(uint32_t));
                    gbuf.depth     = calloc((size_t)(render_w * render_h),     sizeof(float));
                    gbuf.normal    = calloc((size_t)(render_w * render_h * 3), sizeof(float));
                    pg.object_id = gbuf.object_id;
                    pg.depth     = gbuf.depth;
                    pg.normal    = gbuf.normal;
                    viewport = (rt_viewport){ render_w, render_h, FOV };
                    glBindTexture(GL_TEXTURE_2D, display_tex);
                    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, render_w, render_h, 0,
                                 GL_BGRA, GL_UNSIGNED_BYTE, NULL);
                    fprintf(stderr, "Preset: %s (%dx%d)\n",
                            PRESETS[preset].name, render_w, render_h);
            }
        }

        const Uint8 *keys = SDL_GetKeyboardState(NULL);
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
        if (keys[SDL_SCANCODE_SPACE])  cam_pos.y += move_speed * dt;
        if (keys[SDL_SCANCODE_LSHIFT]) cam_pos.y -= move_speed * dt;

        scene_camera_place(cam, cam_pos, cam_dir_from_yaw_pitch(cam_yaw, cam_pitch));

        Uint32 r_start = SDL_GetTicks();
        rt_renderer_render(active, scn, cam, &viewport, pixels, &gbuf);
        Uint32 r_done = SDL_GetTicks();
        postfx_toon_apply(pixels, &pg, render_w, render_h, &toon_cfg);
        Uint32 t_done = SDL_GetTicks();
        if (outlines_on) {
            postfx_apply_edges(pixels, &pg, render_w, render_h, &edges_cfg);
        }
        Uint32 e_done = SDL_GetTicks();
        render_ms_accum += r_done - r_start;
        toon_ms_accum   += t_done - r_done;
        edge_ms_accum   += e_done - t_done;

        display_pixels(display_tex, display_fbo, pixels,
                       render_w, render_h, window_w, window_h);
        SDL_GL_SwapWindow(window);

        fps_frames++;
        Uint32 now = SDL_GetTicks();
        if (now - fps_last >= 1000) {
            float avg_render = (fps_frames > 0) ? (float)render_ms_accum / (float)fps_frames : 0.0f;
            float avg_toon   = (fps_frames > 0) ? (float)toon_ms_accum   / (float)fps_frames : 0.0f;
            float avg_edge   = (fps_frames > 0) ? (float)edge_ms_accum   / (float)fps_frames : 0.0f;
            snprintf(title_buf, sizeof(title_buf),
                     "Toon Raytrace - %s %s toon=%s b=%d rim=%.2f outlines=%s %d FPS (rt=%.1fms toon=%.1fms edge=%.1fms)",
                     rt_renderer_name(active), PRESETS[preset].name,
                     toon_cfg.enabled ? "on" : "off",
                     toon_cfg.bands, toon_cfg.rim_strength,
                     outlines_on ? "on" : "off",
                     fps_frames, avg_render, avg_toon, avg_edge);
            SDL_SetWindowTitle(window, title_buf);
            fprintf(stderr, "[%s %s toon=%d b=%d rim=%.2f outlines=%d] %d FPS, rt=%.1fms toon=%.1fms edge=%.1fms\n",
                    rt_renderer_name(active), PRESETS[preset].name,
                    toon_cfg.enabled, toon_cfg.bands, toon_cfg.rim_strength,
                    outlines_on,
                    fps_frames, avg_render, avg_toon, avg_edge);
            fps_frames = 0;
            render_ms_accum = 0;
            toon_ms_accum = 0;
            edge_ms_accum = 0;
            fps_last = now;
        }
    }

    glDeleteFramebuffers(1, &display_fbo);
    glDeleteTextures(1, &display_tex);
    if (cpu_rnd) rt_renderer_destroy(cpu_rnd);
    if (gpu_rnd) rt_renderer_destroy(gpu_rnd);
    free(pixels);
    free(gbuf.object_id);
    free(gbuf.depth);
    free(gbuf.normal);
    scene_camera_destroy(cam);
    scene_destroy(scn);
    SDL_GL_DeleteContext(gl_ctx);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
