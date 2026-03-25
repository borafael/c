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

    /* ECS component arrays */
    uint32_t component_masks[MAX_ENTITIES];
    bf_position positions[MAX_ENTITIES];
    bf_visual visuals[MAX_ENTITIES];
    bf_locomotion locomotions[MAX_ENTITIES];
    bf_selection selections[MAX_ENTITIES];

    /* Free list for entity allocation */
    int free_stack[MAX_ENTITIES];
    int free_top;

    /* Unit definitions */
    bf_unit_def unit_defs[MAX_UNIT_DEFS];
    int unit_def_count;

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

/* Simple hash-based gradient noise for natural-looking terrain */
static float noise_hash(int ix, int iz) {
    int n = ix + iz * 1327;
    n = (n << 13) ^ n;
    return 1.0f - ((n * (n * n * 15731 + 789221) + 1376312589) & 0x7fffffff)
                  / 1073741824.0f;
}

static float noise_smooth(float x, float z) {
    int ix = (int)floorf(x);
    int iz = (int)floorf(z);
    float fx = x - ix;
    float fz = z - iz;
    /* smoothstep for less grid-aligned artifacts */
    float sx = fx * fx * (3.0f - 2.0f * fx);
    float sz = fz * fz * (3.0f - 2.0f * fz);

    float v00 = noise_hash(ix,     iz);
    float v10 = noise_hash(ix + 1, iz);
    float v01 = noise_hash(ix,     iz + 1);
    float v11 = noise_hash(ix + 1, iz + 1);

    float a = v00 + sx * (v10 - v00);
    float b = v01 + sx * (v11 - v01);
    return a + sz * (b - a);
}

