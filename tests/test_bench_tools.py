"""Unit tests for bench tooling (stats order, validate, compare). No ROM."""
import json
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def run_mod(mod, *args, input_text=None):
    p = subprocess.run([sys.executable, "-m", mod, *args],
                       input=input_text, capture_output=True, text=True,
                       cwd=ROOT / "tools" / "bench")
    return p


def test_validate_accepts_good_run():
    good = {"device": "host", "config": "t", "git_rev": "x", "core_rev": "n",
            "case": "c", "iters": 10, "avg_ns": 100.0, "med_ns": 90.0,
            "p95_ns": 120.0, "p99_ns": 130.0, "max_ns": 140.0,
            "work_per_s": 1e6, "checksum": "deadbeef"}
    p = run_mod("validate", input_text=json.dumps(good) + "\n")
    assert p.returncode == 0, p.stderr
    assert "validate ok: 1 cases" in p.stdout


def test_validate_rejects_zero_checksum():
    bad = {"device": "host", "config": "t", "git_rev": "x", "core_rev": "n",
           "case": "c", "iters": 10, "avg_ns": 100.0, "med_ns": 90.0,
           "p95_ns": 120.0, "p99_ns": 130.0, "max_ns": 140.0,
           "work_per_s": 1e6, "checksum": "00000000"}
    p = run_mod("validate", input_text=json.dumps(bad) + "\n")
    assert p.returncode != 0


def test_validate_rejects_bad_percentile_order():
    bad = {"device": "host", "config": "t", "git_rev": "x", "core_rev": "n",
           "case": "c", "iters": 10, "avg_ns": 100.0, "med_ns": 200.0,
           "p95_ns": 120.0, "p99_ns": 130.0, "max_ns": 140.0,
           "work_per_s": 1e6, "checksum": "deadbeef"}
    p = run_mod("validate", input_text=json.dumps(bad) + "\n")
    assert p.returncode != 0


def test_compare_detects_regression_and_checksum_change():
    def row(case, avg, checksum="deadbeef"):
        return {"device": "h", "config": "t", "git_rev": "x",
                "core_rev": "n", "case": case, "iters": 10, "avg_ns": avg,
                "med_ns": avg * 0.9, "p95_ns": avg * 1.2,
                "p99_ns": avg * 1.3, "max_ns": avg * 1.4,
                "work_per_s": 1e6, "checksum": checksum}

    with tempfile.TemporaryDirectory() as td:
        base = Path(td) / "base.jsonl"
        new = Path(td) / "new.jsonl"
        base.write_text(json.dumps(row("a", 100.0)) + "\n" +
                        json.dumps(row("b", 100.0)) + "\n")
        new.write_text(json.dumps(row("a", 200.0)) + "\n" +
                       json.dumps(row("b", 100.0, checksum="cafef00d")) + "\n")
        p = subprocess.run([sys.executable, str(ROOT / "tools" / "bench" /
                                                 "compare.py"),
                            str(base), str(new)],
                           capture_output=True, text=True)
        assert p.returncode != 0  # 'a' regressed 2x
        assert "REGRESS" in p.stdout
        assert "CHECKSUM CHANGED" in p.stdout
