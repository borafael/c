# Battleforge Engine Implementation Plan

> **For agentic workers:** REQUIRED: Use superpowers:subagent-driven-development (if subagents available) or superpowers:executing-plans to implement this plan. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build `libs/battleforge`, a headless scene engine with command-in pixels-out API, and `apps/battleforge`, a thin SDL shell demonstrating it.

**Architecture:** The engine manages camera, map, and entities internally. Shells push commands, call tick/render, and display the resulting pixel buffer. Internally uses `libs/raytrace` for rendering and `libs/thread` for multithreading. The raytracer scene is rebuilt each frame from game state.

**Tech Stack:** C, GNU Autotools, libs/raytrace, libs/thread, SDL2 (shell only)

**Spec:** `docs/superpowers/specs/2026-03-11-battleforge-engine-design.md`

---

## File Structure

| Action | File | Responsibility |
|--------|------|----------------|
| Modify | `libs/raytrace/raytrace.c:76-84` | Fix `rt_scene_clear` to also clear lights |
| Create | `libs/battleforge/Makefile.am` | Build config for battleforge library |
| Create | `libs/battleforge/battleforge.h` | Public API: types, command enum, function declarations |
| Create | `libs/battleforge/battleforge.c` | Implementation: engine lifecycle, command queue, tick, render |
| Create | `apps/battleforge/Makefile.am` | Build config for battleforge shell app |
| Create | `apps/battleforge/main.c` | SDL shell: display, input translation, demo scene |
| Modify | `configure.ac:11-18` | Add battleforge lib and app Makefiles |
| Modify | `Makefile.am:5` | Add battleforge to SUBDIRS |

---

## Chunk 1: Foundation

### Task 1: Fix rt_scene_clear to also clear lights

The spec notes that `rt_scene_clear` does not reset `light_count`. Since `bf_render` rebuilds the scene each frame (clear + re-add), lights would accumulate without this fix.

**Files:**
- Modify: `libs/raytrace/raytrace.c:76-84`

- [ ] **Step 1: Add `light_count` reset to `rt_scene_clear`**

In `libs/raytrace/raytrace.c`, in the `rt_scene_clear` function, after `scene->sprite_count = 0;` (line 83), add:

```c
    scene->light_count    = 0;
```

- [ ] **Step 2: Verify it compiles**

Run: `cd /home/rafa/repos/c && make`
Expected: Compiles with no errors.

- [ ] **Step 3: Commit**

```bash
git add libs/raytrace/raytrace.c
git commit -m "fix(raytrace): clear lights in rt_scene_clear"
```

---

### Task 2: Build system setup

Create the directory structure and build files for both `libs/battleforge` and `apps/battleforge`, and wire them into the top-level build.

**Files:**
- Create: `libs/battleforge/Makefile.am`
- Create: `apps/battleforge/Makefile.am`
- Modify: `configure.ac:11-18`
- Modify: `Makefile.am:5`

- [ ] **Step 1: Create libs/battleforge directory and Makefile.am**

```bash
mkdir -p libs/battleforge
```

Create `libs/battleforge/Makefile.am`:

```makefile
noinst_LTLIBRARIES = libbattleforge.la
libbattleforge_la_SOURCES = battleforge.c
libbattleforge_la_CPPFLAGS = -I$(top_srcdir)/libs/math -I$(top_srcdir)/libs/raytrace -I$(top_srcdir)/libs/thread
libbattleforge_la_LIBADD = $(top_builddir)/libs/raytrace/libraytrace.la $(top_builddir)/libs/thread/libthread.la -lm -lpthread
```

- [ ] **Step 2: Create apps/battleforge directory and Makefile.am**

```bash
mkdir -p apps/battleforge
```

Create `apps/battleforge/Makefile.am`:

```makefile
bin_PROGRAMS = battleforge
battleforge_SOURCES = main.c
battleforge_CPPFLAGS = -I$(top_srcdir)/libs/math -I$(top_srcdir)/libs/raytrace -I$(top_srcdir)/libs/battleforge -I$(top_srcdir)/libs/thread $(SDL2_CFLAGS)
battleforge_LDADD = $(top_builddir)/libs/battleforge/libbattleforge.la $(top_builddir)/libs/raytrace/libraytrace.la $(top_builddir)/libs/thread/libthread.la -lm -lpthread $(SDL2_LIBS)
```

- [ ] **Step 3: Update configure.ac**

