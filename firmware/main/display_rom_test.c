/* GBA frame display tests on the Tab5 panel.
 *
 * Two entry points share one ROM session helper:
 *   - gba_display_rom_test_run(): run 600 no-input frames, verify the
 *     framebuffer CRC, then show one still frame.
 *   - gba_display_stream_test_run(): display every emulated frame, scaled into
 *     the back DPI frame buffer and swapped in at the frame boundary, recording
 *     per-stage timings.
 *
 * Both are silent (no audio output), take no input, never save and never write
 * to the SD card.
 */
#include "display_test.h"

#include <fcntl.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#ifdef GBA_DISPLAY_PPA_STREAM_TEST
#include "driver/ppa.h"
#endif
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "gba_scale.h"
#include "gba_sd.h"

#include <mgba-util/crc32.h>
#include <mgba-util/vfs.h>
#include <mgba/core/config.h>
#include <mgba/core/core.h>
#include <mgba/core/log.h>
#include <mgba/gba/core.h>

#if defined(GBA_RENDER_TIMING) || defined(GBA_FRAME_ACCOUNT)
#include "mgba_render_timing.h"
#endif

#ifdef GBA_IDLE_DETECT
#include <mgba/internal/gba/gba.h>
#endif

static const char *TAG = "gba_display_rom";

/* Panel layout: GBA frame rotated 90 deg counter-clockwise, aspect-fit at
 * 4.5x. The rotated image is 160x240, so 4.5x gives a 720x1080 rect that
 * fills the panel width exactly and centres vertically with 100 px black
 * bands top and bottom. Rendered with gba_rotate_ccw45x_rgb565_nn_into();
 * the source top edge becomes the left edge (up becomes left). */
#define GBA_SCALED_W (GBA_FRAME_HEIGHT / 2u * 9u)
#define GBA_SCALED_H (GBA_FRAME_WIDTH / 2u * 9u)
#define GBA_SCALED_Y ((GBA_DISPLAY_V_RES - GBA_SCALED_H) / 2)

#ifdef GBA_DISPLAY_PPA_STREAM_TEST
static esp_err_t ppa_convert_gba_frame(ppa_client_handle_t client,
                                       const uint16_t *source,
                                       uint16_t *panel_fb) {
    ppa_srm_oper_config_t op = {
        .in.buffer = source,
        .in.pic_w = GBA_FRAME_WIDTH,
        .in.pic_h = GBA_FRAME_HEIGHT,
        .in.block_w = GBA_FRAME_WIDTH,
        .in.block_h = GBA_FRAME_HEIGHT,
        .in.block_offset_x = 0,
        .in.block_offset_y = 0,
        .in.srm_cm = PPA_SRM_COLOR_MODE_RGB565,
        .out.buffer = panel_fb,
        .out.buffer_size = GBA_DISPLAY_PIXELS * sizeof(uint16_t),
        .out.pic_w = GBA_DISPLAY_H_RES,
        .out.pic_h = GBA_DISPLAY_V_RES,
        .out.block_offset_x = 0,
        .out.block_offset_y = GBA_SCALED_Y,
        .out.srm_cm = PPA_SRM_COLOR_MODE_RGB565,
        .rotation_angle = PPA_SRM_ROTATION_ANGLE_90,
        .scale_x = 4.5f,
        .scale_y = 4.5f,
        .mirror_x = false,
        .mirror_y = false,
        .rgb_swap = false,
        .byte_swap = false,
        .alpha_update_mode = PPA_ALPHA_NO_CHANGE,
        .mode = PPA_TRANS_MODE_BLOCKING,
        .user_data = NULL,
    };
    return ppa_do_scale_rotate_mirror(client, &op);
}
#endif

_Static_assert(GBA_SCALED_W == GBA_DISPLAY_H_RES,
               "rotated GBA width must exactly fill the panel width");
_Static_assert(GBA_SCALED_Y * 2 + GBA_SCALED_H == GBA_DISPLAY_V_RES,
               "GBA image must be vertically centred with equal black bands");

static void discard_mgba_log(struct mLogger *logger, int category,
                             enum mLogLevel level, const char *format,
                             va_list args) {
    (void)logger;
    (void)category;
    (void)level;
    (void)format;
    (void)args;
}

static struct mLogger quiet_mgba_logger = {
    .log = discard_mgba_log,
    .filter = NULL,
};

static uint32_t framebuffer_crc(const color_t *fb) {
    return doCrc32(fb, GBA_FRAME_PIXELS * sizeof(color_t));
}

/* ----- shared ROM session ------------------------------------------------- */

