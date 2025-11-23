#pragma once
#include <stdint.h>

/* Initialize metrics subsystem (starts background printer). */
int metrics_init(void);

/* Shutdown metrics subsystem (joins background thread). */
void metrics_shutdown(void);

/* Record a completed request.
   - latency_ms: request handling latency in milliseconds
   - bytes: number of response bytes sent (approx)
   - status: HTTP status code (200, 404, 500, ...)
*/
void metrics_record_request(uint64_t latency_ms, uint64_t bytes, int status);

/* Called when a job is submitted (est may be 0 if unknown). */
void metrics_inc_submit(long est);

/* Called when a job is popped by a worker (est passed through). */
void metrics_inc_pop(long est);