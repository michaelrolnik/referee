#!/usr/bin/env python3
"""
Draw a referee run trace as a timing diagram.

    python3 tools/view.py run.ndjson -o run.html

No dependencies. That is deliberate: the output is a single self-contained
HTML file with inline SVG, which can be attached to a CI artifact or pasted
into a bug report. A live app cannot be forwarded, and a viewer that needs a
package installed is one more thing between someone and the picture.

A Bokeh version would read exactly the same file. The format is the contract
and the viewer is replaceable -- see docs/run-trace-format.md.
"""
import argparse, html, json, sys

#  Drawn deliberately differently, because they mean different things. A
#  `state` value is a fact about that state; a `temporal` value is a claim
#  about the whole suffix from it. Rendered identically, the second lies.
CSS = """
body   { font: 13px/1.5 ui-monospace, Menlo, Consolas, monospace; margin: 2rem; color: #222; }
h1     { font-size: 1.1rem; margin: 0 0 .3rem; }
h2     { font-size: .95rem; margin: 1.6rem 0 .4rem; font-weight: 600; }
.meta  { color: #666; margin-bottom: 1.4rem; }
.req   { border-left: 3px solid #ddd; padding-left: .8rem; margin-bottom: .4rem; }
.pass  { border-left-color: #3a7; }
.fail  { border-left-color: #c44; }
.vac   { border-left-color: #e90; }
.tag   { font-size: .8rem; padding: .05rem .4rem; border-radius: .2rem; margin-left: .5rem; }
.t-pass{ background: #e6f5ec; color: #256; }
.t-fail{ background: #fbe9e9; color: #922; }
.t-vac { background: #fdf0d5; color: #85520a; }
.why   { color: #85520a; font-size: .85rem; margin: .2rem 0 .5rem; }
svg    { display: block; margin: .2rem 0 .6rem; overflow: visible; }
.lbl   { font-size: 11px; fill: #444; }
.axis  { font-size: 10px; fill: #888; }
select { font: inherit; padding: .25rem; margin-bottom: 1rem; }
.hide  { display: none; }
.sigs  { color: #888; font-size: .8rem; margin: .1rem 0 .4rem; }
"""

#  Only the signals a requirement actually reads. The format already answers
#  this -- rows carry `ref` into the signal lines -- so the relevant subset is
#  exactly the referenced ids, not a guess. Drawing every signal for every
#  requirement would imply they are all relevant, and the ones that are not
#  compete for attention with the ones that are.
JS = """
const sel = document.getElementById('pick');
sel.addEventListener('change', () => {
  const want = sel.value;
  document.querySelectorAll('[data-req]').forEach(el => {
    el.classList.toggle('hide', want !== 'all' && el.dataset.req !== want);
  });
});
"""

TRUE, FALSE, ABSENT = "#3a7", "#f2f2f2", "#fff"


def dense(row, n):
    """Both encodings become one value per state, so drawing sees one shape."""
    if row.get("encoding") != "sparse":
        return (row.get("values") or []) + [None] * (n - len(row.get("values") or []))

    out, at, vals = [None] * n, row["at"], row["values"]
    for i, start in enumerate(at):
        end = at[i + 1] if i + 1 < len(at) else n
        for s in range(start, min(end, n)):
            out[s] = vals[i]
    return out


def cell_colour(v):
    if v is None:      return ABSENT      #  not evaluated -- not the same as false
    if v is True:      return TRUE
    if v is False:     return FALSE
    return "#dbe8f5"                       #  a number or string: present, not a verdict


def svg_row(values, witnesses, n, w, y, h):
    """One row. A temporal value draws an arc to whatever settled it."""
    step, out = w / max(n, 1), []

    for s, v in enumerate(values):
        x = s * step
        out.append(f'<rect x="{x:.1f}" y="{y}" width="{step:.1f}" height="{h}" '
                   f'fill="{cell_colour(v)}" stroke="#fff" stroke-width=".5"/>')
        if v is not None and not isinstance(v, bool):
            out.append(f'<text x="{x + step/2:.1f}" y="{y + h - 3}" text-anchor="middle" '
                       f'class="axis">{html.escape(str(v))}</text>')

    for s, wit in enumerate(witnesses or []):
        if wit is None or wit == s:
            continue
        x0, x1 = s * step + step / 2, wit * step + step / 2
        out.append(f'<path d="M{x0:.1f},{y + h/2} L{x1:.1f},{y + h/2}" stroke="#268" '
                   f'stroke-width="1.2" marker-end="url(#a)" opacity=".75"/>')
    return "".join(out)


