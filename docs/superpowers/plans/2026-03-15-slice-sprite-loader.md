# Slice Sprite Loader Implementation Plan

> **For agentic workers:** REQUIRED: Use superpowers:subagent-driven-development (if subagents available) or superpowers:executing-plans to implement this plan. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a sprite sheet loader library (`libs/slice`) that loads PNG+INI sprite sheets, and integrate animation playback into the battleforge engine.

**Architecture:** New `libs/slice` library handles PNG loading (via vendored `stb_image.h`) and INI parsing. Battleforge engine replaces `bf_sprite_def` with `slice_sheet*` for all sprite storage, adds animation state to entities, and adds `BF_CMD_ENTITY_ANIMATE`. Shell converts its procedural smiley into a programmatic `slice_sheet` to keep the demo working.

**Tech Stack:** C11, GNU Autotools, stb_image.h (vendored, public domain)

**Spec:** `docs/superpowers/specs/2026-03-15-slice-sprite-loader-design.md`

---

## File Structure

**New files:**
- `libs/slice/slice.h` — public API: `slice_sheet`, `slice_anim`, `slice_load`, `slice_free`, `slice_anim_index`
- `libs/slice/slice.c` — implementation: PNG loading, INI parsing, pixel slicing, RGBA→ARGB conversion
- `libs/slice/stb_image.h` — vendored single-header image loader
- `libs/slice/Makefile.am` — autotools build for libslice

**Modified files:**
- `configure.ac` — add `libs/slice/Makefile` to `AC_CONFIG_FILES`
- `Makefile.am` — add `libs/slice` to `SUBDIRS`
- `libs/battleforge/Makefile.am` — link libslice, add include path
- `libs/battleforge/battleforge.h` — remove `bf_sprite_def`, change `bf_register_sprite` signature, add `BF_CMD_ENTITY_ANIMATE`, forward-declare `slice_sheet`
- `libs/battleforge/battleforge.c` — `bf_sprite_entry`, animation fields in `bf_entity`, animate command handler, tick animation, render/pick frame building via helper
- `apps/battleforge/Makefile.am` — add libslice include path
- `apps/battleforge/main.c` — build programmatic `slice_sheet` from procedural frames, use new `bf_register_sprite` signature

---

## Chunk 1: Slice Library

### Task 1: Build system scaffolding

**Files:**
- Create: `libs/slice/Makefile.am`
- Modify: `configure.ac:11-20`
- Modify: `Makefile.am:5`

- [ ] **Step 1: Create `libs/slice/Makefile.am`**

```makefile
noinst_LTLIBRARIES = libslice.la
libslice_la_SOURCES = slice.c slice.h
EXTRA_DIST = stb_image.h
```

- [ ] **Step 2: Add `libs/slice/Makefile` to `configure.ac`**

In `AC_CONFIG_FILES`, add `libs/slice/Makefile` after `libs/battleforge/Makefile`.

- [ ] **Step 3: Add `libs/slice` to root `Makefile.am` SUBDIRS**

Insert `libs/slice` after `libs/raytrace` and before `libs/battleforge`:

```makefile
SUBDIRS = libs/math libs/thread libs/raytrace libs/slice libs/battleforge apps/nbody apps/rtdemo apps/battleforge
```

- [ ] **Step 4: Create empty `libs/slice/slice.c` with stub**

```c
#include "slice.h"
```

- [ ] **Step 5: Create `libs/slice/slice.h` with minimal content**

```c
#ifndef SLICE_H
#define SLICE_H

#include <stdint.h>

typedef struct {
    char name[32];
    int *columns;
    int column_count;
    int loop;
} slice_anim;

typedef struct slice_sheet {
    uint32_t **pixels;
    int angles;
    int total_columns;
    int frame_width;
    int frame_height;
    float fps;
    slice_anim *anims;
    int anim_count;
} slice_sheet;

slice_sheet *slice_load(const char *png_path);
void slice_free(slice_sheet *sheet);
int slice_anim_index(const slice_sheet *sheet, const char *name);

#endif /* SLICE_H */
```

- [ ] **Step 6: Verify build**

