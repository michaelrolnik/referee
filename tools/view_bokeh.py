#!/usr/bin/env python3
"""
Draw a referee run trace as an interactive timing diagram.

    python3 tools/view_bokeh.py run.ndjson -o run.html

Needs bokeh and pandas. tools/view.py draws the same file with no
dependencies at all; this one adds hover, zoom and linked panning, which is
what makes a long trace explorable rather than merely visible.

Both read docs/run-trace-format.md. The format is the contract and the viewer
is replaceable -- that is the whole reason it was written down first.
"""
import argparse, json, sys

import pandas as pd
from bokeh.layouts import column
from bokeh.models import ColumnDataSource, CustomJS, HoverTool, Range1d, Select
from bokeh.plotting import figure, output_file, save

TRUE, FALSE, ABSENT, OTHER = "#33aa77", "#f2f2f2", "#ffffff", "#dbe8f5"


def densify(row, n):
    """Sparse and dense become one list per state, so drawing sees one shape."""
    vals = row.get("values") or []
    if row.get("encoding") != "sparse":
        return vals + [None] * (n - len(vals))

    out, at = [None] * n, row["at"]
    for i, start in enumerate(at):
        end = at[i + 1] if i + 1 < len(at) else n
        for s in range(start, min(end, n)):
            out[s] = vals[i]
    return out


def colour(v):
    #  null is *not evaluated*, and must not look like false: conflating them
    #  is how a vacuous requirement comes to look satisfied.
    if v is None:   return ABSENT
    if v is True:   return TRUE
    if v is False:  return FALSE
    return OTHER


def strip(title, labels, columns, times, witnesses=None):
    """One horizontal band per row, sharing the trace's time axis."""
    n = len(times)
    step = (times[-1] - times[0]) / max(n - 1, 1) if n > 1 else 1

    xs, ys, cs, vs, ls = [], [], [], [], []
    for r, (label, col) in enumerate(zip(labels, columns)):
        for s, v in enumerate(col):
            xs.append(times[s] + step / 2)
            ys.append(len(labels) - 1 - r)
            cs.append(colour(v))
            vs.append("—" if v is None else str(v))
            ls.append(label)

    src = ColumnDataSource(dict(x=xs, y=ys, colour=cs, value=vs, row=ls,
                                t=[times[s] for _ in labels for s in range(n)]))

    p = figure(title=title, height=40 + 26 * len(labels), width=1000,
               y_range=Range1d(-0.6, len(labels) - 0.4),
               tools="xpan,xwheel_zoom,box_zoom,reset,save",
               active_scroll="xwheel_zoom", toolbar_location="right")

    p.rect(x="x", y="y", width=step, height=0.72, source=src,
           fill_color="colour", line_color="white", line_width=0.5)

    p.add_tools(HoverTool(tooltips=[("row", "@row"), ("t", "@t"), ("value", "@value")]))

    p.yaxis.ticker = list(range(len(labels)))
    p.yaxis.major_label_overrides = {len(labels) - 1 - i: l for i, l in enumerate(labels)}
    p.ygrid.grid_line_color = None
    p.xaxis.axis_label = "time"

    #  A temporal value is a claim about the suffix from a state, not a fact
    #  about it. Drawn as a plain cell it reads exactly like a state formula,
    #  so the span to whatever settled it is the whole point.
    for r, wits in enumerate(witnesses or []):
        if not wits:
            continue
        y = len(labels) - 1 - r
        x0 = [times[s] + step / 2 for s, w in enumerate(wits) if w is not None and w != s]
        x1 = [times[w] + step / 2 for s, w in enumerate(wits) if w is not None and w != s]
        if x0:
            p.segment(x0=x0, y0=[y] * len(x0), x1=x1, y1=[y] * len(x0),
                      line_color="#226688", line_width=1.5, line_alpha=0.75)
    return p


def render(lines, path):
    header  = next(l for l in lines if l["kind"] == "header")
    signals = {s["id"]: s for s in lines if s["kind"] == "signal"}
    reqs    = [l for l in lines if l["kind"] == "requirement"]
    times   = header["states"]["time"]
    n       = len(times)

    panels = []

    if signals:
        panels.append(strip("signals",
                            [s["name"] + (" (computed)" if s.get("computed") else "")
                             for s in signals.values()],
                            [densify(s, n) for s in signals.values()],
                            times))

    for r in reqs:
        note = []
        if r["vacuous"]:
            note.append("VACUOUS: " + r["vacuity"]["reason"])
        if "scope" in r and not r["scope"]["active"]:
            note.append("scope never opened")

        title = f"{r.get('name') or r['where']}   [{r['verdict']}]"
        if note:
            title += "   " + " · ".join(note)

        rows    = r.get("rows", [])
        labels  = [row["label"] for row in rows]
        cols    = [densify(signals[row["ref"]] if "ref" in row else row, n)
                   for row in rows]
        wits    = [row.get("witnesses") for row in rows]
        if not labels:
            continue        #  verdict only: the title carries it

        panels.append(strip(title, labels, cols, times, wits))

    #  Linked panning: every panel shares the trace's time axis, so scrolling
    #  one scrolls all of them. Correlating rows across requirements is most
    #  of why this is worth looking at.
    for p in panels[1:]:
        p.x_range = panels[0].x_range

    #  mode="inline" embeds BokehJS rather than linking a CDN. It costs a few
    #  megabytes and buys the property that matters: the file opens with no
    #  network, so it can be attached to a CI artifact, mailed, or read on a
    #  machine that has never heard of bokeh. A viewer whose output only works
    #  online is a viewer whose output cannot be filed against a bug.
    #  One requirement at a time. Past about three the page is a wall, and
    #  scrolling is the wrong way to find one.
    #
    #  Each requirement panel already holds only the rows that requirement
    #  reads -- the format answers that, since rows carry `ref` into the signal
    #  lines -- so hiding the others hides the irrelevant signals with them.
    #  The all-signals overview shows only in "all" mode, where it is context
    #  rather than noise competing with the requirement in view.
    names   = [r.get("name") or r["where"] for r in reqs]
    reqfrom = 1 if signals else 0

    pick = Select(title="show", value="all", options=["all"] + names, width=440)
    pick.js_on_change("value", CustomJS(
        args=dict(panels=panels, reqfrom=reqfrom, names=names),
        code="""
        const want = this.value;
        for (let i = 0; i < panels.length; i++) {
            if (i < reqfrom) { panels[i].visible = (want === "all"); continue; }
            panels[i].visible = (want === "all") || (names[i - reqfrom] === want);
        }
        """))

    output_file(path, title="referee run", mode="inline")
    save(column(pick, *panels))


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("run")
    ap.add_argument("-o", "--output", default="run.html")
    args = ap.parse_args()

    with open(args.run) as f:
        lines = [json.loads(l) for l in f if l.strip()]

    render(lines, args.output)
    print(f"wrote {args.output}", file=sys.stderr)


if __name__ == "__main__":
    main()