def render(lines, out):
    header  = next(l for l in lines if l["kind"] == "header")
    signals = {s["id"]: s for s in lines if l_kind(s)}
    reqs    = [l for l in lines if l["kind"] == "requirement"]
    times   = header["states"]["time"]
    n       = len(times)
    W, H    = 900, 18

    p = [f"<!doctype html><meta charset=utf-8><title>referee run</title><style>{CSS}</style>",
         '<svg width="0" height="0"><defs><marker id="a" markerWidth="6" markerHeight="6" '
         'refX="5" refY="3" orient="auto"><path d="M0,0 L6,3 L0,6 z" fill="#268"/></marker>'
         "</defs></svg>",
         f"<h1>{html.escape(header['specification']['path'])}</h1>",
         f"<div class=meta>{html.escape(header['trace']['path'])} &middot; {n} states "
         f"&middot; {len(reqs)} requirements</div>"]

    if signals:
        p.append("<h2>signals</h2>")
        for s in signals.values():
            vals = dense(s, n)
            mark = " (computed)" if s.get("computed") else ""
            p.append(f'<div class=lbl>{html.escape(s["name"])}{mark}</div>'
                     f'<svg width="{W}" height="{H}">{svg_row(vals, None, n, W, 0, H)}</svg>')

    p.append("<h2>requirements</h2>")

    #  One at a time by default: with more than a handful the page is a wall,
    #  and scrolling is the wrong way to find one.
    opts = "".join(f'<option value="{i}">{html.escape(r.get("name") or r["where"])}'
                   f'{" — vacuous" if r["vacuous"] else ""}</option>'
                   for i, r in enumerate(reqs))
    p.append(f'<select id=pick><option value="all">all {len(reqs)} requirements</option>{opts}</select>')

    for ri, r in enumerate(reqs):
        cls = "vac" if r["vacuous"] else ("pass" if r["verdict"] == "pass" else "fail")
        tag = ('<span class="tag t-vac">vacuous</span>' if r["vacuous"] else
               f'<span class="tag t-{r["verdict"]}">{r["verdict"]}</span>')
        p.append(f'<div class="req {cls}" data-req="{ri}">'
                 f'<b>{html.escape(r.get("name") or r["where"])}</b>{tag}')

        #  What a requirement reads is itself information: one touching fewer
        #  signals than expected is worth noticing.
        rows  = r.get("rows", [])
        reads = [signals[row["ref"]]["name"] for row in rows if "ref" in row]
        if reads:
            p.append(f'<div class=sigs>reads: {html.escape(", ".join(reads))}</div>')

        if r["vacuous"]:
            v = r["vacuity"]
            p.append(f'<div class=why>{html.escape(v["reason"])}'
                     + (f' &mdash; {html.escape(v["detail"])}' if v.get("detail") else "") + "</div>")

        #  Absent scope is not an empty scope: one is "not known", the
        #  other is "never opened", and only the second is vacuity.
        if "scope" in r and not r["scope"]["active"]:
            p.append('<div class=why>scope never opened &mdash; nothing was checked</div>')

        if not rows:
            rows = [{"id": f"v{ri}", "label": f"verdict ({r['verdict']})",
                     "kind": "state", "type": "boolean",
                     "values": [r["verdict"] == "pass"] * n}]
            p.append('<div class=sigs>verdict only — per-subexpression rows '
                     'arrive with the column evaluator</div>')

        for row in rows:
            src  = signals.get(row["ref"]) if "ref" in row else row
            vals = dense(src, n)
            p.append(f'<div class=lbl>{html.escape(row["label"])}</div>'
                     f'<svg width="{W}" height="{H}">'
                     f'{svg_row(vals, row.get("witnesses"), n, W, 0, H)}</svg>')
        p.append("</div>")

    p.append(f"<script>{JS}</script>")
    out.write("\n".join(p) + "\n")


def l_kind(d):
    return d.get("kind") == "signal"


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("run", help="a run trace (newline-delimited JSON)")
    ap.add_argument("-o", "--output", default="-", help="write here instead of stdout")
    args = ap.parse_args()

    with open(args.run) as f:
        lines = [json.loads(l) for l in f if l.strip()]

    if args.output == "-":
        render(lines, sys.stdout)
    else:
        with open(args.output, "w") as f:
            render(lines, f)
        print(f"wrote {args.output}", file=sys.stderr)


if __name__ == "__main__":
    main()
