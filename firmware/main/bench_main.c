/* ROM-independent benchmark app plus an opt-in silent mGBA lifecycle test.
 * The lifecycle test never initializes an audio output device. */
#include <stdio.h>
#include <stdlib.h>
#include <dirent.h>
#include <fcntl.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "bench.h"
#include <mgba/core/config.h>
#include <mgba/core/core.h>
#include <mgba/gba/core.h>

#if defined(GBA_SD_PROBE_TEST) || defined(GBA_SD_ROM_LOAD_TEST) || \
    defined(GBA_SD_FRAME_TEST) || defined(GBA_SD_PERF_TEST)
#include "driver/gpio.h"
#include "driver/sdmmc_host.h"
#include "esp_vfs_fat.h"
#include "sd_pwr_ctrl_by_on_chip_ldo.h"
#include <mgba-util/vfs.h>
#include <mgba-util/crc32.h>
#include <mgba/core/log.h>
#include <mbedtls/sha256.h>
#endif

_Static_assert(sizeof(color_t) == 2, "firmware and mGBA must use RGB565");

static const char *TAG = "gba_bench";

#if !defined(MGBA_CORE_INIT_TEST) && !defined(GBA_SD_PROBE_TEST) && \
    !defined(GBA_SD_ROM_LOAD_TEST) && !defined(GBA_SD_FRAME_TEST) && \
    !defined(GBA_SD_PERF_TEST)
static void emit_console(const char *line, void *ctx) {
    (void)ctx;
    printf("%s\n", line);
}
#endif

#if defined(GBA_SD_PROBE_TEST) || defined(GBA_SD_ROM_LOAD_TEST) || \
    defined(GBA_SD_FRAME_TEST) || defined(GBA_SD_PERF_TEST)
#define GBA_SD_MOUNT_POINT "/sdcard"

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

static esp_err_t gba_sd_mount(sdmmc_card_t **card,
                              sd_pwr_ctrl_handle_t *pwr_ctrl) {
    sd_pwr_ctrl_ldo_config_t ldo_config = {.ldo_chan_id = 4};
    *card = NULL;
    *pwr_ctrl = NULL;
    esp_err_t err = sd_pwr_ctrl_new_on_chip_ldo(&ldo_config, pwr_ctrl);
    if (err != ESP_OK) {
        return err;
    }

    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.slot = SDMMC_HOST_SLOT_0;
    host.max_freq_khz = SDMMC_FREQ_HIGHSPEED;
    host.pwr_ctrl_handle = *pwr_ctrl;
    sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
    slot.width = 4;
    /* Tab5 SDMMC slot-0 wiring (docs/tab5-hardware.md, Tab5 BSP 1.3.2). */
    slot.clk = GPIO_NUM_43;
    slot.cmd = GPIO_NUM_44;
    slot.d0 = GPIO_NUM_39;
    slot.d1 = GPIO_NUM_40;
    slot.d2 = GPIO_NUM_41;
    slot.d3 = GPIO_NUM_42;
    const esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 5,
        .allocation_unit_size = 16 * 1024,
    };
    err = esp_vfs_fat_sdmmc_mount(GBA_SD_MOUNT_POINT, &host, &slot,
                                  &mount_config, card);
    if (err != ESP_OK) {
        sd_pwr_ctrl_del_on_chip_ldo(*pwr_ctrl);
        *pwr_ctrl = NULL;
    }
    return err;
}

static esp_err_t gba_sd_unmount(sdmmc_card_t *card,
                                sd_pwr_ctrl_handle_t pwr_ctrl) {
    esp_err_t err = esp_vfs_fat_sdcard_unmount(GBA_SD_MOUNT_POINT, card);
    if (err != ESP_OK) return err;
    return sd_pwr_ctrl_del_on_chip_ldo(pwr_ctrl);
}
#endif

