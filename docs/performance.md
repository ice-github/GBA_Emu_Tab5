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
