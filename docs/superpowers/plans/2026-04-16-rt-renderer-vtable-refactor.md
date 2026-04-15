# RT Renderer Vtable Refactor Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Convert the single-implementation `rt_renderer` into a vtable-based abstraction that allows multiple backends (CPU today, GPU tomorrow) to sit behind one public API. The CPU backend remains the only concrete implementation after this refactor; no new rendering code is added. All three consumers (`apps/rtdemo`, `apps/nbody`, `libs/battleforge`) migrate to a new `rt_renderer_create(rt_backend type)` signature that explicitly selects the backend. A client-side capability query (`rt_renderer_available`) lets callers discover which backends are built in.

**Architecture:** The public `struct rt_renderer` moves into `libs/raytrace/renderer.h` with function-pointer fields (`destroy_fn`, `render_fn`, `name_fn`) plus a `void *backend_data` payload. Each backend implements the vtable by allocating an `rt_renderer`, wiring the function pointers to its static handlers, and stashing its private state in `backend_data`. A new top-level `libs/raytrace/renderer.c` hosts the public API entry points: `rt_renderer_create(type)` switches on a `rt_backend` enum and delegates to the backend's constructor (`rt_cpu_renderer_create`); `rt_renderer_destroy/render/name` are thin dispatchers that forward through the vtable. The CPU-specific code in `libs/raytrace/cpu/renderer.c` loses its public API definitions and keeps only the static vtable handlers plus the constructor.

The struct is exposed in the public header rather than hidden behind a dedicated `renderer_internal.h`. This is a deliberate trade-off: callers can see the layout but never have a reason to touch it, and it keeps the file count down for a project that controls all its consumers. If the library is ever distributed as a binary, or if the struct grows backend-specific fields, the struct definition can be promoted to a private header in a follow-up — that move is reversible and cheap.

**Tech Stack:** C (C11), GNU Autotools (autoconf/automake/libtool), libtool convenience libraries, existing `libs/thread` thread pool, SDL2 (apps only, unchanged).

**Working directory:** `/home/rafa/claude/c-rt-renderer-vtable`, branch `refactor/rt-renderer-vtable`.

**Before starting:** Run `autoreconf -i && ./configure && make` from the repo root to confirm a clean baseline build. Then run `./apps/rtdemo/rtdemo`, `./apps/nbody/nbody`, and any battleforge binary to confirm they render correctly. This is the visual baseline the refactor must preserve.

---

## File Structure

**Created:**
- `libs/raytrace/renderer.c` — public API dispatchers + `rt_renderer_create(rt_backend)` factory.

**Modified:**
- `libs/raytrace/renderer.h` — add `rt_backend` enum, `rt_renderer_available` declaration, full `struct rt_renderer` definition with vtable fields, and the new `rt_renderer_create` signature.
- `libs/raytrace/cpu/renderer.c` — remove public API definitions; add static vtable handlers (`cpu_destroy`, `cpu_render`, `cpu_name`), a private `cpu_backend_data` struct, and a new `rt_cpu_renderer_create` constructor.
- `libs/raytrace/Makefile.am` — add `renderer.c` to sources; wrap CPU sources in a (trivially-true) `AM_CONDITIONAL` group.
- `configure.ac` — add `AC_DEFINE([RT_HAVE_CPU_BACKEND], 1, ...)` and `AM_CONDITIONAL([BUILD_CPU_BACKEND], ...)` to establish the pattern for future backends.
- `apps/rtdemo/main.c` — update `rt_renderer_create()` call to pass `RT_BACKEND_CPU`.
- `apps/nbody/nbody.c` — same.
- `libs/battleforge/battleforge.c` — same.

**Unchanged:**
- `libs/raytrace/scene.h` / `scene.c`
- `libs/raytrace/camera.h` / `camera.c`
- `libs/raytrace/viewport.h`
- `libs/raytrace/rt_color.h`
- `libs/raytrace/sphere.h` etc. (all primitive headers and CPU intersection files)
- `libs/raytrace/cpu/render_chunk.{h,c}`
- `libs/math/vector.h`
- `libs/thread/thread_pool.h`

**Intentionally not created:**
- No `libs/raytrace/renderer_internal.h`. The struct lives in the public header. If future backends need more shared internal machinery, a private header can be introduced then — for now it would be one file with a single struct definition, which is overhead without benefit.

