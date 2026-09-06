---
title: "Dwyer specification patterns in REF"
description: "Dwyer specification patterns and the real-time PSP catalogue as a compiled DSL — absence, existence, universality, response, precedence, duration and recurrence, across five scopes, lowered to LLVM IR and checked over traces."
---

# Dwyer specification patterns in REF

Referee's pattern language is an executable implementation of the property
specification patterns literature. You write a requirement in structured English;
the compiler desugars it to a temporal formula, lowers that to LLVM IR, and emits
native code.

Three bodies of work meet here:

- the original qualitative catalogue of **Dwyer, Avrunin and Corbett** (ICSE
  1999) — absence, existence, universality, response, precedence, and the five
  scopes that bound them;
- the real-time extensions of **Konrad and Cheng** (ICSE 2005), which add
  duration, recurrence and steady-state behaviour with explicit time bounds;
- the structured-English grammar of **Autili et al.** (TSE 2015), which unifies
  the two into one phrasing system.

Most tooling in this space stops at formula generation. Referee compiles the
pattern all the way to a checker — see [Architecture](architecture.md) for the
pipeline and [Native checkers](native-checkers.md) for the code that comes out
the far end.

## Scopes

A pattern statement is a **scope**, then a **body**, separated by a comma. The
scope chooses the stretch of trace the requirement is about.

| Dwyer scope | REF syntax | Holds over |
| --- | --- | --- |
| Globally | `globally, …` | the whole trace |
| Before *R* | `before P, …` | from the start until the first `P` |
| After *Q* | `after P, …` | from the first `P` to the end |
| — | `while P, …` | every maximal stretch in which `P` holds |
| Between *Q* and *R* | `between P and Q, …` | each complete `P`-to-`Q` segment |
| After *Q* until *R* | `after P until Q, …` | each `P`-to-`Q` segment, including an unclosed final one |

`while` has no counterpart in the 1999 catalogue; it comes from the real-time
line of work, where a scope is the natural way to talk about a mode the system is
in.

## The catalogue

Bodies in grammar order. `?` marks an optional word; `P`, `S`, `T`, `Z` are
arbitrary expressions, temporal operators included.

| Pattern | Origin | REF body |
| --- | --- | --- |
| Universality | Dwyer 1999 | `it is always the case that P holds? <bound>` |
| Absence | Dwyer 1999 | `it is never the case that P holds? <bound>` |
| Existence | Dwyer 1999 | `P eventually holds? <bound>` |
| Transient state | real-time PSP | `P holds after N units` |
| Steady state | real-time PSP | `P holds in the long run` |
| Minimum duration | real-time PSP | `once P becomes satisfied? it remains so for at least N units` |
| Maximum duration | real-time PSP | `once P becomes satisfied? it remains so for less than N units` |
| Recurrence | real-time PSP | `P holds? repeatedly (every N units)?` |
| Precedence | Dwyer 1999 | `if P holds?, then it must have been the case that S has occurred? <interval> before it?` |
| Precedence chain 1-2 | Dwyer 1999 | `if S and afterwards T <upper> holds?, then it must have been the case that P has occurred? <interval> before it?` |
| Precedence chain 2-1 | Dwyer 1999 | `if P holds?, then it must have been the case that S and afterwards T <upper> have occurred? <interval> before it?` |
| Response | Dwyer 1999 | `if P has occurred?, then in response S eventually holds? <bound> <constraint>` |
| Response chain 1-2 | Dwyer 1999 | `if P has occurred?, then in response <bound> <constraint> S followed by T <bound> <constraint> eventually holds?` |
| Response chain 2-1 | Dwyer 1999 | `if S followed by T <bound> <constraint> have occurred?, then in response P eventually holds? <bound> <constraint>` |
| Response invariance | real-time PSP | `if P has occurred?, then in response S holds? continually <bound>` |
| Until | real-time PSP | `P holds? without interruption until S holds? <bound>` |

**Time bounds** (`<bound>`, `<interval>`, `<upper>`) are `within N units`,
`after N units`, `between N and M units`, or nothing at all. `units` is one of
`nanoseconds`, `microseconds`, `milliseconds`, `seconds`, `minutes`.
**Constraints** are `without Z holding in between`, or nothing.

Omit every optional part and a pattern reduces to its qualitative Dwyer form; add
a bound and you get the real-time variant. There is one grammar, not two.

## What the common patterns mean

Under `globally`, informally:

| Pattern | Reading |
| --- | --- |
| Universality | `G(P)` |
| Absence | `G(!P)` |
| Existence | `F(P)` |
| Recurrence | `G(F(P))` |
| Response | `G(P -> F(S))` |
| Precedence | `G(P -> O(S))` — Referee has past operators, so precedence reads directly |