static float noise_fbm(float x, float z, int octaves, float lacunarity,
                        float persistence) {
    float value = 0.0f;
    float amp   = 1.0f;
    float freq  = 1.0f;
    for (int i = 0; i < octaves; i++) {
        value += amp * noise_smooth(x * freq, z * freq);
        amp   *= persistence;
        freq  *= lacunarity;
    }
    return value;
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
            /* scale into noise space so features aren't too large or small */
            float h = noise_fbm(wx * 0.08f, wz * 0.08f, 6, 2.0f, 0.5f);

            /* gaussian mountain peak near the back-right of the map */
            float mx = 0.5f * map->width;
            float mz = 0.5f * map->depth;
            float mr = 12.0f;  /* radius of influence */
            float dx = wx - mx;
            float dz = wz - mz;
            float d2 = (dx * dx + dz * dz) / (mr * mr);
            h += 18.0f * expf(-d2);

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

    /* Initialize free list (push all indices, highest first so 0 is popped first) */
    e->free_top = -1;
    for (int i = MAX_ENTITIES - 1; i >= 0; i--)
        e->free_stack[++e->free_top] = i;
    e->selected_entity_id = -1;

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

/* --- Entity allocation helpers --- */

static int alloc_entity(bf_engine *e) {
    if (e->free_top < 0) return -1;
    return e->free_stack[e->free_top--];
}

static void free_entity(bf_engine *e, int id) {
    if (id < 0 || id >= MAX_ENTITIES) return;
    e->component_masks[id] = BF_COMP_NONE;
    e->free_stack[++e->free_top] = id;
}

/* --- Command handlers --- */

static void cmd_camera_set(bf_engine *e, const bf_cmd *cmd) {
    e->camera.position = cmd->camera_set.position;
    e->camera.direction = cmd->camera_set.direction;
}

static void cmd_camera_move(bf_engine *e, const bf_cmd *cmd) {
    e->camera.position = vector_add(e->camera.position, cmd->camera_move.delta);
}

static void cmd_register_unit(bf_engine *e, const bf_cmd *cmd) {
    if (e->unit_def_count >= MAX_UNIT_DEFS) {
        bf_log(e, BF_LOG_ERROR, "cannot register unit: max unit defs reached");
        return;
    }
    e->unit_defs[e->unit_def_count] = cmd->register_unit.def;
    bf_log(e, BF_LOG_INFO, "registered unit def %d: '%s'",
           e->unit_def_count, cmd->register_unit.def.name);
    e->unit_def_count++;
}

static void cmd_entity_create(bf_engine *e, const bf_cmd *cmd) {
    int def_id = cmd->entity_create.unit_def_id;
    if (def_id < 0 || def_id >= e->unit_def_count) {
        bf_log(e, BF_LOG_ERROR, "cannot create entity: invalid unit_def_id %d", def_id);
        return;
    }

    int id = alloc_entity(e);
    if (id < 0) {
        bf_log(e, BF_LOG_ERROR, "cannot create entity: max entities reached");
        return;
    }

    /* The client passes an id hint in cmd->entity_create.id but we use
       our own allocator.  To let the client reference entities by the id
       we return, we still honour the hint: if the requested slot is free
       we use it; otherwise we use whatever alloc gave us. */
    int requested = cmd->entity_create.id;
    if (requested >= 0 && requested < MAX_ENTITIES && requested != id) {
        /* Check if requested slot is free (i.e., mask == NONE) and swap back */
        if (e->component_masks[requested] == BF_COMP_NONE) {
            /* Push the id we got back, and use the requested one */
            e->free_stack[++e->free_top] = id;
            id = requested;
            /* Remove 'requested' from the free stack */
            for (int i = 0; i <= e->free_top; i++) {
                if (e->free_stack[i] == requested) {
                    e->free_stack[i] = e->free_stack[e->free_top--];
                    break;
                }
            }
        }
    }

    bf_unit_def *def = &e->unit_defs[def_id];

    /* Position component */
    e->positions[id].position = cmd->entity_create.position;
    e->positions[id].direction = cmd->entity_create.direction;

    /* Snap to terrain */
    if (e->map_set && e->map.heights) {
        e->positions[id].position.y = bf_map_height_at(&e->map,
            cmd->entity_create.position.x, cmd->entity_create.position.z);
    }

    uint32_t mask = BF_COMP_POSITION;

    /* Visual component */
    if (def->sprite_id >= 0) {
        e->visuals[id].sprite_id = def->sprite_id;
        e->visuals[id].anim_index = -1;
        e->visuals[id].anim_frame = 0;
        e->visuals[id].frame_timer = 0.0f;
        e->visuals[id].anim_fps = 0.0f;
        mask |= BF_COMP_VISUAL;
    }

    /* Selection component */
    e->selections[id].selected = 0;
    if (def->has_selection)
        mask |= BF_COMP_SELECTION;
    e->component_masks[id] = mask;

    bf_log(e, BF_LOG_INFO, "entity %d created at (%.1f, %.1f, %.1f)",
           id, e->positions[id].position.x, e->positions[id].position.y,
           e->positions[id].position.z);
}

static void cmd_entity_destroy(bf_engine *e, const bf_cmd *cmd) {
    int id = cmd->entity_destroy.id;
    if (id < 0 || id >= MAX_ENTITIES) return;
    if (!(e->component_masks[id] & BF_COMP_POSITION)) return;
    if (e->selected_entity_id == id)
        e->selected_entity_id = -1;
    free_entity(e, id);
    bf_log(e, BF_LOG_INFO, "entity %d destroyed", id);
}

static void cmd_entity_move(bf_engine *e, const bf_cmd *cmd) {
    int id = cmd->entity_move.id;
    if (id < 0 || id >= MAX_ENTITIES) return;
    if (!(e->component_masks[id] & BF_COMP_POSITION)) return;

    bf_loco_type ltype = cmd->entity_move.loco_type;

    if (ltype == BF_LOCO_INSTANT) {
        /* Instant: just set position directly */
        e->positions[id].position = cmd->entity_move.target;
        if (e->map_set && e->map.heights) {
            e->positions[id].position.y = bf_map_height_at(&e->map,
                cmd->entity_move.target.x, cmd->entity_move.target.z);
        }
        e->component_masks[id] &= ~BF_COMP_LOCOMOTION;
        bf_log(e, BF_LOG_INFO, "entity %d teleported to (%.1f, %.1f, %.1f)",
               id, e->positions[id].position.x, e->positions[id].position.y,
               e->positions[id].position.z);
        return;
    }

    e->locomotions[id].type = ltype;
    if (ltype == BF_LOCO_LINEAR) {
        e->locomotions[id].linear = (bf_trajectory_linear){
            .origin = e->positions[id].position,
            .target = cmd->entity_move.target,
            .speed = cmd->entity_move.speed,
            .progress = 0.0f
        };
    } else if (ltype == BF_LOCO_PARABOLIC) {
        e->locomotions[id].parabolic = (bf_trajectory_parabolic){
            .origin = e->positions[id].position,
            .target = cmd->entity_move.target,
            .speed = cmd->entity_move.speed,
            .progress = 0.0f,
            .arc_height = 5.0f
        };
    }
    e->component_masks[id] |= BF_COMP_LOCOMOTION;

    bf_log(e, BF_LOG_INFO, "entity %d moving to (%.1f, %.1f, %.1f)",
           id, cmd->entity_move.target.x,
           cmd->entity_move.target.y, cmd->entity_move.target.z);
}

static void cmd_entity_face(bf_engine *e, const bf_cmd *cmd) {
    int id = cmd->entity_face.id;
    if (id < 0 || id >= MAX_ENTITIES) return;
    if (!(e->component_masks[id] & BF_COMP_POSITION)) return;
    e->positions[id].direction = cmd->entity_face.direction;
}

static void cmd_select(bf_engine *e, const bf_cmd *cmd) {
    /* Deselect previous */
    if (e->selected_entity_id >= 0 && e->selected_entity_id < MAX_ENTITIES &&
        (e->component_masks[e->selected_entity_id] & BF_COMP_SELECTION)) {
        e->selections[e->selected_entity_id].selected = 0;
    }

    if (cmd->select.id < 0) {
        e->selected_entity_id = -1;
        bf_log(e, BF_LOG_INFO, "deselected");
        return;
    }
    int id = cmd->select.id;
    if (id >= MAX_ENTITIES) return;
    if (!(e->component_masks[id] & BF_COMP_SELECTION)) return;
    e->selections[id].selected = 1;
    e->selected_entity_id = id;
    bf_log(e, BF_LOG_INFO, "selected entity %d", id);
}

static void cmd_entity_animate(bf_engine *e, const bf_cmd *cmd) {
    int id = cmd->entity_animate.id;
    if (id < 0 || id >= MAX_ENTITIES) return;
    if (!(e->component_masks[id] & BF_COMP_VISUAL)) return;
    int sprite_id = e->visuals[id].sprite_id;
    if (sprite_id < 0 || sprite_id >= e->sprite_count) return;
    slice_sheet *sheet = e->sprites[sprite_id].sheet;
    if (!sheet) return;
    int ai = cmd->entity_animate.anim_index;
    if (ai < 0 || ai >= sheet->anim_count) return;
    e->visuals[id].anim_index = ai;
    e->visuals[id].anim_frame = 0;
    e->visuals[id].frame_timer = 0.0f;
    e->visuals[id].anim_fps = sheet->fps;
}

/* --- Dispatch table --- */

static void (*cmd_handlers[BF_CMD_COUNT])(bf_engine *, const bf_cmd *) = {
    [BF_CMD_CAMERA_SET]        = cmd_camera_set,
    [BF_CMD_CAMERA_MOVE]       = cmd_camera_move,
    [BF_CMD_ENTITY_CREATE]     = cmd_entity_create,
    [BF_CMD_ENTITY_DESTROY]    = cmd_entity_destroy,
    [BF_CMD_ENTITY_MOVE]       = cmd_entity_move,
    [BF_CMD_ENTITY_FACE]       = cmd_entity_face,
    [BF_CMD_REGISTER_UNIT]     = cmd_register_unit,
    [BF_CMD_SELECT]            = cmd_select,
    [BF_CMD_ENTITY_ANIMATE]    = cmd_entity_animate,
};

/* --- Locomotion system --- */

static void advance_linear(bf_engine *e, int id, float dt) {
    bf_trajectory_linear *traj = &e->locomotions[id].linear;
    vector to_target = vector_sub(traj->target, e->positions[id].position);
    to_target.y = 0.0f;  /* XZ-only distance */
    float dist = vector_magnitude(to_target);
    float step = traj->speed * dt;

    if (dist <= step) {
        e->positions[id].position.x = traj->target.x;
        e->positions[id].position.z = traj->target.z;
        traj->progress = 1.0f;
    } else {
        vector move_dir = vector_scale(to_target, 1.0f / dist);
        e->positions[id].direction = move_dir;
        e->positions[id].position.x += move_dir.x * step;
        e->positions[id].position.z += move_dir.z * step;
        float total_dist = vector_magnitude(vector_sub(traj->target, traj->origin));
        if (total_dist > 1e-6f)
            traj->progress = 1.0f - (dist - step) / total_dist;
    }

    /* Snap to terrain height */
    if (e->map_set && e->map.heights) {
        e->positions[id].position.y = bf_map_height_at(&e->map,
            e->positions[id].position.x, e->positions[id].position.z);
    }
}

static void advance_parabolic(bf_engine *e, int id, float dt) {
    bf_trajectory_parabolic *traj = &e->locomotions[id].parabolic;
    vector to_target = vector_sub(traj->target, traj->origin);
    to_target.y = 0.0f;
    float total_dist = vector_magnitude(to_target);
    if (total_dist < 1e-6f) {
        traj->progress = 1.0f;
        e->positions[id].position = traj->target;
        return;
    }

    float step = (traj->speed * dt) / total_dist;
    traj->progress += step;
    if (traj->progress > 1.0f) traj->progress = 1.0f;

    float t = traj->progress;
    /* Lerp XZ */
    e->positions[id].position.x = traj->origin.x + (traj->target.x - traj->origin.x) * t;
    e->positions[id].position.z = traj->origin.z + (traj->target.z - traj->origin.z) * t;
    /* Parabolic arc for Y */
    float base_y = traj->origin.y + (traj->target.y - traj->origin.y) * t;
    float arc = 4.0f * traj->arc_height * t * (1.0f - t);
    e->positions[id].position.y = base_y + arc;

    /* Update direction */
    vector dir = vector_sub(traj->target, traj->origin);
    dir.y = 0.0f;
    float mag = vector_magnitude(dir);
    if (mag > 1e-6f)
        e->positions[id].direction = vector_scale(dir, 1.0f / mag);
}

typedef void (*loco_advance_fn)(bf_engine *, int, float);
static loco_advance_fn loco_advancers[] = {
    [BF_LOCO_LINEAR]    = advance_linear,
    [BF_LOCO_PARABOLIC] = advance_parabolic,
    [BF_LOCO_INSTANT]   = NULL,  /* handled at command time */
};

static void system_locomotion(bf_engine *e, float dt) {
    uint32_t required = BF_COMP_POSITION | BF_COMP_LOCOMOTION;
    for (int i = 0; i < MAX_ENTITIES; i++) {
        if ((e->component_masks[i] & required) != required) continue;

        bf_loco_type ltype = e->locomotions[i].type;
        if (ltype >= 0 && ltype <= BF_LOCO_PARABOLIC && loco_advancers[ltype])
            loco_advancers[ltype](e, i, dt);

        /* Check if movement complete */
        float progress = 0.0f;
        if (ltype == BF_LOCO_LINEAR)
            progress = e->locomotions[i].linear.progress;
        else if (ltype == BF_LOCO_PARABOLIC)
            progress = e->locomotions[i].parabolic.progress;

        if (progress >= 1.0f) {
            e->component_masks[i] &= ~BF_COMP_LOCOMOTION;
            /* Snap to terrain for parabolic landing */
            if (ltype == BF_LOCO_PARABOLIC && e->map_set && e->map.heights) {
                e->positions[i].position.y = bf_map_height_at(&e->map,
                    e->positions[i].position.x, e->positions[i].position.z);
            }
        }
    }
}

static void system_animation(bf_engine *e, float dt) {
    for (int i = 0; i < MAX_ENTITIES; i++) {
        if (!(e->component_masks[i] & BF_COMP_VISUAL)) continue;
        bf_visual *vis = &e->visuals[i];
        if (vis->anim_index < 0) continue;
        if (vis->sprite_id < 0 || vis->sprite_id >= e->sprite_count) continue;

        slice_sheet *sheet = e->sprites[vis->sprite_id].sheet;
        if (!sheet || vis->anim_index >= sheet->anim_count) continue;
        slice_anim *anim = &sheet->anims[vis->anim_index];
        if (anim->column_count <= 1) continue;
        if (vis->anim_fps <= 0.0f) continue;

        vis->frame_timer += dt;
        float interval = 1.0f / vis->anim_fps;
        while (vis->frame_timer >= interval) {
            vis->frame_timer -= interval;
            vis->anim_frame++;
            if (vis->anim_frame >= anim->column_count) {
                if (anim->loop)
                    vis->anim_frame = 0;
                else
                    vis->anim_frame = anim->column_count - 1;
            }
        }
    }
}

void bf_tick(bf_engine *e, float dt) {
    /* Process command queue */
    while (e->cmd_count > 0) {
        bf_cmd *cmd = &e->cmd_queue[e->cmd_head];
        if (cmd->type >= 0 && cmd->type < BF_CMD_COUNT && cmd_handlers[cmd->type])
            cmd_handlers[cmd->type](e, cmd);
        e->cmd_head = (e->cmd_head + 1) % CMD_QUEUE_SIZE;
        e->cmd_count--;
    }

    system_locomotion(e, dt);
    system_animation(e, dt);
}

static slice_sheet *build_sprite_frames(bf_engine *e, int entity_id,
                                        rt_frame *out_frames) {
    bf_visual *vis = &e->visuals[entity_id];
    if (vis->sprite_id < 0 || vis->sprite_id >= e->sprite_count)
        return NULL;
    slice_sheet *sheet = e->sprites[vis->sprite_id].sheet;
    if (!sheet) return NULL;

    int col = 0;
    if (vis->anim_index >= 0 && vis->anim_index < sheet->anim_count) {
        slice_anim *anim = &sheet->anims[vis->anim_index];
        if (anim->column_count > 0 && vis->anim_frame < anim->column_count)
            col = anim->columns[vis->anim_frame];
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
    /* Count active entities for allocation */
    int active_count = 0;
    for (int i = 0; i < MAX_ENTITIES; i++) {
        if ((e->component_masks[i] & (BF_COMP_POSITION | BF_COMP_VISUAL)) ==
            (BF_COMP_POSITION | BF_COMP_VISUAL))
            active_count++;
    }
    rt_frame (*all_frames)[MAX_ANGLES] = malloc(
        (active_count > 0 ? active_count : 1) * sizeof(*all_frames));
    if (!all_frames) return;
    int frame_idx = 0;
    for (int i = 0; i < MAX_ENTITIES; i++) {
        if ((e->component_masks[i] & (BF_COMP_POSITION | BF_COMP_VISUAL)) !=
            (BF_COMP_POSITION | BF_COMP_VISUAL))
            continue;

        slice_sheet *sheet = build_sprite_frames(e, i, all_frames[frame_idx]);
        if (!sheet) continue;

        int sprite_id = e->visuals[i].sprite_id;
        float spr_h = e->sprites[sprite_id].height;
        vector spr_pos = e->positions[i].position;
        spr_pos.y += spr_h * 0.5f;  /* sprite center above ground */

        rt_scene_add_sprite(e->scene, (rt_sprite){
            .position = spr_pos,
            .direction = e->positions[i].direction,
            .width = e->sprites[sprite_id].width,
            .height = spr_h,
            .frame_count = sheet->angles,
            .frames = all_frames[frame_idx]
        });
        frame_idx++;
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
    bf_pick_result result = { .type = BF_PICK_SKY, .entity_id = -1,
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
    int closest_id = -1;
    vector closest_pos = {0};

    uint32_t pick_mask = BF_COMP_POSITION | BF_COMP_VISUAL | BF_COMP_SELECTION;
    for (int i = 0; i < MAX_ENTITIES; i++) {
        if ((e->component_masks[i] & pick_mask) != pick_mask) continue;

        rt_frame frames[32];
        slice_sheet *sheet = build_sprite_frames(e, i, frames);
        if (!sheet) continue;

        int sprite_id = e->visuals[i].sprite_id;
        rt_sprite spr = {
            .position = e->positions[i].position,
            .direction = e->positions[i].direction,
            .width = e->sprites[sprite_id].width,
            .height = e->sprites[sprite_id].height,
            .frame_count = sheet->angles,
            .frames = frames
        };

        vector hp;
        float t = rt_pick_sprite(origin, ray_dir, &spr, origin, &hp);
        if (t > 0.0f && t < closest_t) {
            closest_t = t;
            closest_id = i;
            closest_pos = hp;
        }
    }

    if (closest_id >= 0) {
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