typedef struct {
    sdmmc_card_t *card;
    sd_pwr_ctrl_handle_t pwr_ctrl;
    struct mCore *core;
    color_t *framebuffer;      /* 240x160 RGB565, PSRAM */
    unsigned char *rom_image;  /* 8 MiB, PSRAM */
    bool core_initialized;
    bool config_initialized;
    bool rom_loaded;
} gba_rom_session_t;

/* Mount the SD card, verify and load the ROM, and set up the framebuffer.
 * Leaves the core ready to run frames. On failure everything acquired so far is
 * released before returning. */
static esp_err_t rom_session_open(gba_rom_session_t *s) {
    memset(s, 0, sizeof(*s));
    mLogSetDefaultLogger(&quiet_mgba_logger);

    esp_err_t err = gba_sd_mount(&s->card, &s->pwr_ctrl);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "SD mount failed; no format/write attempted: %s",
                 esp_err_to_name(err));
        return err;
    }

    s->core = GBACoreCreate();
    if (!s->core) {
        ESP_LOGE(TAG, "GBACoreCreate failed");
        goto fail;
    }
    if (!s->core->init(s->core)) {
        ESP_LOGE(TAG, "mGBA core init failed");
        free(s->core);
        s->core = NULL;
        goto fail;
    }
    s->core_initialized = true;
    /* Match host/gba-smoke.c and avoid machine/user-specific config files. */
    mCoreInitConfig(s->core, "gbaSmoke");
    s->config_initialized = true;
    mCoreConfigSetDefaultValue(&s->core->config, "idleOptimization", "remove");
#ifdef GBA_IDLE_DETECT
    /* GBACreate defaults to REMOVE with idleLoop NONE, a combination that
     * never halts, so busy-wait loops burn full cycles. DETECT lets the core
     * analyse and skip them; framebuffer CRCs must still match. */
    ((struct GBA *)s->core->board)->idleOptimization = IDLE_LOOP_DETECT;
    ESP_LOGI(TAG, "idleOptimization=detect");
#else
    ESP_LOGI(TAG, "idleOptimization=remove (default)");
#endif

    struct VFile *sd_vf = VFileOpen(GBA_ROM_PATH, O_RDONLY);
    if (!sd_vf) {
        ESP_LOGE(TAG, "ROM open failed: %s", GBA_ROM_PATH);
        goto fail;
    }
    ssize_t file_size = sd_vf->size(sd_vf);
    if (file_size != (ssize_t)GBA_ROM_SIZE) {
        ESP_LOGE(TAG, "ROM size mismatch: got=%ld expected=%u",
                 (long)file_size, (unsigned)GBA_ROM_SIZE);
        sd_vf->close(sd_vf);
        goto fail;
    }
    s->rom_image =
