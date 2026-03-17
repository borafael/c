#include "battleforge.h"
#include "raytrace.h"
#include "thread_pool.h"
#include "vector.h"
#include "slice.h"
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <float.h>
#include <stdarg.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

#define CMD_QUEUE_SIZE 1024
#define MAX_ENTITIES 1024
#define MAX_SPRITES 256
#define MAX_ANGLES 32

typedef struct {
    int id;
    int sprite_id;
    vector position;
    vector direction;
    vector target;
    float speed;
    int active;
    int anim_index;     /* -1 = no animation */
    int anim_frame;     /* current frame within animation */
    float frame_timer;  /* time accumulator */
    float anim_fps;     /* playback speed */
} bf_entity;

typedef struct {
    vector position;
    vector direction;
} bf_camera_state;

typedef struct {
    uint32_t *pixels;
    const rt_viewport *viewport;
    int y_start;
    int y_end;
    const rt_camera *camera;
    const rt_scene *scene;
} render_task;

struct bf_engine {
    bf_config config;
    bf_camera_state camera;
    bf_map map;
    int map_set;

    struct {
        slice_sheet *sheet;
        float width;
        float height;
    } sprites[MAX_SPRITES];
    int sprite_count;

    bf_entity entities[MAX_ENTITIES];
    int entity_count;
    int selected_entity_id;

    bf_cmd cmd_queue[CMD_QUEUE_SIZE];
    int cmd_head;
    int cmd_tail;
    int cmd_count;

    /* Log ring buffer */
    bf_log_entry log_buffer[BF_LOG_BUFFER_SIZE];
    int log_write_pos;
    int log_count;

    rt_scene *scene;
    rt_camera *rt_cam;
    rt_viewport viewport;
    thread_pool *pool;
    int num_threads;
    render_task *tasks;
};

static void render_chunk_fn(void *arg) {
    render_task *t = (render_task *)arg;
    rt_render_chunk(t->pixels, t->viewport, t->y_start, t->y_end,
                    t->camera, t->scene);
}

void bf_map_generate_test_terrain(bf_map *map) {
    int rows = map->grid_rows;
    int cols = map->grid_cols;

    map->heights = calloc(rows * cols, sizeof(float));
    map->colors  = calloc((rows - 1) * (cols - 1) * 3, sizeof(uint8_t));
    map->normals = calloc(rows * cols * 3, sizeof(float));

    float cell_w = map->width / (float)(cols - 1);
    float cell_d = map->depth / (float)(rows - 1);

    float h_min = FLT_MAX, h_max = -FLT_MAX;
    for (int r = 0; r < rows; r++) {
        for (int c = 0; c < cols; c++) {
            float wx = c * cell_w;
            float wz = r * cell_d;
            float h = sinf(wx * 0.3f) * cosf(wz * 0.2f) * 5.0f
                    + sinf(wx * 0.7f + wz * 0.5f) * 2.5f;
            map->heights[r * cols + c] = h;
            if (h < h_min) h_min = h;
            if (h > h_max) h_max = h;
        }
    }

    float range = h_max - h_min;
    if (range < 1e-6f) range = 1.0f;
    for (int r = 0; r < rows; r++) {
        for (int c = 0; c < cols; c++) {
            map->heights[r * cols + c] =
                ((map->heights[r * cols + c] - h_min) / range) * map->max_height;
        }
    }

    for (int r = 0; r < rows - 1; r++) {
        for (int c = 0; c < cols - 1; c++) {
            float avg = (map->heights[r * cols + c]
                       + map->heights[r * cols + c + 1]
                       + map->heights[(r + 1) * cols + c]
                       + map->heights[(r + 1) * cols + c + 1]) * 0.25f;
            float t = avg / map->max_height;
            int ci = (r * (cols - 1) + c) * 3;
            if (t < 0.3f) {
                map->colors[ci]     = 40;
                map->colors[ci + 1] = 120;
                map->colors[ci + 2] = 40;
            } else if (t < 0.7f) {
                map->colors[ci]     = 80;
                map->colors[ci + 1] = 160;
                map->colors[ci + 2] = 60;
            } else {
                map->colors[ci]     = 140;
                map->colors[ci + 1] = 110;
                map->colors[ci + 2] = 70;
            }
        }
    }

    for (int r = 0; r < rows; r++) {
        for (int c = 0; c < cols; c++) {
            vector sum = {0, 0, 0};
            int count = 0;
            for (int dr = -1; dr <= 0; dr++) {
                for (int dc = -1; dc <= 0; dc++) {
                    int cr = r + dr;
                    int cc = c + dc;
                    if (cr < 0 || cr >= rows - 1 || cc < 0 || cc >= cols - 1)
                        continue;
                    float h00 = map->heights[cr * cols + cc];
                    float h10 = map->heights[(cr + 1) * cols + cc];
                    float h01 = map->heights[cr * cols + cc + 1];
                    float h11 = map->heights[(cr + 1) * cols + cc + 1];

                    float cw = map->width / (float)(cols - 1);
                    float cd = map->depth / (float)(rows - 1);

                    vector a0 = {cc * cw, h00, cr * cd};
                    vector a1 = {cc * cw, h10, (cr + 1) * cd};
                    vector a2 = {(cc + 1) * cw, h01, cr * cd};
                    vector fn_a = vector_normalize(vector_cross(
                        vector_sub(a1, a0), vector_sub(a2, a0)));
                    sum = vector_add(sum, fn_a);
                    count++;

                    vector b0 = {cc * cw, h10, (cr + 1) * cd};
                    vector b1 = {(cc + 1) * cw, h11, (cr + 1) * cd};
                    vector b2 = {(cc + 1) * cw, h01, cr * cd};
                    vector fn_b = vector_normalize(vector_cross(
                        vector_sub(b1, b0), vector_sub(b2, b0)));
                    sum = vector_add(sum, fn_b);
                    count++;
                }
            }
            vector n = (count > 0) ? vector_normalize(sum) : (vector){0, 1, 0};
            int idx = (r * cols + c) * 3;
            map->normals[idx]     = n.x;
            map->normals[idx + 1] = n.y;
            map->normals[idx + 2] = n.z;
        }
    }
}

