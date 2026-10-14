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
