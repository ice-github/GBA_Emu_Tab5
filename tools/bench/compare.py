"""Compare two bench JSONL runs case-by-case.

Fails on: differing case sets, checksum change (workload changed, not
comparable), unverified runs, or manifest mismatch (device/config/core_rev)
unless explicitly relaxed. Performance deltas beyond --threshold count as
regressions.
"""
import argparse
import json
import math
import sys


def load(path):
    out = {}
    with open(path) as fh:
        for lineno, line in enumerate(fh, 1):
            line = line.strip()
            if not line:
                continue
            r = json.loads(line)
            if r["case"] in out:
                print(f"{path}:{lineno}: duplicate case {r['case']!r}",
                      file=sys.stderr)
                sys.exit(1)
            out[r["case"]] = r
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("base")
    ap.add_argument("new")
    ap.add_argument("--threshold", type=float, default=0.05)
    ap.add_argument("--allow-device-change", action="store_true")
    ap.add_argument("--allow-config-change", action="store_true")
    ap.add_argument("--allow-core-change", action="store_true")
    args = ap.parse_args()

    base, new = load(args.base), load(args.new)
    if set(base) != set(new):
        print(f"case sets differ:\n  base={sorted(base)}\n"
              f"  new={sorted(new)}", file=sys.stderr)
        return 1
    failures = 0
    for case in sorted(base):
        b, n = base[case], new[case]
        if not b.get("verified") or not n.get("verified"):
            print(f"{case}: UNVERIFIED run, refusing to compare")
            failures += 1
            continue
        for key, flag in (("device", "allow_device_change"),
                          ("config", "allow_config_change"),
                          ("core_rev", "allow_core_change")):
            if b.get(key) != n.get(key) and not getattr(args, flag):
                opt = "--allow-" + key.replace("_", "-") + "-change"
                print(case + ": MANIFEST CHANGED " + key + ": " +
                      repr(b.get(key)) + " -> " + repr(n.get(key)) +
                      " (use " + opt + " to compare anyway)")
                failures += 1
        if b["checksum"] != n["checksum"]:
            print(f"{case}: CHECKSUM CHANGED {b['checksum']} -> "
                  f"{n['checksum']} (workload changed, not comparable)")
            failures += 1
            continue
        for stat in ("avg_ns", "p95_ns", "max_ns"):
            if not (math.isfinite(b[stat]) and math.isfinite(n[stat])):
                print(f"{case} {stat}: non-finite value, refusing")
                failures += 1
                continue
            delta = (n[stat] - b[stat]) / b[stat] if b[stat] else 0.0
            flag = "REGRESS" if delta > args.threshold else "ok"
            if flag == "REGRESS":
                failures += 1
            print(f"{case} {stat}: base={b[stat]:.1f} new={n[stat]:.1f} "
                  f"delta={delta:+.2%} {flag}")
    print(f"failures: {failures}")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
