#pragma once

#include <stddef.h>
#include <stdint.h>

/*
 * threadpool_t - opaque handle for the thread pool.
 *
 * The threadpool owns worker threads and a bounded queue of client FDs.
 * Use the API below to create/destroy the pool and submit client sockets.
 */
typedef struct threadpool threadpool_t;

/*
 * threadpool_create:
 *  - Create a new thread pool with `nworkers` worker threads and a
 *    task queue capacity of `queue_capacity`.
 *  - `docroot` is duplicated by the pool and provided to worker tasks;
 *    it may be NULL (pool will default to "./www").
 *  - Returns a pointer to the pool on success or NULL on allocation failure.
 *
 * Notes:
 *  - This is a best-effort create: partial failures in starting threads
 *    are printed but the function still returns a pool handle.
 */
threadpool_t *threadpool_create(size_t nworkers, size_t queue_capacity, const char *docroot);

/*
 * threadpool_destroy:
 *  - Request shutdown of the pool, wake workers, and join all threads.
 *  - Blocks until all worker threads have exited and resources are freed.
 *  - Safe to call once; passing NULL is a no-op.
 *
 * Notes:
 *  - After this returns the returned handle is no longer valid.
 */
void threadpool_destroy(threadpool_t *tp);

/*
 * Job descriptor used by schedulers and the threadpool.
 * Replace the previous "raw fd in queue" approach with job_t when you
 * want to experiment with scheduling policies (SJF/priority).
 */
typedef struct {
    int client_fd;        /* client socket FD */
    long est_cost;        /* estimated cost (e.g. file size) - application provided */
    int priority;         /* priority (higher = serve earlier) */
    uint64_t arrival_ms;  /* monotonic arrival timestamp (ms), optional */
} job_t;

/*
 * threadpool_set_scheduler:
 *  - Atomically replace the scheduler used by the threadpool. The
 *    provided scheduler instance is adopted by the pool and the old
 *    scheduler (if any) is destroyed.
 *
 * Notes:
 *  - Forward-declared here to avoid header cycles; pass a scheduler
 *    instance created by your scheduler factory (e.g. scheduler_fifo_create,
 *    scheduler_sjf_create).
 */
struct scheduler;
void threadpool_set_scheduler(threadpool_t *tp, struct scheduler *sched);

/* Submit job variants:
 * - submit a raw fd (keeps backward compatibility)
 * - submit a full job (preferred for scheduling experiments)
 *
 * Both block when the queue is full and return 0 on success, -1 if the pool
 * is shutting down or on allocation error.
 */
int threadpool_submit(threadpool_t *tp, int client_fd);
int threadpool_submit_job(threadpool_t *tp, job_t job);