#ifdef GBA_ALIGNED_ROM_ALLOC
        heap_caps_aligned_alloc(64, GBA_ROM_SIZE,
                                MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#else
        heap_caps_malloc(GBA_ROM_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#endif
    if (!s->rom_image) {
        ESP_LOGE(TAG, "PSRAM ROM allocation failed: %u bytes",
                 (unsigned)GBA_ROM_SIZE);
        sd_vf->close(sd_vf);
        goto fail;
    }
    char sha256[65];
    bool loaded = gba_rom_load_image(sd_vf, GBA_ROM_SIZE, s->rom_image, sha256);
    sd_vf->close(sd_vf);
    if (!loaded) {
        ESP_LOGE(TAG, "ROM read/hash failed or file changed during read");
        goto fail;
    }
    ESP_LOGI(TAG, "PSRAM ROM image SHA-256: %s (%s)", sha256, GBA_ROM_LABEL);
    if (strcmp(sha256, GBA_ROM_SHA256) != 0) {
        ESP_LOGE(TAG, "ROM hash mismatch; refusing core load");
        goto fail;
    }

    struct VFile *rom_vf = VFileFromConstMemory(s->rom_image, GBA_ROM_SIZE);
    if (!rom_vf) {
        ESP_LOGE(TAG, "ROM memory VFile allocation failed");
        goto fail;
    }
    if (!s->core->loadROM(s->core, rom_vf)) {
        ESP_LOGE(TAG, "mGBA ROM load failed");
        rom_vf->close(rom_vf);
        goto fail;
    }
    /* Core owns the VFile now; the backing PSRAM image remains ours. */
    s->rom_loaded = true;

    if (s->core->romSize(s->core) != GBA_ROM_SIZE) {
        ESP_LOGE(TAG, "core ROM size mismatch: %u",
                 (unsigned)s->core->romSize(s->core));
        goto fail;
    }

    s->framebuffer = heap_caps_malloc(GBA_FRAME_PIXELS * sizeof(color_t),
                                      MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s->framebuffer) {
        ESP_LOGE(TAG, "PSRAM framebuffer allocation failed");
        goto fail;
    }
    s->core->setVideoBuffer(s->core, s->framebuffer, GBA_FRAME_WIDTH);
    s->core->reset(s->core);
    s->core->clearKeys(s->core, 0xFFFFFFFFu);
    return ESP_OK;

fail:
    /* Nothing is displayed yet, so free the framebuffer too. */
    if (s->framebuffer) {
        heap_caps_free(s->framebuffer);
        s->framebuffer = NULL;
    }
    if (s->core) {
        if (s->rom_loaded) s->core->unloadROM(s->core);
        if (s->config_initialized) mCoreConfigDeinit(&s->core->config);
        if (s->core_initialized) s->core->deinit(s->core);
        else free(s->core);
        s->core = NULL;
    }
    if (s->rom_image) {
        heap_caps_free(s->rom_image);
        s->rom_image = NULL;
    }
    if (s->card) {
        gba_sd_unmount(s->card, s->pwr_ctrl);
        s->card = NULL;
        s->pwr_ctrl = NULL;
    }
    return ESP_FAIL;
}

/* Release the ROM, core and SD card but keep the framebuffer, which is the
 * verified image the caller still needs. */
static esp_err_t rom_session_release_core(gba_rom_session_t *s) {
    s->core->unloadROM(s->core);
    s->rom_loaded = false;
    mCoreConfigDeinit(&s->core->config);
    s->config_initialized = false;
    s->core->deinit(s->core);
    s->core_initialized = false;
    s->core = NULL;
    heap_caps_free(s->rom_image);
    s->rom_image = NULL;
    esp_err_t err = gba_sd_unmount(s->card, s->pwr_ctrl);
    s->card = NULL;
    s->pwr_ctrl = NULL;
    return err;
}

/* Release everything, optionally including the framebuffer (which must be kept
 * while the image it produced is on screen). */
static void rom_session_close(gba_rom_session_t *s, bool free_framebuffer) {
    if (s->core) {
        if (s->rom_loaded) s->core->unloadROM(s->core);
        if (s->config_initialized) mCoreConfigDeinit(&s->core->config);
        if (s->core_initialized) s->core->deinit(s->core);
        else free(s->core);
        s->core = NULL;
    }
    if (s->rom_image) {
        heap_caps_free(s->rom_image);
        s->rom_image = NULL;
    }
    if (s->card) {
        gba_sd_unmount(s->card, s->pwr_ctrl);
        s->card = NULL;
        s->pwr_ctrl = NULL;
    }
    if (free_framebuffer && s->framebuffer) {
        heap_caps_free(s->framebuffer);
        s->framebuffer = NULL;
    }
}

/* ----- still frame -------------------------------------------------------- */

esp_err_t gba_display_rom_test_run(void) {
    const size_t psram_before = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    ESP_LOGI(TAG, "silent SD ROM -> panel still-frame test; "
                  "no audio/input/save/SD-write");

    gba_rom_session_t session;
    uint16_t *surface = NULL;
    bool panel_ready = false;
    esp_err_t ret = ESP_FAIL;

    esp_err_t err = rom_session_open(&session);
    if (err != ESP_OK) {
        return err;
    }

    static const struct {
        uint32_t frame;
        uint32_t crc;
    } goldens[] = {
        {1u, GBA_ROM_GOLDEN_CRC_FRAME1},
        {60u, GBA_ROM_GOLDEN_CRC_FRAME60},
        {GBA_ROM_FRAME_COUNT, GBA_ROM_GOLDEN_CRC_LAST},
    };
    bool crc_ok = true;
    for (uint32_t i = 1; i <= GBA_ROM_FRAME_COUNT; ++i) {
        session.core->runFrame(session.core);
        for (size_t g = 0; g < sizeof(goldens) / sizeof(goldens[0]); ++g) {
            if (goldens[g].frame != i) {
                continue;
            }
            uint32_t crc = framebuffer_crc(session.framebuffer);
            bool match = crc == goldens[g].crc;
            crc_ok &= match;
            ESP_LOGI(TAG,
                     "frame=%" PRIu32 " rgb565_crc32=%08" PRIx32
                     " golden=%08" PRIx32 " %s",
                     i, crc, goldens[g].crc, match ? "match" : "MISMATCH");
        }
        /* Keep IDLE/watchdog and other tasks alive between frames. */
        if (i < GBA_ROM_FRAME_COUNT) {
            vTaskDelay(1);
        }
    }
    uint32_t frames = session.core->frameCounter(session.core);
    if (frames != GBA_ROM_FRAME_COUNT) {
        ESP_LOGE(TAG, "frame counter mismatch: got=%" PRIu32 " expected=%u",
                 frames, (unsigned)GBA_ROM_FRAME_COUNT);
        goto cleanup;
    }
    if (!crc_ok) {
        ESP_LOGE(TAG, "frame CRC deviates from the host golden; not displaying");
        goto cleanup;
    }

    /* The framebuffer now holds the reference image. Release the ROM and core
     * (and the SD card) so the panel buffers fit comfortably, but keep the
     * framebuffer: it is re-checked after the transfer. */
    if (rom_session_release_core(&session) != ESP_OK) {
        ESP_LOGE(TAG, "SD unmount/power teardown failed");
        goto cleanup;
    }
    ESP_LOGI(TAG, "ROM/core released; psram_free=%u",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));

    err = gba_display_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "display init failed: %s", esp_err_to_name(err));
        goto cleanup;
    }
    panel_ready = true;

    surface = heap_caps_malloc((size_t)GBA_DISPLAY_PIXELS * sizeof(uint16_t),
                               MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!surface) {
        ESP_LOGE(TAG, "PSRAM panel surface allocation failed (%u bytes)",
                 (unsigned)(GBA_DISPLAY_PIXELS * sizeof(uint16_t)));
        goto cleanup;
    }
    for (size_t i = 0; i < GBA_DISPLAY_PIXELS; ++i) {
        surface[i] = 0x0000;
    }
    if (!gba_rotate_ccw45x_rgb565_nn_into(
            (const uint16_t *)session.framebuffer, GBA_FRAME_WIDTH,
            GBA_FRAME_HEIGHT,
            surface + (size_t)GBA_SCALED_Y * GBA_DISPLAY_H_RES,
            GBA_DISPLAY_H_RES)) {
        ESP_LOGE(TAG, "rotating/scaling the GBA frame to the panel surface failed");
        goto cleanup;
    }

    err = gba_display_present_full(surface);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "present failed: %s", esp_err_to_name(err));
        goto cleanup;
    }
    /* Only now reveal the panel: the frame has been verified and copied. */
    err = gba_display_backlight_set(GBA_DISPLAY_BRIGHTNESS_PERCENT);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "backlight set failed: %s", esp_err_to_name(err));
        goto cleanup;
    }

    uint32_t after_crc = framebuffer_crc(session.framebuffer);
    ESP_LOGI(TAG,
             "source framebuffer CRC after scaling+present: %08" PRIx32
             " (%s)",
             after_crc,
             after_crc == GBA_ROM_GOLDEN_CRC_LAST ? "unchanged" : "CHANGED");
    if (after_crc != GBA_ROM_GOLDEN_CRC_LAST) {
        ESP_LOGE(TAG, "source framebuffer changed during scaling/present");
        goto cleanup;
    }

    ret = ESP_OK;
    ESP_LOGI(TAG,
             "still frame shown at %d%%; layout %dx%d at y=%d; "
             "psram_free=%u (was %u)",
             GBA_DISPLAY_BRIGHTNESS_PERCENT, GBA_SCALED_W, GBA_SCALED_H,
             GBA_SCALED_Y, (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
             (unsigned)psram_before);

cleanup:
    if (ret != ESP_OK) {
        if (surface) {
            heap_caps_free(surface);
        }
        if (panel_ready) {
            gba_display_deinit();
        }
        rom_session_close(&session, true);
    } else {
        /* Keep framebuffer and surface: the image stays on screen. */
        rom_session_close(&session, false);
    }
    return ret;
}

