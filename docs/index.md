---
title: Referee
description: "Referee (REF) — a runtime-verification language and compiler: write Dwyer specification patterns in English, compiled via ANTLR4 + LLVM IR to a monitor that checks LTL/MTL properties over traces and logs."
---

# Referee

Write a behavioural requirement in structured English, and Referee compiles it to a checker:

```text
globally, it is never the case that door.CLOSED && alarm.ON;
globally, if button.DEPRESSED, then in response lock.ON after 100 milliseconds;
after lock.ON, if door.OPENED, then it must have been the case that lock.OFF has occurred before it;
```

**Referee (REF)** is a C++ compiler toolchain for the **REF** language — built to make
formal requirement verification practical for real systems. You write requirements as
**Dwyer specification patterns** — absence, existence, universality, response, precedence,
and their real-time variants — in structured English; Referee parses them with **ANTLR4**,
builds an AST, and lowers them to optimized **LLVM IR**, so a requirement compiles to an
executable **monitor** that runs **online over traces and logs**.

The patterns desugar to **LTL / MTL** temporal-logic formulas (LTL/TPTL-style reasoning),
expressing behavioural constraints clearly and unambiguously, in a form suitable for
automated checking. See **[Dwyer specification patterns in REF](specification-patterns.md)**
for the full catalogue and the formula each desugars to.

## Architecture

- [Architecture](architecture.md) — the compiler pipeline end to end

## Runtime monitoring & checkers

- [Online monitoring](monitor.md) — a verdict as the trace unfolds
- [Building the monitor](monitor-implementation.md) — the implementation plan
- [Native checkers](native-checkers.md) — ahead-of-time compiled checkers

## Language & semantics

- [The REF language](language.md) — the whole surface syntax: statements, declarations, types, operators, temporal operators, specification patterns
- [Dwyer specification patterns in REF](specification-patterns.md) — the pattern × scope grid (absence, existence, response, precedence, chains) and the LTL/MTL each desugars to
- [Bounded quantifiers](quantifiers.md) — quantification over arrays
- [Ragged arrays](ragged-arrays.md) — `T[]`, whose extent comes from the trace
- [External functions](external-functions.md) — calling out from REF
- [References](references.md) — the temporal-logic and pattern literature behind it

## Traces

- [Run traces](run-traces.md) — what they are, and why they are mostly not about failures
- [Run-trace format](run-trace-format.md) — the on-the-wire format
- [Trace expectations](trace-expectations.md) — expressing what a trace should show

## Implementation notes

- [Accumulator cost](accumulator-cost.md) — why accumulators go quadratic under a temporal scope
- [Signal-node leak](signal-node-leak.md) — a bug: AST signal nodes leaking between specifications

Source: [github.com/michaelrolnik/referee](https://github.com/michaelrolnik/referee)
