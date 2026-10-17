/* Rotate-90-CCW + 4.5x nearest-neighbour cases.
 *
 * Every case renders the same 240x160 RGB565 source into the same 720x1080
 * destination (the aspect-fit rect for the 720x1280 portrait panel, stride
 * 720), so the throughput basis and the output checksum are directly
 * comparable: one run answers "which rotate+scale implementation is faster"
 * without reflashing and without a GBA ROM.
 *
 * The variants mirror cases_scale.c and separate the same two effects:
 *   - 16-bit vs 32-bit stores in the band-row expansion;
 *   - duplicating the remaining band rows with memcpy vs writing them
 *     directly from the source column.
 *
 * One extra constraint vs the 3x scaler: 4.5x runs are 4 or 5 pixels wide,
 * so a run can start at a 2-byte-misaligned address. The 32-bit variants
 * therefore store a leading half pixel first when misaligned; see
 * store_run_u32().
 *
 * All variants must produce identical pixels. The harness checksums each
 * variant's untimed output, so their reported `checksum` fields must be equal;
 * tools/bench/validate.py enforces that for every `rot45x_*` case.
 *
 * Portable: gba_scale has no dependencies, so this file builds for the host and
 * for the Tab5 unchanged.
 */
#include "bench.h"
#include "gba_scale.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

/* int constants (not macros) so the loops compare int against int and the
   host build stays warning-free under -Wall -Wextra. */
enum {
    SRC_W = BENCH_GBA_W,       /* 240 */
    SRC_H = BENCH_GBA_H,       /* 160 */
    DST_W = SRC_H / 2 * 9,     /* 720 */
    DST_H = SRC_W / 2 * 9,     /* 1080 */
    DST_PIXELS = DST_W * DST_H,
};

static EXT_RAM_BSS_ATTR uint16_t s_src[BENCH_GBA_PIXELS];

/* Held as 32-bit words so the 32-bit store variants start from a defined,
 * 4-byte-aligned base; the scaler's own contract is expressed in uint16_t. */
static EXT_RAM_BSS_ATTR uint32_t s_dst_words[(DST_PIXELS + 1) / 2];
static uint16_t *const s_dst = (uint16_t *)s_dst_words;