Run: `autoreconf -i && ./configure && make`
Expected: Clean build, libslice.la created.

- [ ] **Step 7: Commit**

```bash
git add libs/slice/Makefile.am libs/slice/slice.h libs/slice/slice.c configure.ac Makefile.am
git commit -m "feat(slice): add build scaffolding for libs/slice"
```

---

### Task 2: Vendor stb_image.h

**Files:**
- Create: `libs/slice/stb_image.h`

- [ ] **Step 1: Download stb_image.h**

```bash
curl -o libs/slice/stb_image.h https://raw.githubusercontent.com/nothings/stb/master/stb_image.h
```

- [ ] **Step 2: Verify the file was downloaded**

Check that `libs/slice/stb_image.h` exists and contains the `stbi_load` function declaration.

- [ ] **Step 3: Commit**

```bash
git add libs/slice/stb_image.h
git commit -m "vendor: add stb_image.h for PNG loading"
```

---

### Task 3: Implement INI parser and PNG slicer

**Files:**
- Modify: `libs/slice/slice.c`

This is the core of the slice library. It implements:
1. `slice_load` — reads PNG via stb_image, reads INI sidecar, slices into per-cell pixel buffers
2. `slice_free` — frees all owned memory
3. `slice_anim_index` — name lookup

- [ ] **Step 1: Implement `slice_anim_index`**

In `libs/slice/slice.c`:

```c
#include "slice.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

int slice_anim_index(const slice_sheet *sheet, const char *name) {
    if (!sheet || !name) return -1;
    for (int i = 0; i < sheet->anim_count; i++) {
        if (strcmp(sheet->anims[i].name, name) == 0)
            return i;
    }
    return -1;
}
```

- [ ] **Step 2: Implement `slice_free`**

```c
void slice_free(slice_sheet *sheet) {
    if (!sheet) return;
    if (sheet->pixels) {
        int total = sheet->angles * sheet->total_columns;
        for (int i = 0; i < total; i++)
            free(sheet->pixels[i]);
        free(sheet->pixels);
    }
    if (sheet->anims) {
        for (int i = 0; i < sheet->anim_count; i++)
            free(sheet->anims[i].columns);
        free(sheet->anims);
    }
    free(sheet);
}
```

- [ ] **Step 3: Implement INI parsing helper**

A static function that reads the INI file and populates the sheet's metadata and animation list:

```c
#define MAX_ANIMS 64
#define MAX_LINE 256

/* Parse comma-separated integers into out array. Returns count. */
static int parse_int_list(const char *str, int *out, int max) {
    int count = 0;
    const char *p = str;
    while (*p && count < max) {
        out[count++] = atoi(p);
        while (*p && *p != ',') p++;
        if (*p == ',') p++;
    }
    return count;
}

/* Read INI sidecar file. Returns 0 on success, -1 on failure. */
static int parse_ini(const char *ini_path, slice_sheet *sheet) {
    FILE *f = fopen(ini_path, "r");
    if (!f) {
        fprintf(stderr, "slice: cannot open INI '%s'\n", ini_path);
        return -1;
    }

    slice_anim anims[MAX_ANIMS];
    int anim_count = 0;
    int in_section = 0;  /* 1 = inside an [animation] section */
    char line[MAX_LINE];

    while (fgets(line, sizeof(line), f)) {
        /* Strip newline */
        char *nl = strchr(line, '\n');
        if (nl) *nl = '\0';
        char *cr = strchr(line, '\r');
        if (cr) *cr = '\0';

        /* Skip empty lines and comments */
        if (line[0] == '\0' || line[0] == '#' || line[0] == ';')
            continue;

        /* Section header */
        if (line[0] == '[') {
            char *end = strchr(line, ']');
            if (!end) continue;
            *end = '\0';
            if (anim_count >= MAX_ANIMS) continue;
            memset(&anims[anim_count], 0, sizeof(slice_anim));
            strncpy(anims[anim_count].name, line + 1, 31);
            anims[anim_count].name[31] = '\0';
            anims[anim_count].loop = 1;  /* default */
            in_section = 1;
            anim_count++;
            continue;
        }

        /* Key=value */
        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = '\0';
        char *key = line;
        char *val = eq + 1;

        if (!in_section) {
            /* Global keys */
            if (strcmp(key, "frame_width") == 0) sheet->frame_width = atoi(val);
            else if (strcmp(key, "frame_height") == 0) sheet->frame_height = atoi(val);
            else if (strcmp(key, "angles") == 0) sheet->angles = atoi(val);
            else if (strcmp(key, "fps") == 0) sheet->fps = (float)atof(val);
        } else {
            /* Animation section keys */
            slice_anim *a = &anims[anim_count - 1];
            if (strcmp(key, "frames") == 0) {
                int tmp[256];
                int n = parse_int_list(val, tmp, 256);
                a->columns = malloc(sizeof(int) * n);
                if (a->columns) {
                    memcpy(a->columns, tmp, sizeof(int) * n);
                    a->column_count = n;
                }
            } else if (strcmp(key, "loop") == 0) {
                a->loop = (strcmp(val, "false") != 0);
            }
        }
    }

    fclose(f);

    /* Copy anims to heap */
    if (anim_count > 0) {
        sheet->anims = malloc(sizeof(slice_anim) * anim_count);
        if (sheet->anims) {
            memcpy(sheet->anims, anims, sizeof(slice_anim) * anim_count);
            sheet->anim_count = anim_count;
        }
    }

    return 0;
}
```

