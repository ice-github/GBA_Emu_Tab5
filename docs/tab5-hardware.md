# Tab5 hardware facts (reused, provenance included)

Represented hardware: M5Stack Tab5 with ESP32-P4 **rev v1.0**,
16 MB Flash, 32 MB PSRAM, USB-Serial-JTAG on USB-C.
All facts are from the `../Tab5` `firmware/main/main.c`,
`esp32-p4-bringup` and `tab5-linux-build-deploy` skills, and `.beads` memories,
cross-checked against the newer firmware. Sources are noted per item.

## Board peripherals used in the prior project

- Display: MIPI-DSI, 720×1280, RGB565.
- SDMMC: 4-bit, DAT0-3=GPIO39-42, CLK=43, CMD=44.
- Backlight: PWM controlled on GPIO22 (`CONFIG_BSP_DISPLAY_BRIGHTNESS_LEDC_CH`,
  10-bit resolution).
- Touch: GT911-style controller via `bsp_touch_new`.
- Battery / charge: IP2326 on U7 expander; enable requires P7 (CHG_EN) high and
  P5 (nQC) low.
- INA226 at 0x41 on the shared I2C bus (`SYS_BAT_PRE` rail) where fitted.

Source: `../Tab5` memories (`tab5-hw`, `tab5-battery`, firmware code).

## Proven hardware findings (from measurements)

- Backlight PWM flicker under load: 5 kHz LEDC PWM beat with playback; a DC
  backlight source was clean; 20 kHz PWM was clean with brightness control and
  restored brightness after fix. Do not generalize beyond this: the official
  UserDemo V0.4 also flickered on the same 5 kHz PWM, and the genuine panel
  reports ID `98/81/5c`.
  Source: `tab5-flicker` memory; `firmware/main/main.c`, lines 54,1341–1369.

- Rev v1.0 on IDF v5.5.x: bootloader requires a v3.1+ range by default; set
  `CONFIG_ESP32P4_SELECTS_REV_LESS_V3=y`, `CONFIG_ESP32P4_REV_MIN_100=y`,
  and verify the **generated** `sdkconfig` shows
  `ESP_REV_MIN_FULL=100` and `ESP_REV_MAX_FULL=199`, plus LFN heap,
  16 MB Flash, `MAIN_TASK_STACK_SIZE=16384`, and no format-on-mount-fail.
  Source: `tab5-rev1` (older, corrected) memory; `sdkconfig.defaults`;
  `firmware/dependencies.lock`.

- Rev v1.0 under IDF v5.4 with `CONFIG_PM_ENABLE` hung before/beside
  `app_main` and rebooted repeatedly. Power-management savings were dropped;
  A/B/D features stayed without DFS (see `docs/performance.md`).
  Source: `tab5-pm-hang`, `tab5-p45.3`.

- Display bring-up re-verified on this unit (2026-10-15): `bsp_display_new()`
  reports board version 1 (LCD ILI9881C, touch GT911), panel ID `98/81/5c`, DSI
  2 lanes at 1000 Mbps, native 720×1280 RGB565. It sets the backlight to 0% and
  does not turn the panel on; call `esp_lcd_panel_disp_on_off(panel, true)` and
  raise the backlight only after the first frame is presented. The verified
  backlight fix is not just a frequency retune: deinit the BSP's 5 kHz setup,
  recreate its 10-bit LEDC timer at 20 kHz and GPIO22/Kconfig-selected channel,
  then restore brightness. This mirrors `../Tab5/firmware/main/main.c:1341-1368`
  while keeping duty at zero until the first valid frame.
  Source: `../Tab5` Beads `tab5-uhl` and firmware; ported in
  `firmware/main/display_test.c`.

- Panel refresh rate measured 2026-10-15: **48.21 Hz**, not 60 Hz. The BSP
  configures the DPI pixel clock at 60 MHz with blanking
  `hsync: 40/140/40` and `vsync: 4/20/20`, i.e. `h_total = 940`,
  `v_total = 1324`, so `60e6 / (940 * 1324) = 48.21 Hz`. Confirmed on device by
  the `on_refresh_done` rate. The DPI clock is hardcoded in the BSP (not a
  Kconfig option); 60 Hz would need ~74.6 MHz or reduced blanking. Any 60 fps
  assumption must be re-stated against 48.2 Hz.
  Source: `bsp_display.c: dpi_config_ili9881c`, ROM-free animated pattern run.

- Frame buffers: the DPI driver allocates `CONFIG_BSP_LCD_DPI_BUFFER_NUMS`
  720×1280 RGB565 buffers in PSRAM (DMA-capable) and pre-clears them to black.
  Two are used here (`firmware/sdkconfig.defaults`); presenting a driver buffer
  makes the swap happen at the frame boundary, which is what removes tearing.
  Cost: 1,843,200 bytes per buffer.
  Source: `esp_lcd_panel_dpi.c`; `firmware/main/display_test.c`.

## Known caveats

- Per-unit revision may differ. Always probe with `esptool chip_id`.
- Hard assumptions (charge enable, PWM, SD) depend on board variant and BSP
  revision. Adapt only after verification.
- Anything described for the prior video player (JPEG decode, frames, audio
  sample clock) does **not** apply to a GBA path; performance numbers are not
  transferable.
