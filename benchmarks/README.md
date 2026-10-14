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
| `rgb565_to_xrgb` | 240×160 RGB565 → XRGB8888 expansion | pixels |

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
