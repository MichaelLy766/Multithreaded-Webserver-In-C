#include "scheduler.h"
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

/* internal SJF heap state */
typedef struct {
    job_t *arr;
    size_t capacity;
    size_t count;
} sjf_state;

/* compare: smaller est_cost is higher priority; break ties by arrival_ms */
static int job_less(const job_t *a, const job_t *b) {
    if (a->est_cost < b->est_cost) return 1;
    if (a->est_cost > b->est_cost) return 0;
    return a->arrival_ms < b->arrival_ms;
}

static void swap_jobs(job_t *a, job_t *b) {
    job_t tmp = *a;
    *a = *b;
    *b = tmp;
}

static void sift_up(sjf_state *st, size_t idx) {
    while (idx > 0) {
        size_t parent = (idx - 1) >> 1;
        if (job_less(&st->arr[idx], &st->arr[parent])) {
            swap_jobs(&st->arr[idx], &st->arr[parent]);
            idx = parent;
        } else break;
    }
}

static void sift_down(sjf_state *st, size_t idx) {
    size_t n = st->count;
    while (1) {
        size_t l = idx * 2 + 1;
        size_t r = l + 1;
        size_t smallest = idx;
        if (l < n && job_less(&st->arr[l], &st->arr[smallest])) smallest = l;
        if (r < n && job_less(&st->arr[r], &st->arr[smallest])) smallest = r;
        if (smallest == idx) break;
        swap_jobs(&st->arr[idx], &st->arr[smallest]);
        idx = smallest;
    }
}

static int sjf_push(scheduler_t *s, job_t job) {
    sjf_state *st = (sjf_state*)s->state;
    if (st->count == st->capacity) return -1;
    st->arr[st->count] = job;
    sift_up(st, st->count);
    st->count++;
    return 0;
}

static int sjf_pop(scheduler_t *s, job_t *out) {
    sjf_state *st = (sjf_state*)s->state;
    if (st->count == 0) return -1;
    *out = st->arr[0];
    st->count--;
    if (st->count > 0) {
        st->arr[0] = st->arr[st->count];
        sift_down(st, 0);
    }
    return 0;
}

static void sjf_destroy(scheduler_t *s) {
    if (!s) return;
    sjf_state *st = (sjf_state*)s->state;
    free(st->arr);
    free(st);
    free(s);
}

scheduler_t *scheduler_sjf_create(size_t capacity) {
    scheduler_t *s = calloc(1, sizeof(*s));
    if (!s) return NULL;
    sjf_state *st = calloc(1, sizeof(*st));
    if (!st) { free(s); return NULL; }
    st->arr = calloc(capacity, sizeof(job_t));
    if (!st->arr) { free(st); free(s); return NULL; }
    st->capacity = capacity;
    st->count = 0;
    s->state = st;
    s->push = sjf_push;
    s->pop = sjf_pop;
    s->destroy = sjf_destroy;
    return s;
}