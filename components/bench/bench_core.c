#include "bench.h"
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#if defined(ESP_PLATFORM)
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
uint64_t bench_now_ns(void) { return (uint64_t)esp_timer_get_time() * 1000u; }
/* Yield between cases. A device run takes far longer than the 5 s task
 * watchdog window, and the bench never blocks, so without this the watchdog
 * fires, dumps registers, and can corrupt a JSON line on the way out. */
static void bench_case_gap(void) { vTaskDelay(1); }
#else
#include <time.h>
uint64_t bench_now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000u + (uint64_t)ts.tv_nsec;
}
/* The host has no watchdog and no competing idle task. */
static void bench_case_gap(void) {}
#endif

void bench_sink(const void *p, size_t n) {
    volatile const unsigned char *q = (volatile const unsigned char *)p;
    volatile unsigned char acc = 0;
    for (size_t i = 0; i < n; i++) acc ^= q[i];
    (void)acc;
}

uint32_t bench_checksum32(const void *p, size_t n) {
    const unsigned char *q = (const unsigned char *)p;
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < n; i++) {
        h ^= q[i];
        h *= 16777619u;
    }
    return h;
}

#define BENCH_MAX_CASES 64
static const bench_case_t *s_cases[BENCH_MAX_CASES];
static uint32_t s_ncases;

void bench_register(const bench_case_t *c) {
    if (s_ncases < BENCH_MAX_CASES) s_cases[s_ncases++] = c;
}

/* Per-file registration entry points (defined in cases_*.c). */
void bench_cases_mem_register(void);
void bench_cases_memplacement_register(void);
void bench_cases_branch_register(void);
void bench_cases_pixel_register(void);
void bench_cases_scale_register(void);
void bench_cases_rot45x_register(void);
void bench_cases_rotsplit_register(void);
void bench_cases_store_register(void);

void bench_register_all(void) {
    s_ncases = 0;
    bench_cases_mem_register();
    bench_cases_memplacement_register();
    bench_cases_branch_register();
    bench_cases_pixel_register();
    bench_cases_scale_register();
    bench_cases_rot45x_register();
    bench_cases_rotsplit_register();
    bench_cases_store_register();
}

static int cmp_u64(const void *a, const void *b) {
    uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b;
    return (x > y) - (x < y);
}

static double percentile_ns(const uint64_t *sorted, uint32_t n, double p) {
    if (n == 0) return 0.0;
    double rank = p * (double)(n - 1);
    uint32_t lo = (uint32_t)rank;
    uint32_t hi = lo + 1 < n ? lo + 1 : lo;
    double frac = rank - (double)lo;
    return (double)sorted[lo] * (1.0 - frac) + (double)sorted[hi] * frac;
}

void bench_stats(const uint64_t *ns, uint32_t n, uint32_t work_items,
                 bench_result_t *out) {
    static uint64_t tmp[4096];
    if (n > 4096) n = 4096;
    memcpy(tmp, ns, n * sizeof(uint64_t));
    qsort(tmp, n, sizeof(uint64_t), cmp_u64);
    double sum = 0.0;
    for (uint32_t i = 0; i < n; i++) sum += (double)tmp[i];
    out->iters = n;
    out->avg_ns = n ? sum / (double)n : 0.0;
    out->med_ns = percentile_ns(tmp, n, 0.50);
    out->p95_ns = percentile_ns(tmp, n, 0.95);
    out->p99_ns = percentile_ns(tmp, n, 0.99);
    out->max_ns = n ? (double)tmp[n - 1] : 0.0;
    out->work_per_s =
        out->avg_ns > 0.0 ? (double)work_items * 1e9 / out->avg_ns : 0.0;
}

#define BENCH_MAX_SAMPLES 512

void bench_run_all(bench_emit_fn emit, void *ctx, const char *device,
                   const char *config, const char *git_rev,
                   const char *core_rev) {
    char line[1024];
    for (uint32_t ci = 0; ci < s_ncases; ci++) {
        const bench_case_t *c = s_cases[ci];
        for (uint32_t w = 0; w < c->warmup_iters; w++) {
            uint32_t cs = c->run(c->work_items);
            bench_sink(&cs, sizeof(cs));
        }
        static uint64_t samples[BENCH_MAX_SAMPLES];
        uint32_t n = c->measure_iters < BENCH_MAX_SAMPLES ? c->measure_iters
                                                          : BENCH_MAX_SAMPLES;
        uint32_t token = 0;
        for (uint32_t i = 0; i < n; i++) {
            uint64_t t0 = bench_now_ns();
            uint32_t tok = c->run(c->work_items);
            uint64_t t1 = bench_now_ns();
            samples[i] = t1 - t0;
            /* Mix tokens so identical per-iter values cannot cancel out
               (xor would vanish for even n). */
            token = token * 1000003u + tok + i;
        }
        bench_sink(&token, sizeof(token));
        /* Untimed verification, twice: determinism is required. */
        uint32_t v0 = c->verify(c->work_items);
        uint32_t v1 = c->verify(c->work_items);
        int verified = (v0 == v1);
        bench_result_t r;
        memset(&r, 0, sizeof(r));
        r.case_id = c->id;
        r.checksum = v0;
        bench_stats(samples, n, c->work_items, &r);
        /* Sanity: zero-time runs are failures, but we still emit them so
           the runner can reject them explicitly. */
        int nch = snprintf(line, sizeof(line),
                           "{\"device\":\"%s\",\"config\":\"%s\","
                           "\"git_rev\":\"%s\",\"core_rev\":\"%s\","
                           "\"case\":\"%s\",\"mem\":\"%s\","
                           "\"iters\":%" PRIu32 ","
                           "\"avg_ns\":%.1f,\"med_ns\":%.1f,"
                           "\"p95_ns\":%.1f,\"p99_ns\":%.1f,\"max_ns\":%.1f,"
                           "\"work_per_s\":%.1f,\"checksum\":\"%08" PRIX32
                           "\",\"verified\":%s}",
                           device, config, git_rev, core_rev, c->id, c->mem,
                           r.iters,
                           r.avg_ns, r.med_ns, r.p95_ns, r.p99_ns, r.max_ns,
                           r.work_per_s, r.checksum,
                           verified ? "true" : "false");
        if (nch > 0 && (size_t)nch < sizeof(line)) emit(line, ctx);
        bench_case_gap();
    }
}