In `configure.ac`, add `libs/battleforge/Makefile` and `apps/battleforge/Makefile` to the `AC_CONFIG_FILES` block. The block (lines 11-18) should become:

```
AC_CONFIG_FILES([
    Makefile
    libs/math/Makefile
    libs/thread/Makefile
    libs/raytrace/Makefile
    libs/battleforge/Makefile
    apps/nbody/Makefile
    apps/rtdemo/Makefile
    apps/battleforge/Makefile
])
```

- [ ] **Step 4: Update top-level Makefile.am**

Change the SUBDIRS line (line 5) to include battleforge lib (before apps) and app:

```makefile
SUBDIRS = libs/math libs/thread libs/raytrace libs/battleforge apps/nbody apps/rtdemo apps/battleforge
```

- [ ] **Step 5: Create stub files so the build system works**

Create a minimal `libs/battleforge/battleforge.h`:

```c
#ifndef BATTLEFORGE_H
#define BATTLEFORGE_H

#endif /* BATTLEFORGE_H */
```

Create a minimal `libs/battleforge/battleforge.c`:

```c
#include "battleforge.h"
```

Create a minimal `apps/battleforge/main.c`:

```c
#include "battleforge.h"

int main(void) {
    return 0;
}
```

- [ ] **Step 6: Regenerate build system and verify**

Run: `cd /home/rafa/repos/c && autoreconf -i && ./configure --quiet && make`
Expected: Compiles with no errors. `apps/battleforge/battleforge` binary exists.

- [ ] **Step 7: Commit**

```bash
git add libs/battleforge/ apps/battleforge/ configure.ac Makefile.am
git commit -m "feat(battleforge): scaffold build system for lib and app"
```

---

## Chunk 2: Public API Header

### Task 3: Write the battleforge public header

All types, enums, and function declarations from the spec.

**Files:**
- Create: `libs/battleforge/battleforge.h` (replace stub)

- [ ] **Step 1: Write the complete header**

Replace `libs/battleforge/battleforge.h` with:

```c
#ifndef BATTLEFORGE_H
#define BATTLEFORGE_H

#include <stdint.h>
#include "vector.h"
#include "raytrace.h"

/* --- Configuration --- */

typedef struct {
    int render_width;
    int render_height;
    float fov;            /* field of view in radians */
    int num_threads;      /* 0 = auto-detect via sysconf */
} bf_config;

/* --- Sprite definition --- */

typedef struct {
    float width;          /* world-space quad width */
    float height;         /* world-space quad height */
    int frame_count;      /* number of viewing angles */
    rt_frame *frames;     /* one frame per angle, clockwise from front */
} bf_sprite_def;

/* --- Map --- */

typedef struct {
    float width;          /* world units */
    float depth;          /* world units */
    uint8_t r, g, b;     /* ground color */
    float ambient;        /* ambient light level (0-1) */
    vector light_dir;     /* directional light direction */
    float light_intensity;/* directional light intensity (0-1) */
} bf_map;

/* --- Commands --- */

typedef enum {
    BF_CMD_CAMERA_SET,
    BF_CMD_CAMERA_MOVE,
    BF_CMD_ENTITY_CREATE,
    BF_CMD_ENTITY_DESTROY,
    BF_CMD_ENTITY_MOVE,
    BF_CMD_ENTITY_FACE,
    BF_CMD_ENTITY_SET_SPEED,
    BF_CMD_COUNT
} bf_cmd_type;

typedef struct {
    bf_cmd_type type;
    union {
        struct { vector position; vector direction; } camera_set;
        struct { vector delta; } camera_move;
        struct { int id; int sprite_id; vector position; vector direction; float speed; } entity_create;
        struct { int id; } entity_destroy;
        struct { int id; vector position; } entity_move;
        struct { int id; vector direction; } entity_face;
        struct { int id; float speed; } entity_set_speed;
    };
} bf_cmd;

/* --- Engine --- */

typedef struct bf_engine bf_engine;

bf_engine  *bf_create(bf_config config);
void        bf_destroy(bf_engine *e);

int         bf_register_sprite(bf_engine *e, bf_sprite_def def);
void        bf_set_map(bf_engine *e, bf_map map);

int         bf_command(bf_engine *e, bf_cmd cmd);
void        bf_tick(bf_engine *e, float dt);
void        bf_render(bf_engine *e, uint32_t *pixel_buf);

#endif /* BATTLEFORGE_H */
```

