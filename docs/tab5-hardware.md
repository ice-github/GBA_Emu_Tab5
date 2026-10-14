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

## Known caveats

- Per-unit revision may differ. Always probe with `esptool chip_id`.
- Hard assumptions (charge enable, PWM, SD) depend on board variant and BSP
  revision. Adapt only after verification.
- Anything described for the prior video player (JPEG decode, frames, audio
  sample clock) does **not** apply to a GBA path; performance numbers are not
  transferable.
