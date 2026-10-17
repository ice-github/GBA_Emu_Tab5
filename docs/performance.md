# Performance measurement rules (reused policy)

## Baseline

- Define metric, load profile, and hardware before optimizing.
- Measure stages separately (SD read, decode, draw, audio), not only totals.
- Report both averages and p95/max. Skip counts are required; average FPS alone
  is insufficient.
- Record hardware/revision frequencies, compiler/config hash, and conditions
  for each run.

## Tab5-proven tactics (not requirements for this project)

- Profile ranks real bottlenecks; stage times can mislead, because video
  pacing may be gated by audio-clock updates from blocking writes.
- PSRAM preload of ~7 MB moved per-frame work from `pread` (~24 ms) to
  `memcpy` (~0 ms) in the prior video player. Does not decide a GBA picture.
- Audio chunk size affects clock granularity. 1920-byte (~10 ms @48 kHz)
  chunks refreshed the clock faster than the frame budget.
- Sector alignment and DMA-capable PSRAM decide whether SD reads use direct
  DMA or a 512-byte bounce buffer.
- SD read speed on the variable cards ranged ~965 KB/s random–10 MB/s
  sequential; measure the actual card in use.
- PWM backplane issues appear at 5 kHz but not 20 kHz (flicker resolved).

## Project-specific notes

- A GBA core at ~60 FPS on ESP32-P4 will not be helped by the video player’s
  interleaved-decode assumptions. Keep metrics focused on frames (video + audio
  + input) and input latency, not just throughput.
- Keep a backup of each measured build: `{app}.elf/.bin` hashes and sdkconfig
  content.
- Do not treat past video FPS numbers as GBA results.

## First mGBA headless frame baseline (2026-10-15)

- Workload: mGBA 0.10.5, Golden Sun 1 (8 MiB; SHA-256
  `088bedae4bad8b67e87ff10035a898d3639f3182d486fe5a5d113bab223e0a26`),
  no input, RGB565 framebuffer in PSRAM, 600 frames per run.
- Target: Tab5 / ESP32-P4 rev v1.0, 360 MHz, 32 MiB PSRAM, ESP-IDF 5.5.4.
- Three independent cold boots. `runFrame()` only was timed; CRC, console
  output, ROM loading, and the one-RTOS-tick inter-frame yield were excluded.
  No display or audio output/AV sink was initialized; no audio metrics were
  collected. SD was opened read-only, with auto-format disabled.
- Observed ranges across the three runs:

  | metric | range |
  |---|---:|
  | average | 17.157–17.159 ms |
  | median | 8.762–8.772 ms |
  | p95 | 42.730–42.742 ms |
  | p99 | 56.544–56.557 ms |
  | max | 67.836–67.849 ms |
  | frames over 16.742 ms | 260 / 600 in every run |

- All runs completed without watchdog warning/reset and restored PSRAM free
  space to 33,551,752 bytes. This is a headless baseline, not a displayed-game
  FPS claim. The high tail latency and 260/600 budget overruns are the next
  profiling target; no optimization has been applied yet.
- Build identity: source `c5fd6b4-dirty`, app binary SHA-256
  `48c6320958aacf09da1d97ed3de8c9e75bdd358f062f1abd6d4ac6a917730991`,
  sdkconfig SHA-256
  `80b59a0a7cd13b23929e56e9bbd4d14fc185c0741a4fd46e53a1f5eaf5a541f0`.
- Raw runs are kept outside Git under `/tmp/opencode/gba-perf-run{1,2,3}.log`.

## Continuous panel display with the mGBA core running (2026-10-15)

> Superseded: the numbers below were taken with one DPI frame buffer
> (`CONFIG_BSP_LCD_DPI_BUFFER_NUMS=1`) and a caller-owned 720x1280 surface copied
> into it by DMA2D, so the swap was not frame-aligned. The double-buffered path
> described further down replaced that; the CPU stage costs (runFrame, CRC,
> scale) are still representative.

- Workload: same ROM and device as above, but every one of the 600 no-input
  frames is scaled 3x and pushed to the panel. `GBA_DISPLAY_STREAM_TEST`.
- Display path: 240x160 RGB565 PSRAM framebuffer -> CPU integer 3x nearest
  scaler -> 720x480 at y=400 inside a 720x1280 RGB565 PSRAM surface (400 px
  black bands top and bottom) -> `esp_lcd_panel_draw_bitmap` -> DMA2D copy into
  one DPI frame buffer (`CONFIG_BSP_LCD_DPI_BUFFER_NUMS=1`), DSI 2 lanes at
  1000 Mbps, backlight 20 kHz at 30 %, no LVGL, no audio, no input.
- Correctness: all 600 per-frame framebuffer CRCs matched the generated host
  vector (`firmware/main/gba_gs1_frame_crcs.h`, produced by
  `tools/gen_frame_crcs.py` from `host/gba-smoke-565`); 0 mismatches.
  `on_color_trans_done` (copy into the panel frame buffer finished) and
  `on_refresh_done` (frame scanned out) are consumed as separate events; the
  ISR callbacks only give a semaphore / bump a counter. The task waits for
  copy-done with a 200 ms bound before rewriting the shared surface.
- No DSI underrun was logged in this run (the bridge kept up even at the
  reduced frame rate). Panel refresh counted 1528 in 31.65 s (~48 Hz).
- Observed per-stage times over 600 frames (average / median / p95 / max, us):

  | stage | avg | median | p95 | max |
  |---|---:|---:|---:|---:|
  | `runFrame()` | 19722.6 | 9986 | 45673 | 70086 |
  | framebuffer CRC (verify only) | 3600.2 | 3745 | 3755 | 3809 |
  | 3x scale | 12621.1 | 12629 | 12812 | 12910 |
  | `draw_bitmap` call | 876.1 | 881 | 893 | 911 |
  | wait for copy-done | 12394.3 | 12321 | 12542 | 12566 |

- End to end: 600 frames in 31.65 s = **18.96 displayed frames/s**. The
  one-RTOS-tick inter-frame yield (FreeRTOS HZ = 100) is included, so this is
  not a pipeline-bandwidth limit alone. This is a correctness result, not a
  playable-framerate claim; 60 FPS is not the acceptance bar for display
  bring-up.
- The 12.6 ms scale and 3.6 ms CRC are PSRAM-bandwidth bound. Presenting only
  the 720x480 region (instead of the full 720x1280 surface) would cut the
  DMA2D copy ~2.7x and shrink the surface to 691 KB; both are follow-up
  optimizations, not correctness issues.
- Build identity: source `22b9fd1-dirty`, app binary SHA-256
  `0b7a631ce8fb9f4642edfea5af8b0cca4868019b06d26d0b08197f28683cb70b`,
  source `firmware/sdkconfig` SHA-256
  `38cc94737fb8eb014e2576de8e2e2629c12323351893ef99203a6a4cded81f67`.
- Raw log is kept outside Git under `/tmp/opencode/stream-serial.log`.

## Panel refresh rate is 48.2 Hz, not 60 Hz (2026-10-15)

- The BSP creates the ILI9881C DPI panel with `dpi_clock_freq_mhz = 60` and
  blanking `hsync_back_porch=140, hsync_pulse_width=40, hsync_front_porch=40,
  vsync_back_porch=20, vsync_pulse_width=4, vsync_front_porch=20`.
