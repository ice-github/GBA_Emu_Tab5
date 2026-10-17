/* Host checks for the RGB565 integer scaler (components/gba_scale).
 *
 * No ROM, no mGBA, no ESP-IDF. Exits non-zero if any check fails.
 * `--dump` prints one canonical scaling result as hex so an independent
 * implementation (tests/test_gba_scale.py) can cross-check it.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gba_scale.h"

static int g_checks;
static int g_failures;

#define CHECK(cond, ...)                          \
    do {                                          \
        ++g_checks;                               \
        if (!(cond)) {                            \
            ++g_failures;                         \
            printf("FAIL %s:%d: ", __func__, __LINE__); \
            printf(__VA_ARGS__);                  \
            printf("\n");                         \
        }                                         \
    } while (0)

/* Deliberately simple reference: one division per destination pixel. */
static void ref_scale(const uint16_t *src, int sw, int sh, uint16_t *dst,
                      int dw, int scale) {
    for (int y = 0; y < sh * scale; ++y) {
        for (int x = 0; x < sw * scale; ++x) {
            dst[(size_t)y * (size_t)dw + (size_t)x] =
                src[(size_t)(y / scale) * (size_t)sw + (size_t)(x / scale)];
        }
    }
}

static uint16_t lcg(uint32_t *state) {
    *state = (*state * 1103515245u) + 12345u;
    return (uint16_t)(*state >> 8);
}

static void test_matches_reference(void) {
    static const int dims[][2] = {{1, 1}, {3, 2}, {17, 5}, {240, 160}};
    static const int scales[] = {1, 2, 3, 4};
    uint32_t seed = 0x1234ABCDu;

    for (size_t d = 0; d < sizeof(dims) / sizeof(dims[0]); ++d) {
        int sw = dims[d][0];
        int sh = dims[d][1];
        uint16_t *src = malloc((size_t)sw * sh * sizeof(uint16_t));
        for (size_t s = 0; s < sizeof(scales) / sizeof(scales[0]); ++s) {
            int scale = scales[s];
            int ow = sw * scale;
            int oh = sh * scale;
            uint16_t *got = malloc((size_t)ow * oh * sizeof(uint16_t));
            uint16_t *want = malloc((size_t)ow * oh * sizeof(uint16_t));
            for (int i = 0; i < sw * sh; ++i) {
                src[i] = lcg(&seed);
            }
            memset(got, 0, (size_t)ow * oh * sizeof(uint16_t));
            ref_scale(src, sw, sh, want, ow, scale);
            CHECK(gba_scale_rgb565_nn(src, sw, sh, got, scale),
                  "scale %dx%d x%d rejected", sw, sh, scale);
            CHECK(memcmp(got, want, (size_t)ow * oh * sizeof(uint16_t)) == 0,
                  "content mismatch %dx%d x%d", sw, sh, scale);
            free(got);
            free(want);
        }
        free(src);
    }
}

static void test_exact_3x3_blocks(void) {
    /* 4x3 image, distinct values; every 3x3 block must be a solid copy. */
    uint16_t src[12];
    uint16_t dst[12 * 9];
    for (int i = 0; i < 12; ++i) {
        src[i] = (uint16_t)(i * 0x1111u);
    }
    CHECK(gba_scale_rgb565_nn(src, 4, 3, dst, 3), "3x scale rejected");
    for (int sy = 0; sy < 3; ++sy) {
        for (int sx = 0; sx < 4; ++sx) {
            for (int by = 0; by < 3; ++by) {
                for (int bx = 0; bx < 3; ++bx) {
                    size_t o = (size_t)(sy * 3 + by) * 12 + (size_t)(sx * 3 + bx);
                    CHECK(dst[o] == src[sy * 4 + sx], "block (%d,%d) off", sx, sy);
                }
            }
        }
    }
}

