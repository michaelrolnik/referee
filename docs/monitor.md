# Design: online monitoring — a verdict as the trace unfolds

**Status:** proposed. Nothing here is built yet; this records the design so it
can be argued with before any code is written.

**Scope:** a `referee monitor spec.ref` that consumes states one at a time from
a stream and emits a verdict per requirement per step, deciding the instant a
requirement is settled rather than waiting for a finished trace.

## The gap

Everything referee does today is *post-hoc*. `execute` and the AOT checkers
need a complete `.rdb` (or a whole CSV/YAML) before they can say anything: the
trace is opened, its length is known, and the requirement functions run over
the finished buffer. That is the right shape for checking a log after a run,
and it is the only shape there is.

But the language describes behaviour over time, and a great deal of what one
wants to check happens *while a system is running* — a test bench feeding
inputs, a device on a bench, a log being tailed. For that you want to hand the
checker one state, get the verdicts so far, hand it the next, and be told the
moment a requirement is violated — not after the run is over and the damage is
done. That is online monitoring, and it is the runtime-verification use case
the project's own framing points at without yet serving.

## The verdict is three-valued

A finished trace gives every requirement a boolean. A *prefix* cannot, and
pretending otherwise is the whole difficulty. `G p` (always `p`) is not `true`
on any prefix — the next state could break it — yet it is `false` the instant
`p` fails. `F p` (eventually `p`) is not `false` on any prefix — `p` could
still arrive — yet it is `true` the instant `p` holds. So a prefix verdict has
three values, the standard **LTL₃** domain:

| verdict     | meaning                                                        |
| ----------- | -------------------------------------------------------------- |
| `false`     | every continuation of this prefix violates the requirement — settled, will never recover |
| `true`      | every continuation satisfies it — settled, cannot be broken    |
| `unknown`   | continuations exist both ways — the prefix has not decided     |

`false` and `true` are final: once emitted they never change. `unknown` is the
honest answer while the future still matters. A monitor's job is to emit
`unknown` for as short a time as possible and to reach a settled verdict as
early as the prefix allows.

## The monitor reports violations, not just a verdict

The three-valued verdict is the requirement-level answer, and it is monotone:
once a requirement is `false` it stays `false`. But a monitor's useful output is
not one latched bit per requirement — it is the *stream of violations*
themselves. When a requirement is violated the monitor prints the offending
state and its `__time__`, then keeps running and reports the next one, all the
way to end of stream (unless `--stop-at-first`). The final per-requirement
verdict is just `FAIL` if any violation was reported, `PASS` if none.

What counts as one violation depends on the requirement's shape:

- A **safety / invariant** requirement — `globally, it is always the case that
  P`, or `… never Q` — is checkable state by state, so every state that breaks
  it is its own violation, reported with that state as the counterexample and
  its time. A stream that breaks the invariant ten times prints ten lines, not
  one.
- A **future / liveness** obligation — a response deadline, an eventually —
  has no per-state counterexample: it is violated at the moment it is *decided*
  unmet, which is a bounded deadline elapsing, a scope closing with the
  obligation undischarged, or end of stream. The violation is reported there,
  with the state that opened the unmet obligation as the witness.

The counterexample is the projected state — only the signals the requirement
reads (see the execution model below) — so a violation line shows exactly the
values that made it fail, at the time it failed, and nothing else.

## What is online-friendly, and what is not

The evaluation strategy divides cleanly along the same line the operators do.

**Past operators are already online.** `H`, `O`, `Y`, `S`, `T` are lowered as a
*forward* recurrence — `val[i]` depends on `val[i-1]` and the current state —
so each new state extends the fold with no need to look ahead. `O(a)` becomes
`true` the first time `a` holds and stays there; `H(a)` is `true` until the
first `!a`. Unbounded, each carries a single scalar of state, updated per step.
Their *bounded* forms (`O[a:b]`, `S[a:b]`, …) still only ever look backward, so
they are equally online, but they need a sliding window of the states inside
the last `b` time units rather than a scalar — bounded memory, not constant.

