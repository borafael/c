# RT Renderer CPU Refactor Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Introduce a backend-agnostic `rt_renderer` API (CPU implementation only), reorganize `libs/raytrace` into shared top-level headers plus a `cpu/` subfolder for CPU-specific implementation files, and migrate all three consumers (`apps/rtdemo`, `apps/nbody`, `libs/battleforge`) to the new API so thread-pool and chunk-split glue no longer leaks into application code.

**Architecture:** A new `libs/raytrace/renderer.h` exposes an opaque `rt_renderer` handle with create/destroy/render/name operations. `libs/raytrace/cpu/renderer.c` owns a thread pool (created once, destroyed once) and splits each frame into horizontal scanline strips dispatched in parallel to the existing per-scanline loop. The public render call is stateless — `rt_renderer_render(r, scene, camera, viewport, pixels)` — and carries scene/camera/viewport/pixels per frame. No vtables, no "backend" enum, no internal abstraction layer: there is exactly one concrete implementation behind one opaque handle. A hypothetical future GPU backend would require introducing a vtable to dispatch between multiple implementations at runtime, but the public API stays the same and none of the current consumers would need to change — that future refactor is out of scope for this plan.

**Tech Stack:** C (C11), GNU Autotools (autoconf/automake/libtool), libtool convenience libraries, existing `libs/thread` thread pool, SDL2 (apps only, unchanged).

**Working directory:** `/home/rafa/claude/c-rt-renderer-backend`, branch `refactor/rt-renderer-backend`.

**Before starting:** Run `autoreconf -i && ./configure && make` from the repo root to confirm a clean baseline build. Record the build succeeds before touching anything.

---

## File Structure

**Created:**
- `libs/raytrace/renderer.h` — public API (rt_renderer handle + 4 functions).
- `libs/raytrace/viewport.h` — `rt_viewport` struct extracted from `raytrace.h`.
- `libs/raytrace/cpu/` — new subdirectory for CPU implementation files.
- `libs/raytrace/cpu/renderer.c` — thread pool + chunk split + render dispatch.
- `libs/raytrace/cpu/render_chunk.h` — private CPU-internal declaration of `rt_render_chunk`.
- `libs/raytrace/cpu/render_chunk.c` — `rt_render_chunk` body (content moves from current `raytrace.c`).

**Moved via `git mv` (preserves history):**
- `libs/raytrace/sphere.c` → `libs/raytrace/cpu/sphere.c`
- `libs/raytrace/plane.c` → `libs/raytrace/cpu/plane.c`
- `libs/raytrace/disc.c` → `libs/raytrace/cpu/disc.c`
- `libs/raytrace/cylinder.c` → `libs/raytrace/cpu/cylinder.c`
- `libs/raytrace/triangle.c` → `libs/raytrace/cpu/triangle.c`
- `libs/raytrace/box.c` → `libs/raytrace/cpu/box.c`
- `libs/raytrace/sprite.c` → `libs/raytrace/cpu/sprite.c`
- `libs/raytrace/heightfield.c` → `libs/raytrace/cpu/heightfield.c`

**Modified:**
- `libs/raytrace/raytrace.h` — extracts rt_viewport out, then deleted entirely at the end.
- `libs/raytrace/Makefile.am` — new source paths, new include path for subfolder.
- `apps/rtdemo/main.c` — replace thread_pool + chunk split with rt_renderer calls, update includes.
- `apps/nbody/nbody.c` — same migration, update includes.
- `apps/nbody/nbody.h` — drop transitive `raytrace.h` include if present; add explicit includes for the raytrace types it uses.
- `libs/battleforge/battleforge.c` — same migration, update includes.
- `libs/battleforge/battleforge.h` — drop `raytrace.h` umbrella include, replace with explicit includes for the raytrace types used in its public API.

**Deleted:**
- `libs/raytrace/raytrace.h` (after all consumers stop depending on it).
- `libs/raytrace/raytrace.c` (after content moves to `cpu/render_chunk.c`).

**Unchanged:**
- `libs/raytrace/scene.h` / `scene.c`
- `libs/raytrace/camera.h` / `camera.c`
- `libs/raytrace/rt_color.h`
- `libs/raytrace/sphere.h`, `plane.h`, `disc.h`, `cylinder.h`, `triangle.h`, `box.h`, `sprite.h`, `heightfield.h` (the primitive headers stay at the top level; only their `.c` files move)
- `libs/math/vector.h`
- `libs/thread/thread_pool.h`

**Intentionally not moved:**
- The primitive `.h` files (`sphere.h`, etc.) stay at the top level. They contain both the shared struct definition AND the C intersection function declarations. The declarations are "CPU-specific" in the sense that only CPU code implements them today, but the header clutter is zero-cost and splitting each primitive header into two files would add 8 new files for no runtime benefit. Leave them mixed.

---

## Tasks

### Task 1: Extract `viewport.h` from `raytrace.h`

**Rationale:** `rt_viewport` is a shared data type — every current and future rendering path needs frame dimensions and FOV. Pulling it into its own header lets non-umbrella callers include just the viewport type without dragging in every primitive header.

**Files:**
- Create: `libs/raytrace/viewport.h`
- Modify: `libs/raytrace/raytrace.h` (remove struct definition, include `viewport.h` instead — temporary backwards-compat shim)
- Modify: `libs/raytrace/Makefile.am` (add `viewport.h` to `noinst_HEADERS`)

- [ ] **Step 1: Create `libs/raytrace/viewport.h`**

```c
#ifndef RT_VIEWPORT_H
#define RT_VIEWPORT_H

/**
 * Viewport defining projection parameters.
 * Shared by all renderer implementations.
 */
typedef struct {
    int width;
    int height;
    float fov;  /* radians */
} rt_viewport;

#endif /* RT_VIEWPORT_H */
```

- [ ] **Step 2: Update `libs/raytrace/raytrace.h` — remove struct, add include**

Replace the current file with:

```c
#ifndef RAYTRACE_H
#define RAYTRACE_H

#include <stdint.h>
#include "vector.h"
#include "rt_color.h"
#include "viewport.h"
#include "sphere.h"
#include "plane.h"
#include "disc.h"
#include "cylinder.h"
#include "triangle.h"
#include "box.h"
#include "sprite.h"
#include "heightfield.h"
#include "scene.h"
#include "camera.h"

/**
 * Render a chunk of scanlines [y_start, y_end) into pixel_buf.
 * pixel_buf is ARGB8888 format, viewport->width * viewport->height uint32_t's.
 * fov is in radians. Caller is responsible for parallelizing across chunks.
 */
void rt_render_chunk(uint32_t *pixel_buf, const rt_viewport *viewport,
                     int y_start, int y_end,
                     const rt_camera *camera, const rt_scene *scene);

#endif /* RAYTRACE_H */
```

The only change vs. current: the `typedef struct { int width; int height; float fov; } rt_viewport;` block is gone, replaced by `#include "viewport.h"` higher up. Backwards-compatible — anything that included `raytrace.h` still sees `rt_viewport`.