float bf_map_height_at(const bf_map *map, float x, float z) {
    if (!map->heights) return 0.0f;

    int cols = map->grid_cols;
    int rows = map->grid_rows;
    float cell_w = map->width / (float)(cols - 1);
    float cell_d = map->depth / (float)(rows - 1);

    float gx = x / cell_w;
    float gz = z / cell_d;

    if (gx < 0.0f) gx = 0.0f;
    if (gx > (float)(cols - 2)) gx = (float)(cols - 2);
    if (gz < 0.0f) gz = 0.0f;
    if (gz > (float)(rows - 2)) gz = (float)(rows - 2);

    int c0 = (int)floorf(gx);
    int r0 = (int)floorf(gz);
    if (c0 >= cols - 1) c0 = cols - 2;
    if (r0 >= rows - 1) r0 = rows - 2;

    float fx = gx - c0;
    float fz = gz - r0;

    float h00 = map->heights[r0 * cols + c0];
    float h01 = map->heights[r0 * cols + c0 + 1];
    float h10 = map->heights[(r0 + 1) * cols + c0];
    float h11 = map->heights[(r0 + 1) * cols + c0 + 1];

    float h = h00 * (1.0f - fx) * (1.0f - fz)
            + h01 * fx * (1.0f - fz)
            + h10 * (1.0f - fx) * fz
            + h11 * fx * fz;
    return h;
}

bf_engine *bf_create(bf_config config) {
    bf_engine *e = calloc(1, sizeof(bf_engine));
    if (!e) return NULL;

    e->config = config;
    e->viewport = (rt_viewport){ config.render_width, config.render_height, config.fov };

    /* Default camera */
    e->camera.position = (vector){0.0f, 5.0f, 10.0f};
    e->camera.direction = (vector){0.0f, -0.3f, -1.0f};

    /* Raytracer resources */
    e->scene = rt_scene_create();
    e->rt_cam = rt_camera_create(e->camera.position, e->camera.direction);
    if (!e->scene || !e->rt_cam) {
        bf_destroy(e);
        return NULL;
    }

    /* Thread pool */
    int nt = config.num_threads;
    if (nt <= 0) {
#ifdef _WIN32
        SYSTEM_INFO si;
        GetSystemInfo(&si);
        nt = (int)si.dwNumberOfProcessors;
#else
        nt = (int)sysconf(_SC_NPROCESSORS_ONLN);
#endif
        if (nt < 1) nt = 4;
    }
    e->num_threads = nt;
    e->pool = thread_pool_create(nt);
    e->tasks = malloc(sizeof(render_task) * nt);
    if (!e->pool || !e->tasks) {
        bf_destroy(e);
        return NULL;
    }

    return e;
}