/* ----- continuous display ------------------------------------------------- */

static int compare_u32(const void *left, const void *right) {
    uint32_t a = *(const uint32_t *)left;
    uint32_t b = *(const uint32_t *)right;
    return (a > b) - (a < b);
}

static void report_stage(const char *name, uint32_t *samples, uint32_t n,
                         uint64_t total_us) {
    qsort(samples, n, sizeof(*samples), compare_u32);
    uint32_t median = (n & 1u) ? samples[n / 2]
                               : (samples[n / 2 - 1] + samples[n / 2]) / 2;
    uint32_t p95 = samples[(n * 95u + 99u) / 100u - 1u];
    ESP_LOGI(TAG,
             "stage=%-8s avg_us=%.1f median_us=%u p95_us=%u max_us=%u",
             name, (double)total_us / n, (unsigned)median, (unsigned)p95,
             (unsigned)samples[n - 1]);
}

esp_err_t gba_display_stream_test_run(void) {
    const size_t psram_before = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    ESP_LOGI(TAG, "silent SD ROM -> continuous panel display test "
                  "(double buffered); no audio/input/save/SD-write; "
                  "crc_verify="
#ifdef GBA_STREAM_NO_VERIFY
                  "off (timing only, pixels unverified)"
#else
                  "on"
#endif
                  );

    gba_rom_session_t session;
    uint16_t *fb[2] = {NULL, NULL};
    uint32_t *run_us = NULL;
    uint32_t *crc_us = NULL;
    uint32_t *scale_us = NULL;
    uint32_t *present_us = NULL;
    uint32_t *wait_us = NULL;
#ifdef GBA_FRAME_ACCOUNT
    /* Per-frame core breakdown (only filled with GBA_FRAME_ACCOUNT). */
    uint32_t *f_draw_us = NULL;
    uint32_t *f_dma_r = NULL;
    uint32_t *f_dma_w = NULL;
    uint32_t *f_event_us = NULL;
    uint32_t *f_audio_us = NULL;
#endif
    bool panel_ready = false;
    bool sync_enabled = false;
#ifdef GBA_DISPLAY_PPA_STREAM_TEST
    ppa_client_handle_t ppa_client = NULL;
#endif
    esp_err_t ret = ESP_FAIL;

    const size_t sample_arrays = 5
#ifdef GBA_FRAME_ACCOUNT
                                 + 5
#endif
        ;
    const size_t samples_bytes = GBA_ROM_FRAME_COUNT * sizeof(uint32_t) *
                                 sample_arrays;
    uint32_t *samples = heap_caps_malloc(samples_bytes, MALLOC_CAP_SPIRAM);
    if (!samples) {
        ESP_LOGE(TAG, "PSRAM timing table allocation failed (%u bytes)",
                 (unsigned)samples_bytes);
        return ESP_FAIL;
    }
    run_us = samples;
    crc_us = run_us + GBA_ROM_FRAME_COUNT;
    scale_us = crc_us + GBA_ROM_FRAME_COUNT;
    present_us = scale_us + GBA_ROM_FRAME_COUNT;
    wait_us = present_us + GBA_ROM_FRAME_COUNT;
#ifdef GBA_FRAME_ACCOUNT
    f_draw_us = wait_us + GBA_ROM_FRAME_COUNT;
    f_dma_r = f_draw_us + GBA_ROM_FRAME_COUNT;
    f_dma_w = f_dma_r + GBA_ROM_FRAME_COUNT;
    f_event_us = f_dma_w + GBA_ROM_FRAME_COUNT;
    f_audio_us = f_event_us + GBA_ROM_FRAME_COUNT;
#endif

    esp_err_t err = rom_session_open(&session);
    if (err != ESP_OK) {
        heap_caps_free(samples);
        return err;
    }

    err = gba_display_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "display init failed: %s", esp_err_to_name(err));
        goto cleanup;
    }
    panel_ready = true;

    err = gba_display_sync_enable();
    if (err != ESP_OK) {
        goto cleanup;
    }
    sync_enabled = true;

    /* Emulate into the GBA framebuffer, scale straight into whichever driver
     * frame buffer is not currently front, and present it. Because the driver
     * recognises the pointer as one of its own buffers, that costs only a cache
     * write-back (no DMA2D copy) and the swap happens at the next frame
     * boundary, so a torn frame is never scanned out. */
    err = gba_display_get_frame_buffers(&fb[0], &fb[1]);
    if (err != ESP_OK) {
        goto cleanup;
    }

