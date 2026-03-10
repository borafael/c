#include "nbody.h"
#include "render.h"
#include "vector.h"
#include "thread_pool.h"
#include "raytrace.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stddef.h>
#include <time.h>

#define MAX_ENTITIES 10000
#define MAX_THREADS 64
#define ARRAY_LEN(a) (sizeof(a) / sizeof((a)[0]))

static inline float clampf(float value, float min, float max) {
    if (value < min) return min;
    if (value > max) return max;
    return value;
}

static int num_entities = 500;
static float gravity = 0.5f;
static float dt = 0.016f;
static float world_radius = 800.0f;
static float softening = 5.0f;
static int num_threads = 8;
static float rotation_speed = 0.05f;

typedef enum {
    NONE     = 0,
    POSITION = (1 << 0),
    PHYSICS  = (1 << 1)
} component_type;

typedef struct {
    vector coordinates;
} position_component;

typedef struct {
    vector velocity;
    vector acceleration;
    float mass;
} physics_component;

static unsigned int entity_masks[MAX_ENTITIES] = {0};
static position_component position_components[MAX_ENTITIES];
static physics_component physics_components[MAX_ENTITIES];

typedef struct {
    int i;
    int j;
} merge_pair;

static int free_stack[MAX_ENTITIES];
static int top = -1;

#define MAX_MERGES (MAX_ENTITIES / 2)
static merge_pair merge_list[MAX_MERGES];
static int merge_count = 0;

typedef struct {
    int start;
    int end;
    vector local_accel[MAX_ENTITIES];
    merge_pair local_merges[MAX_MERGES];
    int merge_count;
} force_task_args;

static thread_pool *pool;
static force_task_args task_args[MAX_THREADS];

static int bounds_enabled = 0;
static float camera_azimuth = 0.0f;
static float camera_elevation = 0.3f;
static float camera_distance = 1500.0f;
static float time_scale = 1.0f;
static rt_camera *camera = NULL;

/* Raytracer state (lazy-initialized in nbody_render) */
#define RT_SCALE 4
static rt_scene *rt_scene_ptr = NULL;
static uint32_t *pixel_buffer = NULL;
static void *rt_texture = NULL;
static int rt_width = 0;
static int rt_height = 0;

typedef struct {
    uint32_t *pixel_buf;
    const rt_viewport *viewport;
    int y_start;
    int y_end;
    const rt_camera *camera;
    const rt_scene *scene;
} render_chunk_args;

static int create_entity(void) {
    if (top >= 0) {
        return free_stack[top--];
    }
    return -1;
}

static void destroy_entity(int id) {
    entity_masks[id] = NONE;
    free_stack[++top] = id;
}

void nbody_set_bounds(int enabled) {
    bounds_enabled = enabled;
}

static void update_camera_from_orbital(void) {
    vector pos = {
        camera_distance * cosf(camera_elevation) * sinf(camera_azimuth),
        camera_distance * sinf(camera_elevation),
        -camera_distance * cosf(camera_elevation) * cosf(camera_azimuth)
    };
    vector dir = vector_normalize(vector_scale(pos, -1.0f));
    rt_camera_place(camera, pos, dir);
}

static void handle_reset(void)      { nbody_reset(); }
static void handle_zoom_in(void)    { camera_distance = clampf(camera_distance / 1.1f, 10.0f, 20000.0f); }
static void handle_zoom_out(void)   { camera_distance = clampf(camera_distance * 1.1f, 10.0f, 20000.0f); }
static void handle_pan_left(void)   { camera_azimuth -= rotation_speed; }
static void handle_pan_right(void)  { camera_azimuth += rotation_speed; }
static void handle_pan_up(void)     { camera_elevation = clampf(camera_elevation + rotation_speed, -1.5f, 1.5f); }
static void handle_pan_down(void)   { camera_elevation = clampf(camera_elevation - rotation_speed, -1.5f, 1.5f); }
static void handle_speed_up(void)   { time_scale = clampf(time_scale * 1.5f, 0.1f, 50.0f); }
static void handle_speed_down(void) { time_scale = clampf(time_scale / 1.5f, 0.1f, 50.0f); }

typedef struct {
    size_t event_offset;
    void (*handler)(void);
} input_action;