void bf_destroy(bf_engine *e) {
    if (!e) return;
    if (e->pool) thread_pool_destroy(e->pool);
    free(e->tasks);
    if (e->rt_cam) rt_camera_destroy(e->rt_cam);
    if (e->scene) rt_scene_destroy(e->scene);
    free(e->map.heights);
    free(e->map.colors);
    free(e->map.normals);
    free(e);
}

int bf_register_sprite(bf_engine *e, slice_sheet *sheet,
                       float world_width, float world_height) {
    if (e->sprite_count >= MAX_SPRITES) return -1;
    int id = e->sprite_count;
    e->sprites[e->sprite_count].sheet = sheet;
    e->sprites[e->sprite_count].width = world_width;
    e->sprites[e->sprite_count].height = world_height;
    e->sprite_count++;
    return id;
}

void bf_set_map(bf_engine *e, bf_map map) {
    e->map = map;
    e->map_set = 1;
}

int bf_command(bf_engine *e, bf_cmd cmd) {
    if (e->cmd_count >= CMD_QUEUE_SIZE) return -1;
    e->cmd_queue[e->cmd_tail] = cmd;
    e->cmd_tail = (e->cmd_tail + 1) % CMD_QUEUE_SIZE;
    e->cmd_count++;
    return 0;
}

/* --- Logging --- */

void bf_log(bf_engine *e, bf_log_level level, const char *fmt, ...) {
    bf_log_entry *entry = &e->log_buffer[e->log_write_pos];
    entry->level = level;
    va_list args;
    va_start(args, fmt);
    vsnprintf(entry->text, BF_LOG_TEXT_SIZE, fmt, args);
    va_end(args);
    e->log_write_pos = (e->log_write_pos + 1) % BF_LOG_BUFFER_SIZE;
    if (e->log_count < BF_LOG_BUFFER_SIZE)
        e->log_count++;
}

int bf_log_count(const bf_engine *e) {
    return e->log_count;
}

const bf_log_entry *bf_log_get(const bf_engine *e, int index) {
    if (index < 0 || index >= e->log_count) return NULL;
    int pos = (e->log_write_pos - e->log_count + index + BF_LOG_BUFFER_SIZE)
              % BF_LOG_BUFFER_SIZE;
    return &e->log_buffer[pos];
}

/* --- Entity lookup by id --- */

static bf_entity *find_entity(bf_engine *e, int id) {
    for (int i = 0; i < e->entity_count; i++) {
        if (e->entities[i].id == id && e->entities[i].active)
            return &e->entities[i];
    }
    return NULL;
}

/* --- Command handlers --- */

static void cmd_camera_set(bf_engine *e, const bf_cmd *cmd) {
    e->camera.position = cmd->camera_set.position;
    e->camera.direction = cmd->camera_set.direction;
}

static void cmd_camera_move(bf_engine *e, const bf_cmd *cmd) {
    e->camera.position = vector_add(e->camera.position, cmd->camera_move.delta);
}

static void cmd_entity_create(bf_engine *e, const bf_cmd *cmd) {
    if (e->entity_count >= MAX_ENTITIES) {
        bf_log(e, BF_LOG_ERROR, "cannot create entity: max entities reached");
        return;
    }
    bf_entity ent = {
        .id = cmd->entity_create.id,
        .sprite_id = cmd->entity_create.sprite_id,
        .position = cmd->entity_create.position,
        .direction = cmd->entity_create.direction,
        .target = cmd->entity_create.position,
        .speed = cmd->entity_create.speed,
        .active = 1,
        .anim_index = -1,
        .anim_frame = 0,
        .frame_timer = 0.0f,
        .anim_fps = 0.0f
    };
    e->entities[e->entity_count++] = ent;
    bf_log(e, BF_LOG_INFO, "entity %d created at (%.1f, %.1f, %.1f)",
           ent.id, ent.position.x, ent.position.y, ent.position.z);
}

