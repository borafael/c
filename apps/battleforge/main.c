#include "battleforge.h"
#include "slice.h"
#include <SDL2/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#define _USE_MATH_DEFINES
#include <math.h>

#define WINDOW_W 800
#define WINDOW_H 600
#define FOV (M_PI / 3.0f)
#define MOVE_SPEED 8.0f
#define ROT_SPEED  2.0f

/* --- Reuse smiley face sprite from rtdemo --- */

#define S 16
#define PX(r,g,b) (0xFF000000u | ((r)<<16) | ((g)<<8) | (b))
#define TP 0x00000000u

static uint32_t frame_data[16][S * S];

static void set(uint32_t *buf, int x, int y, uint32_t c) {
    if (x >= 0 && x < S && y >= 0 && y < S)
        buf[y * S + x] = c;
}

static void fill_circle(uint32_t *buf, int cx, int cy, int r, uint32_t c) {
    for (int y = cy - r; y <= cy + r; y++)
        for (int x = cx - r; x <= cx + r; x++)
            if ((x-cx)*(x-cx) + (y-cy)*(y-cy) <= r*r)
                set(buf, x, y, c);
}

static void clear_frame(uint32_t *buf) {
    for (int i = 0; i < S * S; i++) buf[i] = TP;
}

static void draw_head(uint32_t *buf, uint32_t skin_c, uint32_t hair_c) {
    fill_circle(buf, 7, 7, 6, skin_c);
    for (int x = 2; x <= 12; x++)
        for (int y = 1; y <= 3; y++)
            if ((x-7)*(x-7) + (y-7)*(y-7) <= 36)
                set(buf, x, y, hair_c);
}

