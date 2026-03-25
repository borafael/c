# ECS Component Refactor Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Refactor `libs/battleforge` from a monolithic `bf_entity` struct into a component-based ECS with struct-of-arrays, bitmask tagging, and data-driven unit/map definitions loaded from INI files.

**Architecture:** Break `bf_entity` into four component arrays (Position, Visual, Locomotion, Selection) with bitmask per entity, free list for ID management. Add `libs/ini` as a general-purpose INI parser. Refactor `libs/slice` to use it. Load unit definitions and map definitions from INI files in the client.

**Tech Stack:** C, GNU Autotools, Check (unit testing framework), libs/raytrace, libs/thread, libs/slice, SDL2 (client only)

**Spec:** `docs/superpowers/specs/2026-03-24-ecs-component-refactor-design.md`

---

## File Structure

### New files
```
libs/ini/ini.h                    — public API (parser interface)
libs/ini/ini.c                    — implementation (load, query, iterate, free)
libs/ini/Makefile.am              — build config
libs/ini/tests/check_ini.c        — unit tests
apps/barrier/units/rifleman.ini   — example unit definition
apps/barrier/maps/battlefield.ini — example map definition
```

### Modified files
```
libs/battleforge/battleforge.h    — new component structs, updated commands, unit defs
libs/battleforge/battleforge.c    — ECS arrays, systems, free list, updated handlers
libs/slice/slice.c                — refactor parse_ini to use libs/ini
libs/slice/Makefile.am            — add libs/ini dependency
libs/battleforge/Makefile.am      — add libs/ini dependency
apps/barrier/main.c               — load INI files, use new commands
apps/barrier/console.c            — update commands for new API
apps/barrier/console.h            — update help text
apps/barrier/Makefile.am          — add libs/ini dependency
configure.ac                      — add libs/ini/Makefile
Makefile.am                       — add libs/ini to SUBDIRS
```

---

### Task 1: Create libs/ini — INI parser library

**Files:**
- Create: `libs/ini/ini.h`
- Create: `libs/ini/ini.c`
- Create: `libs/ini/Makefile.am`
- Create: `libs/ini/tests/check_ini.c`
- Modify: `configure.ac`
- Modify: `Makefile.am`

- [ ] **Step 1: Write the test file**

Create `libs/ini/tests/check_ini.c` using the Check framework (same pattern as `libs/math/tests/check_vector.c`):

```c
#include <stdlib.h>
#include <check.h>
#include "ini.h"

START_TEST(test_load_nonexistent)
{
    ini_file *ini = ini_load("nonexistent.ini");
    ck_assert_ptr_null(ini);
}
END_TEST

START_TEST(test_load_and_get)
{
    ini_file *ini = ini_load("libs/ini/tests/test.ini");
    ck_assert_ptr_nonnull(ini);

    const char *val = ini_get(ini, "section", "key");
    ck_assert_ptr_nonnull(val);
    ck_assert_str_eq(val, "value");

    ini_free(ini);
}
END_TEST

START_TEST(test_get_int)
{
    ini_file *ini = ini_load("libs/ini/tests/test.ini");
    ck_assert_ptr_nonnull(ini);

    int val = ini_get_int(ini, "section", "number", -1);
    ck_assert_int_eq(val, 42);

    int missing = ini_get_int(ini, "section", "missing", -1);
    ck_assert_int_eq(missing, -1);

    ini_free(ini);
}
END_TEST

START_TEST(test_get_float)
{
    ini_file *ini = ini_load("libs/ini/tests/test.ini");
    ck_assert_ptr_nonnull(ini);

    float val = ini_get_float(ini, "section", "decimal", -1.0f);
    ck_assert_float_eq_tol(val, 3.14f, 0.001f);

    ini_free(ini);
}
END_TEST

START_TEST(test_get_bool)
{
    ini_file *ini = ini_load("libs/ini/tests/test.ini");
    ck_assert_ptr_nonnull(ini);

    int t = ini_get_bool(ini, "section", "enabled", 0);
    ck_assert_int_eq(t, 1);

    int f = ini_get_bool(ini, "section", "disabled", 1);
    ck_assert_int_eq(f, 0);

    ini_free(ini);
}
END_TEST

START_TEST(test_dot_separated_sections)
{
    ini_file *ini = ini_load("libs/ini/tests/test.ini");
    ck_assert_ptr_nonnull(ini);

    const char *val = ini_get(ini, "visual.animation.idle", "columns");
    ck_assert_ptr_nonnull(val);
    ck_assert_str_eq(val, "0");

    const char *loop = ini_get(ini, "visual.animation.idle", "loop");
    ck_assert_ptr_nonnull(val);
    ck_assert_str_eq(loop, "true");

    ini_free(ini);
}
END_TEST

START_TEST(test_empty_section)
{
    ini_file *ini = ini_load("libs/ini/tests/test.ini");
    ck_assert_ptr_nonnull(ini);

    /* Empty section exists but has no keys */
    int count = ini_key_count(ini, "selection");
    ck_assert_int_eq(count, 0);

    /* Section should still be discoverable */
    int found = 0;
    int sections = ini_section_count(ini);
    for (int i = 0; i < sections; i++) {
        if (strcmp(ini_section_name(ini, i), "selection") == 0) {
            found = 1;
            break;
        }
    }
    ck_assert_int_eq(found, 1);

    ini_free(ini);
}
END_TEST

START_TEST(test_comments_ignored)
{
    ini_file *ini = ini_load("libs/ini/tests/test.ini");
    ck_assert_ptr_nonnull(ini);

    const char *val = ini_get(ini, "section", "commented");
    ck_assert_ptr_null(val);

    ini_free(ini);
}
END_TEST

START_TEST(test_section_iteration)
{
    ini_file *ini = ini_load("libs/ini/tests/test.ini");
    ck_assert_ptr_nonnull(ini);

    int sections = ini_section_count(ini);
    ck_assert_int_ge(sections, 3);

    ini_free(ini);
}
END_TEST

START_TEST(test_key_iteration)
{
    ini_file *ini = ini_load("libs/ini/tests/test.ini");
    ck_assert_ptr_nonnull(ini);

    int keys = ini_key_count(ini, "section");
    ck_assert_int_ge(keys, 4);

    ini_free(ini);
}
END_TEST

START_TEST(test_global_keys)
{
    ini_file *ini = ini_load("libs/ini/tests/test.ini");
    ck_assert_ptr_nonnull(ini);

    /* Keys before any section header use empty string as section */
    const char *val = ini_get(ini, "", "global_key");
    ck_assert_ptr_nonnull(val);
    ck_assert_str_eq(val, "global_value");

    ini_free(ini);
}
END_TEST

Suite *ini_suite(void)
{
    Suite *s = suite_create("ini");
    TCase *tc = tcase_create("core");
    tcase_add_test(tc, test_load_nonexistent);
    tcase_add_test(tc, test_load_and_get);
    tcase_add_test(tc, test_get_int);
    tcase_add_test(tc, test_get_float);
    tcase_add_test(tc, test_get_bool);
    tcase_add_test(tc, test_dot_separated_sections);
    tcase_add_test(tc, test_empty_section);
    tcase_add_test(tc, test_comments_ignored);
    tcase_add_test(tc, test_section_iteration);
    tcase_add_test(tc, test_key_iteration);
    tcase_add_test(tc, test_global_keys);
    suite_add_tcase(s, tc);
    return s;
}

int main(void)
{
    Suite *s = ini_suite();
    SRunner *sr = srunner_create(s);
    srunner_run_all(sr, CK_NORMAL);
    int failed = srunner_ntests_failed(sr);
    srunner_free(sr);
    return (failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
```

