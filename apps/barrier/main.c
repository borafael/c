#include "battleforge.h"
#include "console.h"
#include "ini.h"
#include "renderer.h"
#include "stb_image.h"    /* declarations only — implementation lives in slice.c */
#include <SDL2/SDL.h>
#define GL_GLEXT_PROTOTYPES 1
#include "gl_compat.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define _USE_MATH_DEFINES
#include <math.h>

#define WINDOW_W 800
#define WINDOW_H 600
#define FOV (M_PI / 3.0f)
#define MOVE_SPEED 8.0f
#define ROT_SPEED  2.0f

/* Placeholder terrain + sky materials — set in main() next to
 * unit_material so all iteration surfaces sit together. Zero-init
 * defaults (tex_kind=NONE, reflectivity=0, sky_radius=0) keep the
 * legacy flat-terrain / black-sky behavior. */
static scene_material g_terrain_material;
static scene_material g_sky_material;
static float       g_sky_radius;

/* --- Map loading --- */

static int load_map_from_ini(const char *name, bf_engine *engine, void *user_data) {
    (void)user_data;
    char ini_path[256];
    snprintf(ini_path, sizeof(ini_path), "apps/barrier/maps/%s.ini", name);

    ini_file *ini = ini_load(ini_path);
    if (!ini) {
        fprintf(stderr, "load_map: cannot open '%s'\n", ini_path);
        return -1;
    }

    bf_map map = {0};
    map.width       = ini_get_float(ini, "map", "width", 100.0f);
    map.depth       = ini_get_float(ini, "map", "depth", 100.0f);
    map.grid_cols   = ini_get_int(ini, "map", "grid_cols", 64);
    map.grid_rows   = ini_get_int(ini, "map", "grid_rows", 64);
    map.max_height  = ini_get_float(ini, "map", "max_height", 10.0f);
    map.ambient     = ini_get_float(ini, "lighting", "ambient", 0.15f);
    map.light_intensity = ini_get_float(ini, "lighting", "light_intensity", 0.85f);

    /* Parse light_dir as "x, y, z" */
    const char *ld = ini_get(ini, "lighting", "light_dir");
    if (ld) {
        float lx = 1.0f, ly = 1.0f, lz = -1.0f;
        sscanf(ld, "%f , %f , %f", &lx, &ly, &lz);
        map.light_dir = (vector){lx, ly, lz};
    } else {
        map.light_dir = (vector){1.0f, 1.0f, -1.0f};
    }

    /* Load heightmap if specified */
    const char *hm_file = ini_get(ini, "map", "heightmap");
    int have_heightmap = 0;
    if (hm_file) {
        char hm_path[256];
        snprintf(hm_path, sizeof(hm_path), "apps/barrier/maps/%s", hm_file);
        int img_w, img_h, channels;
        unsigned char *img = stbi_load(hm_path, &img_w, &img_h, &channels, 1);
        if (img) {
            if (img_w == map.grid_cols && img_h == map.grid_rows) {
                map.heights = malloc(sizeof(float) * map.grid_rows * map.grid_cols);
                if (map.heights) {
                    for (int i = 0; i < map.grid_rows * map.grid_cols; i++)
                        map.heights[i] = (img[i] / 255.0f) * map.max_height;
                    have_heightmap = 1;
                }
            } else {
                fprintf(stderr, "load_map: heightmap %s is %dx%d, expected %dx%d\n",
                        hm_file, img_w, img_h, map.grid_cols, map.grid_rows);
            }
            stbi_image_free(img);
        } else {
            fprintf(stderr, "load_map: cannot load heightmap '%s'\n", hm_path);
        }
    }

    ini_free(ini);

    if (!have_heightmap) {
        /* Fall back to procedural terrain */
        bf_map_generate_test_terrain(&map);
    } else {
        /* Generate normals from finite differences, then hand off to
           bf_map_colorize for the slope-aware palette. */
        int rows = map.grid_rows;
        int cols = map.grid_cols;
        map.colors  = calloc((rows - 1) * (cols - 1) * 3, sizeof(uint8_t));
        map.normals = calloc(rows * cols * 3, sizeof(float));

        float cell_w = map.width / (float)(cols - 1);
        float cell_d = map.depth / (float)(rows - 1);
        for (int r = 0; r < rows; r++) {
            for (int c = 0; c < cols; c++) {
                float hL = map.heights[r * cols + (c > 0 ? c - 1 : c)];
                float hR = map.heights[r * cols + (c < cols - 1 ? c + 1 : c)];
                float hD = map.heights[(r > 0 ? r - 1 : r) * cols + c];
                float hU = map.heights[(r < rows - 1 ? r + 1 : r) * cols + c];
                vector n = vector_normalize((vector){
                    (hL - hR) / (2.0f * cell_w),
                    1.0f,
                    (hD - hU) / (2.0f * cell_d)
                });
                int ni = (r * cols + c) * 3;
                map.normals[ni] = n.x;
                map.normals[ni + 1] = n.y;
                map.normals[ni + 2] = n.z;
            }
        }

        bf_map_colorize(&map);
    }

    map.terrain_material = g_terrain_material;
    map.sky_material     = g_sky_material;
    map.sky_radius       = g_sky_radius;

    /* Allocate map on heap — engine takes ownership */
    bf_map *heap_map = malloc(sizeof(bf_map));
    if (!heap_map) {
        free(map.heights);
        free(map.colors);
        free(map.normals);
        return -1;
    }
    *heap_map = map;

    /* Register map with engine */
    bf_cmd load_cmd = { .type = BF_CMD_LOAD_MAP };
    snprintf(load_cmd.load_map.name, BF_MAP_NAME_SIZE, "%s", name);
    load_cmd.load_map.map = heap_map;
    bf_command(engine, load_cmd);

    /* Flush the load command, then select the map */
    bf_tick(engine, 0.0f);

    /* Find the map index (it's the latest registered) */
    static int map_count = 0;
    int map_idx = map_count++;

    bf_command(engine, (bf_cmd){
        .type = BF_CMD_SELECT_MAP,
        .select_map = { .index = map_idx }
    });

    fprintf(stderr, "Loaded map '%s' (idx=%d, %dx%d, max_h=%.1f)\n",
            name, map_idx, map.grid_cols, map.grid_rows, map.max_height);
    return 0;
}

