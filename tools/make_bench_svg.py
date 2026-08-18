#!/usr/bin/env python3
"""Regenerate docs/bench-light.svg and docs/bench-dark.svg from benchmark numbers.

Run `make bench-vs`, put the medians in RESULTS below, then run this script.
Two files rather than one with a media query, because that is what GitHub's
<picture> element needs to switch themes reliably.

    python3 tools/make_bench_svg.py
"""

import os

# workload label, csv.h MB/s, libcsv MB/s   (medians of five runs)
RESULTS = [
    ("short fields  ~6 B", 415, 159),
    ("quoted + escapes",   391, 188),
    ("long fields ~220 B", 663, 154),
]

SUBTITLE = "64 MB document, gcc 13 -O2, one core of a 2.8 GHz Xeon"

THEME = {
    "light": dict(surface="#fcfcfb", primary="#0b0b0b", secondary="#52514e",
                  muted="#84837d", rule="#e6e5e1", s1="#2a78d6", s2="#eb6834"),
    "dark":  dict(surface="#1a1a19", primary="#ffffff", secondary="#c3c2b7",
                  muted="#8f8e86", rule="#33332f", s1="#3987e5", s2="#d95926"),
}

W, H = 760, 300
LEFT, RIGHT = 176, 96          # label gutter, value gutter
TOP = 78
BAR_H, BAR_GAP, GROUP_GAP = 20, 4, 24
FONT = ('-apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, '
        "Helvetica, Arial, sans-serif")


def esc(s):
    return s.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")


def svg(mode):
    t = THEME[mode]
    plot_w = W - LEFT - RIGHT
    vmax = max(max(a, b) for _, a, b in RESULTS)
    scale = plot_w / (vmax * 1.06)

    o = []
    add = o.append
    add(f'<svg xmlns="http://www.w3.org/2000/svg" width="{W}" height="{H}" '
        f'viewBox="0 0 {W} {H}" role="img" '
        f'aria-label="Throughput of csv.h versus libcsv across three workloads">')
    add(f'<style>text{{font-family:{FONT};}}</style>')
    add(f'<rect width="{W}" height="{H}" rx="8" fill="{t["surface"]}"/>')

    # title + subtitle
    add(f'<text x="28" y="36" font-size="16" font-weight="600" '
        f'fill="{t["primary"]}">Throughput vs libcsv 3.0.3</text>')
    add(f'<text x="28" y="56" font-size="12" fill="{t["muted"]}">'
        f'{esc(SUBTITLE)}</text>')

    # legend
    lx = W - 28
    for label, color in (("libcsv", t["s2"]), ("csv.h", t["s1"])):
        tw = len(label) * 6.6
        lx -= tw
        add(f'<text x="{lx:.0f}" y="36" font-size="12" fill="{t["secondary"]}">'
            f'{label}</text>')
        lx -= 8 + 10
        add(f'<rect x="{lx:.0f}" y="26" width="10" height="10" rx="2" '
            f'fill="{color}"/>')
        lx -= 16

    y = TOP
    for label, ours, theirs in RESULTS:
        top = y
        for value, color in ((ours, t["s1"]), (theirs, t["s2"])):
            w = max(value * scale, 6)
            # 4px rounded data-end, square against the baseline
            add(f'<path d="M{LEFT} {y}h{w - 4:.1f}a4 4 0 0 1 4 4v{BAR_H - 8}'
                f'a4 4 0 0 1-4 4H{LEFT}z" fill="{color}"/>')
            add(f'<text x="{LEFT + w + 10:.1f}" y="{y + BAR_H - 6}" '
                f'font-size="12" fill="{t["secondary"]}">{value} MB/s</text>')
            y += BAR_H + BAR_GAP

        # one line per bar in the gutter: what it is, then how much faster
        add(f'<text x="{LEFT - 16}" y="{top + BAR_H - 6}" font-size="12.5" '
            f'text-anchor="end" fill="{t["secondary"]}">{esc(label)}</text>')
        add(f'<text x="{LEFT - 16}" y="{top + 2 * BAR_H + BAR_GAP - 6}" '
            f'font-size="11.5" text-anchor="end" fill="{t["muted"]}">'
            f'{ours / float(theirs):.1f}× slower</text>')
        y += GROUP_GAP - BAR_GAP

    # baseline
    add(f'<line x1="{LEFT}" y1="{TOP - 8}" x2="{LEFT}" y2="{y - GROUP_GAP + 4}" '
        f'stroke="{t["rule"]}" stroke-width="1"/>')
    add(f'<text x="28" y="{H - 16}" font-size="11" fill="{t["muted"]}">'
        f'Higher is better. Both parsers walk every field byte; field counts and '
        f'checksums match exactly.</text>')
    add("</svg>")
    return "\n".join(o) + "\n"


def main():
    out = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "docs")
    os.makedirs(out, exist_ok=True)
    for mode in ("light", "dark"):
        path = os.path.join(out, "bench-%s.svg" % mode)
        with open(path, "w") as f:
            f.write(svg(mode))
        print("wrote", os.path.normpath(path))


if __name__ == "__main__":
    main()
