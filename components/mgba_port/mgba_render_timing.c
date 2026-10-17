/* See mgba_render_timing.h. Only standard entry points are wrapped, and each
 * trampoline tail-calls the saved backend entry with the backend pointer. */
#include "mgba_render_timing.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <mgba/core/timing.h>
#include <mgba/internal/arm/arm.h>
#include <mgba/internal/gba/audio.h>
#include <mgba/internal/gba/gba.h>
#include <mgba/internal/gba/video.h>

#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "mgba_timing";

static void (*s_draw)(struct GBAVideoRenderer *, int) = NULL;
static void (*s_finish)(struct GBAVideoRenderer *) = NULL;
static struct GBAVideoRenderer *s_backend = NULL;
static bool s_installed = false;

static uint64_t s_draw_us;
static uint32_t s_draw_calls;
static uint64_t s_finish_us;
static uint32_t s_finish_calls;
static uint64_t s_frame_cur_us;
static uint64_t s_frame_max_us;
static uint32_t s_frame_max_idx;
static uint32_t s_frames;

/* Defined with the DMA accounting below; closes one accounting frame from
 * the finish trampoline when frame accounting is on. */
static void account_frame_close(void);
static bool s_account_installed = false;

static void rt_draw(struct GBAVideoRenderer *renderer, int y) {
    (void)renderer;
    int64_t t0 = esp_timer_get_time();
    s_draw(s_backend, y);
    uint64_t dt = (uint64_t)(esp_timer_get_time() - t0);
    s_draw_us += dt;
    s_draw_calls++;
    s_frame_cur_us += dt;
}

static void rt_finish(struct GBAVideoRenderer *renderer) {
    (void)renderer;
    int64_t t0 = esp_timer_get_time();
    s_finish(s_backend);
    uint64_t dt = (uint64_t)(esp_timer_get_time() - t0);
    s_finish_us += dt;
    s_finish_calls++;
    /* finishFrame closes the frame (it runs once per completed frame when
     * frameskip does not skip it; skipped frames simply do not advance the
     * counter, which the frames= count makes visible). */
    s_frame_cur_us += dt;
    if (s_frame_cur_us > s_frame_max_us) {
        s_frame_max_us = s_frame_cur_us;
        s_frame_max_idx = s_frames + 1; /* 1-based, matches log lines */
    }
    s_frame_cur_us = 0;
    s_frames++;
    if (s_account_installed) {
        account_frame_close();
    }
}

void mgba_render_timing_install(struct mCore *core) {
    if (s_installed || core == NULL) {
        return;
    }
    struct GBA *gba = (struct GBA *)core->board;
    struct GBAVideoRenderer *backend = gba->video.renderer;
    if (backend == NULL || backend->drawScanline == NULL ||
        backend->finishFrame == NULL) {
        return;
    }
    s_backend = backend;
    s_draw = backend->drawScanline;
    s_finish = backend->finishFrame;
    backend->drawScanline = rt_draw;
    backend->finishFrame = rt_finish;
    s_draw_us = 0;
    s_draw_calls = 0;
    s_finish_us = 0;
    s_finish_calls = 0;
    s_frame_cur_us = 0;
    s_frame_max_us = 0;
    s_frame_max_idx = 0;
    s_frames = 0;
    s_installed = true;
}

void mgba_render_timing_uninstall(struct mCore *core) {
    if (!s_installed) {
        return;
    }
    if (core != NULL) {
        struct GBA *gba = (struct GBA *)core->board;
        /* The renderer pointer itself is never swapped, only its two entries
         * are patched, so restore in place. If something re-associated the
         * renderer meanwhile, leave it alone rather than corrupt it. */
        if (gba->video.renderer == s_backend) {
            s_backend->drawScanline = s_draw;
            s_backend->finishFrame = s_finish;
        }
    }
    s_backend = NULL;
    s_draw = NULL;
    s_finish = NULL;
    s_installed = false;
}

void mgba_render_timing_report(void) {
    ESP_LOGI(TAG,
             "render: draw_ms=%.1f calls=%u finish_ms=%.1f calls=%u "
             "frames=%u maxframe_ms=%.1f@%u",
             (double)s_draw_us / 1000.0, (unsigned)s_draw_calls,
             (double)s_finish_us / 1000.0, (unsigned)s_finish_calls,
             (unsigned)s_frames, (double)s_frame_max_us / 1000.0,
             (unsigned)s_frame_max_idx);
}

/* ----- DMA byte/call accounting + audio synthesis timing ----- */