**Bounded future operators decide inside their window.** `F[0:5](p)`,
`p U[a:b] q`, `G[0:n](p)` refer only to states within a fixed time horizon of
now, so a monitor settles each obligation once that many time units have
elapsed — no unbounded wait. The cost is a bounded buffer: the monitor holds
the states still inside some open window.

**Unbounded future operators are the hard part, and today's lowering does not
help.** `G`, `F`, and unbounded `U`/`R` are compiled *offline*: a backward
recurrence `val[i] = rhs[i] || (lhs[i] && val[i+1])`, buffered over
`bool[numStates]`, which needs the whole suffix and so cannot run on a prefix.
A monitor cannot reuse those buffers. What it can do instead is carry the
*obligation* forward: `G p` holds a single latched flag that flips to `false`
on the first `!p`; `F p` holds a flag that flips to `true` on the first `p`; an
unbounded `p U q` is `false` once `p` fails before `q`, `true` once `q` holds,
`unknown` until then. These are exactly the LTL₃ residuals, and each is a small
fixed amount of state — no growing buffer. So the monitor is not a reuse of the
future-operator column functions; it is a distinct forward evaluator that
shares the *atoms* — the non-temporal per-state predicates, which the code
generator can emit for a single state (see *Shape*) — but not the temporal
folds.

**Computed signals inherit the same split.** A computed signal (`data x = expr`)
is filled by `__prepare__` ahead of the requirements, and it classifies exactly
as the operators above do: a non-temporal one evaluates per step; a bounded
temporal one (`data nxt = Xs(a)`, next-state) settles after its fixed delay; but
an *unbounded* temporal computed signal (`data ever_a = F(a)`) is itself an
unbounded-future obligation that then feeds other requirements as if it were an
ordinary signal — so it carries the same LTL₃ latch as any future operator,
resolved before the requirements that read it that step, or is kept offline-only
like the constructs listed at the end.

A worked prefix makes the three values concrete. Two requirements,
`G(p)` and `F(q)`, over states arriving left to right:

| step | `p` | `q` | `G(p)` | `F(q)` |
| ---- | --- | --- | ------ | ------ |
| 1    | T   | F   | `?`    | `?`    |
| 2    | T   | F   | `?`    | `?`    |
| 3    | T   | T   | `?`    | **`PASS`** — `q` held, and no future can un-hold it |
| 4    | F   | F   | **`FAIL`** — `p` broke, and no future can repair it | `PASS` |

`G(p)` sits at `?` as long as `p` keeps holding — it can never earn `PASS` on a
prefix, only lose to a single `F` — and settles `FAIL` at step 4. `F(q)` earns
`PASS` the instant `q` first holds and never looks back. A settled column never
changes again; that is what lets a supervisor act on it.

## Monitorability

Not every requirement can settle early, and the design must not pretend
otherwise. `G(F(p))` — "`p` infinitely often" — is `unknown` on *every* finite
prefix: no finite observation can confirm it (more `p` might always be owed) or
refute it (the next `p` might still come). Such properties are
*non-monitorable*: the monitor correctly reports `?` throughout and only
resolves them at end of stream, by the finite-trace rule below. This is a
property of the logic, not a shortcoming of the implementation, but the monitor
should be able to *tell the user* at compile time which requirements can never
give an early verdict, so a `?` that will never resolve is not mistaken for one
that simply has not yet.

Time-bounded operators are always monitorable, which is another reason to reach
for `F[0:n]` over bare `F` when a deadline is what is actually meant — and the
windows are measured in `__time__`, the trace's own clock, so a monitor settles
`F[0:n](p)` once `__time__` has advanced `n` past the obligation's start, however
many or few states fell in between.

## End of stream

A stream ends. When it does, every still-`unknown` requirement must be given a
final boolean, and the strong/weak distinction is precisely the rule for it —
the same rule the finite-trace semantics already use. `F p` still `unknown` at
end of stream resolves to `false` (the eventuality never arrived); `G p` still
`unknown` resolves to `true` (nothing ever broke it). A strong operator that
never discharged fails; its weak partner passes. So closing the stream is not a
special case bolted on — it is the finite-trace verdict the offline checker
would have produced, and the online and offline answers must agree on the same
trace. That agreement is the correctness property worth testing.