static void cmd_entity_destroy(bf_engine *e, const bf_cmd *cmd) {
    bf_entity *ent = find_entity(e, cmd->entity_destroy.id);
    if (ent) {
        ent->active = 0;
        if (e->selected_entity_id == ent->id)
            e->selected_entity_id = 0;
        bf_log(e, BF_LOG_INFO, "entity %d destroyed", cmd->entity_destroy.id);
    }
}

static void cmd_entity_move(bf_engine *e, const bf_cmd *cmd) {
    bf_entity *ent = find_entity(e, cmd->entity_move.id);
    if (ent) {
        ent->target = cmd->entity_move.position;
        bf_log(e, BF_LOG_INFO, "entity %d moving to (%.1f, %.1f, %.1f)",
               cmd->entity_move.id, cmd->entity_move.position.x,
               cmd->entity_move.position.y, cmd->entity_move.position.z);
    }
}

static void cmd_entity_face(bf_engine *e, const bf_cmd *cmd) {
    bf_entity *ent = find_entity(e, cmd->entity_face.id);
    if (ent) ent->direction = cmd->entity_face.direction;
}

static void cmd_entity_set_speed(bf_engine *e, const bf_cmd *cmd) {
    bf_entity *ent = find_entity(e, cmd->entity_set_speed.id);
    if (ent) ent->speed = cmd->entity_set_speed.speed;
}

static void cmd_select(bf_engine *e, const bf_cmd *cmd) {
    if (cmd->select.id <= 0) {
        e->selected_entity_id = 0;
        bf_log(e, BF_LOG_INFO, "deselected");
        return;
    }
    bf_entity *ent = find_entity(e, cmd->select.id);
    if (ent) {
        e->selected_entity_id = cmd->select.id;
        bf_log(e, BF_LOG_INFO, "selected entity %d", cmd->select.id);
    }
}

static void cmd_entity_animate(bf_engine *e, const bf_cmd *cmd) {
    bf_entity *ent = find_entity(e, cmd->entity_animate.id);
    if (!ent) return;
    if (ent->sprite_id < 0 || ent->sprite_id >= e->sprite_count) return;
    slice_sheet *sheet = e->sprites[ent->sprite_id].sheet;
    if (!sheet) return;
    int ai = cmd->entity_animate.anim_index;
    if (ai < 0 || ai >= sheet->anim_count) return;
    ent->anim_index = ai;
    ent->anim_frame = 0;
    ent->frame_timer = 0.0f;
    ent->anim_fps = sheet->fps;
}

/* --- Dispatch table --- */

static void (*cmd_handlers[BF_CMD_COUNT])(bf_engine *, const bf_cmd *) = {
    [BF_CMD_CAMERA_SET]        = cmd_camera_set,
    [BF_CMD_CAMERA_MOVE]       = cmd_camera_move,
    [BF_CMD_ENTITY_CREATE]     = cmd_entity_create,
    [BF_CMD_ENTITY_DESTROY]    = cmd_entity_destroy,
    [BF_CMD_ENTITY_MOVE]       = cmd_entity_move,
    [BF_CMD_ENTITY_FACE]       = cmd_entity_face,
    [BF_CMD_ENTITY_SET_SPEED]  = cmd_entity_set_speed,
    [BF_CMD_SELECT]            = cmd_select,
    [BF_CMD_ENTITY_ANIMATE]    = cmd_entity_animate,
};

