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

Seven of eight pass. The remaining failure is not diagnosed, and is marked as
such rather than tuned away.

| requirement | |
| --- | --- |
| `power_up_locked` | PASS |
| `alarm_only_when_open` | PASS |
| `alarm_after_thirty_seconds` | PASS |
| `button_unlocks` | PASS |
| `unlocked_at_least_two_seconds` | **FAIL — undiagnosed** |
| `unlocked_less_than_two_one` | PASS |
| `no_opening_without_unlocking` | PASS |
| `no_unlocking_without_button` | PASS |

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

## One I could not diagnose

`unlocked_at_least_two_seconds` should hold: the lock is ON from 1050 to 3060,
which is 2.01 s. Whether the trace is subtly wrong, the minimum-duration
pattern measures an interval differently than expected, or something is
genuinely broken, I did not establish — and guessing here would be worse than
leaving it marked.

That is the next thing to look at, and it is worth doing before trusting the
pattern elsewhere.

## Not done

Failure traces, and checking that each requirement can be made to fail on
purpose. Every requirement in `examples/mctp/` was verified that way — a
requirement that only ever passes is indistinguishable from one that means
nothing — and this example has not had that treatment yet.
