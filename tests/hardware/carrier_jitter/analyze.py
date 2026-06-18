#!/usr/bin/env python3
"""Plot the carrier / mark-width jitter sweep CSV produced by
test_carrier_jitter.py.

Produces two PNGs next to the CSV (one set per carrier frequency found):
  jitter_vs_mark_<hz>.png  - jitter vs mark width, one line per duty (the
                             carrier-period oscillation); dashed vertical lines
                             mark integer-cycle widths.
  jitter_vs_duty_<hz>.png  - jitter vs duty, one line at each duty's best mark
                             width and one at the fixed 560 us NEC width.

Pass more than one CSV to also emit an overlay comparing carrier frequencies:
  compare_carriers.png - jitter vs mark width at a couple of low duties, one
                         line per carrier (shows the effect is sharpest at the
                         TSOP's tuned frequency).

Run (matplotlib is not a project dependency):
  cd tests
  uv run --with matplotlib python hardware/carrier_jitter/analyze.py \
      [hardware/carrier_jitter/data/sweep_38k.csv ...]
"""
import csv
import os
import sys
from collections import defaultdict

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt

METRIC = "mark_sd_us"  # jitter metric to plot
REF_MARK = 560         # NEC standard bit-mark width for the reference line


def load(path):
    rows = []
    with open(path, encoding="utf-8") as f:
        for r in csv.DictReader(f):
            rows.append({
                "hz": int(r["carrier_hz"]),
                "duty": float(r["duty_pct"]),
                "mark": int(r["mark_us"]),
                "sd": float(r[METRIC]),
            })
    return rows


def plot_vs_mark(rows, hz, out):
    period = 1_000_000.0 / hz
    by_duty = defaultdict(list)
    for r in rows:
        by_duty[r["duty"]].append((r["mark"], r["sd"]))
    marks = sorted({r["mark"] for r in rows})
    lo, hi = marks[0], marks[-1]

    fig, ax = plt.subplots(figsize=(10, 6))
    for duty in sorted(by_duty):
        pts = sorted(by_duty[duty])
        ax.plot([m for m, _ in pts], [s for _, s in pts],
                marker="o", ms=3, label=f"{duty:g}%")
    # Integer-cycle mark widths (where the mark ends on a carrier-cycle boundary).
    k = int(lo / period)
    while k * period <= hi:
        x = k * period
        if x >= lo:
            ax.axvline(x, color="gray", ls="--", lw=0.8, alpha=0.6)
            ax.text(x, ax.get_ylim()[1], f"{k}c", color="gray",
                    fontsize=8, va="top", ha="center")
        k += 1
    ax.set_xlabel("mark width (us)")
    ax.set_ylabel(f"jitter {METRIC} (us)")
    ax.set_title(f"TX jitter vs mark width @ {hz/1000:g} kHz "
                 f"(period {period:.2f} us); dashed = integer carrier cycles")
    ax.legend(title="carrier duty", ncol=2)
    ax.grid(True, alpha=0.3)
    fig.tight_layout()
    fig.savefig(out, dpi=130)
    plt.close(fig)
    print(f"wrote {out}")


def plot_vs_duty(rows, hz, out):
    by_duty = defaultdict(list)
    for r in rows:
        by_duty[r["duty"]].append((r["mark"], r["sd"]))
    duties = sorted(by_duty)
    best = []
    ref = []
    for duty in duties:
        pts = by_duty[duty]
        best.append(min(s for _, s in pts))
        at_ref = [s for m, s in pts if m == REF_MARK]
        ref.append(at_ref[0] if at_ref else float("nan"))

    fig, ax = plt.subplots(figsize=(9, 6))
    ax.plot(duties, best, marker="o", label="best mark width")
    ax.plot(duties, ref, marker="s", label=f"{REF_MARK} us (NEC)")
    ax.set_xlabel("carrier duty (%)")
    ax.set_ylabel(f"jitter {METRIC} (us)")
    ax.set_title(f"TX jitter vs carrier duty @ {hz/1000:g} kHz")
    ax.legend()
    ax.grid(True, alpha=0.3)
    fig.tight_layout()
    fig.savefig(out, dpi=130)
    plt.close(fig)
    print(f"wrote {out}")


def plot_compare(rows, duties, out):
    """Overlay jitter vs mark width across carrier frequencies, one panel per
    duty. Dotted verticals mark each frequency's integer-cycle widths."""
    hzs = sorted({r["hz"] for r in rows})
    colors = {hz: f"C{i}" for i, hz in enumerate(hzs)}
    by = defaultdict(lambda: defaultdict(dict))  # hz -> duty -> mark -> sd
    for r in rows:
        by[r["hz"]][r["duty"]][r["mark"]] = r["sd"]
    marks = sorted({r["mark"] for r in rows})
    lo, hi = marks[0], marks[-1]

    fig, axes = plt.subplots(len(duties), 1, figsize=(11, 4.5 * len(duties)),
                             sharex=True, squeeze=False)
    for ax, duty in zip(axes[:, 0], duties):
        for hz in hzs:
            pts = sorted(by[hz].get(duty, {}).items())
            if not pts:
                continue
            ax.plot([m for m, _ in pts], [s for _, s in pts],
                    "-o", ms=2.5, color=colors[hz], label=f"{hz/1000:g} kHz")
            period = 1_000_000.0 / hz
            k = int(lo / period)
            while k * period <= hi:
                if k * period >= lo:
                    ax.axvline(k * period, color=colors[hz], ls=":", lw=0.8, alpha=0.5)
                k += 1
        ax.set_title(f"duty {duty:g}%  (dotted = each frequency's integer carrier cycles)")
        ax.set_ylabel(f"jitter {METRIC} (us)")
        ax.grid(True, alpha=0.3)
        ax.legend(title="carrier")
    axes[-1, 0].set_xlabel("mark width (us)")
    fig.tight_layout()
    fig.savefig(out, dpi=130)
    plt.close(fig)
    print(f"wrote {out}")


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    paths = [a for a in sys.argv[1:] if not a.startswith("--")]
    if not paths:
        paths = [os.path.join(here, "data", "sweep_38k.csv")]
    rows = []
    for p in paths:
        rows.extend(load(p))
    out_dir = os.path.dirname(os.path.abspath(paths[0]))
    hzs = sorted({r["hz"] for r in rows})
    for hz in hzs:
        sub = [r for r in rows if r["hz"] == hz]
        plot_vs_mark(sub, hz, os.path.join(out_dir, f"jitter_vs_mark_{hz}.png"))
        plot_vs_duty(sub, hz, os.path.join(out_dir, f"jitter_vs_duty_{hz}.png"))
    if len(hzs) > 1:
        all_duties = sorted(set.intersection(*({r["duty"] for r in rows if r["hz"] == hz}
                                               for hz in hzs)))
        duties = [d for d in (15.0, 20.0) if d in all_duties] or all_duties[:2]
        plot_compare(rows, duties, os.path.join(out_dir, "compare_carriers.png"))


if __name__ == "__main__":
    main()
