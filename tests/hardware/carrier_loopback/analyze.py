#!/usr/bin/env python3
"""Analyze carrier_loopback captures.

Parses CARRIER_RAW lines (raw 1 us RMT capture of the carrier-modulated marks,
TSOP removed) and reports, per mark:
  - carrier period stability (mean/sd of high+low) -> is the period clean, or
    dithered by the 10 us TX resolution?
  - carrier high / low pulse widths (duty as emitted)
  - cycles per mark, and how the cycle count / mark-end phase varies mark-to-mark
    -> does the carrier free-run (phase drift) across marks?

Importable (used by test_carrier_loopback.py) and runnable on a saved dump:
    uv run python hardware/carrier_loopback/analyze.py <dump.txt> [--plot out.png]
"""
import re
import statistics
import sys

LINE = re.compile(
    r"CARRIER_RAW seq=(?P<seq>\d+) lvl0=(?P<lvl0>[01]) len=(?P<len>\d+) us=(?P<us>[0-9,]+)"
)

SPACE_MIN_US = 100   # a low longer than this ends a mark (it is an inter-mark space)
CARRIER_HIGH_MAX_US = 40  # a carrier-on pulse is short; longer "high" is anomalous


def parse_line(line: str):
    """Return (seq, [(level, dur_us), ...]) or None."""
    m = LINE.search(line)
    if not m:
        return None
    durs = [int(x) for x in m.group("us").split(",") if x != ""]
    lvl = int(m.group("lvl0"))
    pairs = []
    for d in durs:
        pairs.append((lvl, d))
        lvl ^= 1
    return int(m.group("seq")), pairs


def split_marks(pairs):
    """Split a capture into marks: lists of (level,dur) cycle data, breaking on a
    long low (space) and dropping the trailing frame-ender low."""
    marks = []
    cur = []
    for level, dur in pairs:
        if level == 0 and dur >= SPACE_MIN_US:
            if cur:
                marks.append(cur)
                cur = []
            continue
        cur.append((level, dur))
    if cur:
        marks.append(cur)
    return marks


def mark_stats(mark):
    """Per-mark carrier stats. `mark` is a list of (level,dur) with carrier highs
    (level 1) and intra-cycle lows (level 0)."""
    highs = [d for lv, d in mark if lv == 1 and d <= CARRIER_HIGH_MAX_US]
    lows = [d for lv, d in mark if lv == 0 and d <= CARRIER_HIGH_MAX_US]
    n_cycles = len(highs)
    periods = []
    # pair each high with the following low to form a period
    seq = [(lv, d) for lv, d in mark]
    for i in range(len(seq) - 1):
        if seq[i][0] == 1 and seq[i + 1][0] == 0 and seq[i + 1][1] <= CARRIER_HIGH_MAX_US:
            periods.append(seq[i][1] + seq[i + 1][1])
    on_span = sum(d for _, d in mark)  # total carrier-on span (highs + intra lows)
    return {
        "cycles": n_cycles,
        "high_mean": statistics.fmean(highs) if highs else float("nan"),
        "high_sd": statistics.pstdev(highs) if len(highs) > 1 else 0.0,
        "low_mean": statistics.fmean(lows) if lows else float("nan"),
        "period_mean": statistics.fmean(periods) if periods else float("nan"),
        "period_sd": statistics.pstdev(periods) if len(periods) > 1 else 0.0,
        "period_min": min(periods) if periods else float("nan"),
        "period_max": max(periods) if periods else float("nan"),
        "on_span": on_span,
        "first_high": highs[0] if highs else float("nan"),
        "last_high": highs[-1] if highs else float("nan"),
    }


def analyze(text: str):
    """Return (frames, marks, summary dict) for a multi-line dump."""
    frames = [p for p in (parse_line(ln) for ln in text.splitlines()) if p]
    all_marks = []
    for _seq, pairs in frames:
        for mk in split_marks(pairs):
            if len(mk) >= 4:  # ignore stubs
                all_marks.append(mark_stats(mk))
    if not all_marks:
        return frames, all_marks, {}
    all_periods_mean = [m["period_mean"] for m in all_marks if m["period_mean"] == m["period_mean"]]
    cycles = [m["cycles"] for m in all_marks]
    on_spans = [m["on_span"] for m in all_marks]
    period_sds = [m["period_sd"] for m in all_marks if m["period_sd"] == m["period_sd"]]
    summary = {
        "frames": len(frames),
        "marks": len(all_marks),
        "cycles_per_mark": sorted(set(cycles)),
        "cycles_min": min(cycles),
        "cycles_max": max(cycles),
        "cycles_varies": (max(cycles) - min(cycles)),
        "period_mean": statistics.fmean(all_periods_mean) if all_periods_mean else float("nan"),
        "period_sd_within": statistics.fmean(period_sds) if period_sds else float("nan"),
        "on_span_mean": statistics.fmean(on_spans) if on_spans else float("nan"),
        "on_span_sd": statistics.pstdev(on_spans) if len(on_spans) > 1 else 0.0,
    }
    return frames, all_marks, summary


def print_summary(summary, marks):
    if not summary:
        print("  (no marks parsed)")
        return
    print(f"  frames={summary['frames']}  marks={summary['marks']}")
    print(f"  carrier period : mean {summary['period_mean']:.2f} us, "
          f"within-mark sd {summary['period_sd_within']:.2f} us")
    print(f"  cycles/mark    : {summary['cycles_per_mark']} "
          f"(spread {summary['cycles_varies']})")
    print(f"  carrier-on span: mean {summary['on_span_mean']:.1f} us, "
          f"sd {summary['on_span_sd']:.2f} us  <- mark-to-mark jitter")
    # Interpretation hints
    if summary["cycles_varies"] >= 1 or summary["on_span_sd"] > 3:
        print("  => mark-to-mark variation present: consistent with a FREE-RUNNING "
              "carrier (phase drifts across marks).")
    if summary["period_sd_within"] > 2:
        print("  => carrier period is not clean within a mark: consistent with TX "
              "RESOLUTION dithering (10 us tick can't divide the carrier evenly).")


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 1
    text = open(sys.argv[1]).read()
    frames, marks, summary = analyze(text)
    print(f"Analyzed {sys.argv[1]}")
    print_summary(summary, marks)
    if "--plot" in sys.argv:
        out = sys.argv[sys.argv.index("--plot") + 1]
        try:
            import matplotlib
            matplotlib.use("Agg")
            import matplotlib.pyplot as plt
        except ImportError:
            print("matplotlib not available; rerun with: uv run --with matplotlib ...")
            return 0
        cyc = [m["cycles"] for m in marks]
        span = [m["on_span"] for m in marks]
        fig, ax = plt.subplots(2, 1, figsize=(8, 6))
        ax[0].plot(cyc, "o-"); ax[0].set_title("carrier cycles per mark"); ax[0].set_xlabel("mark #")
        ax[1].plot(span, "o-"); ax[1].set_title("carrier-on span per mark (us)"); ax[1].set_xlabel("mark #")
        fig.tight_layout(); fig.savefig(out); print(f"wrote {out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