#ifdef GBA_SD_PROBE_TEST
static void app_sd_probe_test(void) {
    ESP_LOGI(TAG, "silent SD read-only probe; formatting is disabled");
    sdmmc_card_t *card = NULL;
    sd_pwr_ctrl_handle_t pwr_ctrl = NULL;
    esp_err_t err = gba_sd_mount(&card, &pwr_ctrl);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "SD mount failed; no format/write attempted: %s",
                 esp_err_to_name(err));
        return;
    }

    DIR *dir = opendir(GBA_SD_MOUNT_POINT);
    if (!dir) {
        ESP_LOGE(TAG, "cannot open SD mount point %s", GBA_SD_MOUNT_POINT);
    } else {
        struct dirent *entry;
        unsigned gba_count = 0;
        while ((entry = readdir(dir)) != NULL) {
            size_t len = strlen(entry->d_name);
            if (len < 4 || strcasecmp(entry->d_name + len - 4, ".gba") != 0)
                continue;
            char path[256];
            int n = snprintf(path, sizeof(path), "%s/%s", GBA_SD_MOUNT_POINT,
                             entry->d_name);
            if (n < 0 || (size_t)n >= sizeof(path)) {
                ESP_LOGW(TAG, "skip overlong SD path");
                continue;
            }
            struct stat st;
            if (stat(path, &st) != 0 || !S_ISREG(st.st_mode)) {
                ESP_LOGW(TAG, "cannot stat ROM candidate: %s", entry->d_name);
                continue;
            }
            ESP_LOGI(TAG, "ROM candidate: %s bytes=%llu", entry->d_name,
                     (unsigned long long)st.st_size);
            ++gba_count;
        }
        closedir(dir);
        ESP_LOGI(TAG, "SD probe complete; gba_files=%u", gba_count);
    }

    err = gba_sd_unmount(card, pwr_ctrl);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "SD unmount/power teardown failed: %s", esp_err_to_name(err));
        return;
    }
    ESP_LOGI(TAG, "SD unmounted; no file writes performed; no audio output initialized");
}
#endif

#if defined(GBA_SD_ROM_LOAD_TEST) || defined(GBA_SD_FRAME_TEST) || \
    defined(GBA_SD_PERF_TEST)
#define GBA_ROM_PATH \
    GBA_SD_MOUNT_POINT "/gba/OugonNoTaiyouHirakareshiFuuin.gba"
#define GBA_ROM_SIZE 8388608u
#define GBA_ROM_SHA256 \
    "088bedae4bad8b67e87ff10035a898d3639f3182d486fe5a5d113bab223e0a26"
#define GBA_FRAME_WIDTH 240u
#define GBA_FRAME_HEIGHT 160u
#define GBA_FRAME_PIXELS (GBA_FRAME_WIDTH * GBA_FRAME_HEIGHT)
#ifndef GBA_SD_FRAME_COUNT
#define GBA_SD_FRAME_COUNT 1
#endif

static bool load_rom_image(struct VFile *sd_vf, size_t size,
                           unsigned char *rom, char hex[65]) {
    unsigned char digest[32];
    mbedtls_sha256_context ctx;
    mbedtls_sha256_init(&ctx);
    bool ok = mbedtls_sha256_starts(&ctx, 0) == 0;
    size_t total = 0;
    while (ok && total < size) {
        size_t want = size - total;
        if (want > 64 * 1024) want = 64 * 1024;
        ssize_t got = sd_vf->read(sd_vf, rom + total, want);
        if (got <= 0 || (size_t)got > want) {
            ok = false;
            break;
        }
        if (mbedtls_sha256_update(&ctx, rom + total, (size_t)got) != 0) {
            ok = false;
            break;
        }
        total += (size_t)got;
    }
    unsigned char extra;
    if (ok && total == size && sd_vf->read(sd_vf, &extra, 1) != 0)
        ok = false;
    if (ok && total == size)
        ok = mbedtls_sha256_finish(&ctx, digest) == 0;
    else
        ok = false;
    mbedtls_sha256_free(&ctx);
    if (!ok) return false;
    for (size_t i = 0; i < sizeof(digest); ++i)
        snprintf(hex + i * 2, 3, "%02x", digest[i]);
    hex[64] = '\0';
    return true;
}

#ifdef GBA_SD_PERF_TEST
static int compare_u32(const void *left, const void *right) {
    uint32_t a = *(const uint32_t *)left;
    uint32_t b = *(const uint32_t *)right;
    return (a > b) - (a < b);
}
#endif