- [ ] **Step 4: Implement `slice_load`**

```c
slice_sheet *slice_load(const char *png_path) {
    if (!png_path) return NULL;

    /* Build INI path by replacing .png with .ini */
    size_t len = strlen(png_path);
    if (len < 5) {
        fprintf(stderr, "slice: path too short '%s'\n", png_path);
        return NULL;
    }
    char *ini_path = malloc(len + 1);
    if (!ini_path) return NULL;
    memcpy(ini_path, png_path, len + 1);
    memcpy(ini_path + len - 4, ".ini", 5);

    /* Allocate sheet */
    slice_sheet *sheet = calloc(1, sizeof(slice_sheet));
    if (!sheet) { free(ini_path); return NULL; }

    /* Parse INI first to get frame dimensions and angles */
    if (parse_ini(ini_path, sheet) < 0) {
        free(ini_path);
        slice_free(sheet);
        return NULL;
    }
    free(ini_path);

    /* Validate INI values */
    if (sheet->frame_width <= 0 || sheet->frame_height <= 0 ||
        sheet->angles <= 0 || sheet->fps <= 0.0f) {
        fprintf(stderr, "slice: invalid INI values (fw=%d fh=%d angles=%d fps=%.1f)\n",
                sheet->frame_width, sheet->frame_height, sheet->angles, sheet->fps);
        slice_free(sheet);
        return NULL;
    }

    /* Load PNG */
    int img_w, img_h, channels;
    unsigned char *rgba = stbi_load(png_path, &img_w, &img_h, &channels, 4);
    if (!rgba) {
        fprintf(stderr, "slice: cannot load PNG '%s': %s\n", png_path, stbi_failure_reason());
        slice_free(sheet);
        return NULL;
    }

    /* Compute grid */
    sheet->total_columns = img_w / sheet->frame_width;
    int expected_rows = sheet->angles;
    int actual_rows = img_h / sheet->frame_height;
    if (actual_rows < expected_rows) {
        fprintf(stderr, "slice: PNG has %d rows but INI expects %d angles\n",
                actual_rows, expected_rows);
        stbi_image_free(rgba);
        slice_free(sheet);
        return NULL;
    }

    /* Allocate pixel buffer array */
    int total_cells = sheet->angles * sheet->total_columns;
    sheet->pixels = calloc(total_cells, sizeof(uint32_t *));
    if (!sheet->pixels) {
        stbi_image_free(rgba);
        slice_free(sheet);
        return NULL;
    }

    /* Slice each cell: RGBA → ARGB conversion */
    int fw = sheet->frame_width;
    int fh = sheet->frame_height;
    for (int row = 0; row < sheet->angles; row++) {
        for (int col = 0; col < sheet->total_columns; col++) {
            uint32_t *buf = malloc(sizeof(uint32_t) * fw * fh);
            if (!buf) {
                stbi_image_free(rgba);
                slice_free(sheet);
                return NULL;
            }
            sheet->pixels[row * sheet->total_columns + col] = buf;

            for (int y = 0; y < fh; y++) {
                for (int x = 0; x < fw; x++) {
                    int src_x = col * fw + x;
                    int src_y = row * fh + y;
                    unsigned char *src = &rgba[4 * (src_y * img_w + src_x)];
                    buf[y * fw + x] = ((uint32_t)src[3] << 24)
                                    | ((uint32_t)src[0] << 16)
                                    | ((uint32_t)src[1] << 8)
                                    |  (uint32_t)src[2];
                }
            }
        }
    }

    stbi_image_free(rgba);
    return sheet;
}
```

