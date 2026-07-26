# Building the monitor — an implementation plan

**Status:** phase 1 built; the rest proposed. Companion to
[monitor.md](monitor.md), which is the design — the semantics, the verdict
domain, and why the pieces are shaped as they are. This is the *how*: what to
reuse, what to write, and in what order.

Phase 1 (`Referee::monitor`, `src/driver/referee.cpp`; the `monitor` subcommand)
took the pragmatic route over the phased build below: rather than the incremental
frame-and-atom evaluator, it reuses the whole offline path per prefix — build the
JIT once with `buildJitFromRef`, then for each streamed row re-ingest the
accumulated CSV (`ingestWithModule` → `Reader`) and run `runOneTrace`, diffing
the per-requirement verdicts. That is O(N²), not single-pass, but correct and
tiny, and it de-risked the front end exactly as intended. A requirement with an
eventually operator (`hasEventually` walks for `F`/until/release) is finalised at
end of stream so a pessimistic prefix is never cried as a violation; everything
else is reported the instant its prefix verdict turns false. The incremental
evaluator, projection, segments, `wait`/`yield`/`refresh` and scopes remain the
proposed work below.

## What it reuses, and what is new

The monitor is a new front end over machinery that already exists, not a second
compiler.

Reused as-is:

- **the loader** (`src/core/loaders/csv.cpp`, `row.cpp`) — turns one input row
  into one `state_t`, with the schema's layout and stride, the string pool, and
  ragged `{count, pointer}` descriptors. A live row and a `.rdb` record are the
  same bytes.
- **the code generator** (`src/core/visitors/compile.cpp`) — for the atoms, the
  non-temporal per-state predicates. The temporal folds it emits are the offline
  ones and are *not* reused for future operators (see the design).
- **the AOT checker ABI** (`referee_module_v1`) and the whole-state calling
  convention — the deployable, LLVM-free path the monitor's atoms can bind to at
  the edge.
- **the terminal-aware colouring** (`src/core/colormod.hpp`) — verdict and
  violation lines reuse it unchanged.

New:

1. a **classification pass** over each requirement's AST;
2. **single-state atom compilation** — a narrowing of the existing code
   generator;
3. a **per-requirement frame** — the explicit-state realisation of
   `wait`/`yield`/`refresh`;
4. a **stream front end** and a **cooperative scheduler**;
5. the **`referee monitor`** CLI subcommand.

## The pieces

**1. Classification pass.** A static walk of each requirement's AST (the same
visitor infrastructure the type checker uses) produces, per requirement:

- its **signal footprint** — the `ExprData` names it reads, for projection;
- flags: **uses-next** (`Xs`/`Xw`/`Ys`/`Yw`), **timed** (any bounded operator,
  or `Itg`), **accumulating** (`Sum`/`Cnt`/`Itg`), and its **scope shape**
  (`globally`, `before`, `after`, `between … and …`, `after … until …`).

From those follow every downstream decision: which signals to project, whether
the requirement may stutter-collapse and to what unit (value / timed segment /
segment-with-count / every state), whether its window is bounded, and where its
`refresh` points are. This pass is cheap and gates the rest.

**2. Single-state atom compilation.** *(Built.)* Today's compiled functions take
`(frst, last)` — a whole trace. The monitor needs the requirement's *atoms* —
its predicates over one state — compiled to take a single `state_t*`. This is a
narrowing of the existing generator, not new semantics: the same expression
lowering, emitted against one state instead of a range.

The generator now emits, per requirement, a companion `__atom__<name>(curr,
conf)` — a two-argument function, no `frst`/`last` — whenever the requirement
reads only the current state: directly (a non-temporal requirement) or as the
body of one ranging operator, `G`/`F`/`H`/`O`, over such a predicate, so an
invariant `G(P)` yields `P`. The eligibility test is `readsOnlyCurrent`, which
unlike `is_temporal()` also rejects `Xs`/`Xw`/`Ys`/`Yw` (they shift the state
but are modelled as plain binaries) and freeze. The two-argument shape is the
third case in the compile-visitor constructor, beside the requirement
`(frst,last,conf)` and column `(frst,last,curr,conf)` forms; `curr` stands in
for `frst`/`last` so the pointer type resolves, and an atom carries nothing
temporal to read them. Like the other `__…` companions, `__atom__` is filtered
out of the requirement harvest (three sites) so it is never run as a
requirement, and dropped from an AOT object where nothing calls it.

