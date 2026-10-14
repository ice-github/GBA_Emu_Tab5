# Troubleshooting patterns proven on Tab5 (reused, operator-facing)

## Panic triage (decode first, then edit)

- Capture `Guru Meditation`, `MEPC`, `MCAUSE`, and backtrace.
- Decode with `riscv32-esp-elf-addr2line`.
- A NULL config passed to `usb_serial_jtag_driver_install` crashes; pass
  configuration with nonzero `rx_buffer_size` / `tx_buffer_size`.
- A stack-protection fault in `main` indicates the default main-task stack is
  too small for multi-KB transfer buffers; raise it (16 K used) and store
  large buffers in static storage.

## Reboot-loop triage

- A regular READY→ROM period (around 1 s) indicates firmware reboot, not a
  host problem.
- One empty read treated as hard failure (early `return -1`) and
  `esp_restart()` can create that loop.
- Unit mismatch trap: `esp_timer_get_time()` is **microseconds**. A
  `120 * 1000` "millisecond" deadline is 120 ms, not 120 s.

## USB-Serial-JTAG RX rules

- `fread()` semantics flip with driver state (0 immediate without driver,
  blocks forever with driver installed). Use `usb_serial_jtag_read_bytes()`
  with explicit timeouts inside an `esp_timer_get_time()` deadline loop.
- After `usb_serial_jtag_driver_install()`, call
  `usb_serial_jtag_vfs_use_driver()`. Without it, VFS-fread always returns 0.
- Raw LF line endings both directions; binary data is otherwise corrupted.
- Drain stale RX before emitting READY; skip blank lines in header parsing.

## SD / FatFS / DMA

- Validate the generated `sdkconfig`, not `.defaults`.
- Long filenames such as `meta.json` need `CONFIG_FATFS_LFN_HEAP=y`.
- Never enable `CONFIG_BSP_SD_FORMAT_ON_MOUNT_FAIL` without user confirmation.
- SD reading via `f_read` (verified ~10 MB/s) is much faster than `fread`
  (~1.2 MB/s) in the prior configuration.
- Start bulk reads at sector-aligned offsets or DMA falls to a 512-byte
  bounce buffer on the prior profile.
- Sub-tick delays (`vTaskDelay` of 1–2 ms at 100 Hz) round to zero ticks and
  starve IDLE.

## Watchdog

- Determine whether the watchdog backtrace includes the transfer path before
  making changes. Tight poll loops and bulk writes starve IDLE.
- Track transfer phase separately from playback phase.

## Display (MIPI-DSI / ILI9881C)

- `bsp_display_new()` with a zeroed configuration aborts because
  `lane_bit_rate_mbps` is 0. Set it to `BSP_LCD_MIPI_DSI_LANE_BITRATE_MBPS`
  (1000 = 1 Gbps) and `phy_clk_src = 0`.
- Failure signature: transfer completes and CRCs are OK, then reboots near
  `bsp_display_new`.

## Stale builds and races

- Re-print READY-adjacent marker every few seconds until first FILE header,
  else a host connecting mid-cycle may wait a full coordinator period.
- Clean rebuild (`rm -rf build`) before verification: ninja reuse can leave an
  old `app_desc` timestamp.
- No debugging magic: never use `--force` to push mismatched bootloaders onto
  rev v1.0.
