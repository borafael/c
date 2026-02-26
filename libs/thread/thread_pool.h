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