- [ ] **Step 5: Verify build**

Run: `make`
Expected: Clean build with no warnings from slice.c.

- [ ] **Step 6: Commit**

```bash
git add libs/slice/slice.c
git commit -m "feat(slice): implement PNG + INI sprite sheet loader"
```

---

## Chunk 2: Battleforge Engine Changes

### Task 4: Update battleforge public API

**Files:**
- Modify: `libs/battleforge/battleforge.h`
- Modify: `libs/battleforge/Makefile.am`
- Modify: `apps/battleforge/Makefile.am`

- [ ] **Step 1: Update battleforge Makefile.am — add slice dependency**

```makefile
noinst_LTLIBRARIES = libbattleforge.la
libbattleforge_la_SOURCES = battleforge.c
libbattleforge_la_CPPFLAGS = -I$(top_srcdir)/libs/math -I$(top_srcdir)/libs/raytrace -I$(top_srcdir)/libs/thread -I$(top_srcdir)/libs/slice
libbattleforge_la_LIBADD = $(top_builddir)/libs/raytrace/libraytrace.la $(top_builddir)/libs/thread/libthread.la $(top_builddir)/libs/slice/libslice.la -lm -lpthread
```

- [ ] **Step 2: Update apps/battleforge/Makefile.am — add slice include path**

```makefile
bin_PROGRAMS = battleforge
battleforge_SOURCES = main.c
battleforge_CPPFLAGS = -I$(top_srcdir)/libs/math -I$(top_srcdir)/libs/raytrace -I$(top_srcdir)/libs/battleforge -I$(top_srcdir)/libs/thread -I$(top_srcdir)/libs/slice $(SDL2_CFLAGS)
battleforge_LDADD = $(top_builddir)/libs/battleforge/libbattleforge.la $(top_builddir)/libs/raytrace/libraytrace.la $(top_builddir)/libs/thread/libthread.la $(top_builddir)/libs/slice/libslice.la -lm -lpthread $(SDL2_LIBS)
```

- [ ] **Step 3: Update `battleforge.h`**

Replace `bf_sprite_def` with forward declaration of `slice_sheet`. Change `bf_register_sprite` signature. Add `BF_CMD_ENTITY_ANIMATE` to the enum and union:

```c
#ifndef BATTLEFORGE_H
#define BATTLEFORGE_H

#include <stdint.h>
#include "vector.h"
#include "raytrace.h"

/* Forward declaration — full definition in slice.h */
typedef struct slice_sheet slice_sheet;

/* --- Configuration --- */

typedef struct {
    int render_width;
    int render_height;
    float fov;
    int num_threads;
} bf_config;

/* --- Map --- */

typedef struct {
    float width;
    float depth;
    uint8_t r, g, b;
    float ambient;
    vector light_dir;
    float light_intensity;
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
    BF_CMD_SELECT,
    BF_CMD_ENTITY_ANIMATE,
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
        struct { int id; } select;
        struct { int id; int anim_index; } entity_animate;
    };
} bf_cmd;

/* --- Picking --- */

typedef enum {
    BF_PICK_SKY,
    BF_PICK_GROUND,
    BF_PICK_ENTITY
} bf_pick_type;

typedef struct {
    bf_pick_type type;
    int entity_id;
    vector position;
} bf_pick_result;

/* --- Engine --- */

typedef struct bf_engine bf_engine;

bf_engine  *bf_create(bf_config config);
void        bf_destroy(bf_engine *e);

int         bf_register_sprite(bf_engine *e, slice_sheet *sheet,
                               float world_width, float world_height);
void        bf_set_map(bf_engine *e, bf_map map);

int         bf_command(bf_engine *e, bf_cmd cmd);
void        bf_tick(bf_engine *e, float dt);
void        bf_render(bf_engine *e, uint32_t *pixel_buf);
bf_pick_result bf_pick(bf_engine *e, int screen_x, int screen_y);

#endif /* BATTLEFORGE_H */
```

