"""Unit tests for bench tooling (validate, compare). No ROM."""
import json
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def good_row(**kw):
    r = {"device": "host", "config": "t", "git_rev": "x", "core_rev": "n",
         "case": "c", "mem": "dram", "iters": 10, "avg_ns": 100.0,
         "med_ns": 90.0, "p95_ns": 120.0, "p99_ns": 130.0, "max_ns": 140.0,
         "work_per_s": 1e6, "checksum": "deadbeef", "verified": True}
    r.update(kw)
    return r


def run_validate(text):
    p = subprocess.run([sys.executable, "-m", "validate"],
                       input=text, capture_output=True, text=True,
                       cwd=ROOT / "tools" / "bench")
    return p


def test_validate_accepts_good_run():
    p = run_validate(json.dumps(good_row()) + "\n")
    assert p.returncode == 0, p.stderr
    assert "validate ok: 1 cases" in p.stdout


def test_validate_accepts_zero_checksum_when_verified():
    p = run_validate(json.dumps(good_row(checksum="00000000")) + "\n")
    assert p.returncode == 0, p.stderr


def test_validate_rejects_unverified():
    p = run_validate(json.dumps(good_row(verified=False)) + "\n")
    assert p.returncode != 0


def test_validate_rejects_duplicate_case():
    row = json.dumps(good_row()) + "\n"
    p = run_validate(row + row)
    assert p.returncode != 0


def test_validate_rejects_nan():
    p = run_validate(json.dumps(good_row(avg_ns=float("nan"))) + "\n")
    assert p.returncode != 0


def test_validate_rejects_bad_percentile_order():
    p = run_validate(json.dumps(good_row(med_ns=200.0)) + "\n")
    assert p.returncode != 0


def test_validate_rejects_missing_key():
    r = good_row()
    del r["mem"]
    p = run_validate(json.dumps(r) + "\n")
    assert p.returncode != 0


def test_compare_detects_regression_checksum_and_manifest():
    def row(case, avg, checksum="deadbeef", config="t"):
        return good_row(case=case, avg_ns=avg, med_ns=avg * 0.9,
                        p95_ns=avg * 1.2, p99_ns=avg * 1.3,
                        max_ns=avg * 1.4, checksum=checksum, config=config)

    with tempfile.TemporaryDirectory() as td:
        base = Path(td) / "base.jsonl"
        new = Path(td) / "new.jsonl"
        base.write_text("\n".join(json.dumps(r) for r in
                                  [row("a", 100.0), row("b", 100.0),
                                   row("c", 100.0)]) + "\n")
        new.write_text("\n".join(json.dumps(r) for r in
                                 [row("a", 200.0),
                                  row("b", 100.0, checksum="cafef00d"),
                                  row("c", 100.0, config="other")]) + "\n")
        p = subprocess.run([sys.executable, str(ROOT / "tools" / "bench" /
                                                 "compare.py"),
                            str(base), str(new)],
                           capture_output=True, text=True)
        assert p.returncode != 0  # regress + checksum + manifest
        assert "REGRESS" in p.stdout
        assert "CHECKSUM CHANGED" in p.stdout
        assert "MANIFEST CHANGED" in p.stdout


def test_compare_relaxed_manifest_allows_config_change():
    def row(case, config):
        return good_row(case=case, config=config)

    with tempfile.TemporaryDirectory() as td:
        base = Path(td) / "base.jsonl"
        new = Path(td) / "new.jsonl"
        base.write_text(json.dumps(row("a", "t")) + "\n")
        new.write_text(json.dumps(row("a", "t2")) + "\n")
        p = subprocess.run([sys.executable, str(ROOT / "tools" / "bench" /
                                                 "compare.py"),
                            str(base), str(new),
                            "--allow-config-change"],
                           capture_output=True, text=True)
        assert p.returncode == 0, p.stdout + p.stderr