---

## Tasks

### Task 1: Add `rt_backend` enum + `rt_renderer_available` query

**Rationale:** Establish the public shape that future backends will plug into, before any architectural surgery happens. The enum starts with a single value (`RT_BACKEND_CPU`); the capability query unconditionally reports CPU as available. The existing `rt_renderer_create(void)` signature is NOT touched in this task — apps keep compiling unchanged. This task exists so the vtable restructuring (Task 2) and the signature flip (Task 3) can land as independent, reviewable commits.

**Files:**
- Modify: `libs/raytrace/renderer.h` (add enum, add `rt_renderer_available` declaration)
- Modify: `libs/raytrace/cpu/renderer.c` (add trivial `rt_renderer_available` implementation)

- [ ] **Step 1: Edit `libs/raytrace/renderer.h`**

Add the enum after the include block, and add the `rt_renderer_available` declaration after `rt_renderer_name`. The file becomes:

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
 * Available backend implementations. Each value corresponds to a
 * concrete rt_renderer implementation. Which backends are actually
 * built into the library depends on configure-time flags — use
 * rt_renderer_available() to check at runtime.
 */
typedef enum {
    RT_BACKEND_CPU = 0,
} rt_backend;

/**
 * Return non-zero if the given backend was compiled into the library
 * and can be instantiated via rt_renderer_create(). Zero otherwise.
 * This is a pure predicate — it never allocates, never fails, and is
 * safe to call before any other rt_* function.
 */
int rt_renderer_available(rt_backend type);

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

- [ ] **Step 2: Edit `libs/raytrace/cpu/renderer.c` — add the availability implementation**

Append this function anywhere after the existing `rt_renderer_name` definition:

```c
int rt_renderer_available(rt_backend type) {
    switch (type) {
    case RT_BACKEND_CPU: return 1;
    }
    return 0;
}
```

This lives in `cpu/renderer.c` temporarily — Task 2 will move it out to the top-level `renderer.c` factory along with the rest of the public API dispatchers.

- [ ] **Step 3: Build and verify**

```bash
autoreconf -i && ./configure && make
```

Expected: clean build, no warnings. No functional change — the new function isn't called from anywhere yet.

- [ ] **Step 4: Commit**

```bash
git add libs/raytrace/renderer.h libs/raytrace/cpu/renderer.c
git commit -m "feat(raytrace): add rt_backend enum + rt_renderer_available

Public surface for the upcoming multi-backend refactor. The enum
currently has a single value (RT_BACKEND_CPU) and the query always
reports CPU available; future backends will add enum values and gate
availability on configure-time flags. The rt_renderer_create signature
is deliberately unchanged in this commit — apps still build without
modification.

Co-Authored-By: Claude Code (Claude Opus 4.6) <noreply@anthropic.com>"
```

---

### Task 2: Convert `rt_renderer` to a vtable + create top-level `renderer.c`

**Rationale:** This is the architectural surgery. Three things happen together and cannot be cleanly split:

1. `struct rt_renderer` moves from `cpu/renderer.c` into `renderer.h` with function-pointer fields (`destroy_fn`, `render_fn`, `name_fn`) and a `void *backend_data` payload.
2. `cpu/renderer.c` is reshaped: the old thread-pool / task-scratch fields move into a private `cpu_backend_data` struct; static handlers `cpu_destroy` / `cpu_render` / `cpu_name` implement the vtable; a new `rt_cpu_renderer_create` allocates the `rt_renderer`, wires the vtable, and returns the handle.
3. A new `libs/raytrace/renderer.c` file hosts the public API dispatchers (`rt_renderer_create`, `rt_renderer_destroy`, `rt_renderer_render`, `rt_renderer_name`, `rt_renderer_available`). These are thin — they forward through the vtable or switch on the backend enum.

The **public signature of `rt_renderer_create` is NOT changed in this task** — it still takes `(void)` and still returns a CPU renderer. The refactor is internal. Apps continue to build and run unchanged, which is the whole point of keeping this task separate from Task 3 (the breaking signature change).

**Files:**
- Modify: `libs/raytrace/renderer.h` (add full `struct rt_renderer` definition)
- Modify: `libs/raytrace/cpu/renderer.c` (reshape around `cpu_backend_data` + static vtable fns + `rt_cpu_renderer_create`)
- Create: `libs/raytrace/renderer.c` (public API dispatchers + factory)
- Modify: `libs/raytrace/Makefile.am` (add `renderer.c` to sources)

