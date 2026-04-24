#include "nbody.h"
#include "render.h"
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <getopt.h>

#define ARRAY_LEN(a) (sizeof(a) / sizeof((a)[0]))

typedef enum { OPT_INT, OPT_FLOAT } opt_type;

typedef struct {
    int key;
    opt_type type;
    size_t offset;
} opt_descriptor;

static const opt_descriptor opt_table[] = {
    { 'n', OPT_INT,   offsetof(nbody_config, num_entities) },
    { 'g', OPT_FLOAT, offsetof(nbody_config, gravity) },
    { 't', OPT_FLOAT, offsetof(nbody_config, dt) },
    { 'r', OPT_FLOAT, offsetof(nbody_config, world_radius) },
    { 's', OPT_FLOAT, offsetof(nbody_config, softening) },
    { 'T', OPT_INT,   offsetof(nbody_config, num_threads) },
    { 'R', OPT_FLOAT, offsetof(nbody_config, rotation_speed) },
};

static int apply_option(int opt, const char *arg, nbody_config *cfg) {
    if (!arg) return 0;
    for (size_t i = 0; i < ARRAY_LEN(opt_table); i++) {
        if (opt_table[i].key == opt) {
            void *field = (char *)cfg + opt_table[i].offset;
            switch (opt_table[i].type) {
                case OPT_INT:   *(int *)field   = atoi(arg); break;
                case OPT_FLOAT: *(float *)field = (float)atof(arg); break;
            }
            return 1;
        }
    }
    return 0;
}

static void print_usage(const char *prog) {
    nbody_config defaults = nbody_default_config();
    printf("Usage: %s [options]\n", prog);
    printf("\nSimulation parameters:\n");
    printf("  -n, --entities N     Number of entities (default: %d)\n", defaults.num_entities);
    printf("  -g, --gravity F      Gravitational constant (default: %.3f)\n", defaults.gravity);
    printf("  -t, --dt F           Time step (default: %.3f)\n", defaults.dt);
    printf("  -r, --radius F       World radius (default: %.1f)\n", defaults.world_radius);
    printf("  -s, --softening F    Softening distance (default: %.1f)\n", defaults.softening);
    printf("  -T, --threads N      Number of threads (default: %d)\n", defaults.num_threads);
    printf("  -R, --rot-speed F    Camera rotation speed (default: %.3f)\n", defaults.rotation_speed);
    printf("  -b, --bounds         Enable boundary collision\n");
    printf("  -G, --gpu            Use OpenGL compute backend (falls back to CPU if unavailable)\n");
    printf("  -h, --help           Show this help message\n");
    printf("\nControls:\n");
    printf("  ESC          Quit\n");
    printf("  R            Reset simulation\n");
    printf("  F11          Toggle fullscreen\n");
    printf("  +/-          Camera distance [10 .. 20000]\n");
    printf("  F/S          Speed up/down [0.1x .. 50x]\n");
    printf("  Left/Right   Rotate azimuth\n");
    printf("  Up/Down      Rotate elevation [-1.5 .. 1.5 rad]\n");
}

int main(int argc, char** argv) {
    nbody_config config = nbody_default_config();

    static struct option long_options[] = {
        {"entities",  required_argument, 0, 'n'},
        {"gravity",   required_argument, 0, 'g'},
        {"dt",        required_argument, 0, 't'},
        {"radius",    required_argument, 0, 'r'},
        {"softening", required_argument, 0, 's'},
        {"threads",   required_argument, 0, 'T'},
        {"rot-speed", required_argument, 0, 'R'},
        {"bounds",    no_argument,       0, 'b'},
        {"gpu",       no_argument,       0, 'G'},
        {"help",      no_argument,       0, 'h'},
        {0, 0, 0, 0}
    };

    int opt;
    while ((opt = getopt_long(argc, argv, "n:g:t:r:s:T:R:bGh", long_options, NULL)) != -1) {
        if (apply_option(opt, optarg, &config)) {
            continue;
        }
        switch (opt) {
            case 'b': config.bounded = 1; break;
            case 'G': config.use_gpu = 1; break;
            case 'h':
                print_usage(argv[0]);
                return 0;
            default:
                print_usage(argv[0]);
                return 1;
        }
    }

    if (render_init() < 0) {
        return 1;
    }

    nbody_init(&config);
    nbody_spawn_entities();

    int screen_width, screen_height;
    render_get_size(&screen_width, &screen_height);

    int running = 1;
    while (running) {
        input_events events;
        input_poll(&events);

        if (events.quit) running = 0;
        if (events.toggle_fullscreen) {
            render_toggle_fullscreen();
            render_get_size(&screen_width, &screen_height);
        }
        nbody_handle_input(&events);

        nbody_update();
        nbody_render(screen_width, screen_height);
        render_delay(1);
    }

    nbody_cleanup();
    render_cleanup();
    return 0;
}