static void app_sd_rom_load_test(void) {
    const size_t psram_before = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    sdmmc_card_t *card = NULL;
    sd_pwr_ctrl_handle_t pwr_ctrl = NULL;
    struct mCore *core = NULL;
    struct VFile *sd_vf = NULL;
    struct VFile *rom_vf = NULL;
    unsigned char *rom_image = NULL;
    color_t *framebuffer = NULL;
#ifdef GBA_SD_PERF_TEST
    uint32_t *frame_times_us = NULL;
#endif
    bool core_initialized = false;
    bool config_initialized = false;
    bool rom_loaded = false;
    bool passed = false;

#ifdef GBA_SD_FRAME_TEST
    ESP_LOGI(TAG, "silent SD frame CRC test; no audio/display; no SD writes");
#elif defined(GBA_SD_PERF_TEST)
    ESP_LOGI(TAG, "silent SD frame timing test; no audio/display; no SD writes");
#else
    ESP_LOGI(TAG, "silent SD ROM load test; no frame/audio/display; no SD writes");
#endif
    mLogSetDefaultLogger(&quiet_mgba_logger);
    esp_err_t err = gba_sd_mount(&card, &pwr_ctrl);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "SD mount failed without format: %s", esp_err_to_name(err));
        return;
    }

    core = GBACoreCreate();
    if (!core) {
        ESP_LOGE(TAG, "GBACoreCreate failed");
        goto cleanup;
    }
    if (!core->init(core)) {
        ESP_LOGE(TAG, "mGBA core init failed");
        free(core);
        core = NULL;
        goto cleanup;
    }
    core_initialized = true;
    /* Match host/gba-smoke.c and avoid machine/user-specific config files. */
    mCoreInitConfig(core, "gbaSmoke");
    config_initialized = true;
    mCoreConfigSetDefaultValue(&core->config, "idleOptimization", "remove");

    sd_vf = VFileOpen(GBA_ROM_PATH, O_RDONLY);
    if (!sd_vf) {
        ESP_LOGE(TAG, "ROM open failed: %s", GBA_ROM_PATH);
        goto cleanup;
    }
    ssize_t file_size = sd_vf->size(sd_vf);
    if (file_size != (ssize_t)GBA_ROM_SIZE) {
        ESP_LOGE(TAG, "ROM size mismatch: got=%ld expected=%u",
                 (long)file_size, (unsigned)GBA_ROM_SIZE);
        goto cleanup;
    }
    rom_image = heap_caps_malloc(GBA_ROM_SIZE,
                                 MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!rom_image) {
        ESP_LOGE(TAG, "PSRAM ROM allocation failed: %u bytes",
                 (unsigned)GBA_ROM_SIZE);
        goto cleanup;
    }
    char sha256[65];
    if (!load_rom_image(sd_vf, GBA_ROM_SIZE, rom_image, sha256)) {
        ESP_LOGE(TAG, "ROM read/hash failed or file changed during read");
        goto cleanup;
    }
    ESP_LOGI(TAG, "PSRAM ROM image SHA-256: %s", sha256);
    if (strcmp(sha256, GBA_ROM_SHA256) != 0) {
        ESP_LOGE(TAG, "ROM hash mismatch; refusing core load");
        goto cleanup;
    }
    sd_vf->close(sd_vf);
    sd_vf = NULL;

    rom_vf = VFileFromConstMemory(rom_image, GBA_ROM_SIZE);
    if (!rom_vf) {
        ESP_LOGE(TAG, "ROM memory VFile allocation failed");
        goto cleanup;
    }
    if (!core->loadROM(core, rom_vf)) {
        ESP_LOGE(TAG, "mGBA ROM load failed");
        goto cleanup;
    }
    /* Core owns the VFile now; the backing PSRAM image remains ours until unload. */
    rom_vf = NULL;
    rom_loaded = true;

    if (core->romSize(core) != GBA_ROM_SIZE) {
        ESP_LOGE(TAG, "core ROM size mismatch: %u",
                 (unsigned)core->romSize(core));
        goto cleanup;
    }
    char title[13] = {0};
    /* mGBA writes the 8-byte "AGB-xxxx" code without a terminator. */
    char code[9] = {0};
    core->getGameTitle(core, title);
    core->getGameCode(core, code);
    ESP_LOGI(TAG, "ROM loaded: title='%s' code='%s' bytes=%u frames=%u",
             title, code, (unsigned)core->romSize(core),
             (unsigned)core->frameCounter(core));

