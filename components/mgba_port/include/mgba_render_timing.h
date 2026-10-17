/* Observational timing/accounting shims for the GBA core.
 *
 * All hooks are in-place trampolines over live upstream function pointers: no
 * upstream source touched, no objects copied, backend callbacks always receive
 * the backend pointers they expect. Nothing is skipped, reordered, or altered,
 * so framebuffer CRCs must be identical with any combination on or off.
 * Install after core->reset() and uninstall before unload/deinit. Single core
 * at a time; same task.
 *
 * Three independent features, each gated by its own build flag at the call
 * site (this translation unit always builds all three):
 *   render timing  drawScanline/finishFrame wall time + slowest frame
 *   DMA accounting byte/call totals for emulated DMA only (wrappers check
 *                  performingDMA, so normal CPU traffic costs one predictable
 *                  branch and is never counted)
 *   audio timing   wall time and event count of the audio sample callback
 *   region accounting counts changes to mGBA's active GBA memory region via
 *                   ARM setActiveRegion (entry counts, not instruction fetch
 *                   counts or time spent executing in a region)
 */
#pragma once

#include <mgba/core/core.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Patch the live renderer's drawScanline/finishFrame; zero the accumulators.
 * No-op when already installed or when the backend lacks either entry. */
void mgba_render_timing_install(struct mCore *core);

/* Restore the saved entry points. No-op when not installed. */
void mgba_render_timing_uninstall(struct mCore *core);

/* Log totals: draw/finish time and call counts, frame count, and the slowest
 * frame by draw+finish time. Safe to call with nothing installed (zeros). */
void mgba_render_timing_report(void);

/* Wrap the CPU memory accessors to count emulated-DMA bytes/calls, and the
 * audio sample event to time synthesis. Zero the accumulators. No-op when
 * already installed. */
void mgba_frame_account_install(struct mCore *core);

/* Restore all wrapped entry points. No-op when not installed. */
void mgba_frame_account_uninstall(struct mCore *core);

/* Log DMA bytes/calls (+ slowest frame by DMA bytes) and audio synthesis
 * time/calls. Safe to call with nothing installed (zeros). */
void mgba_frame_account_report(void);

/* Per-runFrame accounting snapshot, diffed by the caller around each
 * core->runFrame() call. Frame numbering here is 1-based: the first
 * completed frame is frame 1, matching the stream loop's log lines. */
typedef struct {
    uint32_t draw_us;
    uint32_t draw_calls;
    uint32_t finish_us;
    uint32_t dma_read_b;
    uint32_t dma_write_b;
    uint32_t dma_calls;
    uint32_t event_us;
    uint32_t audio_us;
    uint32_t audio_calls;
    /* Per-runFrame destination-region changes observed at setActiveRegion.
     * Index by the GBA address region number (0x0..0xF); index 16 is the
     * invalid/unmapped destination bucket. These are transition counts, not
     * fetch counts or time/cycle shares. */
    uint32_t region_transitions[17];
} mgba_frame_acct_t;

/* Snapshot all accumulators; call immediately before core->runFrame(). */
void mgba_frame_account_begin(void);

/* Diff the accumulators against the begin snapshot into *out; call
 * immediately after core->runFrame(). Saturates at UINT32_MAX per field. */
void mgba_frame_account_end(mgba_frame_acct_t *out);

#ifdef __cplusplus
}
#endif
