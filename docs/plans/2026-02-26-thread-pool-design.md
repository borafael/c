# Thread Pool Library Design

## Purpose

A minimal thread pool library in `libs/thread/` using pthreads. Primary use case: parallelize the O(n^2) force calculation in the nbody simulation. Designed for reuse across the monorepo.

## API

```c
#include "thread_pool.h"

/* Lifecycle */
thread_pool *thread_pool_create(int num_threads);
void         thread_pool_destroy(thread_pool *pool);

/* Submit work and wait */
void thread_pool_submit(thread_pool *pool, void (*fn)(void *), void *arg);
void thread_pool_wait(thread_pool *pool);
```

## Internals

- **Workers**: `num_threads` pthreads created once at pool creation, sleeping on a condition variable
- **Task queue**: Fixed-size circular buffer of `{fn, arg}` pairs, protected by a mutex
- **Wait mechanism**: A counter of pending tasks (submitted but not yet completed). `thread_pool_wait` blocks on a condition variable until the counter reaches zero.
- **Shutdown**: `thread_pool_destroy` sets a shutdown flag, broadcasts to wake all workers, joins all threads

## Data Structures

```c
typedef struct {
    void (*fn)(void *);
    void *arg;
} task;

struct thread_pool {
    pthread_t *threads;
    int num_threads;

    task *queue;
    int queue_size;
    int queue_head;
    int queue_tail;
    int queue_count;

    pthread_mutex_t mutex;
    pthread_cond_t not_empty;   /* signals workers: new task available */
    pthread_cond_t done;        /* signals waiter: a task completed */
    int pending;                /* submitted but not yet completed */
    int shutdown;
};
```

## Worker Loop

```
lock mutex
loop:
    while queue is empty AND not shutdown:
        wait on not_empty
    if shutdown AND queue is empty:
        unlock, exit
    dequeue task
    unlock mutex
    execute task
    lock mutex
    decrement pending
    if pending == 0:
        signal done
```

## File Structure

```
libs/thread/
├── thread_pool.h    # Public API
├── thread_pool.c    # Implementation
└── Makefile.am      # Build as noinst convenience library
```

## Build Integration

- `libs/thread/Makefile.am`: builds `noinst_LTLIBRARIES = libthread.la`, links `-lpthread`
- Root `Makefile.am`: add `libs/thread` to `SUBDIRS` before `apps/nbody`
- `configure.ac`: add `libs/thread/Makefile` to `AC_CONFIG_FILES`
- `apps/nbody/Makefile.am`: add `-I` for thread headers, link against `libthread.la`

## Usage in nbody

```c
/* in nbody_init() */
pool = thread_pool_create(8);

/* in nbody_step(), force calculation phase */
for (int t = 0; t < num_threads; t++) {
    args[t] = (force_args){ .start = ..., .end = ... };
    thread_pool_submit(pool, compute_forces_chunk, &args[t]);
}
thread_pool_wait(pool);
/* all forces computed, proceed to merge phase */
```

## Future Extensions

- Simple `thread_spawn` / `thread_join` API for one-off tasks (same internal primitives)
- Not planned now: dynamic resizing, task priorities, cancellation

## Design Decisions

- Fixed-size queue (1024 slots) — sufficient for partitioning work into chunks, avoids dynamic allocation
- No per-task result/future — `thread_pool_wait` waits for ALL pending tasks, which matches the fork-join usage pattern in nbody
- Header-only struct definition — allows stack allocation if desired, though `thread_pool_create` uses heap
