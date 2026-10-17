/* ROM-free bench-split cases for the rotate+scale pipeline.
 *
 * The fused `rot45x_*` cases answer "which implementation is faster" but not
 * *where* the time goes. These four cases split the costs so one run
 * separates them without reflashing and without a GBA ROM:
 *   - rot_only_160x240: 90deg CCW rotation WITHOUT scaling (240x160 ->
 *     160x240), u16 loop. Establishes the transpose/read-stride cost alone.
 *   - scale45x_only_1080x720: 4.5x nearest-neighbour WITHOUT rotation
 *     (240x160 -> 1080x720), u16 row expansion + memcpy row duplication.
 *     Establishes the pure 4.5x expansion cost.
 *   - rot45x_scratch: fused rotate+4.5x (240x160 -> 720x1080) that expands
 *     each band row into a small internal-DRAM scratch row (720 px, NO
 *     EXT_RAM_BSS_ATTR so it lands in internal RAM on device) then copies
 *     rows out to the PSRAM destination. Tests whether a hot intermediate
 *     helps. Its pixels are identical to the `rot45x_*` group, so
 *     tools/bench/validate.py already enforces equality via the `rot45x_`
 *     prefix; no validator change is needed.
 *   - copy_720x1080: plain 720x1080 RGB565 block copy (memcpy) as the
 *     transfer-only reference for the rotated extent. Its work basis is
 *     actual bytes moved, not GBA source pixels.
 *
 * The first three cases share the throughput basis BENCH_GBA_PIXELS with the
 * rot45x/scale3x groups (source pixels per second); the copy case reports
 * bytes per second like the mem cases.
 *
 * Portable: this file builds for the host and for the Tab5 unchanged. The
 * scratch row deliberately has no EXT_RAM_BSS_ATTR; on the host the macro is
 * empty so placement is irrelevant there.
 */
#include "bench.h"

#include <stdint.h>
#include <string.h>

/* int constants (not macros) so the loops compare int against int and the
   host build stays warning-free under -Wall -Wextra. */
enum {
    SRC_W = BENCH_GBA_W,   /* 240 */
    SRC_H = BENCH_GBA_H,   /* 160 */

    ROT_W = SRC_H,         /* 160 */
    ROT_H = SRC_W,         /* 240 */
    ROT_PIXELS = ROT_W * ROT_H,

    S45_W = SRC_W / 2 * 9, /* 1080 */
    S45_H = SRC_H / 2 * 9, /* 720 */
    S45_PIXELS = S45_W * S45_H,

    RS_W = SRC_H / 2 * 9,  /* 720 */
    RS_H = SRC_W / 2 * 9,  /* 1080 */
    RS_PIXELS = RS_W * RS_H,

    CPY_W = 720,
    CPY_H = 1080,
    CPY_PIXELS = CPY_W * CPY_H,
    CPY_BYTES = CPY_PIXELS * 2,
};

static EXT_RAM_BSS_ATTR uint16_t s_src[BENCH_GBA_PIXELS];

/* Held as 32-bit words so the O(1) token reads start from a defined,
 * 4-byte-aligned base; the pixel contract is expressed in uint16_t. */
static EXT_RAM_BSS_ATTR uint32_t s_rot_words[(ROT_PIXELS + 1) / 2];
static EXT_RAM_BSS_ATTR uint32_t s_s45_words[(S45_PIXELS + 1) / 2];
static EXT_RAM_BSS_ATTR uint32_t s_rs_words[(RS_PIXELS + 1) / 2];
static uint16_t *const s_rot_dst = (uint16_t *)s_rot_words;
static uint16_t *const s_s45 = (uint16_t *)s_s45_words;
static uint16_t *const s_rs = (uint16_t *)s_rs_words;

/* Small hot intermediate for rot45x_scratch. Deliberately WITHOUT
 * EXT_RAM_BSS_ATTR: on device this lands in internal DRAM. */
static uint16_t s_scratch[RS_W];

static EXT_RAM_BSS_ATTR uint32_t s_cpy_src_words[(CPY_PIXELS + 1) / 2];
static EXT_RAM_BSS_ATTR uint32_t s_cpy_dst_words[(CPY_PIXELS + 1) / 2];
static uint16_t *const s_cpy_src = (uint16_t *)s_cpy_src_words;
static uint16_t *const s_cpy_dst = (uint16_t *)s_cpy_dst_words;

static void rotsplit_src_init(void) {
    for (uint32_t i = 0; i < BENCH_GBA_PIXELS; i++) {
        uint32_t x = i * 2654435761u + 0x9e3779b9u;
        x ^= x >> 15;
        x *= 2246822519u;
        x ^= x >> 13;
        s_src[i] = (uint16_t)(x >> 16);
    }
    /* Same generator, distinct stream length, so the copy reference is
     * deterministic but not a copy of the GBA frame. */
    for (uint32_t i = 0; i < (uint32_t)CPY_PIXELS; i++) {
        uint32_t x = i * 2654435761u + 0x85ebca6bu;
        x ^= x >> 15;
        x *= 2246822519u;
        x ^= x >> 13;
        s_cpy_src[i] = (uint16_t)(x >> 16);
    }
}

/* Width of the run covering index i (4,5,4,5,...); mirrors the production
 * 4.5x factor without depending on its internals. */
static int run45(int i) {
    return (int)(((size_t)(i + 1) * 9u >> 1) - ((size_t)i * 9u >> 1));
}

/* --- case 1: rotation only, 240x160 -> 160x240, u16 loop --- */

