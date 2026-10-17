/* PPA vs CPU image-path benchmark (GBA_PPA_BENCH_TEST).
 *
 * ROM-free, no SD, no panel, no audio/input/save. A fixed asymmetric 240x160
 * RGB565 test image goes through PPA SRM paths and the CPU reference, timed
 * per operation in BLOCKING mode:
 *   - copy:    rot0 + scale 1.0  (must be pixel-identical to memcpy; gated)
 *   - rotonly: rot90 + scale 1.0 (orientation checked via corner markers)
 *   - scaleonly: rot0 + scale 4.5
 *   - rot45x:  rot90 + scale 4.5 (the production layout; pixels expected to
 *              differ from nearest-neighbour CPU output — reported, not gated)
 *
 * The CPU reference (gba_rotate_ccw45x_rgb565_nn_into) is the correctness
 * anchor for the copy path only. PPA has no filter selection, so its scaled
 * pixels are a performance alternative, never a silent swap.
 */
#include "display_test.h"

#include <inttypes.h>
#include <string.h>

#include "driver/ppa.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "gba_scale.h"

static const char *TAG = "gba_ppa_bench";

#define PPA_SRC_W 240
#define PPA_SRC_H 160
#define PPA_ROT_W 720
#define PPA_ROT_H 1080
#define PPA_ITERS 50

/* Asymmetric test image: distinct corners, 1px diagonal + grid, gradient,
 * checker. Any rotation/scale/sampling error moves a detectable feature. */
static void ppa_test_image(uint16_t *fb) {
    for (int y = 0; y < PPA_SRC_H; ++y) {
        for (int x = 0; x < PPA_SRC_W; ++x) {
            uint16_t v = (uint16_t)(((x * 31 / PPA_SRC_W) << 11) |
                                    ((y * 63 / PPA_SRC_H) << 5) |
                                    ((x + y) % 32));
            if (((x / 8) + (y / 8)) % 2 == 0) {
                v ^= 0x0841;
            }
            if (x == y || x % 16 == 0 || y % 16 == 0) {
                v = (uint16_t)~v;
            }
            fb[(size_t)y * PPA_SRC_W + (size_t)x] = v;
        }
    }
    fb[0] = 0xF800;
    fb[PPA_SRC_W - 1] = 0x07E0;
    fb[(size_t)(PPA_SRC_H - 1) * PPA_SRC_W] = 0x001F;
    fb[(size_t)(PPA_SRC_H - 1) * PPA_SRC_W + PPA_SRC_W - 1] = 0xFFFF;
}

static uint32_t buf_checksum(const uint16_t *p, size_t pixels) {
    uint32_t h = 2166136261u;
    const unsigned char *q = (const unsigned char *)p;
    for (size_t i = 0; i < pixels * 2; ++i) {
        h ^= q[i];
        h *= 16777619u;
    }
    return h;
}

/* Count pixels differing from the reference over the same extent. */
static uint32_t count_mismatch(const uint16_t *got, const uint16_t *want,
                               size_t pixels, uint32_t *first_xy) {
    uint32_t n = 0;
    for (size_t i = 0; i < pixels; ++i) {
        if (got[i] != want[i]) {
            if (n == 0 && first_xy) {
                *first_xy = (uint32_t)i;
            }
            n++;
        }
    }
    return n;
}

typedef struct {
    const char *name;
    ppa_srm_rotation_angle_t rot;
    float scale;
    int out_w;
    int out_h;
} ppa_path_t;

