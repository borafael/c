# Thread Pool Library Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Implement a minimal pthreads-based thread pool in `libs/thread/` and use it to parallelize the O(n^2) force calculation in nbody.

**Architecture:** Fixed-size thread pool with circular task queue. Workers sleep on a condition variable, wake to dequeue tasks. `thread_pool_wait` blocks until all submitted tasks complete. Force calculation is parallelized by partitioning the outer loop across threads, using per-thread local acceleration buffers to avoid write contention, then summing after all threads complete.

**Tech Stack:** C, pthreads, GNU Autotools

**Design doc:** `docs/plans/2026-02-26-thread-pool-design.md`

---

### Task 1: Create thread_pool.h

**Files:**
- Create: `libs/thread/thread_pool.h`

**Step 1: Write the header**

```c
#ifndef THREAD_POOL_H
#define THREAD_POOL_H

typedef struct thread_pool thread_pool;

/**
 * Create a thread pool with the given number of worker threads.
 * Returns NULL on failure.
 */
thread_pool *thread_pool_create(int num_threads);

/**
 * Submit a task to the pool. The function fn will be called with arg
 * by one of the worker threads.
 */
void thread_pool_submit(thread_pool *pool, void (*fn)(void *), void *arg);

/**
 * Block until all submitted tasks have completed.
 */
void thread_pool_wait(thread_pool *pool);

/**
 * Shut down the pool and free all resources.
 * Waits for in-progress tasks to finish before returning.
 */
void thread_pool_destroy(thread_pool *pool);

#endif /* THREAD_POOL_H */
```

**Step 2: Commit**

```bash
git add libs/thread/thread_pool.h
git commit -m "feat(thread): add thread pool public API header"
```

---

### Task 2: Create thread_pool.c

**Files:**
- Create: `libs/thread/thread_pool.c`

**Step 1: Write the implementation**

```c
#include "thread_pool.h"
#include <pthread.h>
#include <stdlib.h>

#define QUEUE_SIZE 1024

typedef struct {
    void (*fn)(void *);
    void *arg;
} task;

struct thread_pool {
    pthread_t *threads;
    int num_threads;

    task queue[QUEUE_SIZE];
    int head;
    int tail;
    int count;

    pthread_mutex_t mutex;
    pthread_cond_t not_empty;
    pthread_cond_t done;
    int pending;
    int shutdown;
};

static void *worker(void *arg) {
    thread_pool *pool = (thread_pool *)arg;

    for (;;) {
        pthread_mutex_lock(&pool->mutex);

        while (pool->count == 0 && !pool->shutdown) {
            pthread_cond_wait(&pool->not_empty, &pool->mutex);
        }

        if (pool->shutdown && pool->count == 0) {
            pthread_mutex_unlock(&pool->mutex);
            return NULL;
        }

        task t = pool->queue[pool->head];
        pool->head = (pool->head + 1) % QUEUE_SIZE;
        pool->count--;

        pthread_mutex_unlock(&pool->mutex);

        t.fn(t.arg);

        pthread_mutex_lock(&pool->mutex);
        pool->pending--;
        if (pool->pending == 0) {
            pthread_cond_signal(&pool->done);
        }
        pthread_mutex_unlock(&pool->mutex);
    }

    return NULL;
}

thread_pool *thread_pool_create(int num_threads) {
    thread_pool *pool = calloc(1, sizeof(thread_pool));
    if (!pool) return NULL;

    pool->threads = malloc(sizeof(pthread_t) * num_threads);
    if (!pool->threads) {
        free(pool);
        return NULL;
    }
    pool->num_threads = num_threads;

    pthread_mutex_init(&pool->mutex, NULL);
    pthread_cond_init(&pool->not_empty, NULL);
    pthread_cond_init(&pool->done, NULL);

    for (int i = 0; i < num_threads; i++) {
        pthread_create(&pool->threads[i], NULL, worker, pool);
    }

    return pool;
}

void thread_pool_submit(thread_pool *pool, void (*fn)(void *), void *arg) {
    pthread_mutex_lock(&pool->mutex);

    pool->queue[pool->tail].fn = fn;
    pool->queue[pool->tail].arg = arg;
    pool->tail = (pool->tail + 1) % QUEUE_SIZE;
    pool->count++;
    pool->pending++;

    pthread_cond_signal(&pool->not_empty);
    pthread_mutex_unlock(&pool->mutex);
}

void thread_pool_wait(thread_pool *pool) {
    pthread_mutex_lock(&pool->mutex);
    while (pool->pending > 0) {
        pthread_cond_wait(&pool->done, &pool->mutex);
    }
    pthread_mutex_unlock(&pool->mutex);
}

void thread_pool_destroy(thread_pool *pool) {
    pthread_mutex_lock(&pool->mutex);
    pool->shutdown = 1;
    pthread_cond_broadcast(&pool->not_empty);
    pthread_mutex_unlock(&pool->mutex);

    for (int i = 0; i < pool->num_threads; i++) {
        pthread_join(pool->threads[i], NULL);
    }

    pthread_mutex_destroy(&pool->mutex);
    pthread_cond_destroy(&pool->not_empty);
    pthread_cond_destroy(&pool->done);
    free(pool->threads);
    free(pool);
}
```