- Totals: `h_total = 720 + 140 + 40 + 40 = 940`, `v_total = 1280 + 20 + 4 + 20 =
  1324`, so the frame rate is `60e6 / (940 * 1324) = 48.21 Hz`.
- Confirmed on device: with a present-on-every-refresh loop the measured
  `on_refresh_done` rate is 48.2 Hz and a 600-frame run reports 601 refresh
  events (exactly one refresh per presented frame).
- Consequence: **48.2 fps is the ceiling of this display path as configured.**
  Reaching 60 Hz would need the DPI pixel clock raised to ~74.6 MHz (or smaller
  blanking), which the BSP hardcodes; it is not a Kconfig option. Any
  "60 fps" target must be re-stated against 48.2 Hz or the BSP timing changed.

## Tear-free double buffering via the driver's own frame buffers (2026-10-15)

- `CONFIG_BSP_LCD_DPI_BUFFER_NUMS=2` in `firmware/sdkconfig.defaults`. The DPI
  driver allocates both 720x1280 RGB565 buffers in PSRAM and pre-clears them to
  black, so the 400 px bands are correct without ever being written.
- Continuous display renders straight into the back buffer and presents it by
  pointing `gba_display_present_region()` at that buffer. The driver detects the
  pointer as one of its own (`dpi_panel_draw_bitmap()`, no-copy branch), so it
  only does `esp_cache_msync` (no DMA2D transfer at all) and sets
  `cur_fb_index`; `mipi_dsi_dma_trans_done_cb()` applies the new link list at the
  frame boundary. The swap is therefore frame-aligned and no torn frame is ever
  scanned out.
- With one frame buffer the DSI scans the same buffer that DMA2D writes, so
  tearing was inherent; the double-buffered path removes it by construction.
- Synchronisation is `on_refresh_done` only, delivered from the DSI bridge
  VSYNC ISR. `gba_display_wait_refresh()` waits for the refresh counter to
  *advance* (task notification + counter compare), so a stale event can never
  make it return early. A level-triggered semaphore with drain-then-take was
  tried first and cost a full extra refresh per frame (24.1 fps instead of
  48.2 fps). `on_color_trans_done` is deliberately not registered: with
  direct-to-frame-buffer rendering there is no copy to await.
- Signals used: `on_refresh_done` from the bridge VSYNC ISR; `on_color_trans_done`
  only on the external-surface path (still frame), where `draw_bitmap` does an
  asynchronous DMA2D copy.

### ROM-free animated pattern (`GBA_DISPLAY_ANIM_TEST`), 600 frames

- Pattern: moving white bar (12 px/frame), counter-moving yellow bar (7
  px/frame), a full-width scrolling gray gradient with a hard seam (6 px/frame)
  in the middle band, a blinking corner block, a 60-frame progress bar, and a
  1 px frame border. Presented into the 720x480 region at y=400, exactly the
  final layout. No SD, audio, input or save.
- Result: **48.21 fps, 601 refresh events for 600 frames** (one refresh per
  frame, i.e. perfectly vsync-locked), no underrun, no panic, last frame held.
- First implementation used a plain per-pixel fill loop and measured
  `fill = 24967 us avg / 25489 us max`, `present = 709 us`, `wait = 15804 us`;
  total ~41.5 ms is exactly two refresh periods, so the loop locked to 24.1 fps
  and reported 1201 refresh events. The sync logic was correct — the pattern
  generator was too slow.
- Switching the fills to 32-bit stores and broadcasting one prebuilt gradient
  row gave `fill = 14816 us avg / 15427 us max`, `present = 689 us`,
  `wait = 5228 us` for the same 48.21 fps. The ~5.2 ms of slack per frame is the
  headroom the display path has left at this refresh rate.
- The fill remains the only significant cost (863 KB written per frame,
  ~58 MB/s effective). Damage tracking (repaint only the moving elements) would
  remove most of it; it is not needed to hit the panel rate, only to leave room
  for a heavier renderer.
- Repeatability run (`-D GBA_DISPLAY_ANIM_FRAMES=4800`, ~100 s of continuous
  display instead of 600 frames): **48.21 fps, 4801 refresh events for 4800
  frames**, no underrun, no panic, last frame held. `fill = 14820 us avg /
  15462 us max`, `present = 690 us avg / 765 us max`, `wait = 5223 us avg /
  6296 us max`. Over eight times the frames the rate, and the exactly 1:1
  frame-to-refresh ratio, are unchanged — the sync does not drift.
- Build identity (600-frame run): app binary SHA-256
  `738f94070edd7960b572277fc29dd13b8f019284469449b5a58a9002f2360f8d`; the
  4800-frame run is `8929f806594b36d3847872ab8dbcab2a60cdd72fcdb870d93f97cd774887feec`.
  Source `firmware/sdkconfig` SHA-256
  `6570674125235d174a7252329b04858de2b91ab51a286ffe1ff3efaadcd97224` for both.

## ROM profiles and verification vectors

The SD/ROM modes take the ROM path, size, SHA-256 and per-frame CRC vector from
a *profile* selected at configure time:

```bash
idf.py -C firmware -D GBA_ROM_PROFILE=ffta -D GBA_DISPLAY_STREAM_TEST=ON build
```

`gs1` (Golden Sun 1, default) and `ffta` (Final Fantasy Tactics Advance) exist
today. `firmware/main/gba_rom_profile.h` is the single place that defines one and
`firmware/main/CMakeLists.txt` rejects anything else. The SHA-256 is checked
against what is actually read from the card, so a missing, renamed or different
ROM fails loudly instead of producing a bogus pass; a profile never silently
falls back to another ROM.

Vectors are regenerated from the host reference runner, never hand-edited:

```bash
tools/gen_frame_crcs.py <rom.gba> --symbol gba_ffta_frame \
    --label "Final Fantasy Tactics Advance (Japan)" \
    --out firmware/main/gba_ffta_frame_crcs.h
```

Both vectors cover 600 frames; `GBA_ROM_FRAME_COUNT` and the vector length are
asserted to match at compile time. The vectors are verification data, not ROM
content, and contain no ROM bytes.

Host-measured first 600 no-input frames of each profile (`--sample-every=1`):

| profile | frame 1 | frame 60 | frame 600 | distinct CRCs in 600 |
|---|---|---|---|---|
| `gs1` | `22bafa3a` | `89e57f72` | `0674d5f9` | 60 |
| `ffta` | `b5532989` | `0eaadad0` | `40ce7f1f` | 319 |

FFTA is the better motion workload: over 1800 host frames it produced 1392
distinct CRCs. Frames 39-219 are a static hold, then every frame from 220 on
differs. Golden Sun 1 stays on the name-entry screen without input, so it is
useful for correctness but not for judging motion.

## ROM-independent machine bench (M2 baseline, extended 2026-10-16)

`components/bench` runs the same cases on the host and the Tab5 and emits JSONL
per case; `tools/bench/validate.py` gates it. A default firmware build runs the
whole set on device, so a performance question can be answered without a GBA
ROM, an SD card, or the emulator core.

Re-baselining the eleven original cases on 2026-10-16 reproduced the recorded
2026-10-14 A3 medians within noise (memcpy 519 vs 565 us, memset 96 vs 96,
checksum 1933 vs 1935, prng 2134 vs 2136, strided 58 vs 58, dispatch 305 vs 306,
map_decode 1169 vs 1175, frame_copy 515 vs 559, frame_fill 961 vs 962,
scale2x 6619 vs 6618, xrgb 7050 vs 7046), so the harness itself is stable.