static void init_sprite_frames(void) {
    uint32_t skin  = PX(255, 200, 150);
    uint32_t hair  = PX(100,  60,  20);
    uint32_t eye_w = PX(255, 255, 255);
    uint32_t eye_p = PX( 30,  30,  30);
    uint32_t mouth = PX(200,  60,  60);

    /* 16 frames at 22.5° increments, clockwise from front.
       Even frames (0,2,4,...) are the original 8 cardinal/diagonal angles.
       Odd frames (1,3,5,...) are new intermediates. */

    /* Frame 0: Front (0°) */
    clear_frame(frame_data[0]);
    draw_head(frame_data[0], skin, hair);
    fill_circle(frame_data[0], 5, 6, 1, eye_w);
    set(frame_data[0], 5, 6, eye_p);
    fill_circle(frame_data[0], 9, 6, 1, eye_w);
    set(frame_data[0], 9, 6, eye_p);
    set(frame_data[0], 5, 10, mouth);
    set(frame_data[0], 6, 11, mouth);
    set(frame_data[0], 7, 11, mouth);
    set(frame_data[0], 8, 11, mouth);
    set(frame_data[0], 9, 10, mouth);

    /* Frame 1: Front-to-front-right (22.5°) */
    clear_frame(frame_data[1]);
    draw_head(frame_data[1], skin, hair);
    fill_circle(frame_data[1], 6, 6, 1, eye_w);
    set(frame_data[1], 6, 6, eye_p);
    fill_circle(frame_data[1], 10, 6, 1, eye_w);
    set(frame_data[1], 10, 6, eye_p);
    set(frame_data[1], 6, 10, mouth);
    set(frame_data[1], 7, 11, mouth);
    set(frame_data[1], 8, 11, mouth);
    set(frame_data[1], 9, 10, mouth);

    /* Frame 2: Front-right (45°) */
    clear_frame(frame_data[2]);
    draw_head(frame_data[2], skin, hair);
    fill_circle(frame_data[2], 6, 6, 1, eye_w);
    set(frame_data[2], 7, 6, eye_p);
    fill_circle(frame_data[2], 10, 6, 1, eye_w);
    set(frame_data[2], 11, 6, eye_p);
    set(frame_data[2], 7, 10, mouth);
    set(frame_data[2], 8, 11, mouth);
    set(frame_data[2], 9, 11, mouth);
    set(frame_data[2], 10, 10, mouth);

    /* Frame 3: Front-right-to-right (67.5°) */
    clear_frame(frame_data[3]);
    draw_head(frame_data[3], skin, hair);
    fill_circle(frame_data[3], 8, 6, 1, eye_w);
    set(frame_data[3], 9, 6, eye_p);
    fill_circle(frame_data[3], 11, 6, 1, eye_w);
    set(frame_data[3], 12, 6, eye_p);
    set(frame_data[3], 8, 10, mouth);
    set(frame_data[3], 9, 11, mouth);
    set(frame_data[3], 10, 10, mouth);

    /* Frame 4: Right (90°) */
    clear_frame(frame_data[4]);
    draw_head(frame_data[4], skin, hair);
    fill_circle(frame_data[4], 9, 6, 1, eye_w);
    set(frame_data[4], 10, 6, eye_p);
    set(frame_data[4], 12, 7, skin);
    set(frame_data[4], 13, 8, skin);
    set(frame_data[4], 9, 10, mouth);
    set(frame_data[4], 10, 11, mouth);
    set(frame_data[4], 11, 10, mouth);

    /* Frame 5: Right-to-back-right (112.5°) */
    clear_frame(frame_data[5]);
    draw_head(frame_data[5], skin, hair);
    fill_circle(frame_data[5], 10, 6, 1, eye_w);
    set(frame_data[5], 11, 6, eye_p);
    set(frame_data[5], 12, 7, skin);
    set(frame_data[5], 13, 8, skin);
    set(frame_data[5], 10, 10, mouth);
    set(frame_data[5], 11, 10, mouth);

    /* Frame 6: Back-right (135°) */
    clear_frame(frame_data[6]);
    draw_head(frame_data[6], skin, hair);
    set(frame_data[6], 2, 7, skin);
    set(frame_data[6], 1, 7, skin);
    set(frame_data[6], 1, 8, skin);

    /* Frame 7: Back-right-to-back (157.5°) */
    clear_frame(frame_data[7]);
    draw_head(frame_data[7], skin, hair);
    for (int x = 4; x <= 11; x++)
        for (int y = 2; y <= 5; y++)
            if ((x-7)*(x-7) + (y-7)*(y-7) <= 36)
                set(frame_data[7], x, y, hair);
    set(frame_data[7], 2, 7, skin);
    set(frame_data[7], 1, 8, skin);

    /* Frame 8: Back (180°) */
    clear_frame(frame_data[8]);
    draw_head(frame_data[8], skin, hair);
    for (int x = 3; x <= 11; x++)
        for (int y = 2; y <= 6; y++)
            if ((x-7)*(x-7) + (y-7)*(y-7) <= 36)
                set(frame_data[8], x, y, hair);

    /* Frame 9: Back-to-back-left (202.5°) */
    clear_frame(frame_data[9]);
    draw_head(frame_data[9], skin, hair);
    for (int x = 3; x <= 10; x++)
        for (int y = 2; y <= 5; y++)
            if ((x-7)*(x-7) + (y-7)*(y-7) <= 36)
                set(frame_data[9], x, y, hair);
    set(frame_data[9], 12, 7, skin);
    set(frame_data[9], 13, 8, skin);

    /* Frame 10: Back-left (225°) */
    clear_frame(frame_data[10]);
    draw_head(frame_data[10], skin, hair);
    set(frame_data[10], 12, 7, skin);
    set(frame_data[10], 13, 7, skin);
    set(frame_data[10], 13, 8, skin);

    /* Frame 11: Back-left-to-left (247.5°) */
    clear_frame(frame_data[11]);
    draw_head(frame_data[11], skin, hair);
    fill_circle(frame_data[11], 4, 6, 1, eye_w);
    set(frame_data[11], 3, 6, eye_p);
    set(frame_data[11], 2, 7, skin);
    set(frame_data[11], 1, 8, skin);
    set(frame_data[11], 4, 10, mouth);
    set(frame_data[11], 5, 10, mouth);

    /* Frame 12: Left (270°) */
    clear_frame(frame_data[12]);
    draw_head(frame_data[12], skin, hair);
    fill_circle(frame_data[12], 5, 6, 1, eye_w);
    set(frame_data[12], 4, 6, eye_p);
    set(frame_data[12], 2, 7, skin);
    set(frame_data[12], 1, 8, skin);
    set(frame_data[12], 3, 10, mouth);
    set(frame_data[12], 4, 11, mouth);
    set(frame_data[12], 5, 10, mouth);

    /* Frame 13: Left-to-front-left (292.5°) */
    clear_frame(frame_data[13]);
    draw_head(frame_data[13], skin, hair);
    fill_circle(frame_data[13], 4, 6, 1, eye_w);
    set(frame_data[13], 3, 6, eye_p);
    fill_circle(frame_data[13], 7, 6, 1, eye_w);
    set(frame_data[13], 6, 6, eye_p);
    set(frame_data[13], 4, 10, mouth);
    set(frame_data[13], 5, 11, mouth);
    set(frame_data[13], 6, 10, mouth);

    /* Frame 14: Front-left (315°) */
    clear_frame(frame_data[14]);
    draw_head(frame_data[14], skin, hair);
    fill_circle(frame_data[14], 4, 6, 1, eye_w);
    set(frame_data[14], 3, 6, eye_p);
    fill_circle(frame_data[14], 8, 6, 1, eye_w);
    set(frame_data[14], 7, 6, eye_p);
    set(frame_data[14], 4, 10, mouth);
    set(frame_data[14], 5, 11, mouth);
    set(frame_data[14], 6, 11, mouth);
    set(frame_data[14], 7, 10, mouth);

    /* Frame 15: Front-left-to-front (337.5°) */
    clear_frame(frame_data[15]);
    draw_head(frame_data[15], skin, hair);
    fill_circle(frame_data[15], 5, 6, 1, eye_w);
    set(frame_data[15], 4, 6, eye_p);
    fill_circle(frame_data[15], 9, 6, 1, eye_w);
    set(frame_data[15], 8, 6, eye_p);
    set(frame_data[15], 5, 10, mouth);
    set(frame_data[15], 6, 11, mouth);
    set(frame_data[15], 7, 11, mouth);
    set(frame_data[15], 8, 10, mouth);
}

