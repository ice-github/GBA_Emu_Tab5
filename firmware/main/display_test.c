/* Panel bring-up + fixed verification pattern. See display_test.h. */
#include "display_test.h"

#include <stddef.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "driver/i2c_master.h"
#include "driver/ledc.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_lcd_panel_ops.h"

#include "bsp/m5stack_tab5.h"
#include "bsp/display.h"

static const char *TAG = "gba_display";

/* bsp_display.c selects 1000 Mbps for the ILI9881C/GT911 board and 965 Mbps
 * for the ST712x family, but only exposes the ILI9881C value in a header. */
#define TAB5_ST712X_LANE_BITRATE_MBPS 965u
/* esp_lcd_touch_st7123.h defines this as ESP_LCD_TOUCH_IO_I2C_ST7123_ADDRESS.
 * Kept literal to avoid depending on the touch component from the LCD path. */
#define TAB5_ST7123_TOUCH_ADDRESS 0x55

static esp_lcd_panel_handle_t s_panel = NULL;
static esp_lcd_panel_io_handle_t s_io = NULL;
static bool s_ready = false;

static TaskHandle_t s_refresh_task = NULL;
static volatile uint32_t s_refresh_count = 0;

/* Fired from the DSI bridge VSYNC ISR once a frame has been scanned out. The
 * callback only wakes the waiting task and bumps a counter. A task notification
 * is used rather than a semaphore so that a stale event cannot make a later
 * wait return early: wait_refresh compares the counter and keeps sleeping until
 * it actually advances. */
static bool IRAM_ATTR on_refresh_done(esp_lcd_panel_handle_t panel,
                                      esp_lcd_dpi_panel_event_data_t *edata,
                                      void *user_ctx) {
    (void)panel;
    (void)edata;
    (void)user_ctx;
    s_refresh_count++;
    BaseType_t woken = pdFALSE;
    if (s_refresh_task) {
        vTaskNotifyGiveFromISR(s_refresh_task, &woken);
    }
    return woken == pdTRUE;
}

esp_err_t gba_display_sync_enable(void) {
    if (!s_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    if (s_refresh_task) {
        return ESP_OK;
    }
    /* The refresh event wakes the task that enables synchronisation, so the
     * matching waits must be issued from this same task. */
    s_refresh_task = xTaskGetCurrentTaskHandle();
    s_refresh_count = 0;
    esp_lcd_dpi_panel_event_callbacks_t cbs = {
        /* on_color_trans_done is intentionally left NULL: the continuous paths
         * render directly into a driver frame buffer, so there is no DMA2D
         * copy into a frame buffer to await. */
        .on_refresh_done = on_refresh_done,
    };
    esp_err_t err = esp_lcd_dpi_panel_register_event_callbacks(s_panel, &cbs,
                                                               NULL);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "panel callback registration failed: %s",
                 esp_err_to_name(err));
        s_refresh_task = NULL;
    }
    return err;
}

esp_err_t gba_display_wait_refresh_since(uint32_t before,
                                        uint32_t timeout_ms) {
    if (!s_refresh_task) {
        return ESP_ERR_INVALID_STATE;
    }
    if (xTaskGetCurrentTaskHandle() != s_refresh_task) {
        return ESP_ERR_INVALID_STATE;
    }
    const int64_t deadline_us =
        esp_timer_get_time() + (int64_t)timeout_ms * 1000;
    while (s_refresh_count == before) {
        const int64_t left_us = deadline_us - esp_timer_get_time();
        if (left_us <= 0) {
            return ESP_ERR_TIMEOUT;
        }
        TickType_t ticks = pdMS_TO_TICKS((uint32_t)((left_us + 999) / 1000));
        if (ticks == 0) {
            ticks = 1;
        }
        (void)ulTaskNotifyTake(pdTRUE, ticks);
    }
    return ESP_OK;
}

esp_err_t gba_display_wait_refresh(uint32_t timeout_ms) {
    return gba_display_wait_refresh_since(s_refresh_count, timeout_ms);
}