#if defined(GBA_SD_FRAME_TEST) || defined(GBA_SD_PERF_TEST)
    framebuffer = heap_caps_malloc(GBA_FRAME_PIXELS * sizeof(color_t),
                                   MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!framebuffer) {
        ESP_LOGE(TAG, "PSRAM framebuffer allocation failed");
        goto cleanup;
    }
    core->setVideoBuffer(core, framebuffer, GBA_FRAME_WIDTH);
    core->reset(core);
    core->clearKeys(core, 0xFFFFFFFFu);
#endif

#ifdef GBA_SD_FRAME_TEST
    for (uint32_t i = 0; i < GBA_SD_FRAME_COUNT; ++i) {
        core->runFrame(core);
        uint32_t framebuffer_crc = doCrc32(
            framebuffer, GBA_FRAME_PIXELS * sizeof(color_t));
        ESP_LOGI(TAG, "frame=%" PRIu32 " rgb565_crc32=%08" PRIx32,
                 i + 1, framebuffer_crc);
        if (i + 1 < GBA_SD_FRAME_COUNT) {
            /* Let IDLE/watchdog and other firmware tasks run between frames. */
            vTaskDelay(1);
        }
    }
    uint32_t frames = core->frameCounter(core);
    if (frames != GBA_SD_FRAME_COUNT) {
        ESP_LOGE(TAG, "frame counter mismatch: got=%" PRIu32 " expected=%u",
                 frames, (unsigned)GBA_SD_FRAME_COUNT);
        goto cleanup;
    }
#elif defined(GBA_SD_PERF_TEST)
    frame_times_us = heap_caps_malloc(
        GBA_SD_FRAME_COUNT * sizeof(*frame_times_us),
        MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!frame_times_us) {
        ESP_LOGE(TAG, "internal frame timing buffer allocation failed");
        goto cleanup;
    }
    uint32_t over_budget = 0;
    uint64_t total_us = 0;
    for (uint32_t i = 0; i < GBA_SD_FRAME_COUNT; ++i) {
        int64_t start_us = esp_timer_get_time();
        core->runFrame(core);
        int64_t elapsed_us = esp_timer_get_time() - start_us;
        if (elapsed_us < 0 || elapsed_us > UINT32_MAX) {
            ESP_LOGE(TAG, "invalid runFrame timing sample");
            goto cleanup;
        }
        frame_times_us[i] = (uint32_t)elapsed_us;
        total_us += frame_times_us[i];
        if (frame_times_us[i] > 16742u) ++over_budget;
        /* Yield is outside the timed runFrame interval. */
        if (i + 1 < GBA_SD_FRAME_COUNT) vTaskDelay(1);
    }
    uint32_t frames = core->frameCounter(core);
    if (frames != GBA_SD_FRAME_COUNT) {
        ESP_LOGE(TAG, "frame counter mismatch: got=%" PRIu32 " expected=%u",
                 frames, (unsigned)GBA_SD_FRAME_COUNT);
        goto cleanup;
    }
    qsort(frame_times_us, GBA_SD_FRAME_COUNT, sizeof(*frame_times_us),
          compare_u32);
    uint32_t median_us;
    if (GBA_SD_FRAME_COUNT & 1u) {
        median_us = frame_times_us[GBA_SD_FRAME_COUNT / 2];
    } else {
        median_us = (uint32_t)(((uint64_t)frame_times_us[
            GBA_SD_FRAME_COUNT / 2 - 1] +
            frame_times_us[GBA_SD_FRAME_COUNT / 2]) / 2);
    }
    uint32_t p95_index = (GBA_SD_FRAME_COUNT * 95u + 99u) / 100u - 1u;
    uint32_t p99_index = (GBA_SD_FRAME_COUNT * 99u + 99u) / 100u - 1u;
    ESP_LOGI(TAG,
             "frame timing: n=%u avg_us=%.1f median_us=%u p95_us=%u "
             "p99_us=%u max_us=%u over_16742us=%u",
             (unsigned)GBA_SD_FRAME_COUNT,
             (double)total_us / GBA_SD_FRAME_COUNT, (unsigned)median_us,
             (unsigned)frame_times_us[p95_index],
             (unsigned)frame_times_us[p99_index],
             (unsigned)frame_times_us[GBA_SD_FRAME_COUNT - 1],
             (unsigned)over_budget);
#endif
    passed = true;