#ifdef GBA_DISPLAY_PPA_STREAM_TEST
    ppa_client_config_t ppa_cfg = {
        .oper_type = PPA_OPERATION_SRM,
        .max_pending_trans_num = 1,
        .data_burst_length = PPA_DATA_BURST_LENGTH_128,
    };
    err = ppa_register_client(&ppa_cfg, &ppa_client);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "PPA SRM client registration failed: %s",
                 esp_err_to_name(err));
        goto cleanup;
    }
    ESP_LOGI(TAG, "PPA SRM active: CCW 90 + 4.5x into panel buffers; "
                  "emulator framebuffer CRCs remain the CPU reference");
#endif

    /* The driver pre-clears both buffers to black, so the 100 px bands are
     * already correct and are never rewritten. Put fb[0] up, then reveal the
     * panel. */
    uint16_t *front = fb[0] + (size_t)GBA_SCALED_Y * GBA_DISPLAY_H_RES;
    err = gba_display_present_region(0, GBA_SCALED_Y, GBA_SCALED_W,
                                     GBA_SCALED_H, front);
    if (err == ESP_OK &&
        gba_display_wait_refresh(GBA_DISPLAY_SYNC_TIMEOUT_MS) != ESP_OK) {
        ESP_LOGE(TAG, "initial refresh timed out");
        err = ESP_ERR_TIMEOUT;
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "making fb0 the front buffer failed: %s",
                 esp_err_to_name(err));
        goto cleanup;
    }
    err = gba_display_backlight_set(GBA_DISPLAY_BRIGHTNESS_PERCENT);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "backlight set failed: %s", esp_err_to_name(err));
        goto cleanup;
    }

    uint64_t run_total = 0, crc_total = 0, scale_total = 0, present_total = 0,
             wait_total = 0;
    uint32_t crc_mismatches = 0;
    uint32_t refresh_timeouts = 0;
    /* Slowest frames by runFrame time, for spike attribution against the
     * render/DMA/audio maxima (which carry their own frame indices). */
    uint32_t run_slow_idx[3] = {0, 0, 0};
    uint32_t run_slow_us[3] = {0, 0, 0};
    int index = 1; /* fb[0] is front, so draw into fb[1] */
    bool ok = true;
    uint32_t previous_present_snapshot = 0;

    const int64_t stream_start_us = esp_timer_get_time();