Note: `slice_sheet` is forward-declared as a typedef here. This works because `slice.h` already defines it with a struct tag (`typedef struct slice_sheet { ... } slice_sheet;`) from Task 1.

- [ ] **Step 4: Verify build**

Run: `make`
Expected: Compile errors in `apps/battleforge/main.c` (removed `bf_sprite_def` type, changed `bf_register_sprite` signature). This is expected — the shell will be fixed in Task 6. The libraries themselves should compile cleanly.

- [ ] **Step 5: Commit**

```bash
git add libs/battleforge/battleforge.h libs/battleforge/Makefile.am apps/battleforge/Makefile.am
git commit -m "feat(battleforge): update public API for slice_sheet sprites and animation"
```

---

### Task 5: Update battleforge engine internals

**Files:**
- Modify: `libs/battleforge/battleforge.c`

- [ ] **Step 1: Add slice include and update sprite storage**

At the top of `battleforge.c`, add:

```c
#include "slice.h"
```

Replace the `bf_sprite_def sprites[MAX_SPRITES]` in `struct bf_engine` with:

```c
    struct {
        slice_sheet *sheet;
        float width;
        float height;
    } sprites[MAX_SPRITES];
```

- [ ] **Step 2: Add animation fields to `bf_entity`**

Add these fields to the `bf_entity` struct:

```c
    int anim_index;     /* -1 = no animation */
    int anim_frame;     /* current frame within animation */
    float frame_timer;  /* time accumulator */
    float anim_fps;     /* playback speed */
```

- [ ] **Step 3: Update `bf_register_sprite`**

```c
int bf_register_sprite(bf_engine *e, slice_sheet *sheet,
                       float world_width, float world_height) {
    if (e->sprite_count >= MAX_SPRITES) return -1;
    int id = e->sprite_count;
    e->sprites[e->sprite_count].sheet = sheet;
    e->sprites[e->sprite_count].width = world_width;
    e->sprites[e->sprite_count].height = world_height;
    e->sprite_count++;
    return id;
}
```

- [ ] **Step 4: Update `cmd_entity_create` to initialize animation fields**

```c
static void cmd_entity_create(bf_engine *e, const bf_cmd *cmd) {
    if (e->entity_count >= MAX_ENTITIES) return;
    bf_entity ent = {
        .id = cmd->entity_create.id,
        .sprite_id = cmd->entity_create.sprite_id,
        .position = cmd->entity_create.position,
        .direction = cmd->entity_create.direction,
        .target = cmd->entity_create.position,
        .speed = cmd->entity_create.speed,
        .active = 1,
        .anim_index = -1,
        .anim_frame = 0,
        .frame_timer = 0.0f,
        .anim_fps = 0.0f
    };
    e->entities[e->entity_count++] = ent;
}
```

- [ ] **Step 5: Add `cmd_entity_animate` handler**

```c
static void cmd_entity_animate(bf_engine *e, const bf_cmd *cmd) {
    bf_entity *ent = find_entity(e, cmd->entity_animate.id);
    if (!ent) return;
    if (ent->sprite_id < 0 || ent->sprite_id >= e->sprite_count) return;
    slice_sheet *sheet = e->sprites[ent->sprite_id].sheet;
    if (!sheet) return;
    int ai = cmd->entity_animate.anim_index;
    if (ai < 0 || ai >= sheet->anim_count) return;
    ent->anim_index = ai;
    ent->anim_frame = 0;
    ent->frame_timer = 0.0f;
    ent->anim_fps = sheet->fps;
}
```