struct mgba_mem_wrapped {
    uint32_t (*load32)(struct ARMCore *, uint32_t, int *);
    uint32_t (*load16)(struct ARMCore *, uint32_t, int *);
    uint32_t (*load8)(struct ARMCore *, uint32_t, int *);
    void (*store32)(struct ARMCore *, uint32_t, int32_t, int *);
    void (*store16)(struct ARMCore *, uint32_t, int16_t, int *);
    void (*store8)(struct ARMCore *, uint32_t, int8_t, int *);
    uint32_t (*loadMultiple)(struct ARMCore *, uint32_t, int,
                             enum LSMDirection, int *);
    uint32_t (*storeMultiple)(struct ARMCore *, uint32_t, int,
                              enum LSMDirection, int *);
};

static struct mgba_mem_wrapped s_mem;
static void (*s_sample)(struct mTiming *, void *, uint32_t) = NULL;
static void (*s_set_active_region)(struct ARMCore *, uint32_t) = NULL;
static struct GBA *s_gba = NULL;

static uint64_t s_dma_bytes;
static uint64_t s_dma_read_b;
static uint64_t s_dma_write_b;
static uint64_t s_dma_calls;
static uint64_t s_dma_frame_bytes;
static uint64_t s_dma_frame_max;
static uint32_t s_dma_frame_max_idx;
static uint32_t s_dma_frames;
static uint64_t s_audio_us;
static uint32_t s_audio_calls;
static uint64_t s_event_us;
static uint32_t s_event_calls;
/* Counts changes to GBAMemory.activeRegion made by setActiveRegion, bucketed
 * by the destination address's top-byte region. This is not fetch count or
 * time spent executing in a region. */
#define MGBA_REGION_BUCKETS 17
static uint64_t s_region_transitions[MGBA_REGION_BUCKETS];

static void rt_set_active_region(struct ARMCore *cpu, uint32_t address) {
    int old_region = s_gba->memory.activeRegion;
    s_set_active_region(cpu, address);
    int new_region = s_gba->memory.activeRegion;
    if (old_region != new_region) {
        unsigned bucket = new_region >= 0 && new_region < 16
                              ? (unsigned)new_region
                              : 16u;
        s_region_transitions[bucket]++;
    }
}

static void dma_note(unsigned bytes, int is_write) {
    /* Only the DMA service path sets performingDMA; normal CPU traffic pays
     * one predictable branch here and is never counted. */
    if (s_gba->performingDMA) {
        s_dma_bytes += bytes;
        s_dma_calls++;
        s_dma_frame_bytes += bytes;
        if (is_write) {
            s_dma_write_b += bytes;
        } else {
            s_dma_read_b += bytes;
        }
    }
}

static uint32_t rt_load32(struct ARMCore *cpu, uint32_t address, int *cc) {
    uint32_t v = s_mem.load32(cpu, address, cc);
    dma_note(4, 0);
    return v;
}

static uint32_t rt_load16(struct ARMCore *cpu, uint32_t address, int *cc) {
    uint32_t v = s_mem.load16(cpu, address, cc);
    dma_note(2, 0);
    return v;
}

static uint32_t rt_load8(struct ARMCore *cpu, uint32_t address, int *cc) {
    uint32_t v = s_mem.load8(cpu, address, cc);
    dma_note(1, 0);
    return v;
}

static void rt_store32(struct ARMCore *cpu, uint32_t address, int32_t value,
                       int *cc) {
    s_mem.store32(cpu, address, value, cc);
    dma_note(4, 1);
}

static void rt_store16(struct ARMCore *cpu, uint32_t address, int16_t value,
                       int *cc) {
    s_mem.store16(cpu, address, value, cc);
    dma_note(2, 1);
}

static void rt_store8(struct ARMCore *cpu, uint32_t address, int8_t value,
                      int *cc) {
    s_mem.store8(cpu, address, value, cc);
    dma_note(1, 1);
}

static uint32_t rt_loadMultiple(struct ARMCore *cpu, uint32_t base, int mask,
                                enum LSMDirection dir, int *cc) {
    uint32_t v = s_mem.loadMultiple(cpu, base, mask, dir, cc);
    dma_note((unsigned)__builtin_popcount((unsigned)mask) * 4u, 0);
    return v;
}

static uint32_t rt_storeMultiple(struct ARMCore *cpu, uint32_t base, int mask,
                                 enum LSMDirection dir, int *cc) {
    uint32_t v = s_mem.storeMultiple(cpu, base, mask, dir, cc);
    dma_note((unsigned)__builtin_popcount((unsigned)mask) * 4u, 1);
    return v;
}

