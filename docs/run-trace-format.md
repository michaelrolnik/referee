# Run trace format

**Status:** implemented and enforced -- the producer validates against `schema/run-trace.schema.json`, and the CLI test runs the validator. `witnesses` on temporal rows are optional (derived on demand; omitted today).
**Produced by:** `referee execute spec.ref trace.csv --explain run.json`
**Consumed by:** anything. A Bokeh page is the intended first reader; a VCD exporter should also be possible from the same file, which is a constraint on the design rather than a nice-to-have.

## Shape: newline-delimited JSON

One JSON document per line: a header, then the trace's signals, then one requirement per line.

```
{"kind":"header",      ...}
{"kind":"signal",      ...}
{"kind":"signal",      ...}
{"kind":"requirement", ...}
{"kind":"requirement", ...}
```

Signals come before requirements because requirements reference them, so a reader that streams has every signal in hand by the time it needs one.

Why not a single document: a run over a long trace is large, requirements are independent, and NDJSON streams — a writer never has to hold the whole thing, and a reader can stop early. It is also one line of pandas (`read_json(lines=True)`), which is what keeps the visualiser cheap.

Why not one line *per state*: the timestamps would repeat for every requirement, and the volume is already (requirements × states).

## The header

```json
{
  "kind": "header",
  "version": 1,
  "specification": { "path": "mctp.ref", "sha256": "9f86d0…" },
  "trace":         { "path": "captures/nominal.csv", "states": 4 },
  "libraries": [
    { "path": "plugins/libpec.so", "sha256": "2c26b4…" }
  ],
  "states": {
    "time": [0, 1000, 2000, 3000]
  }
}
```

`states.time` is written **once** and every value array in the file is parallel to it. That is the main reason this is not one row per state.

`libraries` is not decoration. A verdict that depended on an external function is not reproducible without knowing which object was loaded — the same argument that says the `.so` belongs in the report and not in the `.rdb`.

## Signals

The trace's own data, one line each, written once.

```json
{"kind":"signal","id":"s1","name":"k",   "type":"enum",   "values":["SOM","MID","EOM","SOM"]}
{"kind":"signal","id":"s2","name":"len", "type":"byte",   "values":[13,13,9,13]}
{"kind":"signal","id":"s3","name":"eom", "type":"boolean","computed":true,"values":[false,false,true,false]}
```

Requirement rows reference these by `id` rather than repeating them. Without
that, a signal read by twenty requirements appears twenty times — but the
duplication is the smaller problem. The larger one is that a viewer could only
draw what some requirement happened to mention, and could not draw the trace
itself, or a neighbouring signal nobody referenced. That is most of why a
timing diagram is worth looking at: the correlation you did not predict is the
one you needed to see.

### Dense or sparse, per signal

A signal is a value over time, and how best to write it down depends entirely
on how often it changes.

```json
{"kind":"signal","id":"s1","name":"dir","type":"enum","encoding":"sparse",
 "at":[0],"values":["M2S"]}

{"kind":"signal","id":"s2","name":"b","type":"byte","encoding":"dense",
 "values":[58,15,9,67]}
```

`at[i]` is the **state index** — not a timestamp — at which `values[i]` takes
effect, holding until the next entry. Indices rather than times because they
are small, exact, and save the reader searching `states.time` for a match.

Neither encoding wins in general, which is why both exist. A direction flag
constant across a million-octet capture is *one* sparse entry and a million
dense ones. An octet that changes at every state is cheaper dense, since
sparse would carry an index alongside every value. The writer picks per signal
by measuring; the reader handles both, which is a few lines
(`reindex().ffill()` in pandas). Forcing one encoding on data with these
characteristics makes a format either bloated or slow, and this data has both
kinds in the same file — that is what a trace *is*.

One thing sparse must not lose: `null` means *not evaluated here*, and is not
the same as *unchanged*. So a change **to** `null` is an entry like any other.
If absence meant both, a signal outside a scope would be indistinguishable
from one holding its last value, and that is precisely the confusion the
format exists to prevent.

`computed` marks a `data x = expr` signal, which is a property of the
specification rather than of the recording — the same distinction that keeps
computed signals out of a `.rdb`.

## A requirement

```json
{
  "kind": "requirement",
  "name": "mctp_message_terminates",
  "where": "mctp.ref:120:0 .. 120:62",
  "verdict": "fail",
  "vacuous": false,
  "scope": { "kind": "globally", "active": [[0, 4]] },
  "witness": 2,
  "rows": [ … ]
}
```

