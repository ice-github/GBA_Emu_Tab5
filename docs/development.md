# Development workflow for Tab5 (Linux first, reusable knowledge)

`firmware/` is a bench-app skeleton (M1). Host bench runs today;
device build/run lands in M2. Steps below apply to both.

## 1. Operating model

- Primary target OS: real Linux. WSL + usbipd is the secondary path for device
  access.
- Use ESP-IDF **v5.5.4** with ESP32-P4 target.
- User-space toolchain preferred; never touch OS Python/CMake.
- User-provided ROM/BIOS, backups, and measurement artifacts live outside git.

## 2. User-space toolchain layout (from verified run)

| What | Where |
|---|---|
| ESP-IDF | `~/esp/esp-idf-v5.5.4/` (v5.5.4, `--recursive`) |
| IDF tools / Python env | `~/.espressif/` (`IDF_TOOLS_PATH` default) |
| Build Python 3.12 | `uv python install 3.12` |
| Host tools venv | `~/.venv-tab5-host` + `pyserial` |
| Flash backups | `backups/` (git-ignored) |
| Run logs | outside the repository |

Build Python 3.12 is required because the distro may ship 3.14; put a shim
dir first on `PATH` with `python3 -> uv python 3.12` before running
`install.sh esp32p4`.

## 3. Linux permissions

- Add the account to the serial group (`uucp` on CachyOS; otherwise `dialout`).
- Then log in again / restart the agent session so group membership applies.
- Gate: `test -w /dev/ttyACM0` should succeed for the agent process.
- Temporary gate only: `sudo setfacl -m u:na0ki:rw /dev/ttyACM0`.

## 4. Device identification and preservation

- Use the stable name in place of `ttyACM0`:
  `/dev/serial/by-id/usb-Espressif_USB_JTAG_serial_debug_unit_<MAC>-if00`.
- Read-only probe before any write:
  `python -m esptool --port "$PORT" chip_id` / `flash-id`.
- Back up the full 16 MB flash before the first write, with size + SHA-256 and
  the restore command `write-flash 0x0 <file>`.
- Gate: chip + revision + flash size known, backup recorded, or stop with reason
  before flashing.

## 5. Revision and dependency constraints

Example for the prior board to carry forward (adjust only after probe):
- `CONFIG_ESP32P4_SELECTS_REV_LESS_V3=y`,
  `CONFIG_ESP32P4_REV_MIN_100=y` (range v1.0–v1.99).
- LFN heap, no format-on-mount-fail, 16 K main stack, 16 MB Flash.
- `firmware/dependencies.lock` in the source repo listed:
  `m5stack_tab5 1.3.2`, `esp_lvgl_port 2.5.0`, `esp_codec_dev 1.5.11`.

Verify the generated `sdkconfig` (never trust `.defaults` alone), and after
changes delete generated files and rebuild from clean when a defaults option
appears to have no effect.

## 6. Build / flash commands (reference once firmware exists)

```bash
. ~/esp/esp-idf-v5.5.4/export.sh && idf.py -C firmware build
idf.py -C firmware -p "$PORT" flash
idf.py -C firmware -p "$PORT" monitor
```

Rules inherited from prior experience:
- No `erase-flash` in the normal path, no manual offsets.
- No running monitor and sender concurrently.
- Re-check port permission/ownership before requesting BOOT/RESET button help.

## 7. WSL device-access notes

- USB ID for M5Stack Tab5/esptool integration: `303a:1001`.
- BUSID (`1-2`) can change on reconnect; use `--hardware-id 303a:1001`.
- Device group is `dialout`; user must belong; fix via root WSL usermod.
- `uv` and `esptool` via `uvx` available (no `pip` in the referenced image).

Verify with `test -w` gates after each hot-plug.
