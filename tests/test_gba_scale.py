"""Host checks for the RGB565 integer scaler (components/gba_scale).

Builds and runs host/scale_test, then cross-checks its canonical output against
an independent Python implementation. No ROM, no device.
"""
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
HOST = ROOT / "host"


def build_scale_test():
    p = subprocess.run(["make", "-C", str(HOST), "scale_test"],
                       capture_output=True, text=True)
    assert p.returncode == 0, p.stdout + p.stderr


def run_scale_test(*args):
    p = subprocess.run([str(HOST / "scale_test"), *args],
                       capture_output=True, text=True)
    return p


def test_scale_test_passes():
    build_scale_test()
    p = run_scale_test()
    assert p.returncode == 0, p.stdout + p.stderr
    assert "0 failures" in p.stdout, p.stdout


def test_dump_matches_python_reference():
    """The dumped image must equal a straightforward Python scaling."""
    build_scale_test()
    p = run_scale_test("--dump")
    assert p.returncode == 0, p.stderr
    got = [[int(v, 16) for v in line.split()]
           for line in p.stdout.strip().splitlines()]
    assert len(got) == 9 and all(len(r) == 12 for r in got), "dump shape"

    src = [(0x0100 * i + i) & 0xFFFF for i in range(12)]
    want = [[src[(y // 3) * 4 + (x // 3)] for x in range(12)] for y in range(9)]
    assert got == want


def test_dump_values_are_replicated_blocks():
    """Sanity: each 3x3 block in the dump is a solid colour."""
    build_scale_test()
    p = run_scale_test("--dump")
    got = [[int(v, 16) for v in line.split()]
           for line in p.stdout.strip().splitlines()]
    for by in range(3):
        for bx in range(4):
            block = {got[by * 3 + dy][bx * 3 + dx]
                     for dy in range(3) for dx in range(3)}
            assert len(block) == 1, f"block ({bx},{by}) not solid: {block}"


def test_dump_rot_matches_python_reference():
    """The rotated dump must equal an independent Python implementation.

    4x2 source -> 9x18 output. Rotation is 90 CCW (top edge goes left):
    dest (lx,ly) samples source (sx,sy) = (sw-1-ry, rx) with
    rx,ry = (2*lx+1)//9, (2*ly+1)//9.
    """
    build_scale_test()
    p = run_scale_test("--dump-rot")
    assert p.returncode == 0, p.stderr
    got = [[int(v, 16) for v in line.split()]
           for line in p.stdout.strip().splitlines()]
    assert len(got) == 18 and all(len(r) == 9 for r in got), "dump-rot shape"

    sw, sh = 4, 2
    src = [(0x0100 * i + i) & 0xFFFF for i in range(sw * sh)]
    want = [[src[((2 * lx + 1) // 9) * sw + (sw - 1 - (2 * ly + 1) // 9)]
             for lx in range(9)] for ly in range(18)]
    assert got == want


def test_dump_rot_corners():
    """Corner oracle on the small dump: source corners land in fixed blocks."""
    build_scale_test()
    p = run_scale_test("--dump-rot")
    got = [[int(v, 16) for v in line.split()]
           for line in p.stdout.strip().splitlines()]
    # src values: index i -> 0x0101*i. top-right (idx 3) -> top-left block,
    # top-left (idx 0) -> bottom-left block.
    assert got[0][0] == 0x0303, f"top-left block: {got[0][0]:04x}"
    assert got[17][0] == 0x0000, f"bottom-left block: {got[17][0]:04x}"
    assert got[0][8] == 0x0707, f"top-right block: {got[0][8]:04x}"
    assert got[17][8] == 0x0404, f"bottom-right block: {got[17][8]:04x}"
