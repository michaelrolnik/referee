---
title: Referee
---

# Referee

**Referee** is a C++ compiler toolchain for the **REF** language — built to make
formal requirement verification practical for real systems. It parses REF source
with ANTLR4, builds an AST, and lowers programs to optimized LLVM IR, so
human-readable requirement intent compiles to executable checkers that run
**online over traces and logs**.

The language is inspired by temporal-logic verification (LTL/TPTL-style reasoning
and requirement patterns), aimed at expressing behavioral constraints clearly and
unambiguously, in a form suitable for automated checking.

## Architecture

- [Architecture](architecture.md) — the compiler pipeline end to end

## Runtime monitoring & checkers

- [Online monitoring](monitor.md) — a verdict as the trace unfolds
- [Building the monitor](monitor-implementation.md) — the implementation plan
- [Native checkers](native-checkers.md) — ahead-of-time compiled checkers

## Language & semantics

- [The REF language](language.md) — the whole surface syntax: statements, declarations, types, operators, temporal operators, specification patterns
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