static const input_action actions[] = {
    { offsetof(input_events, reset),      handle_reset },
    { offsetof(input_events, zoom_in),    handle_zoom_in },
    { offsetof(input_events, zoom_out),   handle_zoom_out },
    { offsetof(input_events, pan_left),   handle_pan_left },
    { offsetof(input_events, pan_right),  handle_pan_right },
    { offsetof(input_events, pan_up),     handle_pan_up },
    { offsetof(input_events, pan_down),   handle_pan_down },
    { offsetof(input_events, speed_up),   handle_speed_up },
    { offsetof(input_events, speed_down), handle_speed_down },
};

void nbody_handle_input(const input_events *events) {
    for (size_t i = 0; i < ARRAY_LEN(actions); i++) {
        if (*(const int *)((const char *)events + actions[i].event_offset)) {
            actions[i].handler();
        }
    }
    update_camera_from_orbital();
}

nbody_config nbody_default_config(void) {
    nbody_config config;
    config.num_entities = 200;
    config.gravity = 0.5f;
    config.dt = 0.016f;
    config.world_radius = 400.0f;
    config.softening = 5.0f;
    config.num_threads = 8;
    config.rotation_speed = 0.05f;
    return config;
}

void nbody_init(const nbody_config *config) {
    num_entities = config->num_entities;
    if (num_entities > MAX_ENTITIES) {
        fprintf(stderr, "Warning: num_entities %d exceeds max %d, clamping\n",
                num_entities, MAX_ENTITIES);
        num_entities = MAX_ENTITIES;
    }
    gravity = config->gravity;
    dt = config->dt;
    world_radius = config->world_radius;
    softening = config->softening;
    num_threads = config->num_threads;
    if (num_threads > MAX_THREADS) {
        fprintf(stderr, "Warning: num_threads %d exceeds max %d, clamping\n",
                num_threads, MAX_THREADS);
        num_threads = MAX_THREADS;
    }
    if (num_threads < 1) num_threads = 1;
    rotation_speed = config->rotation_speed;

    for (int i = MAX_ENTITIES - 1; i >= 0; i--) {
        free_stack[++top] = i;
    }
    pool = thread_pool_create(num_threads);
    if (!pool) {
        fprintf(stderr, "Failed to create thread pool\n");
        exit(EXIT_FAILURE);
    }

    camera = rt_camera_create((vector){0, 0, 0}, (vector){0, 0, -1});
    if (!camera) {
        fprintf(stderr, "Failed to create camera\n");
        exit(EXIT_FAILURE);
    }
    update_camera_from_orbital();
}

void nbody_spawn_entities(void) {
    srand(time(NULL));
    for (int i = 0; i < num_entities; i++) {
        int id = create_entity();
        if (id < 0) break;

        entity_masks[id] = POSITION | PHYSICS;

        /* Rejection sampling: random point in sphere */
        float x, y, z;
        do {
            x = ((float)rand() / RAND_MAX - 0.5f) * 2.0f;
            y = ((float)rand() / RAND_MAX - 0.5f) * 2.0f;
            z = ((float)rand() / RAND_MAX - 0.5f) * 2.0f;
        } while (x * x + y * y + z * z > 1.0f);

        position_components[id].coordinates = (vector){
            x * world_radius,
            y * world_radius,
            z * world_radius
        };

        physics_components[id].velocity = (vector){0, 0, 0};
        physics_components[id].acceleration = (vector){0, 0, 0};
        physics_components[id].mass = 1.0f + (float)(rand() % 10);
    }
}

void nbody_reset(void) {
    camera_azimuth = 0.0f;
    camera_elevation = 0.3f;
    camera_distance = 1500.0f;
    time_scale = 1.0f;
    for (int i = 0; i < MAX_ENTITIES; i++) {
        entity_masks[i] = NONE;
    }
    top = -1;
    for (int i = MAX_ENTITIES - 1; i >= 0; i--) {
        free_stack[++top] = i;
    }
    nbody_spawn_entities();

    update_camera_from_orbital();
}

static void render_chunk_task(void *arg) {
    render_chunk_args *a = (render_chunk_args *)arg;
    rt_render_chunk(a->pixel_buf, a->viewport,
                    a->y_start, a->y_end, a->camera, a->scene);
}