### 3x scaler: the real panel layout, four implementations

Every variant scales the same 240x160 source by 3 into the same 720x480
destination at stride 720. All four report checksum `5E44F83C`, i.e. they are
bit-identical; that equality is enforced by `validate.py`.

| case | what differs | avg (host) | avg (Tab5) | Tab5 vs production |
|---|---|---:|---:|---:|
| `scale3x_prod` | production scaler: 16-bit stores + `memcpy` row duplication | 0.054 ms | **10.427 ms** | — |
| `scale3x_u32m` | paired 32-bit stores + `memcpy` rows | 0.018 ms | **8.959 ms** | **1.16x faster** |
| `scale3x_u32x3` | paired 32-bit stores, all 3 rows written directly | 0.041 ms | 9.273 ms | 1.12x faster |
| `scale3x_u16x3` | 16-bit stores, rows duplicated by loop | 0.014 ms | 12.769 ms | 0.82x (slower) |

- The host ranking is inverted and meaningless for the device: the host compiler
  auto-vectorises the 16-bit loop and the whole destination fits in cache, so
  `scale3x_u16x3` looks fastest there (0.014 ms) and slowest on the Tab5
  (12.77 ms). Only the device run decides.
- On the Tab5 the 32-bit stores win and the `memcpy` row duplication is the right
  choice (`u32x3`, which writes three rows from the source and drops the memcpy,
  is slightly worse: more store traffic, and it re-reads the source three times).
- In the real display path the same production scaler measured 12.57 ms, i.e.
  **~2.1 ms more than the 10.43 ms measured here with the panel idle**: that gap
  is contention with the DSI scan-out DMA (1.84 MB per refresh at 48.21 Hz,
  ~89 MB/s), which this bench does not include. Measuring with and without the
  display is the "display on/off delta" M2 asks for.
- `u32m` in the display path would save roughly 1.5 ms of the 12.57 ms measured
  there, which is real but small, because the scaler turns out to be sitting on
  the memory system's write path rather than on slow code. See the next section.
- Raw log: `/tmp/opencode/bench-tab5-1.jsonl`, `/tmp/opencode/bench-serial.log`.

### Why the scaler is where it is: the PSRAM write path

Adding raw store/copy/read cases (`components/bench/cases_store.c`, all on the
same 691,200-byte 720x480 extent in PSRAM) explains the scaler numbers, and the
two device runs (128 KB L2) agreed to four significant digits.

| case | bytes | Tab5 ms | MB/s |
|---|---:|---:|---:|
| `memset_75k` | 76,800 | 0.096 | **797** |
| `memset_691k` | 691,200 | 8.042 | **86** |
| `fill_u16_691k` | 691,200 | 16.906 | 41 |
| `fill_u32_691k` | 691,200 | 10.998 | 63 |
| `fill_u64_691k` | 691,200 | 8.920 | 78 |
| `read_u32_691k` | 691,200 | 9.911 | 70 |
| `read_u32_76k` | 76,800 | 0.600 | 128 |
| `copy_u16_691k` | 691,200 | 22.964 | 30 |
| `copy_u32_691k` | 691,200 | 16.256 | 43 |
| `memcpy_691k` | 691,200 | 13.195 | 52 |
| `memcpy_rows_1440` | 460,800 written | 5.576 | 83 |
| `scaler_block3_u16` | 230,400 | 4.092 | 56 |
| `scaler_block3_u32` | 230,400 | 3.868 | 60 |

- **A full 720x480 RGB565 frame cannot be written faster than ~8 ms by the CPU**
  on this configuration: `memset_691k` is the fastest possible full-extent write
  and it takes 8.04 ms (86 MB/s). The scaler's 10.42 ms is therefore *close to the
  floor*, not far from it.
- **Store width is the single biggest lever**, and almost linearly so: 2 B stores
  give 41 MB/s, 4 B give 63, 8 B give 78, libc `memset` 86. That is the signature
  of a **per-store-transaction** limit (each store waits on the PSRAM write path),
  not of a bandwidth limit, so wider stores amortise a fixed per-transaction cost.
- **Cache capacity is the other lever.** The same `memset` over 76,800 B runs at
  797 MB/s and over 691,200 B at 86 MB/s: a 9.3x difference, on the same
  instruction sequence. The 128 KB L2 holds the small buffer and not the frame,
  so the large case pays the writeback path (with write-allocate, roughly 691 KB
  read for ownership plus 691 KB written, i.e. ~172 MB/s of real PSRAM traffic).
- The scaler is the **sum of its parts**: `prod` = `block3_u16` (4.09 ms, its row
  build) + `memcpy_rows_1440` (5.58 ms, its row duplication) = 9.67 ms of the
  10.42 ms measured. `u32m` = 3.87 + 5.58 = 9.45 ms of 8.96 ms measured (the
  in-cache source row makes the second and third passes cheaper than the model).
  There is no hidden inefficiency left to remove in the loop itself.
- Consequences: (a) promoting the 32-bit-store variant is worth ~1.4 ms of 10.4 ms
  and is cheap; (b) a *large* win needs either far fewer bytes written (a smaller
  destination region) or an engine that writes with bursts — DMA (PPA / DMA2D)
  does not pay the read-for-ownership cost and does not serialise per store, so it
  is the plausible route past 86 MB/s. That is the next measurement, not a code
  rewrite.
- The four reference cases reproduce as expected on the host but the host numbers
  are useless for prediction (all cache-resident, 30-66 GB/s).
- Tried and **rejected**: `CONFIG_CACHE_L2_CACHE_SIZE` = 512 KB with 128 B lines. The image builds,
  applies (`CACHE_L2_CACHE_SIZE = 524288`, line 128), and then faults at startup
  with a `Load access fault` in the console path (`emit_console` -> `puts`),
  before the first case. Enlarging L2 takes internal SRAM away from the rest of
  the system on this board, and this behaviour is not understood; 256 KB was not
  tried. Not pursued; the 128 KB configuration is restored.
- Raw logs: `/tmp/opencode/bench-tab5-3.jsonl` (128 KB L2, 27 cases),
  `/tmp/opencode/bench-tab5-2.jsonl` (first run of the store cases),
  `/tmp/opencode/l2-clean.log` (the rejected 512 KB build).

### Rotated display spec: 90 CCW + 4.5x aspect-fit (host-verified)

- Panel stays portrait 720x1280. The 240x160 image rotated 90° CCW is 160x240;
  aspect-fit scale is min(720/160, 1280/240) = 4.5, giving a **720x1080 rect at
  y=100** (100 px black bands top and bottom, full width). Note: with the panel
  itself portrait, the bands are top/bottom, not left/right.
- `gba_rotate_ccw45x_rgb565_nn_into()` (in `gba_scale`, dependency-free):
  mapping rx=sy, ry=(src_w-1)-sx, i.e. the source top edge becomes the left
  edge (up becomes left). Runs are 4,5,4,5,... per axis, no floating point.
  The fused loop renders one source column into one 720-px row band and
  duplicates it k times, mirroring the 3x scaler's expand+memcpy split.
- Corner oracle (240x160, verified by `scale_test` + pytest `--dump-rot`):
  top-right -> rect top-left, top-left -> rect bottom-left, bottom-right ->
  rect top-right, bottom-left -> rect bottom-right.
