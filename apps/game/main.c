#include "game.h"
#include "render.h"
#include "input.h"

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;

    game_init();

    if (render_init() < 0) {
        return 1;
    }

    game_spawn_entities();

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
            game_reset();
        }

        game_update();
        game_render(screen_width, screen_height);
        render_delay(1);
    }

    render_cleanup();
    return 0;
}