An unbounded-scope liveness obligation is the sharp case, and worth stating on
its own. A requirement under `globally` or an open `after Q` — `F(p)`, or `if Q
then eventually R` with no deadline — can never be pronounced *failed* while the
stream runs: `p` or `R` could always still arrive. So it does not
fail-and-continue the way an invariant does; it stays pending, and its verdict
is decided only at end of stream, where an undischarged obligation becomes a
single violation witnessed by the state that opened it. There is no continuation
past that point because the stream is over.

The pending state this needs is almost always O(1) — a single `R` clears every
outstanding `Q` at once, so one latch plus the earliest outstanding time (the
witness to report) is enough — and grows with the stream only for a matched
freeze. *What actually accumulates* below is the full picture.

## Dwyer scopes can make an unbounded operator bounded

The pessimism about unbounded future operators lifts under a *closing* Dwyer
scope. `F(p)` under `globally` may never settle before end of stream; the same
`F(p)` written `between Q and R, ... F(p) ...` only has to discharge before the
scope's own closing boundary `R`. So the obligation is bounded by `R`, and the
scope instance earns a settled verdict the moment `R` fires — not at end of
stream.

This wants no new machinery; it reuses the offline lowering at a smaller scale.
The boundaries `Q` and `R` are ordinary predicates, evaluated per state, so
scope open and close are detected online. The monitor buffers the states of an
open scope, and when the scope closes it runs the existing O(N) backward fold
over *just that interval* — a bounded, per-instance offline evaluation. Memory
is the width of the widest currently-open scope, not the whole trace.

Which scopes help, and which do not, follows the scope's own shape:

| scope                 | bound on a future obligation inside it              |
| --------------------- | --------------------------------------------------- |
| `between Q and R`     | bounded — closes at the next `R`                    |
| `after Q until R`     | bounded while `R` comes; open (to end) if it does not |
| `before R`            | bounded — closes at the first `R`                   |
| `after Q`             | unbounded — opens at `Q`, never closes              |
| `globally`            | unbounded — the whole stream                        |

A scope left open at end of stream (a `between` whose `R` never arrived, an
`after` that ran to the end) falls back to the finite-trace finalization rule
like any other unresolved obligation, and the scope-vacuity reporting that
already exists covers a scope that never opened at all.

## Execution model: projected windows and a wait/yield operator

Two refinements sharpen the per-scope picture.

**Each pattern sees only its own signals.** A requirement reads a known set of
signals — a static walk of its AST — so the monitor can project the input to
just those before handing them over: a `between Q and R` pattern that mentions
only `p`, `Q`, `R` never sees the rest of the state. Projection is cheap and
shrinks both the per-pattern buffer and the work.

**And only when those signals change.** Because a requirement reads a subset of
the state, consecutive states often look identical *to it* even as other signals
move, and those repeats can be collapsed: push a state to a requirement only
when its projected view differs from the last it saw. Real streams are sticky,
so this can shrink what a requirement processes by orders of magnitude, and it
reduces a scope window to the *changes* within the scope rather than every
state. But it is stutter-collapsing, sound only for the stutter-invariant
fragment, which the compiler must confirm from the AST before applying it:

- **next-free** — `Xs`/`Xw`/`Ys`/`Yw` count steps, so dropping a repeated state
  changes what "next" and "previous" refer to. A requirement that uses them
  keeps every state.
- **untimed** — a bounded operator (`F[0:5]`, `G[a:b]`) measures `__time__`,
  which advances even when signals do not, so the repeat is not dropped but
  collapsed into a *segment* carrying its `[start, end)` time span, and the
  deadline still fires on time.
- **non-accumulating** — `Sum`/`Cnt` count states and `Itg` integrates over
  time, so a collapsed run must carry its state count (the segment's duration
  already gives `Itg` the time it needs).

So the general delivery unit is a segment — projected values plus the time span
and state count they held for. A next-free untimed boolean requirement is the
special case that keeps only the value and collapses maximally; an `Xs`/`Ys`
requirement is the opposite case that keeps every state. The classification is
the same static walk that finds the signal footprint.

