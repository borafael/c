/* Dungeon — first-person walk-through of a procedural dungeon maze.
 *
 * A raytraced dungeon with moldy stone walls, floor, ceiling, torches,
 * pillars, a reflective pool, and a treasure chamber. WASD + mouse look
 * for first-person exploration.
 *
 * Controls:
 *   ESC        quit
 *   WASD       walk (yaw-aligned, no pitch)
 *   Mouse      look around (click to capture)
 *   M          toggle mouse capture
 *   TAB        toggle CPU / OpenGL backend
 *   1..4       resolution preset
 *   F11        fullscreen
 */

#include "renderer.h"
#include "viewport.h"
#include "scene.h"
#include "box.h"
#include "sphere.h"
#include "cylinder.h"
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
#define FOV ((float)M_PI / 3.0f)
#define WALK_SPEED 2.5f
#define MOUSE_SENS 0.003f
#define PITCH_LIMIT 1.4f
#define CELL_SIZE 2.0f
#define HALF_CELL (CELL_SIZE * 0.5f)
#define WALL_HEIGHT 3.0f
#define EYE_HEIGHT 1.4f
#define DUNGEON_COLS 11
#define DUNGEON_ROWS 11
#define ORIGIN_OFFSET ((DUNGEON_COLS - 1) * 0.5f)

static const struct { int w, h; const char *name; } PRESETS[] = {
    { 160,  100, "160x100" },
    { 320,  200, "320x200" },
    { 480,  300, "480x300" },
    { 960,  600, "960x600" },
};
#define PRESET_COUNT ((int)(sizeof(PRESETS) / sizeof(PRESETS[0])))
#define PRESET_DEFAULT 2

static const char MAP[DUNGEON_ROWS][DUNGEON_COLS] = {
    {1,1,1,1,1,1,1,1,1,1,1},
    {1,0,0,0,1,0,0,0,0,0,1},
    {1,0,1,0,1,0,1,1,1,0,1},
    {1,0,1,0,0,0,0,0,1,0,1},
    {1,0,1,1,1,0,1,0,0,0,1},
    {1,0,0,0,0,0,1,0,1,0,1},
    {1,0,1,0,1,0,1,0,1,0,1},
    {1,0,1,0,0,0,0,0,1,0,1},
    {1,0,1,1,1,1,0,1,1,0,1},
    {1,0,0,0,0,0,0,0,0,0,1},
    {1,1,1,1,1,1,1,1,1,1,1},
};

static const char *WALL_TEX_PATH  = "apps/dungeon/textures/rock_wall_12_diff_4k.jpg";
static const char *FLOOR_TEX_PATH = "apps/dungeon/textures/stone_embedded_concrete_diff_4k.jpg";

static const struct { int r, c; } TORCHES[] = {
    {1,4}, {2,2}, {3,8}, {4,6}, {5,6}, {6,2}, {6,8}, {7,2},
};
#define NUM_TORCHES ((int)(sizeof(TORCHES) / sizeof(TORCHES[0])))

static float col_to_x(int c) {
    return ((float)c - ORIGIN_OFFSET) * CELL_SIZE;
}
static float row_to_z(int r) {
    return ((float)r - ORIGIN_OFFSET) * CELL_SIZE;
}

static int world_to_cell(float w) {
    return (int)roundf((w / CELL_SIZE) + ORIGIN_OFFSET);
}

static int is_blocked(float x, float z) {
    float margin = 0.35f;
    for (int i = -1; i <= 1; i += 2) {
        for (int j = -1; j <= 1; j += 2) {
            int c = world_to_cell(x + (float)i * margin);
            int r = world_to_cell(z + (float)j * margin);
            if (r < 0 || r >= DUNGEON_ROWS || c < 0 || c >= DUNGEON_COLS)
                return 1;
            if (MAP[r][c] == 1)
                return 1;
        }
    }
    return 0;
}

static vector cam_dir_from_yaw_pitch(float yaw, float pitch) {
    return (vector){
        cosf(pitch) * sinf(yaw),
        sinf(pitch),
        cosf(pitch) * cosf(yaw),
    };
}