static void rot_only_work(void) {
    for (int sx = 0; sx < SRC_W; ++sx) {
        int ry = (SRC_W - 1) - sx;
        uint16_t *drow = s_rot_dst + (size_t)ry * (size_t)ROT_W;
        for (int sy = 0; sy < SRC_H; ++sy) {
            drow[sy] = s_src[(size_t)sy * (size_t)SRC_W + (size_t)sx];
        }
    }
}

static uint32_t rot_only_token(void) {
    return *(volatile const uint32_t *)s_rot_dst;
}

static uint32_t run_rot_only(uint32_t items) {
    (void)items;
    rot_only_work();
    return rot_only_token();
}

static uint32_t verify_rot_only(uint32_t items) {
    (void)items;
    rot_only_work();
    return bench_checksum32(s_rot_dst,
                            (size_t)ROT_PIXELS * sizeof(uint16_t));
}

/* --- case 2: 4.5x scale only, 240x160 -> 1080x720, u16 + memcpy rows --- */

static void scale45x_only_work(void) {
    for (int sy = 0; sy < SRC_H; ++sy) {
        int y0 = (int)((size_t)sy * 9u >> 1);
        int k = run45(sy);
        const uint16_t *srow = s_src + (size_t)sy * (size_t)SRC_W;
        uint16_t *drow = s_s45 + (size_t)y0 * (size_t)S45_W;
        uint16_t *p = drow;
        for (int sx = 0; sx < SRC_W; ++sx) {
            uint16_t v = srow[sx];
            int w = run45(sx);
            for (int t = 0; t < w; ++t) {
                *p++ = v;
            }
        }
        for (int r = 1; r < k; ++r) {
            memcpy(drow + (size_t)r * (size_t)S45_W, drow,
                   (size_t)S45_W * sizeof(uint16_t));
        }
    }
}

static uint32_t scale45x_only_token(void) {
    return *(volatile const uint32_t *)s_s45;
}

static uint32_t run_scale45x_only(uint32_t items) {
    (void)items;
    scale45x_only_work();
    return scale45x_only_token();
}

static uint32_t verify_scale45x_only(uint32_t items) {
    (void)items;
    scale45x_only_work();
    return bench_checksum32(s_s45, (size_t)S45_PIXELS * sizeof(uint16_t));
}

/* --- case 3: fused rotate+4.5x via internal-DRAM scratch row --- */

static void rot45x_scratch_work(void) {
    for (int sx = 0; sx < SRC_W; ++sx) {
        int ry = (SRC_W - 1) - sx;
        int y0 = (int)((size_t)ry * 9u >> 1);
        int k = run45(ry);
        uint16_t *p = s_scratch;
        for (int sy = 0; sy < SRC_H; ++sy) {
            uint16_t v = s_src[(size_t)sy * (size_t)SRC_W + (size_t)sx];
            int w = run45(sy);
            for (int t = 0; t < w; ++t) {
                *p++ = v;
            }
        }
        for (int r = 0; r < k; ++r) {
            memcpy(s_rs + ((size_t)y0 + (size_t)r) * (size_t)RS_W,
                   s_scratch, sizeof(s_scratch));
        }
    }
}

static uint32_t rot45x_scratch_token(void) {
    return *(volatile const uint32_t *)s_rs;
}

static uint32_t run_rot45x_scratch(uint32_t items) {
    (void)items;
    rot45x_scratch_work();
    return rot45x_scratch_token();
}

static uint32_t verify_rot45x_scratch(uint32_t items) {
    (void)items;
    rot45x_scratch_work();
    return bench_checksum32(s_rs, (size_t)RS_PIXELS * sizeof(uint16_t));
}

/* --- case 4: plain 720x1080 RGB565 block copy --- */

static void copy_work(void) {
    memcpy(s_cpy_dst, s_cpy_src,
           (size_t)CPY_PIXELS * sizeof(uint16_t));
}

static uint32_t copy_token(void) {
    return *(volatile const uint32_t *)s_cpy_dst;
}

static uint32_t run_copy(uint32_t items) {
    if (items > (uint32_t)CPY_BYTES) {
        items = (uint32_t)CPY_BYTES;
    }
    (void)items;
    copy_work();
    return copy_token();
}

static uint32_t verify_copy(uint32_t items) {
    if (items > (uint32_t)CPY_BYTES) {
        items = (uint32_t)CPY_BYTES;
    }
    (void)items;
    copy_work();
    return bench_checksum32(s_cpy_dst,
                            (size_t)CPY_PIXELS * sizeof(uint16_t));
}

static const bench_case_t kCases[] = {
    {"rot_only_160x240", "rotCCW only 160x240, u16 loop (transpose cost)",
     "src-px/s", "psram", 3, 25, run_rot_only, verify_rot_only,
     BENCH_GBA_PIXELS},
    {"scale45x_only_1080x720", "4.5x only 1080x720, u16 + memcpy rows",
     "src-px/s", "psram", 3, 25, run_scale45x_only, verify_scale45x_only,
     BENCH_GBA_PIXELS},
    {"rot45x_scratch", "rotCCW+4.5x 720x1080 via DRAM scratch row + memcpy",
     "src-px/s", "psram", 3, 25, run_rot45x_scratch, verify_rot45x_scratch,
     BENCH_GBA_PIXELS},
    {"copy_720x1080", "memcpy 720x1080 RGB565 (transfer-only reference)",
     "B/s", "psram", 3, 25, run_copy, verify_copy, CPY_BYTES},
};

void bench_cases_rotsplit_register(void) {
    rotsplit_src_init();
    for (size_t i = 0; i < sizeof(kCases) / sizeof(kCases[0]); i++)
        bench_register(&kCases[i]);
}