static esp_err_t run_path(ppa_client_handle_t client, const ppa_path_t *path,
                          const uint16_t *src, uint16_t *dst,
                          const uint16_t *cpu_ref) {
    size_t out_px = (size_t)path->out_w * (size_t)path->out_h;
    ppa_srm_oper_config_t op = {
        .in.buffer = src,
        .in.pic_w = PPA_SRC_W,
        .in.pic_h = PPA_SRC_H,
        .in.block_w = PPA_SRC_W,
        .in.block_h = PPA_SRC_H,
        .in.block_offset_x = 0,
        .in.block_offset_y = 0,
        .in.srm_cm = PPA_SRM_COLOR_MODE_RGB565,
        .out.buffer = dst,
        .out.buffer_size = (uint32_t)(out_px * sizeof(uint16_t)),
        .out.pic_w = (uint32_t)path->out_w,
        .out.pic_h = (uint32_t)path->out_h,
        .out.block_offset_x = 0,
        .out.block_offset_y = 0,
        .out.srm_cm = PPA_SRM_COLOR_MODE_RGB565,
        .rotation_angle = path->rot,
        .scale_x = path->scale,
        .scale_y = path->scale,
        .mirror_x = false,
        .mirror_y = false,
        .rgb_swap = false,
        .byte_swap = false,
        .alpha_update_mode = PPA_ALPHA_NO_CHANGE,
        .mode = PPA_TRANS_MODE_BLOCKING,
        .user_data = NULL,
    };

    /* One warmup issue before timing (clock/queue settle). */
    esp_err_t err = ppa_do_scale_rotate_mirror(client, &op);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "%s: warmup failed: %s", path->name,
                 esp_err_to_name(err));
        return err;
    }
    uint64_t total = 0;
    uint64_t max_us = 0;
    for (int i = 0; i < PPA_ITERS; ++i) {
        int64_t t0 = esp_timer_get_time();
        err = ppa_do_scale_rotate_mirror(client, &op);
        uint64_t dt = (uint64_t)(esp_timer_get_time() - t0);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "%s: iter %d failed: %s", path->name, i,
                     esp_err_to_name(err));
            return err;
        }
        total += dt;
        if (dt > max_us) {
            max_us = dt;
        }
        /* Yield between issues so the idle task and watchdog stay happy. */
        vTaskDelay(1);
    }
    uint32_t cs = buf_checksum(dst, out_px);
    uint32_t first_xy = 0;
    uint32_t mismatch = 0;
    if (cpu_ref) {
        mismatch = count_mismatch(dst, cpu_ref, out_px, &first_xy);
    }
    ESP_LOGI(TAG,
             "%s: n=%d avg_us=%.1f max_us=%llu checksum=%08" PRIx32
             " mismatch_vs_cpu=%u%s",
             path->name, PPA_ITERS, (double)total / PPA_ITERS,
             (unsigned long long)max_us, cs, mismatch,
             cpu_ref ? "" : " (no CPU reference for this geometry)");
    if (cpu_ref) {
        ESP_LOGI(TAG, "%s: first_mismatch_xy=%u (linear index)", path->name,
                 first_xy);
    }
    return ESP_OK;
}