static void test_placement_and_padding(void) {
    /* Mimic the panel layout: 240x160 GBA frame scaled 3x into the middle of a
     * 720x1280 surface, stride == width. */
    const int W = 720, H = 1280, OY = 400;
    const uint16_t sentinel = 0xDEAD;
    uint16_t *src = malloc(240 * 160 * sizeof(uint16_t));
    uint16_t *dst = malloc((size_t)W * H * sizeof(uint16_t));
    uint16_t *want = malloc((size_t)W * H * sizeof(uint16_t));
    for (int i = 0; i < 240 * 160; ++i) {
        src[i] = (uint16_t)(i * 7u + 3u);
    }
    for (size_t i = 0; i < (size_t)W * H; ++i) {
        dst[i] = sentinel;
        want[i] = sentinel;
    }
    ref_scale(src, 240, 160, want + (size_t)OY * W, W, 3);

    CHECK(gba_scale_rgb565_nn_into(src, 240, 160, dst + (size_t)OY * W, W, 3),
          "placed scale rejected");
    CHECK(memcmp(dst, want, (size_t)W * H * sizeof(uint16_t)) == 0,
          "placed image differs from reference");

    size_t above = (size_t)OY * W;
    size_t below = (size_t)(H - (OY + 480)) * W;
    int top_ok = 1, bottom_ok = 1;
    for (size_t i = 0; i < above; ++i) {
        top_ok &= (dst[i] == sentinel);
    }
    for (size_t i = 0; i < below; ++i) {
        bottom_ok &= (dst[(size_t)(OY + 480) * W + i] == sentinel);
    }
    CHECK(top_ok, "top black band modified");
    CHECK(bottom_ok, "bottom black band modified");
    free(src);
    free(dst);
    free(want);
}

static void test_stride_padding(void) {
    /* Destination stride wider than the scaled width: the extra columns must
     * stay untouched on every row. */
    const int SW = 3, SH = 2, SCALE = 2, OW = 6, OH = 4, STRIDE = 10;
    const uint16_t sentinel = 0xBEEF;
    uint16_t src[6] = {1, 2, 3, 4, 5, 6};
    uint16_t dst[4 * 10];
    for (size_t i = 0; i < sizeof(dst) / sizeof(dst[0]); ++i) {
        dst[i] = sentinel;
    }
    CHECK(gba_scale_rgb565_nn_into(src, SW, SH, dst, STRIDE, SCALE),
          "strided scale rejected");
    CHECK(dst[0] == 1 && dst[1] == 1 && dst[10] == 1 && dst[11] == 1,
          "strided content wrong");
    for (int y = 0; y < OH; ++y) {
        for (int x = OW; x < STRIDE; ++x) {
            CHECK(dst[y * STRIDE + x] == sentinel, "stride pad row %d col %d", y, x);
        }
    }
}

static void test_source_unchanged(void) {
    uint16_t src[12];
    uint16_t copy[12];
    uint16_t dst[12 * 9];
    for (int i = 0; i < 12; ++i) {
        src[i] = (uint16_t)(0x9000u + (unsigned)i);
    }
    memcpy(copy, src, sizeof(src));
    CHECK(gba_scale_rgb565_nn(src, 4, 3, dst, 3), "scale rejected");
    CHECK(memcmp(src, copy, sizeof(src)) == 0, "source buffer was modified");
}

static void test_scale_one_is_identity(void) {
    uint16_t src[6] = {0xAAAA, 0xBBBB, 0xCCCC, 0xDDDD, 0xEEEE, 0xFFFF};
    uint16_t dst[6] = {0};
    CHECK(gba_scale_rgb565_nn(src, 3, 2, dst, 1), "scale 1 rejected");
    CHECK(memcmp(src, dst, sizeof(src)) == 0, "scale 1 is not identity");
}