- [ ] **Step 2: Create the test INI fixture file**

Create `libs/ini/tests/test.ini`:

```ini
global_key=global_value

[section]
key=value
number=42
decimal=3.14
enabled=true
disabled=false
# commented=hidden

[visual.animation.idle]
columns=0
loop=true

[visual.animation.walk]
columns=0,1,2,3
loop=true

[selection]

[locomotion]
speed=3.0
```

- [ ] **Step 3: Write the header file**

Create `libs/ini/ini.h`:

```c
#ifndef INI_H
#define INI_H

typedef struct ini_file ini_file;

ini_file   *ini_load(const char *path);
void        ini_free(ini_file *ini);

const char *ini_get(const ini_file *ini, const char *section, const char *key);
int         ini_get_int(const ini_file *ini, const char *section, const char *key, int fallback);
float       ini_get_float(const ini_file *ini, const char *section, const char *key, float fallback);
int         ini_get_bool(const ini_file *ini, const char *section, const char *key, int fallback);

int         ini_section_count(const ini_file *ini);
const char *ini_section_name(const ini_file *ini, int index);
int         ini_key_count(const ini_file *ini, const char *section);
const char *ini_key_name(const ini_file *ini, const char *section, int index);

#endif /* INI_H */
```

- [ ] **Step 4: Write the implementation**

Create `libs/ini/ini.c`. Internal data structure:

- Array of sections, each section has a name (char[256]) and an array of key-value pairs (both char[256])
- `ini_load` reads line by line: tracks current section (starts as `""`), parses `[section]` headers, `key=value` pairs, skips `#` and `;` comments and blank lines
- Strip leading/trailing whitespace from keys and values
- All queries do linear scan — fine for small files

```c
#include "ini.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define MAX_SECTIONS 128
#define MAX_KEYS_PER_SECTION 64
#define MAX_NAME 256
#define MAX_LINE 1024

typedef struct {
    char key[MAX_NAME];
    char value[MAX_NAME];
} ini_pair;

typedef struct {
    char name[MAX_NAME];
    ini_pair pairs[MAX_KEYS_PER_SECTION];
    int pair_count;
} ini_section;

struct ini_file {
    ini_section sections[MAX_SECTIONS];
    int section_count;
};

static char *strip(char *s) {
    while (*s && isspace((unsigned char)*s)) s++;
    char *end = s + strlen(s) - 1;
    while (end > s && isspace((unsigned char)*end)) *end-- = '\0';
    return s;
}

static ini_section *find_section(const ini_file *ini, const char *name) {
    for (int i = 0; i < ini->section_count; i++) {
        if (strcmp(ini->sections[i].name, name) == 0)
            return (ini_section *)&ini->sections[i];
    }
    return NULL;
}

static ini_section *ensure_section(ini_file *ini, const char *name) {
    ini_section *s = find_section(ini, name);
    if (s) return s;
    if (ini->section_count >= MAX_SECTIONS) return NULL;
    s = &ini->sections[ini->section_count++];
    strncpy(s->name, name, MAX_NAME - 1);
    s->name[MAX_NAME - 1] = '\0';
    s->pair_count = 0;
    return s;
}

ini_file *ini_load(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) return NULL;

    ini_file *ini = calloc(1, sizeof(ini_file));
    if (!ini) { fclose(f); return NULL; }

    /* Start with global section (empty name) */
    ensure_section(ini, "");
    char current_section[MAX_NAME] = "";

    char line[MAX_LINE];
    while (fgets(line, sizeof(line), f)) {
        /* Strip newline */
        char *nl = strchr(line, '\n');
        if (nl) *nl = '\0';
        char *cr = strchr(line, '\r');
        if (cr) *cr = '\0';

        char *trimmed = strip(line);

        /* Skip empty lines and comments */
        if (*trimmed == '\0' || *trimmed == '#' || *trimmed == ';')
            continue;

        /* Section header */
        if (*trimmed == '[') {
            char *end = strchr(trimmed, ']');
            if (!end) continue;
            *end = '\0';
            char *name = strip(trimmed + 1);
            strncpy(current_section, name, MAX_NAME - 1);
            current_section[MAX_NAME - 1] = '\0';
            ensure_section(ini, current_section);
            continue;
        }

        /* Key=value pair */
        char *eq = strchr(trimmed, '=');
        if (!eq) continue;
        *eq = '\0';
        char *key = strip(trimmed);
        char *val = strip(eq + 1);

        ini_section *sec = ensure_section(ini, current_section);
        if (!sec || sec->pair_count >= MAX_KEYS_PER_SECTION) continue;

        ini_pair *p = &sec->pairs[sec->pair_count++];
        strncpy(p->key, key, MAX_NAME - 1);
        p->key[MAX_NAME - 1] = '\0';
        strncpy(p->value, val, MAX_NAME - 1);
        p->value[MAX_NAME - 1] = '\0';
    }

    fclose(f);
    return ini;
}

void ini_free(ini_file *ini) {
    free(ini);
}

const char *ini_get(const ini_file *ini, const char *section, const char *key) {
    if (!ini) return NULL;
    ini_section *s = find_section(ini, section);
    if (!s) return NULL;
    for (int i = 0; i < s->pair_count; i++) {
        if (strcmp(s->pairs[i].key, key) == 0)
            return s->pairs[i].value;
    }
    return NULL;
}

int ini_get_int(const ini_file *ini, const char *section, const char *key, int fallback) {
    const char *val = ini_get(ini, section, key);
    if (!val) return fallback;
    return atoi(val);
}

float ini_get_float(const ini_file *ini, const char *section, const char *key, float fallback) {
    const char *val = ini_get(ini, section, key);
    if (!val) return fallback;
    return (float)atof(val);
}

int ini_get_bool(const ini_file *ini, const char *section, const char *key, int fallback) {
    const char *val = ini_get(ini, section, key);
    if (!val) return fallback;
    if (strcmp(val, "true") == 0 || strcmp(val, "1") == 0 || strcmp(val, "yes") == 0)
        return 1;
    if (strcmp(val, "false") == 0 || strcmp(val, "0") == 0 || strcmp(val, "no") == 0)
        return 0;
    return fallback;
}

int ini_section_count(const ini_file *ini) {
    if (!ini) return 0;
    return ini->section_count;
}

const char *ini_section_name(const ini_file *ini, int index) {
    if (!ini || index < 0 || index >= ini->section_count) return NULL;
    return ini->sections[index].name;
}

int ini_key_count(const ini_file *ini, const char *section) {
    if (!ini) return 0;
    ini_section *s = find_section(ini, section);
    if (!s) return 0;
    return s->pair_count;
}

const char *ini_key_name(const ini_file *ini, const char *section, int index) {
    if (!ini) return NULL;
    ini_section *s = find_section(ini, section);
    if (!s || index < 0 || index >= s->pair_count) return NULL;
    return s->pairs[index].key;
}
```

