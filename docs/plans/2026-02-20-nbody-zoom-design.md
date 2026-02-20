# N-Body Zoom In/Out Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Add keyboard-controlled zoom (+ to zoom in, - to zoom out) to the nbody simulation, centered on the world center.

**Architecture:** Modify the world-to-screen render mapping in `nbody.c` with a zoom factor and derived camera offset. Input events added in `input.c`, wired through `main.c`. No new files, no camera struct, no SDL-level transforms.

**Tech Stack:** C, SDL2

---

### Task 1: Add zoom input events

**Files:**
- Modify: `apps/nbody/input.h:7-10`
- Modify: `apps/nbody/input.c:4-22`

**Step 1: Add zoom fields to input_events struct**

In `apps/nbody/input.h`, add `zoom_in` and `zoom_out` to the struct:

```c
typedef struct {
    int quit;      /* Window close or Escape pressed */
    int reset;     /* R key pressed */
    int zoom_in;   /* + key pressed */
    int zoom_out;  /* - key pressed */
} input_events;
```

**Step 2: Handle zoom keys in input_poll**

In `apps/nbody/input.c`, reset the new fields and handle the key events. The full function becomes:

```c
void input_poll(input_events* events) {
    events->quit = 0;
    events->reset = 0;
    events->zoom_in = 0;
    events->zoom_out = 0;

    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        if (e.type == SDL_QUIT) {
            events->quit = 1;
        }
        if (e.type == SDL_KEYDOWN) {
            if (e.key.keysym.sym == SDLK_ESCAPE) {
                events->quit = 1;
            }
            if (e.key.keysym.sym == SDLK_r) {
                events->reset = 1;
            }
            if (e.key.keysym.sym == SDLK_EQUALS) {
                events->zoom_in = 1;
            }
            if (e.key.keysym.sym == SDLK_MINUS) {
                events->zoom_out = 1;
            }
        }
    }
}
```

**Step 3: Build to verify no compile errors**

Run: `cd /home/rafa/repos/c && make`
Expected: Compiles successfully (new struct fields are unused but that's OK)

**Step 4: Commit**

```bash
git add apps/nbody/input.h apps/nbody/input.c
git commit -m "feat(nbody): add zoom_in and zoom_out input events"
```

---

### Task 2: Add zoom state and functions to nbody module

**Files:**
- Modify: `apps/nbody/nbody.h:1-36`
- Modify: `apps/nbody/nbody.c:1-14` (add static zoom variable)
- Modify: `apps/nbody/nbody.c:81-90` (reset zoom in nbody_reset)

**Step 1: Declare zoom functions in header**

In `apps/nbody/nbody.h`, add before the `#endif`:

```c
/**
 * Zoom in (increase zoom level).
 */
void nbody_zoom_in(void);

/**
 * Zoom out (decrease zoom level).
 */
void nbody_zoom_out(void);
```

**Step 2: Add zoom state and functions in nbody.c**

Add the static zoom variable after the existing static declarations (after line 38):

```c
static float zoom = 1.0f;
```

Add the zoom functions after `nbody_set_bounds` (after line 54):

```c
void nbody_zoom_in(void) {
    zoom *= 1.1f;
    if (zoom > 20.0f) zoom = 20.0f;
}

void nbody_zoom_out(void) {
    zoom /= 1.1f;
    if (zoom < 0.1f) zoom = 0.1f;
}
```

**Step 3: Reset zoom in nbody_reset**

In `nbody_reset()`, add `zoom = 1.0f;` at the start of the function body (after the opening brace, before the for loop):

```c
void nbody_reset(void) {
    zoom = 1.0f;
    for (int i = 0; i < MAX_ENTITIES; i++) {
        entity_masks[i] = NONE;
    }
    // ... rest unchanged
```

**Step 4: Build to verify no compile errors**

Run: `cd /home/rafa/repos/c && make`
Expected: Compiles successfully

**Step 5: Commit**

```bash
git add apps/nbody/nbody.h apps/nbody/nbody.c
git commit -m "feat(nbody): add zoom state and zoom_in/zoom_out functions"
```

---

### Task 3: Apply zoom transform in rendering

**Files:**
- Modify: `apps/nbody/nbody.c:186-218` (nbody_render function)

**Step 1: Modify nbody_render to apply zoom**

Replace the world-to-screen coordinate mapping inside `nbody_render`. The full updated function:

```c
void nbody_render(int screen_width, int screen_height) {
    render_clear();

    float camera_x = WORLD_WIDTH / 2.0f - (WORLD_WIDTH / 2.0f) / zoom;
    float camera_y = WORLD_HEIGHT / 2.0f - (WORLD_HEIGHT / 2.0f) / zoom;

    for (int i = 0; i < MAX_ENTITIES; i++) {
        if ((entity_masks[i] & POSITION) != POSITION)
            continue;

        int sx = (int)((position_components[i].coordinates.x - camera_x) * zoom / WORLD_WIDTH * screen_width);
        int sy = (int)((position_components[i].coordinates.y - camera_y) * zoom / WORLD_HEIGHT * screen_height);

        if (sx >= 0 && sx < screen_width && sy >= 0 && sy < screen_height) {
            int radius = 2;
            uint8_t r = 100, g = 100, b = 255;

            if ((entity_masks[i] & PHYSICS) == PHYSICS) {
                float mass = physics_components[i].mass;
                float t = logf(mass) / logf(1000.0f);
                if (t < 0) t = 0;
                if (t > 1) t = 1;

                r = (uint8_t)(50 + t * 205);
                g = (uint8_t)(50 * (1 - t * t));
                b = (uint8_t)(255 * (1 - t * t));

                radius = 2 + (int)(logf(mass) * 2.0f);
            }

            render_circle(sx, sy, radius, r, g, b);
        }
    }

    render_present();
}
```

The only changes from the original are:
- Added `camera_x` and `camera_y` computation before the loop
- Changed the `sx` formula from `(x / WORLD_WIDTH * screen_width)` to `((x - camera_x) * zoom / WORLD_WIDTH * screen_width)`
- Same for `sy`

**Step 2: Build to verify no compile errors**

Run: `cd /home/rafa/repos/c && make`
Expected: Compiles successfully

**Step 3: Commit**

```bash
git add apps/nbody/nbody.c
git commit -m "feat(nbody): apply zoom transform in rendering"
```

---

### Task 4: Wire zoom input to nbody in main loop

**Files:**
- Modify: `apps/nbody/main.c:25-39` (main loop event handling)

**Step 1: Add zoom event handling in main loop**

In `main.c`, add zoom handling after the reset block (after line 34):

```c
        if (events.zoom_in) {
            nbody_zoom_in();
        }
        if (events.zoom_out) {
            nbody_zoom_out();
        }
```

The full loop body becomes:

```c
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

        nbody_update();
        nbody_render(screen_width, screen_height);
        render_delay(1);
    }
```

**Step 2: Build to verify everything compiles**

Run: `cd /home/rafa/repos/c && make`
Expected: Compiles successfully with zero warnings

**Step 3: Run the application and test zoom**

Run: `cd /home/rafa/repos/c && ./apps/nbody/nbody`

Manual test:
- Press `+` (equals key) several times: view should zoom into center
- Press `-` several times: view should zoom out from center
- Press `R`: zoom should reset to default 1.0x view
- Press `ESC`: should quit

**Step 4: Commit**

```bash
git add apps/nbody/main.c
git commit -m "feat(nbody): wire zoom input events to main loop"
```
