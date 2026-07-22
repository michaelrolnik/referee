# Design: run traces, and why they are mostly not about failures

**Status:** proposed, not built.
**Scope:** `referee execute --explain run.json`, producing a per-requirement record of what was evaluated where, for a visualiser (Bokeh) to draw.

## The obvious motivation, and the better one

The obvious one: when a requirement fails, the report says *which* requirement and nothing else. For anything with a scope or a nested temporal operator, that leaves the real question — *why* — to be answered by squinting at the trace.

The better one is **vacuity**, and it is worth leading with because it is the failure mode this project keeps rediscovering. A requirement that only ever passes is indistinguishable from one that is meaningless. In the course of writing the MCTP examples alone:

* two of three specification files had a version that passed against every broken trace, because a bare requirement is evaluated only at the first state;
* `Cnt(Us(mid, eom))` looked like it counted a message's packets and counted the whole trace's;
* a `while` scope that never opened would have passed silently.

Each was caught by deliberately breaking a trace and checking the requirement noticed. That works, but it is manual, and it only catches what someone thought to break.

A run trace makes vacuity **visible rather than inferred**. If the visualiser draws, per requirement, the states where its scope was active and the states where its antecedent held, then a requirement whose scope never opened is a blank row. Nobody has to think of the right trace to break; the picture is empty.

That reframes the feature: it is not primarily a debugger, it is *coverage for requirements*.

## What a run trace has to contain

Per requirement, over the states of one trace:

| | why |
| --- | --- |
| the requirement's own value per state | the baseline row |
| the **scope's** active interval | `while p`, `between p and q`, `after p until q` — a blank interval is the vacuity signal |
| each **subexpression's** value per state | where a compound requirement actually went wrong |
| the **antecedent** of an implication | an implication whose left side is never true is vacuously true forever |
| the **witness** for a failure | for `G`, the first state that failed; for `F`, that no state ever satisfied it |

Source positions already exist and are already used as requirement labels (`[file:]row:col .. row:col`), so subexpressions can be keyed the same way with no new identity scheme.

## How the values are produced

Requirements compile to one function per requirement, and the lowering
deliberately does not materialise intermediate values — the O(N) recurrences
for unbounded operators and the decisive-index walk for bounded ones exist
precisely to avoid computing per-state tables.

The obvious move is to instrument that lowering: thread stores of intermediate
values through it, enabled in explain mode. **Do not.** It means editing the
recurrences that were written to avoid exactly this, and it creates a second
path that can drift from the first — and the failure mode is the worst kind, a
picture that confidently contradicts the verdict.

There is a simpler route that makes drift impossible rather than unlikely.

### Compile each subexpression as its own function

`CompileExprImpl::make(expr)` already compiles *any* `Expr` into code. So for
each subexpression worth a row, compile a function evaluating **that node** at
a given state, and call it once per state. The column falls out.

Nothing about the requirement's own lowering changes. The no-drift property
holds by construction rather than by discipline: it is the same compiler,
applied to the same AST node the requirement itself uses. There is no second
implementation to keep in step.

What this needs from the code generator is an *addition* — an entry point that
compiles an arbitrary `Expr` into a callable over `(frst, last, curr, conf)` —
not a change to anything that currently works.

### Witnesses and windows are derived, not compiled

This is the part that looked hard and is not. Given a child's column, the
witness is a pass over it in C++:

| row | witness at state `s` |
| --- | --- |
| `F(p)` | the first index ≥ `s` where `p` holds |
| `G(p)` | the first index ≥ `s` where `p` fails |
| `Us(p, q)` | the first `q` at or after `s`, with `p` holding before it |
| `Sum[lo:hi]` | the window, from the time column and the bounds |

None of it needs code generation. Deriving it from columns the producer
already has removes the messiest part of the job entirely.

### The costs, stated plainly

**Quadratic.** One function per node, called N times, and a bounded temporal
node is itself O(N) per call — so O(N²) per row. Affordable for explain mode
on one trace, given the measured split of ~700 ms fixed compilation against
~0.12 ms per row, and the reason normal runs must not pay it.

**Bound names cannot be compiled standalone.** A subexpression under a freeze
variable or a quantifier binder refers to names that exist only in the
enclosing context: `t@(...)` and `all x in xs:` cannot be lifted out. Those
nodes are either skipped, or the producer supplies the binding and emits one
row per binding.

A first version should skip them **and say so in the output**. A missing row
is precisely the wrong thing to be quiet about in a picture whose purpose is
showing what was never evaluated.

## A subtlety the visualiser must not hide

"The value of a subexpression at state *s*" is well defined for a state formula, and *also* well defined for a temporal one — but they mean different things. `Us(p, q)` being false at state 5 is not a fact about state 5; it is a judgement about the whole suffix from 5 onward.

Drawn as an undifferentiated row of green and red cells, those two read identically and one of them is misleading. Temporal subexpressions should be drawn so it is obvious their value is a claim about the future (or past), not about the instant — a different mark, or an explicit span from the state to the witness that settled it.

The same applies to accumulators: `Sum`/`Cnt`/`Itg` at a state summarise a window, and the window is the interesting thing to draw, not the scalar.

## Format

Specified separately, in [`run-trace-format.md`](run-trace-format.md) --
newline-delimited JSON, a header carrying the state timestamps once and one
line per requirement.

The format is the contract and the visualiser is replaceable, which is why it
is written down first and on its own. Two constraints shaped it: every value
array is parallel to a single shared `states.time`, so timestamps are not
repeated per requirement; and each row declares whether it is a `state`,
`temporal` or `window` value, so a reader cannot accidentally draw a claim
about a suffix as a fact about an instant.

Vacuity is computed by referee and recorded per requirement, rather than left
for a reader to infer -- a requirement can be `"verdict": "pass"` and
`"vacuous": true` at once, and that combination is the whole point.

## Open questions

1. **Which requirements get instrumented by default?** Failing ones is the obvious answer, but it is the wrong one for the vacuity use case: a vacuous requirement *passes*. Perhaps all of them on request, failing ones automatically.
2. **How much of the subexpression tree?** Every node is a lot of rows and most are uninteresting. Leaves and the operands of the outermost operator may be enough; the rest on demand.
3. **Does this subsume `--verbose`?** The per-requirement table that `-v 2` prints is a coarse projection of the same data. It may be worth generating both from one source rather than maintaining two.
4. **Should vacuity be a verdict rather than a picture?** If the data is there, referee could report "this requirement's scope never opened" without a visualiser at all — which would catch it in CI, where a picture cannot. That may be the more valuable half of this feature, and it does not need Bokeh.