static void rt_sample(struct mTiming *timing, void *context, uint32_t late) {
    int64_t t0 = esp_timer_get_time();
    s_sample(timing, context, late);
    s_audio_us += (uint64_t)(esp_timer_get_time() - t0);
    s_audio_calls++;
}

static void (*s_process_events)(struct ARMCore *) = NULL;

static void rt_process_events(struct ARMCore *cpu) {
    int64_t t0 = esp_timer_get_time();
    s_process_events(cpu);
    s_event_us += (uint64_t)(esp_timer_get_time() - t0);
    s_event_calls++;
}

/* Close one accounting frame. Called from the finish trampoline when frame
 * accounting is on, so DMA per-frame maxima stay aligned with rendered
 * frames even though counting itself is renderer-independent. */
static void account_frame_close(void) {
    if (s_dma_frame_bytes > s_dma_frame_max) {
        s_dma_frame_max = s_dma_frame_bytes;
        s_dma_frame_max_idx = s_dma_frames + 1; /* 1-based */
    }
    s_dma_frame_bytes = 0;
    s_dma_frames++;
}

void mgba_frame_account_install(struct mCore *core) {
    if (s_account_installed || core == NULL) {
        return;
    }
    struct GBA *gba = (struct GBA *)core->board;
    struct ARMCore *cpu = (struct ARMCore *)core->cpu;
    if (gba == NULL || cpu == NULL) {
        return;
    }
    s_gba = gba;
    s_mem.load32 = cpu->memory.load32;
    s_mem.load16 = cpu->memory.load16;
    s_mem.load8 = cpu->memory.load8;
    s_mem.store32 = cpu->memory.store32;
    s_mem.store16 = cpu->memory.store16;
    s_mem.store8 = cpu->memory.store8;
    s_mem.loadMultiple = cpu->memory.loadMultiple;
    s_mem.storeMultiple = cpu->memory.storeMultiple;
    cpu->memory.load32 = rt_load32;
    cpu->memory.load16 = rt_load16;
    cpu->memory.load8 = rt_load8;
    cpu->memory.store32 = rt_store32;
    cpu->memory.store16 = rt_store16;
    cpu->memory.store8 = rt_store8;
    cpu->memory.loadMultiple = rt_loadMultiple;
    cpu->memory.storeMultiple = rt_storeMultiple;
    s_sample = gba->audio.sampleEvent.callback;
    gba->audio.sampleEvent.callback = rt_sample;
    s_process_events = cpu->irqh.processEvents;
    cpu->irqh.processEvents = rt_process_events;
    s_set_active_region = cpu->memory.setActiveRegion;
    if (s_set_active_region != NULL) {
        cpu->memory.setActiveRegion = rt_set_active_region;
    }
    s_dma_bytes = 0;
    s_dma_read_b = 0;
    s_dma_write_b = 0;
    s_dma_calls = 0;
    s_dma_frame_bytes = 0;
    s_dma_frame_max = 0;
    s_dma_frame_max_idx = 0;
    s_dma_frames = 0;
    s_audio_us = 0;
    s_audio_calls = 0;
    s_event_us = 0;
    s_event_calls = 0;
    for (unsigned i = 0; i < MGBA_REGION_BUCKETS; ++i) {
        s_region_transitions[i] = 0;
    }
    s_account_installed = true;
    /* Frame boundaries come from the finish trampoline; ensure it is active
     * while accounting runs, independent of the render-timing flag. */
    mgba_render_timing_install(core);
}

void mgba_frame_account_uninstall(struct mCore *core) {
    if (!s_account_installed) {
        return;
    }
    if (core != NULL) {
        struct GBA *gba = (struct GBA *)core->board;
        struct ARMCore *cpu = (struct ARMCore *)core->cpu;
        if (gba != NULL && cpu != NULL) {
            cpu->memory.load32 = s_mem.load32;
            cpu->memory.load16 = s_mem.load16;
            cpu->memory.load8 = s_mem.load8;
            cpu->memory.store32 = s_mem.store32;
            cpu->memory.store16 = s_mem.store16;
            cpu->memory.store8 = s_mem.store8;
            cpu->memory.loadMultiple = s_mem.loadMultiple;
            cpu->memory.storeMultiple = s_mem.storeMultiple;
            gba->audio.sampleEvent.callback = s_sample;
            cpu->irqh.processEvents = s_process_events;
            if (cpu->memory.setActiveRegion == rt_set_active_region) {
                cpu->memory.setActiveRegion = s_set_active_region;
            }
        }
    }
    s_gba = NULL;
    s_sample = NULL;
    s_process_events = NULL;
    s_set_active_region = NULL;
    s_account_installed = false;
    mgba_render_timing_uninstall(core);
}

