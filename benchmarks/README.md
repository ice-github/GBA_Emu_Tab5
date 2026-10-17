# Benchmarks (ROM-independent, core-independent)

All cases in this directory run without a GBA ROM, BIOS, or emulator core.
They measure the machine (host PC or Tab5), not emulation speed.

## Cases

| id | workload | unit basis |
|---|---|---|
| `memcpy_75k` | `memcpy` of one 240×160 RGB565 frame (75 KiB) | bytes |
| `memset_75k` | `memset` of one frame | bytes |
| `checksum_75k` | FNV-1a over one frame | bytes |
| `prng_fill_75k` | xorshift fill of one frame | bytes |
| `strided_read_75k` | 64 B-stride 4 B reads over one frame | bytes |
| `dispatch_4k` | 16-way switch dispatch over 4096 ops | ops |
| `map_decode_8k` | masked region decode over 8192 accesses | ops |
| `frame_copy_38k` | 240×160 RGB565 frame copy | pixels |
| `frame_fill_38k` | 240×160 RGB565 frame fill | pixels |
| `scale2x_nearest` | 240×160 → 480×320 nearest-neighbor 2x (CPU reference for PPA comparison) | src pixels |
| `scale3x_prod` | 240×160 → **720×480** 3x, the production `gba_scale` (16-bit stores, `memcpy` row duplication) | src pixels |
| `scale3x_u32m` | same 3x, paired 32-bit stores, `memcpy` row duplication | src pixels |
| `scale3x_u32x3` | same 3x, paired 32-bit stores, all three rows written directly | src pixels |
| `scale3x_u16x3` | same 3x, 16-bit stores, rows duplicated by loop (no libc `memcpy`) | src pixels |
| `rot45x_prod` | 240×160 → **720×1080** rotate-CCW-90 + 4.5x, production (16-bit, `memcpy` band duplication) | src pixels |
| `rot45x_u32m` | same rot+4.5x, 32-bit run stores (misalignment-safe), `memcpy` band duplication | src pixels |
| `rot45x_u32d` | same rot+4.5x, 32-bit run stores, all band rows written directly | src pixels |
| `rot45x_u16l` | same rot+4.5x, 16-bit stores, band rows duplicated by loop | src pixels |
| `rgb565_to_xrgb` | 240×160 RGB565 → XRGB8888 expansion | pixels |
| `copy_u16_691k` / `copy_u32_691k` / `memcpy_691k` | scalar 16-bit / 32-bit copy loop vs libc `memcpy` over the full 720×480 extent | bytes |
| `fill_u16_691k` / `fill_u32_691k` / `fill_u64_691k` | store width, values that vary per iteration | bytes |
| `memset_691k` / `memset_75k` | libc `memset` over the full extent vs a frame-sized buffer (128 KB L2 boundary) | bytes |
| `read_u32_691k` / `read_u32_76k` | streaming read of the full extent vs a frame-sized buffer | bytes |
| `scaler_block3_u16` / `scaler_block3_u32` | the scaler's inner loop alone: three stores per source pixel | bytes written |
| `memcpy_rows_1440` | the scaler's row-duplication pattern: 320 × 1440-byte `memcpy` | bytes written |

`scale3x_*` is a controlled comparison of the real panel layout: every variant
scales the same source into the same 720×480 destination inside a 720-wide
buffer, so the throughput basis is identical and **one run compares all four
without reflashing**. They differ in two effects only — 16-bit vs 32-bit stores,
and duplicating the two extra destination rows with `memcpy` (read + write of the
whole row) vs writing them directly from the source.

All four must produce identical pixels. The harness checksums each variant's
untimed output, so their `checksum` fields must be equal and
`tools/bench/validate.py` fails the run if they are not. The same rule holds
for the four `rot45x_*` cases as a group.

`rot45x_*` is the same controlled comparison for the rotated layout
(`gba_rotate_ccw45x_rgb565_nn_into`: 90° CCW + 4.5x into 720×1080, the
aspect-fit rect for the 720×1280 portrait panel at y=100). Runs are 4 or 5
pixels wide, so a run can start 2-byte-misaligned; the 32-bit variants store
a leading half pixel first in that case. See `docs/performance.md` for the
corner oracle (source top-left lands at rect bottom-left).

Note that the host and the Tab5 disagree on the ranking: the host compiler
auto-vectorises the 16-bit store loop and the whole destination fits in cache,
while the Tab5 has no SIMD and writes through PSRAM. Neither number predicts the
other; only the device run does.

The `*_691k` cases exist to explain *why* the scaler costs what it costs, and
they must store values that **vary per iteration**: a constant fill is
recognised by the compiler and lowered to `memset`/vector stores, which on the
host silently made every fill case run at the same 60 GB/s and hid the store
width difference entirely. Their findings are in `docs/performance.md` under
"ROM-independent machine bench".

Notes:
- Per-iteration checksums are mixed into the result; a `00000000` checksum
  means dead code and is rejected by `tools/bench/validate.py`.
- Timing includes the checksum pass, so absolute numbers are comparative,
  not pure op costs. Compare runs with `tools/bench/compare.py` on the
  same machine/config only.
- PPA uses bilinear scaling, so `scale2x_nearest` is a speed reference
  only; quality differs.

## Run (host)

```bash
make -C host check   # build + run + validate
python3 -m pytest tests/ -q
./host/bench_host --config=host-O2 --git-rev=$(git rev-parse --short HEAD) \
  > results/host-$(date +%Y%m%d).jsonl
python3 tools/bench/compare.py results/base.jsonl results/new.jsonl
```

## Run (device, once `firmware/` builds)

The firmware bench app prints the same JSONL over the console; capture it
with `idf.py monitor` or a serial logger, then run `validate.py` /
`compare.py` on the captured lines.

## Result policy

- Never commit `results/` (git-ignored). Summaries may go in Beads notes.
- Every published run records: git rev, core rev (`none` until a core
  lands), config/CFLAGS, CPU/PSRAM settings, sdkconfig hash (device).
- These numbers are hardware baselines. They do not claim any GBA fps.
