# Emulator adapter boundary (M4+ draft)

`components/emulator_adapter/include/emulator_adapter.h` defines the only
surface a vendored core may expose to the firmware app.

## Why thin and frame-granular

- Per-instruction or per-access callbacks through the adapter would add
  overhead to the hottest paths (CPU dispatch, memory map). Those stay
  inside the core.
- The firmware app owns scheduling: it calls `step_frame()`, then moves
  the RGB565 framebuffer to display, drains PCM to I2S, and polls input.
- ROM/save/audio/display memory budgeting is the app's job; see the memory
  table below (to be filled with measured numbers in M2/M4).

## Memory budget table (template — fill after measurement)

| region | size | placement | notes |
|---|---|---|---|
| ROM window | ? | PSRAM / SD-paged | 32 MB worst case never fully resident |
| EWRAM+IWRAM | 288 KiB + core overhead | internal or PSRAM | measure |
| VRAM/palette/OAM | ~98 KiB | — | measure |
| framebuffer ×N | 75 KiB each | PSRAM, DMA-capable | N=TBD (double-buffer min) |
| audio ring | ? | — | chunk = TBD frames |
| save blob | ≤128 KiB | NVS/SD | TBD |

## Error policy

- `load_rom` rejects oversize/unsupported ROMs before allocating the full
  window; never truncate silently.
- Out-of-memory, unreadable save, or missing BIOS (when required) stops
  with an explicit on-console error, not a reboot loop.
- Savestate/ROM/BIOS bytes are never committed to git.
