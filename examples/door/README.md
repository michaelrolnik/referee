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

| requirement | |
| --- | --- |
| `power_up_locked` | PASS |
| `alarm_only_when_open` | PASS |
| `alarm_after_thirty_seconds` | PASS |
| `button_unlocks` | PASS |
| `unlocked_at_least_two_seconds` | **FAIL** |
| `unlocked_less_than_two_one` | PASS |
| `door_stays_shut_until_unlocked` | **FAIL** |
| `no_opening_without_unlocking` | **FAIL** |
| `no_unlocking_without_button` | **FAIL** |

## Two are over-specified, and reading them shows why

`door_stays_shut_until_unlocked` says *between `door.CLOSED` and `lock.OFF`,
the door stays closed*. The trace opens the door at 1450 while the lock is ON,
which is inside that window — so the requirement forbids the one thing the
system exists to allow. It reads like "the door must not open before it is
unlocked" and means "the door must not open between closing and the next
locking", which is a longer interval containing the legitimate opening.

`no_opening_without_unlocking` and `no_unlocking_without_button` are scoped
`after lock.ON`. Precedence then looks for the earlier event *within that
scope*, and the qualifying `lock.OFF` and `button.DEPRESSED` happen before the
scope opens. As written they can only be satisfied by a second unlock cycle.

Both are the same shape of error, and it is the mirror of vacuity: a
requirement that is too strong fails on correct behaviour, where one that is
too weak passes on incorrect behaviour. The second is more dangerous because
nothing draws attention to it — but the first still costs a day if you assume
the system is wrong.

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