static void rot_src_init(void) {
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

/* Width of the run covering rotated column/row index i (4,5,4,5,...). */
static int rot_run(int i) {
    return (int)(((size_t)(i + 1) * 9u >> 1) - ((size_t)i * 9u >> 1));
}

/* Store one run of `w` (4 or 5) copies of `v`, advancing `*pp`. Uses 32-bit
 * stores wherever the address allows; a 2-byte-misaligned start stores one
 * leading half pixel first. */
static void store_run_u32(uint16_t **pp, uint16_t v, int w) {
    uint16_t *p = *pp;
    if (((uintptr_t)p & 2u) != 0) {
        *p++ = v;
        w--;
    }
    uint32_t *q = (uint32_t *)p;
    uint32_t vv = (uint32_t)v | ((uint32_t)v << 16);
    while (w >= 2) {
        *q++ = vv;
        w -= 2;
    }
    p = (uint16_t *)q;
    while (w > 0) {
        *p++ = v;
        w--;
    }
    *pp = p;
}

/* Expand one source column into a 720-pixel destination row. */
static void u32_band_row(const uint16_t *src, int src_w, int sx,
                         uint16_t *drow, int src_h) {
    uint16_t *p = drow;
    for (int sy = 0; sy < src_h; ++sy) {
        uint16_t v = src[(size_t)sy * (size_t)src_w + (size_t)sx];
        store_run_u32(&p, v, rot_run(sy));
    }
}

static void u16_band_row(const uint16_t *src, int src_w, int sx,
                         uint16_t *drow, int src_h) {
    uint16_t *p = drow;
    for (int sy = 0; sy < src_h; ++sy) {
        uint16_t v = src[(size_t)sy * (size_t)src_w + (size_t)sx];
        int w = rot_run(sy);
        for (int t = 0; t < w; ++t) {
            *p++ = v;
        }
    }
}

/* Start row of the destination band for source column sx. */
static int band_y0(int src_w, int sx) {
    int ry = (src_w - 1) - sx;
    return (int)((size_t)ry * 9u >> 1);
}

/* --- variant 0: the production rotate+scale --- */

static bool prod_work(void) {
    return gba_rotate_ccw45x_rgb565_nn_into(s_src, SRC_W, SRC_H, s_dst, DST_W);
}

static uint32_t run_rot45x_prod(uint32_t items) {
    (void)items;
    prod_work();
    return dst_token();
}

static uint32_t verify_rot45x_prod(uint32_t items) {
    (void)items;
    /* Inverting on failure keeps a bad result visible in the checksum column
     * instead of silently matching the other variants. */
    bool ok = prod_work();
    uint32_t sum = dst_checksum();
    return ok ? sum : ~sum;
}

/* --- variant 1: 32-bit run stores, duplicate band rows with memcpy --- */

static void u32m_work(void) {
    for (int sx = 0; sx < SRC_W; ++sx) {
        int y0 = band_y0(SRC_W, sx);
        int k = rot_run((SRC_W - 1) - sx);
        uint16_t *rows = s_dst + (size_t)y0 * DST_W;
        u32_band_row(s_src, SRC_W, sx, rows, SRC_H);
        for (int r = 1; r < k; ++r) {
            memcpy(rows + (size_t)r * DST_W, rows,
                   (size_t)DST_W * sizeof(uint16_t));
        }
    }
}

static uint32_t run_rot45x_u32m(uint32_t items) {
    (void)items;
    u32m_work();
    return dst_token();
}

static uint32_t verify_rot45x_u32m(uint32_t items) {
    (void)items;
    u32m_work();
    return dst_checksum();
}

/* --- variant 2: 32-bit run stores, all band rows written directly --- */

static void u32d_work(void) {
    for (int sx = 0; sx < SRC_W; ++sx) {
        int y0 = band_y0(SRC_W, sx);
        int k = rot_run((SRC_W - 1) - sx);
        for (int r = 0; r < k; ++r) {
            u32_band_row(s_src, SRC_W, sx,
                         s_dst + ((size_t)y0 + (size_t)r) * DST_W, SRC_H);
        }
    }
}

static uint32_t run_rot45x_u32d(uint32_t items) {
    (void)items;
    u32d_work();
    return dst_token();
}

static uint32_t verify_rot45x_u32d(uint32_t items) {
    (void)items;
    u32d_work();
    return dst_checksum();
}

/* --- variant 3: 16-bit stores, band rows duplicated by loop --- */

static void u16l_work(void) {
    for (int sx = 0; sx < SRC_W; ++sx) {
        int y0 = band_y0(SRC_W, sx);
        int k = rot_run((SRC_W - 1) - sx);
        uint16_t *rows = s_dst + (size_t)y0 * DST_W;
        u16_band_row(s_src, SRC_W, sx, rows, SRC_H);
        for (int r = 1; r < k; ++r) {
            uint16_t *dst_r = rows + (size_t)r * DST_W;
            for (int i = 0; i < DST_W; ++i) dst_r[i] = rows[i];
        }
    }
}

static uint32_t run_rot45x_u16l(uint32_t items) {
    (void)items;
    u16l_work();
    return dst_token();
}

static uint32_t verify_rot45x_u16l(uint32_t items) {
    (void)items;
    u16l_work();
    return dst_checksum();
}

static const bench_case_t kCases[] = {
    {"rot45x_prod", "rotCCW+4.5x 720x1080, production (16-bit, memcpy rows)",
     "src-px/s", "psram", 3, 25, run_rot45x_prod, verify_rot45x_prod,
     BENCH_GBA_PIXELS},
    {"rot45x_u32m", "rotCCW+4.5x 720x1080, 32-bit run stores, memcpy rows",
     "src-px/s", "psram", 3, 25, run_rot45x_u32m, verify_rot45x_u32m,
     BENCH_GBA_PIXELS},
    {"rot45x_u32d", "rotCCW+4.5x 720x1080, 32-bit runs, rows written directly",
     "src-px/s", "psram", 3, 25, run_rot45x_u32d, verify_rot45x_u32d,
     BENCH_GBA_PIXELS},
    {"rot45x_u16l", "rotCCW+4.5x 720x1080, 16-bit stores, rows loop-duplicated",
     "src-px/s", "psram", 3, 25, run_rot45x_u16l, verify_rot45x_u16l,
     BENCH_GBA_PIXELS},
};

void bench_cases_rot45x_register(void) {
    rot_src_init();
    for (size_t i = 0; i < sizeof(kCases) / sizeof(kCases[0]); i++)
        bench_register(&kCases[i]);
}
