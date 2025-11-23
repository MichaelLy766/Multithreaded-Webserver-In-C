#pragma once

#include <stddef.h>
#include "threadpool.h"

/* scheduler_t: abstract scheduler interface
 * push/pop are expected to be called while holding the threadpool lock.
 * They must not block; threadpool handles blocking (condvars).
 */
typedef struct scheduler scheduler_t;

struct scheduler {
    void *state;
    int (*push)(scheduler_t *s, job_t job);   /* return 0 on success, -1 if full */
    int (*pop)(scheduler_t *s, job_t *out);    /* return 0 on success, -1 if empty */
    void (*destroy)(scheduler_t *s);
};

/* FIFO scheduler factory */
scheduler_t *scheduler_fifo_create(size_t capacity);

/* SJF scheduler factory (min-heap by est_cost ascending) */
scheduler_t *scheduler_sjf_create(size_t capacity);