- [ ] **Step 3: Update `libs/raytrace/Makefile.am`**

Add `viewport.h` to the `noinst_HEADERS` list. The resulting file:

```make
noinst_LTLIBRARIES = libraytrace.la
libraytrace_la_SOURCES = raytrace.c scene.c camera.c sphere.c plane.c disc.c \
                         cylinder.c triangle.c box.c sprite.c heightfield.c
noinst_HEADERS = raytrace.h viewport.h rt_color.h scene.h camera.h sphere.h \
                 plane.h disc.h cylinder.h triangle.h box.h sprite.h heightfield.h
libraytrace_la_CPPFLAGS = -I$(top_srcdir)/libs/math
libraytrace_la_LIBADD = -lm
```

- [ ] **Step 4: Build and verify**

Run from the repo root:

```bash
autoreconf -i && ./configure && make
```

Expected: clean build, exit code 0, all existing binaries link and rebuild as before. No functional change yet — we just extracted a struct definition.

- [ ] **Step 5: Commit**

```bash
git add libs/raytrace/viewport.h libs/raytrace/raytrace.h libs/raytrace/Makefile.am
git commit -m "refactor(raytrace): extract rt_viewport into viewport.h

Shared data type — every renderer implementation needs frame dimensions
and FOV. Pull it out of raytrace.h so callers can include it without the
umbrella. raytrace.h still works via a transitive include (backwards
compatible).

Co-Authored-By: Claude Code (Claude Opus 4.6) <noreply@anthropic.com>"
```

---

### Task 2: Introduce the public `rt_renderer` API + CPU implementation

**Rationale:** This is the core of the refactor. Create `renderer.h` (the public handle + create/destroy/render/name functions) and `cpu/renderer.c` (the CPU implementation that owns a thread pool and splits each frame into scanline strips). After this task, the new API exists and builds, but no callers use it yet — this task is purely additive and leaves all existing apps untouched.

The CPU implementation at this stage still calls the current top-level `rt_render_chunk` (declared in `raytrace.h`). A later task will move `rt_render_chunk` into `cpu/render_chunk.c` under a private `cpu/render_chunk.h` header. For now we depend on the existing location so this task is self-contained.

**Files:**
- Create: `libs/raytrace/renderer.h`
- Create: `libs/raytrace/cpu/` (directory)
- Create: `libs/raytrace/cpu/renderer.c`
- Modify: `libs/raytrace/Makefile.am`

- [ ] **Step 1: Create the `cpu/` subdirectory**

```bash
mkdir -p libs/raytrace/cpu
```

- [ ] **Step 2: Create `libs/raytrace/renderer.h`**

```c
#ifndef RT_RENDERER_H
#define RT_RENDERER_H

#include <stdint.h>
#include "viewport.h"
#include "scene.h"
#include "camera.h"

/**
 * Opaque renderer handle. The concrete type is private to the
 * implementation file (currently libs/raytrace/cpu/renderer.c).
 */
typedef struct rt_renderer rt_renderer;

/**
 * Create a new renderer. Returns NULL on allocation or thread-pool
 * creation failure. The returned handle must be freed with
 * rt_renderer_destroy.
 *
 * The renderer allocates a thread pool sized to the number of online
 * CPUs. The same pool is reused for every frame — do not create a new
 * renderer per frame.
 */
rt_renderer *rt_renderer_create(void);

/**
 * Destroy a renderer and release its resources (thread pool, task
 * scratch buffer). Safe to call with NULL.
 */
void rt_renderer_destroy(rt_renderer *r);

/**
 * Render a single frame into pixels. Fully synchronous: returns only
 * after the frame is complete. pixels must point to at least
 * viewport->width * viewport->height uint32_t's in ARGB8888 format.
 *
 * Scene/camera/viewport are passed by pointer and consumed read-only
 * during the call. The caller retains ownership; the renderer does not
 * keep references after the call returns.
 */
void rt_renderer_render(rt_renderer *r,
                        const rt_scene *scene,
                        const rt_camera *camera,
                        const rt_viewport *viewport,
                        uint32_t *pixels);

/**
 * Return a human-readable name for the renderer implementation
 * ("CPU"). The returned string is statically allocated and must not be
 * freed.
 */
const char *rt_renderer_name(const rt_renderer *r);

#endif /* RT_RENDERER_H */
```

- [ ] **Step 3: Create `libs/raytrace/cpu/renderer.c`**

```c
#include "renderer.h"
#include "raytrace.h"       /* rt_render_chunk (will move to cpu/render_chunk.h later) */
#include "thread_pool.h"

#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>

typedef struct {
    uint32_t *pixels;
    const rt_viewport *viewport;
    int y_start;
    int y_end;
    const rt_camera *camera;
    const rt_scene *scene;
} cpu_render_task;

static void cpu_render_task_fn(void *arg) {
    cpu_render_task *t = arg;
    rt_render_chunk(t->pixels, t->viewport, t->y_start, t->y_end,
                    t->camera, t->scene);
}

struct rt_renderer {
    thread_pool *pool;
    int num_threads;
    cpu_render_task *tasks;  /* scratch buffer sized [num_threads] */
};

rt_renderer *rt_renderer_create(void) {
    rt_renderer *r = calloc(1, sizeof(*r));
    if (!r) return NULL;

    int n = (int)sysconf(_SC_NPROCESSORS_ONLN);
    if (n < 1) n = 4;

    r->num_threads = n;
    r->pool = thread_pool_create(n);
    if (!r->pool) {
        free(r);
        return NULL;
    }

    r->tasks = malloc(sizeof(cpu_render_task) * (size_t)n);
    if (!r->tasks) {
        thread_pool_destroy(r->pool);
        free(r);
        return NULL;
    }

    return r;
}

void rt_renderer_destroy(rt_renderer *r) {
    if (!r) return;
    thread_pool_destroy(r->pool);
    free(r->tasks);
    free(r);
}

void rt_renderer_render(rt_renderer *r,
                        const rt_scene *scene,
                        const rt_camera *camera,
                        const rt_viewport *viewport,
                        uint32_t *pixels) {
    int rows_per = viewport->height / r->num_threads;
    if (rows_per < 1) rows_per = 1;

    int n = viewport->height / rows_per;
    if (n > r->num_threads) n = r->num_threads;
    if (n < 1) n = 1;

    for (int i = 0; i < n; i++) {
        r->tasks[i] = (cpu_render_task){
            .pixels   = pixels,
            .viewport = viewport,
            .y_start  = i * rows_per,
            .y_end    = (i == n - 1) ? viewport->height : (i + 1) * rows_per,
            .camera   = camera,
            .scene    = scene,
        };
        thread_pool_submit(r->pool, cpu_render_task_fn, &r->tasks[i]);
    }
    thread_pool_wait(r->pool);
}

const char *rt_renderer_name(const rt_renderer *r) {
    (void)r;
    return "CPU";
}
```