- [ ] **Step 1: Edit `libs/raytrace/renderer.h` — add the struct body**

Replace the forward-declaration line with a full definition and add a forward declaration for the backend constructor:

```c
#ifndef RT_RENDERER_H
#define RT_RENDERER_H

#include <stdint.h>
#include "viewport.h"
#include "scene.h"
#include "camera.h"

typedef struct rt_renderer rt_renderer;

/**
 * Available backend implementations. [... existing doc ...]
 */
typedef enum {
    RT_BACKEND_CPU = 0,
} rt_backend;

/**
 * Renderer vtable. Exposed in the public header so the dispatchers in
 * libs/raytrace/renderer.c can call through the function pointers
 * without an extra translation-unit hop. Every field is private — do
 * not read or write them directly. Always go through the
 * rt_renderer_* functions below.
 *
 * backend_data points to an allocation owned by the backend (e.g.,
 * cpu_backend_data for the CPU implementation). The backend's
 * destroy_fn is responsible for freeing both backend_data and the
 * rt_renderer itself.
 */
struct rt_renderer {
    void        (*destroy_fn)(struct rt_renderer *r);
    void        (*render_fn)(struct rt_renderer *r,
                             const rt_scene *scene,
                             const rt_camera *camera,
                             const rt_viewport *viewport,
                             uint32_t *pixels);
    const char *(*name_fn)(const struct rt_renderer *r);
    void         *backend_data;
};

/* ... rt_renderer_available, rt_renderer_create, etc. — unchanged ... */

#endif /* RT_RENDERER_H */
```

Keep all the existing function declarations below the struct. The `rt_renderer_create` signature stays `(void)` for this task.

- [ ] **Step 2: Reshape `libs/raytrace/cpu/renderer.c`**

Replace the file contents entirely with:

```c
#include "renderer.h"
#include "render_chunk.h"
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

typedef struct {
    thread_pool *pool;
    int num_threads;
    cpu_render_task *tasks;  /* scratch buffer sized [num_threads] */
} cpu_backend_data;

static void cpu_render_task_fn(void *arg) {
    cpu_render_task *t = arg;
    rt_render_chunk(t->pixels, t->viewport, t->y_start, t->y_end,
                    t->camera, t->scene);
}

static void cpu_destroy(rt_renderer *r) {
    cpu_backend_data *d = r->backend_data;
    thread_pool_destroy(d->pool);
    free(d->tasks);
    free(d);
    free(r);
}

static void cpu_render(rt_renderer *r,
                       const rt_scene *scene,
                       const rt_camera *camera,
                       const rt_viewport *viewport,
                       uint32_t *pixels) {
    cpu_backend_data *d = r->backend_data;

    int rows_per = viewport->height / d->num_threads;
    if (rows_per < 1) rows_per = 1;

    int n = viewport->height / rows_per;
    if (n > d->num_threads) n = d->num_threads;
    if (n < 1) n = 1;

    for (int i = 0; i < n; i++) {
        d->tasks[i] = (cpu_render_task){
            .pixels   = pixels,
            .viewport = viewport,
            .y_start  = i * rows_per,
            .y_end    = (i == n - 1) ? viewport->height : (i + 1) * rows_per,
            .camera   = camera,
            .scene    = scene,
        };
        thread_pool_submit(d->pool, cpu_render_task_fn, &d->tasks[i]);
    }
    thread_pool_wait(d->pool);
}

static const char *cpu_name(const rt_renderer *r) {
    (void)r;
    return "CPU";
}

rt_renderer *rt_cpu_renderer_create(void) {
    rt_renderer *r = calloc(1, sizeof(*r));
    if (!r) return NULL;

    cpu_backend_data *d = calloc(1, sizeof(*d));
    if (!d) {
        free(r);
        return NULL;
    }

    int n = (int)sysconf(_SC_NPROCESSORS_ONLN);
    if (n < 1) n = 4;

    d->num_threads = n;
    d->pool = thread_pool_create(n);
    if (!d->pool) {
        free(d);
        free(r);
        return NULL;
    }

    d->tasks = malloc(sizeof(cpu_render_task) * (size_t)n);
    if (!d->tasks) {
        thread_pool_destroy(d->pool);
        free(d);
        free(r);
        return NULL;
    }

    r->destroy_fn   = cpu_destroy;
    r->render_fn    = cpu_render;
    r->name_fn      = cpu_name;
    r->backend_data = d;
    return r;
}
```

