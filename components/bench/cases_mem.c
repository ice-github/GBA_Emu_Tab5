#include "bench.h"
#include <stdlib.h>
#include <string.h>

/* Buffers sized for one GBA frame plus headroom. On device these live in
   PSRAM (EXT_RAM_BSS_ATTR): ~1MB total does not fit internal DRAM. */
static EXT_RAM_BSS_ATTR unsigned char s_src[128 * 1024];
static EXT_RAM_BSS_ATTR unsigned char s_dst[128 * 1024];

static void prng_fill(unsigned char *p, size_t n, uint32_t seed) {
    uint32_t x = seed ? seed : 0x9e3779b9u;
    for (size_t i = 0; i < n; i++) {
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        p[i] = (unsigned char)(x & 0xffu);
    }
}

static uint32_t run_memcpy(uint32_t bytes) {
    if (bytes > sizeof(s_src)) bytes = sizeof(s_src);
    memcpy(s_dst, s_src, bytes);
    return bench_checksum32(s_dst, bytes);
}

static uint32_t run_memset(uint32_t bytes) {
    if (bytes > sizeof(s_dst)) bytes = sizeof(s_dst);
    memset(s_dst, 0x5a, bytes);
    return bench_checksum32(s_dst, bytes);
}

static uint32_t run_checksum(uint32_t bytes) {
    if (bytes > sizeof(s_src)) bytes = sizeof(s_src);
    return bench_checksum32(s_src, bytes);
}

static uint32_t run_prng_fill(uint32_t bytes) {
    if (bytes > sizeof(s_dst)) bytes = sizeof(s_dst);
    prng_fill(s_dst, bytes, 0x12345678u);
    return bench_checksum32(s_dst, bytes);
}

/* Scatter-ish read: strided 4B loads summed, to contrast with memcpy. */
static uint32_t run_strided_read(uint32_t bytes) {
    if (bytes > sizeof(s_src)) bytes = sizeof(s_src);
    volatile uint32_t acc = 0;
    for (uint32_t i = 0; i + 4 <= bytes; i += 64)
        acc += *(volatile uint32_t *)(s_src + i);
    return acc;
}

static const bench_case_t kCases[] = {
    {"memcpy_75k", "memcpy one GBA frame", "B/s", 10, 100, run_memcpy,
     BENCH_GBA_FRAME_BYTES},
    {"memset_75k", "memset one GBA frame", "B/s", 10, 100, run_memset,
     BENCH_GBA_FRAME_BYTES},
    {"checksum_75k", "FNV-1a over one GBA frame", "B/s", 10, 100,
     run_checksum, BENCH_GBA_FRAME_BYTES},
    {"prng_fill_75k", "xorshift fill one GBA frame", "B/s", 5, 50,
     run_prng_fill, BENCH_GBA_FRAME_BYTES},
    {"strided_read_75k", "64B-stride 4B reads over frame", "B/s", 10, 100,
     run_strided_read, BENCH_GBA_FRAME_BYTES},
};

void bench_cases_mem_register(void) {
    prng_fill(s_src, sizeof(s_src), 0xabcdef01u);
    memset(s_dst, 0, sizeof(s_dst));
    for (size_t i = 0; i < sizeof(kCases) / sizeof(kCases[0]); i++)
        bench_register(&kCases[i]);
}