**Step 2: Commit**

```bash
git add libs/thread/thread_pool.c
git commit -m "feat(thread): implement thread pool with pthreads"
```

---

### Task 3: Integrate into build system

**Files:**
- Create: `libs/thread/Makefile.am`
- Modify: `Makefile.am:5` (SUBDIRS line)
- Modify: `configure.ac:11-15` (AC_CONFIG_FILES block)
- Modify: `apps/nbody/Makefile.am:4-5` (CPPFLAGS and LDADD)

**Step 1: Create libs/thread/Makefile.am**

```makefile
noinst_LTLIBRARIES = libthread.la
libthread_la_SOURCES = thread_pool.c
libthread_la_LIBADD = -lpthread
```

**Step 2: Add libs/thread to root Makefile.am SUBDIRS**

Change line 5 from:
```makefile
SUBDIRS = libs/math apps/nbody
```
to:
```makefile
SUBDIRS = libs/math libs/thread apps/nbody
```

**Step 3: Add libs/thread/Makefile to configure.ac**

Change AC_CONFIG_FILES block to:
```m4
AC_CONFIG_FILES([
    Makefile
    libs/math/Makefile
    libs/thread/Makefile
    apps/nbody/Makefile
])
```

**Step 4: Link nbody against libthread**

Change `apps/nbody/Makefile.am` to:
```makefile
bin_PROGRAMS = nbody
nbody_SOURCES = main.c nbody.c render.c input.c

nbody_CPPFLAGS = -I$(top_srcdir)/libs/math -I$(top_srcdir)/libs/thread $(SDL2_CFLAGS)
nbody_LDADD = $(top_builddir)/libs/thread/libthread.la -lm $(SDL2_LIBS)
```

**Step 5: Regenerate build system and verify it compiles**

```bash
autoreconf -i
./configure
make clean
make
```

Expected: builds successfully with thread pool library linked.

**Step 6: Commit**

```bash
git add libs/thread/Makefile.am Makefile.am configure.ac apps/nbody/Makefile.am
git commit -m "build: integrate thread pool library into autotools"
```

---

### Task 4: Parallelize force calculation in nbody

**Files:**
- Modify: `apps/nbody/nbody.c`

This is the core change. The strategy:
- Add `#include "thread_pool.h"` and `#include <string.h>`
- Define `NUM_THREADS 8` and a `force_task_args` struct containing:
  - `start`, `end` — range of outer loop `i` values for this thread
  - `local_accel[MAX_ENTITIES]` — thread-local acceleration buffer (avoids write contention)
  - `local_merges[]` + `merge_count` — thread-local merge candidate list
- Create a static pool and static args array
- Create/destroy pool in `nbody_init()` (add `nbody_cleanup()` for destroy)
- Extract the inner force loop body into `compute_forces_chunk(void *arg)`
- In `nbody_step()`:
  1. Reset accelerations (unchanged)
  2. Partition active entities across NUM_THREADS chunks
  3. Submit each chunk to pool, wait
  4. Sum all local_accel buffers into physics_components[].acceleration
  5. Concatenate all local merge lists into merge_list[]
  6. Apply merges (unchanged)
  7. Integrate (unchanged)

**Step 1: Add includes, constants, and thread structures**

At top of nbody.c, add include for thread_pool.h and string.h.