Notes on the reshape:

- The old `struct rt_renderer` (pool, num_threads, tasks) becomes `cpu_backend_data` — a private, file-local struct.
- `cpu_destroy` is responsible for freeing both the backend data AND the outer `rt_renderer`. This is the vtable contract: `rt_renderer_destroy` (in the top-level dispatcher) just calls `r->destroy_fn(r)` and does nothing else.
- `cpu_render` and `cpu_name` dereference `r->backend_data` to reach the pool and task scratch buffer.
- `rt_cpu_renderer_create` is declared in no header yet — Task 2 is a single translation unit's internals. The top-level `renderer.c` (created in Step 3 below) will forward-declare it inline.
- The functions `rt_renderer_create`, `rt_renderer_destroy`, `rt_renderer_render`, `rt_renderer_name`, and `rt_renderer_available` are all GONE from this file. They move to the new `libs/raytrace/renderer.c` in Step 3.

- [ ] **Step 3: Create `libs/raytrace/renderer.c`**

This new file is the entire public API surface. It holds the factory (which still takes `(void)` in this task), the dispatchers, and the availability query:

```c
#include "renderer.h"

#include <stddef.h>

/* Forward-declare the CPU backend's internal constructor. This is a
 * cross-translation-unit symbol that lives in libs/raytrace/cpu/renderer.c.
 * It is not part of the public API — callers must go through
 * rt_renderer_create. */
rt_renderer *rt_cpu_renderer_create(void);

rt_renderer *rt_renderer_create(void) {
    return rt_cpu_renderer_create();
}

void rt_renderer_destroy(rt_renderer *r) {
    if (!r) return;
    r->destroy_fn(r);
}

void rt_renderer_render(rt_renderer *r,
                        const rt_scene *scene,
                        const rt_camera *camera,
                        const rt_viewport *viewport,
                        uint32_t *pixels) {
    r->render_fn(r, scene, camera, viewport, pixels);
}

const char *rt_renderer_name(const rt_renderer *r) {
    return r->name_fn(r);
}

int rt_renderer_available(rt_backend type) {
    switch (type) {
    case RT_BACKEND_CPU: return 1;
    }
    return 0;
}
```

This file compiles on its own; it depends only on `renderer.h` for the struct layout and the backend forward declaration.

- [ ] **Step 4: Update `libs/raytrace/Makefile.am`**

Add `renderer.c` at the top of `libraytrace_la_SOURCES`:

```make
noinst_LTLIBRARIES = libraytrace.la
libraytrace_la_SOURCES = renderer.c scene.c camera.c \
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

- [ ] **Step 5: Build and verify**

```bash
autoreconf -i && ./configure && make
```

Expected: clean build, no warnings. The library now has an extra translation unit (`renderer.c`) and a reshaped `cpu/renderer.c`, but all symbols the apps link against (`rt_renderer_create`, `rt_renderer_destroy`, `rt_renderer_render`, `rt_renderer_name`, `rt_renderer_available`) are still defined — just in different files.

- [ ] **Step 6: Visual verification**

Run all three apps and confirm rendering is unchanged:

```bash
./apps/rtdemo/rtdemo         # move around, confirm scene renders correctly, title shows "CPU"
./apps/nbody/nbody           # confirm particles render and animate
# Run the battleforge binary (whatever its actual name is)
```

If any app crashes or renders incorrectly, investigate before committing. Common pitfalls:
- `cpu_destroy` frees the wrong pointer (e.g., frees `r->backend_data` twice, or forgets to free `r` itself).
- `cpu_render` dereferences `r->backend_data` before checking it's non-NULL (should never be NULL after `rt_cpu_renderer_create` succeeds, but a bug in Step 2 could leave it zero).
- The forward declaration of `rt_cpu_renderer_create` in `renderer.c` doesn't match its definition in `cpu/renderer.c` (e.g., wrong return type).

- [ ] **Step 7: Commit**

```bash
git add libs/raytrace/renderer.h libs/raytrace/renderer.c \
        libs/raytrace/cpu/renderer.c libs/raytrace/Makefile.am