- Bytes per frame written: 720*1080*2 = **1,555,200 B (2.25x the 3x path)**.
- Host (cache-resident, ranking not predictive for the device):
  `rot45x_prod` 0.102 ms, `u32m` 0.087, `u32d` 0.311, `u16l` 0.249;
  all four checksums equal. `rot45x_prod` costs ~2x `scale3x_prod` on host
  (0.102 vs 0.054 ms), roughly tracking the 2.25x bytes.
- Bench cases `rot45x_{prod,u32m,u32d,u16l}` compare four implementations in
  one run with checksum-equality enforced by `validate.py`. Device numbers
  (`-Og` / `-O2`, 240x160 -> 720x1080):

  | case | -Og ms | -O2 ms | note |
  |---|---:|---:|---|
  | `rot45x_prod` | 20.739 | 19.831 | fastest both ways; 1.05x from `-O2` (memory-bound) |
  | `rot45x_u32m` | 21.471 | 20.417 | wide stores do not beat prod here |
  | `rot45x_u32d` | 33.602 | 27.568 | re-reading strided columns 4-5x each is the cost |
  | `rot45x_u16l` | 31.923 | 22.781 | loop-duplication far worse than `memcpy` |

  All four checksums equal (and equal across `-Og`/`-O2`). The 4.5x path
  costs ~2x the 3x path for 2.25x the bytes — consistent with the ~8 ms
  full-frame write floor plus loop overhead. Conclusion for the firmware
  switch: keep the prod structure (u16 expand + `memcpy` band duplication);
  no variant wins. Raw logs: `/tmp/opencode/bench-tab5-4.jsonl`,
  `/tmp/opencode/bench-tab5-o2.jsonl`.
- The firmware display path now renders this rotated layout (switched
  2026-10-16, GS1 + FFTA visually passed).

### PPA SRM paths on device (2026-10-16): 2.3x faster, different pixels

- ROM-free `GBA_PPA_BENCH_TEST`, `-O2`, BLOCKING mode, 50 iters each,
  asymmetric 240x160 test image, 64 B-aligned PSRAM buffers:

  | path | output | avg | max | pixels vs CPU |
  |---|---|---:|---:|---|
  | `ppa_copy` (rot0/scale1) | 240x160 | 1376.5 us | 1391 us | **memcpy-identical (gate passes)** |
  | `ppa_rotonly` (rot90/scale1) | 160x240 | 1358.4 us | 1368 us | orientation gate passes (corner lands top-left: same CCW convention) |
  | `ppa_scaleonly` (rot0/scale4.5) | 1080x720 | 8713.2 us | 8721 us | different filter (reported) |
  | `ppa_rot45x` (rot90/scale4.5) | 720x1080 | 8714.3 us | 8722 us | **593,949/777,600 differ (76%)**, first at index 2 |

- `ppa_rot45x` at 8.7 ms vs CPU `rot45x_prod` 19.8 ms panel-idle (24.0 ms
  in-path): **2.3–2.8x faster, ~11–15 ms saved per frame**. But the pixels
  differ (no filter selection on PPA — confirmed by measurement, not just by
  spec reading).
- Fixed overhead is visible: even the 76.8 KB copy costs 1.38 ms (~56 MB/s
  effective), while the 1.56 MB outputs run at ~178 MB/s. Small ops do not
  benefit; the frame-sized op does.
- Human A/B verdict on the Tab5 (CPU nearest-neighbour vs PPA, alternating
  every second for 60 turns): orientation, placement, colors, and black bands
  match; lines and grid visibly blur on the synthetic pattern. This alone was
  not enough to judge a real game's readability, so the GS1 screen was tested
  next. No DMA2D/tile winner established yet.
- The A/B loop is test-only (`GBA_PPA_BENCH_TEST`); it is not connected to the
  ROM display path. Latest serial capture: `/tmp/opencode/ppa-serial5.log`.
- `GBA_DISPLAY_PPA_STREAM_TEST` is a separate GS1-only visual check: it runs
  the existing CRC-verified ROM stream from frame 1 and uses PPA SRM to rotate
  and scale each framebuffer into the panel's back buffer. The emulated
  framebuffer CRC remains the CPU-reference correctness gate; final output
  quality is judged visually because PPA sampling differs.
- GS1 PPA stream on hardware: ROM SHA-256 matched, **600/600 CPU framebuffer
  CRCs matched**, no refresh timeout; 29.76 fps with PPA conversion averaging
  9.16 ms/frame. User judged the startup-screen font readable and acceptable;
  this makes PPA a viable candidate pending FFTA visual review.
- Moving-picture flicker was reported with the first PPA run. The backlight
  setup was then changed from `ledc_set_freq()` alone to the proven
  `../Tab5` sequence (BSP brightness deinit, recreate 10-bit 20 kHz timer and
  GPIO22/Kconfig channel, duty 0 until first frame). The same GS1 PPA stream
  again passed 600/600 CRCs; user confirmed the moving-image flicker was gone.
  Raw logs: `/tmp/opencode/ppa-gs1-serial.log` and
  `/tmp/opencode/ppa-gs1-backlight-serial.log`.

### Build optimisation: everything ships at -Og (measured 2026-10-16)

- Verified from `/tmp/opencode/gba-bench3/compile_commands.json`: all ~1131
  firmware translation units (mGBA, renderers, `gba_scale`, bench) build with
  `-Og -fno-jump-tables -fno-tree-switch-conversion`. The `-Og` comes from the
  Kconfig default `CONFIG_COMPILER_OPTIMIZATION_DEBUG`; the two `-fno-*`
  flags are unconditional IDF protections (IRAM jump tables) and stay in force
  under `-O2`/`-Os` (verified: 1108/1109 `-O2` TUs still carry both).
- An `-O2` variant was built in a separate build dir + separate sdkconfig
  (`/tmp/opencode/gba-bench-o2`, default `-Og` build untouched). It needed two
  targeted `-Wno-error=` downgrades in `components/mgba_port/CMakeLists.txt`
  for warnings `-O2` newly enables on upstream code (untouched submodule):
  `format-truncation` (`src/core/log.c:140`) and `stringop-truncation`
  (`src/util/string.c:28`, strncpy+explicit-NUL idiom). Same pattern as the
  pre-existing three downgrades.
- Size: `-O2` .bin 286,208 bytes vs `-Og` 293,648 bytes (smaller; 93% of the
  app partition free either way).
- Full 31-case A/B on device, all checksums identical across `-Og`/`-O2`
  (same pixels, same bench outputs):

  | case | -Og ms | -O2 ms | speedup |
  |---|---:|---:|---:|
  | `checksum_75k` | 1.935 | 1.083 | **1.79x** |
  | `frame_fill_38k` | 0.962 | 0.428 | **2.25x** |
  | `fill_u16_691k` | 16.906 | 8.085 | **2.09x** |
  | `read_u32_76k` | 0.600 | 0.229 | **2.62x** |
  | `scale2x_nearest` | 6.618 | 3.878 | **1.71x** |
  | `scale3x_u16x3` | 12.771 | 8.998 | **1.42x** |
  | `copy_u16_691k` | 22.968 | 13.197 | **1.74x** |
  | `read_u32_691k` | 9.911 | 6.626 | **1.50x** |
  | `map_decode_8k` | 1.169 | 0.765 | **1.53x** |
  | `scale3x_prod` | 10.473 | 9.492 | 1.10x |
  | `rot45x_prod` | 20.739 | 19.831 | 1.05x |
  | `memset_691k` / `memcpy_691k` / `memcpy_rows_1440` | — | — | 1.00x |

