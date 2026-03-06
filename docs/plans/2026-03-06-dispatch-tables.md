# Dispatch Tables Refactoring — Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Replace if/else and switch chains with data-driven dispatch tables using `offsetof` and function pointers in three sites.

**Architecture:** Each site gets a static const table that maps a key to an action. A generic loop replaces the branching logic. A shared `ARRAY_LEN` macro and a `clampf` helper support all three sites.

**Tech Stack:** C11, GNU Autotools, SDL2

---

### Task 1: Add `ARRAY_LEN` macro and `clampf` helper to `nbody.c`

**Files:**
- Modify: `apps/nbody/nbody.c:12` (add after existing `#define` lines)

**Step 1: Add the macro and helper at the top of nbody.c**

After line 13 (`#define MAX_THREADS 64`), add:

```c
#define ARRAY_LEN(a) (sizeof(a) / sizeof((a)[0]))

static inline float clampf(float value, float min, float max) {
    if (value < min) return min;
    if (value > max) return max;
    return value;
}
```

**Step 2: Build to verify no errors**

Run: `make`
Expected: clean build, no errors

**Step 3: Commit**

```
refactor(nbody): add ARRAY_LEN macro and clampf helper
```

---

### Task 2: Refactor input polling — keybinding table in `input.c`

**Files:**
- Modify: `apps/nbody/input.c`

**Step 1: Replace the if-chain with a keybinding table**

Replace the entire contents of `input.c` with:

```c
#include "input.h"
#include <stddef.h>
#include <SDL2/SDL.h>

#define ARRAY_LEN(a) (sizeof(a) / sizeof((a)[0]))

typedef struct {
    SDL_Keycode key;
    size_t offset;
} key_binding;

static const key_binding bindings[] = {
    { SDLK_ESCAPE, offsetof(input_events, quit) },
    { SDLK_r,      offsetof(input_events, reset) },
    { SDLK_EQUALS, offsetof(input_events, zoom_in) },
    { SDLK_MINUS,  offsetof(input_events, zoom_out) },
    { SDLK_f,      offsetof(input_events, speed_up) },
    { SDLK_s,      offsetof(input_events, speed_down) },
    { SDLK_UP,     offsetof(input_events, pan_up) },
    { SDLK_DOWN,   offsetof(input_events, pan_down) },
    { SDLK_LEFT,   offsetof(input_events, pan_left) },
    { SDLK_RIGHT,  offsetof(input_events, pan_right) },
};

void input_poll(input_events* events) {
    events->quit = 0;
    events->reset = 0;
    events->zoom_in = 0;
    events->zoom_out = 0;
    events->speed_up = 0;
    events->speed_down = 0;
    events->pan_up = 0;
    events->pan_down = 0;
    events->pan_left = 0;
    events->pan_right = 0;

    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        if (e.type == SDL_QUIT) {
            events->quit = 1;
        }
        if (e.type == SDL_KEYDOWN) {
            for (size_t i = 0; i < ARRAY_LEN(bindings); i++) {
                if (e.key.keysym.sym == bindings[i].key) {
                    *(int *)((char *)events + bindings[i].offset) = 1;
                }
            }
        }
    }
}
```

**Step 2: Build and run to verify behavior is identical**

Run: `make`
Expected: clean build

Run: `./apps/nbody/nbody` — verify ESC, R, +/-, F/S, arrows all work.

**Step 3: Commit**

```
refactor(nbody): replace input polling if-chain with keybinding dispatch table
```

---

### Task 3: Refactor input handling — function pointer dispatch in `nbody.c`

**Files:**
- Modify: `apps/nbody/nbody.c:116-149` (the `nbody_handle_input` function)

**Step 1: Add handler functions and dispatch table before `nbody_handle_input`**

Insert these static functions before `nbody_handle_input` (after `update_camera_from_orbital`, around line 115):

```c
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
```

**Step 2: Replace the body of `nbody_handle_input`**

```c
void nbody_handle_input(const input_events *events) {
    for (size_t i = 0; i < ARRAY_LEN(actions); i++) {
        if (*(const int *)((const char *)events + actions[i].event_offset)) {
            actions[i].handler();
        }
    }
    update_camera_from_orbital();
}
```

**Step 3: Ensure `<stddef.h>` is included in `nbody.c`**

Add `#include <stddef.h>` at the top of `nbody.c` if not already present (needed for `offsetof` and `size_t`).

**Step 4: Build and run to verify behavior is identical**

Run: `make`
Expected: clean build

Run: `./apps/nbody/nbody` — verify camera controls, speed, and reset all work.

**Step 5: Commit**

```
refactor(nbody): replace input handling if-chain with function pointer dispatch
```

---

### Task 4: Refactor CLI parsing — descriptor table in `main.c`

**Files:**
- Modify: `apps/nbody/main.c`

**Step 1: Replace the switch with a descriptor table**

Replace the full contents of `main.c` with:

```c
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
        if (apply_option(opt, optarg, &config)) {
            continue;
        }
        switch (opt) {
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
```

**Step 2: Build and test CLI options**

Run: `make`
Expected: clean build

Run: `./apps/nbody/nbody -h` — verify help output is unchanged.
Run: `./apps/nbody/nbody -n 100 -g 1.0 -b` — verify options are applied.

**Step 3: Commit**

```
refactor(nbody): replace CLI switch with descriptor table dispatch
```

---

### Task 5: Final build and verification

**Files:** none (verification only)

**Step 1: Clean build**

Run: `make clean && make`
Expected: clean build, zero warnings

**Step 2: Run the simulation and test all inputs**

Run: `./apps/nbody/nbody -n 300 -g 0.8 -b`

Verify:
- Simulation starts with 300 entities and boundary collision
- ESC quits
- R resets
- +/- adjust camera distance
- F/S adjust speed
- Arrow keys rotate camera
- `-h` prints help

**Step 3: Commit all if any fixups were needed, otherwise done**
