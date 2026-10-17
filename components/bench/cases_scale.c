/* 3x nearest-neighbour scaler cases.
 *
 * Every case scales the same 240x160 RGB565 source by 3 into the same 720x480
 * destination (the real panel region, stride 720), so the throughput basis and
 * the output checksum are directly comparable: one run answers "which
 * implementation is faster" without reflashing and without a GBA ROM.
 *
 * The variants exist to separate two effects that the production scaler
 * combines:
 *   - 16-bit vs 32-bit stores in the row expansion;
 *   - duplicating the two remaining destination rows with memcpy (read + write
 *     of the whole row) instead of writing them directly from the source.
 *
 * All variants must produce identical pixels. The harness checksums each
 * variant's untimed output, so their reported `checksum` fields must be equal;
 * tools/bench/validate.py enforces that for every `scale3x_*` case.
 *
 * Portable: gba_scale has no dependencies, so this file builds for the host and
 * for the Tab5 unchanged.
 */
#include "bench.h"
#include "gba_scale.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#define SCALE_FACTOR 3

/* int constants (not macros) so the square loops compare int against int and
   the host build stays warning-free under -Wall -Wextra. */
enum {
    SRC_W = BENCH_GBA_W,          /* 240 */
    SRC_H = BENCH_GBA_H,          /* 160 */
    DST_W = SRC_W * SCALE_FACTOR, /* 720 */
    DST_H = SRC_H * SCALE_FACTOR, /* 480 */
    DST_PIXELS = DST_W * DST_H,
};

static EXT_RAM_BSS_ATTR uint16_t s_src[BENCH_GBA_PIXELS];

/* Held as 32-bit words so the 32-bit store variants start from a defined,
 * 4-byte-aligned base; the scaler's own contract is expressed in uint16_t. */
static EXT_RAM_BSS_ATTR uint32_t s_dst_words[(DST_PIXELS + 1) / 2];
static uint16_t *const s_dst = (uint16_t *)s_dst_words;

static void scale_src_init(void) {
    for (uint32_t i = 0; i < BENCH_GBA_PIXELS; i++) {
        uint32_t x = i * 2654435761u + 0x9e3779b9u;
        x ^= x >> 15;
        x *= 2246822519u;
        x ^= x >> 13;
        s_src[i] = (uint16_t)(x >> 16);
    }
}

/* O(1) token derived from the result; the harness consumes it so the work
 * cannot be discarded, without hashing inside the timed region. */
static uint32_t dst_token(void) { return *(volatile const uint32_t *)s_dst; }

static uint32_t dst_checksum(void) {
    return bench_checksum32(s_dst, (size_t)DST_PIXELS * sizeof(uint16_t));
}

/* Expand one 240-pixel source row into 720 destination pixels using paired
 * 32-bit stores: two source pixels (a, b) produce a a a b b b, which is three
 * aligned words. `drow` must be 4-byte aligned (it always is: the row pitch is
 * 720 uint16_t and the base is word-aligned). */
static void u32_pairs_row(const uint16_t *srow, uint16_t *drow) {
    uint32_t *p = (uint32_t *)drow;
    for (int sx = 0; sx < SRC_W; sx += 2) {
        uint32_t a = srow[sx];
        uint32_t b = srow[sx + 1];
        p[0] = a | (a << 16);                 /* a a */
        p[1] = (b << 16) | a;                 /* a b */
        p[2] = b | (b << 16);                 /* b b */
        p += 3;
    }
}

/* --- variant 0: the production scaler --- */

static bool prod_work(void) {
    return gba_scale_rgb565_nn_into(s_src, SRC_W, SRC_H, s_dst, DST_W,
                                    SCALE_FACTOR);
}

static uint32_t run_scale3x_prod(uint32_t items) {
    (void)items;
    prod_work();
    return dst_token();
}

static uint32_t verify_scale3x_prod(uint32_t items) {
    (void)items;
    /* Inverting on failure keeps a bad result visible in the checksum column
     * instead of silently matching the other variants. */
    bool ok = prod_work();
    uint32_t sum = dst_checksum();
    return ok ? sum : ~sum;
}