- Reading: compute/loop-bound cases gain clearly (up to 2.6x); cases already
  on the memory-system ceiling do not move (1.00x), which corroborates the
  write-path analysis rather than contradicting it. Notably `scale3x_prod`
  moves only 1.10x while `scale3x_u16x3` moves 1.42x — the optimizer mostly
  fixes the naive loop, and the already-decent `prod`/`u32m` shapes barely
  move, exactly as expected for memory-bound code.
- Raw logs: `/tmp/opencode/bench-tab5-4.jsonl` (`-Og`, 31 cases),
  `/tmp/opencode/bench-tab5-o2.jsonl` (`-O2`, 31 cases).
- The adoption gate has since passed (next section): identical CRCs on the
  GS1 stream plus `-O2` `runFrame` distributions.
- Reproducible procedure (default build stays `-Og`): fragment
  `firmware/sdkconfig.perf` (`CONFIG_COMPILER_OPTIMIZATION_PERF=y`) passed
  via `-DSDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.perf"` with
  `-DSDKCONFIG=<builddir>/sdkconfig` so generated files stay outside the
  repo. The bench manifest `config` field records `idf-O2` vs `idf-Og`
  (from Kconfig, in `bench_main.c`), and `validate.py`/`compare.py` treat a
  config change as a manifest mismatch unless explicitly relaxed.

## Where the per-frame budget goes (double buffered)

### Real ROM through the double-buffered path (2026-10-16)

- Same workload as the superseded run (Golden Sun 1, 600 no-input frames,
  every frame scaled and presented), but on the double-buffered path.
- Correctness: **600/600 per-frame CRCs matched the host vector, 0 mismatches**,
  0 refresh timeouts, no DSI underrun, no panic. The ROM SHA-256 read back from
  SD matched the recorded value, and the run was read-only (`SD mount` then
  `unmount`, no writes, no format).
- `refresh_done = 1413` in 29.29 s (~48.3 Hz), i.e. the panel kept refreshing at
  its own rate throughout; nothing was blocked on the display.
- Observed per-stage times over 600 frames (average / median / p95 / max, us):

  | stage | avg | median | p95 | max |
  |---|---:|---:|---:|---:|
  | `runFrame()` | 19806.8 | 10013 | 45918 | 71024 |
  | framebuffer CRC (verify only) | 3614.1 | 3736 | 3807 | 3854 |
  | 3x scale | 12507.9 | 12487 | 12659 | 12804 |
  | present (no-copy path) | 588.2 | 591 | 602 | 610 |
  | wait for refresh | 12280.3 | 14803 | 18385 | 20766 |

- End to end 600 frames in 29.29 s = **20.49 displayed frames/s**.
- Two things to read from this table:
  - `present` fell from 876.1 us to **588.2 us** and no longer moves data at all:
    it is `esp_cache_msync` of the 720x480 region plus setting `cur_fb_index`.
    The 1.38 MB DMA2D copy of the old path is gone.
  - `wait` max is **20766 us**, exactly one refresh period (20.77 ms at
    48.21 Hz): the wait is bounded by the panel, never by a stall, confirming
    the frame-boundary handshake works.
- The ROM path is therefore CPU-bound, not display-bound: emulation (19.8 ms avg,
  45.9 ms p95) plus scale (12.5 ms) already exceed the 20.77 ms period, so it
  cannot be vsync-locked. The CRC is verification-only; without it the path is
  ~45 ms/frame.
- PSRAM: `psram_free = 20,660,164` after the run (33,551,752 before). The
  difference covers the two DPI frame buffers (3,686,400 bytes), the 8 MiB ROM
  image, the 240x160 framebuffer and the timing tables.
- Raw log: `/tmp/opencode/stream-serial3.log`.

### Same GS1 stream at -O2 (2026-10-16): CRC gate passes, core 1.18x

- Same 600 no-input frames, `-O2` app build, identical ROM SHA-256.
  **600/600 CRCs match, 0 mismatches** — the optimizer does not change
  emulation results, so `-O2` is safe for the core by this gate.
- Per-stage times over 600 frames (average / median / p95 / max, us):

  | stage | avg | median | p95 | max | vs -Og avg |
  |---|---:|---:|---:|---:|---:|
  | `runFrame()` | 16715.8 | 9077 | 35987 | 57306 | **1.18x** |
  | framebuffer CRC (verify only) | 2987.1 | 3125 | 3184 | 3197 | 1.21x |
  | 3x scale | 11523.1 | 11457 | 11741 | 11860 | 1.09x |
  | present (no-copy path) | 568.1 | 574 | 586 | 599 | 1.04x |
  | wait for refresh | 14236.9 | 17340 | 19904 | 20671 | — |

- End to end 600 frames in 27.63 s = **21.72 displayed frames/s** (was 20.49).
- The `-O2` effect is real but small where it matters most: `runFrame` p95
  falls 45.9 -> 36.0 ms and max 71.0 -> 57.3 ms, yet the frame (31.8 ms of
  work without wait) is still far above the 20.77 ms period. `-O2` is worth
  adopting (free, safe by CRC), but it is not the route to 48.2 fps; the
  structural items (idle-loop, audio waste, DMA attribution, overlap) remain.
- `psram_free` identical at 20,660,164 — same allocation behaviour.
- Raw log: `/tmp/opencode/stream-o2-serial.log`.

### Rotated GS1 stream on device (2026-10-16): 600/600 CRC, 19.23 fps

- First run with the firmware display path switched to rotate-CCW-90 + 4.5x
  aspect-fit (720x1080 rect at y=100). Same GS1 600-frame workload, `-O2`.
- Correctness: **600/600 CRCs match, 0 mismatches**, no panic/timeout. The
  CRC gate covers the unrotated 240x160 emulator framebuffer, so rotation is
  verified not to disturb emulation; the rotated pixels themselves are covered
  by the host corner-oracle + reference tests and the bench checksum equality.

  | stage | avg | median | p95 | max |
  |---|---:|---:|---:|---:|
  | `runFrame()` | 16762.1 | 9130 | 36076 | 57231 |
  | framebuffer CRC (verify only) | 2967.9 | 3102 | 3152 | 3186 |
  | rot+4.5x scale | 23995.1 | 23979 | 24183 | 24232 |
  | present (no-copy path) | 548.0 | 549 | 556 | 562 |
  | wait for refresh | 7704.0 | 5168 | 16752 | 20735 |

- End to end 600 frames in 31.20 s = **19.23 displayed frames/s**.
- The in-path rot scale (24.0 ms) runs ~4.2 ms slower than the panel-idle
  bench (`rot45x_prod` `-O2`: 19.83 ms). Same DSI scan-out contention effect
  as the 3x path (~2.0 ms there); the gap scales roughly with bytes written
  (2.25x), as expected.
- Raw log: `/tmp/opencode/stream-rot-serial.log`.
- User visual verdict (2026-10-16): **PASS** — rotated 90 deg CCW, full-width,
  bands and orientation correct on the held last frame.

### Rotated FFTA stream on device (2026-10-16): 600/600 CRC, 14.04 fps