static void test_invalid_arguments(void) {
    uint16_t src[6] = {0};
    uint16_t dst[64] = {0};
    uint16_t want_dst[64];
    memcpy(want_dst, dst, sizeof(dst));

    CHECK(!gba_scale_rgb565_nn(NULL, 3, 2, dst, 2), "NULL src accepted");
    CHECK(!gba_scale_rgb565_nn(src, 3, 2, NULL, 2), "NULL dst accepted");
    CHECK(!gba_scale_rgb565_nn(src, 0, 2, dst, 2), "zero width accepted");
    CHECK(!gba_scale_rgb565_nn(src, 3, 0, dst, 2), "zero height accepted");
    CHECK(!gba_scale_rgb565_nn(src, -3, 2, dst, 2), "negative width accepted");
    CHECK(!gba_scale_rgb565_nn(src, 3, 2, dst, 0), "scale 0 accepted");
    CHECK(!gba_scale_rgb565_nn(src, 3, 2, dst, -2), "negative scale accepted");
    CHECK(!gba_scale_rgb565_nn_into(src, 3, 2, dst, 5, 2),
          "undersized stride accepted");
    CHECK(!gba_scale_rgb565_nn(src, 2147483647, 2, dst, 2),
          "overflowing width accepted");
    CHECK(memcmp(dst, want_dst, sizeof(dst)) == 0,
          "destination written on rejection");
}

static void dump_canonical(void) {
    uint16_t src[12];
    uint16_t dst[12 * 9];
    for (int i = 0; i < 12; ++i) {
        src[i] = (uint16_t)(0x0100u * (unsigned)i + (unsigned)i);
    }
    if (!gba_scale_rgb565_nn(src, 4, 3, dst, 3)) {
        printf("ERR\n");
        return;
    }
    for (int y = 0; y < 9; ++y) {
        for (int x = 0; x < 12; ++x) {
            printf("%04x%s", (unsigned)dst[y * 12 + x], x == 11 ? "\n" : " ");
        }
    }
}

/* ----- rotate 90 CCW + 4.5x ----- */

/* Independent per-pixel reference: sample the source pixel whose 4/5-run
 * block covers the destination pixel (pixel-centre convention). */
static void ref_rot45(const uint16_t *src, int sw, int sh, uint16_t *dst,
                      int dw) {
    int ow = sh / 2 * 9;
    int oh = sw / 2 * 9;
    for (int ly = 0; ly < oh; ++ly) {
        int ry = (2 * ly + 1) / 9;
        int sx = (sw - 1) - ry;
        for (int lx = 0; lx < ow; ++lx) {
            int rx = (2 * lx + 1) / 9;
            int sy = rx;
            dst[(size_t)ly * (size_t)dw + (size_t)lx] =
                src[(size_t)sy * (size_t)sw + (size_t)sx];
        }
    }
}

static void test_rot_matches_reference(void) {
    static const int dims[][2] = {{2, 2}, {4, 2}, {6, 4}, {10, 8}, {240, 160}};
    uint32_t seed = 0x51ab3d7du;

    for (size_t d = 0; d < sizeof(dims) / sizeof(dims[0]); ++d) {
        int sw = dims[d][0];
        int sh = dims[d][1];
        int ow = sh / 2 * 9;
        int oh = sw / 2 * 9;
        uint16_t *src = malloc((size_t)sw * sh * sizeof(uint16_t));
        uint16_t *got = malloc((size_t)ow * oh * sizeof(uint16_t));
        uint16_t *want = malloc((size_t)ow * oh * sizeof(uint16_t));
        for (int i = 0; i < sw * sh; ++i) {
            src[i] = lcg(&seed);
        }
        memset(got, 0, (size_t)ow * oh * sizeof(uint16_t));
        ref_rot45(src, sw, sh, want, ow);
        CHECK(gba_rotate_ccw45x_rgb565_nn_into(src, sw, sh, got, ow),
              "rot45 %dx%d rejected", sw, sh);
        CHECK(memcmp(got, want, (size_t)ow * oh * sizeof(uint16_t)) == 0,
              "rot45 content mismatch %dx%d", sw, sh);
        free(got);
        free(want);
        free(src);
    }
}

