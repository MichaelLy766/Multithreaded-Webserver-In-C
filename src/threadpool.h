#pragma once

#include <stddef.h>

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

/* Submit job variants:
 * - submit a raw fd (keeps backward compatibility)
 * - submit a full job (preferred for scheduling experiments)
 *
 * Both block when the queue is full and return 0 on success, -1 if the pool
 * is shutting down or on allocation error.
 */
int threadpool_submit(threadpool_t *tp, int client_fd);
int threadpool_submit_job(threadpool_t *tp, job_t job);
