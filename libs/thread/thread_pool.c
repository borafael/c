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
