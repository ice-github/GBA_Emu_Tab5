/* Raw PSRAM store/copy patterns, used to explain the scaler numbers.
 *
 * The 3x scaler moves ~1.15 MB per frame and needs ~10.4 ms (Tab5), while a
 * plain 153 KB frame copy needs ~0.5 ms. These cases isolate which property of
 * the scaler's access pattern costs the difference:
 *
 *   copy_u16/u32_691k   scalar copy loop vs libc memcpy, to see whether the
 *                       loop itself or the bytes are the cost
 *   fill_u16/u32_691k   store width, with values that vary per iteration
 *   scaler_*_230k       the scaler's actual inner loop: three stores per source
 *                       pixel, 16-bit vs paired 32-bit
 *   memcpy_rows_1440    the scaler's row-duplication pattern (320 x 1440 B)
 *
 * Values always come from a source buffer rather than a constant, so the
 * compiler cannot recognise the loop as a fill and vectorise or fall back to
 * memset: on the host that turned every constant-fill case into the same
 * 60 GB/s memset and hid the store-width difference entirely.
 *
 * All cases touch the same 720x480 RGB565 extent (691,200 bytes) in PSRAM, so
 * their throughputs are comparable. Portable: no ESP-IDF, no display.
 */
#include "bench.h"

#include <stdint.h>
#include <string.h>

enum {
    ROWS = 480,        /* destination rows in the panel region */
    COLS = 720,        /* uint16_t per row */
    WORDS = COLS / 2,  /* uint32_t per row */
    PIXELS = ROWS * COLS,          /* 345600 px = 691200 B */
    BYTES = PIXELS * 2,            /* 691200 */
    N_WORDS = PIXELS / 2,          /* 172800 */
    SRC_PX = BENCH_GBA_W * BENCH_GBA_H, /* 38400 */
    SRC_ROWS = BENCH_GBA_H,        /* 160 source rows */
    ROW_BYTES = COLS * 2,          /* 1440 */
};

static EXT_RAM_BSS_ATTR uint32_t s_dst[(PIXELS + 1) / 2];
static EXT_RAM_BSS_ATTR uint32_t s_big[(PIXELS + 1) / 2];
static EXT_RAM_BSS_ATTR uint16_t s_src[SRC_PX];
static EXT_RAM_BSS_ATTR uint16_t s_row[COLS];
static EXT_RAM_BSS_ATTR uint32_t s_row32[WORDS];
/* 76,800 bytes: the same size as a frame, and small enough to sit in the
 * 128 KB L2, so reading it measures cache throughput rather than PSRAM. */
static EXT_RAM_BSS_ATTR uint32_t s_small[SRC_PX / 2];
/* 8-byte-aligned view of the full extent, for the 64-bit store case. */
static EXT_RAM_BSS_ATTR uint64_t s_dst64[(PIXELS * 2) / 8];

/* Keeps load results from being optimised away without using a checksum. */
static volatile uint32_t s_read_sink;

static uint16_t *const s_dst16 = (uint16_t *)s_dst;
static const uint16_t *const s_big16 = (const uint16_t *)s_big;
static const uint32_t *const s_big32 = (const uint32_t *)s_big;

static void store_src_init(void) {
    for (int i = 0; i < PIXELS / 2; i++) {
        uint32_t x = (uint32_t)i * 2654435761u + 0x9e3779b9u;
        x ^= x >> 15;
        s_big[i] = x;
    }
    for (int i = 0; i < SRC_PX; i++) {
        uint32_t x = (uint32_t)i * 40503u + 17u;
        x ^= x >> 11;
        s_src[i] = (uint16_t)(x >> 8);
    }
    for (int i = 0; i < COLS; i++) {
        uint32_t v = (uint32_t)(i * 2654435761u) >> 16;
        s_row[i] = (uint16_t)v;
        if (i < WORDS) s_row32[i] = v | (v << 16);
    }
    for (int i = 0; i < SRC_PX / 2; i++) {
        uint32_t x = (uint32_t)i * 2246822519u + 101u;
        x ^= x >> 13;
        s_small[i] = x;
    }
}

static uint32_t dst_token(void) { return *(volatile const uint32_t *)s_dst; }

static uint32_t dst_checksum(void) {
    return bench_checksum32(s_dst16, (size_t)BYTES);
}

/* --- scalar copy loops vs libc memcpy --- */

static void copy_u16_work(void) {
    for (int i = 0; i < PIXELS; i++) s_dst16[i] = s_big16[i];
}