git commit -m "refactor(raytrace): convert rt_renderer to a vtable

Pull struct rt_renderer into the public header as a vtable (destroy_fn,
render_fn, name_fn, backend_data). The CPU implementation's per-instance
state moves into a file-local cpu_backend_data struct; static
cpu_destroy/render/name functions implement the vtable; a new
rt_cpu_renderer_create constructs the outer rt_renderer, allocates the
backend_data, and wires the function pointers.

A new libs/raytrace/renderer.c hosts the public API dispatchers
(rt_renderer_create, _destroy, _render, _name, _available). The
dispatchers are thin — create delegates to rt_cpu_renderer_create, the
rest forward through the vtable.

Public signatures are unchanged in this commit. Apps still call
rt_renderer_create(void) and still get a CPU renderer. The signature
flip + consumer migration happens in a follow-up commit.

Co-Authored-By: Claude Code (Claude Opus 4.6) <noreply@anthropic.com>"
```

---

### Task 3: Switch `rt_renderer_create` to take `rt_backend` + migrate consumers

**Rationale:** The public API flips from `rt_renderer_create(void)` to `rt_renderer_create(rt_backend type)`. This is a breaking signature change — it cannot be done incrementally because the first app migration would leave the other two unable to compile. All five edits (one header, one factory, three call sites) land in a single commit.

**Files:**
- Modify: `libs/raytrace/renderer.h` (change signature)
- Modify: `libs/raytrace/renderer.c` (switch on enum)
- Modify: `apps/rtdemo/main.c` (pass `RT_BACKEND_CPU`)
- Modify: `apps/nbody/nbody.c` (same)
- Modify: `libs/battleforge/battleforge.c` (same)

- [ ] **Step 1: Edit `libs/raytrace/renderer.h` — change the create signature**

Replace:

```c
rt_renderer *rt_renderer_create(void);
```

with:

```c
/**
 * Create a new renderer backed by the requested implementation.
 * Returns NULL if the requested backend is not built into the library
 * (check with rt_renderer_available first) or if allocation fails.
 *
 * The returned handle must be freed with rt_renderer_destroy.
 */
rt_renderer *rt_renderer_create(rt_backend type);
```

Also update the doc comment about the thread pool — it now belongs on the CPU backend specifically, not the create function:

> The CPU backend allocates a thread pool sized to the number of online
> CPUs. The same pool is reused for every frame — do not create a new
> renderer per frame.

Drop this from the `rt_renderer_create` doc and leave it only as a note elsewhere, or remove it entirely (the requirement is generic: don't create renderers per frame, regardless of backend).

- [ ] **Step 2: Edit `libs/raytrace/renderer.c` — switch on the enum**

Replace:

```c
rt_renderer *rt_renderer_create(void) {
    return rt_cpu_renderer_create();
}
```

with:

```c
rt_renderer *rt_renderer_create(rt_backend type) {
    switch (type) {
    case RT_BACKEND_CPU: return rt_cpu_renderer_create();
    }
    return NULL;
}
```

Task 4 will add an `#ifdef RT_HAVE_CPU_BACKEND` guard around the case. For now the CPU backend is always compiled in, so no guard is needed.

- [ ] **Step 3: Migrate `apps/rtdemo/main.c`**

Find the call at `apps/rtdemo/main.c:293` (currently `rt_renderer *rnd = rt_renderer_create();`) and change it to:

```c
rt_renderer *rnd = rt_renderer_create(RT_BACKEND_CPU);
```

No other changes — the rest of the file is unaffected.

- [ ] **Step 4: Migrate `apps/nbody/nbody.c`**

Find the call at `apps/nbody/nbody.c:205` (currently `rt_rnd = rt_renderer_create();`) and change it to:

```c
rt_rnd = rt_renderer_create(RT_BACKEND_CPU);
```

- [ ] **Step 5: Migrate `libs/battleforge/battleforge.c`**

Find the call at `libs/battleforge/battleforge.c:287` (currently `e->renderer = rt_renderer_create();`) and change it to:

```c
e->renderer = rt_renderer_create(RT_BACKEND_CPU);
```

- [ ] **Step 6: Build and verify**

```bash
make clean && autoreconf -i && ./configure && make
```

Expected: clean build, no warnings. Any app that wasn't migrated will fail with "too few arguments to function 'rt_renderer_create'" — use that as a safety net.

