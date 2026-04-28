#include "nbody.h"
#include "render.h"
#include "vector.h"
#include "physics.h"
#include "renderer.h"
#include "viewport.h"
#include "scene.h"
#include "sphere.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stddef.h>
#include <time.h>

#define ARRAY_LEN(a) (sizeof(a) / sizeof((a)[0]))

static inline float clampf(float value, float min, float max) {
    if (value < min) return min;
    if (value > max) return max;
    return value;
}

static physics_world *world = NULL;
static nbody_config sim_config;
static float world_radius = 400.0f;
static float rotation_speed = 0.05f;

static float camera_azimuth = 0.0f;
static float camera_elevation = 0.3f;
static float camera_distance = 1500.0f;
static float time_scale = 1.0f;
static scene_camera *camera = NULL;

/* Raytracer state (lazy-initialized in nbody_render) */
#define RT_SCALE 4
static scene *scene_ptr = NULL;
static uint32_t *pixel_buffer = NULL;
static void *sdl_texture = NULL;
static int rt_width = 0;
static int rt_height = 0;
static rt_renderer *rt_rnd = NULL;
static float lighting_time = 0.0f;

static void update_camera_from_orbital(void) {
    vector pos = {
        camera_distance * cosf(camera_elevation) * sinf(camera_azimuth),
        camera_distance * sinf(camera_elevation),
        -camera_distance * cosf(camera_elevation) * cosf(camera_azimuth)
    };
    vector dir = vector_normalize(vector_scale(pos, -1.0f));
    scene_camera_place(camera, pos, dir);
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
    config.use_gpu = 0;
    config.bounded = 0;
    return config;
}

void nbody_init(const nbody_config *config) {
    sim_config = *config;
    world_radius = config->world_radius;
    rotation_speed = config->rotation_speed;

    physics_config pc = physics_default_config();
    pc.max_bodies       = config->num_entities;
    pc.num_threads      = config->num_threads;
    pc.gravity          = config->gravity;
    pc.dt               = config->dt;
    pc.softening        = config->softening;
    pc.merge_on_contact = 1;
    pc.bounded          = config->bounded;
    pc.world_radius     = config->world_radius;

    world = physics_world_create(&pc);
    if (!world) {
        fprintf(stderr, "Failed to create physics world\n");
        exit(EXIT_FAILURE);
    }

    rt_backend backend = RT_BACKEND_CPU;
    if (config->use_gpu) {
        if (rt_renderer_available(RT_BACKEND_OPENGL)) {
            backend = RT_BACKEND_OPENGL;
        } else {
            fprintf(stderr, "OpenGL backend unavailable, falling back to CPU\n");
        }
    }
    rt_rnd = rt_renderer_create(backend);
    if (!rt_rnd) {
        fprintf(stderr, "Failed to create raytrace renderer\n");
        physics_world_destroy(world);
        world = NULL;
        exit(EXIT_FAILURE);
    }
    fprintf(stderr, "Using renderer: %s\n", rt_renderer_name(rt_rnd));

    camera = scene_camera_create((vector){0, 0, 0}, (vector){0, 0, -1});
    if (!camera) {
        fprintf(stderr, "Failed to create camera\n");
        exit(EXIT_FAILURE);
    }
    update_camera_from_orbital();
}

void nbody_spawn_entities(void) {
    srand(time(NULL));
    int n = sim_config.num_entities;
    for (int i = 0; i < n; i++) {
        /* Rejection sampling: random point in unit ball */
        float x, y, z;
        do {
            x = ((float)rand() / RAND_MAX - 0.5f) * 2.0f;
            y = ((float)rand() / RAND_MAX - 0.5f) * 2.0f;
            z = ((float)rand() / RAND_MAX - 0.5f) * 2.0f;
        } while (x * x + y * y + z * z > 1.0f);

        vector pos = {
            x * world_radius,
            y * world_radius,
            z * world_radius,
        };
        float mass = 1.0f + (float)(rand() % 10);
        if (physics_world_add_body(world, pos, (vector){0,0,0}, mass) < 0) break;
    }
}

