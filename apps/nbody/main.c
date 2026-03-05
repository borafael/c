#include "nbody.h"
#include "render.h"
#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>

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
    printf("  -h, --help           Show this help message\n");
    printf("\nControls:\n");
    printf("  ESC          Quit\n");
    printf("  R            Reset simulation\n");
    printf("  +/-          Camera distance [10 .. 20000]\n");
    printf("  F/S          Speed up/down [0.1x .. 50x]\n");
    printf("  Left/Right   Rotate azimuth\n");
    printf("  Up/Down      Rotate elevation [-1.5 .. 1.5 rad]\n");
}

int main(int argc, char** argv) {
    nbody_config config = nbody_default_config();
    int bounds = 0;

    static struct option long_options[] = {
        {"entities",  required_argument, 0, 'n'},
        {"gravity",   required_argument, 0, 'g'},
        {"dt",        required_argument, 0, 't'},
        {"radius",    required_argument, 0, 'r'},
        {"softening", required_argument, 0, 's'},
        {"threads",   required_argument, 0, 'T'},
        {"rot-speed", required_argument, 0, 'R'},
        {"bounds",    no_argument,       0, 'b'},
        {"help",      no_argument,       0, 'h'},
        {0, 0, 0, 0}
    };

    int opt;
    while ((opt = getopt_long(argc, argv, "n:g:t:r:s:T:R:bh", long_options, NULL)) != -1) {
        switch (opt) {
            case 'n': config.num_entities = atoi(optarg); break;
            case 'g': config.gravity = (float)atof(optarg); break;
            case 't': config.dt = (float)atof(optarg); break;
            case 'r': config.world_radius = (float)atof(optarg); break;
            case 's': config.softening = (float)atof(optarg); break;
            case 'T': config.num_threads = atoi(optarg); break;
            case 'R': config.rotation_speed = (float)atof(optarg); break;
            case 'b': bounds = 1; break;
            case 'h':
                print_usage(argv[0]);
                return 0;
            default:
                print_usage(argv[0]);
                return 1;
        }
    }

    nbody_init(&config);

    if (bounds) {
        nbody_set_bounds(1);
    }

    if (render_init() < 0) {
        return 1;
    }

    nbody_spawn_entities();

    int screen_width, screen_height;
    render_get_size(&screen_width, &screen_height);

    int running = 1;
    while (running) {
        input_events events;
        input_poll(&events);

        if (events.quit) running = 0;
        nbody_handle_input(&events);

        nbody_update();
        nbody_render(screen_width, screen_height);
        render_delay(1);
    }

    nbody_cleanup();
    render_cleanup();
    return 0;
}
