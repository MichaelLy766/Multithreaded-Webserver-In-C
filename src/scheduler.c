#include "scheduler.h"
#include <stdlib.h>
#include <string.h>

/* internal FIFO state */
typedef struct {
    job_t *arr;
    size_t capacity;
    size_t head, tail, count;
} fifo_state;

static int fifo_push(scheduler_t *s, job_t job) {
    fifo_state *st = (fifo_state*)s->state;
    if (st->count == st->capacity) return -1;
    st->arr[st->tail] = job;
    st->tail = (st->tail + 1) % st->capacity;
    st->count++;
    return 0;
}

static int fifo_pop(scheduler_t *s, job_t *out) {
    fifo_state *st = (fifo_state*)s->state;
    if (st->count == 0) return -1;
    *out = st->arr[st->head];
    st->head = (st->head + 1) % st->capacity;
    st->count--;
    return 0;
}

static void fifo_destroy(scheduler_t *s) {
    if (!s) return;
    fifo_state *st = (fifo_state*)s->state;
    free(st->arr);
    free(st);
    free(s);
}

scheduler_t *scheduler_fifo_create(size_t capacity) {
    scheduler_t *s = calloc(1, sizeof(*s));
    if (!s) return NULL;
    fifo_state *st = calloc(1, sizeof(*st));
    if (!st) { free(s); return NULL; }
    st->arr = calloc(capacity, sizeof(job_t));
    if (!st->arr) { free(st); free(s); return NULL; }
    st->capacity = capacity;
    st->head = st->tail = st->count = 0;
    s->state = st;
    s->push = fifo_push;
    s->pop = fifo_pop;
    s->destroy = fifo_destroy;
    return s;
}