void nbody_cleanup(void) {
    if (pool) {
        thread_pool_destroy(pool);
        pool = NULL;
    }
    if (camera) {
        rt_camera_destroy(camera);
        camera = NULL;
    }
    if (rt_scene_ptr) {
        rt_scene_destroy(rt_scene_ptr);
        rt_scene_ptr = NULL;
    }
    if (pixel_buffer) {
        free(pixel_buffer);
        pixel_buffer = NULL;
    }
    if (rt_texture) {
        render_destroy_texture(rt_texture);
        rt_texture = NULL;
    }
}

static void compute_forces_chunk(void *arg) {
    force_task_args *a = (force_task_args *)arg;

    memset(a->local_accel, 0, sizeof(a->local_accel));
    a->merge_count = 0;

    for (int i = a->start; i < a->end; i++) {
        if ((entity_masks[i] & (POSITION | PHYSICS)) != (POSITION | PHYSICS))
            continue;

        for (int j = i + 1; j < MAX_ENTITIES; j++) {
            if ((entity_masks[j] & (POSITION | PHYSICS)) != (POSITION | PHYSICS))
                continue;

            vector diff = vector_sub(position_components[j].coordinates,
                                     position_components[i].coordinates);
            float dist = vector_magnitude(diff);

            if (dist < softening) {
                int max_local = MAX_MERGES / num_threads;
                if (a->merge_count < max_local) {
                    a->local_merges[a->merge_count].i = i;
                    a->local_merges[a->merge_count].j = j;
                    a->merge_count++;
                }
                continue;
            }

            float force = gravity * physics_components[i].mass
                        * physics_components[j].mass / (dist * dist);
            vector dir = vector_scale(diff, 1.0f / dist);
            vector force_vec = vector_scale(dir, force);

            a->local_accel[i] = vector_add(a->local_accel[i],
                vector_scale(force_vec, 1.0f / physics_components[i].mass));
            a->local_accel[j] = vector_sub(a->local_accel[j],
                vector_scale(force_vec, 1.0f / physics_components[j].mass));
        }
    }
}

static void reset_accelerations(void) {
    for (int i = 0; i < MAX_ENTITIES; i++) {
        if ((entity_masks[i] & (POSITION | PHYSICS)) != (POSITION | PHYSICS))
            continue;
        physics_components[i].acceleration = (vector){0, 0, 0};
    }
}

static void accumulate_forces(void) {
    int chunk = MAX_ENTITIES / num_threads;
    for (int t = 0; t < num_threads; t++) {
        task_args[t].start = t * chunk;
        task_args[t].end = (t == num_threads - 1) ? MAX_ENTITIES : (t + 1) * chunk;
        thread_pool_submit(pool, compute_forces_chunk, &task_args[t]);
    }
    thread_pool_wait(pool);

    /* Sum thread-local accelerations */
    for (int t = 0; t < num_threads; t++) {
        for (int i = 0; i < MAX_ENTITIES; i++) {
            physics_components[i].acceleration = vector_add(
                physics_components[i].acceleration, task_args[t].local_accel[i]);
        }
    }

    /* Collect merge candidates from all threads */
    merge_count = 0;
    for (int t = 0; t < num_threads; t++) {
        for (int m = 0; m < task_args[t].merge_count; m++) {
            if (merge_count < MAX_MERGES) {
                merge_list[merge_count++] = task_args[t].local_merges[m];
            }
        }
    }
}

static void apply_merges(void) {
    for (int m = 0; m < merge_count; m++) {
        int i = merge_list[m].i;
        int j = merge_list[m].j;

        if ((entity_masks[i] & (POSITION | PHYSICS)) != (POSITION | PHYSICS))
            continue;
        if ((entity_masks[j] & (POSITION | PHYSICS)) != (POSITION | PHYSICS))
            continue;

        float m_i = physics_components[i].mass;
        float m_j = physics_components[j].mass;
        float total_mass = m_i + m_j;

        position_components[i].coordinates = vector_scale(
            vector_add(vector_scale(position_components[i].coordinates, m_i),
                       vector_scale(position_components[j].coordinates, m_j)),
            1.0f / total_mass);

        physics_components[i].velocity = vector_scale(
            vector_add(vector_scale(physics_components[i].velocity, m_i),
                       vector_scale(physics_components[j].velocity, m_j)),
            1.0f / total_mass);

        physics_components[i].mass = total_mass;

        destroy_entity(j);
    }
}

