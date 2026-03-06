# Dispatch Tables Refactoring Design

Replace if/else and switch chains with data-driven dispatch tables using
`offsetof` and function pointers across three sites.

## Site 1: CLI Parsing (`apps/nbody/main.c`)

**Current:** `switch` on `getopt_long` result with per-case `atoi`/`atof` calls.

**Design:** Descriptor table with type tags and `offsetof`.

```c
typedef enum { OPT_INT, OPT_FLOAT, OPT_FLAG } opt_type;

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
```

A generic `apply_option` function uses the type tag to parse and write:

```c
static int apply_option(int opt, const char *arg,
                        nbody_config *cfg, int *bounds) {
    for (size_t i = 0; i < ARRAY_LEN(opt_table); i++) {
        if (opt_table[i].key == opt) {
            void *field = (char *)cfg + opt_table[i].offset;
            switch (opt_table[i].type) {
                case OPT_INT:   *(int *)field   = atoi(arg); break;
                case OPT_FLOAT: *(float *)field = (float)atof(arg); break;
                case OPT_FLAG:  *(int *)field   = 1; break;
            }
            return 1;
        }
    }
    return 0;
}
```

Special cases (`-b` for bounds, `-h` for help) remain outside the table.

## Site 2: Input Polling (`apps/nbody/input.c`)

**Current:** 10 identical `if (keysym == X) events->field = 1` statements.

**Design:** Keybinding table with `offsetof` — no function pointers needed.

```c
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
```

Single loop replaces all if-statements:

```c
for (size_t i = 0; i < ARRAY_LEN(bindings); i++) {
    if (e.key.keysym.sym == bindings[i].key) {
        *(int *)((char *)events + bindings[i].offset) = 1;
    }
}
```

## Site 3: Input Handling (`apps/nbody/nbody.c`)

**Current:** Chain of `if (events->field)` with unique logic per action.

**Design:** Function pointer dispatch table. Each action gets a small static
handler function.

```c
typedef struct {
    size_t event_offset;
    void (*handler)(void);
} input_action;

static void handle_reset(void)      { nbody_reset(); }
static void handle_zoom_in(void)    { camera_distance /= 1.1f; clamp(&camera_distance, 10.0f, 20000.0f); }
static void handle_zoom_out(void)   { camera_distance *= 1.1f; clamp(&camera_distance, 10.0f, 20000.0f); }
static void handle_pan_left(void)   { camera_azimuth -= rotation_speed; }
static void handle_pan_right(void)  { camera_azimuth += rotation_speed; }
static void handle_pan_up(void)     { camera_elevation += rotation_speed; clamp(&camera_elevation, -1.5f, 1.5f); }
static void handle_pan_down(void)   { camera_elevation -= rotation_speed; clamp(&camera_elevation, -1.5f, 1.5f); }
static void handle_speed_up(void)   { time_scale *= 1.5f; clamp(&time_scale, 0.1f, 50.0f); }
static void handle_speed_down(void) { time_scale /= 1.5f; clamp(&time_scale, 0.1f, 50.0f); }

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

Dispatch loop:

```c
void nbody_handle_input(const input_events *events) {
    for (size_t i = 0; i < ARRAY_LEN(actions); i++) {
        if (*(const int *)((const char *)events + actions[i].event_offset)) {
            actions[i].handler();
        }
    }
}
```

A small `clampf` helper consolidates the repeated clamping pattern.

## Shared Utility

An `ARRAY_LEN` macro used across all three sites:

```c
#define ARRAY_LEN(a) (sizeof(a) / sizeof((a)[0]))
```

## Files Changed

| File | Change |
|------|--------|
| `apps/nbody/main.c` | Replace switch with descriptor table |
| `apps/nbody/input.c` | Replace if-chain with keybinding table |
| `apps/nbody/nbody.c` | Replace if-chain with function pointer dispatch |
