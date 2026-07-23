# Door, button, lock and alarm

The worked example from a specification written against an older grammar,
translated to current REF. **All eight requirements pass** against the nominal
trace -- but getting there surfaced three requirements that were wrong as
first written, which is the interesting part; see *Three that were wrong*
below.

```bash
./build/referee execute examples/door/door.ref examples/door/nominal.csv
```

## The system

> After the button is depressed the lock becomes unlocked for 2 seconds.
> The door can be opened only while the lock is unlocked.
> If the door is open for more than 30 seconds the alarm sounds.

`nominal.csv` is 48 states: locked at rest, a press at t=1000, unlocked from
1050 to 3060 (2.01 s), the door opened while unlocked and left open for 24 s —
under the alarm threshold.

## What the run says

All eight pass.

```text
power_up_locked                          PASS
alarm_only_when_open                     PASS
alarm_after_thirty_seconds               PASS
button_unlocks                           PASS
unlocked_at_least_1900ms                 PASS
unlocked_less_than_2100ms                PASS
no_opening_without_unlocking             PASS
no_unlocking_without_button              PASS
```

## Look at it

```bash
./build/referee execute examples/door/door.ref examples/door/nominal.csv \
                --explain examples/door/nominal.ndjson

python3 tools/view.py       examples/door/nominal.ndjson -o nominal.html
python3 tools/view_bokeh.py examples/door/nominal.ndjson -o nominal.bokeh.html
```

Both are committed beside the trace: `nominal.html` (69 KB, static) and
`nominal.bokeh.html` (1.7 MB, hover and zoom, BokehJS inlined so it opens
offline).

Every signal comes out **sparse** — a door system changes state a few dozen
times across 48 states, so `alarm` is a single entry and `door` is two. That is
the encoding heuristic doing what it is for, and it is visible in the file
rather than only in a benchmark.

Each requirement draws a **verdict band** and, beside it now, the interval its
scope was actually open:

**Scope ranges.** `scope.active` is emitted, derived from each pattern's scope
condition. Here `power_up_locked` (`before button.DEPRESSED`) is open only over
`[0, 2)` — the states before the first press — while the `globally` requirements
span the whole `[0, 48)`, and `alarm_after_thirty_seconds` (`while
door.OPENED`) opens at `[7, 48)` where the door is open. A requirement whose
`active` came out empty would be marked vacuous with `scope_never_opened`; none
here is, because the nominal trace exercises every scope. That is the point of
the ranges beside the verdicts: a requirement that passed over an empty scope
reads exactly like one that was satisfied, and only the empty band tells them
apart.

**Per-subexpression rows** are emitted for bare requirements — the operands of
the outermost operator, so a compound requirement shows which side gave way.
This spec is written entirely in Dwyer patterns, which quantify over the whole
trace and so carry the verdict and scope rather than a per-state column; a
`G(a && b)` written directly would draw `a` and `b` as their own rows.

## `__time__` is in nanoseconds

Worth stating early, because getting it wrong here cost real time and produced
a confident, wrong bug report. A bound written `1 milliseconds` is 1 000 000
time units. A trace stepping by 1000 spans *microseconds*, so every
minimum-duration requirement fails and every maximum-duration one passes —
which reads exactly like a broken pattern rather than a mis-scaled trace.

`nominal.csv` steps in milliseconds and multiplies by 1 000 000.

## Three that were wrong, and how

**Scoping precedence at the event it looks for.** Two requirements were written
`after lock.ON, if X, then Y must have occurred before it`. Precedence searches
for the earlier event *within its scope*, so scoping at the unlock put the very
`lock.OFF` and `button.DEPRESSED` being looked for outside it. Both were only
satisfiable by a second cycle. Scoped `globally`, both pass.

**A window longer than the one intended.** `between door.CLOSED and lock.OFF,
the door stays closed` reads as "the door must not open before it is unlocked"
and means "must not open between closing and the next locking" — an interval
that contains the legitimate opening, so it forbade the one thing the system
exists to allow.

**A state where an event was meant.** Its replacement, `never door.OPENED &&
lock.OFF`, failed too — and correctly. The trace leaves the door open after the
lock re-engages two seconds later, which is what a real door does. "The door
can be opened only when unlocked" is about the *moment of opening*; written as
a prohibition on the pair it also forbids remaining open. English uses one word
for the event and the state, and the requirement has to pick. The precedence
rule already says the right thing, so the state version was deleted rather than
repaired.

All three are the mirror of vacuity: a requirement too strong fails on correct
behaviour, where one too weak passes on incorrect behaviour. The second is more
dangerous, but the first still costs a day spent suspecting the system.

## Not done

Failure traces, and checking that each requirement can be made to fail on
purpose. Every requirement in `examples/mctp/` was verified that way — a
requirement that only ever passes is indistinguishable from one that means
nothing — and this example has not had that treatment yet.