- [ ] **Step 2: Verify it compiles**

Run: `cd /home/rafa/repos/c && make`
Expected: Compiles with no errors.

- [ ] **Step 3: Commit**

```bash
git add libs/battleforge/battleforge.h
git commit -m "feat(battleforge): define complete public API header"
```

---

## Chunk 3: Engine Implementation

### Task 4: Implement the battleforge engine

The core engine: lifecycle, command queue, command handlers, tick simulation, and render pipeline.

**Files:**
- Create: `libs/battleforge/battleforge.c` (replace stub)

- [ ] **Step 1: Write engine struct and constants**

Replace `libs/battleforge/battleforge.c` with the following. Start with includes, constants, internal types, and the engine struct:

```c
#include "battleforge.h"
#include "raytrace.h"
#include "thread_pool.h"
#include "vector.h"
#include <stdlib.h>
#include <math.h>
#include <unistd.h>

#define CMD_QUEUE_SIZE 1024
#define MAX_ENTITIES 1024
#define MAX_SPRITES 256

typedef struct {
    int id;
    int sprite_id;
    vector position;
    vector direction;
    vector target;
    float speed;
    int active;
} bf_entity;

typedef struct {
    vector position;
    vector direction;
} bf_camera_state;

typedef struct {
    uint32_t *pixels;
    const rt_viewport *viewport;
    int y_start;
    int y_end;
    const rt_camera *camera;
    const rt_scene *scene;
} render_task;

struct bf_engine {
    bf_config config;
    bf_camera_state camera;
    bf_map map;
    int map_set;

    bf_sprite_def sprites[MAX_SPRITES];
    int sprite_count;

    bf_entity entities[MAX_ENTITIES];
    int entity_count;

    bf_cmd cmd_queue[CMD_QUEUE_SIZE];
    int cmd_head;
    int cmd_tail;
    int cmd_count;

    rt_scene *scene;
    rt_camera *rt_cam;
    rt_viewport viewport;
    thread_pool *pool;
    int num_threads;
    render_task *tasks;
};
```

- [ ] **Step 2: Add render chunk helper**

After the struct, add:

```c
static void render_chunk_fn(void *arg) {
    render_task *t = (render_task *)arg;
    rt_render_chunk(t->pixels, t->viewport, t->y_start, t->y_end,
                    t->camera, t->scene);
}
```

- [ ] **Step 3: Implement bf_create and bf_destroy**

```c
bf_engine *bf_create(bf_config config) {
    bf_engine *e = calloc(1, sizeof(bf_engine));
    if (!e) return NULL;

    e->config = config;
    e->viewport = (rt_viewport){ config.render_width, config.render_height, config.fov };

    /* Default camera */
    e->camera.position = (vector){0.0f, 5.0f, 10.0f};
    e->camera.direction = (vector){0.0f, -0.3f, -1.0f};

    /* Raytracer resources */
    e->scene = rt_scene_create();
    e->rt_cam = rt_camera_create(e->camera.position, e->camera.direction);
    if (!e->scene || !e->rt_cam) {
        bf_destroy(e);
        return NULL;
    }

    /* Thread pool */
    int nt = config.num_threads;
    if (nt <= 0) {
        nt = (int)sysconf(_SC_NPROCESSORS_ONLN);
        if (nt < 1) nt = 4;
    }
    e->num_threads = nt;
    e->pool = thread_pool_create(nt);
    e->tasks = malloc(sizeof(render_task) * nt);
    if (!e->pool || !e->tasks) {
        bf_destroy(e);
        return NULL;
    }

    return e;
}

void bf_destroy(bf_engine *e) {
    if (!e) return;
    if (e->pool) thread_pool_destroy(e->pool);
    free(e->tasks);
    if (e->rt_cam) rt_camera_destroy(e->rt_cam);
    if (e->scene) rt_scene_destroy(e->scene);
    free(e);
}
```

- [ ] **Step 4: Implement bf_register_sprite and bf_set_map**

```c
int bf_register_sprite(bf_engine *e, bf_sprite_def def) {
    if (e->sprite_count >= MAX_SPRITES) return -1;
    int id = e->sprite_count;
    e->sprites[e->sprite_count++] = def;
    return id;
}

void bf_set_map(bf_engine *e, bf_map map) {
    e->map = map;
    e->map_set = 1;
}
```

- [ ] **Step 5: Implement bf_command (queue enqueue)**