#if defined(GBA_FRAME_ACCOUNT)
    mgba_frame_account_install(session.core);
#elif defined(GBA_RENDER_TIMING)
    mgba_render_timing_install(session.core);
#endif

    for (uint32_t i = 0; i < GBA_ROM_FRAME_COUNT && ok; ++i) {
#ifdef GBA_FRAME_ACCOUNT
        mgba_frame_account_begin();
#endif
        int64_t t0 = esp_timer_get_time();
        session.core->runFrame(session.core);
        int64_t t1 = esp_timer_get_time();
#ifdef GBA_FRAME_ACCOUNT
        mgba_frame_acct_t facct;
        mgba_frame_account_end(&facct);
        f_draw_us[i] = facct.draw_us;
        f_dma_r[i] = facct.dma_read_b;
        f_dma_w[i] = facct.dma_write_b;
        f_event_us[i] = facct.event_us;
        f_audio_us[i] = facct.audio_us;
#endif

#ifdef GBA_STREAM_NO_VERIFY
        /* Performance run: the checksum is outside the timed pipeline.
         * Correctness must come from a separate verifying run. */
        int64_t t2 = t1;
#else
        uint32_t crc = framebuffer_crc(session.framebuffer);
        int64_t t2 = esp_timer_get_time();
        if (crc != GBA_ROM_FRAME_CRC[i]) {
            if (crc_mismatches < 4) {
                ESP_LOGE(TAG, "frame=%" PRIu32 " crc=%08" PRIx32
                              " expected=%08" PRIx32,
                         i + 1, crc, GBA_ROM_FRAME_CRC[i]);
            }
            crc_mismatches++;
        }
#endif

#ifdef GBA_DISPLAY_PPA_OVERLAP_TEST
        /* CPU emulation/CRC ran while the prior image scanned out. Gate PPA
         * writes until that submitted target has crossed a refresh boundary. */
        if (i > 0) {
            int64_t wait_start = esp_timer_get_time();
            if (gba_display_wait_refresh_since(previous_present_snapshot,
                                                GBA_DISPLAY_SYNC_TIMEOUT_MS) != ESP_OK) {
                ESP_LOGE(TAG, "refresh timed out before conversion at frame=%" PRIu32,
                         i + 1);
                refresh_timeouts++;
                ok = false;
                err = ESP_ERR_TIMEOUT;
                break;
            }
            int64_t waited = esp_timer_get_time() - wait_start;
            wait_us[i - 1] = (uint32_t)waited;
            wait_total += (uint64_t)waited;
            index ^= 1;
        }
#endif

#ifdef GBA_DISPLAY_PPA_OVERLAP_TEST
        int64_t convert_start = esp_timer_get_time();
#endif
        uint16_t *dst = fb[index] + (size_t)GBA_SCALED_Y * GBA_DISPLAY_H_RES;
#ifdef GBA_DISPLAY_PPA_STREAM_TEST
        err = ppa_convert_gba_frame(ppa_client,
                                    (const uint16_t *)session.framebuffer,
                                    fb[index]);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "PPA conversion failed at frame %" PRIu32 ": %s",
                     i + 1, esp_err_to_name(err));
            ok = false;
            break;
        }
#else
        if (!gba_rotate_ccw45x_rgb565_nn_into(
                (const uint16_t *)session.framebuffer, GBA_FRAME_WIDTH,
                GBA_FRAME_HEIGHT, dst, GBA_DISPLAY_H_RES)) {
            ESP_LOGE(TAG, "rotating/scaling failed at frame %" PRIu32, i + 1);
            ok = false;
            break;
        }
