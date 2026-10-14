/* Bench app entry: runs ROM-independent cases, prints JSONL on console.
 * Skeleton status (M1): compiles against IDF, not yet run on device.
 * Display/audio/SD cases land in M2; core adapter lands in M4+. */
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "bench.h"

static const char *TAG = "gba_bench";

static void emit_console(const char *line, void *ctx) {
    (void)ctx;
    printf("%s\n", line);
}

void app_main(void) {
    ESP_LOGI(TAG, "gba_bench start (rom-independent)");
    bench_register_all();
    bench_run_all(emit_console, NULL, "tab5", "idf-default", "local",
                  "none");
    ESP_LOGI(TAG, "gba_bench done; idling");
    while (1) vTaskDelay(pdMS_TO_TICKS(1000));
}