**Bounded scopes are delivered as windows, not single states.** For a closing
scope the natural unit is the whole `[Q, R)` interval, not one state: the
monitor accumulates the projected states of an open scope and hands the pattern
the completed window when `R` fires — exactly when its per-instance offline fold
can run. `globally` and open-ended `after Q` have no closing boundary, so they
stay per-state (windowed only at end of stream).

**A wait/yield operator is the programming model for both.** Written the obvious
way, a monitored requirement is a loop that consumes states, *waits* when it
needs more, and *yields* a verdict when a scope closes or an obligation settles
— a coroutine, not a function called from outside. That expresses nested future
formulas far better than hand-threaded latches, and it unifies the two
refinements: `wait` is how a pattern asks for its next state, or its next
window.

**A refresh primitive bounds the memory.** `wait` and `yield` move a requirement
through time; a third primitive, `refresh`, drops accumulated window state once
it is provably no longer needed. When a `between Q and R` scope closes, its
window has been folded and its verdict yielded, so the buffered states are dead
— `refresh` discards them and re-arms the pattern for the next `[Q, R)`
instance, and memory returns to nothing between scopes. Which shapes accumulate,
and how much, is *What actually accumulates* below; what matters here is that
making the drop explicit turns it into a compile-time guarantee — an obligation
set no control path ever refreshes away is an unbounded-memory requirement the
monitor can refuse to compile rather than discover in production.

The open question is how far to compile it:

- **Explicit state** — each requirement carries a small frame of latches and a
  window buffer that the code generator threads across per-step calls. This is
  what the LTL₃-plus-scope-fold latches already are; it needs no new codegen and
  is equivalent in power.
- **A real compiled coroutine** — LLVM's `coro` intrinsics, or suspend/resume
  added to the whole-state calling convention. It reads far better for deep
  nesting, but is a large investment in the code generator and its ORC JIT
  interaction.

Start with explicit state; reach for compiled coroutines only if the
hand-threaded machines for nested future formulas become the thing that is hard
to get right.

All of this is cooperative and single-threaded: one scheduler pumps every
requirement's coroutine as data arrives, deterministically and without
synchronisation. That is deliberately *not* a preemptive OS thread per
requirement — a shared stream fanned out under backpressure, plus a per-step
barrier to print one coherent verdict line, pays thread overhead only to erase
its own parallelism. The axis on which parallelism *does* pay is independent
traces, a corpus checked trace-per-core: embarrassingly parallel, no
shared-stream problem, and a property of the offline `execute` rather than of a
single live monitor.

## What actually accumulates

The memory story, in one place, since it is what decides whether the monitor is
deployable. The governing principle is *collapsibility*: a requirement's state
stays bounded exactly when its outstanding obligations either **collapse** — one
event discharges all of them at once — or **age out** — a time bound retires
them — and it grows only when they can do neither.

| shape | state | why |
| ----- | ----- | --- |
| unbounded past (`O`, `H`, `Y`, `S`, `T`) | O(1) scalar | the forward recurrence folds all history into one value; past states are never needed again |
| bounded past (`O[a:b]`, `S[a:b]`) | O(window) | a sliding window of the last `b` time units, ageing off the back |
| bare unbounded future (`G`, `F`, unbounded `U`/`R`) | O(1) latch | one flag settles it; no per-instance state |
| bounded future (`F[0:n]`, `U[a:b]`) | O(window) | pending obligations, each retired when its deadline passes |
| uncorrelated response (`G(Q => F R)`) | O(1) latch | a single `R` discharges every outstanding `Q` — they collapse to "any `Q` pending" |
| matched freeze with a deadline (`… == t.id && t.elapsed <= n`) | O(window) | each obligation ages out `n` after its freeze, so only the freezes in the last `n` units are ever in flight |
| **matched freeze, no deadline** | **unbounded** | each frozen value is its own obligation; nothing collapses them and nothing retires them |