/* --- Main --- */

int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        fprintf(stderr, "SDL init failed: %s\n", SDL_GetError());
        return 1;
    }

    SDL_Window *window = SDL_CreateWindow("Battleforge",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        WINDOW_W, WINDOW_H, 0);
    if (!window) {
        fprintf(stderr, "Window creation failed: %s\n", SDL_GetError());
        return 1;
    }

    SDL_Renderer *renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    SDL_Texture *texture = SDL_CreateTexture(renderer,
        SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING,
        WINDOW_W, WINDOW_H);

    uint32_t *pixels = calloc(WINDOW_W * WINDOW_H, sizeof(uint32_t));

    /* Create engine */
    bf_engine *engine = bf_create((bf_config){
        .render_width = WINDOW_W,
        .render_height = WINDOW_H,
        .fov = FOV,
        .num_threads = 0
    });

    /* Set map and generate terrain */
    bf_map map = {
        .width = 100.0f,
        .depth = 100.0f,
        .grid_cols = 64,
        .grid_rows = 64,
        .max_height = 10.0f,
        .ambient = 0.15f,
        .light_dir = {1.0f, 1.0f, -1.0f},
        .light_intensity = 0.85f
    };
    bf_map_generate_test_terrain(&map);
    bf_set_map(engine, map);

    /* Build a programmatic slice_sheet from procedural frame data.
       Static storage: engine borrows these pointers for its lifetime. */
    init_sprite_frames();

    static uint32_t *pixel_ptrs[16];
    for (int i = 0; i < 16; i++)
        pixel_ptrs[i] = frame_data[i];

    static int idle_col = 0;
    static slice_anim smiley_anim = {
        .name = "idle",
        .columns = NULL,
        .column_count = 1,
        .loop = 1
    };
    smiley_anim.columns = &idle_col;

    static slice_sheet smiley_sheet;
    smiley_sheet = (slice_sheet){
        .pixels = pixel_ptrs,
        .angles = 16,
        .total_columns = 1,
        .frame_width = S,
        .frame_height = S,
        .fps = 1.0f,
        .anims = &smiley_anim,
        .anim_count = 1
    };

    int spr_id = bf_register_sprite(engine, &smiley_sheet, 2.0f, 2.0f);

    /* Load 3D-rendered sprite sheet from PNG */
    slice_sheet *unit_sheet = slice_load("apps/battleforge/assets/test_unit.png");
    int unit_spr = -1;
    int walk_anim = -1;
    if (unit_sheet) {
        unit_spr = bf_register_sprite(engine, unit_sheet, 2.0f, 2.0f);
        walk_anim = slice_anim_index(unit_sheet, "walk");
        fprintf(stderr, "Loaded test_unit sprite (id=%d, walk_anim=%d)\n",
                unit_spr, walk_anim);
    } else {
        fprintf(stderr, "Warning: could not load test_unit.png, using smiley only\n");
    }

    /* Create entities */
    bf_command(engine, (bf_cmd){
        .type = BF_CMD_ENTITY_CREATE,
        .entity_create = { .id = 1, .sprite_id = spr_id,
                           .position = {0.0f, 0.0f, 0.0f},
                           .direction = {0.0f, 0.0f, 1.0f},
                           .speed = 3.0f }
    });
    bf_command(engine, (bf_cmd){
        .type = BF_CMD_ENTITY_CREATE,
        .entity_create = { .id = 2, .sprite_id = spr_id,
                           .position = {5.0f, 0.0f, -3.0f},
                           .direction = {-1.0f, 0.0f, 0.0f},
                           .speed = 3.0f }
    });

    /* Entity 3: use the PNG-loaded sprite if available */
    int ent3_spr = (unit_spr >= 0) ? unit_spr : spr_id;
    bf_command(engine, (bf_cmd){
        .type = BF_CMD_ENTITY_CREATE,
        .entity_create = { .id = 3, .sprite_id = ent3_spr,
                           .position = {-4.0f, 0.0f, 2.0f},
                           .direction = {1.0f, 0.0f, 0.0f},
                           .speed = 3.0f }
    });
    /* Play walk animation on entity 3 */
    if (walk_anim >= 0) {
        bf_command(engine, (bf_cmd){
            .type = BF_CMD_ENTITY_ANIMATE,
            .entity_animate = { .id = 3, .anim_index = walk_anim }
        });
    }

    /* Camera starts looking at the scene */
    bf_command(engine, (bf_cmd){
        .type = BF_CMD_CAMERA_SET,
        .camera_set = {
            .position = {0.0f, 15.0f, 15.0f},
            .direction = {0.0f, -0.4f, -1.0f}
        }
    });

    float cam_yaw = 0.0f;  /* facing -Z initially */
    float cam_x = 0.0f, cam_y = 15.0f, cam_z = 15.0f;
    int selected_id = 0;

    Uint32 fps_last = SDL_GetTicks();
    Uint32 frame_last = SDL_GetTicks();
    int fps_frames = 0;
    char title_buf[128];
    int running = 1;

    while (running) {
        Uint32 frame_now = SDL_GetTicks();
        float dt = (frame_now - frame_last) / 1000.0f;
        frame_last = frame_now;
        if (dt > 0.1f) dt = 0.1f;  /* cap to avoid spiral of death */

        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) running = 0;
            if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE)
                running = 0;
            if (e.type == SDL_MOUSEBUTTONDOWN) {
                bf_pick_result pick = bf_pick(engine, e.button.x, e.button.y);
                if (e.button.button == SDL_BUTTON_LEFT) {
                    if (pick.type == BF_PICK_ENTITY) {
                        selected_id = pick.entity_id;
                        bf_command(engine, (bf_cmd){
                            .type = BF_CMD_SELECT,
                            .select = { .id = pick.entity_id }
                        });
                        fprintf(stderr, "Selected entity %d\n", pick.entity_id);
                    } else {
                        selected_id = 0;
                        bf_command(engine, (bf_cmd){
                            .type = BF_CMD_SELECT,
                            .select = { .id = 0 }
                        });
                        fprintf(stderr, "Deselected\n");
                    }
                } else if (e.button.button == SDL_BUTTON_RIGHT) {
                    if (selected_id > 0 && pick.type == BF_PICK_GROUND) {
                        vector dest = pick.position;
                        bf_command(engine, (bf_cmd){
                            .type = BF_CMD_ENTITY_MOVE,
                            .entity_move = { .id = selected_id,
                                             .position = dest }
                        });
                        fprintf(stderr, "Move entity %d to (%.1f, %.1f, %.1f)\n",
                                selected_id, dest.x, dest.y, dest.z);
                    }
                }
            }
        }

        /* Continuous key input for camera */
        const Uint8 *keys = SDL_GetKeyboardState(NULL);
        float move_x = 0.0f, move_z = 0.0f;

        if (keys[SDL_SCANCODE_W]) { move_x += sinf(cam_yaw); move_z += -cosf(cam_yaw); }
        if (keys[SDL_SCANCODE_S]) { move_x -= sinf(cam_yaw); move_z -= -cosf(cam_yaw); }
        if (keys[SDL_SCANCODE_A]) { move_x -= cosf(cam_yaw); move_z -= sinf(cam_yaw); }
        if (keys[SDL_SCANCODE_D]) { move_x += cosf(cam_yaw); move_z += sinf(cam_yaw); }
        if (keys[SDL_SCANCODE_LEFT])  cam_yaw -= ROT_SPEED * dt;
        if (keys[SDL_SCANCODE_RIGHT]) cam_yaw += ROT_SPEED * dt;
        if (keys[SDL_SCANCODE_SPACE]) cam_y += MOVE_SPEED * dt;
        if (keys[SDL_SCANCODE_LSHIFT]) cam_y -= MOVE_SPEED * dt;

        cam_x += move_x * MOVE_SPEED * dt;
        cam_z += move_z * MOVE_SPEED * dt;

        bf_command(engine, (bf_cmd){
            .type = BF_CMD_CAMERA_SET,
            .camera_set = {
                .position = {cam_x, cam_y, cam_z},
                .direction = {sinf(cam_yaw), -0.3f, -cosf(cam_yaw)}
            }
        });

        bf_tick(engine, dt);
        bf_render(engine, pixels);

        SDL_UpdateTexture(texture, NULL, pixels, WINDOW_W * sizeof(uint32_t));
        SDL_RenderCopy(renderer, texture, NULL, NULL);
        SDL_RenderPresent(renderer);

        fps_frames++;
        Uint32 now = SDL_GetTicks();
        if (now - fps_last >= 1000) {
            snprintf(title_buf, sizeof(title_buf),
                     "Battleforge - %d FPS (%dx%d)", fps_frames,
                     WINDOW_W, WINDOW_H);
            SDL_SetWindowTitle(window, title_buf);
            fprintf(stderr, "%d FPS\n", fps_frames);
            fps_frames = 0;
            fps_last = now;
        }
    }

    bf_destroy(engine);
    if (unit_sheet) slice_free(unit_sheet);
    free(pixels);
    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