The duration and state patterns have no one-line formula; they are about how long
a condition survives once entered, and the compiler lowers them to bounded
operators over the trace's timestamps. The authoritative desugaring is the
compiler's, not this table's — the grammar lives in `src/core/referee.g4`.

## Worked examples

```text
before button.DEPRESSED, lock.ON eventually holds after 100 milliseconds;
globally, it is never the case that door.CLOSED && alarm.ON;
while door.OPENED, it is always the case that alarm.ON after 30 seconds;
globally, if button.DEPRESSED, then in response lock.ON after 100 milliseconds;
globally, once lock.ON becomes satisfied it remains so for at least 2 seconds;
between door.CLOSED and lock.OFF, it is always the case that door.CLOSED;
after lock.ON, if door.OPENED, then it must have been the case that lock.OFF has occurred before it;
```

Read line four as the requirement a reviewer would sign off on: pressing the
button locks the door within 100 ms. That is a Dwyer **response** pattern with a
real-time bound, and it is also the thing that runs.

Each line compiles to the same kind of boolean-valued function over the trace as
a raw formula does. Patterns are surface syntax for the temporal logic, not a
separate mechanism — you can mix them freely with `G`, `F`, `O`, `H`, `Us`, `Ss`,
the bounded forms `G[100:1000](a)`, and the TPTL freeze operator.

## Scopes and temporal operators

A temporal operator inside a scoped body reads only that segment. `O(...)` looks
back no further than the segment's first state, `F(...)` no further than its last:

```text
after c, it is never the case that O(a) holds;
```

This holds on a trace where `a` occurs only before `c`. If you meant the
whole-trace question, name the sub-formula as a computed signal — which also keeps
it linear:

```text
data seen_a = O(a);

after c, it is never the case that seen_a holds;
```

The scoped spelling asks whether `a` occurred *since the scope opened*; `seen_a`
asks whether it occurred *at all*. See [The REF language](language.md) for the
full semantics and [Accumulator cost](accumulator-cost.md) for why the scoped
form falls back to an O(N²) scan.

## From pattern to monitor

1. **ANTLR4** parses the `.ref` source and builds an AST.
2. Patterns **desugar** to the core temporal logic — LTL, past-time LTL, MTL
   bounds, TPTL freeze.
3. The formula is lowered to **LLVM IR** and optimized.
4. `referee execute` JIT-compiles it and checks a trace offline;
   `referee monitor` evaluates the same program as the trace streams;
   [native checkers](native-checkers.md) emit a standalone object file.

Name a requirement with `@` and verdicts report by name rather than by source
position, so a corpus of traces survives edits that shift line numbers:

```text
@lock_engages  globally, if button.DEPRESSED, then in response lock.ON after 100 milliseconds;
```

## How this compares

| Tool | Input | Output |
| --- | --- | --- |
| PSPWizard, Prospec | pattern chosen in a GUI | a formula to paste elsewhere |
| FRET | structured English | LTL / CoCoSpec for analysis |
| RTAMT, Reelay | STL / MTL formulas | library-hosted monitor |
| MonPoly, R2U2 | MFOTL / MTL | monitor for logs or embedded targets |
| **Referee (REF)** | PSP structured English or raw formulas | LLVM-compiled checker, offline or streaming |

The gap Referee fills is the last column of the first two rows: the pattern
catalogue is where requirements are written, but tooling usually hands you a
formula and stops. Here the English phrasing is the *source language of a
compiler*.

## References

- M. B. Dwyer, G. S. Avrunin, J. C. Corbett. "Patterns in Property Specifications
  for Finite-State Verification." ICSE 1999, pp. 411–420. ·
  [The Specification Patterns System](https://matthewbdwyer.github.io/psp/)
- S. Konrad, B. H. C. Cheng. "Real-Time Specification Patterns." ICSE 2005,
  pp. 372–381.
- M. Autili, L. Grunske, M. Lumpe, P. Pelliccione, A. Tang. "Aligning Qualitative,
  Real-Time, and Probabilistic Property Specification Patterns Using a Structured
  English Grammar." IEEE Transactions on Software Engineering 41(7), 2015,
  pp. 620–638.
- A. Pnueli. "The Temporal Logic of Programs." FOCS 1977, pp. 46–57.
- R. Koymans. "Specifying Real-Time Properties with Metric Temporal Logic."
  Real-Time Systems 2(4), 1990, pp. 255–299.
- R. Alur, T. A. Henzinger. "A Really Temporal Logic." JACM 41(1), 1994,
  pp. 181–203.

Full bibliography: [References](references.md).

## Further reading

- [The REF language](language.md) — the whole surface syntax
- [Architecture](architecture.md) — how a `.ref` becomes running code
- [Online monitoring](monitor.md) — the same language over a streaming trace
- [Run traces](run-traces.md) · [Run-trace format](run-trace-format.md) — what you check patterns against