```c
int bf_command(bf_engine *e, bf_cmd cmd) {
    if (e->cmd_count >= CMD_QUEUE_SIZE) return -1;
    e->cmd_queue[e->cmd_tail] = cmd;
    e->cmd_tail = (e->cmd_tail + 1) % CMD_QUEUE_SIZE;
    e->cmd_count++;
    return 0;
}
```

- [ ] **Step 6: Implement command handlers**

```c
/* --- Entity lookup by id --- */

static bf_entity *find_entity(bf_engine *e, int id) {
    for (int i = 0; i < e->entity_count; i++) {
        if (e->entities[i].id == id && e->entities[i].active)
            return &e->entities[i];
    }
    return NULL;
}

/* --- Command handlers --- */

static void cmd_camera_set(bf_engine *e, const bf_cmd *cmd) {
    e->camera.position = cmd->camera_set.position;
    e->camera.direction = cmd->camera_set.direction;
}

static void cmd_camera_move(bf_engine *e, const bf_cmd *cmd) {
    e->camera.position = vector_add(e->camera.position, cmd->camera_move.delta);
}

static void cmd_entity_create(bf_engine *e, const bf_cmd *cmd) {
    if (e->entity_count >= MAX_ENTITIES) return;
    bf_entity ent = {
        .id = cmd->entity_create.id,
        .sprite_id = cmd->entity_create.sprite_id,
        .position = cmd->entity_create.position,
        .direction = cmd->entity_create.direction,
        .target = cmd->entity_create.position,
        .speed = cmd->entity_create.speed,
        .active = 1
    };
    e->entities[e->entity_count++] = ent;
}

static void cmd_entity_destroy(bf_engine *e, const bf_cmd *cmd) {
    bf_entity *ent = find_entity(e, cmd->entity_destroy.id);
    if (ent) ent->active = 0;
}

static void cmd_entity_move(bf_engine *e, const bf_cmd *cmd) {
    bf_entity *ent = find_entity(e, cmd->entity_move.id);
    if (ent) ent->target = cmd->entity_move.position;
}

static void cmd_entity_face(bf_engine *e, const bf_cmd *cmd) {
    bf_entity *ent = find_entity(e, cmd->entity_face.id);
    if (ent) ent->direction = cmd->entity_face.direction;
}

static void cmd_entity_set_speed(bf_engine *e, const bf_cmd *cmd) {
    bf_entity *ent = find_entity(e, cmd->entity_set_speed.id);
    if (ent) ent->speed = cmd->entity_set_speed.speed;
}

/* --- Dispatch table --- */

static void (*cmd_handlers[BF_CMD_COUNT])(bf_engine *, const bf_cmd *) = {
    [BF_CMD_CAMERA_SET]        = cmd_camera_set,
    [BF_CMD_CAMERA_MOVE]       = cmd_camera_move,
    [BF_CMD_ENTITY_CREATE]     = cmd_entity_create,
    [BF_CMD_ENTITY_DESTROY]    = cmd_entity_destroy,
    [BF_CMD_ENTITY_MOVE]       = cmd_entity_move,
    [BF_CMD_ENTITY_FACE]       = cmd_entity_face,
    [BF_CMD_ENTITY_SET_SPEED]  = cmd_entity_set_speed,
};
```

- [ ] **Step 7: Implement bf_tick**

```c
void bf_tick(bf_engine *e, float dt) {
    /* Process command queue */
    while (e->cmd_count > 0) {
        bf_cmd *cmd = &e->cmd_queue[e->cmd_head];
        if (cmd->type >= 0 && cmd->type < BF_CMD_COUNT && cmd_handlers[cmd->type])
            cmd_handlers[cmd->type](e, cmd);
        e->cmd_head = (e->cmd_head + 1) % CMD_QUEUE_SIZE;
        e->cmd_count--;
    }

    /* Advance entity movement */
    for (int i = 0; i < e->entity_count; i++) {
        bf_entity *ent = &e->entities[i];
        if (!ent->active || ent->speed <= 0.0f) continue;

        vector to_target = vector_sub(ent->target, ent->position);
        float dist = vector_magnitude(to_target);
        float step = ent->speed * dt;

        if (dist <= step) {
            ent->position = ent->target;
        } else {
            vector move_dir = vector_scale(to_target, 1.0f / dist);
            ent->position = vector_add(ent->position, vector_scale(move_dir, step));
        }
    }
}
```

