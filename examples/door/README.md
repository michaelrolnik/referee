# Door, button, lock and alarm

The worked example from a specification written against an older grammar,
translated to current REF. **All nine requirements compile. Four fail against a
trace built to be nominal**, and that is the interesting part — the file is
committed in that state deliberately, because the failures are findings rather
than mistakes to tidy away.

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

Both are committed beside the trace: `nominal.html` (35 KB, static) and
`nominal.bokeh.html` (1.6 MB, hover and zoom, BokehJS inlined so it opens
offline).

Every signal comes out **sparse** — a door system changes state a few dozen
times across 48 states, so `alarm` is a single entry and `door` is two. That is
the encoding heuristic doing what it is for, and it is visible in the file
rather than only in a benchmark.

Each requirement draws a **verdict band** — a value over the whole trace, which
is what a verdict is — and nothing more yet. Two things are still missing, both
on referee's side rather than the viewers':

**Scope ranges.** `scope.active` would shade where each requirement's scope was
open, which is the interval you actually want beside a verdict. Referee does
not emit it, deliberately: an empty `active` means *never opened*, which is the
vacuity signal, so writing one speculatively would mark everything vacuous.
Emitting it truthfully needs the pattern's scope kind carried from the AST
through to where verdicts are computed, which is plumbing rather than
guesswork.

**Per-subexpression rows.** Those wait on the column evaluator. Until then the
signals section is the picture, and the verdict band says which requirement
decided what.

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