static int build_dungeon(scene *s, int ti_wall_tex, int ti_floor_tex) {
    int use_img = ti_wall_tex >= 0;
    int m_wall = scene_add_material(s, use_img
        ? (scene_material){ .tex_kind = SCENE_TEX_IMAGE, .tex_index = ti_wall_tex, .tex_scale = 0.5f }
        : (scene_material){ .albedo = {120,115,100}, .albedo2 = {85,90,75},
            .tex_kind = SCENE_TEX_BRICKS, .tex_scale = 0.4f });
    int m_mold_wall = scene_add_material(s, use_img
        ? (scene_material){ .tex_kind = SCENE_TEX_IMAGE, .tex_index = ti_wall_tex, .tex_scale = 0.3f }
        : (scene_material){ .albedo = {95,110,85}, .albedo2 = {70,85,60},
            .tex_kind = SCENE_TEX_BRICKS, .tex_scale = 0.4f });
    int m_floor = scene_add_material(s, ti_floor_tex >= 0
        ? (scene_material){ .tex_kind = SCENE_TEX_IMAGE, .tex_index = ti_floor_tex, .tex_scale = 0.5f }
        : (scene_material){ .albedo = {90,82,70}, .albedo2 = {60,55,45},
            .tex_kind = SCENE_TEX_CHECKER, .tex_scale = 0.3f });
    int m_pillar = scene_add_material(s, (scene_material){
        .albedo  = {130, 125, 110},
        .albedo2 = {95,  90,  80},
        .tex_kind = SCENE_TEX_MARBLE,
        .tex_scale = 0.3f });
    int m_torch = scene_add_material(s, (scene_material){
        .albedo = {255, 160, 60},
        .unlit = 1 });
    int m_sconce = scene_add_material(s, (scene_material){
        .albedo = {40, 38, 35} });
    int m_pool = scene_add_material(s, (scene_material){
        .albedo = {25, 30, 35},
        .reflectivity = 0.5f });
    int m_gold = scene_add_material(s, (scene_material){
        .albedo = {220, 190, 50},
        .reflectivity = 0.2f });
    int m_gem = scene_add_material(s, (scene_material){
        .albedo = {200, 60, 220},
        .reflectivity = 0.3f,
        .unlit = 1 });
    if (m_wall < 0 || m_floor < 0 || m_pillar < 0 ||
        m_torch < 0 || m_sconce < 0 || m_pool < 0 || m_gold < 0 || m_gem < 0)
        return -1;

    for (int r = 0; r < DUNGEON_ROWS; r++) {
        for (int c = 0; c < DUNGEON_COLS; c++) {
            if (MAP[r][c] != 1) continue;
            float cx = col_to_x(c);
            float cz = row_to_z(r);
            int mat = ((r + c) % 4 == 0) ? m_mold_wall : m_wall;
            scene_add_box(s, scene_box_aabb(
                (vector){cx - HALF_CELL, 0.0f, cz - HALF_CELL},
                (vector){cx + HALF_CELL, WALL_HEIGHT, cz + HALF_CELL},
                mat));
        }
    }
    scene_add_plane(s, (scene_plane){
        .point = {0, 0, 0}, .normal = {0, 1, 0}, .material = m_floor });


    int pillar_pos[][2] = {{3,3}, {3,7}, {5,3}, {5,7}};
    for (int i = 0; i < 4; i++) {
        float px = col_to_x(pillar_pos[i][1]);
        float pz = row_to_z(pillar_pos[i][0]);
        scene_add_cylinder(s, (scene_cylinder){
            .center = {px, 1.5f, pz},
            .axis = {0, 1, 0},
            .radius = 0.2f,
            .half_height = 1.5f,
            .material = m_pillar });
    }

    for (int i = 0; i < NUM_TORCHES; i++) {
        int tr = TORCHES[i].r, tc = TORCHES[i].c;
        float tx = col_to_x(tc), tz = row_to_z(tr);
        float off = 0.3f;
        float fx = 0.0f, fz = 0.0f;
        if (tc > 0 && MAP[tr][tc - 1] == 0)      fx = -off;
        else if (tc < DUNGEON_COLS - 1 && MAP[tr][tc + 1] == 0) fx = off;
        else if (tr > 0 && MAP[tr - 1][tc] == 0) fz = -off;
        else if (tr < DUNGEON_ROWS - 1 && MAP[tr + 1][tc] == 0) fz = off;

        scene_add_box(s, scene_box_aabb(
            (vector){tx + fx - 0.03f, 1.45f, tz + fz - 0.03f},
            (vector){tx + fx + 0.03f, 1.55f, tz + fz + 0.03f},
            m_sconce));
        scene_add_sphere(s, (scene_sphere){
            .center = {tx + fx, 1.7f, tz + fz},
            .radius = 0.08f,
            .material = m_torch });
    }

    float pool_x = col_to_x(5), pool_z = row_to_z(4);
    scene_add_box(s, scene_box_aabb(
        (vector){pool_x - 0.6f, 0.0f, pool_z - 0.6f},
        (vector){pool_x + 0.6f, 0.05f, pool_z + 0.6f},
        m_pool));

    float gx = col_to_x(8), gz = row_to_z(9);
    scene_add_sphere(s, (scene_sphere){
        .center = {gx - 0.3f, 0.05f, gz},
        .radius = 0.08f, .material = m_gold });
    scene_add_sphere(s, (scene_sphere){
        .center = {gx + 0.3f, 0.05f, gz + 0.2f},
        .radius = 0.08f, .material = m_gold });
    scene_add_sphere(s, (scene_sphere){
        .center = {gx, 0.05f, gz - 0.3f},
        .radius = 0.08f, .material = m_gold });
    scene_add_sphere(s, (scene_sphere){
        .center = {gx, 0.3f, gz},
        .radius = 0.12f, .material = m_gem });

    scene_set_ambient(s, 0.12f);
    scene_add_light(s, (scene_light){
        .direction = {0.5f, 1.0f, 0.3f},
        .intensity = 0.5f });
    scene_add_light(s, (scene_light){
        .direction = {-0.3f, 0.8f, -0.5f},
        .intensity = 0.25f });

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
    SDL_Window *window = SDL_CreateWindow("Dungeon",
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
    if (!scn) {
        fprintf(stderr, "Failed to create scene\n");
        if (cpu_rnd) rt_renderer_destroy(cpu_rnd);
        if (gpu_rnd) rt_renderer_destroy(gpu_rnd);
        SDL_GL_DeleteContext(gl_ctx);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    scene_texture wall_tex = {0}, floor_tex = {0}, bg_tex = {0};
    int have_wall  = scene_texture_load(WALL_TEX_PATH, &wall_tex) == 0;
    int have_floor = scene_texture_load(FLOOR_TEX_PATH, &floor_tex) == 0;
    int ti_wall  = have_wall  ? scene_add_texture(scn, wall_tex)  : -1;
    int ti_floor = have_floor ? scene_add_texture(scn, floor_tex) : -1;
    if (!have_wall)
        fprintf(stderr, "Warning: could not load %s, using procedural wall\n", WALL_TEX_PATH);
    if (!have_floor)
        fprintf(stderr, "Warning: could not load %s, using procedural floor\n", FLOOR_TEX_PATH);

    int have_bg = scene_texture_load("apps/dungeon/deep-space.jpg", &bg_tex) == 0;
    if (have_bg) {
        int ti_bg = scene_add_texture(scn, bg_tex);
        scene_set_background(scn, (scene_color){0,0,0}, ti_bg);
    }

    if (build_dungeon(scn, ti_wall, ti_floor) < 0) {
        fprintf(stderr, "Failed to build dungeon scene\n");
        if (scn) scene_destroy(scn);
        if (have_wall)  scene_texture_free(&wall_tex);
        if (have_floor) scene_texture_free(&floor_tex);
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

    rt_gbuffer gbuf;
    uint32_t *obj_id  = calloc((size_t)(render_w * render_h), sizeof(uint32_t));
    float    *depth   = calloc((size_t)(render_w * render_h), sizeof(float));
    float    *normal  = calloc((size_t)(render_w * render_h * 3), sizeof(float));
    gbuf.object_id = obj_id;
    gbuf.depth     = depth;
    gbuf.normal    = normal;

    postfx_fog fog_cfg = {
        .enabled = 1,
        .color = {35, 40, 45},
        .start = 2.0f,
        .end = 14.0f,
        .max_strength = 0.35f,
        .skip_kinds_mask = 0,
    };
    postfx_vignette vig_cfg = {
        .enabled = 1,
        .intensity = 0.40f,
        .softness = 0.50f,
    };

    GLuint display_tex, display_fbo;
    glGenTextures(1, &display_tex);
    glBindTexture(GL_TEXTURE_2D, display_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, render_w, render_h, 0,
                 GL_BGRA, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glGenFramebuffers(1, &display_fbo);

    float start_x = col_to_x(1) + 0.3f;
    float start_z = row_to_z(1) + 0.3f;
    scene_camera *cam = scene_camera_create(
        (vector){start_x, EYE_HEIGHT, start_z},
        (vector){0.0f, 0.0f, 1.0f});
    vector cam_pos = {start_x, EYE_HEIGHT, start_z};
    float cam_yaw = 0.0f;
    float cam_pitch = 0.0f;
    int mouse_captured = 1;
    SDL_SetRelativeMouseMode(SDL_TRUE);

    int running = 1;
    Uint32 frame_last = SDL_GetTicks();
    Uint32 fps_last = frame_last;
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
                        free(obj_id);
                        free(depth);
                        free(normal);
                        obj_id  = calloc((size_t)(render_w * render_h), sizeof(uint32_t));
                        depth   = calloc((size_t)(render_w * render_h), sizeof(float));
                        normal  = calloc((size_t)(render_w * render_h * 3), sizeof(float));
                        gbuf.object_id = obj_id;
                        gbuf.depth     = depth;
                        gbuf.normal    = normal;
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
        float v_fwd = 0.0f, v_right = 0.0f;
        if (keys[SDL_SCANCODE_W]) v_fwd = v;
        if (keys[SDL_SCANCODE_S]) v_fwd = -v;
        if (keys[SDL_SCANCODE_D]) v_right = v;
        if (keys[SDL_SCANCODE_A]) v_right = -v;

        float new_x = cam_pos.x + fwd.x * v_fwd + right.x * v_right;
        if (!is_blocked(new_x, cam_pos.z))
            cam_pos.x = new_x;
        float new_z = cam_pos.z + fwd.z * v_fwd + right.z * v_right;
        if (!is_blocked(cam_pos.x, new_z))
            cam_pos.z = new_z;
        cam_pos.y = EYE_HEIGHT;

        vector cam_dir = cam_dir_from_yaw_pitch(cam_yaw, cam_pitch);
        scene_camera_place(cam, cam_pos, cam_dir);

        Uint32 r_start = SDL_GetTicks();
        rt_renderer_render(active, scn, cam, &viewport, pixels, &gbuf);
        render_ms_accum += SDL_GetTicks() - r_start;

        postfx_gbuffer pg = { obj_id, depth, normal };
        postfx_fog_apply(pixels, &pg, render_w, render_h, &fog_cfg);
        postfx_vignette_apply(pixels, render_w, render_h, &vig_cfg);

        display_pixels(display_tex, display_fbo, pixels,
                       render_w, render_h, window_w, window_h);
        SDL_GL_SwapWindow(window);

        fps_frames++;
        Uint32 now = SDL_GetTicks();
        if (now - fps_last >= 1000) {
            float avg_ms = fps_frames ? (float)render_ms_accum / (float)fps_frames : 0.0f;
            snprintf(title_buf, sizeof(title_buf),
                     "Dungeon - %s %dx%d  %d FPS (%.1fms) %s",
                     rt_renderer_name(active), render_w, render_h,
                     fps_frames, avg_ms,
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
    free(obj_id);
    free(depth);
    free(normal);
    scene_camera_destroy(cam);
    scene_destroy(scn);
    if (have_wall)  scene_texture_free(&wall_tex);
    if (have_floor) scene_texture_free(&floor_tex);
    if (have_bg)    scene_texture_free(&bg_tex);
    SDL_GL_DeleteContext(gl_ctx);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
