/* Tab5 MIPI-DSI panel bring-up for the GBA framebuffer path.
 *
 * Deliberately minimal: the panel is created with the BSP low-level API only.
 * No LVGL task, touch controller, audio codec, camera or USB is initialised.
 * The GBA framebuffer is never rendered into directly; callers present a
 * separate RGB565 image so the CRC-verified emulator framebuffer stays intact.
 */
#pragma once

#include <stdint.h>
#include "esp_err.h"
#include "esp_lcd_panel_ops.h"

/* Native panel geometry (portrait). */
#define GBA_DISPLAY_H_RES 720
#define GBA_DISPLAY_V_RES 1280
#define GBA_DISPLAY_PIXELS (GBA_DISPLAY_H_RES * GBA_DISPLAY_V_RES)

/* Backlight PWM frequency. The BSP default is 5 kHz, which historically beat
 * with other activity on this board; 20 kHz avoids the audible/visible band. */
#define GBA_DISPLAY_BACKLIGHT_HZ 20000

/* Default backlight level for verification runs. */
#define GBA_DISPLAY_BRIGHTNESS_PERCENT 30

/* Bound for waiting on a panel refresh event before declaring a stall. */
#define GBA_DISPLAY_SYNC_TIMEOUT_MS 200

/* Frames run by the ROM-free animated pattern test. */
#ifndef GBA_DISPLAY_ANIM_FRAMES
#define GBA_DISPLAY_ANIM_FRAMES 600u
#endif

/* Create the DSI panel, turn it on, and set the backlight to off (so no
 * uninitialised frame buffer is ever shown). Idempotent. */
esp_err_t gba_display_init(void);

/* The underlying LCD panel handle, for registering DPI event callbacks. NULL
 * until gba_display_init() succeeds. */
esp_lcd_panel_handle_t gba_display_panel(void);

/* Push a full-screen RGB565 buffer (GBA_DISPLAY_PIXELS pixels). */
esp_err_t gba_display_present_full(const uint16_t *rgb565);

/* Push an RGB565 sub-region. `rgb565` is tightly packed (w pixels/row) and is
 * copied into the panel; the caller keeps ownership. */
esp_err_t gba_display_present_region(int x, int y, int w, int h,
                                     const uint16_t *rgb565);

/* Backlight control (percent in [0,100]). Safe to call before the first image
 * is presented. */
esp_err_t gba_display_backlight_set(int percent);
esp_err_t gba_display_backlight_off(void);

/* Fill a (w x h) RGB565 buffer with the ROM-free verification pattern:
 * colour bars, a gray ramp, a 1px grid, an outer border, four distinguishable
 * corner markers and asymmetric top/bottom markers. */
void gba_display_fixed_pattern(uint16_t *fb, int w, int h);

/* Tear the panel down and release DMA/refresh resources. */
void gba_display_deinit(void);

/* Panel refresh synchronisation (double buffered).
 *
 * Continuous display renders straight into one of the DPI driver's frame
 * buffers and presents it by pointing `gba_display_present_region` at that
 * buffer. The driver recognises the pointer as one of its own, so it performs
 * only a cache write-back (no copy at all) and makes that buffer current at the
 * next frame boundary. The other buffer is the one being scanned out, so the
 * update is tear-free and costs no DMA2D transfer.
 *
 * `on_refresh_done` (a frame has been scanned out) is the event a task waits on
 * before reusing the buffer it is about to draw into. It is delivered from the
 * DSI bridge VSYNC ISR and the callback only wakes the waiting task. Because
 * the wait waits for the counter to advance rather than for a level, a stale
 * event can never make it return early. Synchronisation must be enabled and
 * waited on from the same task.
 *
 * This is distinct from `on_color_trans_done` (a DMA2D copy of a caller-owned
 * surface into a frame buffer finished): that event only exists on the
 * external-surface path, which the continuous tests avoid. */
esp_err_t gba_display_sync_enable(void);
esp_err_t gba_display_wait_refresh(uint32_t timeout_ms);
/* Wait until the refresh counter differs from a snapshot captured after the
 * driver accepted a presented buffer. Callers can do independent work before
 * waiting without losing a later refresh; a boundary during submission is
 * conservatively excluded. */
esp_err_t gba_display_wait_refresh_since(uint32_t snapshot,
                                         uint32_t timeout_ms);
uint32_t gba_display_refresh_count(void);
void gba_display_sync_disable(void);

/* The driver's two PSRAM frame buffers (720x1280 RGB565, pre-cleared to black
 * by the driver). Requires CONFIG_BSP_LCD_DPI_BUFFER_NUMS >= 2. */
esp_err_t gba_display_get_frame_buffers(uint16_t **fb0, uint16_t **fb1);

/* ROM-free animated pattern (GBA_DISPLAY_ANIM_TEST): moving bars, a scrolling
 * gradient seam and a per-frame progress bar, presented one frame at a time so
 * smoothness, tearing and the display path's own throughput can be judged
 * without the emulator in the loop. No SD, audio, input or save. Returns ESP_OK
 * with the last frame still on screen. */
esp_err_t gba_display_anim_test_run(void);

/* ROM-free PPA vs CPU image-path benchmark (GBA_PPA_BENCH_TEST): fixed
 * asymmetric 240x160 image through PPA SRM copy/rotate/scale paths plus the
 * CPU rot45x reference, timed per operation in BLOCKING mode. No SD, panel,
 * audio, input or save. Returns ESP_OK when all paths complete and the copy
 * and orientation gates pass. */
esp_err_t gba_ppa_bench_test_run(void);
/* Full still-frame flow used by the GBA_DISPLAY_ROM_TEST mode:
 * mount the SD card read-only, verify and load the Golden Sun 1 ROM, run 600
 * no-input frames, check the framebuffer RGB565 CRC against the host golden
 * value, and only then bring the panel up, rotate/scale the frame (90 deg CCW,
 * 4.5x aspect-fit) and present it.
 * Returns ESP_OK while the image is displayed; the caller then holds. On
 * ESP_OK the source framebuffer and the panel surface are intentionally kept
 * allocated. No audio output, no input, no save, no SD writes. */
esp_err_t gba_display_rom_test_run(void);

/* Continuous display flow used by the GBA_DISPLAY_STREAM_TEST mode: run and
 * display every one of GBA_ROM_FRAME_COUNT frames, checking each framebuffer
 * CRC against the generated host vector. Panel copies are DMA2D-asynchronous,
 * so the driver's copy-done event is awaited (with a bounded timeout, from a
 * task) before the shared surface is rewritten; the ISR callbacks only signal
 * and count. Per-stage timings and the refresh count are logged. Returns
 * ESP_OK with the last frame still on screen. */
esp_err_t gba_display_stream_test_run(void);