static void test_rot_corners(void) {
    /* Oracle: source corners land in fixed dest blocks (CCW = top edge goes
     * left, right edge goes up). */
    static uint16_t src[240 * 160];
    static uint16_t dst[720 * 1080];
    for (int i = 0; i < 240 * 160; ++i) {
        src[i] = 0x0000;
    }
    src[0] = 0x0001;                        /* top-left */
    src[239] = 0x0002;                      /* top-right */
    src[159 * 240] = 0x0003;                /* bottom-left */
    src[159 * 240 + 239] = 0x0004;          /* bottom-right */
    memset(dst, 0, sizeof(dst));
    CHECK(gba_rotate_ccw45x_rgb565_nn_into(src, 240, 160, dst, 720),
          "rot45 240x160 rejected");
    /* Top-right -> rect top-left block (cols 0..3, rows 0..3). */
    for (int y = 0; y < 4; ++y) {
        for (int x = 0; x < 4; ++x) {
            CHECK(dst[(size_t)y * 720 + (size_t)x] == 0x0002,
                  "top-right block (%d,%d)", x, y);
        }
    }
    /* Top-left -> rect bottom-left block (cols 0..3, rows 1075..1079). */
    for (int y = 1075; y < 1080; ++y) {
        for (int x = 0; x < 4; ++x) {
            CHECK(dst[(size_t)y * 720 + (size_t)x] == 0x0001,
                  "top-left block (%d,%d)", x, y);
        }
    }
    /* Bottom-right -> rect top-right block (cols 715..719, rows 0..3). */
    for (int y = 0; y < 4; ++y) {
        for (int x = 715; x < 720; ++x) {
            CHECK(dst[(size_t)y * 720 + (size_t)x] == 0x0004,
                  "bottom-right block (%d,%d)", x, y);
        }
    }
    /* Bottom-left -> rect bottom-right block (cols 715..719, rows 1075..1079). */
    for (int y = 1075; y < 1080; ++y) {
        for (int x = 715; x < 720; ++x) {
            CHECK(dst[(size_t)y * 720 + (size_t)x] == 0x0003,
                  "bottom-left block (%d,%d)", x, y);
        }
    }
}

static void test_rot_placement_and_bands(void) {
    /* Panel layout: 720x1080 rect at y=100 inside 720x1280, 100 px black
     * bands top and bottom, stride == width. */
    const int W = 720, H = 1280, OY = 100;
    const uint16_t sentinel = 0xDEAD;
    uint16_t *src = malloc(240 * 160 * sizeof(uint16_t));
    uint16_t *dst = malloc((size_t)W * H * sizeof(uint16_t));
    uint16_t *want = malloc((size_t)W * H * sizeof(uint16_t));
    for (int i = 0; i < 240 * 160; ++i) {
        src[i] = (uint16_t)(i * 7u + 3u);
    }
    for (size_t i = 0; i < (size_t)W * H; ++i) {
        dst[i] = sentinel;
        want[i] = sentinel;
    }
    ref_rot45(src, 240, 160, want + (size_t)OY * W, W);

    CHECK(gba_rotate_ccw45x_rgb565_nn_into(src, 240, 160,
                                           dst + (size_t)OY * W, W),
          "placed rot45 rejected");
    CHECK(memcmp(dst, want, (size_t)W * H * sizeof(uint16_t)) == 0,
          "placed rotated image differs from reference");

    size_t above = (size_t)OY * W;
    size_t below = (size_t)(H - (OY + 1080)) * W;
    int top_ok = 1, bottom_ok = 1;
    for (size_t i = 0; i < above; ++i) {
        top_ok &= (dst[i] == sentinel);
    }
    for (size_t i = 0; i < below; ++i) {
        bottom_ok &= (dst[(size_t)(OY + 1080) * W + i] == sentinel);
    }
    CHECK(top_ok, "rot45 top black band modified");
    CHECK(bottom_ok, "rot45 bottom black band modified");
    free(src);
    free(dst);
    free(want);
}

