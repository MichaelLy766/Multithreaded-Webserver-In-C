#pragma once

#include <stddef.h>

typedef struct threadpool threadpool_t;

threadpool_t *threadpool_create(size_t nworkers, size_t queue_capacity, const char *docroot);
void threadpool_destroy(threadpool_t *tp);
int threadpool_submit(threadpool_t *tp, int client_fd);