- Same FFTA 600-frame workload through the rotated path, `-O2`. **600/600
  CRCs match, 0 mismatches**; `psram_free` identical to the unrotated FFTA run
  (12,271,556).

  | stage | avg | median | p95 | max | vs -Og unrotated |
  |---|---:|---:|---:|---:|---:|
  | `runFrame()` | 30770.0 | 30520 | 41807 | 84379 | **1.25x** (was 38587.4) |
  | framebuffer CRC (verify only) | 2761.5 | 2571 | 3104 | 3218 | 1.24x |
  | rot+4.5x scale | 24026.4 | 23997 | 24173 | 24198 | — (path changed) |
  | present (no-copy path) | 546.6 | 549 | 556 | 561 | 1.00x |
  | wait for refresh | 13093.8 | 13871 | 19201 | 20681 | — |

- End to end 600 frames in 42.73 s = **14.04 displayed frames/s**.
- `-O2` helps the heavy title most in absolute terms (runFrame −7.8 ms avg,
  p95 −10.7 ms, max −19.5 ms), yet the frame (57.6 ms of work) remains ~4x
  the period. Structural work stands.
- Raw log: `/tmp/opencode/stream-rot-ffta-serial.log`.
- User visual verdict (2026-10-16): **PASS** — correct colour/orientation,
  no tearing (reduced motion rate accepted as emulation cost).

### Stream without per-frame CRC (2026-10-16): only 1.3% faster

- Same GS1 workload, `-O2`, `GBA_STREAM_VERIFY_CRC=OFF`: the checksum leaves
  the timed pipeline (`crc_verify=off` in the log). Result: 600 frames in
  30.80 s = **19.48 fps** vs 31.20 s / 19.23 fps verifying.
- Stage avgs: runFrame 16.88, crc 0.0, scale 24.08, present 0.55, wait 9.82 ms.
- Removing ~3 ms of CRC work saved only 0.4 s over the run because the
  refresh wait absorbs the slack: the loop is refresh-paced at ~2.5 periods
  per frame, and fps only moves when a whole 20.77 ms period of work comes
  out. Lesson: micro-savings below ~20 ms/frame do not change displayed fps;
  report them as stage times, not fps claims.
- Raw log: `/tmp/opencode/stream-nocrc-serial.log`.

### Idle-loop DETECT on GS1 (2026-10-16): CRC-safe, no gain

- Same GS1 workload, `-O2`, verifying, `GBA_IDLE_DETECT=ON`: **600/600 CRCs
  match** — skipping busy-waits does not change this game's pixels.
- runFrame avg 16,937.9 us vs 16,715.8 REMOVE (+1.3%, noise); median/p95/max
  all within noise. fps 19.21 vs 19.23.
- Conclusion for GS1's intro sequence: either it has no detectable idle loop
  or DETECT never latches. An `idleLoop` readback was added (observational,
  under the same flag) and the A/B moves to FFTA, the heavier title where a
  busy-wait would matter more. Not adopted as default on this evidence.
- Raw log: `/tmp/opencode/stream-idle-serial.log`.

### SD ROM load split: read-bound at 1.70 MiB/s, buffer misaligned (2026-10-16)

- GS1 8 MiB load, instrumented shared loader (`gba_rom_load_image` now logs
  read vs hash per run): **read 4925.5 ms (1.70 MiB/s), hash 153.2 ms
  (55 MiB/s)**. Read dominates 32:1; hashing is negligible.
- `buf_mod64=24` (nonzero) with `cache_line=64`: the PSRAM ROM buffer is
  **not cache-line aligned**, so per the SD survey each 64 KiB chunk likely
  runs as 128 single-sector DMA+memcpy bounces instead of one multi-block
  DMA. 1.70 MiB/s against a 40 MHz 4-bit bus (~20 MiB/s raw) is consistent
  with that.
- Same run re-verified the stream: 600/600 CRC, 19.25 fps, runFrame 16.68 ms
  (repro of the -O2 rotated baseline).
- Next: 64 B-aligned ROM allocation A/B on the read component only; then
  chunk-size sweep if alignment alone does not close the gap. Startup only —
  steady-state fps is unaffected either way.
- Raw log: `/tmp/opencode/stream-o2-sd-serial.log`.

### Renderer timing on GS1 (2026-10-16): draw is ~12% of runFrame

- `GBA_RENDER_TIMING=ON` stream, 600 frames: **CRCs still match** (shim is
  observational), runFrame avg 17,154.9 us (+2.6% vs OFF — the shim's own
  cost, 160 timer reads/frame).
- `render: draw_ms=1168.8 calls=95874 finish_ms=10.7 calls=600 frames=600
  maxframe_ms=11.8@463`: scanline drawing averages **1.95 ms/frame**
  (159.8 lines/frame — the clean-scanline early-out fires occasionally),
  finish is negligible. Slowest render frame is 11.8 ms vs slowest runFrame
  58.2 ms.
- Conclusion: the software renderer is ~12% of the frame; the p95/max spikes
  live elsewhere (DMA bursts, audio synthesis, and event processing are the
  remaining named suspects — DMA/audio accounting is next, same pattern).
- Raw log: `/tmp/opencode/stream-timing-serial.log`.

### DMA + audio accounting on GS1 (2026-10-16): render 12%, audio 10%

- `GBA_FRAME_ACCOUNT=ON` stream, 600 frames: **CRCs still match**
  (wrappers are observational), runFrame avg 18,134 us (+8.5% vs unwrapped —
  the per-memop branch cost; interpret shares against the unwrapped 16.7 ms).
- `dma: bytes=4201556 calls=1052132 frames=600 maxframe_bytes=92232@0`:
  **7.0 KB and 1,754 calls per frame** average; the max is frame 0 (initial
  setup blit), so steady-state DMA is small — bulk in-frame DMA is NOT the
  spike mechanism on this workload.
- `audio: synth_ms=985.4 events=164354`: **1.64 ms/frame, 274 events/frame**
  (exactly the predicted cadence), ~10% of runFrame, all discarded (no
  output). Synthesis runs to completion every frame.
- GS1 runFrame budget (≈16.7 ms unwrapped): render 1.95 + audio 1.64 +
  small DMA ≈ 3.7 ms identified; **~13 ms remains CPU/memory/event
  processing**. Spikes (max 61.8 ms runFrame vs 11.9 render max) are not
  renderer-driven.
- A top-3 slowest-frame index log (`run_slow3`) was added so stage maxima can
  be correlated by frame number; FFTA accounting is next.
- Raw log: `/tmp/opencode/stream-account-serial.log`.

### DMA + audio accounting on FFTA (2026-10-16)

- `GBA_FRAME_ACCOUNT=ON` stream, 600 frames: **CRCs still match**,
  runFrame avg 32,984 us (+7.2% vs unwrapped — same wrapper cost as GS1).
- `dma: bytes=1873660 calls=588706 frames=600 maxframe_bytes=138068@3`:
  **3.1 KB and 981 calls per frame** — even less DMA than GS1; the max is an
  early setup burst, not a mid-run spike mechanism.
- `audio: synth_ms=1023.1 events=164354`: **1.71 ms/frame, 274 events/frame**
  — identical cadence to GS1 (clock-driven, game-independent), all discarded.
