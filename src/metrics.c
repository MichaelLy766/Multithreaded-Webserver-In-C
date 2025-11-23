#include "metrics.h"
#include <stdatomic.h>
#include <pthread.h>
#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include <time.h>

static atomic_uint_fast64_t submits_total;
static atomic_uint_fast64_t submits_est0;
static atomic_uint_fast64_t pops_total;

static atomic_uint_fast64_t requests_total;
static atomic_uint_fast64_t bytes_total;
static atomic_uint_fast64_t errors_total;
static atomic_uint_fast64_t sum_latency_ms;

static pthread_t metrics_thread;
static atomic_int metrics_running;

static uint64_t now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static void *metrics_thread_fn(void *arg) {
    (void)arg;
    uint64_t prev_reqs = 0, prev_bytes = 0;
    const int interval = 5; /* seconds */
    while (atomic_load(&metrics_running)) {
        sleep(interval);
        uint64_t reqs = atomic_load(&requests_total);
        uint64_t bytes = atomic_load(&bytes_total);
        uint64_t errs = atomic_load(&errors_total);
        uint64_t sumlat = atomic_load(&sum_latency_ms);
        uint64_t subs = atomic_load(&submits_total);
        uint64_t subs0 = atomic_load(&submits_est0);
        uint64_t pops = atomic_load(&pops_total);

        uint64_t delta_reqs = reqs - prev_reqs;
        uint64_t delta_bytes = bytes - prev_bytes;
        double reqs_per_s = (double)delta_reqs / interval;
        double mb_per_s = ((double)delta_bytes / (1024.0 * 1024.0)) / interval;
        double avg_latency = reqs ? ((double)sumlat / (double)reqs) : 0.0;
        double est0_frac = subs ? ((double)subs0 / (double)subs) * 100.0 : 0.0;

        fprintf(stderr,
                "[metrics] ts=%llu reqs_total=%llu req/s=%.2f MB/s=%.2f avgLat=%.2fms errors=%llu submits=%llu est0%%=%.1f pops=%llu\n",
                (unsigned long long)now_ms(),
                (unsigned long long)reqs,
                reqs_per_s,
                mb_per_s,
                avg_latency,
                (unsigned long long)errs,
                (unsigned long long)subs,
                est0_frac,
                (unsigned long long)pops);
        fflush(stderr);

        prev_reqs = reqs;
        prev_bytes = bytes;
    }
    return NULL;
}

int metrics_init(void) {
    atomic_init(&submits_total, 0);
    atomic_init(&submits_est0, 0);
    atomic_init(&pops_total, 0);
    atomic_init(&requests_total, 0);
    atomic_init(&bytes_total, 0);
    atomic_init(&errors_total, 0);
    atomic_init(&sum_latency_ms, 0);
    atomic_store(&metrics_running, 1);
    if (pthread_create(&metrics_thread, NULL, metrics_thread_fn, NULL) != 0) {
        perror("metrics thread create");
        atomic_store(&metrics_running, 0);
        return -1;
    }
    return 0;
}

void metrics_shutdown(void) {
    atomic_store(&metrics_running, 0);
    pthread_join(metrics_thread, NULL);
}

void metrics_record_request(uint64_t latency_ms, uint64_t bytes, int status) {
    atomic_fetch_add(&requests_total, 1);
    atomic_fetch_add(&bytes_total, bytes);
    atomic_fetch_add(&sum_latency_ms, latency_ms);
    if (status < 200 || status >= 400) atomic_fetch_add(&errors_total, 1);
}

void metrics_inc_submit(long est) {
    atomic_fetch_add(&submits_total, 1);
    if (est <= 0) atomic_fetch_add(&submits_est0, 1);
}

void metrics_inc_pop(long est) {
    (void)est;
    atomic_fetch_add(&pops_total, 1);
}