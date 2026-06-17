#!/usr/bin/env python3
"""Analyze TX timing jitter from RMT capture logs (RX_JITTER lines).

Works for both hardware/tx_jitter_loopback/ and hardware/tx_jitter/ since both
print `RX_JITTER seq=.. len=.. us=d0,d1,..` (per-edge durations in microseconds).

Usage:
    # Auto: newest /tmp/pytest-embedded run, every *jitter* dut.log
    python analyze.py

    # Specific results dir(s) or dut.log file(s)
    python analyze.py /tmp/pytest-embedded/2026-..-../test_pulsekit_loopback_jitter
    python analyze.py path/to/dut.log

    # Show more of the worst edges
    python analyze.py --worst 10

Per-edge statistics across all captured frames (a correct frame has a fixed
edge count; only durations vary):
    mean_sd  - average per-edge standard deviation  (typical jitter)
    max_sd   - worst per-edge standard deviation
    max_ptp  - worst per-edge peak-to-peak (max-min) (catches rare spikes)
A value of 0 means perfectly deterministic timing (e.g. RMT hardware DMA).
Software / delayMicroseconds transmitters show occasional large max_ptp from
interrupt preemption.
"""
import argparse
import glob
import os
import re
import statistics
import sys
from collections import Counter

RX_JITTER = re.compile(r"RX_JITTER seq=(\d+) len=(\d+) us=([0-9,]+)")
PYTEST_EMBEDDED_BASE = "/tmp/pytest-embedded"


def load_frames(log_path):
    """Return (clean_frames, corrupted). A line is corrupted when the declared
    len= does not match the actual number of us= values (serial truncation)."""
    frames = []
    corrupted = 0
    with open(log_path) as f:
        for m in RX_JITTER.finditer(f.read()):
            declared = int(m.group(2))
            us = [int(x) for x in m.group(3).split(",") if x]
            if len(us) != declared:
                corrupted += 1
                continue
            frames.append(us)
    return frames, corrupted


def analyze(frames):
    if not frames:
        return None
    common_len, _ = Counter(len(f) for f in frames).most_common(1)[0]
    aligned = [f for f in frames if len(f) == common_len]
    sd, ptp, means = [], [], []
    for i in range(common_len):
        col = [f[i] for f in aligned]
        sd.append(statistics.pstdev(col) if len(col) > 1 else 0.0)
        ptp.append(max(col) - min(col))
        means.append(statistics.mean(col))
    return {
        "frames": len(frames),
        "used": len(aligned),
        "edges": common_len,
        "sd": sd,
        "ptp": ptp,
        "means": means,
    }


def find_logs(target):
    """Resolve a CLI arg to a list of (label, dut.log) pairs."""
    if os.path.isfile(target):
        return [(os.path.basename(os.path.dirname(target)) or target, target)]
    if os.path.isdir(target):
        logs = sorted(glob.glob(os.path.join(target, "**", "dut.log"), recursive=True))
        if not logs:  # the dir itself may be the test dir
            direct = os.path.join(target, "dut.log")
            logs = [direct] if os.path.isfile(direct) else []
        return [(os.path.basename(os.path.dirname(p)), p) for p in logs]
    return []


def discover_latest():
    runs = sorted(glob.glob(os.path.join(PYTEST_EMBEDDED_BASE, "20*")), reverse=True)
    for run in runs:
        logs = sorted(glob.glob(os.path.join(run, "*jitter*", "dut.log")))
        if logs:
            print(f"# auto-detected run: {run}")
            return [(os.path.basename(os.path.dirname(p)), p) for p in logs]
    return []


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("targets", nargs="*", help="results dir(s) or dut.log file(s)")
    ap.add_argument("--worst", type=int, default=5, help="worst edges to list (default 5)")
    args = ap.parse_args()

    pairs = []
    if args.targets:
        for t in args.targets:
            pairs.extend(find_logs(t))
    else:
        pairs = discover_latest()

    if not pairs:
        print("No RX_JITTER logs found.", file=sys.stderr)
        return 1

    print(f"{'capture':40} {'clean':>5} {'corrupt':>7} {'used':>5} {'edges':>5} "
          f"{'mean_sd':>8} {'max_sd':>8} {'max_ptp':>8}")
    results = []
    for label, log in pairs:
        frames, corrupted = load_frames(log)
        r = analyze(frames)
        if r is None:
            print(f"{label:40} {'0':>5} {corrupted:>7} <no clean RX_JITTER frames>")
            continue
        results.append((label, r))
        print(f"{label:40} {r['frames']:>5} {corrupted:>7} {r['used']:>5} {r['edges']:>5} "
              f"{statistics.mean(r['sd']):>8.2f} {max(r['sd']):>8.2f} {max(r['ptp']):>8}")

    if args.worst > 0:
        for label, r in results:
            order = sorted(range(r["edges"]), key=lambda i: -r["sd"][i])[: args.worst]
            print(f"\n# {label}: worst {args.worst} edges (idx: sd_us, ptp_us, mean_us)")
            for i in order:
                print(f"   edge[{i:3}] sd={r['sd'][i]:7.2f} ptp={r['ptp'][i]:6} "
                      f"mean={r['means'][i]:8.1f}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