static void copy_u32_work(void) {
    for (int i = 0; i < N_WORDS; i++) s_dst[i] = s_big32[i];
}

static void memcpy_work(void) { memcpy(s_dst, s_big, (size_t)BYTES); }

/* --- store width with varying values --- */

static void fill_u16_work(void) {
    for (int r = 0; r < ROWS; r++) {
        uint16_t *row = s_dst16 + (size_t)r * COLS;
        for (int c = 0; c < COLS; c++) row[c] = s_row[c];
    }
}

static void fill_u32_work(void) {
    for (int r = 0; r < ROWS; r++) {
        uint32_t *row = s_dst + (size_t)r * WORDS;
        for (int c = 0; c < WORDS; c++) row[c] = s_row32[c];
    }
}

/* --- store width taken further, plus the two bandwidth references --- */

static void fill_u64_work(void) {
    uint64_t v = 0x7bcf7bcf7bcf7bcfull;
    for (int r = 0; r < ROWS; r++) {
        uint64_t *row = s_dst64 + (size_t)r * (COLS / 4);
        for (int c = 0; c < COLS / 4; c++) row[c] = v + (uint64_t)c;
    }
}

/* What an optimised wide-store writer achieves on this memory system. */
static void memset_work(void) { memset(s_dst16, 0x7b, (size_t)BYTES); }

static void read_u32_691k_work(void) {
    uint32_t acc = 0;
    for (int i = 0; i < N_WORDS; i++) acc += s_big32[i];
    s_read_sink = acc;
}

static void read_u32_76k_work(void) {
    uint32_t acc = 0;
    for (int i = 0; i < SRC_PX / 2; i++) acc += s_small[i];
    s_read_sink = acc;
}

/* --- the scaler's inner loop: three stores per source pixel (230400 B) --- */

static void block3_u16_work(void) {
    for (int sx = 0; sx < SRC_PX; sx++) {
        uint16_t v = s_src[sx];
        uint16_t *p = s_dst16 + (size_t)sx * 3;
        p[0] = v;
        p[1] = v;
        p[2] = v;
    }
}

static void block3_u32_work(void) {
    for (int sx = 0; sx < SRC_PX; sx += 2) {
        uint32_t a = s_src[sx];
        uint32_t b = s_src[sx + 1];
        uint32_t *p = s_dst + (size_t)(sx / 2) * 3;
        p[0] = a | (a << 16);
        p[1] = (b << 16) | a;
        p[2] = b | (b << 16);
    }
}

/* --- the row-duplication memcpy pattern (320 x 1440 B = 460800 B) --- */

static void memcpy_rows_work(void) {
    uint32_t *p = s_dst;
    for (int r = 0; r < SRC_ROWS; r++) {
        memcpy(p + WORDS, s_row, ROW_BYTES);
        memcpy(p + 2 * WORDS, s_row, ROW_BYTES);
        p += (size_t)3 * WORDS;
    }
}

/* --- run/verify wrappers --- */

static uint32_t run_copy_u16(uint32_t i) { (void)i; copy_u16_work(); return dst_token(); }
static uint32_t run_copy_u32(uint32_t i) { (void)i; copy_u32_work(); return dst_token(); }
static uint32_t run_memcpy(uint32_t i) { (void)i; memcpy_work(); return dst_token(); }
static uint32_t run_fill_u16(uint32_t i) { (void)i; fill_u16_work(); return dst_token(); }
static uint32_t run_fill_u32(uint32_t i) { (void)i; fill_u32_work(); return dst_token(); }
static uint32_t run_fill_u64(uint32_t i) {
    (void)i;
    fill_u64_work();
    return (uint32_t)s_dst64[0] ^ (uint32_t)s_dst64[COLS / 4];
}
static uint32_t run_memset(uint32_t i) { (void)i; memset_work(); return dst_token(); }
static uint32_t run_read_691k(uint32_t i) {
    (void)i;
    read_u32_691k_work();
    return s_read_sink;
}
static uint32_t run_read_76k(uint32_t i) {
    (void)i;
    read_u32_76k_work();
    return s_read_sink;
}
static uint32_t run_block3_u16(uint32_t i) { (void)i; block3_u16_work(); return dst_token(); }
static uint32_t run_block3_u32(uint32_t i) { (void)i; block3_u32_work(); return dst_token(); }
static uint32_t run_memcpy_rows(uint32_t i) { (void)i; memcpy_rows_work(); return dst_token(); }