- [ ] **Step 5: Create the Makefile.am**

Create `libs/ini/Makefile.am`:

```makefile
noinst_LTLIBRARIES = libini.la
libini_la_SOURCES = ini.c ini.h

check_PROGRAMS = tests/check_ini
tests_check_ini_SOURCES = tests/check_ini.c
tests_check_ini_CFLAGS = $(CHECK_CFLAGS) -I$(srcdir)
tests_check_ini_LDADD = libini.la $(CHECK_LIBS)
TESTS = tests/check_ini

EXTRA_DIST = tests/test.ini
```

- [ ] **Step 6: Wire into the build system**

Modify `Makefile.am` — add `libs/ini` before `libs/slice` in SUBDIRS:

```makefile
SUBDIRS = libs/math libs/thread libs/raytrace libs/ini libs/slice libs/battleforge apps/nbody apps/rtdemo apps/barrier
```

Modify `configure.ac` — add `libs/ini/Makefile` to AC_CONFIG_FILES:

```
AC_CONFIG_FILES([
    Makefile
    libs/math/Makefile
    libs/thread/Makefile
    libs/raytrace/Makefile
    libs/ini/Makefile
    libs/slice/Makefile
    libs/battleforge/Makefile
    apps/nbody/Makefile
    apps/rtdemo/Makefile
    apps/barrier/Makefile
])
```

- [ ] **Step 7: Build and run tests**

Run:
```bash
autoreconf -i && ./configure && make check
```
Expected: All `check_ini` tests pass. Existing `check_vector` tests still pass.

- [ ] **Step 8: Commit**

```bash
git add libs/ini/ Makefile.am configure.ac
git commit -m "feat(ini): add general-purpose INI parser library with tests"
```

---

### Task 2: Refactor libs/slice to use libs/ini

**Files:**
- Modify: `libs/slice/slice.c`
- Modify: `libs/slice/Makefile.am`

- [ ] **Step 1: Update slice Makefile.am to depend on libs/ini**

```makefile
noinst_LTLIBRARIES = libslice.la
libslice_la_SOURCES = slice.c slice.h
libslice_la_CPPFLAGS = -I$(top_srcdir)/libs/ini
libslice_la_LIBADD = $(top_builddir)/libs/ini/libini.la
EXTRA_DIST = stb_image.h
```

- [ ] **Step 2: Refactor parse_ini in slice.c**

Replace the `parse_ini` static function and `parse_int_list` helper with a version that uses `libs/ini`. The function should:

1. Add `#include "ini.h"` at the top
2. Remove the `parse_int_list` static function (move it or keep it — it's still useful for parsing comma-separated column lists from `ini_get` return values)
3. Replace `parse_ini` body:
   - Call `ini_load(ini_path)` instead of manual file reading
   - Read global keys (`""` section): `frame_width`, `frame_height`, `angles`, `fps` via `ini_get_int` / `ini_get_float`
   - Iterate sections to find animation definitions (any section that is not the global section)
   - For each animation section: read `frames` via `ini_get`, parse with `parse_int_list`; read `loop` via `ini_get_bool`
   - Call `ini_free` when done

Key: the existing INI format uses keys *before* any section header (global scope) for `frame_width`, `frame_height`, `angles`, `fps`. In `libs/ini`, these go in the `""` (empty string) section.

```c
static int parse_ini(const char *ini_path, slice_sheet *sheet) {
    ini_file *ini = ini_load(ini_path);
    if (!ini) {
        fprintf(stderr, "slice: cannot open INI '%s'\n", ini_path);
        return -1;
    }

    /* Global keys (before any section) */
    sheet->frame_width  = ini_get_int(ini, "", "frame_width", 0);
    sheet->frame_height = ini_get_int(ini, "", "frame_height", 0);
    sheet->angles       = ini_get_int(ini, "", "angles", 0);
    sheet->fps          = ini_get_float(ini, "", "fps", 0.0f);

    /* Count animation sections (any non-empty section name) */
    int sec_count = ini_section_count(ini);
    slice_anim anims[MAX_ANIMS];
    int anim_count = 0;

    for (int i = 0; i < sec_count && anim_count < MAX_ANIMS; i++) {
        const char *name = ini_section_name(ini, i);
        if (!name || name[0] == '\0') continue; /* skip global section */

        memset(&anims[anim_count], 0, sizeof(slice_anim));
        strncpy(anims[anim_count].name, name, 31);
        anims[anim_count].name[31] = '\0';
        anims[anim_count].loop = ini_get_bool(ini, name, "loop", 1);

        const char *frames_str = ini_get(ini, name, "frames");
        if (frames_str) {
            int tmp[256];
            int n = parse_int_list(frames_str, tmp, 256);
            anims[anim_count].columns = malloc(sizeof(int) * n);
            if (anims[anim_count].columns) {
                memcpy(anims[anim_count].columns, tmp, sizeof(int) * n);
                anims[anim_count].column_count = n;
            }
        }
        anim_count++;
    }

    ini_free(ini);

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

- [ ] **Step 3: Build and verify**

Run:
```bash
make clean && make
```
Expected: Builds successfully. No changes to slice's public API — `slice_load` and `slice_free` work exactly as before.

- [ ] **Step 4: Run the barrier app to verify sprites still load**

Run:
```bash
./apps/barrier/barrier
```
Expected: All unit sprites load correctly (check stderr output for "Loaded 20/20 unit sprites"). Visual appearance unchanged.

- [ ] **Step 5: Commit**

```bash
git add libs/slice/slice.c libs/slice/Makefile.am
git commit -m "refactor(slice): use libs/ini instead of hand-rolled INI parser"
```

---

### Task 3: Refactor battleforge.h — ECS component types and updated commands

**Files:**
- Modify: `libs/battleforge/battleforge.h`

- [ ] **Step 1: Add component bitmask enum and component structs**

Add after the `bf_config` struct:

```c
/* --- Components --- */

typedef enum {
    BF_COMP_NONE       = 0,
    BF_COMP_POSITION   = (1 << 0),
    BF_COMP_VISUAL     = (1 << 1),
    BF_COMP_LOCOMOTION = (1 << 2),
    BF_COMP_SELECTION  = (1 << 3),
} bf_component;

typedef struct {
    vector position;
    vector direction;
} bf_position;

typedef struct {
    int sprite_id;
    int anim_index;
    int anim_frame;
    float frame_timer;
    float anim_fps;
} bf_visual;

typedef enum {
    BF_LOCO_LINEAR,
    BF_LOCO_PARABOLIC,
    BF_LOCO_INSTANT,
} bf_loco_type;

typedef struct {
    vector origin;
    vector target;
    float speed;
    float progress;
} bf_trajectory_linear;

typedef struct {
    vector origin;
    vector target;
    float speed;
    float progress;
    float arc_height;
} bf_trajectory_parabolic;

typedef struct {
    bf_loco_type type;
    union {
        bf_trajectory_linear linear;
        bf_trajectory_parabolic parabolic;
    };
} bf_locomotion;

typedef struct {
    int selected;
} bf_selection;
```

- [ ] **Step 2: Add unit definition struct**

Add after the component structs:

```c
/* --- Unit Definitions --- */

#define BF_UNIT_NAME_SIZE 32
#define MAX_UNIT_DEFS 256

typedef struct {
    char name[BF_UNIT_NAME_SIZE];
    int sprite_id;
    float base_speed;
    int has_selection;   /* whether to grant BF_COMP_SELECTION at spawn */
} bf_unit_def;
```

- [ ] **Step 3: Update command enum and struct**

Replace the existing `bf_cmd_type` and `bf_cmd`:

```c
typedef enum {
    BF_CMD_CAMERA_SET,
    BF_CMD_CAMERA_MOVE,
    BF_CMD_REGISTER_UNIT,
    BF_CMD_ENTITY_CREATE,
    BF_CMD_ENTITY_DESTROY,
    BF_CMD_ENTITY_MOVE,
    BF_CMD_ENTITY_FACE,
    BF_CMD_SELECT,
    BF_CMD_ENTITY_ANIMATE,
    BF_CMD_COUNT
} bf_cmd_type;

typedef struct {
    bf_cmd_type type;
    union {
        struct { vector position; vector direction; } camera_set;
        struct { vector delta; } camera_move;
        struct { bf_unit_def def; } register_unit;
        struct { int id; int unit_def_id; vector position; vector direction; } entity_create;
        struct { int id; } entity_destroy;
        struct { int id; vector target; float speed; bf_loco_type loco_type; } entity_move;
        struct { int id; vector direction; } entity_face;
        struct { int id; } select;
        struct { int id; int anim_index; } entity_animate;
    };
} bf_cmd;
```

This removes `BF_CMD_ENTITY_SET_SPEED` and changes `entity_create` and `entity_move` payloads.

- [ ] **Step 4: Commit**

```bash
git add libs/battleforge/battleforge.h
git commit -m "refactor(battleforge): add ECS component types and updated command API"
```

Note: This will **break** the build. Tasks 4 and 5 fix it.

---

### Task 4: Refactor battleforge.c — ECS internals and systems

**Files:**
- Modify: `libs/battleforge/battleforge.c`

- [ ] **Step 1: Replace bf_entity with component arrays**

Remove the `bf_entity` typedef. Replace entity storage in `struct bf_engine` with:

```c
struct bf_engine {
    bf_config config;

    /* Camera */
    struct {
        vector position;
        vector direction;
    } camera;

    /* Map */
    bf_map map;
    int map_set;

    /* Sprites */
    struct {
        slice_sheet *sheet;
        float width;
        float height;
    } sprites[MAX_SPRITES];
    int sprite_count;

    /* ECS — component arrays */
    uint32_t       component_masks[MAX_ENTITIES];
    bf_position    positions[MAX_ENTITIES];
    bf_visual      visuals[MAX_ENTITIES];
    bf_locomotion  locomotions[MAX_ENTITIES];
    bf_selection   selections[MAX_ENTITIES];

    /* Entity ID free list */
    int free_stack[MAX_ENTITIES];
    int free_top;
    int selected_entity_id;

    /* Unit definitions */
    bf_unit_def unit_defs[MAX_UNIT_DEFS];
    int unit_def_count;

    /* Command queue (unchanged) */
    bf_cmd cmd_queue[CMD_QUEUE_SIZE];
    int cmd_head;
    int cmd_tail;
    int cmd_count;

    /* Log ring buffer (unchanged) */
    bf_log_entry log_buffer[BF_LOG_BUFFER_SIZE];
    int log_write_pos;
    int log_count;

    /* Render resources (unchanged) */
    rt_scene *scene;
    rt_camera *rt_cam;
    rt_viewport viewport;
    thread_pool *pool;
    int num_threads;
    render_task *tasks;
};
```

- [ ] **Step 2: Initialize free list in bf_create**

In `bf_create`, after existing initialization, add:

```c
/* Initialize entity free list */
e->free_top = -1;
for (int i = MAX_ENTITIES - 1; i >= 0; i--)
    e->free_stack[++e->free_top] = i;
e->selected_entity_id = -1;
```

- [ ] **Step 3: Add entity alloc/dealloc helpers**

```c
static int alloc_entity(bf_engine *e) {
    if (e->free_top < 0) return -1;
    return e->free_stack[e->free_top--];
}

static void free_entity(bf_engine *e, int id) {
    e->component_masks[id] = BF_COMP_NONE;
    e->free_stack[++e->free_top] = id;
}
```

- [ ] **Step 4: Replace find_entity with component mask check**

Remove `find_entity`. Command handlers that need to verify an entity exists check:

```c
if (!(e->component_masks[id] & BF_COMP_POSITION)) return; /* entity doesn't exist */
```

- [ ] **Step 5: Update command handlers**

Replace `cmd_entity_create`:

```c
static void cmd_register_unit(bf_engine *e, const bf_cmd *cmd) {
    if (e->unit_def_count >= MAX_UNIT_DEFS) {
        bf_log(e, BF_LOG_ERROR, "cannot register unit: max defs reached");
        return;
    }
    e->unit_defs[e->unit_def_count++] = cmd->register_unit.def;
    bf_log(e, BF_LOG_INFO, "registered unit def '%s' (id=%d)",
           cmd->register_unit.def.name, e->unit_def_count - 1);
}

static void cmd_entity_create(bf_engine *e, const bf_cmd *cmd) {
    int def_id = cmd->entity_create.unit_def_id;
    if (def_id < 0 || def_id >= e->unit_def_count) {
        bf_log(e, BF_LOG_ERROR, "invalid unit_def_id %d", def_id);
        return;
    }

    int id = alloc_entity(e);
    if (id < 0) {
        bf_log(e, BF_LOG_ERROR, "cannot create entity: max entities reached");
        return;
    }

    bf_unit_def *def = &e->unit_defs[def_id];
    uint32_t mask = BF_COMP_POSITION;

    /* Position */
    e->positions[id] = (bf_position){
        .position = cmd->entity_create.position,
        .direction = cmd->entity_create.direction
    };

    /* Visual */
    if (def->sprite_id >= 0) {
        mask |= BF_COMP_VISUAL;
        e->visuals[id] = (bf_visual){
            .sprite_id = def->sprite_id,
            .anim_index = -1,
            .anim_frame = 0,
            .frame_timer = 0.0f,
            .anim_fps = 0.0f
        };
    }

    /* Selection */
    if (def->has_selection) {
        mask |= BF_COMP_SELECTION;
        e->selections[id] = (bf_selection){ .selected = 0 };
    }

    /* Snap to terrain */
    if (e->map_set && e->map.heights) {
        e->positions[id].position.y = bf_map_height_at(&e->map,
            e->positions[id].position.x, e->positions[id].position.z);
    }

    e->component_masks[id] = mask;
    bf_log(e, BF_LOG_INFO, "entity %d created (%s) at (%.1f, %.1f, %.1f)",
           id, def->name, e->positions[id].position.x,
           e->positions[id].position.y, e->positions[id].position.z);
}
```

Update `cmd_entity_destroy`:

```c
static void cmd_entity_destroy(bf_engine *e, const bf_cmd *cmd) {
    int id = cmd->entity_destroy.id;
    if (id < 0 || id >= MAX_ENTITIES) return;
    if (!(e->component_masks[id] & BF_COMP_POSITION)) return;
    if (e->selected_entity_id == id)
        e->selected_entity_id = -1;
    free_entity(e, id);
    bf_log(e, BF_LOG_INFO, "entity %d destroyed", id);
}
```

Update `cmd_entity_move` to create a Locomotion component:

```c
static void cmd_entity_move(bf_engine *e, const bf_cmd *cmd) {
    int id = cmd->entity_move.id;
    if (id < 0 || id >= MAX_ENTITIES) return;
    if (!(e->component_masks[id] & BF_COMP_POSITION)) return;

    e->component_masks[id] |= BF_COMP_LOCOMOTION;
    e->locomotions[id] = (bf_locomotion){
        .type = cmd->entity_move.loco_type
    };

    switch (cmd->entity_move.loco_type) {
    case BF_LOCO_LINEAR:
        e->locomotions[id].linear = (bf_trajectory_linear){
            .origin = e->positions[id].position,
            .target = cmd->entity_move.target,
            .speed = cmd->entity_move.speed,
            .progress = 0.0f
        };
        break;
    case BF_LOCO_PARABOLIC:
        e->locomotions[id].parabolic = (bf_trajectory_parabolic){
            .origin = e->positions[id].position,
            .target = cmd->entity_move.target,
            .speed = cmd->entity_move.speed,
            .progress = 0.0f,
            .arc_height = 5.0f  /* default arc height */
        };
        break;
    case BF_LOCO_INSTANT:
        e->positions[id].position = cmd->entity_move.target;
        if (e->map_set && e->map.heights) {
            e->positions[id].position.y = bf_map_height_at(&e->map,
                e->positions[id].position.x, e->positions[id].position.z);
        }
        /* No locomotion component needed — already there */
        e->component_masks[id] &= ~BF_COMP_LOCOMOTION;
        break;
    }

    bf_log(e, BF_LOG_INFO, "entity %d moving to (%.1f, %.1f, %.1f)",
           id, cmd->entity_move.target.x,
           cmd->entity_move.target.y, cmd->entity_move.target.z);
}
```

Update `cmd_entity_face`:

```c
static void cmd_entity_face(bf_engine *e, const bf_cmd *cmd) {
    int id = cmd->entity_face.id;
    if (id < 0 || id >= MAX_ENTITIES) return;
    if (!(e->component_masks[id] & BF_COMP_POSITION)) return;
    e->positions[id].direction = cmd->entity_face.direction;
}
```

Update `cmd_select`:

```c
static void cmd_select(bf_engine *e, const bf_cmd *cmd) {
    int id = cmd->select.id;
    if (id <= 0) {
        /* Deselect current */
        if (e->selected_entity_id >= 0 && e->selected_entity_id < MAX_ENTITIES &&
            (e->component_masks[e->selected_entity_id] & BF_COMP_SELECTION)) {
            e->selections[e->selected_entity_id].selected = 0;
        }
        e->selected_entity_id = -1;
        bf_log(e, BF_LOG_INFO, "deselected");
        return;
    }
    if (id >= MAX_ENTITIES) return;
    if (!(e->component_masks[id] & BF_COMP_SELECTION)) return;

    /* Deselect previous */
    if (e->selected_entity_id >= 0 && e->selected_entity_id < MAX_ENTITIES &&
        (e->component_masks[e->selected_entity_id] & BF_COMP_SELECTION)) {
        e->selections[e->selected_entity_id].selected = 0;
    }

    e->selections[id].selected = 1;
    e->selected_entity_id = id;
    bf_log(e, BF_LOG_INFO, "selected entity %d", id);
}
```

Update `cmd_entity_animate`:

```c
static void cmd_entity_animate(bf_engine *e, const bf_cmd *cmd) {
    int id = cmd->entity_animate.id;
    if (id < 0 || id >= MAX_ENTITIES) return;
    if (!(e->component_masks[id] & BF_COMP_VISUAL)) return;

    bf_visual *vis = &e->visuals[id];
    if (vis->sprite_id < 0 || vis->sprite_id >= e->sprite_count) return;
    slice_sheet *sheet = e->sprites[vis->sprite_id].sheet;
    if (!sheet) return;

    int ai = cmd->entity_animate.anim_index;
    if (ai < 0 || ai >= sheet->anim_count) return;
    vis->anim_index = ai;
    vis->anim_frame = 0;
    vis->frame_timer = 0.0f;
    vis->anim_fps = sheet->fps;
}
```

- [ ] **Step 6: Update the dispatch table**

Remove `cmd_entity_set_speed` handler. Add `cmd_register_unit`. Update table:

```c
static void (*cmd_handlers[BF_CMD_COUNT])(bf_engine *, const bf_cmd *) = {
    [BF_CMD_CAMERA_SET]      = cmd_camera_set,
    [BF_CMD_CAMERA_MOVE]     = cmd_camera_move,
    [BF_CMD_REGISTER_UNIT]   = cmd_register_unit,
    [BF_CMD_ENTITY_CREATE]   = cmd_entity_create,
    [BF_CMD_ENTITY_DESTROY]  = cmd_entity_destroy,
    [BF_CMD_ENTITY_MOVE]     = cmd_entity_move,
    [BF_CMD_ENTITY_FACE]     = cmd_entity_face,
    [BF_CMD_SELECT]          = cmd_select,
    [BF_CMD_ENTITY_ANIMATE]  = cmd_entity_animate,
};
```

- [ ] **Step 7: Extract systems from bf_tick**

Add locomotion advance functions with dispatch table:

```c
static void advance_linear(bf_locomotion *loco, bf_position *pos,
                           const bf_map *map, int map_set, float dt) {
    vector to_target = vector_sub(loco->linear.target, pos->position);
    to_target.y = 0.0f;
    float dist = vector_magnitude(to_target);
    float step = loco->linear.speed * dt;

    if (dist <= step) {
        pos->position.x = loco->linear.target.x;
        pos->position.z = loco->linear.target.z;
        loco->linear.progress = 1.0f;
    } else {
        vector move_dir = vector_scale(to_target, 1.0f / dist);
        pos->direction = move_dir;
        pos->position.x += move_dir.x * step;
        pos->position.z += move_dir.z * step;
        loco->linear.progress = 1.0f - (dist - step) /
            vector_magnitude(vector_sub(loco->linear.target, loco->linear.origin));
    }

    /* Snap to terrain */
    if (map_set && map->heights) {
        pos->position.y = bf_map_height_at(map, pos->position.x, pos->position.z);
    }
}

static void advance_parabolic(bf_locomotion *loco, bf_position *pos,
                              const bf_map *map, int map_set, float dt) {
    vector total = vector_sub(loco->parabolic.target, loco->parabolic.origin);
    total.y = 0.0f;
    float total_dist = vector_magnitude(total);
    if (total_dist < 0.001f) {
        loco->parabolic.progress = 1.0f;
        return;
    }

    float step = loco->parabolic.speed * dt / total_dist;
    loco->parabolic.progress += step;
    if (loco->parabolic.progress > 1.0f) loco->parabolic.progress = 1.0f;

    float t = loco->parabolic.progress;
    pos->position.x = loco->parabolic.origin.x + total.x * t;
    pos->position.z = loco->parabolic.origin.z + total.z * t;

    /* Parabolic arc: height = base_height + arc_height * 4t(1-t) */
    float base_y = 0.0f;
    if (map_set && map->heights)
        base_y = bf_map_height_at(map, pos->position.x, pos->position.z);
    pos->position.y = base_y + loco->parabolic.arc_height * 4.0f * t * (1.0f - t);

    /* Update direction */
    if (total_dist > 0.001f)
        pos->direction = vector_scale(total, 1.0f / total_dist);
}

static void advance_instant(bf_locomotion *loco, bf_position *pos,
                            const bf_map *map, int map_set, float dt) {
    (void)loco;
    (void)dt;
    (void)map;
    (void)map_set;
    /* Instant movement is handled in cmd_entity_move — this is a no-op */
}

static void (*loco_advance[])(bf_locomotion *, bf_position *,
                               const bf_map *, int, float) = {
    [BF_LOCO_LINEAR]    = advance_linear,
    [BF_LOCO_PARABOLIC] = advance_parabolic,
    [BF_LOCO_INSTANT]   = advance_instant,
};

static void system_locomotion(bf_engine *e, float dt) {
    uint32_t required = BF_COMP_POSITION | BF_COMP_LOCOMOTION;
    for (int i = 0; i < MAX_ENTITIES; i++) {
        if ((e->component_masks[i] & required) != required) continue;

        loco_advance[e->locomotions[i].type](
            &e->locomotions[i], &e->positions[i],
            &e->map, e->map_set, dt);

        /* Check if arrived */
        float progress = 0.0f;
        switch (e->locomotions[i].type) {
        case BF_LOCO_LINEAR:    progress = e->locomotions[i].linear.progress; break;
        case BF_LOCO_PARABOLIC: progress = e->locomotions[i].parabolic.progress; break;
        case BF_LOCO_INSTANT:   progress = 1.0f; break;
        }
        if (progress >= 1.0f) {
            e->component_masks[i] &= ~BF_COMP_LOCOMOTION;
        }
    }
}

static void system_animation(bf_engine *e, float dt) {
    for (int i = 0; i < MAX_ENTITIES; i++) {
        if (!(e->component_masks[i] & BF_COMP_VISUAL)) continue;

        bf_visual *vis = &e->visuals[i];
        if (vis->anim_index < 0) continue;
        if (vis->sprite_id < 0 || vis->sprite_id >= e->sprite_count) continue;

        slice_sheet *sheet = e->sprites[vis->sprite_id].sheet;
        if (!sheet || vis->anim_index >= sheet->anim_count) continue;
        slice_anim *anim = &sheet->anims[vis->anim_index];
        if (anim->column_count <= 1) continue;
        if (vis->anim_fps <= 0.0f) continue;

        vis->frame_timer += dt;
        float interval = 1.0f / vis->anim_fps;
        while (vis->frame_timer >= interval) {
            vis->frame_timer -= interval;
            vis->anim_frame++;
            if (vis->anim_frame >= anim->column_count) {
                if (anim->loop)
                    vis->anim_frame = 0;
                else
                    vis->anim_frame = anim->column_count - 1;
            }
        }
    }
}
```

Update `bf_tick`:

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

    /* Run systems */
    system_locomotion(e, dt);
    system_animation(e, dt);
}
```

- [ ] **Step 8: Update build_sprite_frames to use component arrays**

```c
static slice_sheet *build_sprite_frames(bf_engine *e, int entity_id,
                                        rt_frame *out_frames) {
    bf_visual *vis = &e->visuals[entity_id];
    if (vis->sprite_id < 0 || vis->sprite_id >= e->sprite_count)
        return NULL;
    slice_sheet *sheet = e->sprites[vis->sprite_id].sheet;
    if (!sheet) return NULL;

    int col = 0;
    if (vis->anim_index >= 0 && vis->anim_index < sheet->anim_count) {
        slice_anim *anim = &sheet->anims[vis->anim_index];
        if (anim->column_count > 0 && vis->anim_frame < anim->column_count)
            col = anim->columns[vis->anim_frame];
    }

    if (col < 0 || col >= sheet->total_columns) col = 0;

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

- [ ] **Step 9: Update bf_render to use component arrays**

Replace entity iteration in `bf_render`:

```c
/* Count active entities with visual components */
int vis_count = 0;
for (int i = 0; i < MAX_ENTITIES; i++) {
    if ((e->component_masks[i] & (BF_COMP_POSITION | BF_COMP_VISUAL))
        == (BF_COMP_POSITION | BF_COMP_VISUAL))
        vis_count++;
}

rt_frame (*all_frames)[MAX_ANGLES] = malloc(vis_count * sizeof(*all_frames));
int frame_idx = 0;

for (int i = 0; i < MAX_ENTITIES; i++) {
    if ((e->component_masks[i] & (BF_COMP_POSITION | BF_COMP_VISUAL))
        != (BF_COMP_POSITION | BF_COMP_VISUAL))
        continue;

    slice_sheet *sheet = build_sprite_frames(e, i, all_frames[frame_idx]);
    if (!sheet) continue;

    float spr_h = e->sprites[e->visuals[i].sprite_id].height;
    vector spr_pos = e->positions[i].position;
    spr_pos.y += spr_h * 0.5f;

    rt_scene_add_sprite(e->scene, (rt_sprite){
        .position = spr_pos,
        .direction = e->positions[i].direction,
        .width = e->sprites[e->visuals[i].sprite_id].width,
        .height = spr_h,
        .frame_count = sheet->angles,
        .frames = all_frames[frame_idx]
    });
    frame_idx++;
}
```

- [ ] **Step 10: Update bf_pick to use component arrays**

Replace entity iteration in `bf_pick`:

```c
for (int i = 0; i < MAX_ENTITIES; i++) {
    if ((e->component_masks[i] & (BF_COMP_POSITION | BF_COMP_VISUAL | BF_COMP_SELECTION))
        != (BF_COMP_POSITION | BF_COMP_VISUAL | BF_COMP_SELECTION))
        continue;

    rt_frame frames[32];
    slice_sheet *sheet = build_sprite_frames(e, i, frames);
    if (!sheet) continue;

    rt_sprite spr = {
        .position = e->positions[i].position,
        .direction = e->positions[i].direction,
        .width = e->sprites[e->visuals[i].sprite_id].width,
        .height = e->sprites[e->visuals[i].sprite_id].height,
        .frame_count = sheet->angles,
        .frames = frames
    };

    vector hp;
    float t = rt_pick_sprite(origin, ray_dir, &spr, origin, &hp);
    if (t > 0.0f && t < closest_t) {
        closest_t = t;
        closest_id = i;
        closest_pos = hp;
    }
}
```

Note: `bf_pick` now returns entity index (not caller-assigned id). Update `bf_pick_result.entity_id` usage accordingly.

- [ ] **Step 11: Initialize selected_entity_id to -1 in bf_create**

In `bf_create`, add:
```c
e->selected_entity_id = -1;
```

- [ ] **Step 12: Commit**

```bash
git add libs/battleforge/battleforge.c
git commit -m "refactor(battleforge): implement ECS with component arrays and systems"
```

Note: Build may still fail until Task 5 updates the client code.

---

### Task 5: Update the client (apps/barrier)

**Files:**
- Modify: `apps/barrier/main.c`
- Modify: `apps/barrier/console.c`
- Modify: `apps/barrier/console.h`
- Modify: `apps/barrier/Makefile.am`

- [ ] **Step 1: Update barrier Makefile.am to depend on libs/ini**

```makefile
bin_PROGRAMS = barrier
barrier_SOURCES = main.c console.c
barrier_CPPFLAGS = -I$(top_srcdir)/libs/math -I$(top_srcdir)/libs/raytrace -I$(top_srcdir)/libs/battleforge -I$(top_srcdir)/libs/thread -I$(top_srcdir)/libs/slice -I$(top_srcdir)/libs/ini $(SDL2_CFLAGS)
barrier_LDADD = $(top_builddir)/libs/battleforge/libbattleforge.la $(top_builddir)/libs/raytrace/libraytrace.la $(top_builddir)/libs/thread/libthread.la $(top_builddir)/libs/slice/libslice.la $(top_builddir)/libs/ini/libini.la -lm -lpthread $(SDL2_LIBS)
```

- [ ] **Step 2: Update main.c — register unit defs and use new entity_create**

Key changes:
1. After loading each sprite sheet, register a `bf_unit_def` via `BF_CMD_REGISTER_UNIT`
2. Change `BF_CMD_ENTITY_CREATE` to use `unit_def_id` instead of `sprite_id` and `speed`
3. Change `BF_CMD_ENTITY_MOVE` to include `speed` and `loco_type`
4. Remove references to `BF_CMD_ENTITY_SET_SPEED`
5. Entity IDs are now allocated by the engine (indices), not by the client — adjust accordingly

The smiley sprite also needs a unit def registered for it.

In the entity creation section, replace:

```c
/* Register unit definitions for each sprite */
int unit_def_ids[NUM_UNIT_TYPES];
for (int i = 0; i < NUM_UNIT_TYPES; i++) {
    bf_unit_def def = {0};
    strncpy(def.name, unit_names[i], BF_UNIT_NAME_SIZE - 1);
    def.sprite_id = unit_spr_ids[i];
    def.base_speed = 3.0f;
    def.has_selection = 1;
    bf_command(engine, (bf_cmd){
        .type = BF_CMD_REGISTER_UNIT,
        .register_unit = { .def = def }
    });
    unit_def_ids[i] = i + 1; /* +1 because smiley is registered first at index 0 */
}
```

Register the smiley unit def first (index 0), then each unit type.

For entity creation, change from:
```c
.entity_create = { .id = i + 1, .sprite_id = unit_spr_ids[i], ... }
```
to:
```c
.entity_create = { .id = i + 1, .unit_def_id = unit_def_ids[i], ... }
```

Note: The `.id` field in `entity_create` is no longer used by the engine (it allocates via free list). Remove it from the command struct or ignore it. For now, keep it for console logging but the engine uses its own allocation.

For move commands (right-click), change from:
```c
.entity_move = { .id = selected_id, .position = dest }
```
to:
```c
.entity_move = { .id = selected_id, .target = dest, .speed = 3.0f, .loco_type = BF_LOCO_LINEAR }
```

- [ ] **Step 3: Update console.c — remove speed command, update entity create**

1. Remove `cmd_entity_speed` function
2. Remove `"entity speed "` from completions array
3. Update `cmd_entity_create` to use `unit_def_id` instead of `sprite_id`/`speed`:

```c
static void cmd_entity_create(console_state *cs, bf_engine *engine, const char *args)
{
    int id, unit_def_id;
    float x, y, z, dx, dy, dz;
    const char *p = args;

    if (parse_int(&p, &id) || parse_int(&p, &unit_def_id) ||
        parse_float(&p, &x) || parse_float(&p, &y) || parse_float(&p, &z) ||
        parse_float(&p, &dx) || parse_float(&p, &dy) || parse_float(&p, &dz)) {
        console_shell_msg(cs, engine,
            "Usage: entity create id unit_def_id x y z dx dy dz");
        return;
    }

    bf_cmd cmd = {
        .type = BF_CMD_ENTITY_CREATE,
        .entity_create = {
            .id = id, .unit_def_id = unit_def_id,
            .position = {x, y, z},
            .direction = {dx, dy, dz}
        }
    };
    bf_command(engine, cmd);
    console_shell_msg(cs, engine, "Entity %d created (def %d).", id, unit_def_id);
}
```

4. Update `cmd_entity_move` to pass speed and loco_type:

```c
static void cmd_entity_move(console_state *cs, bf_engine *engine, const char *args)
{
    int id;
    float x, y, z;
    const char *p = args;
    if (parse_int(&p, &id) || parse_float(&p, &x) || parse_float(&p, &y) || parse_float(&p, &z)) {
        console_shell_msg(cs, engine, "Usage: entity move id x y z");
        return;
    }
    bf_command(engine, (bf_cmd){ .type = BF_CMD_ENTITY_MOVE,
        .entity_move = { .id = id, .target = {x, y, z},
                         .speed = 3.0f, .loco_type = BF_LOCO_LINEAR } });
    console_shell_msg(cs, engine, "Entity %d moving to (%.1f, %.1f, %.1f).", id, x, y, z);
}
```

5. Update `cmd_help` to reflect changes (remove speed, update create syntax)

6. Remove `"entity speed "` from completions and the entity speed sub-command dispatch

- [ ] **Step 4: Build everything**

Run:
```bash
make clean && make
```
Expected: Full build succeeds with no errors or warnings.

- [ ] **Step 5: Run and test**

Run:
```bash
./apps/barrier/barrier
```
Expected:
- All unit sprites load and render correctly
- Camera movement works (WASD + arrows)
- Left-click selects entities
- Right-click moves selected entity (linear movement)
- Console opens with backtick, commands work
- FPS counter in title bar

- [ ] **Step 6: Run all tests**

Run:
```bash
make check
```
Expected: All tests pass (check_vector, check_ini).

- [ ] **Step 7: Commit**

```bash
git add apps/barrier/main.c apps/barrier/console.c apps/barrier/console.h apps/barrier/Makefile.am
git commit -m "refactor(barrier): update client for ECS component API"
```

---

### Task 6: Add example unit and map INI files

**Files:**
- Create: `apps/barrier/units/rifleman.ini`
- Create: `apps/barrier/maps/battlefield.ini`

- [ ] **Step 1: Create the units directory and example unit INI**

Create `apps/barrier/units/rifleman.ini`:

```ini
[visual]
image = rifleman.png
angles = 32
columns = 1
frame_width = 16
frame_height = 16
fps = 4
width = 2.0
height = 2.0

[visual.animation.idle]
columns = 0
loop = true

[locomotion]
speed = 3.0

[selection]
```

This matches the existing `apps/barrier/assets/rifleman.ini` sprite data plus the new component sections.

- [ ] **Step 2: Create the maps directory and example map INI**

Create `apps/barrier/maps/battlefield.ini`:

```ini
[map]
width = 100.0
depth = 100.0
grid_cols = 64
grid_rows = 64
max_height = 10.0

[lighting]
ambient = 0.15
light_dir = 1.0, 1.0, -1.0
light_intensity = 0.85
```

- [ ] **Step 3: Commit**

```bash
git add apps/barrier/units/ apps/barrier/maps/
git commit -m "feat(barrier): add example unit and map INI definition files"
```

---

### Task 7: Update battleforge Makefile.am for libs/ini dependency

**Files:**
- Modify: `libs/battleforge/Makefile.am`

- [ ] **Step 1: Add libs/ini include path**

The battleforge library itself doesn't use libs/ini directly (the client does the parsing), but if future changes need it, add the include. For now, no change is needed to `libs/battleforge/Makefile.am` since the engine has no filesystem awareness.

Verify the current Makefile.am is correct:

```makefile
noinst_LTLIBRARIES = libbattleforge.la
libbattleforge_la_SOURCES = battleforge.c
libbattleforge_la_CPPFLAGS = -I$(top_srcdir)/libs/math -I$(top_srcdir)/libs/raytrace -I$(top_srcdir)/libs/thread -I$(top_srcdir)/libs/slice
libbattleforge_la_LIBADD = $(top_builddir)/libs/raytrace/libraytrace.la $(top_builddir)/libs/thread/libthread.la $(top_builddir)/libs/slice/libslice.la -lm -lpthread
```

No changes needed — the engine doesn't depend on libs/ini.

- [ ] **Step 2: Final full build and test**

Run:
```bash
autoreconf -i && ./configure && make clean && make && make check
```
Expected: Everything builds. All tests pass. The barrier app runs correctly.

- [ ] **Step 3: Commit (if any Makefile changes were needed)**

Only commit if changes were made. Otherwise, skip.

---

### Task 8: Final integration test and cleanup

**Files:**
- All modified files from previous tasks

- [ ] **Step 1: Full rebuild from clean**

```bash
make distclean
autoreconf -i
./configure
make
make check
```
Expected: Clean build, all tests pass.

- [ ] **Step 2: Run barrier and verify all functionality**

Run:
```bash
./apps/barrier/barrier
```

Test checklist:
- [ ] Window opens, terrain renders with mountain
- [ ] Two armies of units visible (10 units each side)
- [ ] Camera movement: WASD + arrow keys + space/shift
- [ ] Left-click on unit selects it (check stderr output)
- [ ] Right-click on ground moves selected unit (linear movement, unit walks)
- [ ] Left-click on ground deselects
- [ ] Backtick opens console
- [ ] Console commands work: `help`, `entity create`, `entity move`, `select`
- [ ] `entity speed` command is gone (should show "Unknown entity command")
- [ ] FPS counter in title bar
- [ ] ESC quits

- [ ] **Step 3: Run nbody to verify no regressions**

```bash
./apps/nbody/nbody
```
Expected: N-body simulation works as before.

- [ ] **Step 4: Verify rtdemo still works**

```bash
./apps/rtdemo/rtdemo
```
Expected: Ray trace demo works as before.

- [ ] **Step 5: Final commit if any cleanup needed**

Only if issues were found and fixed during integration testing.
