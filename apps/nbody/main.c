#include "nbody.h"
#include "render.h"
#include "input.h"
#include <string.h>

int main(int argc, char** argv) {
    nbody_init();

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--bounds") == 0) {
            nbody_set_bounds(1);
        }
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

        if (events.quit) {
            running = 0;
        }
        if (events.reset) {
            nbody_reset();
        }
        if (events.zoom_in) {
            nbody_zoom_in();
        }
        if (events.zoom_out) {
            nbody_zoom_out();
        }
        if (events.speed_up) {
            nbody_speed_up();
        }
        if (events.speed_down) {
            nbody_speed_down();
        }
        if (events.pan_up) {
            nbody_pan_up();
        }
        if (events.pan_down) {
            nbody_pan_down();
        }
        if (events.pan_left) {
            nbody_pan_left();
        }
        if (events.pan_right) {
            nbody_pan_right();
        }

        nbody_update();
        nbody_render(screen_width, screen_height);
        render_delay(1);
    }

    nbody_cleanup();
    render_cleanup();
    return 0;
}
