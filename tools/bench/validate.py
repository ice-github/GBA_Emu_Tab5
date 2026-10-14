"""Validate bench JSONL: schema, sanity, and no silent success on bad runs."""
import json
import sys

REQUIRED = ["device", "config", "git_rev", "core_rev", "case", "iters",
            "avg_ns", "med_ns", "p95_ns", "p99_ns", "max_ns", "work_per_s",
            "checksum"]


def main(path=None):
    fh = open(path) if path else sys.stdin
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
        if r["iters"] < 1:
            print(f"line {lineno}: iters<1", file=sys.stderr)
            return 1
        if not (r["avg_ns"] > 0 and r["max_ns"] >= r["med_ns"] >= 0):
            print(f"line {lineno}: bad timing stats", file=sys.stderr)
            return 1
        if not (r["p95_ns"] >= r["med_ns"] and r["max_ns"] >= r["p95_ns"]):
            print(f"line {lineno}: bad percentile order", file=sys.stderr)
            return 1
        if r["checksum"] == "00000000":
            print(f"line {lineno}: zero checksum = dead code?", file=sys.stderr)
            return 1
        if r["work_per_s"] <= 0:
            print(f"line {lineno}: non-positive throughput", file=sys.stderr)
            return 1
        n += 1
    if n == 0:
        print("no results", file=sys.stderr)
        return 1
    print(f"validate ok: {n} cases")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1] if len(sys.argv) > 1 else None))