Notes on this file:
- The `struct rt_renderer` definition lives inside this `.c` file — no consumer can see or touch its fields.
- `cpu_render_task_fn` has the `void (*)(void *)` signature required by `thread_pool_submit`; the cast to `cpu_render_task *` is the only type-erased boundary.
- The scanline split math is exactly what `apps/rtdemo/main.c:389-403` currently does, moved inside the renderer so callers don't reimplement it.
- The last chunk absorbs any remainder rows (`(i == n - 1) ? viewport->height : ...`).
- NULL-safety: `rt_renderer_destroy(NULL)` is a no-op, matching standard C library conventions.

- [ ] **Step 4: Update `libs/raytrace/Makefile.am`**

Replace the file with:

```make
noinst_LTLIBRARIES = libraytrace.la
libraytrace_la_SOURCES = raytrace.c scene.c camera.c sphere.c plane.c disc.c \
                         cylinder.c triangle.c box.c sprite.c heightfield.c \
                         cpu/renderer.c
noinst_HEADERS = raytrace.h viewport.h renderer.h rt_color.h scene.h camera.h \
                 sphere.h plane.h disc.h cylinder.h triangle.h box.h sprite.h \
                 heightfield.h
libraytrace_la_CPPFLAGS = -I$(top_srcdir)/libs/math \
                          -I$(top_srcdir)/libs/thread \
                          -I$(top_srcdir)/libs/raytrace
libraytrace_la_LIBADD = -lm
```

Changes from previous:
- `cpu/renderer.c` added to `libraytrace_la_SOURCES`.
- `renderer.h` added to `noinst_HEADERS`.
- `-I$(top_srcdir)/libs/thread` added so `cpu/renderer.c` can find `thread_pool.h`.
- `-I$(top_srcdir)/libs/raytrace` added so files under `libs/raytrace/cpu/` can find top-level headers like `renderer.h`, `raytrace.h`, `viewport.h` via angle-less includes.

- [ ] **Step 5: Regenerate and build**

Run from the repo root:

```bash
autoreconf -i && ./configure && make
```

Expected: clean build, exit code 0. The new files are compiled into `libraytrace.la` but no app references `rt_renderer_*` yet, so no runtime behavior changes. All three existing binaries (rtdemo, nbody, battleforge) still build and work exactly as before.

- [ ] **Step 6: Commit**

```bash
git add libs/raytrace/renderer.h libs/raytrace/cpu/renderer.c libs/raytrace/Makefile.am
git commit -m "feat(raytrace): add rt_renderer public API with CPU implementation

New rt_renderer handle exposes create/destroy/render/name. The CPU
implementation in cpu/renderer.c owns a thread pool (sized to online
CPUs) and splits each frame into horizontal scanline strips dispatched
in parallel. The render call is stateless: scene/camera/viewport/pixels
are passed per-call and consumed read-only.

No callers migrated yet — this task is purely additive. The existing
rt_render_chunk entry point still exists at the top level and is used
by the current three consumers (rtdemo, nbody, battleforge).

Co-Authored-By: Claude Code (Claude Opus 4.6) <noreply@anthropic.com>"
```

---

### Task 3: Migrate `apps/rtdemo` to `rt_renderer`

**Rationale:** Replace the thread pool, render_task struct, and chunk-split loop in `main.c` with `rt_renderer_create` / `rt_renderer_render` / `rt_renderer_destroy`. Also replace the `#include "raytrace.h"` umbrella with explicit per-concern includes, so the file's dependencies are visible.

**Files:**
- Modify: `apps/rtdemo/main.c`

**Before reading this task:** The current `apps/rtdemo/main.c` has a thread pool, a `render_task` struct, a `render_chunk_task` wrapper function, per-frame chunk dispatch, and a window title that includes the thread count. All of that gets replaced. The rest of `main.c` — SDL setup, input handling, `build_scene`, the movement code, the render-scale hotkeys — is unchanged.

- [ ] **Step 1: Delete the `render_task` struct and the `render_chunk_task` function**

At the top of `apps/rtdemo/main.c` (around lines 15-28 in the current file), there's:

```c
typedef struct {
    uint32_t *pixels;
    const rt_viewport *viewport;
    int y_start;
    int y_end;
    const rt_camera *camera;
    const rt_scene *scene;
} render_task;

static void render_chunk_task(void *arg) {
    render_task *t = (render_task *)arg;
    rt_render_chunk(t->pixels, t->viewport, t->y_start, t->y_end,
                    t->camera, t->scene);
}
```

Delete both the struct and the function. They now live inside `libs/raytrace/cpu/renderer.c` and are private to the library.

- [ ] **Step 2: Replace the `#include "raytrace.h"` umbrella with explicit includes**

Current top of the file includes:

```c
#include "raytrace.h"
#include "thread_pool.h"
#include <SDL2/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <unistd.h>
```

Replace with:

```c
#include "renderer.h"
#include "viewport.h"
#include "scene.h"
#include "camera.h"
#include "sphere.h"
#include "plane.h"
#include "box.h"
#include "disc.h"
#include "cylinder.h"
#include "triangle.h"
#include "sprite.h"
#include "heightfield.h"
#include "rt_color.h"
#include <SDL2/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <unistd.h>
```

Notes:
- `thread_pool.h` is removed — the app no longer touches the thread pool directly.
- The exact set of primitive headers needed depends on which ones `build_scene` actually uses. Start with the full list above; once the file compiles, you can prune any unused ones by removing one at a time and rebuilding. Conservative correct first, minimal second.
- `rt_color.h` is included because scene construction may use `rt_color` directly.

- [ ] **Step 3: Remove thread pool creation / destruction, replace with rt_renderer**

In `main()`, around lines 296-300 there is:

```c
int num_threads = (int)sysconf(_SC_NPROCESSORS_ONLN);
if (num_threads < 1) num_threads = 4;
thread_pool *pool = thread_pool_create(num_threads);
render_task *tasks = malloc(sizeof(render_task) * num_threads);
fprintf(stderr, "Using %d render threads\n", num_threads);
```

Replace with:

```c
rt_renderer *rnd = rt_renderer_create();
if (!rnd) {
    fprintf(stderr, "Failed to create renderer\n");
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 1;
}
fprintf(stderr, "Using renderer: %s\n", rt_renderer_name(rnd));
```

Near the end of `main()`, around line 424, find:

```c
thread_pool_destroy(pool);
```

Replace with:

```c
rt_renderer_destroy(rnd);
```

If there's also a `free(tasks);` nearby, delete that line (the tasks scratch is now owned by the renderer).

- [ ] **Step 4: Replace the per-frame chunk dispatch with a single render call**

Around lines 389-404 there is:

```c
/* Split scanlines across threads */
int rows_per = render_h / num_threads;
if (rows_per < 1) rows_per = 1;
int actual_chunks = render_h / rows_per;
for (int i = 0; i < actual_chunks; i++) {
    tasks[i] = (render_task){
        .pixels = pixels,
        .viewport = &viewport,
        .y_start = i * rows_per,
        .y_end = (i == actual_chunks - 1) ? render_h : (i + 1) * rows_per,
        .camera = camera,
        .scene = scene
    };
    thread_pool_submit(pool, render_chunk_task, &tasks[i]);
}
thread_pool_wait(pool);
```

Replace with:

```c
rt_renderer_render(rnd, scene, camera, &viewport, pixels);
```

- [ ] **Step 5: Update the window title to show the renderer name instead of thread count**

Around lines 413-415 there is:

```c
snprintf(title_buf, sizeof(title_buf),
         "Raytrace Demo - %d FPS (%dx%d, 1/%d scale, %d threads)",
         fps_frames, render_w, render_h, render_scale, num_threads);
```

Replace with:

```c
snprintf(title_buf, sizeof(title_buf),
         "Raytrace Demo - %d FPS (%dx%d, 1/%d scale, %s)",
         fps_frames, render_w, render_h, render_scale, rt_renderer_name(rnd));
```

The thread count goes away; the renderer name ("CPU" for now) takes its place. When a GPU backend arrives later, this automatically updates.

- [ ] **Step 6: Remove the now-unused `num_threads` variable**

Search `apps/rtdemo/main.c` for any remaining references to `num_threads`. After the previous steps the variable should be unused; delete its declaration and any stale references. Don't leave a dead local.

- [ ] **Step 7: Build and run**

From the repo root:

```bash
make
```

Expected: clean build, exit code 0.

Then run the binary:

```bash
./apps/rtdemo/rtdemo
```

Expected visual behavior:
- Window opens showing the raytraced scene identical to before.
- Window title shows something like `Raytrace Demo - 60 FPS (800x600, 1/1 scale, CPU)`.
- WASD/arrows/space/shift movement works as before.
- `-` / `=` keys toggle render scale as before.
- ESC quits.
- FPS is in the same ballpark as the pre-refactor baseline (small differences of a few fps are fine; order-of-magnitude differences are a bug).

If the visual output differs from before, the chunk split arithmetic in `cpu/renderer.c` does not match the original — revisit and align.

- [ ] **Step 8: Commit**

```bash
git add apps/rtdemo/main.c
git commit -m "refactor(rtdemo): migrate to rt_renderer API

Replace thread pool, render_task struct, chunk-split loop, and per-frame
task dispatch with a single rt_renderer_render call. The glue code that
was duplicated across rtdemo/nbody/battleforge now lives in one place
inside libs/raytrace/cpu/renderer.c.

Window title now displays rt_renderer_name() instead of thread count.

Co-Authored-By: Claude Code (Claude Opus 4.6) <noreply@anthropic.com>"
```

---

### Task 4: Migrate `apps/nbody` to `rt_renderer`

**Rationale:** Same migration as rtdemo, applied to nbody. nbody uses rt_render_chunk in a slightly different shape — it has its own `render_chunk_args` struct and a `render_scene` helper function. Simplify both.

**Files:**
- Modify: `apps/nbody/nbody.c`
- Modify (if needed): `apps/nbody/nbody.h`

- [ ] **Step 1: Check nbody.h for raytrace includes**

Read `apps/nbody/nbody.h`. If it has `#include "raytrace.h"`, it's relying on the umbrella. Determine which specific raytrace types its public API exposes (likely `rt_scene`, `rt_camera`, `rt_viewport`), then replace with explicit includes.

If nbody.h does not include raytrace.h, skip this step.

- [ ] **Step 2: Update `apps/nbody/nbody.c` includes**

Find the top of `apps/nbody/nbody.c` (currently around lines 1-11):

```c
#include "nbody.h"
#include "render.h"
#include "vector.h"
#include "thread_pool.h"
#include "raytrace.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stddef.h>
#include <time.h>
```

Replace `#include "raytrace.h"` and `#include "thread_pool.h"` with explicit includes:

```c
#include "nbody.h"
#include "render.h"
#include "vector.h"
#include "renderer.h"
#include "viewport.h"
#include "scene.h"
#include "camera.h"
#include "sphere.h"
#include "plane.h"
#include "box.h"
#include "disc.h"
#include "cylinder.h"
#include "triangle.h"
#include "sprite.h"
#include "heightfield.h"
#include "rt_color.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stddef.h>
#include <time.h>
```

Keep `thread_pool.h` only if nbody still uses the thread pool for `compute_forces_chunk` (it does — see lines 330s in the original). In that case retain `#include "thread_pool.h"`:

```c
#include "thread_pool.h"
```

- [ ] **Step 3: Delete `render_chunk_task` and the render-side thread pool usage**

Around lines 255-258 in nbody.c:

```c
static void render_chunk_task(void *arg) {
    render_chunk_args *a = (render_chunk_args *)arg;
    rt_render_chunk(a->pixel_buf, a->viewport,
                    a->y_start, a->y_end, a->camera, a->scene);
}
```

Delete this function. Also delete the `render_chunk_args` struct definition (search for `render_chunk_args` earlier in the file).

- [ ] **Step 4: Add an `rt_renderer` global or struct field**

Where the existing `thread_pool *pool` lives as a file-static (around line 71), add a renderer alongside:

```c
static thread_pool *pool;       /* kept: used by compute_forces_chunk */
static rt_renderer *renderer;   /* new: used by render_scene */
```

Rename the existing `pool` declaration if it currently conflicts with any SDL-provided `renderer` symbol in scope. If there's already a `renderer` identifier (e.g., an SDL_Renderer pointer), rename the new one to `rt_rnd` to avoid collision.

- [ ] **Step 5: Initialize the renderer in nbody_init**

Find `nbody_init` (around the `pool = thread_pool_create(num_threads);` line, approx line 195). After the pool is created, add:

```c
renderer = rt_renderer_create();
if (!renderer) {
    /* pool is already created; tear it down before returning */
    thread_pool_destroy(pool);
    pool = NULL;
    fprintf(stderr, "Failed to create raytrace renderer\n");
    return;  /* or whatever the init function's failure convention is */
}
fprintf(stderr, "Using renderer: %s\n", rt_renderer_name(renderer));
```

