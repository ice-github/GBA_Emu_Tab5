#include "gba_scale.h"

#include <limits.h>
#include <stddef.h>
#include <string.h>

static bool mul_overflows_int(int a, int b, int *out) {
    if (a <= 0 || b <= 0) {
        return true;
    }
    if (a > INT_MAX / b) {
        return true;
    }
    *out = a * b;
    return false;
}

bool gba_scale_rgb565_nn_into(const uint16_t *src, int src_w, int src_h,
                              uint16_t *dst, int dst_stride, int scale) {
    int out_w = 0;
    int out_h = 0;

    if (src == NULL || dst == NULL) {
        return false;
    }
    if (mul_overflows_int(src_w, scale, &out_w)) {
        return false;
    }
    if (mul_overflows_int(src_h, scale, &out_h)) {
        return false;
    }
    if (dst_stride < out_w) {
        return false;
    }

    for (int sy = 0; sy < src_h; ++sy) {
        const uint16_t *srow = src + (size_t)sy * (size_t)src_w;
        uint16_t *drow = dst + (size_t)(sy * scale) * (size_t)dst_stride;

        /* Expand one destination row, then duplicate it `scale` times. */
        for (int sx = 0; sx < src_w; ++sx) {
            uint16_t value = srow[sx];
            uint16_t *block = drow + (size_t)sx * (size_t)scale;
            for (int k = 0; k < scale; ++k) {
                block[k] = value;
            }
        }
        for (int r = 1; r < scale; ++r) {
            memcpy(drow + (size_t)r * (size_t)dst_stride, drow,
                   (size_t)out_w * sizeof(uint16_t));
        }
    }
    return true;
}

bool gba_scale_rgb565_nn(const uint16_t *src, int src_w, int src_h,
                         uint16_t *dst, int scale) {
    int out_w = 0;
    int out_h = 0;
    if (mul_overflows_int(src_w, scale, &out_w)) {
        return false;
    }
    if (mul_overflows_int(src_h, scale, &out_h)) {
        return false;
    }
    return gba_scale_rgb565_nn_into(src, src_w, src_h, dst, out_w, scale);
}

/* Half of the 9/2 factor: output runs alternate 4,5,4,5,... so each pair of
 * source pixels covers exactly 9 destination pixels. */
#define ROT45X_NUM 9

static int rot45x_run(int i) {
    /* floor((i+1)*4.5) - floor(i*4.5), all operands non-negative. */
    return (int)(((size_t)(i + 1) * ROT45X_NUM >> 1) -
                 ((size_t)i * ROT45X_NUM >> 1));
}

static bool rot45x_half_le(int v, int *half) {
    /* v is even and non-negative here; halve it, then *9 must fit in int. */
    *half = v / 2;
    return *half <= INT_MAX / ROT45X_NUM;
}

bool gba_rotate_ccw45x_rgb565_nn_into(const uint16_t *src, int src_w, int src_h,
                                      uint16_t *dst, int dst_stride) {
    int half_w = 0;
    int half_h = 0;

    if (src == NULL || dst == NULL) {
        return false;
    }
    if (src_w <= 0 || src_h <= 0) {
        return false;
    }
    if ((src_w & 1) || (src_h & 1)) {
        return false;
    }
    if (!rot45x_half_le(src_w, &half_w) || !rot45x_half_le(src_h, &half_h)) {
        return false;
    }
    /* Output width is what the stride must cover; the band heights always
     * sum to the output height by construction (pairs of runs sum to 9). */
    int out_w = half_h * ROT45X_NUM;
    if (dst_stride < out_w) {
        return false;
    }

    /* One source column becomes one horizontally expanded destination row
     * band; the band is then duplicated k times, mirroring the scaler's
     * expand-once-memcpy-the-rest split. */
    for (int sx = 0; sx < src_w; ++sx) {
        int ry = (src_w - 1) - sx;
        int y0 = (int)((size_t)ry * ROT45X_NUM >> 1);
        int k = rot45x_run(ry);
        uint16_t *rows = dst + (size_t)y0 * (size_t)dst_stride;
        uint16_t *p = rows;
        for (int sy = 0; sy < src_h; ++sy) {
            uint16_t value = src[(size_t)sy * (size_t)src_w + (size_t)sx];
            int w = rot45x_run(sy);
            for (int t = 0; t < w; ++t) {
                *p++ = value;
            }
        }
        for (int r = 1; r < k; ++r) {
            memcpy(rows + (size_t)r * (size_t)dst_stride, rows,
                   (size_t)out_w * sizeof(uint16_t));
        }
    }
    return true;
}