- [ ] **Step 6: Add handler to dispatch table**

```c
static void (*cmd_handlers[BF_CMD_COUNT])(bf_engine *, const bf_cmd *) = {
    [BF_CMD_CAMERA_SET]        = cmd_camera_set,
    [BF_CMD_CAMERA_MOVE]       = cmd_camera_move,
    [BF_CMD_ENTITY_CREATE]     = cmd_entity_create,
    [BF_CMD_ENTITY_DESTROY]    = cmd_entity_destroy,
    [BF_CMD_ENTITY_MOVE]       = cmd_entity_move,
    [BF_CMD_ENTITY_FACE]       = cmd_entity_face,
    [BF_CMD_ENTITY_SET_SPEED]  = cmd_entity_set_speed,
    [BF_CMD_SELECT]            = cmd_select,
    [BF_CMD_ENTITY_ANIMATE]    = cmd_entity_animate,
};
```

- [ ] **Step 7: Add animation advancement to `bf_tick`**

After the existing movement loop in `bf_tick`, add:

```c
    /* Advance animation */
    for (int i = 0; i < e->entity_count; i++) {
        bf_entity *ent = &e->entities[i];
        if (!ent->active || ent->anim_index < 0) continue;
        if (ent->sprite_id < 0 || ent->sprite_id >= e->sprite_count) continue;

        slice_sheet *sheet = e->sprites[ent->sprite_id].sheet;
        if (!sheet || ent->anim_index >= sheet->anim_count) continue;
        slice_anim *anim = &sheet->anims[ent->anim_index];
        if (anim->column_count <= 1) continue;

        ent->frame_timer += dt;
        float interval = 1.0f / ent->anim_fps;
        while (ent->frame_timer >= interval) {
            ent->frame_timer -= interval;
            ent->anim_frame++;
            if (ent->anim_frame >= anim->column_count) {
                if (anim->loop)
                    ent->anim_frame = 0;
                else
                    ent->anim_frame = anim->column_count - 1;
            }
        }
    }
```

- [ ] **Step 8: Add `build_sprite_frames` helper**

This helper is shared by both `bf_render` and `bf_pick`:

```c
/* Build rt_frame array for an entity's current animation state.
   out_frames must have room for sheet->angles entries.
   Returns the sheet pointer, or NULL if invalid. */
static slice_sheet *build_sprite_frames(bf_engine *e, bf_entity *ent,
                                        rt_frame *out_frames) {
    if (ent->sprite_id < 0 || ent->sprite_id >= e->sprite_count)
        return NULL;
    slice_sheet *sheet = e->sprites[ent->sprite_id].sheet;
    if (!sheet) return NULL;

    /* Determine which column to use */
    int col = 0;
    if (ent->anim_index >= 0 && ent->anim_index < sheet->anim_count) {
        slice_anim *anim = &sheet->anims[ent->anim_index];
        if (anim->column_count > 0 && ent->anim_frame < anim->column_count)
            col = anim->columns[ent->anim_frame];
    }

    /* Clamp column */
    if (col < 0 || col >= sheet->total_columns) col = 0;

    /* Build frame array: one per angle */
    for (int a = 0; a < sheet->angles; a++) {
        out_frames[a] = (rt_frame){
            .pixels = sheet->pixels[a * sheet->total_columns + col],
            .width = sheet->frame_width,
            .height = sheet->frame_height
        };
    }
    return sheet;
}
```

- [ ] **Step 9: Update `bf_render` to use helper**

Replace the entity loop in `bf_render` (the "Entities as sprites" section):

```c
    /* Entities as sprites */
    for (int i = 0; i < e->entity_count; i++) {
        bf_entity *ent = &e->entities[i];
        if (!ent->active) continue;

        rt_frame frames[32];  /* max 32 angles */
        slice_sheet *sheet = build_sprite_frames(e, ent, frames);
        if (!sheet) continue;

        rt_scene_add_sprite(e->scene, (rt_sprite){
            .position = ent->position,
            .direction = ent->direction,
            .width = e->sprites[ent->sprite_id].width,
            .height = e->sprites[ent->sprite_id].height,
            .frame_count = sheet->angles,
            .frames = frames
        });
    }
```