#endif
        int64_t t3 = esp_timer_get_time();

        err = gba_display_present_region(0, GBA_SCALED_Y, GBA_SCALED_W,
                                         GBA_SCALED_H, dst);
        int64_t t4 = esp_timer_get_time();
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "present failed at frame %" PRIu32 ": %s", i + 1,
                     esp_err_to_name(err));
            ok = false;
            break;
        }
#ifdef GBA_DISPLAY_PPA_OVERLAP_TEST
        /* Snapshot after present_region returns, after the driver has selected
         * this buffer. A VSYNC just before/during submission is conservatively
         * not treated as proof that the submitted buffer has been scanned. */
        previous_present_snapshot = gba_display_refresh_count();
#endif

        /* Wait for the buffer just presented to come up, so the next iteration
         * draws into the one that is no longer being scanned out. */
#ifndef GBA_DISPLAY_PPA_OVERLAP_TEST
        int64_t wait_start = esp_timer_get_time();
        if (gba_display_wait_refresh(GBA_DISPLAY_SYNC_TIMEOUT_MS) != ESP_OK) {
            ESP_LOGE(TAG, "refresh timed out at frame %" PRIu32, i + 1);
            refresh_timeouts++;
            ok = false;
            err = ESP_ERR_TIMEOUT;
            break;
        }
        int64_t t5 = esp_timer_get_time();
#endif

        run_us[i] = (uint32_t)(t1 - t0);
        crc_us[i] = (uint32_t)(t2 - t1);
#ifdef GBA_DISPLAY_PPA_OVERLAP_TEST
        /* Buffer-free waiting is reported separately, not charged to PPA. */
        scale_us[i] = (uint32_t)(t3 - convert_start);
#else
        scale_us[i] = (uint32_t)(t3 - t2);
#endif
        present_us[i] = (uint32_t)(t4 - t3);
#ifndef GBA_DISPLAY_PPA_OVERLAP_TEST
        wait_us[i] = (uint32_t)(t5 - wait_start);
        wait_total += wait_us[i];
#endif
        /* Insertion into a 3-entry slowest list (i ascends, so ties keep the
         * earliest frame). */
        for (int s = 0; s < 3; ++s) {
            if (run_us[i] > run_slow_us[s]) {
                for (int m = 2; m > s; --m) {
                    run_slow_us[m] = run_slow_us[m - 1];
                    run_slow_idx[m] = run_slow_idx[m - 1];
                }
                run_slow_us[s] = run_us[i];
                run_slow_idx[s] = i;
                break;
            }
        }
        run_total += run_us[i];
        crc_total += crc_us[i];
        scale_total += scale_us[i];
        present_total += present_us[i];
#ifndef GBA_DISPLAY_PPA_OVERLAP_TEST
        index ^= 1;
#endif
    }
#ifdef GBA_DISPLAY_PPA_OVERLAP_TEST
    if (ok && GBA_ROM_FRAME_COUNT > 0) {
        int64_t wait_start = esp_timer_get_time();
        if (gba_display_wait_refresh_since(previous_present_snapshot,
                                            GBA_DISPLAY_SYNC_TIMEOUT_MS) != ESP_OK) {
            ESP_LOGE(TAG, "final refresh timed out");
            refresh_timeouts++;
            ok = false;
            err = ESP_ERR_TIMEOUT;
        } else {
            int64_t waited = esp_timer_get_time() - wait_start;
            wait_us[GBA_ROM_FRAME_COUNT - 1] = (uint32_t)waited;
            wait_total += (uint64_t)waited;
        }
    }
#endif
    int64_t stream_elapsed_us = esp_timer_get_time() - stream_start_us;
#if defined(GBA_FRAME_ACCOUNT)
    mgba_frame_account_uninstall(session.core);
    mgba_frame_account_report();
    mgba_render_timing_report();
#elif defined(GBA_RENDER_TIMING)
    mgba_render_timing_uninstall(session.core);
    mgba_render_timing_report();
#endif

    if (!ok || crc_mismatches != 0) {
        ESP_LOGE(TAG, "stream test aborted/failed: ok=%d crc_mismatches=%" PRIu32
                      " refresh_timeouts=%" PRIu32,
                 (int)ok, crc_mismatches, refresh_timeouts);
        goto cleanup;
    }

    ESP_LOGI(TAG,
#ifdef GBA_STREAM_NO_VERIFY
             "stream: frames=%u verification=skipped"
#else
             "stream: frames=%u crc_mismatches=%" PRIu32
#endif
             " refresh_done=%" PRIu32 " refresh_timeouts=%" PRIu32
             " elapsed_ms=%.1f fps=%.2f",
#ifdef GBA_STREAM_NO_VERIFY
             (unsigned)GBA_ROM_FRAME_COUNT,
#else
             (unsigned)GBA_ROM_FRAME_COUNT, crc_mismatches,
#endif
             (uint32_t)gba_display_refresh_count(),
             refresh_timeouts,
             (double)stream_elapsed_us / 1000.0,
             (double)GBA_ROM_FRAME_COUNT * 1e6 / (double)stream_elapsed_us);
    ESP_LOGI(TAG, "run_slow3: #%u=%uus #%u=%uus #%u=%uus",
             (unsigned)run_slow_idx[0] + 1, (unsigned)run_slow_us[0],
             (unsigned)run_slow_idx[1] + 1, (unsigned)run_slow_us[1],
             (unsigned)run_slow_idx[2] + 1, (unsigned)run_slow_us[2]);
