"""Compare two bench JSONL runs case-by-case; exit nonzero on regression."""
import json
import sys


def load(path):
    out = {}
    with open(path) as fh:
        for line in fh:
            line = line.strip()
            if line:
                r = json.loads(line)
                out[r["case"]] = r
    return out


def main(base_path, new_path, thresh=0.05):
    base, new = load(base_path), load(new_path)
    if set(base) != set(new):
        print(f"case sets differ: base={sorted(base)} new={sorted(new)}",
              file=sys.stderr)
        return 1
    regressions = 0
    for case in sorted(base):
        b, n = base[case], new[case]
        if b["checksum"] != n["checksum"]:
            print(f"{case}: CHECKSUM CHANGED {b['checksum']} -> "
                  f"{n['checksum']} (workload changed, not comparable)")
            continue
        for stat in ("avg_ns", "p95_ns", "max_ns"):
            delta = (n[stat] - b[stat]) / b[stat] if b[stat] else 0.0
            flag = "REGRESS" if delta > thresh else "ok"
            if flag == "REGRESS":
                regressions += 1
            print(f"{case} {stat}: base={b[stat]:.1f} new={n[stat]:.1f} "
                  f"delta={delta:+.2%} {flag}")
    print(f"regressions(>{thresh:.0%} on avg/p95/max): {regressions}")
    return 1 if regressions else 0


if __name__ == "__main__":
    if len(sys.argv) < 3:
        print("usage: compare.py base.jsonl new.jsonl [threshold]",
              file=sys.stderr)
        sys.exit(2)
    sys.exit(main(sys.argv[1], sys.argv[2],
                  float(sys.argv[3]) if len(sys.argv) > 3 else 0.05))