uint32_t gba_display_refresh_count(void) {
    return s_refresh_count;
}

void gba_display_sync_disable(void) {
    if (!s_ready) {
        return;
    }
    esp_lcd_dpi_panel_event_callbacks_t none = {0};
    esp_lcd_dpi_panel_register_event_callbacks(s_panel, &none, NULL);
    s_refresh_task = NULL;
}

esp_err_t gba_display_get_frame_buffers(uint16_t **fb0, uint16_t **fb1) {
    if (!s_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!fb0 || !fb1) {
        return ESP_ERR_INVALID_ARG;
    }
    if (CONFIG_BSP_LCD_DPI_BUFFER_NUMS < 2) {
        ESP_LOGE(TAG, "double buffering needs CONFIG_BSP_LCD_DPI_BUFFER_NUMS>=2 "
                      "(got %d)", CONFIG_BSP_LCD_DPI_BUFFER_NUMS);
        return ESP_ERR_NOT_SUPPORTED;
    }
    void *first = NULL;
    void *second = NULL;
    esp_err_t err = esp_lcd_dpi_panel_get_frame_buffer(s_panel, 2, &first,
                                                      &second);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_lcd_dpi_panel_get_frame_buffer failed: %s",
                 esp_err_to_name(err));
        return err;
    }
    *fb0 = (uint16_t *)first;
    *fb1 = (uint16_t *)second;
    return ESP_OK;
}

/* The BSP's board-version helper is static, so reproduce only the I2C probe
 * that distinguishes the two panel families (which decides the DSI bitrate). */
static uint32_t tab5_lane_bitrate_mbps(void) {
    if (bsp_feature_enable(BSP_FEATURE_TOUCH, true) != ESP_OK) {
        ESP_LOGW(TAG, "touch enable failed; assuming ILI9881C DSI bitrate");
        return BSP_LCD_MIPI_DSI_LANE_BITRATE_MBPS;
    }
    /* Match the BSP's settle delay before probing the touch controller. */
    vTaskDelay(pdMS_TO_TICKS(500));
    i2c_master_bus_handle_t bus = bsp_i2c_get_handle();
    if (bus && i2c_master_probe(bus, TAB5_ST7123_TOUCH_ADDRESS, 100) == ESP_OK) {
        ESP_LOGI(TAG, "ST712x touch detected -> %u Mbps DSI lanes",
                 (unsigned)TAB5_ST712X_LANE_BITRATE_MBPS);
        return TAB5_ST712X_LANE_BITRATE_MBPS;
    }
    ESP_LOGI(TAG, "assuming ILI9881C/GT911 -> %u Mbps DSI lanes",
             (unsigned)BSP_LCD_MIPI_DSI_LANE_BITRATE_MBPS);
    return BSP_LCD_MIPI_DSI_LANE_BITRATE_MBPS;
}

static esp_err_t backlight_init_20khz(void) {
    /* Match the proven ../Tab5 fix: tear down the BSP's 5 kHz PWM instance,
     * then recreate the same 10-bit timer/channel at 20 kHz. Merely retuning
     * the BSP-owned timer with ledc_set_freq() did not reproduce the tested
     * initialization sequence. Keep duty at zero until a valid frame exists. */
    ESP_RETURN_ON_ERROR(bsp_display_brightness_deinit(), TAG,
                        "BSP backlight deinit failed");

    const ledc_timer_config_t timer = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_10_BIT,
        .timer_num = LEDC_TIMER_0,
        .freq_hz = GBA_DISPLAY_BACKLIGHT_HZ,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ESP_RETURN_ON_ERROR(ledc_timer_config(&timer), TAG,
                        "20 kHz backlight timer config failed");

    const ledc_channel_config_t channel = {
        .gpio_num = GPIO_NUM_22,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = CONFIG_BSP_DISPLAY_BRIGHTNESS_LEDC_CH,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = LEDC_TIMER_0,
        .duty = 0,
        .hpoint = 0,
    };
    ESP_RETURN_ON_ERROR(ledc_channel_config(&channel), TAG,
                        "20 kHz backlight channel config failed");

    ESP_LOGI(TAG, "backlight PWM recreated at %u Hz, 10-bit, GPIO22/ch%d",
             (unsigned)GBA_DISPLAY_BACKLIGHT_HZ,
             CONFIG_BSP_DISPLAY_BRIGHTNESS_LEDC_CH);
    return bsp_display_brightness_set(0);
}

