#include "bench.h"
#include <stdint.h>
#include <string.h>

/* GBA-sized pixel workloads, RGB565. No display hardware touched here;
   display-path cost is measured on device in M2. */

static uint16_t s_frame[BENCH_GBA_PIXELS];
static uint16_t s_frame2[BENCH_GBA_PIXELS];
static uint16_t s_out2x[(BENCH_GBA_W * 2) * (BENCH_GBA_H * 2)];

__attribute__((constructor)) static void pixel_init(void) {
    for (uint32_t i = 0; i < BENCH_GBA_PIXELS; i++) {
        uint32_t x = i * 2654435761u + 0x85ebca6bu;
        x ^= x >> 13;
        s_frame[i] = (uint16_t)(x & 0xffffu);
    }
}

static uint32_t run_frame_copy(uint32_t pixels) {
    if (pixels > BENCH_GBA_PIXELS) pixels = BENCH_GBA_PIXELS;
    memcpy(s_frame2, s_frame, pixels * sizeof(uint16_t));
    return bench_checksum32(s_frame2, pixels * sizeof(uint16_t));
}

static uint32_t run_frame_fill(uint32_t pixels) {
    if (pixels > BENCH_GBA_PIXELS) pixels = BENCH_GBA_PIXELS;
    for (uint32_t i = 0; i < pixels; i++) s_frame2[i] = 0x7bcf;
    return bench_checksum32(s_frame2, pixels * sizeof(uint16_t));
}

/* Nearest-neighbor 2x upscale: the CPU reference that PPA-SRM will be
   compared against on device (PPA uses bilinear, so quality differs). */
static uint32_t run_scale2x_nearest(uint32_t pixels) {
    (void)pixels;
    for (uint32_t y = 0; y < BENCH_GBA_H; y++) {
        for (uint32_t x = 0; x < BENCH_GBA_W; x++) {
            uint16_t v = s_frame[y * BENCH_GBA_W + x];
            uint32_t dx = x * 2, dy = y * 2, w2 = BENCH_GBA_W * 2;
            s_out2x[dy * w2 + dx] = v;
            s_out2x[dy * w2 + dx + 1] = v;
            s_out2x[(dy + 1) * w2 + dx] = v;
            s_out2x[(dy + 1) * w2 + dx + 1] = v;
        }
    }
    return bench_checksum32(s_out2x, sizeof(s_out2x));
}

/* RGB565 -> XRGB8888 expansion, a common upload-path cost. */
static uint32_t s_out32[BENCH_GBA_PIXELS];

static uint32_t run_rgb565_to_xrgb(uint32_t pixels) {
    if (pixels > BENCH_GBA_PIXELS) pixels = BENCH_GBA_PIXELS;
    for (uint32_t i = 0; i < pixels; i++) {
        uint16_t v = s_frame[i];
        uint32_t r = (v >> 11) & 0x1fu, g = (v >> 5) & 0x3fu,
                 b = v & 0x1fu;
        s_out32[i] = (r * 255u / 31u) << 16 | (g * 255u / 63u) << 8 |
                     (b * 255u / 31u);
    }
    return bench_checksum32(s_out32, pixels * sizeof(uint32_t));
}

static const bench_case_t kCases[] = {
    {"frame_copy_38k", "240x160 RGB565 frame copy", "px/s", 10, 100,
     run_frame_copy, BENCH_GBA_PIXELS},
    {"frame_fill_38k", "240x160 RGB565 frame fill", "px/s", 10, 100,
     run_frame_fill, BENCH_GBA_PIXELS},
    {"scale2x_nearest", "240x160 -> 480x320 nearest 2x", "src-px/s", 5, 30,
     run_scale2x_nearest, BENCH_GBA_PIXELS},
    {"rgb565_to_xrgb", "240x160 RGB565 -> XRGB8888", "px/s", 10, 100,
     run_rgb565_to_xrgb, BENCH_GBA_PIXELS},
};

__attribute__((constructor)) static void bench_pixel_ctor(void) {
    for (size_t i = 0; i < sizeof(kCases) / sizeof(kCases[0]); i++)
        bench_register(&kCases[i]);
}