What remains is the *consumer*: the phase-1 monitor still re-ingests the whole
prefix. Wiring it to build one `state_t` per row and call `__atom__` per state
— O(1) per state for an invariant, no re-run — is the next step and the payoff
of this piece.

**3. The per-requirement frame.** A small struct carried across steps, the
explicit-state form of the design's primitives:

- one **latch** per unbounded future operator — the LTL₃ residual (`true` /
  `false` / `unknown`);
- a **segment window** buffer for a bounded scope or a timed collapse — projected
  values with their `[start, end)` span and state count;
- the running **accumulators**;
- the **refresh points** — where a control path drops the window and re-arms.

`wait` advances the frame by one state (or one segment); `yield` emits a verdict
or a violation; `refresh` clears the window. Realised first as this explicit
struct threaded across calls; compiled coroutines (`llvm.coro`) are a later
option only if nested future formulas make the hand-threaded form the hard part.

**4. Stream front end.** Read rows from stdin, build a `state_t` per row via the
loader, then for each requirement project to its footprint and change-collapse to
its delivery unit (from the classification) before feeding its frame. String
signals intern against the pool as they arrive.

**5. Scheduler.** A cooperative, single-threaded loop: per input state, advance
each requirement's frame, print any violation (the projected counterexample and
its `__time__`), and — with `--stop-at-first` — exit non-zero on the first. At
end of stream, run the finite-trace finalisation over every still-`unknown`
requirement and print the closing verdicts.

**6. CLI.** `referee monitor spec.ref --conf conf.csv [--stop-at-first]`, sharing
the argument plumbing and diagnostics with `execute`.

## Build order

Each phase is a usable monitor on its own; each adds one capability and its
tests.

| phase | capability | proves |
| ----- | ---------- | ------ |
| 1 | invariants only (`globally, always/never P`) — no future, no scopes | the stream front end, loader-per-row, projection, per-state violation reporting, colouring |
| 2 | unbounded future via LTL₃ latches (`G`, `F`, unbounded `U`/`R`) + end-of-stream finalisation | the frame's latches, the agreement test |
| 3 | bounded future (`F[a:b]`, `U[a:b]`) + timed segments + change-collapse | the segment delivery unit, timed soundness |
| 4 | Dwyer scopes — open/close detection, per-scope window, per-instance offline fold at close, `refresh` | window bounding, scope vacuity, refuse-to-compile for unbounded memory |
| 5 (maybe) | compiled coroutines | only if phase-3/4 explicit state gets unwieldy |

Phase 1 is the smallest thing worth shipping and de-risks the whole front end
before any temporal state is involved.

## Testing

- **Agreement with the offline checker is the backbone.** Every existing fixture,
  run through both `execute` and `monitor`, must give the same per-requirement
  boolean: the monitor's finalised LTL₃ verdict has to equal the offline result
  on the same trace. This reuses the entire fixture corpus for free and is the
  property that keeps the two evaluators honest.
- **Bounded memory is a testable claim, not a hope.** A next-free untimed
  invariant retains O(1); a bounded scope retains O(window); an unbounded-memory
  requirement is refused at compile time. Each is an assertion, not a
  measurement after the fact.
- **Streaming vs batch equivalence under stutter-collapse** — a trace and its
  change-collapsed form must give the same verdict for every stutter-invariant
  requirement, and must be *rejected* from collapse for the rest.

## Risks and open questions

- **Single-state atom extraction.** The current generator assumes a trace;
  isolating a pure per-state atom needs care around indexing and freeze. This is
  the main technical unknown and phase 1 exists partly to find its edges.
- **Timed-segment soundness.** Collapsing a run into a `[start, end)` segment must
  preserve exactly what a bounded operator reads; the segment boundaries are the
  place bugs will hide.
- **The coroutine route** is a large code-generator change with ORC JIT
  implications, deliberately deferred behind the explicit-state form until there
  is evidence it is needed.