void mgba_frame_account_report(void) {
    ESP_LOGI(TAG,
             "dma: bytes=%llu (r=%llu w=%llu) calls=%llu frames=%u "
             "maxframe_bytes=%llu@%u",
             (unsigned long long)s_dma_bytes,
             (unsigned long long)s_dma_read_b,
             (unsigned long long)s_dma_write_b,
             (unsigned long long)s_dma_calls, (unsigned)s_dma_frames,
             (unsigned long long)s_dma_frame_max,
             (unsigned)s_dma_frame_max_idx);
    ESP_LOGI(TAG, "audio: synth_ms=%.1f events=%u",
             (double)s_audio_us / 1000.0, (unsigned)s_audio_calls);
    ESP_LOGI(TAG, "events: process_ms=%.1f calls=%u",
             (double)s_event_us / 1000.0, (unsigned)s_event_calls);
    ESP_LOGI(TAG,
             "active-region transitions (setActiveRegion changes; not fetch/time): "
             "BIOS=%llu WRAM=%llu IRAM=%llu IO=%llu PAL=%llu VRAM=%llu "
             "OAM=%llu CART0=%llu CART0EX=%llu CART1=%llu CART1EX=%llu "
             "CART2=%llu CART2EX=%llu SRAM=%llu SRAMmirror=%llu invalid=%llu",
             (unsigned long long)s_region_transitions[0],
             (unsigned long long)s_region_transitions[2],
             (unsigned long long)s_region_transitions[3],
             (unsigned long long)s_region_transitions[4],
             (unsigned long long)s_region_transitions[5],
             (unsigned long long)s_region_transitions[6],
             (unsigned long long)s_region_transitions[7],
             (unsigned long long)s_region_transitions[8],
             (unsigned long long)s_region_transitions[9],
             (unsigned long long)s_region_transitions[10],
             (unsigned long long)s_region_transitions[11],
             (unsigned long long)s_region_transitions[12],
             (unsigned long long)s_region_transitions[13],
             (unsigned long long)s_region_transitions[14],
             (unsigned long long)s_region_transitions[15],
             (unsigned long long)s_region_transitions[16]);
}

/* Snapshot of every accumulator, diffed by mgba_frame_account_end. */
static struct {
    uint64_t draw_us;
    uint32_t draw_calls;
    uint64_t finish_us;
    uint64_t dma_read_b;
    uint64_t dma_write_b;
    uint64_t dma_calls;
    uint64_t event_us;
    uint64_t audio_us;
    uint32_t audio_calls;
    uint64_t region_transitions[MGBA_REGION_BUCKETS];
} s_snap;

static uint32_t sat_u64(uint64_t v) {
    return v > (uint64_t)UINT32_MAX ? UINT32_MAX : (uint32_t)v;
}

void mgba_frame_account_begin(void) {
    s_snap.draw_us = s_draw_us;
    s_snap.draw_calls = s_draw_calls;
    s_snap.finish_us = s_finish_us;
    s_snap.dma_read_b = s_dma_read_b;
    s_snap.dma_write_b = s_dma_write_b;
    s_snap.dma_calls = s_dma_calls;
    s_snap.event_us = s_event_us;
    s_snap.audio_us = s_audio_us;
    s_snap.audio_calls = s_audio_calls;
    for (unsigned i = 0; i < MGBA_REGION_BUCKETS; ++i) {
        s_snap.region_transitions[i] = s_region_transitions[i];
    }
}

void mgba_frame_account_end(mgba_frame_acct_t *out) {
    if (out == NULL) {
        return;
    }
    out->draw_us = sat_u64(s_draw_us - s_snap.draw_us);
    out->draw_calls = s_draw_calls - s_snap.draw_calls;
    out->finish_us = sat_u64(s_finish_us - s_snap.finish_us);
    out->dma_read_b = sat_u64(s_dma_read_b - s_snap.dma_read_b);
    out->dma_write_b = sat_u64(s_dma_write_b - s_snap.dma_write_b);
    out->dma_calls = sat_u64(s_dma_calls - s_snap.dma_calls);
    out->event_us = sat_u64(s_event_us - s_snap.event_us);
    out->audio_us = sat_u64(s_audio_us - s_snap.audio_us);
    out->audio_calls = s_audio_calls - s_snap.audio_calls;
    for (unsigned i = 0; i < MGBA_REGION_BUCKETS; ++i) {
        out->region_transitions[i] =
            sat_u64(s_region_transitions[i] - s_snap.region_transitions[i]);
    }
}
