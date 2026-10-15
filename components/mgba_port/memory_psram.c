/* PSRAM-backed replacement for upstream src/platform/posix/memory.c.
 *
 * Upstream: anonymousMemoryMap() = calloc(1, size).
 * Problem on ESP32-P4: the GBA core maps the whole ROM through here
 * (GBALoadROM keeps `memory->rom` resident), so an 8-16MiB ROM plus
 * EWRAM/IWRAM/VRAM easily exceeds internal DRAM. Route anonymous mappings
 * to byte-addressable PSRAM only; falling back to internal DRAM can consume
 * the application's stack budget and still cannot satisfy a large ROM.
 * Fail loudly instead.
 */
#include <mgba-util/common.h>
#include <mgba-util/memory.h>

#include "esp_heap_caps.h"
#include "esp_log.h"

static const char* TAG = "mgba_port";

void* anonymousMemoryMap(size_t size) {
    void* p = heap_caps_calloc(1, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!p) {
        ESP_LOGE(TAG, "PSRAM allocation failed for %u bytes", (unsigned)size);
    }
    return p;
}

void mappedMemoryFree(void* memory, size_t size) {
    UNUSED(size);
    heap_caps_free(memory);
}