static void test_rot_source_unchanged(void) {
    uint16_t src[8];
    uint16_t copy[8];
    uint16_t dst[4 * 9 * 2 * 9];
    for (int i = 0; i < 8; ++i) {
        src[i] = (uint16_t)(0x9000u + (unsigned)i);
    }
    memcpy(copy, src, sizeof(src));
    CHECK(gba_rotate_ccw45x_rgb565_nn_into(src, 4, 2, dst, 9),
          "rot45 rejected");
    CHECK(memcmp(src, copy, sizeof(src)) == 0,
          "rot45 source buffer was modified");
}

static void test_rot_invalid_arguments(void) {
    uint16_t src[8] = {0};
    uint16_t dst[512] = {0};
    uint16_t want_dst[512];
    memcpy(want_dst, dst, sizeof(dst));

    CHECK(!gba_rotate_ccw45x_rgb565_nn_into(NULL, 4, 2, dst, 9),
          "rot45 NULL src accepted");
    CHECK(!gba_rotate_ccw45x_rgb565_nn_into(src, 4, 2, NULL, 9),
          "rot45 NULL dst accepted");
    CHECK(!gba_rotate_ccw45x_rgb565_nn_into(src, 0, 2, dst, 9),
          "rot45 zero width accepted");
    CHECK(!gba_rotate_ccw45x_rgb565_nn_into(src, 4, 0, dst, 9),
          "rot45 zero height accepted");
    CHECK(!gba_rotate_ccw45x_rgb565_nn_into(src, -4, 2, dst, 9),
          "rot45 negative width accepted");
    CHECK(!gba_rotate_ccw45x_rgb565_nn_into(src, 3, 2, dst, 9),
          "rot45 odd width accepted");
    CHECK(!gba_rotate_ccw45x_rgb565_nn_into(src, 4, 3, dst, 18),
          "rot45 odd height accepted");
    CHECK(!gba_rotate_ccw45x_rgb565_nn_into(src, 4, 2, dst, 8),
          "rot45 undersized stride accepted");
    CHECK(memcmp(dst, want_dst, sizeof(dst)) == 0,
          "rot45 destination written on rejection");
}

static void dump_rot_canonical(void) {
    /* 4x2 source -> 9x18 output, small enough for a Python cross-check. */
    uint16_t src[8];
    uint16_t dst[9 * 18];
    for (int i = 0; i < 8; ++i) {
        src[i] = (uint16_t)(0x0100u * (unsigned)i + (unsigned)i);
    }
    if (!gba_rotate_ccw45x_rgb565_nn_into(src, 4, 2, dst, 9)) {
        printf("ERR\n");
        return;
    }
    for (int y = 0; y < 18; ++y) {
        for (int x = 0; x < 9; ++x) {
            printf("%04x%s", (unsigned)dst[y * 9 + x], x == 8 ? "\n" : " ");
        }
    }
}

int main(int argc, char **argv) {
    if (argc > 1 && strcmp(argv[1], "--dump") == 0) {
        dump_canonical();
        return 0;
    }
    if (argc > 1 && strcmp(argv[1], "--dump-rot") == 0) {
        dump_rot_canonical();
        return 0;
    }

    test_matches_reference();
    test_exact_3x3_blocks();
    test_placement_and_padding();
    test_stride_padding();
    test_source_unchanged();
    test_scale_one_is_identity();
    test_invalid_arguments();
    test_rot_matches_reference();
    test_rot_corners();
    test_rot_placement_and_bands();
    test_rot_source_unchanged();
    test_rot_invalid_arguments();

    printf("scale_test: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
