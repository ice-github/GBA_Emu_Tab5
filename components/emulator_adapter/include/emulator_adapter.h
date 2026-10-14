#pragma once
#include <stddef.h>
#include <stdint.h>

/* Thin core boundary (M4+ draft, no core vendored yet).
 *
 * Design rules:
 * - The adapter owns ROM bytes, save bytes, input state, and output
 *   framebuffers. The core never touches Tab5 hardware directly.
 * - No per-instruction abstraction: the hot path is frame-granular.
 * - All methods are synchronous; the caller (firmware app) schedules
 *   display/audio/sd work around frame stepping.
 */
#ifdef __cplusplus
extern "C" {
#endif

#define EMU_FB_W 240u
#define EMU_FB_H 160u
#define EMU_FB_PIXELS (EMU_FB_W * EMU_FB_H)

typedef struct {
    uint16_t buttons; /* bit mask, core-agnostic; mapping defined per core */
    int32_t reserved;
} emu_input_t;

typedef struct {
    /* RGB565 framebuffer, EMU_FB_PIXELS entries, owned by the adapter. */
    uint16_t *framebuffer;
    /* Interleaved stereo s16 PCM produced by this step; may be NULL when
       audio is disabled for headless measurement. */
    int16_t *audio;
    uint32_t audio_frames; /* valid entries per channel in `audio` */
    uint32_t audio_capacity_frames;
} emu_output_t;

typedef struct {
    /* Returns 0 on success, negative errno-style code on failure.
       Failure must leave no half-initialized state behind. */
    int (*load_rom)(const uint8_t *rom, size_t rom_size);
    /* External BIOS if the core needs one; NULL/size 0 selects HLE or
       bundled fallback where the core supports it. */
    int (*set_bios)(const uint8_t *bios, size_t bios_size);
    void (*reset)(void);
    void (*set_input)(const emu_input_t *in);
    /* Advances exactly one video frame (~1/59.73s of emulated time).
       Returns 0, or negative on desync/error. */
    int (*step_frame)(emu_output_t *out);
    /* Save persistence: size query + opaque blob read/write. */
    size_t (*save_size)(void);
    int (*save_read)(uint8_t *dst, size_t n);
    int (*save_write)(const uint8_t *src, size_t n);
} emu_core_vtable_t;

#ifdef __cplusplus
}
#endif
