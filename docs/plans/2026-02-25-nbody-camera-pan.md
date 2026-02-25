# Camera Pan Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Add arrow key camera panning to the nbody simulation.

**Architecture:** Follows the existing event-flag pattern (same as zoom/speed). Four new input flags map to four pan functions that adjust a camera offset, which is applied during rendering. Pan speed scales inversely with zoom.

**Tech Stack:** C, SDL2, GNU Autotools

**Design doc:** `docs/plans/2026-02-25-nbody-camera-pan-design.md`

---

### Task 1: Add pan input events

**Files:**
- Modify: `apps/nbody/input.h:7-14`
- Modify: `apps/nbody/input.c:4-38`

**Step 1: Add pan flags to input_events struct**

In `apps/nbody/input.h`, add 4 new fields after `speed_down` (line 13):

```c
typedef struct {
    int quit;       /* Window close or Escape pressed */
    int reset;      /* R key pressed */
    int zoom_in;    /* + key pressed */
    int zoom_out;   /* - key pressed */
    int speed_up;   /* F key pressed */
    int speed_down; /* S key pressed */
    int pan_up;     /* Up arrow pressed */
    int pan_down;   /* Down arrow pressed */
    int pan_left;   /* Left arrow pressed */
    int pan_right;  /* Right arrow pressed */
} input_events;
```

**Step 2: Reset pan flags and add key mappings in input_poll**

In `apps/nbody/input.c`, add resets after line 10 and key mappings after line 35:

```c
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
            if (e.key.keysym.sym == SDLK_f) {
                events->speed_up = 1;
            }
            if (e.key.keysym.sym == SDLK_s) {
                events->speed_down = 1;
            }
            if (e.key.keysym.sym == SDLK_UP) {
                events->pan_up = 1;
            }
            if (e.key.keysym.sym == SDLK_DOWN) {
                events->pan_down = 1;
            }
            if (e.key.keysym.sym == SDLK_LEFT) {
                events->pan_left = 1;
            }
            if (e.key.keysym.sym == SDLK_RIGHT) {
                events->pan_right = 1;
            }
        }
    }
}
```

**Step 3: Build to verify**

Run: `make`
Expected: Compiles with no errors or warnings.

**Step 4: Commit**

```bash
git add apps/nbody/input.h apps/nbody/input.c
git commit -m "feat(nbody): add pan input events for arrow keys"
```

---

### Task 2: Add camera offset state and pan functions

**Files:**
- Modify: `apps/nbody/nbody.h:44-54`
- Modify: `apps/nbody/nbody.c:39-40` (state), `apps/nbody/nbody.c:68-76` (after speed functions), `apps/nbody/nbody.c:103-114` (reset)

**Step 1: Declare pan functions in header**

In `apps/nbody/nbody.h`, add after `nbody_speed_down` declaration (line 54):

```c
/**
 * Pan camera up.
 */
void nbody_pan_up(void);

/**
 * Pan camera down.
 */
void nbody_pan_down(void);

/**
 * Pan camera left.
 */
void nbody_pan_left(void);

/**
 * Pan camera right.
 */
void nbody_pan_right(void);
```

**Step 2: Add camera offset state and pan constant**

In `apps/nbody/nbody.c`, add after line 40 (`static float time_scale = 1.0f;`):

```c
static float camera_offset_x = 0.0f;
static float camera_offset_y = 0.0f;
#define PAN_SPEED 10.0f
```

**Step 3: Implement pan functions**

In `apps/nbody/nbody.c`, add after `nbody_speed_down` (after line 76):

```c
void nbody_pan_up(void) {
    camera_offset_y += PAN_SPEED / zoom;
}

void nbody_pan_down(void) {
    camera_offset_y -= PAN_SPEED / zoom;
}

void nbody_pan_left(void) {
    camera_offset_x -= PAN_SPEED / zoom;
}

void nbody_pan_right(void) {
    camera_offset_x += PAN_SPEED / zoom;
}
```

Note: "up" adds to Y offset because in the camera transform, subtracting `camera_offset_y` from the center means a positive offset moves the view upward (shows lower Y world coordinates moving down on screen, giving the appearance of panning up).

**Step 4: Reset camera offset in nbody_reset**

In `apps/nbody/nbody.c`, in the `nbody_reset` function, add after `time_scale = 1.0f;` (line 105):

```c
camera_offset_x = 0.0f;
camera_offset_y = 0.0f;
```

**Step 5: Build to verify**

Run: `make`
Expected: Compiles with no errors or warnings.

**Step 6: Commit**

```bash
git add apps/nbody/nbody.h apps/nbody/nbody.c
git commit -m "feat(nbody): add camera offset state and pan functions"
```

---

### Task 3: Apply camera offset in rendering

**Files:**
- Modify: `apps/nbody/nbody.c:214-215`

**Step 1: Update camera_x and camera_y calculation**

In `apps/nbody/nbody.c`, replace lines 214-215:

Old:
```c
float camera_x = WORLD_WIDTH / 2.0f - (WORLD_WIDTH / 2.0f) / zoom;
float camera_y = WORLD_HEIGHT / 2.0f - (WORLD_HEIGHT / 2.0f) / zoom;
```

New:
```c
float camera_x = (WORLD_WIDTH / 2.0f - camera_offset_x) - (WORLD_WIDTH / 2.0f) / zoom;
float camera_y = (WORLD_HEIGHT / 2.0f - camera_offset_y) - (WORLD_HEIGHT / 2.0f) / zoom;
```

**Step 2: Build to verify**

Run: `make`
Expected: Compiles with no errors or warnings.

**Step 3: Commit**

```bash
git add apps/nbody/nbody.c
git commit -m "feat(nbody): apply camera offset in render transform"
```

---

### Task 4: Wire pan input to main loop

**Files:**
- Modify: `apps/nbody/main.c:35-46`

**Step 1: Add pan event handling**

In `apps/nbody/main.c`, add after the `speed_down` block (after line 46):

```c
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
```

**Step 2: Build to verify**

Run: `make`
Expected: Compiles with no errors or warnings.

**Step 3: Run and manually verify**

Run: `./apps/nbody/nbody`
Verify:
- Arrow keys pan the camera (up/down/left/right)
- Pan speed feels appropriate and scales with zoom level
- R key resets camera to center
- Zoom still works correctly alongside panning

**Step 4: Commit**

```bash
git add apps/nbody/main.c
git commit -m "feat(nbody): wire pan input events to main loop"
```
