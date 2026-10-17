#include "bench.h"
#include <stdlib.h>
#include <string.h>

#if defined(ESP_PLATFORM)
#include "esp_heap_caps.h"
#define PLACEMENT_INTERNAL (MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)
#define PLACEMENT_PSRAM (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)
static void *placement_alloc(size_t size, uint32_t caps) {
    return heap_caps_malloc(size, caps);
}
#else
static void *placement_alloc(size_t size, uint32_t caps) {
    (void)caps;
    return malloc(size);
}
#define PLACEMENT_INTERNAL 0u
#define PLACEMENT_PSRAM 0u
#endif

#define MEMPLACEMENT_BYTES (16u * 1024u)

typedef struct {
    unsigned char *src;
    unsigned char *dst;
} placement_buffers_t;

static placement_buffers_t s_internal;
static placement_buffers_t s_psram;

static uint32_t init_source(unsigned char *dst) {
    uint32_t x = 0x6d2b79f5u;
    for (size_t i = 0; i < MEMPLACEMENT_BYTES; i++) {
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        dst[i] = (unsigned char)x;
    }
    return x;
}

static uint32_t run_copy(placement_buffers_t *buffers, uint32_t bytes) {
    if (bytes > MEMPLACEMENT_BYTES) bytes = MEMPLACEMENT_BYTES;
    memcpy(buffers->dst, buffers->src, bytes);
    return *(volatile uint32_t *)buffers->dst;
}

static uint32_t verify_copy(placement_buffers_t *buffers, uint32_t bytes) {
    if (bytes > MEMPLACEMENT_BYTES) bytes = MEMPLACEMENT_BYTES;
    memcpy(buffers->dst, buffers->src, bytes);
    return bench_checksum32(buffers->dst, bytes);
}

static uint32_t run_read(placement_buffers_t *buffers, uint32_t bytes) {
    if (bytes > MEMPLACEMENT_BYTES) bytes = MEMPLACEMENT_BYTES;
    return bench_checksum32(buffers->src, bytes);
}

static uint32_t run_fill(placement_buffers_t *buffers, uint32_t bytes) {
    if (bytes > MEMPLACEMENT_BYTES) bytes = MEMPLACEMENT_BYTES;
    memset(buffers->dst, 0xa5, bytes);
    return *(volatile uint32_t *)buffers->dst;
}

static uint32_t verify_fill(placement_buffers_t *buffers, uint32_t bytes) {
    if (bytes > MEMPLACEMENT_BYTES) bytes = MEMPLACEMENT_BYTES;
    memset(buffers->dst, 0xa5, bytes);
    return bench_checksum32(buffers->dst, bytes);
}

#define DEFINE_PLACEMENT_RUNNERS(suffix, buffers) \
    static uint32_t run_copy_##suffix(uint32_t n) { return run_copy(&(buffers), n); } \
    static uint32_t verify_copy_##suffix(uint32_t n) { return verify_copy(&(buffers), n); } \
    static uint32_t run_read_##suffix(uint32_t n) { return run_read(&(buffers), n); } \
    static uint32_t run_fill_##suffix(uint32_t n) { return run_fill(&(buffers), n); } \
    static uint32_t verify_fill_##suffix(uint32_t n) { return verify_fill(&(buffers), n); }

DEFINE_PLACEMENT_RUNNERS(internal, s_internal)
DEFINE_PLACEMENT_RUNNERS(psram, s_psram)

#define PLACEMENT_CASES(suffix, label) \
    {"memplace_copy_" #suffix "_16k", "hot/reused 16KiB " label " to " label " copy", "B/s", label, 10, 100, run_copy_##suffix, verify_copy_##suffix, MEMPLACEMENT_BYTES}, \
    {"memplace_read_" #suffix "_16k", "hot/reused 16KiB " label " sequential read (FNV-1a)", "B/s", label, 10, 100, run_read_##suffix, run_read_##suffix, MEMPLACEMENT_BYTES}, \
    {"memplace_fill_" #suffix "_16k", "hot/reused 16KiB " label " fill", "B/s", label, 10, 100, run_fill_##suffix, verify_fill_##suffix, MEMPLACEMENT_BYTES}

static const bench_case_t kCases[] = {
    PLACEMENT_CASES(internal, "internal-dram"),
    PLACEMENT_CASES(psram, "psram"),
};

static int alloc_buffers(placement_buffers_t *buffers, uint32_t caps) {
    buffers->src = placement_alloc(MEMPLACEMENT_BYTES, caps);
    buffers->dst = placement_alloc(MEMPLACEMENT_BYTES, caps);
    if (!buffers->src || !buffers->dst) {
        free(buffers->src);
        free(buffers->dst);
        buffers->src = buffers->dst = NULL;
        return 0;
    }
    init_source(buffers->src);
    return 1;
}

void bench_cases_memplacement_register(void) {
    if (!alloc_buffers(&s_internal, PLACEMENT_INTERNAL)) return;
    if (!alloc_buffers(&s_psram, PLACEMENT_PSRAM)) {
        free(s_internal.src);
        free(s_internal.dst);
        s_internal.src = s_internal.dst = NULL;
        return;
    }
    /* Identical initial states ensure placement variants have equal checksums. */
    memcpy(s_psram.src, s_internal.src, MEMPLACEMENT_BYTES);
    for (size_t i = 0; i < sizeof(kCases) / sizeof(kCases[0]); i++)
        bench_register(&kCases[i]);
}