void nbody_reset(void) {
    camera_azimuth = 0.0f;
    camera_elevation = 0.3f;
    camera_distance = 1500.0f;
    time_scale = 1.0f;
    physics_world_clear(world);
    nbody_spawn_entities();
    update_camera_from_orbital();
}

void nbody_cleanup(void) {
    if (rt_rnd) {
        rt_renderer_destroy(rt_rnd);
        rt_rnd = NULL;
    }
    if (world) {
        physics_world_destroy(world);
        world = NULL;
    }
    if (camera) {
        scene_camera_destroy(camera);
        camera = NULL;
    }
    if (scene_ptr) {
        scene_destroy(scene_ptr);
        scene_ptr = NULL;
    }
    if (pixel_buffer) {
        free(pixel_buffer);
        pixel_buffer = NULL;
    }
    if (sdl_texture) {
        render_destroy_texture(sdl_texture);
        sdl_texture = NULL;
    }
}

void nbody_update(void) {
    int steps = (int)ceilf(time_scale);
    for (int s = 0; s < steps; s++) {
        physics_world_step(world);
    }
}

static scene_sphere body_to_sphere(int id, scene *scene) {
    float mass = physics_world_body_mass(world, id);
    float t = logf(mass) / logf(1000.0f);
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;

    scene_material mat = {0};
    mat.albedo.r = (uint8_t)(50 + t * 205);
    mat.albedo.g = (uint8_t)(50 * (1 - t * t));
    mat.albedo.b = (uint8_t)(255 * (1 - t * t));

    scene_sphere sp;
    sp.center = physics_world_body_position(world, id);
    sp.radius = 2.0f + logf(mass) * 2.0f;
    if (sp.radius < 1.0f) sp.radius = 1.0f;
    sp.material = scene_add_material(scene, mat);

    return sp;
}

static void ensure_render_resources(int w, int h) {
    if (!scene_ptr) {
        scene_ptr = scene_create();
        scene_set_ambient(scene_ptr, 0.12f);
    }
    if (!pixel_buffer || rt_width != w || rt_height != h) {
        free(pixel_buffer);
        pixel_buffer = calloc((size_t)w * h, sizeof(uint32_t));
        if (sdl_texture) render_destroy_texture(sdl_texture);
        sdl_texture = render_create_texture(w, h);
        rt_width = w;
        rt_height = h;
    }
}

/* Three-point rig that slowly orbits the scene, so bodies glint
 * as they coalesce. Re-added every frame because scene_clear
 * wipes lights. */
static void setup_lights(void) {
    float a = lighting_time;
    float ca = cosf(a), sa = sinf(a);

    /* Key: high front, rotates around Y */
    scene_add_light(scene_ptr, (scene_light){
        .direction = { ca, 0.9f, sa },
        .intensity = 0.75f
    });
    /* Fill: low opposite, soft */
    scene_add_light(scene_ptr, (scene_light){
        .direction = { -ca, -0.35f, -sa },
        .intensity = 0.25f
    });
    /* Rim: perpendicular to key, subtle edge glow */
    scene_add_light(scene_ptr, (scene_light){
        .direction = { -sa, 0.2f, ca },
        .intensity = 0.35f
    });

    lighting_time += 0.012f;
}

static void render_scene(const scene_camera *cam, const rt_viewport *vp) {
    rt_renderer_render(rt_rnd, scene_ptr, cam, vp, pixel_buffer, NULL);

    render_clear();
    render_texture_update(sdl_texture, pixel_buffer, vp->width * (int)sizeof(uint32_t));
    render_present();
}

void nbody_render(int screen_width, int screen_height) {
    int w = screen_width / RT_SCALE;
    int h = screen_height / RT_SCALE;

    ensure_render_resources(w, h);

    rt_viewport viewport = { .width = w, .height = h, .fov = 0.9273f };

    scene_clear(scene_ptr);
    setup_lights();

    int cap = physics_world_capacity(world);
    for (int i = 0; i < cap; i++) {
        if (!physics_world_body_alive(world, i)) continue;
        scene_add_sphere(scene_ptr, body_to_sphere(i, scene_ptr));
    }

    render_scene(camera, &viewport);
}