- [ ] **Step 7: Visual verification**

Run all three apps again and confirm the output is still identical to baseline:

```bash
./apps/rtdemo/rtdemo
./apps/nbody/nbody
# battleforge binary
```

This is the critical step. The refactor touches the public entry point of every renderer consumer, so a silent regression (e.g., wrong function-pointer wiring picked up by the enum switch) would only show as a visual bug.

- [ ] **Step 8: Commit**

```bash
git add libs/raytrace/renderer.h libs/raytrace/renderer.c \
        apps/rtdemo/main.c apps/nbody/nbody.c \
        libs/battleforge/battleforge.c
git commit -m "refactor(raytrace): rt_renderer_create takes rt_backend enum

Breaking signature change, bundled with all three consumer migrations
so the repo stays buildable after this single commit. Callers now
explicitly select a backend:

    rt_renderer_create(RT_BACKEND_CPU)

The factory in libs/raytrace/renderer.c switches on the enum and
delegates to the chosen backend's constructor. CPU is currently the
only option; future backends will add enum values and case arms.

Co-Authored-By: Claude Code (Claude Opus 4.6) <noreply@anthropic.com>"
```

---

### Task 4: Autoconf/automake backend plumbing

**Rationale:** Establish the `AC_DEFINE` + `AM_CONDITIONAL` pattern that future backends will follow, even though the CPU backend is always built. For CPU the conditional is trivially true; for OpenGL/Vulkan/CUDA it will gate on `PKG_CHECK_MODULES` results. Setting up the pattern now means adding a new backend becomes a pure copy-paste of existing scaffolding.

Three specific things change:

1. `configure.ac` gains `AC_DEFINE([RT_HAVE_CPU_BACKEND], 1, ...)` and `AM_CONDITIONAL([BUILD_CPU_BACKEND], [true])`.
2. `libs/raytrace/Makefile.am` wraps the CPU source files in an `if BUILD_CPU_BACKEND ... endif` block. The library's core sources (`renderer.c`, `scene.c`, `camera.c`) stay outside the conditional — they must build regardless of which backends are enabled.
3. `libs/raytrace/renderer.c` wraps the `RT_BACKEND_CPU` case arm in `#ifdef RT_HAVE_CPU_BACKEND`. With CPU always on this is a no-op, but it proves the pattern compiles and demonstrates the shape future backends must follow.

**Files:**
- Modify: `configure.ac`
- Modify: `libs/raytrace/Makefile.am`
- Modify: `libs/raytrace/renderer.c`

- [ ] **Step 1: Edit `configure.ac`**

Add the backend scaffolding after the existing `PKG_CHECK_MODULES` block and before `AC_CONFIG_FILES`. Insert:

```m4
# Raytrace backends. Each backend has an AM_CONDITIONAL that
# gates whether its source files are compiled into libraytrace,
# and an AC_DEFINE that the factory uses to conditionally include
# the backend's case arm at compile time.
#
# CPU is always available — no configure flag needed. Future
# backends (OpenGL, Vulkan, CUDA) will add --enable-<name> flags
# gated on PKG_CHECK_MODULES results.

AC_DEFINE([RT_HAVE_CPU_BACKEND], [1], [CPU backend is always built])
AM_CONDITIONAL([BUILD_CPU_BACKEND], [true])
```

The final `configure.ac` should look like:

```m4
AC_INIT([c-monorepo], [1.0])
AM_INIT_AUTOMAKE([foreign -Wall -Werror subdir-objects])
AC_CONFIG_MACRO_DIRS([m4])
AM_PROG_AR
LT_INIT
AC_PROG_CC

# Required for AC_DEFINE to land in a config header.
AC_CONFIG_HEADERS([config.h])

PKG_CHECK_MODULES([SDL2], [sdl2])
PKG_CHECK_MODULES([CHECK], [check >= 0.9.6])

AC_DEFINE([RT_HAVE_CPU_BACKEND], [1], [CPU backend is always built])
AM_CONDITIONAL([BUILD_CPU_BACKEND], [true])

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
AC_OUTPUT
```

Note the added `AC_CONFIG_HEADERS([config.h])` line — `AC_DEFINE` needs somewhere to write the define, and the current repo has no `config.h` set up. Add this line and run `autoreconf -i` to regenerate.