| field | |
| --- | --- |
| `name` | the `@name`, absent for an unnamed requirement |
| `where` | the source label, always present — it is the identity when there is no name |
| `verdict` | `pass` \| `fail` |
| `vacuous` | see below; the field that earns this format |
| `scope.kind` | `globally` \| `while` \| `between` \| `after_until` \| `bare` |
| `scope.active` | half-open `[start, end)` state-index pairs where the scope was open |
| `witness` | the state index that settled the verdict, or `null` |

`scope.active` being `[]` is the picture of a scope that never opened, and the reason a viewer can show vacuity without computing anything.

## Rows

A row is one thing worth drawing over time: the requirement itself, a subexpression, a signal it reads.

A row either carries its own `values` — a subexpression, computed here and
nowhere else — or a `ref` to a signal line. Exactly one of the two, never both
and never neither.

```json
{ "id": "r1", "label": "eom", "kind": "state", "type": "boolean", "ref": "s3" }

{ "id": "r2", "label": "flags & 0x40", "kind": "state", "type": "integer",
  "values": [64, 0, 64, 0] }
```

`values` is parallel to `states.time`, and `null` means *not evaluated here* — outside a scope, or short-circuited. A viewer should draw `null` as absent, not as false. The two are different, and conflating them is how a vacuous requirement comes to look like a satisfied one.

### `kind`, which is the part a viewer must not ignore

| kind | meaning | how to draw it |
| --- | --- | --- |
| `state` | a fact about that state | a cell per state |
| `temporal` | a claim about the suffix (or prefix) from that state | a **span** from the state to its witness |
| `window` | an accumulator over a `[lo:hi]` window | the window, with the value on it |

This is the distinction that makes a plotting library the right choice over a waveform viewer. `Us(p, q)` false at state 5 is not a fact about state 5, and drawn as a red cell it reads identically to a state formula being false. A `temporal` row therefore carries per-state witnesses:

```json
{
  "id": "r7",
  "label": "F(eom)",
  "kind": "temporal",
  "type": "boolean",
  "values":    [true, true, true, null],
  "witnesses": [2,    2,    2,    null]
}
```

`witnesses[i]` is the state that settled `values[i]` — the first `eom` here — so the viewer can draw an arc from 0 to 2 rather than three identical green cells. `null` where nothing settled it, which for a strong operator is itself the reason it is false.

A `window` row carries the window instead:

```json
{
  "id": "r9",
  "label": "Sum[0:5000](true, len)",
  "kind": "window",
  "type": "integer",
  "values":  [121, 98, 60, 0],
  "windows": [[0,3], [1,3], [2,3], [3,3]]
}
```

## Vacuity

Computed by referee, not left to the reader. This is the half of the feature that works in CI, where a picture cannot.

```json
"vacuous": true,
"vacuity": { "reason": "scope_never_opened", "detail": "while k.SOM was never true" }
```

Reasons worth distinguishing, because the fix differs:

| reason | meaning |
| --- | --- |
| `scope_never_opened` | `scope.active` is empty |
| `antecedent_never_true` | an implication whose left side never held |
| `evaluated_once` | a bare requirement, checked only at the first state |
| `quantifier_empty` | a quantifier whose guarded domain selected nothing |

A requirement can be `"verdict": "pass"` and `"vacuous": true` at the same time. That combination is the entire point: it passed, and it proved nothing.

## What this deliberately does not have

**No nesting.** Rows are a flat list with `where` for identity; a viewer that wants the expression tree can reconstruct it from source positions. Nesting would make the format harder to stream and harder to read into a dataframe, for a structure most readers will not use.

**No styling.** No colours, no ordering hints, no layout. Those belong to the viewer, and putting them here would make the format a rendering description rather than a record of what happened.

**No per-state timestamps on rows.** They are in the header, once.

## Open questions

1. **Which rows are emitted by default?** Every subexpression is a lot and most are dull. Leaves plus the operands of the outermost operator is probably the useful default, with everything on request.
2. **Does `--explain` imply instrumenting every requirement, or only failing ones?** Failing ones is the cheap answer and the wrong one for vacuity, since a vacuous requirement *passes*.
3. **Should `null` and `false` be distinguished in `values`, or should there be a separate `evaluated` mask?** A mask is more compact for long traces and less likely to be misread; `null` is simpler to write and to eyeball.
4. **Is `sha256` worth the dependency** for the specification and libraries, or is size-and-mtime enough? Reproducibility argues for the hash; the build argues against another dependency.