/* --- variant 1: paired 32-bit stores, duplicate rows with memcpy --- */

static void u32m_work(void) {
    for (int sy = 0; sy < SRC_H; sy++) {
        const uint16_t *srow = s_src + (size_t)sy * SRC_W;
        uint16_t *drow = s_dst + (size_t)(sy * SCALE_FACTOR) * DST_W;
        u32_pairs_row(srow, drow);
        memcpy(drow + DST_W, drow, (size_t)DST_W * sizeof(uint16_t));
        memcpy(drow + 2 * DST_W, drow, (size_t)DST_W * sizeof(uint16_t));
    }
}

static uint32_t run_scale3x_u32m(uint32_t items) {
    (void)items;
    u32m_work();
    return dst_token();
}

static uint32_t verify_scale3x_u32m(uint32_t items) {
    (void)items;
    u32m_work();
    return dst_checksum();
}

/* --- variant 2: paired 32-bit stores, all three rows written directly --- */

static void u32x3_work(void) {
    for (int sy = 0; sy < SRC_H; sy++) {
        const uint16_t *srow = s_src + (size_t)sy * SRC_W;
        uint16_t *drow = s_dst + (size_t)(sy * SCALE_FACTOR) * DST_W;
        for (int r = 0; r < SCALE_FACTOR; r++) {
            u32_pairs_row(srow, drow + (size_t)r * DST_W);
        }
    }
}

static uint32_t run_scale3x_u32x3(uint32_t items) {
    (void)items;
    u32x3_work();
    return dst_token();
}

static uint32_t verify_scale3x_u32x3(uint32_t items) {
    (void)items;
    u32x3_work();
    return dst_checksum();
}

/* --- variant 3: 16-bit stores, rows duplicated by loop (no libc memcpy) --- */

static void u16x3_work(void) {
    for (int sy = 0; sy < SRC_H; sy++) {
        const uint16_t *srow = s_src + (size_t)sy * SRC_W;
        uint16_t *drow = s_dst + (size_t)(sy * SCALE_FACTOR) * DST_W;
        for (int sx = 0; sx < SRC_W; sx++) {
            uint16_t v = srow[sx];
            uint16_t *block = drow + (size_t)sx * SCALE_FACTOR;
            block[0] = v;
            block[1] = v;
            block[2] = v;
        }
        for (int r = 1; r < SCALE_FACTOR; r++) {
            uint16_t *drow_r = drow + (size_t)r * DST_W;
            for (int i = 0; i < DST_W; i++) drow_r[i] = drow[i];
        }
    }
}

static uint32_t run_scale3x_u16x3(uint32_t items) {
    (void)items;
    u16x3_work();
    return dst_token();
}

static uint32_t verify_scale3x_u16x3(uint32_t items) {
    (void)items;
    u16x3_work();
    return dst_checksum();
}

static const bench_case_t kCases[] = {
    {"scale3x_prod", "3x 720x480, production gba_scale (16-bit, memcpy rows)",
     "src-px/s", "psram", 3, 25, run_scale3x_prod, verify_scale3x_prod,
     BENCH_GBA_PIXELS},
    {"scale3x_u32m", "3x 720x480, paired 32-bit stores, memcpy rows",
     "src-px/s", "psram", 3, 25, run_scale3x_u32m, verify_scale3x_u32m,
     BENCH_GBA_PIXELS},
    {"scale3x_u32x3", "3x 720x480, paired 32-bit stores, rows written directly",
     "src-px/s", "psram", 3, 25, run_scale3x_u32x3, verify_scale3x_u32x3,
     BENCH_GBA_PIXELS},
    {"scale3x_u16x3", "3x 720x480, 16-bit stores, rows duplicated by loop",
     "src-px/s", "psram", 3, 25, run_scale3x_u16x3, verify_scale3x_u16x3,
     BENCH_GBA_PIXELS},
};

void bench_cases_scale_register(void) {
    scale_src_init();
    for (size_t i = 0; i < sizeof(kCases) / sizeof(kCases[0]); i++)
        bench_register(&kCases[i]);
}