static uint32_t verify_copy_u16(uint32_t i) { (void)i; copy_u16_work(); return dst_checksum(); }
static uint32_t verify_copy_u32(uint32_t i) { (void)i; copy_u32_work(); return dst_checksum(); }
static uint32_t verify_memcpy(uint32_t i) { (void)i; memcpy_work(); return dst_checksum(); }
static uint32_t verify_fill_u16(uint32_t i) { (void)i; fill_u16_work(); return dst_checksum(); }
static uint32_t verify_fill_u32(uint32_t i) { (void)i; fill_u32_work(); return dst_checksum(); }
static uint32_t verify_fill_u64(uint32_t i) {
    (void)i;
    fill_u64_work();
    return bench_checksum32(s_dst64, (size_t)BYTES);
}
static uint32_t verify_memset(uint32_t i) { (void)i; memset_work(); return dst_checksum(); }
static uint32_t verify_read_691k(uint32_t i) {
    (void)i;
    read_u32_691k_work();
    return s_read_sink;
}
static uint32_t verify_read_76k(uint32_t i) {
    (void)i;
    read_u32_76k_work();
    return s_read_sink;
}

static uint32_t block3_checksum(void) {
    return bench_checksum32(s_dst16, (size_t)SRC_PX * 3 * sizeof(uint16_t));
}

static uint32_t verify_block3_u16(uint32_t i) {
    (void)i;
    memset(s_dst16, 0, (size_t)BYTES);
    block3_u16_work();
    return block3_checksum();
}

static uint32_t verify_block3_u32(uint32_t i) {
    (void)i;
    memset(s_dst16, 0, (size_t)BYTES);
    block3_u32_work();
    return block3_checksum();
}

static uint32_t verify_memcpy_rows(uint32_t i) {
    (void)i;
    memset(s_dst16, 0, (size_t)BYTES);
    memcpy_rows_work();
    return dst_checksum();
}

static const bench_case_t kCases[] = {
    {"copy_u16_691k", "scalar u16 copy of 720x480 (345600 iterations)", "B/s",
     "psram", 3, 25, run_copy_u16, verify_copy_u16, BYTES},
    {"copy_u32_691k", "scalar u32 copy of 720x480 (172800 iterations)", "B/s",
     "psram", 3, 25, run_copy_u32, verify_copy_u32, BYTES},
    {"memcpy_691k", "libc memcpy of 720x480", "B/s", "psram", 3, 25,
     run_memcpy, verify_memcpy, BYTES},
    {"fill_u16_691k", "u16 stores, varying value, 480x720 rows", "B/s",
     "psram", 3, 25, run_fill_u16, verify_fill_u16, BYTES},
    {"fill_u32_691k", "u32 stores, varying value, 480x360 rows", "B/s",
     "psram", 3, 25, run_fill_u32, verify_fill_u32, BYTES},
    {"fill_u64_691k", "u64 stores, varying value, 480x180 rows", "B/s",
     "psram", 3, 25, run_fill_u64, verify_fill_u64, BYTES},
    {"memset_691k", "libc memset of 720x480 (optimised wide stores)", "B/s",
     "psram", 3, 25, run_memset, verify_memset, BYTES},
    {"read_u32_691k", "streaming u32 read of 720x480 (exceeds L2)", "B/s",
     "psram", 3, 25, run_read_691k, verify_read_691k, BYTES},
    {"read_u32_76k", "streaming u32 read of 76800 B (fits in 128 KB L2)",
     "B/s", "psram", 3, 25, run_read_76k, verify_read_76k, SRC_PX * 2},
    {"scaler_block3_u16", "scaler inner loop: 3 u16 stores per source pixel",
     "B/s", "psram", 3, 25, run_block3_u16, verify_block3_u16, SRC_PX * 3 * 2},
    {"scaler_block3_u32", "scaler inner loop: 3 paired u32 stores per pair",
     "B/s", "psram", 3, 25, run_block3_u32, verify_block3_u32, SRC_PX * 3 * 2},
    {"memcpy_rows_1440", "row duplication: 320 x 1440-byte memcpy", "B/s",
     "psram", 3, 25, run_memcpy_rows, verify_memcpy_rows,
     BENCH_GBA_H * 2 * ROW_BYTES},
};

void bench_cases_store_register(void) {
    store_src_init();
    for (size_t i = 0; i < sizeof(kCases) / sizeof(kCases[0]); i++)
        bench_register(&kCases[i]);
}