- [ ] **Step 8: Implement bf_render**

```c
void bf_render(bf_engine *e, uint32_t *pixel_buf) {
    /* Update camera */
    rt_camera_place(e->rt_cam, e->camera.position, e->camera.direction);

    /* Rebuild scene */
    rt_scene_clear(e->scene);

    /* Lighting from map */
    if (e->map_set) {
        rt_scene_set_ambient(e->scene, e->map.ambient);
        rt_scene_add_light(e->scene, (rt_light){
            .direction = e->map.light_dir,
            .intensity = e->map.light_intensity
        });

        /* Ground plane */
        rt_scene_add_plane(e->scene, (rt_plane){
            .point = {0.0f, 0.0f, 0.0f},
            .normal = {0.0f, 1.0f, 0.0f},
            .color = {e->map.r, e->map.g, e->map.b}
        });
    }

    /* Entities as sprites */
    for (int i = 0; i < e->entity_count; i++) {
        bf_entity *ent = &e->entities[i];
        if (!ent->active) continue;
        if (ent->sprite_id < 0 || ent->sprite_id >= e->sprite_count) continue;

        bf_sprite_def *def = &e->sprites[ent->sprite_id];
        rt_scene_add_sprite(e->scene, (rt_sprite){
            .position = ent->position,
            .direction = ent->direction,
            .width = def->width,
            .height = def->height,
            .frame_count = def->frame_count,
            .frames = def->frames
        });
    }

    /* Multithreaded render */
    int render_h = e->viewport.height;
    int rows_per = render_h / e->num_threads;
    if (rows_per < 1) rows_per = 1;
    int chunks = render_h / rows_per;
    if (chunks > e->num_threads) chunks = e->num_threads;

    for (int i = 0; i < chunks; i++) {
        e->tasks[i] = (render_task){
            .pixels = pixel_buf,
            .viewport = &e->viewport,
            .y_start = i * rows_per,
            .y_end = (i == chunks - 1) ? render_h : (i + 1) * rows_per,
            .camera = e->rt_cam,
            .scene = e->scene
        };
        thread_pool_submit(e->pool, render_chunk_fn, &e->tasks[i]);
    }
    thread_pool_wait(e->pool);
}
```

- [ ] **Step 9: Verify it compiles**

Run: `cd /home/rafa/repos/c && make`
Expected: Compiles with no errors or warnings.

- [ ] **Step 10: Commit**

```bash
git add libs/battleforge/battleforge.c
git commit -m "feat(battleforge): implement engine core - lifecycle, commands, tick, render"
```

---

## Chunk 4: Shell App and Verification

### Task 5: Write the SDL shell app

A thin SDL shell that creates an engine, sets up a demo scene, translates keyboard input to commands, and displays rendered pixels.

**Files:**
- Create: `apps/battleforge/main.c` (replace stub)

- [ ] **Step 1: Write the complete shell**

Replace `apps/battleforge/main.c` with:

```c
#include "battleforge.h"
#include <SDL2/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#define WINDOW_W 800
#define WINDOW_H 600
#define FOV (M_PI / 3.0f)
#define MOVE_SPEED 8.0f
#define ROT_SPEED  2.0f

/* --- Reuse smiley face sprite from rtdemo --- */

#define S 16
#define PX(r,g,b) (0xFF000000u | ((r)<<16) | ((g)<<8) | (b))
#define TP 0x00000000u

static uint32_t frame_data[8][S * S];

static void set(uint32_t *buf, int x, int y, uint32_t c) {
    if (x >= 0 && x < S && y >= 0 && y < S)
        buf[y * S + x] = c;
}

static void fill_circle(uint32_t *buf, int cx, int cy, int r, uint32_t c) {
    for (int y = cy - r; y <= cy + r; y++)
        for (int x = cx - r; x <= cx + r; x++)
            if ((x-cx)*(x-cx) + (y-cy)*(y-cy) <= r*r)
                set(buf, x, y, c);
}

static void clear_frame(uint32_t *buf) {
    for (int i = 0; i < S * S; i++) buf[i] = TP;
}

static void draw_head(uint32_t *buf, uint32_t skin_c, uint32_t hair_c) {
    fill_circle(buf, 7, 7, 6, skin_c);
    for (int x = 2; x <= 12; x++)
        for (int y = 1; y <= 3; y++)
            if ((x-7)*(x-7) + (y-7)*(y-7) <= 36)
                set(buf, x, y, hair_c);
}

static void init_sprite_frames(void) {
    uint32_t skin  = PX(255, 200, 150);
    uint32_t hair  = PX(100,  60,  20);
    uint32_t eye_w = PX(255, 255, 255);
    uint32_t eye_p = PX( 30,  30,  30);
    uint32_t mouth = PX(200,  60,  60);

    /* Frame 0: Front */
    clear_frame(frame_data[0]);
    draw_head(frame_data[0], skin, hair);
    fill_circle(frame_data[0], 5, 6, 1, eye_w);
    set(frame_data[0], 5, 6, eye_p);
    fill_circle(frame_data[0], 9, 6, 1, eye_w);
    set(frame_data[0], 9, 6, eye_p);
    set(frame_data[0], 5, 10, mouth);
    set(frame_data[0], 6, 11, mouth);
    set(frame_data[0], 7, 11, mouth);
    set(frame_data[0], 8, 11, mouth);
    set(frame_data[0], 9, 10, mouth);

    /* Frame 1: Front-right */
    clear_frame(frame_data[1]);
    draw_head(frame_data[1], skin, hair);
    fill_circle(frame_data[1], 6, 6, 1, eye_w);
    set(frame_data[1], 7, 6, eye_p);
    fill_circle(frame_data[1], 10, 6, 1, eye_w);
    set(frame_data[1], 11, 6, eye_p);
    set(frame_data[1], 7, 10, mouth);
    set(frame_data[1], 8, 11, mouth);
    set(frame_data[1], 9, 11, mouth);
    set(frame_data[1], 10, 10, mouth);

    /* Frame 2: Right */
    clear_frame(frame_data[2]);
    draw_head(frame_data[2], skin, hair);
    fill_circle(frame_data[2], 9, 6, 1, eye_w);
    set(frame_data[2], 10, 6, eye_p);
    set(frame_data[2], 12, 7, skin);
    set(frame_data[2], 13, 8, skin);
    set(frame_data[2], 9, 10, mouth);
    set(frame_data[2], 10, 11, mouth);
    set(frame_data[2], 11, 10, mouth);

    /* Frame 3: Back-right */
    clear_frame(frame_data[3]);
    draw_head(frame_data[3], skin, hair);
    set(frame_data[3], 2, 7, skin);
    set(frame_data[3], 1, 7, skin);
    set(frame_data[3], 1, 8, skin);

    /* Frame 4: Back */
    clear_frame(frame_data[4]);
    draw_head(frame_data[4], skin, hair);
    for (int x = 3; x <= 11; x++)
        for (int y = 2; y <= 6; y++)
            if ((x-7)*(x-7) + (y-7)*(y-7) <= 36)
                set(frame_data[4], x, y, hair);

    /* Frame 5: Back-left */
    clear_frame(frame_data[5]);
    draw_head(frame_data[5], skin, hair);
    set(frame_data[5], 12, 7, skin);
    set(frame_data[5], 13, 7, skin);
    set(frame_data[5], 13, 8, skin);

    /* Frame 6: Left */
    clear_frame(frame_data[6]);
    draw_head(frame_data[6], skin, hair);
    fill_circle(frame_data[6], 5, 6, 1, eye_w);
    set(frame_data[6], 4, 6, eye_p);
    set(frame_data[6], 2, 7, skin);
    set(frame_data[6], 1, 8, skin);
    set(frame_data[6], 3, 10, mouth);
    set(frame_data[6], 4, 11, mouth);
    set(frame_data[6], 5, 10, mouth);

    /* Frame 7: Front-left */
    clear_frame(frame_data[7]);
    draw_head(frame_data[7], skin, hair);
    fill_circle(frame_data[7], 4, 6, 1, eye_w);
    set(frame_data[7], 3, 6, eye_p);
    fill_circle(frame_data[7], 8, 6, 1, eye_w);
    set(frame_data[7], 7, 6, eye_p);
    set(frame_data[7], 4, 10, mouth);
    set(frame_data[7], 5, 11, mouth);
    set(frame_data[7], 6, 11, mouth);
    set(frame_data[7], 7, 10, mouth);
}

/* --- Main --- */

int main(void) {
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        fprintf(stderr, "SDL init failed: %s\n", SDL_GetError());
        return 1;
    }

    SDL_Window *window = SDL_CreateWindow("Battleforge",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        WINDOW_W, WINDOW_H, 0);
    if (!window) {
        fprintf(stderr, "Window creation failed: %s\n", SDL_GetError());
        return 1;
    }

    SDL_Renderer *renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    SDL_Texture *texture = SDL_CreateTexture(renderer,
        SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING,
        WINDOW_W, WINDOW_H);

    uint32_t *pixels = calloc(WINDOW_W * WINDOW_H, sizeof(uint32_t));

    /* Create engine */
    bf_engine *engine = bf_create((bf_config){
        .render_width = WINDOW_W,
        .render_height = WINDOW_H,
        .fov = FOV,
        .num_threads = 0
    });

    /* Set map */
    bf_set_map(engine, (bf_map){
        .width = 100.0f,
        .depth = 100.0f,
        .r = 80, .g = 120, .b = 40,
        .ambient = 0.15f,
        .light_dir = {1.0f, 1.0f, -1.0f},
        .light_intensity = 0.85f
    });

    /* Register sprite */
    init_sprite_frames();
    rt_frame frames[8];
    for (int i = 0; i < 8; i++)
        frames[i] = (rt_frame){ frame_data[i], S, S };

    int spr_id = bf_register_sprite(engine, (bf_sprite_def){
        .width = 2.0f,
        .height = 2.0f,
        .frame_count = 8,
        .frames = frames
    });

    /* Create entities */
    bf_command(engine, (bf_cmd){
        .type = BF_CMD_ENTITY_CREATE,
        .entity_create = { .id = 1, .sprite_id = spr_id,
                           .position = {0.0f, 1.0f, 0.0f},
                           .direction = {0.0f, 0.0f, 1.0f},
                           .speed = 3.0f }
    });
    bf_command(engine, (bf_cmd){
        .type = BF_CMD_ENTITY_CREATE,
        .entity_create = { .id = 2, .sprite_id = spr_id,
                           .position = {5.0f, 1.0f, -3.0f},
                           .direction = {-1.0f, 0.0f, 0.0f},
                           .speed = 0.0f }
    });
    bf_command(engine, (bf_cmd){
        .type = BF_CMD_ENTITY_CREATE,
        .entity_create = { .id = 3, .sprite_id = spr_id,
                           .position = {-4.0f, 1.0f, 2.0f},
                           .direction = {1.0f, 0.0f, 0.0f},
                           .speed = 0.0f }
    });

    /* Camera starts looking at the scene */
    bf_command(engine, (bf_cmd){
        .type = BF_CMD_CAMERA_SET,
        .camera_set = {
            .position = {0.0f, 8.0f, 15.0f},
            .direction = {0.0f, -0.4f, -1.0f}
        }
    });

    /* Entity 1 patrol target */
    bf_command(engine, (bf_cmd){
        .type = BF_CMD_ENTITY_MOVE,
        .entity_move = { .id = 1, .position = {8.0f, 1.0f, 0.0f} }
    });

    float cam_yaw = 0.0f;  /* facing -Z initially */
    float cam_x = 0.0f, cam_y = 8.0f, cam_z = 15.0f;
    int patrol_dir = 1;  /* 1 = going right, 0 = going left */

    Uint32 fps_last = SDL_GetTicks();
    Uint32 frame_last = SDL_GetTicks();
    int fps_frames = 0;
    char title_buf[128];
    int running = 1;

    while (running) {
        Uint32 frame_now = SDL_GetTicks();
        float dt = (frame_now - frame_last) / 1000.0f;
        frame_last = frame_now;
        if (dt > 0.1f) dt = 0.1f;  /* cap to avoid spiral of death */

        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) running = 0;
            if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE)
                running = 0;
        }

        /* Continuous key input for camera */
        const Uint8 *keys = SDL_GetKeyboardState(NULL);
        float move_x = 0.0f, move_z = 0.0f;

        if (keys[SDL_SCANCODE_W]) { move_x += sinf(cam_yaw); move_z += -cosf(cam_yaw); }
        if (keys[SDL_SCANCODE_S]) { move_x -= sinf(cam_yaw); move_z -= -cosf(cam_yaw); }
        if (keys[SDL_SCANCODE_A]) { move_x += cosf(cam_yaw); move_z += sinf(cam_yaw); }
        if (keys[SDL_SCANCODE_D]) { move_x -= cosf(cam_yaw); move_z -= sinf(cam_yaw); }
        if (keys[SDL_SCANCODE_LEFT])  cam_yaw -= ROT_SPEED * dt;
        if (keys[SDL_SCANCODE_RIGHT]) cam_yaw += ROT_SPEED * dt;
        if (keys[SDL_SCANCODE_SPACE]) cam_y += MOVE_SPEED * dt;
        if (keys[SDL_SCANCODE_LSHIFT]) cam_y -= MOVE_SPEED * dt;

        cam_x += move_x * MOVE_SPEED * dt;
        cam_z += move_z * MOVE_SPEED * dt;

        bf_command(engine, (bf_cmd){
            .type = BF_CMD_CAMERA_SET,
            .camera_set = {
                .position = {cam_x, cam_y, cam_z},
                .direction = {sinf(cam_yaw), -0.3f, -cosf(cam_yaw)}
            }
        });

        /* Simple patrol: entity 1 bounces between two points */
        /* Check approximate position via tick count (simple heuristic) */
        static float patrol_timer = 0.0f;
        patrol_timer += dt;
        if (patrol_timer > 3.0f) {
            patrol_timer = 0.0f;
            if (patrol_dir) {
                bf_command(engine, (bf_cmd){
                    .type = BF_CMD_ENTITY_MOVE,
                    .entity_move = { .id = 1, .position = {-8.0f, 1.0f, 0.0f} }
                });
            } else {
                bf_command(engine, (bf_cmd){
                    .type = BF_CMD_ENTITY_MOVE,
                    .entity_move = { .id = 1, .position = {8.0f, 1.0f, 0.0f} }
                });
            }
            patrol_dir = !patrol_dir;
        }

        bf_tick(engine, dt);
        bf_render(engine, pixels);

        SDL_UpdateTexture(texture, NULL, pixels, WINDOW_W * sizeof(uint32_t));
        SDL_RenderCopy(renderer, texture, NULL, NULL);
        SDL_RenderPresent(renderer);

        fps_frames++;
        Uint32 now = SDL_GetTicks();
        if (now - fps_last >= 1000) {
            snprintf(title_buf, sizeof(title_buf),
                     "Battleforge - %d FPS (%dx%d)", fps_frames,
                     WINDOW_W, WINDOW_H);
            SDL_SetWindowTitle(window, title_buf);
            fprintf(stderr, "%d FPS\n", fps_frames);
            fps_frames = 0;
            fps_last = now;
        }
    }

    bf_destroy(engine);
    free(pixels);
    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
```

