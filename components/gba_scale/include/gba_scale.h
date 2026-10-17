/* Nearest-neighbour integer scaler for RGB565 images.
 *
 * Dependency-free (no ESP-IDF, no mGBA) so the same code is used on the Tab5
 * and in host tests. Colour values are copied verbatim; no conversion is done.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

/* Scale `src` by an integer factor into `dst`.
 *
 * Each source pixel becomes a `scale` x `scale` block of the same RGB565
 * value. `dst` points at the top-left destination pixel and `dst_stride` is the
 * number of uint16_t entries between consecutive destination rows.
 *
 * Only the `src_w * scale` columns and `src_h * scale` rows starting at `dst`
 * are written. Every other destination entry is left untouched, so callers can
 * scale into a sub-region of a larger image (as the panel layout does). The
 * source buffer is only read.
 *
 * Returns true on success. Returns false, writing nothing, when:
 *   - `src` or `dst` is NULL,
 *   - `src_w` or `src_h` is not positive,
 *   - `scale` is less than 1,
 *   - `dst_stride` is smaller than `src_w * scale`, or
 *   - `src_w * scale` or `src_h * scale` would overflow int.
 */
bool gba_scale_rgb565_nn_into(const uint16_t *src, int src_w, int src_h,
                              uint16_t *dst, int dst_stride, int scale);

/* Convenience wrapper for a tightly packed destination of exactly
 * (src_w * scale) x (src_h * scale) pixels. */
bool gba_scale_rgb565_nn(const uint16_t *src, int src_w, int src_h,
                         uint16_t *dst, int scale);

/* Rotate 90 degrees counter-clockwise and scale 4.5x (nearest neighbour).
 *
 * The rotated image is `src_h` wide and `src_w` tall; at 4.5x the output is
 * `(src_h/2*9)` x `(src_w/2*9)` pixels. For the GBA frame (240x160) that is
 * 720x1080, which fills the width of the 720x1280 portrait panel exactly and
 * centres vertically with 100 px black bands top and bottom.
 *
 * Mapping (source `sx` right, `sy` down; rotated `rx` right, `ry` down):
 *   rx = sy, ry = (src_w - 1) - sx
 * i.e. the source top edge becomes the left edge (up becomes left). Each
 * source pixel covers a run of 4 or 5 destination pixels per axis
 * (4,5,4,5,...), so no floating point is needed.
 *
 * `dst` points at the top-left destination pixel and `dst_stride` is the
 * number of uint16_t entries between consecutive destination rows. Only the
 * output rectangle starting at `dst` is written; everything else is left
 * untouched, so callers can render into a sub-region of a larger image.
 * The source buffer is only read.
 *
 * Source dimensions must both be even (4.5 = 9/2 needs pairs). Returns true
 * on success. Returns false, writing nothing, when:
 *   - `src` or `dst` is NULL,
 *   - `src_w` or `src_h` is not positive,
 *   - `src_w` or `src_h` is odd,
 *   - `dst_stride` is smaller than the output width, or
 *   - the output dimensions would overflow int.
 */
bool gba_rotate_ccw45x_rgb565_nn_into(const uint16_t *src, int src_w, int src_h,
                                      uint16_t *dst, int dst_stride);
