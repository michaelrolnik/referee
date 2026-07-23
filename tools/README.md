# Viewers for run traces

Two, reading the same file. The format is the contract and the viewer is
replaceable — see [`../docs/run-trace-format.md`](../docs/run-trace-format.md).

| | |
| --- | --- |
| `view.py` | no dependencies, inline SVG, one static HTML file |
| `view_bokeh.py` | needs `bokeh`; hover, zoom, and panning linked across requirements |

```bash
python3 tools/view.py       run.ndjson -o run.html
python3 tools/view_bokeh.py run.ndjson -o run.html      # in a venv with bokeh
```

Both draw the distinctions the format exists to preserve:

- **`null` is blank, not false.** "Not evaluated" and "false" are different, and
  conflating them is how a vacuous requirement comes to look satisfied.
- **A temporal row draws a span to its witness**, not a row of identical cells.
  `Us(p, q)` false at a state is a claim about the whole suffix from it, and
  drawn as a cell it reads exactly like a state formula being false. This is
  the distinction a waveform viewer structurally cannot make, and the reason
  to prefer a plotting library over VCD.
- **A scope that never opened says so in words**, since that is the finding the
  whole feature exists for.
- **Sparse and dense look identical**, so the encoding stays an encoding
  detail rather than something a reader has to think about.

`view_bokeh.py` passes `mode="inline"`, which embeds BokehJS rather than
linking a CDN. That costs about a megabyte and buys a file that opens with no
network — attachable to a CI artifact, mailable, readable on a machine that
has never heard of bokeh. Two lazily-loaded extras (MathJax and an icon
webfont) still reference a CDN; neither is used here, so only toolbar icons
degrade offline.

## Interactive Features in `view_bokeh.py`

- **Requirement Selector**: Includes a dropdown ("show") allowing you to isolate any single requirement (e.g. `@name` or `file:row:col`). Selecting a requirement displays only the signals that requirement actually reads, hiding irrelevant background noise.
- **Linked Pan & Zoom**: Synchronizes time-axis zooming and panning across all requirement panels simultaneously.
- **Hover Inspection**: Displays state values, timestamps, and witness intervals on hover.
