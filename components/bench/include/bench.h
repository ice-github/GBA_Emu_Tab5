#pragma once
#include <stddef.h>
#include <stdint.h>

#if defined(ESP_PLATFORM)
#include "esp_attr.h" /* EXT_RAM_BSS_ATTR for large bench buffers */
#else
#define EXT_RAM_BSS_ATTR
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* Portable nanosecond timer. Host: clock_gettime. Firmware: esp_timer. */
uint64_t bench_now_ns(void);

/* Prevent the compiler from discarding a computed value. */
void bench_sink(const void *p, size_t n);
uint32_t bench_checksum32(const void *p, size_t n);

/* Workload sizes shared by host and firmware. */
#define BENCH_GBA_W 240u
#define BENCH_GBA_H 160u
#define BENCH_GBA_PIXELS (BENCH_GBA_W * BENCH_GBA_H)
#define BENCH_GBA_FRAME_BYTES (BENCH_GBA_PIXELS * 2u) /* RGB565 */

typedef struct {
    const char *id;      /* stable id, e.g. "memcpy_psram_75k" */
    const char *name;    /* human-readable */
    const char *unit;    /* "ns", "ns/iter", "MB/s" etc (info only) */
    const char *mem;     /* buffer placement on device: "psram" or "dram" */
    uint32_t warmup_iters;
    uint32_t measure_iters;
    /* Timed work. Returns an O(1) token derived from the result; the
       harness consumes it via bench_sink so the work cannot be deleted,
       but no full checksum runs inside the timed region. */
    uint32_t (*run)(uint32_t work_items);
    /* Untimed verification. Runs the full work plus a checksum and
       returns it. Called twice; equality is required for "verified". */
    uint32_t (*verify)(uint32_t work_items);
    uint32_t work_items; /* throughput basis: ACTUAL bytes/ops performed,
                            not the address span touched */
} bench_case_t;

typedef struct {
    const char *case_id;
    uint32_t iters;
    double avg_ns;
    double med_ns;
    double p95_ns;
    double p99_ns;
    double max_ns;
    double work_per_s; /* work_items / (avg_ns/1e9) */
    uint32_t checksum; /* xor of per-iter checksums */
} bench_result_t;

/* Runs all registered cases, emitting one JSON object per line via `emit`. */
typedef void (*bench_emit_fn)(const char *json_line, void *ctx);
void bench_register(const bench_case_t *c);
/* Explicit registration: static-library constructors are dropped by
   --gc-sections, so both mains must call this before bench_run_all. */
void bench_register_all(void);
void bench_run_all(bench_emit_fn emit, void *ctx,
                   const char *device, const char *config,
                   const char *git_rev, const char *core_rev);

/* Computes stats over `ns[0..n)` into result fields. Exported for unit tests
   on the host (tests link bench_core.c directly). */
void bench_stats(const uint64_t *ns, uint32_t n, uint32_t work_items,
                 bench_result_t *out);

#ifdef __cplusplus
}
#endif