- [ ] **Step 2: Verify it compiles**

Run: `cd /home/rafa/repos/c && make`
Expected: Compiles with no errors or warnings.

- [ ] **Step 3: Run and verify visually**

Run: `cd /home/rafa/repos/c && ./apps/battleforge/battleforge`
Expected:
- Window opens showing a green ground plane with 3 smiley face sprites
- Entity 1 (center) patrols back and forth, turning as it moves
- Entities 2 and 3 stand still
- WASD moves the camera, arrow keys rotate, Space/Shift moves up/down
- FPS counter in window title and stderr

- [ ] **Step 4: Commit**

```bash
git add apps/battleforge/main.c
git commit -m "feat(battleforge): add SDL shell with demo scene and free camera"
```

---

### Task 6: Final verification

- [ ] **Step 1: Clean rebuild**

Run: `cd /home/rafa/repos/c && make clean && make`
Expected: Full clean build with no errors or warnings.

- [ ] **Step 2: Verify both apps still work**

Run rtdemo: `./apps/rtdemo/rtdemo` — should work as before (no regressions from rt_scene_clear fix).
Run battleforge: `./apps/battleforge/battleforge` — demo scene with camera controls and patrolling entity.

- [ ] **Step 3: Commit any remaining changes**

If any fixes were needed, commit them now.

---

## Verification Checklist

After all tasks are complete:

- [ ] `make clean && make` compiles with no errors or warnings
- [ ] `apps/battleforge/battleforge` runs and shows the demo scene
- [ ] Camera moves with WASD, rotates with arrow keys, rises/lowers with Space/Shift
- [ ] Entity 1 patrols back and forth (bf_tick entity movement works)
- [ ] Entities 2 and 3 stand still with different facing directions
- [ ] Sprite angle selection works (face changes as camera moves around entities)
- [ ] Ground plane is visible and lit
- [ ] FPS counter works in window title
- [ ] `apps/rtdemo/rtdemo` still works (no regressions)
