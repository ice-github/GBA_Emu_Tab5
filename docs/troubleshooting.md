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

## ESP-IDF component requirements

- `REQUIRES` / `PRIV_REQUIRES` are resolved by an early-expansion CMake pass
  (`tools/cmake/scripts/component_get_requirements.cmake`) that does **not** see
  project build options. A `REQUIRES` placed inside `if(GBA_..._TEST)` is
  silently dropped; the symptom is `fatal error: <header>: No such file or
  directory` from the source that needed it, plus an IDF hint naming the
  component. List such requirements unconditionally. Conditional `SRCS` are
  still fine because sources are collected in the main configure pass.
- The resolved list (and what IDF actually kept) is in
  `build/project_description.json` -> `build_component_info`.

## Display dependency

- Board/display support comes from the **noglib** Tab5 BSP
  (`espressif/m5stack_tab5_noglib`, 1.3.2), declared in
  `firmware/main/idf_component.yml` because `managed_components/` is gitignored.
  The graphical BSP additionally pulls `esp_lvgl_port`; the release resolved for
  IDF 5.5.4 (2.9.0) uses `on_frame_buf_complete`, an IDF 6.x-only DSI callback
  member, and fails to compile. Do not switch back to the graphical variant
  without pinning a compatible `esp_lvgl_port`.

## Stale builds and races

- Re-print READY-adjacent marker every few seconds until first FILE header,
  else a host connecting mid-cycle may wait a full coordinator period.
- Clean rebuild (`rm -rf build`) before verification: ninja reuse can leave an
  old `app_desc` timestamp.
- No debugging magic: never use `--force` to push mismatched bootloaders onto
  rev v1.0.

## sdkconfig changes do not apply to an existing sdkconfig

- ESP-IDF applies `sdkconfig.defaults` only when generating a *new*
  `sdkconfig`. `firmware/sdkconfig` exists (it is gitignored but persists in the
  working tree), so adding a line to `sdkconfig.defaults` and rebuilding silently
  keeps the old value. `-B <builddir>` does not help: the config file location is
  the project directory, not the build directory.
- Symptom seen here: `CONFIG_BSP_LCD_DPI_BUFFER_NUMS=2` was added to
  `sdkconfig.defaults`, the build succeeded, and the running image still had one
  frame buffer.
- Check the applied value (not the defaults file) with
  `<builddir>/config/sdkconfig.json`, or `grep` the project `sdkconfig`.
- To apply: delete `firmware/sdkconfig` and rebuild, or edit the value in
  `firmware/sdkconfig` as well. Confirm with a before/after diff that only the
  intended line changed.

## DPI frame buffers and tearing

- `esp_lcd_panel_draw_bitmap()` has three paths
  (see `esp_lcd_panel_dpi.c`): if the buffer passed is one of the driver's own
  frame buffers it only does `esp_cache_msync` and sets `cur_fb_index`; with
  DMA2D it hands the copy to `esp_async_fbcpy`; otherwise it `memcpy`s inline.
  Only the first path changes which buffer is scanned out, and it always copies
  into `fbs[cur_fb_index]` in the other two.
- Therefore double buffering requires pointing the call at a driver frame
  buffer (obtained via `esp_lcd_dpi_panel_get_frame_buffer()`), not at a private
  surface. With a private surface and `num_fbs=1` the DSI scans the very buffer
  DMA2D writes, so tearing is structural, not a timing accident.
- The switch takes effect in `mipi_dsi_dma_trans_done_cb()` at the frame
  boundary, so the update is frame-aligned.
- Explicit sync semantics: `on_color_trans_done` fires when a DMA2D copy into a
  frame buffer finishes; `on_refresh_done` fires when a frame has been scanned
  out. The first can be invoked from the caller's task (CPU copy path) as well as
  from the GDMA ISR, so a handler that must work in both contexts needs
  `xPortInIsrContext()`; the no-copy path invokes it synchronously. The second
  comes from the DSI bridge VSYNC ISR.
- Waiting on vsync with a binary semaphore plus drain-then-take costs a whole
  extra refresh per frame. Wait for the refresh counter to advance instead
  (task notification plus a counter compare), which is immune to stale events.
