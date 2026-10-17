#!/usr/bin/env python3
"""Generate a per-frame framebuffer CRC verification vector as a C header.

The generated header is verification data (per-frame RGB565 framebuffer CRCs),
not ROM content. It must be produced with the RGB565 host runner so the values
match the Tab5 port.

Usage:
    tools/gen_frame_crcs.py <rom.gba> --symbol gba_ffta_frame \
        --label "Final Fantasy Tactics Advance (Japan)" \
        --out firmware/main/gba_ffta_frame_crcs.h [--frames 600]

The generated names are derived from --symbol: the array is `<symbol>_crc` and
the entry count is `<SYMBOL>_COUNT`, so --symbol gba_gs1_frame reproduces the
existing Golden Sun 1 header.
"""
import argparse
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

HEADER = """/* Generated verification vector, not ROM content.
 *
 * Per-frame RGB565 framebuffer CRC32 verification vector for:
 *   {label}
 * running {frames} no-input frames, produced by the host reference runner:
 *   ./host/gba-smoke-565 <rom> --frames={frames} --sample-every=1
 * built with mGBA COLOR_16_BIT + COLOR_5_6_5, matching the Tab5 port.
 * Regenerate with tools/gen_frame_crcs.py.
 */
#pragma once

#include <stdint.h>

#define {count} {frames}u

/* {table}[i] is the CRC after running frame i+1. */
static const uint32_t {table}[{count}] = {{
"""


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("rom", type=Path)
    ap.add_argument("--symbol", required=True,
                    help="base name, e.g. gba_gs1_frame -> gba_gs1_frame_crc")
    ap.add_argument("--label", required=True,
                    help="human-readable ROM name for the header comment")
    ap.add_argument("--out", type=Path, required=True)
    ap.add_argument("--smoke", type=Path,
                    default=ROOT / "host" / "gba-smoke-565")
    ap.add_argument("--frames", type=int, default=600)
    args = ap.parse_args()

    if not args.symbol.islower() or not re.fullmatch(r"[a-z0-9_]+", args.symbol):
        print(f"--symbol must be a lowercase C identifier: {args.symbol}",
              file=sys.stderr)
        return 2
    if not args.smoke.is_file():
        print(f"missing host runner: {args.smoke}", file=sys.stderr)
        return 2
    if not args.rom.is_file():
        print(f"missing ROM: {args.rom}", file=sys.stderr)
        return 2

    p = subprocess.run(
        [str(args.smoke), str(args.rom), f"--frames={args.frames}",
         "--sample-every=1"],
        capture_output=True, text=True, check=True)
    crcs = re.findall(r'"fb_crc32":"([0-9a-f]{8})"', p.stdout)
    if len(crcs) != args.frames:
        print(f"expected {args.frames} CRCs, got {len(crcs)}", file=sys.stderr)
        return 1

    body = []
    for i in range(0, args.frames, 6):
        body.append("    " + ", ".join(f"0x{v}u" for v in crcs[i:i + 6]) + ",")

    text = HEADER.format(label=args.label, frames=args.frames,
                         table=f"{args.symbol}_crc",
                         count=f"{args.symbol.upper()}_COUNT")
    text += "\n".join(body) + "\n};\n"

    args.out.write_text(text)
    print(f"wrote {args.out} ({args.frames} frames, "
          f"first={crcs[0]} last={crcs[-1]})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
