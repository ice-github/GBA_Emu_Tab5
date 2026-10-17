/* Shared read-only SD and ROM-loading helpers for the opt-in firmware modes.
 *
 * The card is mounted read-only and is never formatted or written. ROMs are
 * user-provided and are never committed (see .gitignore).
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "driver/sdmmc_host.h"
#include "esp_err.h"
#include "sd_pwr_ctrl_by_on_chip_ldo.h"

struct VFile;

#define GBA_SD_MOUNT_POINT "/sdcard"

/* Which ROM is loaded, its size/SHA-256, and the per-frame CRC verification
 * vector (plus GBA_ROM_FRAME_COUNT) live in the selected ROM profile. */
#include "gba_rom_profile.h"

#define GBA_FRAME_WIDTH 240u
#define GBA_FRAME_HEIGHT 160u
#define GBA_FRAME_PIXELS (GBA_FRAME_WIDTH * GBA_FRAME_HEIGHT)

/* Mount the SD card read-only (formatting disabled). */
esp_err_t gba_sd_mount(sdmmc_card_t **card, sd_pwr_ctrl_handle_t *pwr_ctrl);

/* Unmount the SD card and drop its LDO power. */
esp_err_t gba_sd_unmount(sdmmc_card_t *card, sd_pwr_ctrl_handle_t pwr_ctrl);

/* Read exactly `size` bytes into `rom`, rejecting short reads, over-long reads
 * and files that change size mid-read. On success writes the lowercase hex
 * SHA-256 of the data read into `hex` (65 bytes including the terminator). */
bool gba_rom_load_image(struct VFile *sd_vf, size_t size, unsigned char *rom,
                        char hex[65]);
