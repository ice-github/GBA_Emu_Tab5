"""Validate bench JSONL: schema, sanity, determinism, finite stats.

Rejects: missing keys, iters<1, non-finite or misordered stats,
non-positive throughput, verified!=true, duplicate case ids, empty input.
A zero checksum is NOT rejected on its own: legit workloads can hash to
zero. Determinism is proven by the harness "verified" flag instead.
"""
import json
import math
import sys

REQUIRED = ["device", "config", "git_rev", "core_rev", "case", "mem",
            "iters", "avg_ns", "med_ns", "p95_ns", "p99_ns", "max_ns",
            "work_per_s", "checksum", "verified"]


def finite(x):
    return isinstance(x, (int, float)) and math.isfinite(x)


def main(path=None):
    fh = open(path) if path else sys.stdin
    seen = set()
    n = 0
    for lineno, line in enumerate(fh, 1):
        line = line.strip()
        if not line:
            continue
        try:
            r = json.loads(line)
        except json.JSONDecodeError as e:
            print(f"line {lineno}: invalid JSON: {e}", file=sys.stderr)
            return 1
        for k in REQUIRED:
            if k not in r:
                print(f"line {lineno}: missing key {k}", file=sys.stderr)
                return 1
        case = r["case"]
        if case in seen:
            print(f"line {lineno}: duplicate case {case!r}", file=sys.stderr)
            return 1
        seen.add(case)
        if not isinstance(r["iters"], int) or r["iters"] < 1:
            print(f"line {lineno}: bad iters", file=sys.stderr)
            return 1
        for k in ("avg_ns", "med_ns", "p95_ns", "p99_ns", "max_ns",
                  "work_per_s"):
            if not finite(r[k]):
                print(f"line {lineno}: non-finite {k}", file=sys.stderr)
                return 1
        if not (r["avg_ns"] > 0 and r["max_ns"] >= r["p99_ns"] >=
                r["p95_ns"] >= r["med_ns"] >= 0):
            print(f"line {lineno}: bad stat order", file=sys.stderr)
            return 1
        if r["work_per_s"] <= 0:
            print(f"line {lineno}: non-positive throughput", file=sys.stderr)
            return 1
        if r["verified"] is not True:
            print(f"line {lineno}: not verified (nondeterministic?)",
                  file=sys.stderr)
            return 1
        n += 1
    if n == 0:
        print("no results", file=sys.stderr)
        return 1
    print(f"validate ok: {n} cases")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1] if len(sys.argv) > 1 else None))