Add after existing defines:
```c
#define NUM_THREADS 8

typedef struct {
    int start;
    int end;
    vector local_accel[MAX_ENTITIES];
    merge_pair local_merges[MAX_MERGES / NUM_THREADS];
    int merge_count;
} force_task_args;

static thread_pool *pool;
static force_task_args task_args[NUM_THREADS];
```

**Step 2: Add compute_forces_chunk function**

Add before nbody_step():
```c
static void compute_forces_chunk(void *arg) {
    force_task_args *a = (force_task_args *)arg;

    memset(a->local_accel, 0, sizeof(a->local_accel));
    a->merge_count = 0;

    for (int i = a->start; i < a->end; i++) {
        if ((entity_masks[i] & (POSITION | PHYSICS)) != (POSITION | PHYSICS))
            continue;

        for (int j = i + 1; j < MAX_ENTITIES; j++) {
            if ((entity_masks[j] & (POSITION | PHYSICS)) != (POSITION | PHYSICS))
                continue;

            vector diff = vector_sub(position_components[j].coordinates,
                                     position_components[i].coordinates);
            float dist = vector_magnitude(diff);

            if (dist < SOFTENING) {
                int max_local = MAX_MERGES / NUM_THREADS;
                if (a->merge_count < max_local) {
                    a->local_merges[a->merge_count].i = i;
                    a->local_merges[a->merge_count].j = j;
                    a->merge_count++;
                }
                continue;
            }

            float force = G * physics_components[i].mass
                        * physics_components[j].mass / (dist * dist);
            vector dir = vector_scale(diff, 1.0f / dist);
            vector force_vec = vector_scale(dir, force);

            a->local_accel[i] = vector_add(a->local_accel[i],
                vector_scale(force_vec, 1.0f / physics_components[i].mass));
            a->local_accel[j] = vector_sub(a->local_accel[j],
                vector_scale(force_vec, 1.0f / physics_components[j].mass));
        }
    }
}
```

**Step 3: Replace force loop in nbody_step()**

Replace the "Calculate gravitational forces" and "Apply merges" sections with:
```c
    /* Calculate gravitational forces (parallel) */
    int chunk = MAX_ENTITIES / NUM_THREADS;
    for (int t = 0; t < NUM_THREADS; t++) {
        task_args[t].start = t * chunk;
        task_args[t].end = (t == NUM_THREADS - 1) ? MAX_ENTITIES : (t + 1) * chunk;
        thread_pool_submit(pool, compute_forces_chunk, &task_args[t]);
    }
    thread_pool_wait(pool);

    /* Sum thread-local accelerations */
    for (int t = 0; t < NUM_THREADS; t++) {
        for (int i = 0; i < MAX_ENTITIES; i++) {
            physics_components[i].acceleration = vector_add(
                physics_components[i].acceleration, task_args[t].local_accel[i]);
        }
    }

    /* Collect merge candidates from all threads */
    int merge_count = 0;
    for (int t = 0; t < NUM_THREADS; t++) {
        for (int m = 0; m < task_args[t].merge_count; m++) {
            if (merge_count < MAX_MERGES) {
                merge_list[merge_count++] = task_args[t].local_merges[m];
            }
        }
    }

    /* Apply merges */
    /* ... existing merge code unchanged, but use merge_count from above ... */
```

**Step 4: Create/destroy pool in init/cleanup**

In `nbody_init()`, add at the end:
```c
    pool = thread_pool_create(NUM_THREADS);
```

Add new function `nbody_cleanup()`:
```c
void nbody_cleanup(void) {
    thread_pool_destroy(pool);
}
```

**Step 5: Expose nbody_cleanup in nbody.h**

Add declaration:
```c
void nbody_cleanup(void);
```

**Step 6: Call nbody_cleanup in main.c**

Add before `render_cleanup()`:
```c
    nbody_cleanup();
```

**Step 7: Build and run**

```bash
make
./apps/nbody/nbody
```

Expected: simulation runs with visually identical behavior, noticeably smoother with 8000 entities.

**Step 8: Commit**

```bash
git add apps/nbody/nbody.c apps/nbody/nbody.h apps/nbody/main.c
git commit -m "feat(nbody): parallelize force calculation with thread pool"
```
