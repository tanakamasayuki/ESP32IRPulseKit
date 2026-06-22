#!/usr/bin/env python3
"""Heatmap of the JVC mark x zero-space sweep.

Reads the CSV from study_jvc_timing_sweep.py and draws two heatmaps
(rows = zero-space us, cols = bit-mark us):
  - margin_p90_us : 595 - p90(received zero-space). Higher (greener) = more
    headroom before IRremoteESP8266 rejects the JVC zero bit. The optimum.
  - pass_ratio    : fraction of frames decoded as JVC.

    uv run --with matplotlib python studies/jvc_timing_sweep/analyze.py \
        studies/jvc_timing_sweep/data/jvc_sweep.csv [out.png]
"""
import csv
import sys


def load(path):
    rows = []
    with open(path) as fh:
        for r in csv.DictReader(fh):
            rows.append({k: (float(v) if "." in v or k.endswith("_us") or k.endswith("ratio")
                             else int(v)) for k, v in r.items()})
    return rows


def grid(rows, field):
    marks = sorted({int(r["mark_us"]) for r in rows})
    zsps = sorted({int(r["zspace_us"]) for r in rows})
    lut = {(int(r["mark_us"]), int(r["zspace_us"])): r[field] for r in rows}
    z = [[lut.get((m, zs), float("nan")) for m in marks] for zs in zsps]
    return marks, zsps, z


def print_table(rows):
    marks, zsps, z = grid(rows, "margin_p90_us")
    print("margin_p90 (595 - p90 zero-space), us  [rows=zspace, cols=mark]")
    print("zsp\\mark " + " ".join(f"{m:>5}" for m in marks))
    for zs, row in zip(zsps, z):
        print(f"{zs:>6}  " + " ".join(f"{v:>5.0f}" if v == v else "   --" for v in row))
    best = max(rows, key=lambda r: (r["pass_ratio"], r["margin_p90_us"]))
    print(f"\nBEST: mark={int(best['mark_us'])} zspace={int(best['zspace_us'])} "
          f"pass_ratio={best['pass_ratio']:.2f} margin={best['margin_p90_us']:+.0f}us")


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 1
    rows = load(sys.argv[1])
    if not rows:
        print("no rows")
        return 1
    print_table(rows)

    out = sys.argv[2] if len(sys.argv) > 2 else sys.argv[1].replace(".csv", ".png")
    try:
        import matplotlib
        matplotlib.use("Agg")
        import matplotlib.pyplot as plt
    except ImportError:
        print("\nmatplotlib not available; rerun with: uv run --with matplotlib ...")
        return 0

    fig, axes = plt.subplots(1, 2, figsize=(13, 5))
    for ax, (field, title, cmap) in zip(
        axes,
        [("margin_p90_us", "margin to 595us (higher=better)", "RdYlGn"),
         ("pass_ratio", "JVC decode pass ratio", "RdYlGn")],
    ):
        marks, zsps, z = grid(rows, field)
        im = ax.imshow(z, origin="lower", aspect="auto", cmap=cmap,
                       extent=[min(marks) - 5, max(marks) + 5, min(zsps) - 5, max(zsps) + 5])
        ax.set_title(title)
        ax.set_xlabel("bit-mark us")
        ax.set_ylabel("zero-space us")
        fig.colorbar(im, ax=ax)
    fig.suptitle("JVC mark x zero-space sweep -> IRremoteESP8266 RX")
    fig.tight_layout()
    fig.savefig(out)
    print(f"wrote {out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
