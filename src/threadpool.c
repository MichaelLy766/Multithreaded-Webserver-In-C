#include "threadpool.h"
#include "http.h"
#include "scheduler.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>

/* threadpool structure now uses scheduler_t for job management */
struct threadpool {
    pthread_t *workers;
    size_t nworkers;
    scheduler_t *sched;          /* scheduler instance (FIFO by default) */
    size_t capacity;
    pthread_mutex_t lock;
    pthread_cond_t not_empty;
    pthread_cond_t not_full;
    int shutdown;
    char *docroot;
};

static uint64_t now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)(ts.tv_sec) * 1000 + (ts.tv_nsec / 1000000);
}

/* Worker main loop:
   - wait for a job to be available
   - pop job via scheduler_pop, process it, then close fd
   - exit when shutdown is set and no work is left */
static void *worker_main(void *arg) {
    struct threadpool *tp = (struct.threadpool*)arg;
    while (1) {
        pthread_mutex_lock(&tp->lock);
        while (1) {
            /* try pop if available */
            job_t job;
            if (tp->sched->pop(tp->sched, &job) == 0) {
                /* we got work */
                /* signal producers that space is available */
                pthread_cond_signal(&tp->not_full);
                pthread_mutex_unlock(&tp->lock);

                /* process job */
                handle_client(job.client_fd, tp->docroot);
                close(job.client_fd);
                /* loop back to get next job */
                pthread_mutex_lock(&tp->lock);
                continue;
            }

            /* no job available */
            if (tp->shutdown) break;
            /* wait for work or shutdown */
            pthread_cond_wait(&tp->not_empty, &tp->lock);
        }

        /* shutting down - ensure queue empty */
        if (tp->shutdown) {
            /* drain any remaining jobs if desired; current behavior: exit if none */
            job_t leftover;
            while (tp->sched->pop(tp->sched, &leftover) == 0) {
                pthread_mutex_unlock(&tp->lock);
                handle_client(leftover.client_fd, tp->docroot);
                close(leftover.client_fd);
                pthread_mutex_lock(&tp->lock);
            }
            pthread_mutex_unlock(&tp->lock);
            break;
        }
        pthread_mutex_unlock(&tp->lock);
    }
    return NULL;
}

/* Create a threadpool with FIFO scheduler by default */
threadpool_t *threadpool_create(size_t nworkers, size_t queue_capacity, const char *docroot) {
    struct threadpool *tp = calloc(1, sizeof(*tp));
    if (!tp) return NULL;
    tp->nworkers = nworkers;
    tp->workers = calloc(nworkers, sizeof(pthread_t));
    tp->capacity = queue_capacity;
    pthread_mutex_init(&tp->lock, NULL);
    pthread_cond_init(&tp->not_empty, NULL);
    pthread_cond_init(&tp->not_full, NULL);
    tp->shutdown = 0;
    tp->docroot = strdup(docroot ? docroot : "./www");

    tp->sched = scheduler_fifo_create(queue_capacity);
    if (!tp->sched) {
        perror("scheduler_fifo_create");
        /* continue though - thread creation may still work but submissions will fail */
    }

    for (size_t i = 0; i < nworkers; ++i) {
        if (pthread_create(&tp->workers[i], NULL, worker_main, tp) != 0) {
            perror("pthread_create");
        }
    }
    return tp;
}

void threadpool_destroy(threadpool_t *tp) {
    if (!tp) return;
    pthread_mutex_lock(&tp->lock);
    tp->shutdown = 1;
    pthread_cond_broadcast(&tp->not_empty);
    pthread_mutex_unlock(&tp->lock);

    for (size_t i = 0; i < tp->nworkers; ++i) pthread_join(tp->workers[i], NULL);

    if (tp->sched && tp->sched->destroy) tp->sched->destroy(tp->sched);
    free(tp->workers);
    pthread_mutex_destroy(&tp->lock);
    pthread_cond_destroy(&tp->not_empty);
    pthread_cond_destroy(&tp->not_full);
    free(tp->docroot);
    free(tp);
}

/* Submit a raw client fd (backwards-compatible wrapper) */
int threadpool_submit(threadpool_t *tp, int client_fd) {
    if (!tp) return -1;
    job_t j = {.client_fd = client_fd, .est_cost = 0, .priority = 0, .arrival_ms = now_ms()};
    return threadpool_submit_job(tp, j);
}

/* Submit a full job (preferred). Blocks when scheduler is full. */
int threadpool_submit_job(threadpool_t *tp, job_t job) {
    if (!tp) return -1;
    pthread_mutex_lock(&tp->lock);
    while (1) {
        /* try to push; if full, wait (unless shutting down) */
        if (tp->shutdown) {
            pthread_mutex_unlock(&tp->lock);
            return -1;
        }
        if (tp->sched->push(tp->sched, job) == 0) {
            /* success: notify a worker */
            pthread_cond_signal(&tp->not_empty);
            pthread_mutex_unlock(&tp->lock);
            return 0;
        }
        /* full -> wait for space */
        pthread_cond_wait(&tp->not_full, &tp->lock);
    }
}