void bf_tick(bf_engine *e, float dt) {
    /* Process command queue */
    while (e->cmd_count > 0) {
        bf_cmd *cmd = &e->cmd_queue[e->cmd_head];
        if (cmd->type >= 0 && cmd->type < BF_CMD_COUNT && cmd_handlers[cmd->type])
            cmd_handlers[cmd->type](e, cmd);
        e->cmd_head = (e->cmd_head + 1) % CMD_QUEUE_SIZE;
        e->cmd_count--;
    }

    /* Advance entity movement */
    for (int i = 0; i < e->entity_count; i++) {
        bf_entity *ent = &e->entities[i];
        if (!ent->active || ent->speed <= 0.0f) continue;

        vector to_target = vector_sub(ent->target, ent->position);
        to_target.y = 0.0f;  /* XZ-only distance */
        float dist = vector_magnitude(to_target);
        float step = ent->speed * dt;

        if (dist <= step) {
            ent->position.x = ent->target.x;
            ent->position.z = ent->target.z;
        } else {
            vector move_dir = vector_scale(to_target, 1.0f / dist);
            ent->direction = move_dir;
            ent->position.x += move_dir.x * step;
            ent->position.z += move_dir.z * step;
        }

        /* Snap to terrain height */
        if (e->map_set && e->map.heights) {
            ent->position.y = bf_map_height_at(&e->map,
                                                ent->position.x, ent->position.z);
        }
    }

    /* Advance animation */
    for (int i = 0; i < e->entity_count; i++) {
        bf_entity *ent = &e->entities[i];
        if (!ent->active || ent->anim_index < 0) continue;
        if (ent->sprite_id < 0 || ent->sprite_id >= e->sprite_count) continue;

        slice_sheet *sheet = e->sprites[ent->sprite_id].sheet;
        if (!sheet || ent->anim_index >= sheet->anim_count) continue;
        slice_anim *anim = &sheet->anims[ent->anim_index];
        if (anim->column_count <= 1) continue;
        if (ent->anim_fps <= 0.0f) continue;

        ent->frame_timer += dt;
        float interval = 1.0f / ent->anim_fps;
        while (ent->frame_timer >= interval) {
            ent->frame_timer -= interval;
            ent->anim_frame++;
            if (ent->anim_frame >= anim->column_count) {
                if (anim->loop)
                    ent->anim_frame = 0;
                else
                    ent->anim_frame = anim->column_count - 1;
            }
        }
    }
}

static slice_sheet *build_sprite_frames(bf_engine *e, bf_entity *ent,
                                        rt_frame *out_frames) {
    if (ent->sprite_id < 0 || ent->sprite_id >= e->sprite_count)
        return NULL;
    slice_sheet *sheet = e->sprites[ent->sprite_id].sheet;
    if (!sheet) return NULL;

    int col = 0;
    if (ent->anim_index >= 0 && ent->anim_index < sheet->anim_count) {
        slice_anim *anim = &sheet->anims[ent->anim_index];
        if (anim->column_count > 0 && ent->anim_frame < anim->column_count)
            col = anim->columns[ent->anim_frame];
    }

    if (col < 0 || col >= sheet->total_columns) col = 0;

    for (int a = 0; a < sheet->angles; a++) {
        out_frames[a] = (rt_frame){
            .pixels = sheet->pixels[a * sheet->total_columns + col],
            .width = sheet->frame_width,
            .height = sheet->frame_height
        };
    }
    return sheet;
}

void bf_render(bf_engine *e, uint32_t *pixel_buf) {
    /* Update camera */
    rt_camera_place(e->rt_cam, e->camera.position, e->camera.direction);

    /* Rebuild scene */
    rt_scene_clear(e->scene);

    /* Lighting from map */
    if (e->map_set) {
        rt_scene_set_ambient(e->scene, e->map.ambient);
        rt_scene_add_light(e->scene, (rt_light){
            .direction = e->map.light_dir,
            .intensity = e->map.light_intensity
        });

        /* Heightfield terrain */
        if (e->map.heights) {
            rt_heightfield hf = {
                .heights = e->map.heights,
                .colors = e->map.colors,
                .normals = e->map.normals,
                .rows = e->map.grid_rows,
                .cols = e->map.grid_cols,
                .world_width = e->map.width,
                .world_depth = e->map.depth,
                .origin_x = 0.0f,
                .origin_z = 0.0f,
                .max_height = e->map.max_height
            };
            rt_scene_add_heightfield(e->scene, &hf);
        }
    }

    /* Entities as sprites — frame arrays must outlive the render call,
       so allocate them outside the loop rather than per-iteration. */
    int ent_count = e->entity_count;
    rt_frame (*all_frames)[MAX_ANGLES] = malloc(ent_count * sizeof(*all_frames));
    for (int i = 0; i < ent_count; i++) {
        bf_entity *ent = &e->entities[i];
        if (!ent->active) continue;

        slice_sheet *sheet = build_sprite_frames(e, ent, all_frames[i]);
        if (!sheet) continue;

        float spr_h = e->sprites[ent->sprite_id].height;
        vector spr_pos = ent->position;
        spr_pos.y += spr_h * 0.5f;  /* sprite center above ground */

        rt_scene_add_sprite(e->scene, (rt_sprite){
            .position = spr_pos,
            .direction = ent->direction,
            .width = e->sprites[ent->sprite_id].width,
            .height = spr_h,
            .frame_count = sheet->angles,
            .frames = all_frames[i]
        });
    }

    /* Multithreaded render */
    int render_h = e->viewport.height;
    int rows_per = render_h / e->num_threads;
    if (rows_per < 1) rows_per = 1;
    int chunks = render_h / rows_per;
    if (chunks > e->num_threads) chunks = e->num_threads;

    for (int i = 0; i < chunks; i++) {
        e->tasks[i] = (render_task){
            .pixels = pixel_buf,
            .viewport = &e->viewport,
            .y_start = i * rows_per,
            .y_end = (i == chunks - 1) ? render_h : (i + 1) * rows_per,
            .camera = e->rt_cam,
            .scene = e->scene
        };
        thread_pool_submit(e->pool, render_chunk_fn, &e->tasks[i]);
    }
    thread_pool_wait(e->pool);
    free(all_frames);
}

