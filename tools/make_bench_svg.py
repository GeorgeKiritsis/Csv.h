#!/usr/bin/env python3
"""Regenerate docs/bench-light.svg and docs/bench-dark.svg from benchmark numbers.

Run `make bench-vs`, put the medians in RESULTS below, then run this script.
Two files rather than one with a media query, because that is what GitHub's
<picture> element needs to switch themes reliably.

Needs matplotlib:  python3 -m pip install matplotlib

    python3 tools/make_bench_svg.py
"""

import os

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.transforms import blended_transform_factory

# workload label, csv.h MB/s, libcsv MB/s   (medians of five runs)
RESULTS = [
    ("short fields  ~6 B", 415, 159),
    ("quoted + escapes",   391, 188),
    ("long fields ~220 B", 663, 154),
]

TITLE    = "Throughput vs libcsv 3.0.3"
SUBTITLE = "64 MB document, gcc 13 -O2, one core of a 2.8 GHz Xeon"
FOOTNOTE = ("Higher is better. Both parsers walk every field byte; "
            "field counts and checksums match exactly.")

THEME = {
    "light": dict(surface="#fcfcfb", primary="#0b0b0b", secondary="#52514e",
                  muted="#84837d", rule="#e6e5e1", s1="#2a78d6", s2="#eb6834"),
    "dark":  dict(surface="#1a1a19", primary="#ffffff", secondary="#c3c2b7",
                  muted="#8f8e86", rule="#33332f", s1="#3987e5", s2="#d95926"),
}

BAR_H = 0.34   # bar thickness in group units; two bars + gap per group


def render(mode, path):
    t = THEME[mode]
    vmax = max(max(a, b) for _, a, b in RESULTS)

    fig = plt.figure(figsize=(7.6, 3.0), dpi=100)
    fig.patch.set_facecolor(t["surface"])
    ax = fig.add_axes((0.235, 0.17, 0.63, 0.60))
    ax.set_facecolor(t["surface"])

    ys       = list(range(len(RESULTS)))
    ours     = [r[1] for r in RESULTS]
    theirs   = [r[2] for r in RESULTS]
    y_ours   = [y - BAR_H / 2 - 0.02 for y in ys]   # 0.04 group units ~ 2px gap
    y_theirs = [y + BAR_H / 2 + 0.02 for y in ys]

    ax.barh(y_ours,   ours,   height=BAR_H, color=t["s1"], label="csv.h")
    ax.barh(y_theirs, theirs, height=BAR_H, color=t["s2"], label="libcsv")

    # direct value labels at the data end of every bar, in ink not series color
    for y, v in list(zip(y_ours, ours)) + list(zip(y_theirs, theirs)):
        ax.text(v + vmax * 0.02, y, "%d MB/s" % v, va="center",
                fontsize=9.5, color=t["secondary"])

    # left gutter: the workload on the csv.h line, the ratio on the libcsv line
    gutter = blended_transform_factory(ax.transAxes, ax.transData)
    for y, (label, a, b) in zip(ys, RESULTS):
        ax.text(-0.025, y - BAR_H / 2 - 0.02, label, transform=gutter,
                ha="right", va="center", fontsize=10, color=t["secondary"])
        ax.text(-0.025, y + BAR_H / 2 + 0.02, "%.1f× faster" % (a / b),
                transform=gutter, ha="right", va="center", fontsize=9,
                color=t["muted"])

    ax.set_xlim(0, vmax * 1.18)
    ax.set_ylim(len(RESULTS) - 0.5, -0.5)  # first workload on top
    ax.set_xticks([])
    ax.set_yticks([])
    for s in ax.spines.values():
        s.set_visible(False)
    ax.axvline(0, color=t["rule"], linewidth=1)

    fig.text(0.037, 0.895, TITLE, fontsize=13, fontweight="semibold",
             color=t["primary"])
    fig.text(0.037, 0.80, SUBTITLE, fontsize=9.5, color=t["muted"])
    fig.text(0.037, 0.055, FOOTNOTE, fontsize=8.5, color=t["muted"])

    leg = fig.legend(loc="upper right", bbox_to_anchor=(0.965, 0.97),
                     frameon=False, ncol=2, fontsize=9.5,
                     handlelength=1.0, handleheight=1.0, handletextpad=0.5,
                     columnspacing=1.2, labelcolor=t["secondary"])
    for h in leg.legend_handles:
        h.set_height(7)
        h.set_width(7)

    # fonttype="path" renders text as outlines, so the image is identical on
    # every viewer; no Date metadata keeps regeneration diffs clean.
    fig.savefig(path, format="svg", facecolor=t["surface"],
                metadata={"Date": None})
    plt.close(fig)


def main():
    plt.rcParams["svg.fonttype"] = "path"
    out = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "docs")
    os.makedirs(out, exist_ok=True)
    for mode in ("light", "dark"):
        path = os.path.join(out, "bench-%s.svg" % mode)
        render(mode, path)
        print("wrote", os.path.normpath(path))


if __name__ == "__main__":
    main()