- [ ] **Step 10: Update `bf_pick` to use helper**

Replace the entity loop in `bf_pick`:

```c
    for (int i = 0; i < e->entity_count; i++) {
        bf_entity *ent = &e->entities[i];
        if (!ent->active) continue;

        rt_frame frames[32];
        slice_sheet *sheet = build_sprite_frames(e, ent, frames);
        if (!sheet) continue;

        rt_sprite spr = {
            .position = ent->position,
            .direction = ent->direction,
            .width = e->sprites[ent->sprite_id].width,
            .height = e->sprites[ent->sprite_id].height,
            .frame_count = sheet->angles,
            .frames = frames
        };

        vector hp;
        float t = rt_pick_sprite(origin, ray_dir, &spr, origin, &hp);
        if (t > 0.0f && t < closest_t) {
            closest_t = t;
            closest_id = ent->id;
            closest_pos = hp;
        }
    }
```

- [ ] **Step 11: Verify library compiles**

Run: `make -C libs/battleforge`
Expected: Clean compile (shell may still fail).

- [ ] **Step 12: Commit**

```bash
git add libs/battleforge/battleforge.c
git commit -m "feat(battleforge): add animation playback, slice_sheet storage, and frame helper"
```

---

## Chunk 3: Shell Integration

### Task 6: Update shell to use new API

**Files:**
- Modify: `apps/battleforge/main.c`

The procedural smiley frames must be wrapped in a `slice_sheet` struct so the shell can call the new `bf_register_sprite` signature. No PNG loading yet — just a programmatic sheet.

- [ ] **Step 1: Add slice.h include**

At the top of `main.c`, add:

```c
#include "slice.h"
```

- [ ] **Step 2: Replace sprite registration**

Replace the current sprite registration block (the `rt_frame frames[16]` loop and `bf_register_sprite` call, lines 268-279) with:

```c
    /* Build a programmatic slice_sheet from procedural frame data.
       Static storage: engine borrows these pointers for its lifetime. */
    init_sprite_frames();

    static uint32_t *pixel_ptrs[16];
    for (int i = 0; i < 16; i++)
        pixel_ptrs[i] = frame_data[i];

    static int idle_col = 0;
    static slice_anim smiley_anim = {
        .name = "idle",
        .columns = NULL,  /* set below */
        .column_count = 1,
        .loop = 1
    };
    smiley_anim.columns = &idle_col;

    static slice_sheet smiley_sheet;
    smiley_sheet = (slice_sheet){
        .pixels = pixel_ptrs,
        .angles = 16,
        .total_columns = 1,
        .frame_width = S,
        .frame_height = S,
        .fps = 1.0f,
        .anims = &smiley_anim,
        .anim_count = 1
    };

    int spr_id = bf_register_sprite(engine, &smiley_sheet, 2.0f, 2.0f);
```

- [ ] **Step 3: Remove unused includes/types**

The shell no longer needs to reference `bf_sprite_def` or `rt_frame` for sprite registration. Remove any leftover references if they exist. The `rt_frame` type is still used by the engine internally but not by the shell.

- [ ] **Step 4: Verify full build**

Run: `autoreconf -i && ./configure && make`
Expected: Clean build, battleforge binary produced.

- [ ] **Step 5: Run and verify visually**

Run: `./apps/battleforge/battleforge`
Expected: Same smiley face sprites as before, camera controls work, mouse selection and movement work. Entities auto-face movement direction.

- [ ] **Step 6: Commit**

```bash
git add apps/battleforge/main.c
git commit -m "feat(battleforge): update shell to use slice_sheet API"
```

---

### Task 7: Final integration build

- [ ] **Step 1: Clean rebuild from scratch**

```bash
make clean && make
```

Expected: Clean build with no warnings.

- [ ] **Step 2: Run and verify**

Run: `./apps/battleforge/battleforge`
Expected: Full demo works — smiley sprites render, camera moves, entities selectable, move commands work with auto-facing.

- [ ] **Step 3: Final commit if any fixups were needed**

If any fixes were applied during verification, commit them.