If the repo already uses `AC_DEFINE` elsewhere, `AC_CONFIG_HEADERS` already exists — don't duplicate it.

- [ ] **Step 2: Edit `libs/raytrace/Makefile.am`**

Wrap the CPU sources in a conditional block:

```make
noinst_LTLIBRARIES = libraytrace.la

libraytrace_la_SOURCES = renderer.c scene.c camera.c

if BUILD_CPU_BACKEND
libraytrace_la_SOURCES += cpu/renderer.c cpu/render_chunk.c \
                         cpu/sphere.c cpu/plane.c cpu/disc.c \
                         cpu/cylinder.c cpu/triangle.c cpu/box.c \
                         cpu/sprite.c cpu/heightfield.c
endif

noinst_HEADERS = viewport.h renderer.h rt_color.h scene.h camera.h \
                 sphere.h plane.h disc.h cylinder.h triangle.h box.h sprite.h \
                 heightfield.h cpu/render_chunk.h

libraytrace_la_CPPFLAGS = -I$(top_srcdir)/libs/math \
                          -I$(top_srcdir)/libs/thread \
                          -I$(top_srcdir)/libs/raytrace
libraytrace_la_LIBADD = -lm
```

The core sources (`renderer.c`, `scene.c`, `camera.c`) stay outside the conditional — they must always build. `noinst_HEADERS` is not conditional because headers are free and don't contribute to the link.

- [ ] **Step 3: Edit `libs/raytrace/renderer.c` — add the `#ifdef` guard**

Include `config.h` at the top and wrap the CPU case:

```c
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "renderer.h"

#include <stddef.h>

#ifdef RT_HAVE_CPU_BACKEND
rt_renderer *rt_cpu_renderer_create(void);
#endif

rt_renderer *rt_renderer_create(rt_backend type) {
    switch (type) {
#ifdef RT_HAVE_CPU_BACKEND
    case RT_BACKEND_CPU: return rt_cpu_renderer_create();
#endif
    }
    return NULL;
}

int rt_renderer_available(rt_backend type) {
    switch (type) {
#ifdef RT_HAVE_CPU_BACKEND
    case RT_BACKEND_CPU: return 1;
#endif
    }
    return 0;
}

/* rt_renderer_destroy, rt_renderer_render, rt_renderer_name unchanged */
```

`HAVE_CONFIG_H` is defined by autoconf automatically when `config.h` is in use. The `#ifdef` wrapper around the forward declaration is there for cleanliness: without the CPU backend there's no `rt_cpu_renderer_create` symbol and the declaration would be unused.

- [ ] **Step 4: Build and verify**

```bash
make clean && autoreconf -i && ./configure && make
```

Expected: clean build. Because `BUILD_CPU_BACKEND` is trivially true and `RT_HAVE_CPU_BACKEND` is defined, the generated object code is identical to Task 3. This task is a no-op at runtime — it's only the build-system plumbing that differs.

Sanity check that the `#ifdef`s are actually wired correctly: temporarily comment out the `AC_DEFINE` and `AM_CONDITIONAL` in `configure.ac`, re-run `autoreconf -i && ./configure`, and confirm the build fails (with an empty switch in the factory, or missing sources in `cpu/`). Then uncomment and rebuild. This verifies the guards would actually gate a missing backend — don't skip this, even though it seems paranoid.

- [ ] **Step 5: Visual verification**

Re-run all three apps. Since nothing observable changed, output should be identical to Task 3's verification.

- [ ] **Step 6: Commit**

```bash
git add configure.ac libs/raytrace/Makefile.am libs/raytrace/renderer.c
git commit -m "build(raytrace): add backend plumbing for future GPU backends

Establishes the AC_DEFINE/AM_CONDITIONAL pattern that future backends
(OpenGL, Vulkan, CUDA) will follow. CPU is always on, so the conditional
is trivially true and the #ifdef around the factory case arm is a no-op.
Runtime behavior is unchanged — this is pure build-system scaffolding.

Adding a new backend is now a copy-paste of this structure: a
PKG_CHECK_MODULES block, a new AC_DEFINE, a new AM_CONDITIONAL, a new
if/endif group in libs/raytrace/Makefile.am, and a new case arm in the
factory switch.

Co-Authored-By: Claude Code (Claude Opus 4.6) <noreply@anthropic.com>"
```

---

## Post-plan validation