cleanup:
    if (sd_vf) sd_vf->close(sd_vf);
    if (rom_vf) rom_vf->close(rom_vf);
    if (core) {
        if (rom_loaded) core->unloadROM(core);
        if (config_initialized) mCoreConfigDeinit(&core->config);
        if (core_initialized) core->deinit(core);
        else free(core);
    }
    if (framebuffer) heap_caps_free(framebuffer);
    if (rom_image) heap_caps_free(rom_image);
#ifdef GBA_SD_PERF_TEST
    if (frame_times_us) heap_caps_free(frame_times_us);
#endif
    esp_err_t unmount_err = gba_sd_unmount(card, pwr_ctrl);
    if (unmount_err != ESP_OK) {
        ESP_LOGE(TAG, "SD unmount/power teardown failed: %s",
                 esp_err_to_name(unmount_err));
        passed = false;
    }
    size_t psram_after = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    ESP_LOGI(TAG, "ROM load cleanup: psram_before=%u after=%u",
             (unsigned)psram_before, (unsigned)psram_after);
    if (!heap_caps_check_integrity_all(true) || psram_before != psram_after) {
        ESP_LOGE(TAG, "heap integrity or PSRAM recovery failed");
        passed = false;
    }
#ifdef GBA_SD_FRAME_TEST
    ESP_LOGI(TAG, "silent frame test %s; frames=%u",
             passed ? "passed" : "failed", (unsigned)GBA_SD_FRAME_COUNT);
#elif defined(GBA_SD_PERF_TEST)
    ESP_LOGI(TAG, "silent frame timing test %s; frames=%u",
             passed ? "passed" : "failed", (unsigned)GBA_SD_FRAME_COUNT);
#else
    ESP_LOGI(TAG, "silent SD ROM load test %s; no frame execution",
             passed ? "passed" : "failed");
#endif
}
#endif

#ifdef MGBA_CORE_INIT_TEST
static void app_core_init_test(void) {
    ESP_LOGI(TAG, "silent mGBA core lifecycle test (%d iterations)",
             MGBA_CORE_INIT_ITERATIONS);
    for (int i = 0; i < MGBA_CORE_INIT_ITERATIONS; ++i) {
        const size_t free_before = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
        struct mCore *core = GBACoreCreate();
        if (!core) {
            ESP_LOGE(TAG, "GBACoreCreate failed at iteration %d", i);
            return;
        }
        if (!core->init(core)) {
            /* Upstream init frees its partial CPU/board allocations itself;
             * the not-yet-initialized core object has no deinit contract. */
            free(core);
            ESP_LOGE(TAG, "core init failed at iteration %d", i);
            return;
        }

        mCoreInitConfig(core, "gbaTab5InitTest");
        mCoreConfigSetDefaultValue(&core->config, "idleOptimization", "remove");
        mCoreLoadForeignConfig(core, &core->config);
        mCoreConfigDeinit(&core->config);
        core->deinit(core); /* deinit owns and frees the core object */

        const size_t free_after = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
        const size_t internal_free = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
        ESP_LOGI(TAG, "iteration=%d psram_free=%u largest=%u internal_free=%u",
                 i + 1, (unsigned)free_after,
                 (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM |
                                                             MALLOC_CAP_8BIT),
                 (unsigned)internal_free);
        if (!heap_caps_check_integrity_all(true)) {
            ESP_LOGE(TAG, "heap integrity failed at iteration %d", i + 1);
            return;
        }
        if (free_after != free_before) {
            ESP_LOGE(TAG, "PSRAM free-space drift at iteration %d: %u -> %u",
                     i + 1, (unsigned)free_before, (unsigned)free_after);
            return;
        }
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    ESP_LOGI(TAG, "silent mGBA lifecycle test passed; audio output not initialized");
}
#endif

void app_main(void) {
#if defined(GBA_SD_ROM_LOAD_TEST) || defined(GBA_SD_FRAME_TEST) || \
    defined(GBA_SD_PERF_TEST)
    app_sd_rom_load_test();
#elif defined(GBA_SD_PROBE_TEST)
    app_sd_probe_test();
#elif defined(MGBA_CORE_INIT_TEST)
    app_core_init_test();
#else
    ESP_LOGI(TAG, "gba_bench start (rom-independent)");
    bench_register_all();
    bench_run_all(emit_console, NULL, "tab5", "idf-default", "local",
                  "none");
    ESP_LOGI(TAG, "gba_bench done; idling");
#endif
    while (1) vTaskDelay(pdMS_TO_TICKS(1000));
}