esp_err_t gba_ppa_bench_test_run(void) {
    ESP_LOGI(TAG, "ROM-free PPA vs CPU image-path bench; "
                  "no SD/panel/audio/input/save");
    esp_err_t ret = ESP_FAIL;

    static const ppa_path_t paths[] = {
        {"ppa_copy", PPA_SRM_ROTATION_ANGLE_0, 1.0f, 240, 160},
        {"ppa_rotonly", PPA_SRM_ROTATION_ANGLE_90, 1.0f, 160, 240},
        {"ppa_scaleonly", PPA_SRM_ROTATION_ANGLE_0, 4.5f, 1080, 720},
        {"ppa_rot45x", PPA_SRM_ROTATION_ANGLE_90, 4.5f, 720, 1080},
    };

    uint16_t *src = heap_caps_aligned_alloc(64, (size_t)PPA_SRC_W * PPA_SRC_H *
                                                       sizeof(uint16_t),
                                             MALLOC_CAP_SPIRAM |
                                                 MALLOC_CAP_8BIT);
    /* Largest output extent (720x1080); smaller paths reuse the prefix. */
    uint16_t *dst = heap_caps_aligned_alloc(64, (size_t)PPA_ROT_W * PPA_ROT_H *
                                                       sizeof(uint16_t),
                                             MALLOC_CAP_SPIRAM |
                                                 MALLOC_CAP_8BIT);
    uint16_t *ref = heap_caps_malloc((size_t)PPA_ROT_W * PPA_ROT_H *
                                         sizeof(uint16_t),
                                     MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!src || !dst || !ref) {
        ESP_LOGE(TAG, "PSRAM bench buffer allocation failed");
        goto cleanup;
    }
    ESP_LOGI(TAG, "buffer alignment mod64: src=%u dst=%u",
             (unsigned)((uintptr_t)src % 64u),
             (unsigned)((uintptr_t)dst % 64u));
    ppa_test_image(src);

    /* CPU references: rot45x production output, plus a plain memcpy source
     * copy for the copy path. */
    if (!gba_rotate_ccw45x_rgb565_nn_into(src, PPA_SRC_W, PPA_SRC_H, ref,
                                          PPA_ROT_W)) {
        ESP_LOGE(TAG, "CPU rot45x reference failed");
        goto cleanup;
    }
    uint32_t ref_cs = buf_checksum(ref, (size_t)PPA_ROT_W * PPA_ROT_H);
    ESP_LOGI(TAG, "cpu_rot45x checksum=%08" PRIx32, ref_cs);
    /* The copy path must be pixel-identical to a memcpy: establish the
     * source checksum as its gate. */
    uint32_t src_cs = buf_checksum(src, (size_t)PPA_SRC_W * PPA_SRC_H);
    ESP_LOGI(TAG, "src checksum=%08" PRIx32, src_cs);

    ppa_client_config_t client_cfg = {
        .oper_type = PPA_OPERATION_SRM,
        .max_pending_trans_num = 1,
        .data_burst_length = PPA_DATA_BURST_LENGTH_128,
    };
    ppa_client_handle_t client = NULL;
    esp_err_t err = ppa_register_client(&client_cfg, &client);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ppa_register_client failed: %s", esp_err_to_name(err));
        goto cleanup;
    }

    bool saw_error = false;
    for (size_t p = 0; p < sizeof(paths) / sizeof(paths[0]); ++p) {
        const uint16_t *cpu_ref = NULL;
        if (p == 0) {
            cpu_ref = src; /* copy path: expect memcpy-identical */
        } else if (p == 3) {
            cpu_ref = ref; /* rot45x: report difference, do not gate */
        }
        if (run_path(client, &paths[p], src, dst, cpu_ref) != ESP_OK) {
            saw_error = true;
            break;
        }
        /* Gate: the copy path must match memcpy exactly. */
        if (p == 0) {
            uint32_t got = buf_checksum(dst, (size_t)240 * 160);
            if (got != src_cs) {
                ESP_LOGE(TAG, "ppa_copy is NOT memcpy-identical "
                              "(got %08" PRIx32 " want %08" PRIx32 ")",
                         got, src_cs);
                saw_error = true;
                break;
            }
            ESP_LOGI(TAG, "ppa_copy gate: memcpy-identical OK");
        }
        /* Orientation gate for rot90: source top-right corner (green) must
         * land at the output top-left block. */
        if (p == 1) {
            if (dst[0] != 0x07E0) {
                ESP_LOGE(TAG, "ppa_rotonly orientation wrong "
                              "(out[0]=%04x, want 07E0)",
                         (unsigned)dst[0]);
                saw_error = true;
                break;
            }
            ESP_LOGI(TAG, "ppa_rotonly orientation gate OK");
        }
    }

    err = ppa_unregister_client(client);
    client = NULL;
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ppa_unregister_client failed: %s",
                 esp_err_to_name(err));
        saw_error = true;
    }
    if (!saw_error) {
        ret = ESP_OK;
        ESP_LOGI(TAG, "ppa bench done");
    }

    /* On-panel A/B visual (only when the bench passed): alternate the CPU
     * nearest-neighbour reference and the PPA rot45x output every second so
     * crisp-vs-filtered can be judged by eye. The driver pre-clears both
     * buffers to black, so the 100 px bands are already correct. Not a pixel
     * gate; the log lines say which source is currently shown. */
    if (!saw_error) {
        uint16_t *fb0 = NULL;
        uint16_t *fb1 = NULL;
        if (gba_display_init() != ESP_OK ||
            gba_display_get_frame_buffers(&fb0, &fb1) != ESP_OK) {
            ESP_LOGE(TAG, "panel init for PPA visual failed");
            saw_error = true;
        } else {
            ppa_client_config_t show_cfg = {
                .oper_type = PPA_OPERATION_SRM,
                .max_pending_trans_num = 1,
                .data_burst_length = PPA_DATA_BURST_LENGTH_128,
            };
            ppa_client_handle_t show_client = NULL;
            if (ppa_register_client(&show_cfg, &show_client) != ESP_OK) {
                ESP_LOGE(TAG, "PPA client for visual failed");
                saw_error = true;
            } else {
                uint16_t *fbs[2] = {fb0, fb1};
                int index = 0;
                /* 60 toggles at ~1 s each; even toggles show CPU, odd PPA. */
                for (int t = 0; t < 60 && !saw_error; ++t) {
                    uint16_t *back = fbs[index] + (size_t)100 * 720;
                    if (t % 2 == 0) {
                        memcpy(back, ref,
                               (size_t)720 * 1080 * sizeof(uint16_t));
                        ESP_LOGI(TAG, "showing: CPU nearest-neighbour (%d)",
                                 t);
                    } else {
                        ppa_srm_oper_config_t show_op = {
                            .in.buffer = src,
                            .in.pic_w = PPA_SRC_W,
                            .in.pic_h = PPA_SRC_H,
                            .in.block_w = PPA_SRC_W,
                            .in.block_h = PPA_SRC_H,
                            .in.block_offset_x = 0,
                            .in.block_offset_y = 0,
                            .in.srm_cm = PPA_SRM_COLOR_MODE_RGB565,
                            .out.buffer = fbs[index],
                            .out.buffer_size =
                                (uint32_t)(720u * 1280u * 2u),
                            .out.pic_w = 720,
                            .out.pic_h = 1280,
                            .out.block_offset_x = 0,
                            .out.block_offset_y = 100,
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
                        err = ppa_do_scale_rotate_mirror(show_client,
                                                         &show_op);
                        if (err != ESP_OK) {
                            ESP_LOGE(TAG, "PPA visual op failed: %s",
                                     esp_err_to_name(err));
                            saw_error = true;
                            break;
                        }
                        ESP_LOGI(TAG, "showing: PPA filtered (%d)", t);
                    }
                    if (gba_display_present_region(0, 100, 720, 1080, back) !=
                            ESP_OK ||
                        gba_display_backlight_set(
                            GBA_DISPLAY_BRIGHTNESS_PERCENT) != ESP_OK) {
                        ESP_LOGE(TAG, "PPA visual present failed");
                        saw_error = true;
                        break;
                    }
                    index ^= 1;
                    vTaskDelay(pdMS_TO_TICKS(1000));
                }
                ppa_unregister_client(show_client);
                if (!saw_error) {
                    ESP_LOGI(TAG, "PPA A/B visual done; holding last (PPA)");
                }
            }
        }
        if (saw_error) {
            gba_display_deinit();
        }
    }
    if (!saw_error) {
        ret = ESP_OK;
    }

cleanup:
    if (src) {
        heap_caps_free(src);
    }
    if (dst) {
        heap_caps_free(dst);
    }
    if (ref) {
        heap_caps_free(ref);
    }
    ESP_LOGI(TAG, "psram_free=%u",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    return ret;
}