esp_err_t gba_display_init(void) {
    if (s_ready) {
        return ESP_OK;
    }
    bsp_display_config_t config = {
        .dsi_bus = {
            /* 0 lets the driver choose the default DSI PHY clock source. */
            .phy_clk_src = 0,
            .lane_bit_rate_mbps = tab5_lane_bitrate_mbps(),
        },
    };
    ESP_RETURN_ON_ERROR(bsp_display_new(&config, &s_panel, &s_io), TAG,
                        "bsp_display_new failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_disp_on_off(s_panel, true), TAG,
                        "panel display on failed");
    ESP_RETURN_ON_ERROR(backlight_init_20khz(), TAG,
                        "20 kHz backlight setup failed");
    s_ready = true;
    ESP_LOGI(TAG, "panel ready %dx%d RGB565, backlight off until first frame",
             GBA_DISPLAY_H_RES, GBA_DISPLAY_V_RES);
    return ESP_OK;
}

esp_lcd_panel_handle_t gba_display_panel(void) {
    return s_panel;
}

esp_err_t gba_display_present_region(int x, int y, int w, int h,
                                     const uint16_t *rgb565) {
    if (!s_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    if (x < 0 || y < 0 || w <= 0 || h <= 0 || !rgb565) {
        return ESP_ERR_INVALID_ARG;
    }
    if (x + w > GBA_DISPLAY_H_RES || y + h > GBA_DISPLAY_V_RES) {
        return ESP_ERR_INVALID_ARG;
    }
    return esp_lcd_panel_draw_bitmap(s_panel, x, y, x + w, y + h, rgb565);
}

esp_err_t gba_display_present_full(const uint16_t *rgb565) {
    return gba_display_present_region(0, 0, GBA_DISPLAY_H_RES, GBA_DISPLAY_V_RES,
                                      rgb565);
}

esp_err_t gba_display_backlight_set(int percent) {
    if (percent < 0 || percent > 100) {
        return ESP_ERR_INVALID_ARG;
    }
    return bsp_display_brightness_set(percent);
}

esp_err_t gba_display_backlight_off(void) {
    return bsp_display_brightness_set(0);
}

void gba_display_deinit(void) {
    if (!s_ready) {
        return;
    }
    gba_display_sync_disable();
    bsp_display_backlight_off();
    esp_lcd_panel_disp_on_off(s_panel, false);
    /* Deletes panel, IO, DSI bus, PHY LDO power and backlight resources. */
    bsp_display_delete();
    s_panel = NULL;
    s_io = NULL;
    s_ready = false;
}

/* ----- ROM-free verification pattern ------------------------------------- */

static void fill_rect(uint16_t *fb, int w, int h, int x, int y, int rw, int rh,
                      uint16_t color) {
    for (int yy = y; yy < y + rh; ++yy) {
        if (yy < 0 || yy >= h) continue;
        for (int xx = x; xx < x + rw; ++xx) {
            if (xx < 0 || xx >= w) continue;
            fb[yy * w + xx] = color;
        }
    }
}

static uint16_t gray565(unsigned v) {
    return (uint16_t)(((v & 0xF8u) << 8) | ((v & 0xFCu) << 3) | (v >> 3));
}

void gba_display_fixed_pattern(uint16_t *fb, int w, int h) {
    static const uint16_t bars[8] = {
        0xFFFF, /* white   */
        0xFFE0, /* yellow  */
        0x07FF, /* cyan    */
        0x07E0, /* green   */
        0xF81F, /* magenta */
        0xF800, /* red     */
        0x001F, /* blue    */
        0x0000, /* black   */
    };
    if (!fb || w <= 1 || h <= 1) {
        return;
    }

    for (long i = 0; i < (long)w * h; ++i) {
        fb[i] = 0x0000;
    }

    const int band = h / 8; /* 160 px at 1280 */
    for (int b = 0; b < 8; ++b) {
        int x0 = (w * b) / 8;
        int x1 = (w * (b + 1)) / 8;
        fill_rect(fb, w, h, x0, 0, x1 - x0, band, bars[b]);
    }

    /* Gray ramp, left(black) -> right(white). */
    for (int y = band; y < band * 2; ++y) {
        for (int x = 0; x < w; ++x) {
            unsigned v = (unsigned)(x * 255) / (unsigned)(w - 1);
            fb[y * w + x] = gray565(v);
        }
    }

    /* 1px grid every 80px. */
    for (int x = 0; x < w; x += 80) {
        for (int y = 0; y < h; ++y) fb[y * w + x] = 0x4208;
    }
    for (int y = 0; y < h; y += 80) {
        for (int x = 0; x < w; ++x) fb[y * w + x] = 0x4208;
    }

    /* Asymmetric markers: a yellow bar only near the top, a magenta bar only
     * near the bottom. Together with the corner colours these make the image
     * orientation-unambiguous. */
    fill_rect(fb, w, h, w / 2 - 60, band * 2 + 20, 120, 24, 0xFFE0);
    fill_rect(fb, w, h, w / 2 - 60, h - 124, 120, 24, 0xF81F);

    /* Corner markers inside the border. */
    fill_rect(fb, w, h, 2, 2, 40, 40, 0xF800);          /* top-left  red    */
    fill_rect(fb, w, h, w - 42, 2, 40, 40, 0x07E0);     /* top-right green  */
    fill_rect(fb, w, h, 2, h - 42, 40, 40, 0x001F);     /* bottom-l  blue   */
    fill_rect(fb, w, h, w - 42, h - 42, 40, 40, 0xFFFF);/* bottom-r  white  */

    /* 1px outer border. */
    for (int x = 0; x < w; ++x) {
        fb[x] = 0xFFFF;
        fb[(h - 1) * w + x] = 0xFFFF;
    }
    for (int y = 0; y < h; ++y) {
        fb[y * w] = 0xFFFF;
        fb[y * w + (w - 1)] = 0xFFFF;
    }
}

/* ----- ROM-free animated verification pattern ---------------------------- */

/* The animated test only ever rewrites the GBA-sized region, mirroring the
 * final layout: 720x480 at y=400 with 400 px black bands left untouched. */
#define GBA_ANIM_Y 400
#define GBA_ANIM_W 720
#define GBA_ANIM_H 480

_Static_assert(GBA_ANIM_W == (int)GBA_DISPLAY_H_RES,
               "animated region must span the panel width");
_Static_assert(GBA_ANIM_H * 3 == 2 * GBA_ANIM_W,
               "animated region must keep the GBA 3:2 aspect");
_Static_assert(GBA_ANIM_Y + GBA_ANIM_H <= (int)GBA_DISPLAY_V_RES,
               "animated region must fit the panel");

/* 32-bit-store rectangle fill. The pattern is rewritten into PSRAM every
 * frame, so a plain per-pixel store loop dominates the frame cost. Rows of the
 * GBA region are 4-byte aligned (even stride, cache-aligned base), but the
 * leading-pixel fix-up keeps the cast safe in general. */
static void fast_fill(uint16_t *dst, int stride, int width, int rows,
                      uint16_t color) {
    if (width <= 0 || rows <= 0) {
        return;
    }
    const uint32_t c32 = (uint32_t)color | ((uint32_t)color << 16);
    for (int y = 0; y < rows; ++y) {
        uint16_t *row = dst + (size_t)y * stride;
        int n = width;
        if (((uintptr_t)row & 3u) != 0) {
            *row++ = color;
            n--;
        }
        uint32_t *p32 = (uint32_t *)row;
        const int n32 = n / 2;
        for (int i = 0; i < n32; ++i) {
            p32[i] = c32;
        }
        if (n & 1) {
            row[n - 1] = color;
        }
    }
}

/* Black-to-white RGB565 ramp, built once. A table keeps the per-frame gradient
 * to one load instead of a shift/or chain per pixel. */
static uint16_t s_gray[256];
static bool s_gray_ready = false;

static void ensure_gray(void) {
    if (s_gray_ready) {
        return;
    }
    for (unsigned i = 0; i < 256; ++i) {
        s_gray[i] = gray565(i);
    }
    s_gray_ready = true;
}

/* One frame of the animated pattern. `frame` increases by one per presentation.
 * The moving bars and the hard seam in the scrolling gradient make dropped
 * frames and tearing visible to the eye; the bottom bar is a 60-frame progress
 * indicator. Only ever called with (GBA_ANIM_W, GBA_ANIM_H). */
static void anim_pattern(uint16_t *fb, int w, int h, uint32_t frame) {
    ensure_gray();

    /* Background. */
    fast_fill(fb, w, w, h, 0x0841); /* dark blue-gray */

    /* Scrolling gradient with a hard seam: the seam moves right 6 px/frame.
     * A horizontal break at the seam is tearing. Every row of the band is
     * identical, so build it once and broadcast it. */
    static uint16_t row_buf[GBA_ANIM_W];
    const unsigned phase = (unsigned)frame * 6u;
    for (int x = 0; x < w; ++x) {
        row_buf[x] = s_gray[((unsigned)x + phase) & 0xFFu];
    }
    const int gy0 = h / 2 - 60;
    for (int y = gy0; y < gy0 + 120; ++y) {
        memcpy(fb + (size_t)y * w, row_buf, (size_t)w * sizeof(uint16_t));
    }

    /* Two bars moving in opposite directions. */
    int bar1 = (int)((frame * 12u) % (uint32_t)w);
    fast_fill(fb + bar1, w, 24, h, 0xFFFF); /* white, left -> right */
    int bar2 = w - 24 - (int)((frame * 7u) % (uint32_t)w);
    fast_fill(fb + bar2, w, 24, h, 0xFFE0); /* yellow, right -> left */

    /* Blinking corner block, alternating every 30 frames. */
    fast_fill(fb, w, 40, 40, ((frame / 30u) % 2u) ? 0xF800 : 0x001F);

    /* 60-frame progress bar along the bottom edge. */
    int pw = (int)((frame % 60u) * (uint32_t)w / 60u);
    fast_fill(fb + (size_t)(h - 16) * w, w, pw, 16, 0x07E0);

    /* 1px frame border makes any mis-registration obvious. */
    fast_fill(fb, w, w, 1, 0xFFFF);
    fast_fill(fb + (size_t)(h - 1) * w, w, w, 1, 0xFFFF);
    fast_fill(fb, w, 1, h, 0xFFFF);
    fast_fill(fb + (w - 1), w, 1, h, 0xFFFF);
}

esp_err_t gba_display_anim_test_run(void) {
    ESP_LOGI(TAG, "ROM-free animated pattern test (%u frames); "
                  "no SD/audio/input/save",
             (unsigned)GBA_DISPLAY_ANIM_FRAMES);

    esp_err_t err = gba_display_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "display init failed: %s", esp_err_to_name(err));
        return err;
    }
    err = gba_display_sync_enable();
    if (err != ESP_OK) {
        gba_display_deinit();
        return err;
    }

    uint16_t *fb[2] = {NULL, NULL};
    err = gba_display_get_frame_buffers(&fb[0], &fb[1]);
    if (err != ESP_OK) {
        gba_display_deinit();
        return err;
    }

    /* The driver pre-clears both frame buffers to black, so the 400 px bands
     * above and below the GBA region need no work: only the 720x480 region is
     * ever rewritten, exactly as the final layout does. Drawing into the buffer
     * that is not currently front and presenting it makes the driver swap at
     * the next frame boundary, so there is no tearing and no DMA2D copy. */
    uint16_t *const front = fb[0] + (size_t)GBA_ANIM_Y * GBA_DISPLAY_H_RES;
    err = gba_display_present_region(0, GBA_ANIM_Y, GBA_ANIM_W, GBA_ANIM_H,
                                     front);
    if (err == ESP_OK) {
        err = gba_display_wait_refresh(GBA_DISPLAY_SYNC_TIMEOUT_MS);
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "making fb0 the front buffer failed: %s",
                 esp_err_to_name(err));
        gba_display_deinit();
        return err;
    }

    err = gba_display_backlight_set(GBA_DISPLAY_BRIGHTNESS_PERCENT);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "backlight set failed: %s", esp_err_to_name(err));
        gba_display_deinit();
        return err;
    }

    uint32_t timeouts = 0;
    int index = 1; /* draw into fb[1]: fb[0] is front */
    uint64_t fill_total = 0, present_total = 0, wait_total = 0;
    uint32_t fill_max = 0, present_max = 0, wait_max = 0;
    uint32_t frames_done = 0;
    const int64_t start_us = esp_timer_get_time();
    for (uint32_t i = 0; i < GBA_DISPLAY_ANIM_FRAMES; ++i) {
        uint16_t *dst = fb[index] + (size_t)GBA_ANIM_Y * GBA_DISPLAY_H_RES;
        int64_t t0 = esp_timer_get_time();
        anim_pattern(dst, GBA_ANIM_W, GBA_ANIM_H, i);
        int64_t t1 = esp_timer_get_time();
        err = gba_display_present_region(0, GBA_ANIM_Y, GBA_ANIM_W, GBA_ANIM_H,
                                         dst);
        int64_t t2 = esp_timer_get_time();
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "present failed at frame %u: %s", (unsigned)i,
                     esp_err_to_name(err));
            break;
        }
        /* Wait for the buffer we just made front to actually come up, so the
         * next iteration draws into the one that is no longer scanned out. */
        if (gba_display_wait_refresh(GBA_DISPLAY_SYNC_TIMEOUT_MS) != ESP_OK) {
            timeouts++;
            ESP_LOGE(TAG, "refresh timed out at frame %u", (unsigned)i);
            break;
        }
        int64_t t3 = esp_timer_get_time();

        uint32_t fill_us = (uint32_t)(t1 - t0);
        uint32_t present_us = (uint32_t)(t2 - t1);
        uint32_t wait_us = (uint32_t)(t3 - t2);
        fill_total += fill_us;
        present_total += present_us;
        wait_total += wait_us;
        if (fill_us > fill_max) fill_max = fill_us;
        if (present_us > present_max) present_max = present_us;
        if (wait_us > wait_max) wait_max = wait_us;

        frames_done++;
        index ^= 1;
    }
    const int64_t elapsed_us = esp_timer_get_time() - start_us;

    if (frames_done) {
        ESP_LOGI(TAG,
                 "anim stages (us, avg/max): fill=%u/%u present=%u/%u "
                 "wait=%u/%u",
                 (unsigned)(fill_total / frames_done), (unsigned)fill_max,
                 (unsigned)(present_total / frames_done),
                 (unsigned)present_max, (unsigned)(wait_total / frames_done),
                 (unsigned)wait_max);
    }
    if (err != ESP_OK || timeouts) {
        ESP_LOGE(TAG, "animated pattern test failed: err=%s timeouts=%u "
                      "frames_done=%u",
                 esp_err_to_name(err), (unsigned)timeouts,
                 (unsigned)frames_done);
        gba_display_deinit();
        return err != ESP_OK ? err : ESP_ERR_TIMEOUT;
    }

    ESP_LOGI(TAG,
             "anim: frames=%u refresh_done=%u elapsed_ms=%.1f fps=%.2f; "
             "last frame held",
             (unsigned)frames_done,
             (unsigned)gba_display_refresh_count(),
             (double)elapsed_us / 1000.0,
             (double)frames_done * 1e6 / (double)elapsed_us);
    /* Keep the panel and its frame buffers alive: the last frame stays up. */
    return ESP_OK;
}