After all 4 tasks are committed, verify the final state:

```bash
# Branch diff summary — confirms the 4 commits landed
git log --oneline develop..HEAD

# The tree should have the new top-level renderer.c
find libs/raytrace -type f | sort

# No stray references to the old signature
grep -rn 'rt_renderer_create()' apps libs

# Clean build from scratch
make clean && autoreconf -i && ./configure && make
```

Expected findings:
- Exactly 4 commits on the branch beyond `develop`.
- `libs/raytrace/renderer.c` exists at the top level.
- `libs/raytrace/cpu/renderer.c` still exists but no longer defines `rt_renderer_create` / `rt_renderer_destroy` / `rt_renderer_render` / `rt_renderer_name`.
- `grep rt_renderer_create()` returns zero matches — every call site passes `RT_BACKEND_CPU`.
- Clean build succeeds with exit code 0.

**Manual visual verification (mandatory):**

1. Run `./apps/rtdemo/rtdemo`. Move with WASD, look with arrows, change render scale with `-`/`=`. Confirm the scene renders identically to pre-refactor and the window title still shows `... CPU`.
2. Run `./apps/nbody/nbody`. Watch the simulation for a few seconds. Confirm particles, trails, and camera behavior are unchanged.
3. Run the battleforge app (whatever its actual binary name is). Confirm terrain, entity selection, and movement all work.
4. Check `rt_renderer_available(RT_BACKEND_CPU)` returns 1 and `rt_renderer_available(99)` (an invalid value, cast if needed) returns 0. This can be a throwaway `printf` in one of the apps, removed before the final commit — or just inspected in a debugger.

If any of the three apps regressed visually, the refactor is incomplete — investigate the root cause before declaring done.

---

## Rollback plan

If the refactor goes wrong mid-flight and you need to abandon:

```bash
git checkout develop
git branch -D refactor/rt-renderer-vtable
```

Each task commits separately, so you can also partially roll back by resetting to a specific commit:

```bash
git log --oneline  # find the commit before the bad task
git reset --hard <commit-sha>
```

All the work is in the clone at `/home/rafa/claude/c-rt-renderer-vtable`, so this never touches the primary `/home/rafa/repos/c` checkout.

---

## What's deliberately NOT in scope

To prevent scope creep, these things are explicitly out of scope for this plan:

1. **No new backends.** No OpenGL, Vulkan, CUDA, HIP, or Metal implementation. The `rt_backend` enum contains exactly one value (`RT_BACKEND_CPU`). This plan is purely architectural — it makes adding backends possible, not actual.

2. **No runtime backend auto-selection.** There is no `rt_renderer_create_best()` or `rt_renderer_create_auto()` that probes for the fastest available backend. Clients call `rt_renderer_create(RT_BACKEND_CPU)` explicitly. A smart factory can be added when there are at least two backends to choose from.

3. **No backend priority order.** Related to the above: there is no "try OpenGL, fall back to CPU" convenience wrapper. Clients write the fallback logic themselves (`available()` + conditional `create()`).

4. **No internal header (`renderer_internal.h`).** The struct layout lives in the public `renderer.h`. If future backends grow a lot of shared machinery that shouldn't leak to consumers, the struct can be promoted to a private header in a follow-up — reversible.

5. **No capability-flag bitmask.** No `rt_renderer_features()` function or per-backend feature struct. Add it when a second backend has capabilities the CPU backend doesn't (e.g., hardware RT, zero-copy readback).

6. **No performance changes.** The chunk split stays as horizontal strips; `rt_render_chunk` is unchanged; the thread pool setup is unchanged. This refactor must not perturb CPU rendering performance by more than measurement noise.

7. **No unit tests.** The codebase still has no test harness for the raytracer. Verification remains "build succeeds + visual output matches baseline."

8. **No changes to shared types.** `rt_scene`, `rt_camera`, `rt_viewport`, `rt_sphere`, etc., are untouched. Only the renderer handle and its lifecycle change.

9. **No `cpu/` file reorganization.** The primitive intersection files (`cpu/sphere.c`, etc.) and `cpu/render_chunk.{h,c}` keep their current locations and contents. Only `cpu/renderer.c` is reshaped.

10. **No changes to how apps consume primitive types.** The individual primitive headers (`sphere.h`, `plane.h`, etc.) are still included directly where needed. No umbrella header is reintroduced.