static void update_physics_component(int entity_id) {
    physics_components[entity_id].velocity = vector_add(
        physics_components[entity_id].velocity,
        vector_scale(physics_components[entity_id].acceleration, dt));

    position_components[entity_id].coordinates = vector_add(
        position_components[entity_id].coordinates,
        vector_scale(physics_components[entity_id].velocity, dt));
}

static void check_collision_with_boundary(int entity_id) {
    float dist = vector_magnitude(position_components[entity_id].coordinates);
    if (dist > world_radius) {
        vector normal = vector_scale(position_components[entity_id].coordinates,
                                     1.0f / dist);
        position_components[entity_id].coordinates = vector_scale(normal,
                                                                  world_radius);
        float vn = vector_dot(physics_components[entity_id].velocity, normal);
        if (vn > 0) {
            physics_components[entity_id].velocity = vector_sub(
                physics_components[entity_id].velocity,
                vector_scale(normal, 2.0f * vn));
            physics_components[entity_id].velocity = vector_scale(
                physics_components[entity_id].velocity, 0.5f);
        }
    }
}

static void nbody_step(void) {
    reset_accelerations();
    accumulate_forces();
    apply_merges();

    for (int i = 0; i < MAX_ENTITIES; i++) {
        if ((entity_masks[i] & (POSITION | PHYSICS)) != (POSITION | PHYSICS))
            continue;
        update_physics_component(i);
        if (bounds_enabled) check_collision_with_boundary(i);
    }
}

void nbody_update(void) {
    int steps = (int)ceilf(time_scale);
    for (int s = 0; s < steps; s++) {
        nbody_step();
    }
}

static rt_sphere entity_to_sphere(int entity_id) {
    float mass = physics_components[entity_id].mass;
    float t = logf(mass) / logf(1000.0f);
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;

    rt_sphere sp;
    sp.center = position_components[entity_id].coordinates;
    sp.radius = 2.0f + logf(mass) * 2.0f;
    if (sp.radius < 1.0f) sp.radius = 1.0f;
    sp.color.r = (uint8_t)(50 + t * 205);
    sp.color.g = (uint8_t)(50 * (1 - t * t));
    sp.color.b = (uint8_t)(255 * (1 - t * t));

    return sp;
}

static void ensure_render_resources(int w, int h) {
    if (!rt_scene_ptr) {
        rt_scene_ptr = rt_scene_create();
    }
    if (!pixel_buffer || rt_width != w || rt_height != h) {
        free(pixel_buffer);
        pixel_buffer = calloc((size_t)w * h, sizeof(uint32_t));
        if (rt_texture) render_destroy_texture(rt_texture);
        rt_texture = render_create_texture(w, h);
        rt_width = w;
        rt_height = h;
    }
}

static void render_scene(const rt_camera *cam, const rt_viewport *vp) {
    int num_chunks = num_threads;
    render_chunk_args chunk_args[MAX_THREADS];
    int rows_per_chunk = vp->height / num_chunks;

    for (int c = 0; c < num_chunks; c++) {
        chunk_args[c].pixel_buf = pixel_buffer;
        chunk_args[c].viewport = vp;
        chunk_args[c].y_start = c * rows_per_chunk;
        chunk_args[c].y_end = (c == num_chunks - 1) ? vp->height : (c + 1) * rows_per_chunk;
        chunk_args[c].camera = cam;
        chunk_args[c].scene = rt_scene_ptr;
        thread_pool_submit(pool, render_chunk_task, &chunk_args[c]);
    }
    thread_pool_wait(pool);

    render_clear();
    render_texture_update(rt_texture, pixel_buffer, vp->width * (int)sizeof(uint32_t));
    render_present();
}

void nbody_render(int screen_width, int screen_height) {
    int w = screen_width / RT_SCALE;
    int h = screen_height / RT_SCALE;

    ensure_render_resources(w, h);

    rt_viewport viewport = { .width = w, .height = h, .fov = 0.9273f };

    rt_scene_clear(rt_scene_ptr);
    for (int i = 0; i < MAX_ENTITIES; i++) {
        if ((entity_masks[i] & (POSITION | PHYSICS)) != (POSITION | PHYSICS))
            continue;
        rt_scene_add_sphere(rt_scene_ptr, entity_to_sphere(i));
    }

    render_scene(camera, &viewport);
}
