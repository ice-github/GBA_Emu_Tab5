"""ROM catalog + run manifest generator.

Reads an EXTERNAL rom directory (env ROMS_DIR, default ~/workspace/Roms)
and prints a JSON manifest: file metadata (name/size/sha256) plus the
pinned run configuration (core rev, frames, input, audio, color format).
ROMs are never copied into git; only this metadata is recorded.

Usage:
  ROMS_DIR=~/workspace/Roms python3 tools/roms/manifest.py \
      --core-rev 26b7884b --frames 600 --sample-every 60 \
      --color native32 > results/manifest-<date>.json
"""
import argparse
import hashlib
import json
import os
import sys
from pathlib import Path

GBA_FRAME_CYCLES = 280896
GBA_FPS = 16777216 / GBA_FRAME_CYCLES  # ~59.7275


def sha256_of(path, chunk=1 << 20):
    h = hashlib.sha256()
    with open(path, "rb") as fh:
        while True:
            b = fh.read(chunk)
            if not b:
                break
            h.update(b)
    return h.hexdigest()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--roms-dir", default=os.environ.get(
        "ROMS_DIR", os.path.expanduser("~/workspace/Roms")))
    ap.add_argument("--core-rev", required=True)
    ap.add_argument("--frames", type=int, default=600)
    ap.add_argument("--sample-every", type=int, default=60)
    ap.add_argument("--input", default="none",
                    help="input script id; 'none' = no buttons pressed")
    ap.add_argument("--audio", default="counted",
                    help="audio handling: 'counted' = samples+CRC via "
                         "AV sink, no playback")
    ap.add_argument("--color", default="native32",
                    help="'native32' (host default) or 'rgb565' "
                         "(COLOR_16_BIT+COLOR_5_6_5 build)")
    ap.add_argument("--bios", default="hle",
                    help="'hle' = mGBA built-in HLE BIOS")
    args = ap.parse_args()

    roms_dir = Path(args.roms_dir)
    if not roms_dir.is_dir():
        print(f"roms dir not found: {roms_dir}", file=sys.stderr)
        return 1
    roms = []
    for p in sorted(roms_dir.glob("*.gba")):
        roms.append({"file": p.name, "bytes": p.stat().st_size,
                     "sha256": sha256_of(p)})
    if not roms:
        print(f"no .gba files in {roms_dir}", file=sys.stderr)
        return 1
    manifest = {
        "roms_dir": str(roms_dir),
        "roms": roms,
        "run": {
            "core": "mgba",
            "core_rev": args.core_rev,
            "frames": args.frames,
            "sample_every": args.sample_every,
            "input": args.input,
            "audio": args.audio,
            "color": args.color,
            "bios": args.bios,
            "frameskip": 0,
            "gba_frame_cycles": GBA_FRAME_CYCLES,
            "gba_fps": GBA_FPS,
        },
    }
    print(json.dumps(manifest, indent=2))
    return 0


if __name__ == "__main__":
    sys.exit(main())
