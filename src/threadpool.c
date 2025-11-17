#include "threadpool.h"
#include "http.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* threadpool structure:
   - workers: array of pthread_t for worker threads
   - queue: circular buffer of client fds
   - head/tail/count: queue indices and size
   - lock / not_empty / not_full: synchronization primitives
   - shutdown: flag to tell workers to exit
   - docroot: string passed to handlers (owned by pool) */
struct threadpool {
    pthread_t *workers;
    size_t nworkers;
    int *queue;
    size_t capacity;
    size_t head, tail, count;
    pthread_mutex_t lock;
    pthread_cond_t not_empty;
    pthread_cond_t not_full;
    int shutdown;
    char *docroot;
};

/* Worker main loop:
   - wait for a client fd to be available on the queue
   - dequeue it under the lock, signal producers if necessary
   - call handle_client() then close the client socket
   - exit when shutdown is set and queue is empty */
static void *worker_main(void *arg) {
    struct threadpool *tp = (struct threadpool*)arg;
    while (1) {
        pthread_mutex_lock(&tp->lock);
        /* wait until there's work or we're shutting down */
        while (tp->count == 0 && !tp->shutdown) {
            pthread_cond_wait(&tp->not_empty, &tp->lock);
        }
        /* if shutting down and no work left, break out */
        if (tp->shutdown && tp->count == 0) {
            pthread_mutex_unlock(&tp->lock);
            break;
        }
        /* dequeue client fd from circular buffer */
        int client_fd = tp->queue[tp->head];
        tp->head = (tp->head + 1) % tp->capacity;
        tp->count--;
        /* signal any waiting producers that space is available */
        pthread_cond_signal(&tp->not_full);
        pthread_mutex_unlock(&tp->lock);

        /* handle the request (performs I/O) and then close the socket */
        handle_client(client_fd, tp->docroot);
        close(client_fd);
    }
    return NULL;
}

/* Create a threadpool:
   - allocate structure and arrays
   - initialize mutex/conds and duplicate docroot
   - create worker threads (best-effort; errors printed) */
threadpool_t *threadpool_create(size_t nworkers, size_t queue_capacity, const char *docroot) {
    struct threadpool *tp = calloc(1, sizeof(*tp));
    if (!tp) return NULL;
    tp->nworkers = nworkers;
    tp->workers = calloc(nworkers, sizeof(pthread_t)); /* array for thread ids */
    tp->queue = calloc(queue_capacity, sizeof(int));   /* circular queue storage */
    tp->capacity = queue_capacity;
    pthread_mutex_init(&tp->lock, NULL);
    pthread_cond_init(&tp->not_empty, NULL);
    pthread_cond_init(&tp->not_full, NULL);
    tp->docroot = strdup(docroot ? docroot : "./www"); /* pool owns this copy */

    for (size_t i = 0; i < nworkers; ++i) {
        if (pthread_create(&tp->workers[i], NULL, worker_main, tp) != 0) {
            /* partial failure: threads already created will keep running;
               this keeps behavior simple but could be improved to rollback. */
            perror("pthread_create");
        }
    }
    return tp;
}

/* Destroy the threadpool:
   - set shutdown flag and wake all workers
   - join worker threads
   - free resources and destroy synchronization primitives */
void threadpool_destroy(threadpool_t *tp) {
    if (!tp) return;
    pthread_mutex_lock(&tp->lock);
    tp->shutdown = 1;
    /* wake all workers so they can exit when queue empties */
    pthread_cond_broadcast(&tp->not_empty);
    pthread_mutex_unlock(&tp->lock);

    /* join worker threads to ensure clean shutdown */
    for (size_t i = 0; i < tp->nworkers; ++i) pthread_join(tp->workers[i], NULL);

    free(tp->workers);
    free(tp->queue);
    pthread_mutex_destroy(&tp->lock);
    pthread_cond_destroy(&tp->not_empty);
    pthread_cond_destroy(&tp->not_full);
    free(tp->docroot);
    free(tp);
}

/* Submit a client fd to the pool:
   - blocks when queue is full until space is available or shutdown begins
   - returns 0 on success, -1 if pool is shutting down */
int threadpool_submit(threadpool_t *tp, int client_fd) {
    pthread_mutex_lock(&tp->lock);
    /* wait for space in the queue unless shutting down */
    while (tp->count == tp->capacity && !tp->shutdown) {
        pthread_cond_wait(&tp->not_full, &tp->lock);
    }
    if (tp->shutdown) {
        pthread_mutex_unlock(&tp->lock);
        return -1;
    }
    /* enqueue at tail */
    tp->queue[tp->tail] = client_fd;
    tp->tail = (tp->tail + 1) % tp->capacity;
    tp->count++;
    /* notify one waiting worker there's work available */
    pthread_cond_signal(&tp->not_empty);
    pthread_mutex_unlock(&tp->lock);
    return 0;
}