bf_pick_result bf_pick(bf_engine *e, int screen_x, int screen_y) {
    bf_pick_result result = { .type = BF_PICK_SKY, .entity_id = 0,
                              .position = {0.0f, 0.0f, 0.0f} };

    /* Bounds check */
    if (screen_x < 0 || screen_x >= e->config.render_width ||
        screen_y < 0 || screen_y >= e->config.render_height)
        return result;

    /* Construct ray matching the raytracer's projection */
    vector origin, forward, right, up;
    rt_camera_get_basis(e->rt_cam, &origin, &forward, &right, &up);

    float half_w = (float)e->config.render_width * 0.5f;
    float half_h = (float)e->config.render_height * 0.5f;
    float fov_factor = (float)e->config.render_height /
                       (2.0f * tanf(e->config.fov / 2.0f));

    float sx = ((float)screen_x - half_w) / fov_factor;
    float sy = -((float)screen_y - half_h) / fov_factor;

    vector ray_dir = vector_add(
        vector_add(forward, vector_scale(right, sx)),
        vector_scale(up, sy));
    ray_dir = vector_normalize(ray_dir);

    /* Test entities (closest wins) */
    float closest_t = FLT_MAX;
    int closest_id = 0;
    vector closest_pos = {0};

    for (int i = 0; i < e->entity_count; i++) {
        bf_entity *ent = &e->entities[i];
        if (!ent->active) continue;

        rt_frame frames[32];
        slice_sheet *sheet = build_sprite_frames(e, ent, frames);
        if (!sheet) continue;

        rt_sprite spr = {
            .position = ent->position,
            .direction = ent->direction,
            .width = e->sprites[ent->sprite_id].width,
            .height = e->sprites[ent->sprite_id].height,
            .frame_count = sheet->angles,
            .frames = frames
        };

        vector hp;
        float t = rt_pick_sprite(origin, ray_dir, &spr, origin, &hp);
        if (t > 0.0f && t < closest_t) {
            closest_t = t;
            closest_id = ent->id;
            closest_pos = hp;
        }
    }

    if (closest_id > 0) {
        result.type = BF_PICK_ENTITY;
        result.entity_id = closest_id;
        result.position = closest_pos;
        return result;
    }

    /* Test heightfield terrain */
    if (e->map_set && e->map.heights) {
        rt_heightfield hf = {
            .heights = e->map.heights,
            .colors = e->map.colors,
            .normals = e->map.normals,
            .rows = e->map.grid_rows,
            .cols = e->map.grid_cols,
            .world_width = e->map.width,
            .world_depth = e->map.depth,
            .origin_x = 0.0f,
            .origin_z = 0.0f,
            .max_height = e->map.max_height
        };
        float t;
        vector hn;
        if (rt_intersect_heightfield(&hf, origin, ray_dir, &t, &hn, NULL, NULL)) {
            if (t > 0.0f) {
                result.type = BF_PICK_GROUND;
                result.position = vector_add(origin, vector_scale(ray_dir, t));
                return result;
            }
        }
    }

    return result;
}
