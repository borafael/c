#include "nbody.h"
#include "render.h"
#include "input.h"

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;

    nbody_init();

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

        nbody_update();
        nbody_render(screen_width, screen_height);
        render_delay(1);
    }

    render_cleanup();
    return 0;
}
