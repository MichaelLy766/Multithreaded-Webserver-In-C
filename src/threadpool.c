#include "threadpool.h"
#include "http.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

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

static void *worker_main(void *arg) {
    struct threadpool *tp = (struct threadpool*)arg;
    while (1) {
        pthread_mutex_lock(&tp->lock);
        while (tp->count == 0 && !tp->shutdown) {
            pthread_cond_wait(&tp->not_empty, &tp->lock);
        }
        if (tp->shutdown && tp->count == 0) {
            pthread_mutex_unlock(&tp->lock);
            break;
        }
        int client_fd = tp->queue[tp->head];
        tp->head = (tp->head + 1) % tp->capacity;
        tp->count--;
        pthread_cond_signal(&tp->not_full);
        pthread_mutex_unlock(&tp->lock);

        // handle request
        handle_client(client_fd, tp->docroot);
        close(client_fd);
    }
    return NULL;
}

threadpool_t *threadpool_create(size_t nworkers, size_t queue_capacity, const char *docroot) {
    struct threadpool *tp = calloc(1, sizeof(*tp));
    if (!tp) return NULL;
    tp->nworkers = nworkers;
    tp->workers = calloc(nworkers, sizeof(pthread_t));
    tp->queue = calloc(queue_capacity, sizeof(int));
    tp->capacity = queue_capacity;
    pthread_mutex_init(&tp->lock, NULL);
    pthread_cond_init(&tp->not_empty, NULL);
    pthread_cond_init(&tp->not_full, NULL);
    tp->docroot = strdup(docroot ? docroot : "./www");

    for (size_t i = 0; i < nworkers; ++i) {
        if (pthread_create(&tp->workers[i], NULL, worker_main, tp) != 0) {
            // TODO: handle partial creation
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

    free(tp->workers);
    free(tp->queue);
    pthread_mutex_destroy(&tp->lock);
    pthread_cond_destroy(&tp->not_empty);
    pthread_cond_destroy(&tp->not_full);
    free(tp->docroot);
    free(tp);
}

int threadpool_submit(threadpool_t *tp, int client_fd) {
    pthread_mutex_lock(&tp->lock);
    while (tp->count == tp->capacity && !tp->shutdown) {
        pthread_cond_wait(&tp->not_full, &tp->lock);
    }
    if (tp->shutdown) {
        pthread_mutex_unlock(&tp->lock);
        return -1;
    }
    tp->queue[tp->tail] = client_fd;
    tp->tail = (tp->tail + 1) % tp->capacity;
    tp->count++;
    pthread_cond_signal(&tp->not_empty);
    pthread_mutex_unlock(&tp->lock);
    return 0;
}
