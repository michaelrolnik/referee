# The duration patterns are constant

**Status:** open, measured, not fixed. Found while translating the door example.
**Severity:** one of them always passes, which makes every requirement using it vacuous.

## The measurement

A trace where `lock.ON` holds for a full ten seconds:

```text
globally, once lock.ON becomes satisfied it remains so for at least 1 milliseconds;      FAIL
globally, once lock.ON becomes satisfied it remains so for at least 99999 milliseconds;  FAIL
globally, once lock.ON becomes satisfied it remains so for less than 1 milliseconds;     PASS
globally, once lock.ON becomes satisfied it remains so for less than 99999 milliseconds; PASS
```

Neither pattern responds to its bound. `specMinimunDuration` always fails and
`specMaximumDuration` always passes, whatever the number and whatever the
trace.

Reproduce with any two-state trace:

```text
type State : enum { ON, OFF };
data lock : State;
globally, once lock.ON becomes satisfied it remains so for at least 1 milliseconds;
```

```csv
__time__,lock
0,OFF
1000,ON
11000,ON
12000,OFF
```

## Which is worse

The maximum-duration one, by a distance. A pattern that always fails is
noticed the first time someone uses it — that is how this was found. A pattern
that always passes is a requirement that proves nothing, and looks exactly like
one that works.

That is the failure this project keeps meeting, now in the tool itself rather
than in a specification written with it: a check that quietly stops checking
while still reporting green.

Both are in the Dwyer vocabulary and both are documented, so anyone writing

```text
globally, once alarm.ON becomes satisfied it remains so for less than 5 seconds;
```

has a green requirement that has never tested anything.

## Where to look

`specMinimunDuration` and `specMaximumDuration` in `core/referee.g4`, and
whatever lowers them. The grammar accepts both spellings; the question is what
they desugar to and whether the bound reaches it at all. That the result is
*constant* — insensitive to both the bound and the trace — suggests the
duration is not being compared rather than being compared wrongly.

## Why it survived

Nothing in `test/logic/` uses either pattern. They are in the grammar, in the
editor's keyword list, in the snippets, and in the README's pattern table, so
they read as supported. The MCTP examples reach for `G`, `F`, response and
precedence, and never a duration.

A pattern with no fixture is a pattern nobody has run. Worth checking the rest
of the Dwyer vocabulary the same way — by writing one requirement per pattern
against a trace built to violate it, and confirming it fails.