So the one construct that can make online memory grow with the stream is narrow:
a **matched freeze with no time bound** — a freeze that captures a per-instance
value and matches it under a future operator, `G(t @ (Q => F(R && R.id ==
t.id)))`, with nothing to retire the obligations. Add a deadline —
`t.elapsed <= n`, the sugar for `__time__ - t.__time__ <= n` — and each
obligation ages out, capping the in-flight set to the freezes in the last `n`
units. That deadlined form is bounded, and it is the idiomatic one to write (the
`readme_freeze` fixture's shape). Everything else — all of the past, bounded
future, and *uncorrelated* unbounded future — is bounded too, and a freeze that
does not individuate (a constant or unmatched frozen field, like
`G(t@(Ss(a,b)) == Ss(a,b))`) collapses back to the no-freeze case. This is what
lets `refresh` be a compile-time guarantee rather than a hope: the classifier
recognises the narrow un-deadlined shape from the AST and either bounds it or
refuses it, instead of letting a footprint grow unwarned in production.

## Shape

```
referee monitor spec.ref            # states arrive on stdin, one CSV row per line
  --conf conf.csv                   # the fixed configuration, as elsewhere
  --stop-at-first                   # exit on the first FAIL (default: keep going)
```

The natural stream is the same CSV a `.rdb` is built from, one row per line, so
a producer can `| referee monitor` a live feed. (A length-framed binary form of
the same state layout is a later option for machine producers that want to skip
CSV; the semantics are identical.)

Per input state the monitor writes a line of verdicts — one column per
requirement, `?`/`PASS`/`FAIL`. With `--stop-at-first` it exits non-zero the
instant any requirement reaches `false`, so a supervisor can halt the system
under test at the first violation; by default it keeps running so a single pass
collects every violation. On end of stream it prints the finalised verdicts and
the overall result. The verdict output reuses the same terminal-aware colouring
as `execute`.

Each incoming row is turned into one `state_t` by the same loader that builds a
`.rdb`: the schema fixes the layout and stride, string-valued signals are
interned against the pool as they arrive, and a state carrying ragged arrays
brings its own `{count, pointer}` descriptors. A single state must be
self-contained, which it already is.

The atoms — a requirement's non-temporal leaf predicates, the comparisons and
boolean combinations of the current state's signals — are the reusable part.
Today's compiled functions take the whole trace `(frst, last)`, so they are not
themselves single-state callable; what the monitor needs is those same atoms
compiled to evaluate one state, which is a narrowing of the existing code
generator, not new semantics. Only the temporal layer above the atoms is
genuinely new, and it is small: a fixed struct of latches and windowed buffers
carried across steps.

## What does not fit yet, and is out of scope here

- **The un-deadlined matched freeze.** A matched freeze that carries a deadline
  — `t.elapsed <= n`, the sugar for `__time__ - t.__time__ <= n` — is already
  bounded and monitorable: its obligations age out, and it is the idiomatic form
  (see *What actually accumulates*). What is deferred is only the case with *no*
  deadline, where obligations never retire — bounding it by an in-flight count,
  or refusing it outright, is the open question. A freeze that does not
  individuate is fine either way.
- **Accumulators** (`Itg`, `Sum`, `Cnt`) fold over the trace. A running total
  is naturally online, but one under a temporal scope inherits that scope's
  memory characteristics.
- **Freeze or accumulators nested *inside* a Dwyer scope.** The scopes
  themselves are in scope and central (see above); what is not yet worked out is
  their composition with the two constructs above — a freeze or a running total
  whose lifetime is tangled with a scope's open interval.
- **Quantifiers over ragged arrays** are per-state and so unaffected — they
  quantify within one state, not across time.

None of these block the core: past operators, bounded future operators, and the
LTL₃ latches for unbounded future cover the common requirement shapes. The rest
can stay offline-only until there is a reason to move them.

## Relationship to the offline checker

The offline lowering is not replaced. It stays the right tool for a finished
log — faster (one O(N) fold, no per-step dispatch) and complete, handling even
the constructs the monitor leaves offline-only. The monitor is an alternative
front end over the same atoms for the streaming case; where both apply they must
agree, which is the correctness test the *End of stream* section and the
implementation plan both rest on.

See [monitor-implementation.md](monitor-implementation.md) for how this design
is meant to be built — the reused machinery, the new pieces, and a phased order.
See also [run-traces.md](run-traces.md) for the per-state column machinery the
monitor's output resembles, and [references.md](references.md) for LTL₃ and the
finite-trace semantics the end-of-stream rule comes from.
