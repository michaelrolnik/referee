# Design: run traces, and why they are mostly not about failures

**Status:** partly built. Header, signal, and requirement lines are written. A bare requirement carries its own per-state column; Dwyer patterns carry the verdict alone (they quantify over the whole trace and have no per-state value). Subexpression rows, scope, and computed vacuity are still to come.
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

### Bottom-up, one pass per node

Calling a per-node function once per state is O(N) calls, and a temporal node
re-walks the trace on each call, so it is O(N²) per row. That is affordable
for one trace and still wasteful, and it throws away the insight the ordinary
lowering already has.

The unbounded operators compile to an O(N) recurrence precisely because a
temporal value at state *s* is a fold of the child's value at *s* and the
parent's value at *s+1*:

```text
Us(p, q)[s]  =  q[s] || (p[s] && Us(p, q)[s+1])
```

That is a statement about **columns**, not about states. So evaluate the tree
bottom-up, a whole column at a time:

* leaves — signals and computed props — are already materialised by
  `__prepare__` before anything runs;
* state formulas are element-wise over their children's columns;
* temporal operators are a single backward pass, exactly the recurrence above;
* accumulators are a windowed pass.

One pass per node, so O(N × nodes) for the whole requirement rather than O(N²)
per row. Every column is written as it is produced, and the topmost one is the
requirement's own verdict.

### What that costs, and the check that pays for it

This is a **second evaluation path**, which is what the previous section said
to avoid. The concern was real: a picture that contradicts the verdict is
worse than no picture.

What makes it safe is that the disagreement is now *detectable*. The verdict
still comes from the compiled requirement, as it always did. The topmost
column is computed independently — so the two must agree, and referee should
assert it on every explain run:

> if the topmost column disagrees with the compiled verdict, that is a bug in
> referee, and it should be reported as one rather than drawn as a picture.

That turns "these might drift" into "drift is caught the first time it
happens, by the run that would have been misled by it". A single
implementation cannot self-check; two can. It is the same reasoning that makes
the MCTP examples inject defects rather than trusting a green result.

### Where it stays expensive

Freeze does not fold. `t@(...)` binds a state, so the inner expression has a
different column for every choice of `t` — N columns of N values, and O(N²)
again for that subtree. Quantifier binders are cheaper only because their
domain is fixed at compile time.

So the linear treatment covers the ordinary case and freeze remains the
expensive one, which is the same shape the requirement lowering already has:
`hasFreeContext` exists precisely because a temporal loop under `@` cannot be
evaluated linearly.

## A subtlety the visualiser must not hide

"The value of a subexpression at state *s*" is well defined for a state formula, and *also* well defined for a temporal one — but they mean different things. `Us(p, q)` being false at state 5 is not a fact about state 5; it is a judgement about the whole suffix from 5 onward.

Drawn as an undifferentiated row of green and red cells, those two read identically and one of them is misleading. Temporal subexpressions should be drawn so it is obvious their value is a claim about the future (or past), not about the instant — a different mark, or an explicit span from the state to the witness that settled it.

The same applies to accumulators: `Sum`/`Cnt`/`Itg` at a state summarise a window, and the window is the interesting thing to draw, not the scalar.

## What exists

```bash
referee execute spec.ref trace.csv --explain run.ndjson
python3 tools/view.py run.ndjson -o run.html          # or view_bokeh.py
```

Header and signal lines, both encodings, recorded and computed signals alike.
No instrumentation was needed for any of it: `__prepare__` materialises the
computed signals before any requirement runs, so by the time this writes,
every signal is a pointer away.

Sentinels are excluded. Ingest brackets the real states with one at each end so
a temporal walk always has somewhere to stop; they carry no recorded data and
no timestamp anyone chose, so emitting them would put two states in every
picture that were never in the capture.

The encoding is chosen per signal by counting changes over the stored bytes —
so the decision and the output agree by construction rather than by care. A
flag with two distinct values across ten states goes sparse with two entries;
a counter changing at every state stays dense.

**Built since:** requirement lines, each with the requirement's own per-state
column. The column comes from a companion `__col__<req>` compiled beside every
bare requirement -- the same node the verdict comes from, evaluated at a state
the caller chooses. It is the same compiler on the same AST, so there is no
second evaluator to drift from the verdict, and referee checks that the
column's first-state value equals the verdict on every explain run. A
disagreement prints `INTERNAL` and is a bug in referee, not a drawing choice.

Each column is marked `state` or `temporal` from the AST: `G(p)` false at a
state is a claim about the suffix, `p == 1` false is a fact about the instant,
and a viewer must not draw them the same. That distinction is the whole reason
this is worth a column rather than a verdict.

**Cost, and the ceiling on it.** A temporal column is O(N) per state and so
O(N^2) per requirement -- each state rebuilds the operator's buffer. ~0.9 s for
one `G` over 20 000 states, against 0.16 s without `--explain`. Affordable for
one trace, and exactly the waste the bottom-up column evaluator below removes;
until then, `--explain` is opt-in and single-trace.

**Built since:** subexpression rows and computed vacuity.

Each bare requirement now draws the operands of its outermost operator as
their own rows -- for `G(a && b)`, `a` and `b` beneath the conjunction, so a
picture shows which side gave way and when rather than only that the whole
thing did. The operand functions and their labels come from the code
generator, recorded on the module as it emits them, so the host draws them
without walking the AST a second time and risking a different answer. A
constant operand gets no row (a flat line nobody needs); a freeze-bound one
gets none either (it cannot compile on its own, which is the case the ordinary
lowering already treats as separate).

Vacuity is computed for the one case that needs no scope analysis: an
implication whose antecedent never fires. `G(a => b)` on a trace where `a` is
never true holds no matter what `b` does, and referee marks it rather than
leaving a green row to be mistaken for a satisfied one. A `__ante__` companion
is the antecedent's own column, read from the raw tree before `=>` is
canonicalised to `!a || b`.

**Built since:** `scope.active` and `scope_never_opened` vacuity for Dwyer
patterns.

Each pattern's scope decides where it is even checked. The scope's boundary
conditions -- `after Q`, `between Q and R`, `while Q` -- are compiled to column
functions, and the host folds the active intervals out of them: a fold over a
column needs no code generation, the same reasoning that keeps witnesses out of
the generator. `globally` is the whole trace; `after Q` opens at the first `Q`;
`between Q and R` is each complete window; `after Q until R` stays open to the
end when `R` never comes, which is the one place it differs from `between`.

A scope that never opens is `scope_never_opened` vacuity: the requirement
passed because nothing exercised it, which reads identically to a satisfied one
and is the coverage gap this half of the feature exists to close. It applies
where an opening is required -- `after`, `while`, `between`, `after_until` --
and not to `globally`/`before`, which cover the trace even when their boundary
is absent.

**Still not built:** `quantifier_empty` vacuity, and the O(N^2)-to-linear
bottom-up column evaluator.

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
