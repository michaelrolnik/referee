---
title: "Requirements cookbook"
description: "Worked temporal requirements for real systems — watchdogs, startup ordering, bounded response, mode invariants, debounce and acknowledgement — written as Dwyer specification patterns in REF."
---

# Requirements cookbook

Requirements that come up in real systems, written as REF specification
patterns. Every recipe here is a line from `test/logic/docs_cookbook.ref`, which
the test suite runs against `docs_cookbook.csv` on every build — so these
compile and pass rather than merely looking plausible.

The signals are declared once:

```text
data self_test_passed : boolean;
data motor_on         : boolean;
data request          : boolean;
data reply            : boolean;
data in_flight        : boolean;
data door_open        : boolean;
data relay_on         : boolean;
data alarm            : boolean;
data ack              : boolean;
data reset            : boolean;
data charging         : boolean;
data full             : boolean;
```

## Nothing starts before the self-test

*The motor must never run unless the power-on self-test has already passed.*

```text
@motor_after_selftest
globally, if motor_on, then it must have been the case that self_test_passed has occurred before it;
```

This is the **precedence** pattern. Because REF has past-time operators it reads
directly as `G(motor_on -> O(self_test_passed))` — no encoding tricks. Precedence
is the pattern for every "X requires that Y already happened" clause, which in
practice is most of a startup or interlock specification.

## A request gets an answer, in time

*Every request is answered within 250 ms.*

```text
@reply_in_time
globally, if request, then in response reply within 250 milliseconds;
```

The **bounded response** pattern — the single most common real-time requirement
there is. Drop `within 250 milliseconds` and you get the qualitative Dwyer
response (`G(p -> F(q))`): the answer must come, eventually, with no deadline.

Bounds come in three shapes: `within N units` (deadline), `after N units`
(the response must not come sooner), and `between N and M units` (a window).

## An invariant that only applies in one mode

*While in flight, the door must be shut.*

```text
@door_shut_in_flight
while in_flight, it is always the case that !door_open;
```

`while P,` opens a scope over every maximal stretch where `P` holds, and the
body is checked over each of them independently. This is how you say "only when
the system is in this mode" without the mode leaking into the property itself.

The other scopes: `before P,`, `after P,`, `between P and Q,`, and
`after P until Q,` — the last differing from `between` in that it also checks a
final segment that never closes.

## Debounce: once it engages, it stays engaged

*The relay must hold for at least 50 ms once it closes.*

```text
@relay_debounce
globally, once relay_on becomes satisfied it remains so for at least 50 milliseconds;
```

The **minimum duration** pattern. Its mirror, `it remains so for less than N
units`, is **maximum duration** — for things that must not latch. Neither has a
one-line LTL reading; they are about how long a condition survives once entered,
and lower to bounded operators over the trace's timestamps.

## An alarm must be acknowledged, and not by a reset

*Every alarm is acknowledged within five seconds, and a reset in between does
not count.*

```text
@alarm_acked
globally, if alarm, then in response ack within 5 seconds without reset holding in between;
```

`without Z holding in between` is a **constraint**, and it is what separates a
requirement that means something from one that is trivially satisfiable. Without
it, a system that resets and clears the alarm would pass.

## A phase that runs to completion

*Charging continues uninterrupted until the battery is full.*

```text
@charge_until_full
globally, charging holds without interruption until full holds;
```

The **until** pattern. Note what it does not say: it does not require `full` to
ever arrive. If you need that too, pair it with an existence requirement
(`globally, full eventually holds;`).

## Counting and totalling

Not every requirement is a temporal shape. REF has accumulators that walk the
trace directly:

```text
Cnt(k.SOM)      == 2;       # states where the condition holds
Sum(k.MID, len) == 98;      # total of a value over those states
Itg(valve_open, flow)       # integral over *time*, not records
```

`Sum` weights each contributing state equally and `Itg` weights it by duration —
which is the difference between "how many bytes were in this message" and "how
long was the valve open". Choosing the wrong one is a quiet, plausible-looking
bug. See [Accumulator cost](accumulator-cost.md) for the evaluation cost.

## Naming requirements

Give every requirement a name:

```text
@motor_after_selftest
globally, if motor_on, then it must have been the case that self_test_passed has occurred before it;
```

Named requirements are reported by name rather than by source position, so a
corpus of traces keeps pointing at the right requirement after you edit the file
above it. Names must be unique across the program, imports included.

## Further reading

- [Dwyer specification patterns in REF](specification-patterns.md) — the full
  catalogue and the scope grid
- [Temporal operators](temporal-operators.md) — for when a pattern is not the
  right shape and you want the formula
- [Getting started](getting-started.md) — install, build, first trace
- [Trace expectations](trace-expectations.md) — declaring what a trace should show