(Match the init function's actual signature and error handling — this sketch assumes a void return. Adapt to reality.)

- [ ] **Step 6: Destroy the renderer in nbody_cleanup**

In `nbody_cleanup` (around line 260), before or after `thread_pool_destroy(pool)`, add:

```c
if (renderer) {
    rt_renderer_destroy(renderer);
    renderer = NULL;
}
```

- [ ] **Step 7: Replace the per-frame chunk dispatch in `render_scene`**

Around lines 474-489, the body of `render_scene` is:

```c
static void render_scene(const rt_camera *cam, const rt_viewport *vp) {
    int num_chunks = num_threads;
    render_chunk_args chunk_args[MAX_THREADS];
    int rows_per_chunk = vp->height / num_chunks;

    for (int c = 0; c < num_chunks; c++) {
        chunk_args[c].pixel_buf = pixel_buffer;
        chunk_args[c].viewport = vp;
        chunk_args[c].y_start = c * rows_per_chunk;
        chunk_args[c].y_end = (c == num_chunks - 1) ? vp->height : (c + 1) * rows_per_chunk;
        chunk_args[c].camera = cam;
        chunk_args[c].scene = rt_scene_ptr;
        thread_pool_submit(pool, render_chunk_task, &chunk_args[c]);
    }
    thread_pool_wait(pool);

    render_clear();
    render_texture_update(rt_texture, pixel_buffer, vp->width * (int)sizeof(uint32_t));
    render_present();
}
```

Replace the body with:

```c
static void render_scene(const rt_camera *cam, const rt_viewport *vp) {
    rt_renderer_render(renderer, rt_scene_ptr, cam, vp, pixel_buffer);

    render_clear();
    render_texture_update(rt_texture, pixel_buffer, vp->width * (int)sizeof(uint32_t));
    render_present();
}
```

- [ ] **Step 8: Build**

```bash
make
```

Expected: exit code 0. If `thread_pool.h` was wrongly removed and `compute_forces_chunk` breaks, add the include back.

- [ ] **Step 9: Run and verify**

```bash
./apps/nbody/nbody
```

Expected:
- N-body simulation runs and renders identically to the pre-refactor baseline.
- ESC quits.
- R resets the simulation.
- No new warnings or errors in stderr.
- FPS comparable to baseline.

- [ ] **Step 10: Commit**

```bash
git add apps/nbody/nbody.c apps/nbody/nbody.h
git commit -m "refactor(nbody): migrate to rt_renderer API

Replace per-frame chunk-split/thread-pool dispatch with a single
rt_renderer_render call. The physics thread pool (for compute_forces_chunk)
is unchanged; only the render path is migrated.

Co-Authored-By: Claude Code (Claude Opus 4.6) <noreply@anthropic.com>"
```

(Omit `nbody.h` from the `git add` if you didn't actually need to modify it.)

---

### Task 5: Migrate `libs/battleforge` to `rt_renderer`

**Rationale:** Same migration as rtdemo and nbody, applied to battleforge. battleforge is a library, not an app — its header (`battleforge.h`) currently includes `raytrace.h` transitively, which leaks the umbrella into anything that includes battleforge. Fix that too.

**Files:**
- Modify: `libs/battleforge/battleforge.h`
- Modify: `libs/battleforge/battleforge.c`

- [ ] **Step 1: Read `libs/battleforge/battleforge.h` and identify the raytrace types it uses**

Read the file top-to-bottom. Write down which `rt_*` types appear in:
- struct definitions
- function parameter types
- function return types

Typical candidates: `rt_scene`, `rt_camera`, `rt_viewport`, `rt_sphere`, `vector` (from vector.h, not raytrace). Note every type that shows up in the *public* surface of `battleforge.h`.

- [ ] **Step 2: Replace `#include "raytrace.h"` in `battleforge.h` with explicit includes**

Current top of `libs/battleforge/battleforge.h`:

```c
#ifndef BATTLEFORGE_H
#define BATTLEFORGE_H

#include <stdint.h>
#include "vector.h"
#include "raytrace.h"
```

Replace the `#include "raytrace.h"` line with explicit per-concern includes based on what you identified in Step 1. A conservative replacement that covers almost any usage:

```c
#ifndef BATTLEFORGE_H
#define BATTLEFORGE_H

#include <stdint.h>
#include "vector.h"
#include "viewport.h"
#include "scene.h"
#include "camera.h"
#include "sphere.h"
#include "plane.h"
#include "box.h"
#include "disc.h"
#include "cylinder.h"
#include "triangle.h"
#include "sprite.h"
#include "heightfield.h"
```

After the file compiles, you can prune unused includes one at a time to keep the public surface minimal.

- [ ] **Step 3: Update `libs/battleforge/battleforge.c` includes**

Current top of file:

```c
#include "battleforge.h"
#include "raytrace.h"
#include "thread_pool.h"
#include "vector.h"
#include "slice.h"
#include <stdlib.h>
...
```

Replace `#include "raytrace.h"` with `#include "renderer.h"`. Delete `#include "thread_pool.h"` if battleforge doesn't use the thread pool for anything besides rendering (check — if there are non-render thread pool uses, keep it).

Resulting top:

```c
#include "battleforge.h"
#include "renderer.h"
#include "vector.h"
#include "slice.h"
#include <stdlib.h>
...
```

(`battleforge.h` transitively includes `viewport.h`, `scene.h`, `camera.h`, etc. via the explicit includes added in Step 2, so `.c` doesn't need to re-list them.)

- [ ] **Step 4: Delete `render_chunk_fn` and its surrounding glue**

In `libs/battleforge/battleforge.c` around lines 88-96:

```c
static void render_chunk_fn(void *arg) {
    render_task *t = (render_task *)arg;
    rt_render_chunk(t->pixels, t->viewport, t->y_start, t->y_end,
                    t->camera, t->scene);
}
```

Delete this function. Also search the file for `render_task` (the struct) and delete its definition if it's battleforge-local (it almost certainly is — if it was shared it would already be in a header).

- [ ] **Step 5: Find battleforge's render function and replace the chunk-split dispatch**

Find the function in `battleforge.c` that actually renders a frame. It will have a pattern like:

```c
int rows_per = ...;
for (int i = 0; i < num_chunks; i++) {
    thread_pool_submit(pool, render_chunk_fn, &tasks[i]);
}
thread_pool_wait(pool);
```

Replace the entire block with:

```c
rt_renderer_render(e->renderer, scene, camera, &viewport, pixels);
```

The exact local variable names will differ — use whatever battleforge's render function actually has for the scene, camera, viewport, and pixel buffer. The renderer handle `e->renderer` assumes you'll add the renderer as a field on battleforge's main engine/context struct in the next step.

- [ ] **Step 6: Add `rt_renderer` to battleforge's engine struct**

Find battleforge's main engine/context struct (probably `struct bf_engine` or similar, in `battleforge.c` or exposed as `bf_engine` in `battleforge.h`). Add a field:

```c
struct bf_engine {
    /* ... existing fields ... */
    rt_renderer *renderer;
};
```

If the struct is in `battleforge.h`, add `rt_renderer` forward declaration or `#include "renderer.h"` as needed. If it's private (in `battleforge.c`), the include is already there from Step 3.

- [ ] **Step 7: Create the renderer during battleforge initialization**

Find battleforge's init/create function (likely `bf_engine_create` or `bf_init`). Where it currently creates its thread pool, add:

```c
e->renderer = rt_renderer_create();
if (!e->renderer) {
    /* clean up what's been allocated so far, then return failure */
    ...
    return NULL;
}
```

Then delete any code that was creating a thread pool *for rendering*. If battleforge has a thread pool for other work (unlikely, since all its chunk dispatch is rendering), keep that thread pool separate.

- [ ] **Step 8: Destroy the renderer during battleforge cleanup**

Find battleforge's destroy/cleanup function. Add:

```c
if (e->renderer) {
    rt_renderer_destroy(e->renderer);
    e->renderer = NULL;
}
```

Delete the corresponding `thread_pool_destroy` call for the render pool (keep any non-render pool cleanup).

- [ ] **Step 9: Build**

```bash
make
```

Expected: clean build. If battleforge.h pruning removed a header some external consumer of battleforge.h needed, the build will break in a different file — if that happens, add the missing header back to battleforge.h.

- [ ] **Step 10: Run battleforge and verify**

battleforge is a library, not a standalone binary. It's linked by `apps/battleforge/battleforge_app` (or similar — find the actual executable). Run it:

```bash
./apps/battleforge/battleforge
```

Expected:
- Terrain renders as before.
- Entity selection / movement / mouse interaction works as before.
- No new stderr warnings.
- Visual quality and framerate match the baseline.

- [ ] **Step 11: Commit**

```bash
git add libs/battleforge/battleforge.c libs/battleforge/battleforge.h
git commit -m "refactor(battleforge): migrate to rt_renderer API

Replace per-frame chunk-split/thread-pool render dispatch with a single
rt_renderer_render call. Drop raytrace.h umbrella from battleforge.h in
favor of explicit per-concern includes so the library's public
dependencies are visible.

Co-Authored-By: Claude Code (Claude Opus 4.6) <noreply@anthropic.com>"
```

---

### Task 6: Move `rt_render_chunk` into `cpu/render_chunk.{h,c}`

**Rationale:** After Tasks 3-5, nothing outside `libs/raytrace/cpu/renderer.c` calls `rt_render_chunk` directly. That means it can stop being a top-level public function and become a private helper inside `cpu/`. This move preserves the existing file contents verbatim, just relocates and renames.

**Files:**
- Create: `libs/raytrace/cpu/render_chunk.h`
- Create: `libs/raytrace/cpu/render_chunk.c`
- Delete: `libs/raytrace/raytrace.c`
- Modify: `libs/raytrace/raytrace.h` (remove `rt_render_chunk` declaration)
- Modify: `libs/raytrace/cpu/renderer.c` (include `render_chunk.h` instead of `raytrace.h`)
- Modify: `libs/raytrace/Makefile.am`

- [ ] **Step 1: Create `libs/raytrace/cpu/render_chunk.h`**

```c
#ifndef RT_CPU_RENDER_CHUNK_H
#define RT_CPU_RENDER_CHUNK_H

#include <stdint.h>
#include "viewport.h"
#include "camera.h"
#include "scene.h"

/**
 * Render a chunk of scanlines [y_start, y_end) into pixel_buf.
 * pixel_buf is ARGB8888 format, viewport->width * viewport->height uint32_t's.
 * fov is in radians. Caller is responsible for parallelizing across chunks.
 *
 * CPU backend internal — only cpu/renderer.c calls this function. Not
 * part of the public rt_renderer API.
 */
void rt_render_chunk(uint32_t *pixel_buf, const rt_viewport *viewport,
                     int y_start, int y_end,
                     const rt_camera *camera, const rt_scene *scene);

#endif /* RT_CPU_RENDER_CHUNK_H */
```

- [ ] **Step 2: Move the `rt_render_chunk` body into `cpu/render_chunk.c`**

Use `git mv` to preserve history:

```bash
git mv libs/raytrace/raytrace.c libs/raytrace/cpu/render_chunk.c
```

Then open the new file and update its includes. Current content starts with:

```c
#include "raytrace.h"
#include <math.h>
#include <float.h>
```

Replace with:

```c
#include "render_chunk.h"
#include "sphere.h"
#include "plane.h"
#include "disc.h"
#include "cylinder.h"
#include "triangle.h"
#include "box.h"
#include "sprite.h"
#include "heightfield.h"
#include <math.h>
#include <float.h>
```

The body of `rt_render_chunk` (the `void rt_render_chunk(...) { ... }` definition, lines 5-165 in the original) is unchanged — it still calls `rt_intersect_sphere`, `rt_intersect_plane`, etc., which are still declared in the top-level primitive headers.

- [ ] **Step 3: Remove the `rt_render_chunk` declaration from `raytrace.h`**

Edit `libs/raytrace/raytrace.h` to remove the declaration block:

```c
void rt_render_chunk(uint32_t *pixel_buf, const rt_viewport *viewport,
                     int y_start, int y_end,
                     const rt_camera *camera, const rt_scene *scene);
```

and the preceding comment. `raytrace.h` now contains only `#include` lines. It's essentially a dead file — Task 8 will delete it entirely. For now keep it in place so anything that still transitively depends on it (there shouldn't be anything, but just in case) keeps working.

- [ ] **Step 4: Update `libs/raytrace/cpu/renderer.c` to include `render_chunk.h`**

Find the line:

```c
#include "raytrace.h"       /* rt_render_chunk (will move to cpu/render_chunk.h later) */
```

Replace with:

```c
#include "render_chunk.h"
```

(The other includes in `cpu/renderer.c` — `renderer.h`, `thread_pool.h`, etc. — are unchanged.)

Note: `cpu/render_chunk.h` lives in the same directory as `cpu/renderer.c`, so a plain `#include "render_chunk.h"` resolves via the compiler's default search path (directory of the including file) even without an additional `-I`. No Makefile change needed for this resolution.

- [ ] **Step 5: Update `libs/raytrace/Makefile.am`**

Current (after Task 2):

```make
noinst_LTLIBRARIES = libraytrace.la
libraytrace_la_SOURCES = raytrace.c scene.c camera.c sphere.c plane.c disc.c \
                         cylinder.c triangle.c box.c sprite.c heightfield.c \
                         cpu/renderer.c
noinst_HEADERS = raytrace.h viewport.h renderer.h rt_color.h scene.h camera.h \
                 sphere.h plane.h disc.h cylinder.h triangle.h box.h sprite.h \
                 heightfield.h
libraytrace_la_CPPFLAGS = -I$(top_srcdir)/libs/math \
                          -I$(top_srcdir)/libs/thread \
                          -I$(top_srcdir)/libs/raytrace
libraytrace_la_LIBADD = -lm
```

Update to:

```make
noinst_LTLIBRARIES = libraytrace.la
libraytrace_la_SOURCES = scene.c camera.c sphere.c plane.c disc.c \
                         cylinder.c triangle.c box.c sprite.c heightfield.c \
                         cpu/renderer.c cpu/render_chunk.c
noinst_HEADERS = raytrace.h viewport.h renderer.h rt_color.h scene.h camera.h \
                 sphere.h plane.h disc.h cylinder.h triangle.h box.h sprite.h \
                 heightfield.h cpu/render_chunk.h
libraytrace_la_CPPFLAGS = -I$(top_srcdir)/libs/math \
                          -I$(top_srcdir)/libs/thread \
                          -I$(top_srcdir)/libs/raytrace
libraytrace_la_LIBADD = -lm
```

Changes:
- `raytrace.c` removed from `libraytrace_la_SOURCES` (file no longer exists — `git mv` already removed it).
- `cpu/render_chunk.c` added.
- `cpu/render_chunk.h` added to `noinst_HEADERS`.

- [ ] **Step 6: Build and verify**

```bash
autoreconf -i && ./configure && make
```

Expected: clean build. The `rt_render_chunk` function is now compiled into `libraytrace.la` at `cpu/render_chunk.o` instead of `raytrace.o`. Link-time behavior is identical since all three apps already migrated to the public API and don't reference the symbol directly.

- [ ] **Step 7: Run one of the apps to verify runtime behavior**

```bash
./apps/rtdemo/rtdemo
```

Expected: identical to Task 3 verification. Quit with ESC.

- [ ] **Step 8: Commit**

```bash
git add libs/raytrace/cpu/render_chunk.h libs/raytrace/cpu/render_chunk.c libs/raytrace/raytrace.h libs/raytrace/cpu/renderer.c libs/raytrace/Makefile.am
git commit -m "refactor(raytrace): move rt_render_chunk into cpu/render_chunk.{h,c}

rt_render_chunk is the CPU per-scanline loop. Now that no app calls it
directly, hide it inside libs/raytrace/cpu/ as a private helper used
only by cpu/renderer.c. File contents are unchanged — this is a move
and rename, not a rewrite.

Co-Authored-By: Claude Code (Claude Opus 4.6) <noreply@anthropic.com>"
```

---

### Task 7: Move per-primitive `.c` files into `cpu/`

**Rationale:** The per-primitive intersection and normal functions (`rt_intersect_sphere`, `rt_normal_sphere`, etc.) are CPU-specific implementations of the shared primitive math contract. Their `.h` files stay at the top level because they expose shared struct types. The `.c` files move into `cpu/` to make the split visible.

This task is pure file-relocation plus a Makefile update. No code changes inside the moved files — they already include their paired header by the same filename, and the top-level `-I$(top_srcdir)/libs/raytrace` added in Task 2 means `#include "sphere.h"` still resolves from any location.

**Files:**
- Move: 8 `.c` files from `libs/raytrace/` into `libs/raytrace/cpu/`
- Modify: `libs/raytrace/Makefile.am`

- [ ] **Step 1: Move all 8 primitive `.c` files with `git mv`**

```bash
git mv libs/raytrace/sphere.c libs/raytrace/cpu/sphere.c
git mv libs/raytrace/plane.c libs/raytrace/cpu/plane.c
git mv libs/raytrace/disc.c libs/raytrace/cpu/disc.c
git mv libs/raytrace/cylinder.c libs/raytrace/cpu/cylinder.c
git mv libs/raytrace/triangle.c libs/raytrace/cpu/triangle.c
git mv libs/raytrace/box.c libs/raytrace/cpu/box.c
git mv libs/raytrace/sprite.c libs/raytrace/cpu/sprite.c
git mv libs/raytrace/heightfield.c libs/raytrace/cpu/heightfield.c
```

Verify by running `git status` — you should see 8 renames staged.

- [ ] **Step 2: Update `libs/raytrace/Makefile.am`**

Replace the file's `libraytrace_la_SOURCES` line to reflect the new paths:

```make
noinst_LTLIBRARIES = libraytrace.la
libraytrace_la_SOURCES = scene.c camera.c \
                         cpu/renderer.c cpu/render_chunk.c \
                         cpu/sphere.c cpu/plane.c cpu/disc.c \
                         cpu/cylinder.c cpu/triangle.c cpu/box.c \
                         cpu/sprite.c cpu/heightfield.c
noinst_HEADERS = raytrace.h viewport.h renderer.h rt_color.h scene.h camera.h \
                 sphere.h plane.h disc.h cylinder.h triangle.h box.h sprite.h \
                 heightfield.h cpu/render_chunk.h
libraytrace_la_CPPFLAGS = -I$(top_srcdir)/libs/math \
                          -I$(top_srcdir)/libs/thread \
                          -I$(top_srcdir)/libs/raytrace
libraytrace_la_LIBADD = -lm
```

Only `libraytrace_la_SOURCES` changes. Everything else is unchanged.

- [ ] **Step 3: Check moved files compile from their new location**

The moved `.c` files include headers like `#include "sphere.h"`. Because `libraytrace_la_CPPFLAGS` includes `-I$(top_srcdir)/libs/raytrace`, those includes still resolve from any directory. No per-file edits should be needed.

If any of the moved files include headers with a relative path (e.g., `#include "./sprite.h"`) or with assumptions about being at `libs/raytrace/`, adjust those includes. Grep for any unusual include paths:

```bash
grep -rn '#include' libs/raytrace/cpu/*.c
```

The expected pattern is angle-less quoted includes like `"sphere.h"`, `"vector.h"`, `"rt_color.h"`, etc. No `./` prefixes, no `../` escapes.

- [ ] **Step 4: Build**

```bash
autoreconf -i && ./configure && make
```

Expected: clean build, exit code 0. If the build fails with "sphere.h: No such file or directory" or similar, the CPPFLAGS `-I` is missing or wrong — double-check Step 2.

- [ ] **Step 5: Run an app to verify runtime**

```bash
./apps/rtdemo/rtdemo
```

Expected: unchanged behavior.

- [ ] **Step 6: Commit**

```bash
git add libs/raytrace/cpu/sphere.c libs/raytrace/cpu/plane.c libs/raytrace/cpu/disc.c libs/raytrace/cpu/cylinder.c libs/raytrace/cpu/triangle.c libs/raytrace/cpu/box.c libs/raytrace/cpu/sprite.c libs/raytrace/cpu/heightfield.c libs/raytrace/Makefile.am
git commit -m "refactor(raytrace): move per-primitive .c files into cpu/

The primitive intersection/normal functions (rt_intersect_sphere, etc.)
are CPU-specific implementations of the shared primitive math contract.
Move their .c files into cpu/ to make the split visible; the .h files
stay at the top level because they define shared struct types used by
all current and future rendering paths.

Pure file relocation via git mv — no code changes inside the moved
files. Makefile.am updated to reflect new paths.

Co-Authored-By: Claude Code (Claude Opus 4.6) <noreply@anthropic.com>"
```

---

### Task 8: Delete the top-level `raytrace.h`

**Rationale:** After Tasks 3-5, no app includes `raytrace.h` — all three callers migrated to explicit per-concern includes. After Task 6, `rt_render_chunk`'s declaration was removed from `raytrace.h`. The file now contains only a list of `#include` statements and has zero users. Delete it.

**Files:**
- Delete: `libs/raytrace/raytrace.h`
- Modify: `libs/raytrace/Makefile.am`

- [ ] **Step 1: Verify `raytrace.h` has no remaining users**

From the repo root:

```bash
grep -rn '#include "raytrace.h"' .
grep -rn '#include <raytrace.h>' .
```

Expected: zero hits. If any result appears, migrate that caller to explicit includes before deleting. (Do not delete the header while anything still references it — that'd break the build.)

- [ ] **Step 2: Delete the file**

```bash
git rm libs/raytrace/raytrace.h
```

- [ ] **Step 3: Update `libs/raytrace/Makefile.am`**

Remove `raytrace.h` from `noinst_HEADERS`:

```make
noinst_LTLIBRARIES = libraytrace.la
libraytrace_la_SOURCES = scene.c camera.c \
                         cpu/renderer.c cpu/render_chunk.c \
                         cpu/sphere.c cpu/plane.c cpu/disc.c \
                         cpu/cylinder.c cpu/triangle.c cpu/box.c \
                         cpu/sprite.c cpu/heightfield.c
noinst_HEADERS = viewport.h renderer.h rt_color.h scene.h camera.h \
                 sphere.h plane.h disc.h cylinder.h triangle.h box.h sprite.h \
                 heightfield.h cpu/render_chunk.h
libraytrace_la_CPPFLAGS = -I$(top_srcdir)/libs/math \
                          -I$(top_srcdir)/libs/thread \
                          -I$(top_srcdir)/libs/raytrace
libraytrace_la_LIBADD = -lm
```

Only `noinst_HEADERS` changes (removal of `raytrace.h`).

- [ ] **Step 4: Full clean rebuild**

```bash
autoreconf -i && ./configure && make clean && make
```

Expected: clean rebuild, exit code 0. `make clean` forces every object file to be rebuilt, so any lingering stale references to the old `raytrace.h` show up as errors.

- [ ] **Step 5: Run all three apps to verify nothing regressed**

```bash
./apps/rtdemo/rtdemo
```

Expected: works identically to pre-refactor. Quit with ESC.

```bash
./apps/nbody/nbody
```

Expected: N-body sim runs normally. Quit with ESC.

```bash
./apps/battleforge/battleforge
```

(Or whatever the battleforge binary is actually named — find it with `find apps -name 'battleforge*' -executable -type f`.)

Expected: battleforge runs, terrain renders, interaction works.

If any app fails to run or renders incorrectly, investigate before committing — the refactor is not done until all three apps work.

- [ ] **Step 6: Commit**

```bash
git add libs/raytrace/Makefile.am
git commit -m "refactor(raytrace): delete top-level raytrace.h

No remaining users — all three consumers (rtdemo, nbody, battleforge)
migrated to explicit per-concern includes in earlier tasks, and
rt_render_chunk moved into cpu/render_chunk.h. The umbrella header is
now dead code.

Co-Authored-By: Claude Code (Claude Opus 4.6) <noreply@anthropic.com>"
```

---

## Post-plan validation

After all 8 tasks are committed, verify the final state:

```bash
# Branch diff summary — confirms the 8 commits landed
git log --oneline develop..HEAD

# Tree under libs/raytrace/ should match the target layout
find libs/raytrace -type f | sort

# No stray references to removed symbols or files
grep -rn 'raytrace\.h' apps libs
grep -rn 'rt_render_chunk' apps libs

# Clean build from scratch
make clean && autoreconf -i && ./configure && make
```

Expected findings:
- 8 commits on the branch beyond `develop`.
- `libs/raytrace/` tree has `cpu/` subdirectory with 10 `.c` files + 1 `.h` file; top level has all `.h` files except the deleted `raytrace.h`, plus `scene.c` and `camera.c`.
- `grep raytrace.h` in `apps/` and `libs/` returns zero matches (the file name only appears in git history, not in source).
- `grep rt_render_chunk` returns matches only inside `libs/raytrace/cpu/render_chunk.h` and `libs/raytrace/cpu/render_chunk.c` — no external uses.
- Clean build succeeds with exit code 0.

Manual visual verification (mandatory — the type system cannot catch rendering bugs):

1. Run `./apps/rtdemo/rtdemo`. Move with WASD, look with arrows, change render scale with `-`/`=`. Confirm the scene renders identically to pre-refactor and the window title shows `... CPU`.
2. Run `./apps/nbody/nbody`. Watch the simulation for a few seconds. Confirm particles, trails, and camera behavior are unchanged.
3. Run the battleforge app (whatever its actual binary name is). Confirm terrain, entity selection, and movement all work.

If any of the three apps regressed visually, the refactor is incomplete — investigate the root cause before declaring done.

---

## Rollback plan

If the refactor goes wrong mid-flight and you need to abandon:

```bash
git checkout develop
git branch -D refactor/rt-renderer-backend
```

Each task commits separately, so you can also partially roll back by resetting to a specific commit:

```bash
git log --oneline  # find the commit before the bad task
git reset --hard <commit-sha>
```

All the work is in the clone at `/home/rafa/claude/c-rt-renderer-backend`, so this never touches the primary `/home/rafa/repos/c` checkout.

---

## What's deliberately NOT in scope

To prevent scope creep, these things are explicitly out of scope for this plan:

1. **No vtable or backend enum.** `rt_renderer` has exactly one concrete implementation. Adding a vtable is a future refactor that happens when a second implementation actually exists. See prior design discussion for rationale.
2. **No GPU backend.** No OpenCL, Vulkan, CUDA, or compute-shader code. The CPU implementation is the entire renderer right now.
3. **No capability-flag bitmask.** No `rt_renderer_features()` function or `rt_feature_flags` enum. Add it when a second backend has different capabilities from the CPU.
4. **No tiled work distribution.** The chunk split is still horizontal strips, identical to the current scheme. Switching to 2D tiles is a future perf optimization.
5. **No zero-copy GPU readback path.** The renderer writes pixels into a caller-provided `uint32_t *`. When a GPU backend eventually exists, we'll evaluate whether to add a render-to-GPU-texture path at that time.
6. **No dynamic scene-version dirty tracking.** The CPU backend reads the scene pointer fresh every frame, so dirty tracking is meaningless. When a GPU backend arrives, it'll add internal `last_scene` / `last_version` caching.
7. **No unit tests.** The codebase does not currently have a test harness. Introducing one is its own project. Verification in this plan is "build succeeds + visual output matches baseline."
8. **No primitive header splitting.** `sphere.h` etc. retain both the struct definition AND the C function declarations. Splitting each primitive header into "struct-only" and "cpu-impl" halves would add 8 new headers for zero runtime benefit.
9. **No `rt_render_chunk` rename.** The function keeps its current name even though it's now CPU-internal. Renaming breaks `git blame` continuity and adds no clarity beyond what the folder location already conveys.
10. **No API changes to scene/camera/primitive types.** `rt_scene`, `rt_camera`, `rt_sphere`, etc. are untouched. Only the rendering entry point changes.