- `render: draw_ms=2856.6 calls=95874 ... maxframe_ms=10.2@523`: **4.76
  ms/frame** (2.4x GS1 — this is where FFTA's extra cost partly sits).
- `run_slow3: #4=94678us #5=59265us #517=47945us`: the two slowest frames
  are startup transients (boot/setup, not representative); #517 is the largest
  mid-run spike. Neither coincides with the DMA max (@3) or render max (@523).
- FFTA runFrame budget (≈30.8 ms unwrapped): render 4.76 + audio 1.71 + small
  DMA ≈ 6.6 ms identified; **~24 ms (78%) remains CPU/memory/event
  processing** — same shape as GS1, larger scale.
- Raw log: `/tmp/opencode/stream-account-ffta-serial.log`.

### Per-frame top-20 on FFTA, fixed numbering (2026-10-16): #4 is DMA

- A reporting bug was found and fixed: `report_stage` sorts its array in
  place, so the top-20 selection ran over frame-sorted (not frame-ordered)
  data and printed wrong frame numbers. Selection now runs before any
  `report_stage` call; all indices are 1-based everywhere.
- Corrected top frames (CRC match, `run/draw/dmaR+dmaW/event/audio`):
  - **#4: run=98953 draw=447 dma=138072 event=64350 audio=804** — a 138 KB
    DMA burst inside event processing explains the slowest frame. DMA max
    (@4, 1-based) and run max (#4) coincide.
  - #5: run=61450, dma=60584, event=32567 — same startup-burst shape.
  - #513–535 cluster: run≈49 ms, draw≈10.2 ms (2x normal), event≈16.9 ms,
    dma≈3.7 KB, audio≈2 ms — a heavy scene, not a DMA burst.
- `events: process_ms=6201.8 calls=614284`: **10.3 ms/frame average** inside
  event processing (includes draw+audio+DMA service by nesting). Against
  runFrame avg 34.6 ms, roughly **30% in-events / 70% outside** (direct ARM
  stepping between events).
- FFTA steady budget: ARM stepping ≈24 ms + events ≈10.3 ms (draw 4.76,
  audio 1.7, DMA service/timers/IRQ ≈3.9).
- Raw log: `/tmp/opencode/stream-account3-serial.log`.

### FFTA ROM load with alignment (2026-10-16): 16 MiB in 1.45 s

- `read_ms=1451.5 (11.02 MiB/s)`, hash 224.4 ms, SHA-256 matches, load test
  passes. Predicted ~1.5 s confirmed; GS1-class startup is now ~1 s total.
- Raw log: `/tmp/opencode/ffta-load-serial.log`.

### SD ROM load fix: 64 B alignment gives 6.75x (2026-10-16, adopted)

- `GBA_ALIGNED_ROM_ALLOC=ON` (now the default): GS1 8 MiB loads in
  **729.9 ms (11.49 MiB/s)** vs 4925.5 ms (1.70 MiB/s) unaligned —
  **6.75x**. `buf_mod64=0` confirms the multi-block DMA path.
  SHA-256 identical (`088bedae...`), so the adoption gate passes.
- GS1 startup read drops ~4.9 s -> ~0.7 s; FFTA 16 MiB should drop
  ~10 s -> ~1.5 s (same mechanism, to be confirmed on a load run).
- Adoption confirmed end-to-end (default ON, stream rebuild): ROM load
  730.0 ms, 600/600 CRC, 19.23 fps, runFrame 16.69 ms — all repro.
- Raw log: `/tmp/opencode/romload-align-serial.log`.

### Idle-loop DETECT on FFTA (2026-10-16): detector never latches, no gain

- Same FFTA workload, `-O2`, verifying, `GBA_IDLE_DETECT=ON`: **600/600 CRCs
  match**. runFrame avg 31,107.5 us vs 30,770.0 REMOVE (+1.1%, noise);
  median/p95/max all within noise. fps 14.05 vs 14.04.
- Decisive readback: `idleLoop=ffffffff` (NONE) at end of run — the detector
  never latched onto a loop in 600 frames. (`idleOptimization` reads back -1
  = IGNORE: the detect-fail path writes that back when analysis rejects a
  candidate, confirming the machinery engaged but found nothing skippable.)
- Conclusion: idle-loop skipping is a dead end for both measured workloads.
  Not adopted. The remaining core-side items are audio-synthesis cost,
  in-frame DMA attribution, and renderer timing — all observational first.
- Raw log: `/tmp/opencode/stream-idle-ffta-serial.log`.

### Real animated title: Final Fantasy Tactics Advance (2026-10-16)

- Same mode and path, `-D GBA_ROM_PROFILE=ffta` (16 MiB ROM, SHA-256
  `b13dd536808ef5d0fd4494386a9499f6feb8310835d3f867cd17cc340d82bf9a`), 600
  no-input frames.
- Correctness: **600/600 per-frame CRCs matched the host FFTA vector, 0
  mismatches**, 0 refresh timeouts, no underrun, no panic, read-only SD, ROM
  hash matched. This is a different title and a different vector from the GS1
  run, so the emulation and the profile plumbing are both confirmed.
- `refresh_done = 2042` in 42.34 s (~48.2 Hz); the panel again ran at its own
  rate throughout.
- Per-stage times over 600 frames (average / median / p95 / max, us):

  | stage | avg | median | p95 | max |
  |---|---:|---:|---:|---:|
  | `runFrame()` | 38587.4 | 38819 | 52506 | 103849 |
  | framebuffer CRC (verify only) | 3417.3 | 3220 | 3794 | 3853 |
  | 3x scale | 12571.5 | 12552 | 12773 | 12818 |
  | present (no-copy path) | 542.2 | 584 | 603 | 608 |
  | wait for refresh | 15424.6 | 16846 | 20102 | 20731 |

- End to end 600 frames in 42.34 s = **14.17 displayed frames/s**. FFTA is a much
  heavier core workload than Golden Sun 1 (38.6 ms vs 19.8 ms average per frame,
  103.8 ms worst case), so the *picture* is correct and tear-free while the
  *motion* runs at roughly a quarter of real time. That gap is emulation cost,
  not a display defect; FFTA is not a viable 48.2 fps target without core-side
  work.
- `wait` max is 20731 us, again one refresh period, so the frame-boundary
  handshake held even on the slowest frames.
- PSRAM: `psram_free = 12,271,556` after the run (33,551,752 before). The
  difference covers the two DPI frame buffers (3,686,400 bytes), the 16 MiB ROM
  image, the 240x160 framebuffer and the timing tables.
- Raw log: `/tmp/opencode/ffta-stream-serial.log`.

### Stage budget summary

Ordering per frame in the continuous ROM path (double buffered):

| stage | note |
|---|---|
| `runFrame()` | mGBA core; ~19.8 ms avg / 45.9 ms p95 (GS1), ~38.6 ms / 103.8 ms (FFTA) |
| framebuffer CRC | verification only; ~3.4-3.6 ms, not part of the product path |
| scale | 240x160 -> 720x480 into the back frame buffer; ~12.6 ms |
| present | `draw_bitmap` no-copy branch: `esp_cache_msync` + `cur_fb_index`; ~0.5-0.6 ms |
| wait | `on_refresh_done` for the next frame boundary; up to one period |

Scaling and emulation are the two items worth attacking, in that order of
certainty: the scaler is a known 12.6 ms of pure PSRAM traffic and is the same
for every title, while the display-path ceiling (48.21 Hz) is already proven
attainable by the ROM-free pattern. The emulation side varies wildly by title:
FFTA costs about twice Golden Sun 1 per frame, which is the difference between
20.5 fps and 14.2 fps after scaling is accounted for.
# Experimental PPA/display overlap test

The serialized `GBA_DISPLAY_PPA_STREAM_TEST` remains the baseline. The opt-in
`GBA_DISPLAY_PPA_OVERLAP_TEST` keeps the PPA CCW90+4.5x conversion blocking,
but runs the next emulated frame (including its CRC) while the previously
presented DPI buffer scans out. It waits for refresh before starting PPA into
the other buffer. Compare only after a CRC-verified serialized run; the overlap
mode retains the selected GS1/FFTA per-frame CRC gate by default. No audio,
input, saves, or SD writes are used.

Device verification command (from `firmware/`, with the Linux ESP-IDF
environment configured):

```sh
idf.py -D GBA_DISPLAY_PPA_OVERLAP_TEST=ON -D GBA_ROM_PROFILE=gs1 build
```

For FFTA substitute `-D GBA_ROM_PROFILE=ffta`. Keep the 20 kHz backlight
initialization unchanged. Measurement boundaries now separate the buffer-free
wait from the PPA call; the earlier single GS1 run's PPA stage included that
wait and is superseded below. Each mode/profile combination was run three times
(600 frames per run); displayed values below were identical across the three
runs to the shown precision.

| ROM | serialized PPA | overlap PPA | async PPA | async vs overlap | CRC / timeouts |
|---|---:|---:|---:|---:|---|
| GS1 | 29.76 fps | 35.19 fps | **38.62 fps** | **+9.7%** | 600/600 / 0 |
| FFTA | 19.01 fps | 23.79 fps | **27.52–27.73 fps** | **+15.9%** | 600/600 / 0 |

Corrected overlap stage averages (serialized → overlap, us):

| ROM | runFrame | CRC | PPA | present | refresh / buffer wait |
|---|---:|---:|---:|---:|---:|
| GS1 | 16,051 → 16,087 | 2,793 → 2,796 | 9,149 → 9,162 | 32 → 34 | 5,565 → 320 |
| FFTA | 30,104 → 30,066 | 2,636 → 2,645 | 9,240 → 9,254 | 30 → 32 | 10,596 → 21 |

GS1 overlap refresh-wait p95/max: 1,069/8,427 us; FFTA: 24/3,720 us. The
benefit is largest on FFTA because its longer `runFrame` typically finishes
after the next panel refresh, so the buffer is already free when conversion
begins. On GS1 there is still a shorter residual wait. User confirms no visible
flicker or tearing on the overlap display for either ROM. This remains an
opt-in test mode; results establish these two workloads, not every ROM.

### CRC-off baseline, ARMCore placement, and async PPA experiment (2026-10-17)

These are separate experiments, all on the same Tab5/P4 rev v1.0 with the
existing O2/performance sdkconfig and three 600-frame runs per ROM/mode. CRC-off
is a timing baseline only; correctness is established by separate CRC-enabled
runs. It removes about 2.8–3.0ms/frame of checksum work, but throughput does
not increase by that full amount because refresh/buffer waiting changes with
the schedule:

| ROM | CRC-off overlap throughput (3-run range) | runFrame avg | PPA avg | buffer wait avg |
|---|---:|---:|---:|---:|
| GS1 | 37.66–37.76 fps | 14,269–14,274 us | 8,946–8,947 us | 3,244–3,308 us |
| FFTA | 26.04–26.06 fps | 28,960–28,964 us | 9,130–9,131 us | 251–282 us |

Representative CRC-off stage statistics (avg / p95 / max, us; CRC stage is
disabled and therefore zero):

| ROM | runFrame | CRC | PPA | present | buffer wait |
|---|---:|---:|---:|---:|---:|
| GS1 | 14,269 / 34,575 / 56,425 | 0 / 0 / 0 | 8,946 / 9,433 / 9,507 | 15 / 33 / 36 | 3,244 / 6,459 / 8,928 |
| FFTA | 28,964 / 40,450 / 84,428 | 0 / 0 / 0 | 9,131 / 9,422 / 9,491 | 19 / 32 / 37 | 282 / 2,288 / 19,062 |

The opt-in `GBA_ARMCORE_INTERNAL=ON` allocator placed exactly the 412-byte
`ARMCore` object in internal DRAM; other anonymous mGBA mappings remained in
PSRAM. It was CRC-enabled and retained the default PSRAM placement as the
control. Results did not show a useful gain: GS1 was 34.98 fps in all three
internal runs versus 35.19 fps for PSRAM; FFTA was 23.77–23.79 fps versus
23.79 fps for PSRAM. Keep the option as an A/B diagnostic, not a recommended
default.

Representative ARMCore-internal run stage statistics (avg / p95 / max, us):

| ROM | runFrame | CRC | PPA | present | buffer wait |
|---|---:|---:|---:|---:|---:|
| GS1 | 16,258 / 35,485 / 57,501 | 2,796 / 3,033 / 3,186 | 9,155 / 9,424 / 9,465 | 34 / 42 / 46 | 329 / 985 / 9,385 |
| FFTA | 30,074 / 41,252 / 84,706 | 2,645 / 2,920 / 3,206 | 9,253 / 9,437 / 9,459 | 33 / 42 / 46 | 17 / 22 / 143 |

The separately gated `GBA_DISPLAY_PPA_ASYNC_TEST=ON` mode snapshots the
75-KiB emulator framebuffer to PSRAM, submits non-blocking PPA, and runs the
next emulated frame while PPA reads that immutable snapshot. A CPU1 presentation
worker performs the panel handoff after PPA completion; the main task waits for
refresh before reusing the destination. A one-tick yield every 64 frames avoids
starving IDLE0. Three runs per ROM completed with all 600 CRCs matching, zero
PPA/refresh timeouts, and no WDT/panic logs. The asynchronous mode remains
experimental: CRCs validate the emulated framebuffer, not the transformed
panel image, and human visual acceptance for this new scheduling path is still
required before adoption.

| ROM | Async runFrame avg / p95 / max | CRC avg / p95 / max | PPA completion avg / p95 / max | snapshot avg / p95 / max | present avg / p95 / max | refresh wait avg / p95 / max | throughput |
|---|---:|---:|---:|---:|---:|---:|---:|
| GS1 | 17,008 / 36,281 / 57,721 us | 2,991 / 3,180 / 3,197 us | 9,437 / 9,457 / 9,471 us | 967 / 1,124 / 1,274 us | 9 / 15 / 43 us | 4,035 / 6,990 / 8,941 us | 38.62 fps |
| FFTA | 30,997 / 42,190 / 84,668 us | 2,785 / 3,162 / 3,214 us | 9,438 / 9,464 / 9,476 us | 1,078 / 1,176 / 1,208 us | 10 / 17 / 50 us | 289–562 / 2,161–5,246 / 5,338–7,295 us | 27.52–27.73 fps |

Values are the per-run summaries; repeated runs were effectively identical to
the shown precision except elapsed throughput and refresh wait. `ppa_block`
additional wait was 22 us average in both cases (8 us p95; 8.7ms max from the
first frame, before useful overlap). Async improves both tested workloads but
still falls well short of the 59.73 emulated-frame/s real-time target; do not
conflate that with the panel's separately measured 48.21Hz refresh ceiling.