/* --- Main --- */

/* Blit a uint32 BGRA buffer to the default framebuffer using an
 * intermediate texture. Mirrors the display path in apps/rtdemo and
 * apps/nbody so the same window works for CPU or OpenGL-backend
 * raytrace output. */
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
        if (strcmp(argv[i], "-G") == 0 || strcmp(argv[i], "--gpu") == 0) {
            preferred = RT_BACKEND_OPENGL;
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            printf("Usage: %s [options]\n", argv[0]);
            printf("  -G, --gpu    Start with OpenGL raytrace backend (falls back to CPU)\n");
            printf("  -h, --help   Show this help\n");
            printf("\nPress TAB in-game to toggle the backend.\n");
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

    int window_w = WINDOW_W;
    int window_h = WINDOW_H;
    int fullscreen = 0;
    SDL_Window *window = SDL_CreateWindow("Barrier",
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

    GLuint display_tex, display_fbo;
    glGenTextures(1, &display_tex);
    glBindTexture(GL_TEXTURE_2D, display_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, WINDOW_W, WINDOW_H, 0,
                 GL_BGRA, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glGenFramebuffers(1, &display_fbo);

    uint32_t *pixels = calloc(WINDOW_W * WINDOW_H, sizeof(uint32_t));

    /* Create engine */
    bf_engine *engine = bf_create((bf_config){
        .render_width = WINDOW_W,
        .render_height = WINDOW_H,
        .fov = FOV,
        .num_threads = 0,
        .backend = preferred
    });
    if (preferred == RT_BACKEND_OPENGL &&
        bf_get_backend(engine) != RT_BACKEND_OPENGL) {
        fprintf(stderr, "OpenGL backend unavailable, using CPU\n");
    }
    fprintf(stderr, "Raytrace backend: %s (press TAB to toggle)\n",
            bf_get_backend(engine) == RT_BACKEND_OPENGL ? "OpenGL" : "CPU");

    /* --- Placeholder visuals — tweak freely while iterating on look.
     *     Swap reflectivity, tex_kind (see libs/raytrace/material.h for
     *     the full set: CHECKER, MARBLE, CELLS, STRIPES, DOTS, BRICKS,
     *     CLOUDS, SPOTS, etc.), colors — everything picks up the change
     *     on the next build. Must be set before load_map_from_ini so
     *     g_terrain_material flows into bf_map. --- */
    scene_material unit_material = {
        .albedo       = {200, 200, 210},
        .reflectivity = 1.0f,
    };
    float unit_radius = 1.0f;

    g_terrain_material = (scene_material){
        .albedo       = {235, 225, 200},   /* warm sand */
        .albedo2      = { 70, 100,  60},   /* dark moss */
        .tex_kind     = SCENE_TEX_SPOTS,      /* leopard blotches */
        .tex_scale    = 4.5f,
        .reflectivity = 0.0f,
    };

    /* Sky sphere — a giant sphere surrounding the camera with a vertical
     * gradient. Set sky_radius = 0 to disable (black background). */
    g_sky_material = (scene_material){
        .albedo       = {235, 200, 170},   /* warm horizon */
        .albedo2      = { 90, 130, 210},   /* blue zenith */
        .tex_kind     = SCENE_TEX_GRADIENT,
        .tex_scale    = 500.0f,            /* gradient spans y=0..500 */
        .reflectivity = 0.0f,
        .unlit        = 1,                 /* sky ignores scene lighting */
    };
    g_sky_radius = 1000.0f;

    /* Load default map via the command system (after materials are set). */
    load_map_from_ini("battlefield", engine, NULL);

    /* One unit per 3D primitive kind, all sharing the mirror material. */
    bf_unit_def unit_defs[] = {
        { .base_speed = 3.0f, .has_selection = 1, .visual = {
            .kind = BF_VIS_SPHERE,
            .sphere = { .radius = unit_radius, .material = unit_material } } },
        { .base_speed = 3.0f, .has_selection = 1, .visual = {
            .kind = BF_VIS_BOX,
            .box = { .half_extents = {0.9f, 0.9f, 0.9f},
                     .euler_xyz   = {0.3f, 0.7f, 0.2f},   /* tilted cube */
                     .material    = unit_material } } },
        { .base_speed = 3.0f, .has_selection = 1, .visual = {
            .kind = BF_VIS_CYLINDER,
            .cylinder = { .radius = 0.7f, .half_height = 1.0f,
                          .axis = {0.0f, 1.0f, 0.0f},
                          .material = unit_material } } },
        { .base_speed = 3.0f, .has_selection = 1, .visual = {
            .kind = BF_VIS_CONE,
            .cone = { .radius = 0.9f, .height = 2.0f,
                      .material = unit_material } } },
        { .base_speed = 3.0f, .has_selection = 1, .visual = {
            .kind = BF_VIS_TORUS,
            .torus = { .major_radius = 0.9f, .minor_radius = 0.35f,
                       .material = unit_material } } },
    };
    static const char *unit_names[] = {
        "orb", "cube", "pillar", "spire", "donut"
    };
    #define NUM_UNIT_TYPES ((int)(sizeof(unit_defs) / sizeof(unit_defs[0])))
    #define ARMY_SIZE      NUM_UNIT_TYPES

    for (int i = 0; i < NUM_UNIT_TYPES; i++) {
        snprintf(unit_defs[i].name, BF_UNIT_NAME_SIZE, "%s", unit_names[i]);
        bf_command(engine, (bf_cmd){
            .type = BF_CMD_REGISTER_UNIT,
            .register_unit = { .def = unit_defs[i] }
        });
    }
    fprintf(stderr, "Registered %d unit types\n", NUM_UNIT_TYPES);

    /* Process registration commands before creating entities */
    bf_tick(engine, 0.0f);

    /* Two armies, one of each primitive per side. Spread along x so
     * every shape is plainly visible. */
    for (int i = 0; i < ARMY_SIZE; i++) {
        float x = 30.0f + i * 4.0f;
        bf_command(engine, (bf_cmd){
            .type = BF_CMD_ENTITY_CREATE,
            .entity_create = { .id = i + 1, .unit_def_id = i,
                               .position = {x, 0.0f, 15.0f},
                               .direction = {0.0f, 0.0f, 1.0f} }
        });
    }
    for (int i = 0; i < ARMY_SIZE; i++) {
        float x = 30.0f + i * 4.0f;
        bf_command(engine, (bf_cmd){
            .type = BF_CMD_ENTITY_CREATE,
            .entity_create = { .id = ARMY_SIZE + i + 1, .unit_def_id = i,
                               .position = {x, 0.0f, 85.0f},
                               .direction = {0.0f, 0.0f, -1.0f} }
        });
    }

    /* Camera starts looking at the scene */
    bf_command(engine, (bf_cmd){
        .type = BF_CMD_CAMERA_SET,
        .camera_set = {
            .position = {30.0f, 20.0f, 55.0f},
            .direction = {0.0f, -0.4f, -1.0f}
        }
    });

    float cam_yaw = 0.0f;  /* facing -Z initially */
    float cam_pitch = -0.3f;
    float cam_x = 30.0f, cam_y = 20.0f, cam_z = 55.0f;
    int selected_id = -1;

    Uint32 fps_last = SDL_GetTicks();
    Uint32 frame_last = SDL_GetTicks();
    int fps_frames = 0;
    char title_buf[128];
    int running = 1;

    console_state console;
    if (console_init(&console, WINDOW_W, WINDOW_H,
                     "apps/barrier/assets/font.png") < 0) {
        fprintf(stderr, "Warning: console disabled (font not found)\n");
    }
    console.load_map = load_map_from_ini;
    console.load_map_user_data = NULL;
    SDL_StartTextInput();

    while (running) {
        Uint32 frame_now = SDL_GetTicks();
        float dt = (frame_now - frame_last) / 1000.0f;
        frame_last = frame_now;
        if (dt > 0.1f) dt = 0.1f;  /* cap to avoid spiral of death */

        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) running = 0;

            /* Backtick toggles console */
            if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_BACKQUOTE) {
                console_toggle(&console);
                continue;
            }

            if (console_is_open(&console)) {
                /* Console captures keyboard input only */
                if (e.type == SDL_KEYDOWN) {
                    console_handle_key(&console, e.key.keysym.sym,
                                       e.key.keysym.scancode, engine);
                    continue;
                } else if (e.type == SDL_TEXTINPUT) {
                    if (e.text.text[0] != '`')
                        console_handle_text(&console, e.text.text);
                    continue;
                }
                /* Mouse events fall through to game input below */
            }

            /* Game input (always for mouse, keyboard only when console closed) */
            if (!console_is_open(&console) &&
                e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE)
                running = 0;
            if (!console_is_open(&console) &&
                e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_TAB) {
                rt_backend cur = bf_get_backend(engine);
                rt_backend next = (cur == RT_BACKEND_CPU)
                                  ? RT_BACKEND_OPENGL : RT_BACKEND_CPU;
                if (bf_set_backend(engine, next) == 0) {
                    fprintf(stderr, "Raytrace backend: %s\n",
                            next == RT_BACKEND_OPENGL ? "OpenGL" : "CPU");
                } else {
                    fprintf(stderr, "Backend %s unavailable\n",
                            next == RT_BACKEND_OPENGL ? "OpenGL" : "CPU");
                }
            }
            if (!console_is_open(&console) &&
                e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_F11) {
                fullscreen = !fullscreen;
                SDL_SetWindowFullscreen(window,
                    fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
                SDL_GetWindowSize(window, &window_w, &window_h);
            }
            if (e.type == SDL_MOUSEBUTTONDOWN &&
                !(console_visible_height(&console) > 0 && e.button.y < console_visible_height(&console))) {
                int pick_x = e.button.x * WINDOW_W / window_w;
                int pick_y = e.button.y * WINDOW_H / window_h;
                bf_pick_result pick = bf_pick(engine, pick_x, pick_y);
                if (e.button.button == SDL_BUTTON_LEFT) {
                    if (pick.type == BF_PICK_ENTITY) {
                        selected_id = pick.entity_id;
                        bf_command(engine, (bf_cmd){
                            .type = BF_CMD_SELECT,
                            .select = { .id = pick.entity_id }
                        });
                        fprintf(stderr, "Selected entity %d\n", pick.entity_id);
                    } else {
                        selected_id = -1;
                        bf_command(engine, (bf_cmd){
                            .type = BF_CMD_SELECT,
                            .select = { .id = -1 }
                        });
                        fprintf(stderr, "Deselected\n");
                    }
                } else if (e.button.button == SDL_BUTTON_RIGHT) {
                    if (selected_id >= 0 && pick.type == BF_PICK_GROUND) {
                        vector dest = pick.position;
                        bf_command(engine, (bf_cmd){
                            .type = BF_CMD_ENTITY_MOVE,
                            .entity_move = { .id = selected_id,
                                             .target = dest,
                                             .speed = 3.0f,
                                             .loco_type = BF_LOCO_LINEAR }
                        });
                        fprintf(stderr, "Move entity %d to (%.1f, %.1f, %.1f)\n",
                                selected_id, dest.x, dest.y, dest.z);
                    }
                }
            }
        }

        if (!console_is_open(&console)) {
            /* Continuous key input for camera */
            const Uint8 *keys = SDL_GetKeyboardState(NULL);
            float move_x = 0.0f, move_y = 0.0f, move_z = 0.0f;

            /* Forward/back follow the full camera direction (yaw + pitch) */
            float fw_x = cosf(cam_pitch) * sinf(cam_yaw);
            float fw_y = sinf(cam_pitch);
            float fw_z = cosf(cam_pitch) * -cosf(cam_yaw);
            /* Strafe stays horizontal */
            float rt_x = cosf(cam_yaw);
            float rt_z = sinf(cam_yaw);

            /* scene_camera derives right = up × forward (camera.c), so with
             * forward = -Z, cam.right = world -X. Strafe and yaw are
             * defined against that convention: D/A and RIGHT/LEFT must
             * push/rotate in the direction of the actual cam.right. */
            if (keys[SDL_SCANCODE_W]) { move_x += fw_x; move_y += fw_y; move_z += fw_z; }
            if (keys[SDL_SCANCODE_S]) { move_x -= fw_x; move_y -= fw_y; move_z -= fw_z; }
            if (keys[SDL_SCANCODE_A]) { move_x += rt_x; move_z += rt_z; }
            if (keys[SDL_SCANCODE_D]) { move_x -= rt_x; move_z -= rt_z; }
            if (keys[SDL_SCANCODE_LEFT])  cam_yaw += ROT_SPEED * dt;
            if (keys[SDL_SCANCODE_RIGHT]) cam_yaw -= ROT_SPEED * dt;
            if (keys[SDL_SCANCODE_UP])    cam_pitch += ROT_SPEED * dt;
            if (keys[SDL_SCANCODE_DOWN])  cam_pitch -= ROT_SPEED * dt;
            if (cam_pitch >  1.4f) cam_pitch =  1.4f;
            if (cam_pitch < -1.4f) cam_pitch = -1.4f;
            if (keys[SDL_SCANCODE_SPACE]) cam_y += MOVE_SPEED * dt;
            if (keys[SDL_SCANCODE_LSHIFT]) cam_y -= MOVE_SPEED * dt;

            cam_x += move_x * MOVE_SPEED * dt;
            cam_y += move_y * MOVE_SPEED * dt;
            cam_z += move_z * MOVE_SPEED * dt;

            bf_command(engine, (bf_cmd){
                .type = BF_CMD_CAMERA_SET,
                .camera_set = {
                    .position = {cam_x, cam_y, cam_z},
                    .direction = {cosf(cam_pitch) * sinf(cam_yaw), sinf(cam_pitch), cosf(cam_pitch) * -cosf(cam_yaw)}
                }
            });
        }

        console_update(&console, dt);
        bf_tick(engine, dt);
        bf_render(engine, pixels);
        console_render(&console, pixels, WINDOW_W, WINDOW_H, engine);

        display_pixels(display_tex, display_fbo, pixels,
                       WINDOW_W, WINDOW_H, window_w, window_h);
        SDL_GL_SwapWindow(window);

        fps_frames++;
        Uint32 now = SDL_GetTicks();
        if (now - fps_last >= 1000) {
            const char *bname =
                bf_get_backend(engine) == RT_BACKEND_OPENGL ? "OpenGL" : "CPU";
            snprintf(title_buf, sizeof(title_buf),
                     "Barrier - %s %d FPS (%dx%d)", bname, fps_frames,
                     WINDOW_W, WINDOW_H);
            SDL_SetWindowTitle(window, title_buf);
            fprintf(stderr, "[%s] %d FPS\n", bname, fps_frames);
            fps_frames = 0;
            fps_last = now;
        }
    }

    console_destroy(&console);
    bf_destroy(engine);
    free(pixels);
    glDeleteFramebuffers(1, &display_fbo);
    glDeleteTextures(1, &display_tex);
    SDL_GL_DeleteContext(gl_ctx);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