#ifdef GBA_FRAME_ACCOUNT
    /* Top 20 frames by runFrame time with their in-frame splits, so a slow
     * frame can be attributed to draw/DMA/event/audio by frame number.
     * NOTE: report_stage sorts its array in place, so this selection must run
     * before any report_stage call — it needs run_us[] in frame order. The
     * f_* arrays are never sorted. */
    {
        uint32_t picked[20];
        for (int t = 0; t < 20; ++t) {
            picked[t] = GBA_ROM_FRAME_COUNT;
        }
        for (int t = 0; t < 20; ++t) {
            uint32_t best = 0;
            uint32_t bi = GBA_ROM_FRAME_COUNT;
            for (uint32_t i = 0; i < GBA_ROM_FRAME_COUNT; ++i) {
                bool done = false;
                for (int p = 0; p < t; ++p) {
                    if (picked[p] == i) {
                        done = true;
                        break;
                    }
                }
                if (!done && run_us[i] >= best) {
                    best = run_us[i];
                    bi = i;
                }
            }
            if (bi >= GBA_ROM_FRAME_COUNT) {
                break;
            }
            picked[t] = bi;
            ESP_LOGI(TAG,
                     "frametop: #%u run=%u draw=%u dmar=%u dmaw=%u "
                     "event=%u audio=%u",
                     (unsigned)bi + 1, (unsigned)run_us[bi],
                     (unsigned)f_draw_us[bi], (unsigned)f_dma_r[bi],
                     (unsigned)f_dma_w[bi], (unsigned)f_event_us[bi],
                     (unsigned)f_audio_us[bi]);
        }
    }
#endif
    report_stage("runFrame", run_us, GBA_ROM_FRAME_COUNT, run_total);
    report_stage("crc", crc_us, GBA_ROM_FRAME_COUNT, crc_total);
#ifdef GBA_DISPLAY_PPA_STREAM_TEST
    report_stage("ppa", scale_us, GBA_ROM_FRAME_COUNT, scale_total);
#else
    report_stage("scale", scale_us, GBA_ROM_FRAME_COUNT, scale_total);
#endif
    report_stage("present", present_us, GBA_ROM_FRAME_COUNT, present_total);
#ifdef GBA_DISPLAY_PPA_OVERLAP_TEST
    report_stage("buffer_wait", wait_us, GBA_ROM_FRAME_COUNT, wait_total);
#else
    report_stage("refresh", wait_us, GBA_ROM_FRAME_COUNT, wait_total);
#endif

    ret = ESP_OK;
    ESP_LOGI(TAG, "last frame displayed; psram_free=%u (was %u)",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
             (unsigned)psram_before);
#ifdef GBA_IDLE_DETECT
    /* Observational only: did the detector ever latch onto a loop? NONE
     * (0xFFFFFFFF) means it never engaged on this workload. */
    ESP_LOGI(TAG, "idleLoop=%08x idleOptimization=%d",
             (unsigned)((struct GBA *)session.core->board)->idleLoop,
             (int)((struct GBA *)session.core->board)->idleOptimization);
#endif

cleanup:
#ifdef GBA_DISPLAY_PPA_STREAM_TEST
    if (ppa_client) {
        esp_err_t ppa_err = ppa_unregister_client(ppa_client);
        if (ppa_err != ESP_OK) {
            ESP_LOGE(TAG, "PPA SRM client unregister failed: %s",
                     esp_err_to_name(ppa_err));
            if (ret == ESP_OK) ret = ppa_err;
        }
    }
#endif
    if (sync_enabled) {
        gba_display_sync_disable();
    }
    if (ret != ESP_OK) {
        if (panel_ready) {
            gba_display_deinit();
        }
        rom_session_close(&session, true);
    } else {
        /* Keep the framebuffer and the panel with its frame buffers: the last
         * frame stays on screen. */
        rom_session_close(&session, false);
    }
    heap_caps_free(samples);
    return ret;
}
