#include "battleforge.h"
#include "raytrace.h"
#include "thread_pool.h"
#include "vector.h"
#include <stdlib.h>
#include <math.h>
#include <float.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

#define CMD_QUEUE_SIZE 1024
#define MAX_ENTITIES 1024
#define MAX_SPRITES 256

typedef struct {
    int id;
    int sprite_id;
    vector position;
    vector direction;
    vector target;
    float speed;
    int active;
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

    bf_sprite_def sprites[MAX_SPRITES];
    int sprite_count;

    bf_entity entities[MAX_ENTITIES];
    int entity_count;
    int selected_entity_id;

    bf_cmd cmd_queue[CMD_QUEUE_SIZE];
    int cmd_head;
    int cmd_tail;
    int cmd_count;

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
    free(e);
}

int bf_register_sprite(bf_engine *e, bf_sprite_def def) {
    if (e->sprite_count >= MAX_SPRITES) return -1;
    int id = e->sprite_count;
    e->sprites[e->sprite_count++] = def;
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
    if (e->entity_count >= MAX_ENTITIES) return;
    bf_entity ent = {
        .id = cmd->entity_create.id,
        .sprite_id = cmd->entity_create.sprite_id,
        .position = cmd->entity_create.position,
        .direction = cmd->entity_create.direction,
        .target = cmd->entity_create.position,
        .speed = cmd->entity_create.speed,
        .active = 1
    };
    e->entities[e->entity_count++] = ent;
}

static void cmd_entity_destroy(bf_engine *e, const bf_cmd *cmd) {
    bf_entity *ent = find_entity(e, cmd->entity_destroy.id);
    if (ent) {
        ent->active = 0;
        if (e->selected_entity_id == ent->id)
            e->selected_entity_id = 0;
    }
}

static void cmd_entity_move(bf_engine *e, const bf_cmd *cmd) {
    bf_entity *ent = find_entity(e, cmd->entity_move.id);
    if (ent) ent->target = cmd->entity_move.position;
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
        return;
    }
    bf_entity *ent = find_entity(e, cmd->select.id);
    if (ent) e->selected_entity_id = cmd->select.id;
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
        float dist = vector_magnitude(to_target);
        float step = ent->speed * dt;

        if (dist <= step) {
            ent->position = ent->target;
        } else {
            vector move_dir = vector_scale(to_target, 1.0f / dist);
            ent->direction = move_dir;
            ent->position = vector_add(ent->position, vector_scale(move_dir, step));
        }
    }
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

        /* Ground plane */
        rt_scene_add_plane(e->scene, (rt_plane){
            .point = {0.0f, 0.0f, 0.0f},
            .normal = {0.0f, 1.0f, 0.0f},
            .color = {e->map.r, e->map.g, e->map.b}
        });
    }

    /* Entities as sprites */
    for (int i = 0; i < e->entity_count; i++) {
        bf_entity *ent = &e->entities[i];
        if (!ent->active) continue;
        if (ent->sprite_id < 0 || ent->sprite_id >= e->sprite_count) continue;

        bf_sprite_def *def = &e->sprites[ent->sprite_id];
        rt_scene_add_sprite(e->scene, (rt_sprite){
            .position = ent->position,
            .direction = ent->direction,
            .width = def->width,
            .height = def->height,
            .frame_count = def->frame_count,
            .frames = def->frames
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
        if (ent->sprite_id < 0 || ent->sprite_id >= e->sprite_count) continue;

        bf_sprite_def *def = &e->sprites[ent->sprite_id];
        rt_sprite spr = {
            .position = ent->position,
            .direction = ent->direction,
            .width = def->width,
            .height = def->height,
            .frame_count = def->frame_count,
            .frames = def->frames
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

    /* Test ground plane (y=0) */
    if (e->map_set && fabsf(ray_dir.y) > 1e-6f) {
        float t = -origin.y / ray_dir.y;
        if (t > 0.0f) {
            result.type = BF_PICK_GROUND;
            result.position = vector_add(origin, vector_scale(ray_dir, t));
            return result;
        }
    }

    return result;
}
