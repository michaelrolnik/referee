---
title: "Temporal operators in REF"
description: "The REF temporal operator set — LTL G/F/X/U/R, past-time H/O/Y/S/T, strong and weak variants on finite traces, MTL bounded windows, accumulators and the TPTL freeze."
---

# Temporal operators in REF

Specification patterns are surface syntax; underneath them is an ordinary
temporal logic you can write directly. Patterns and formulas mix freely — a
pattern's operands may be formulas, and a formula may name a computed signal
that was itself defined with a pattern's vocabulary.

Every example below is a line from `test/logic/docs_temporal_operators.ref`,
checked against a fixture trace on every build.

## The operator set

| Future | Past | Meaning |
| --- | --- | --- |
| `G(p)` | `H(p)` | `p` at every state (globally / historically) |
| `F(p)` | `O(p)` | `p` at some state (eventually / once) |
| `Xs(p)` / `Xw(p)` | `Ys(p)` / `Yw(p)` | strong / weak next / yesterday |
| `Us(p,q)` / `Uw(p,q)` | `Ss(p,q)` / `Sw(p,q)` | strong / weak until / since |
| `Rs(p,q)` / `Rw(p,q)` | `Ts(p,q)` / `Tw(p,q)` | strong / weak release / triggered |
| `Itg(v)` / `Itg(c,v)` | — | integral over time of numeric `v`, while `c` holds |
| `Sum(c,v)` | — | total of `v` over the states where `c` holds |
| `Cnt(c)` | — | count of the states where `c` holds |

The past-time half is not a convenience. It is what lets **precedence** read as
`G(p -> O(s))` rather than the contorted future-only encoding the Dwyer
mappings need, and it is why "X requires that Y already happened" is a
one-liner in REF.

## Strong and weak, and why finite traces need both

A trace ends. That is the whole reason every next/until/release operator comes
in two flavours.

`Xs(p)` — **strong** next — is false at the last state: there is no next state,
so the claim fails. `Xw(p)` — **weak** next — is true there: there is no next
state, so nothing contradicts the claim. Same recurrence, opposite boundary.

Pick by what the requirement means when the log runs out. "The next state
acknowledges" is strong: a trace that ends before the acknowledgement did not
satisfy it. "Nothing after this violates the invariant" is weak: a trace that
ends immediately has nothing to violate it.

This is [LTLf](references.md) — linear temporal logic on finite traces — and it
is why a formula that is valid in a textbook can behave differently against your
log.

## Worked examples

Against a five-state trace where `a` holds at states 1–2, `b` at states 2–3, and
`v` counts `1..5`:

```text
@eventually_a          F(a);
@always_positive       G(v > 0);
@a_before_b            !b && F(a);
@b_holds_after_a       G(b => O(a));
@a_until_b             Us(!b, b);
@strong_next           Xs(a);
```

A statement with no scope is evaluated at the trace's first state, so
`@strong_next` asks whether `a` holds at state 1, and `@a_until_b` asks whether
`b` stays false until it becomes true.

## Bounded windows

Any operator takes an MTL-style `[lo:hi]` window, in the same time units as
`__time__` — nanoseconds:

```text
@a_within_200ms        F[0:200000000](a);
```

One-sided bounds work too: `[lo:]` and `[:hi]`. A bound may be a `conf` value
rather than a literal, which is how you keep a timing budget in one place:

```text
conf deadline : integer;

G(Us[0:deadline](pending, done));
```

Note the two spellings of a time bound. Inside a **formula** the window is
`[lo:hi]` in nanoseconds; inside a **specification pattern** it is English —
`within 250 milliseconds` — and the unit keyword does the scaling. They mean the
same thing.

## Accumulators

`Cnt`, `Sum` and `Itg` walk the trace and return a value rather than a verdict:

```text
@count_of_a            Cnt(a) == 2;
@total_v_while_a       Sum(a, v) == 5;
@records               Cnt(true) == 5;
```

The distinction that matters: `Sum(c, v)` weights every selected state equally,
`Itg(c, v)` weights it by how long the state lasted. Message lengths and packet
counts are discrete and want `Sum`; "how long was the valve open" is continuous
and wants `Itg`. States where the condition fails are **skipped, not stopped
at** — the walk carries on past them.

Accumulators have a cost model of their own; see
[Accumulator cost](accumulator-cost.md).

## Computed signals

Naming a sub-formula evaluates it once per state for the whole trace, so a
sub-formula shared by ten requirements costs one pass rather than ten:

```text
data seen_a    = O(a);
data both      = a && b;
data next_both = Xs(both);
```

This also matters for **meaning**, not only cost. A temporal operator written
directly inside a scoped pattern reads only that scope's segment; the same
operator named as a computed signal keeps its whole-trace reading:

```text
after c, it is never the case that O(a) holds;      # `a` since the scope opened
after c, it is never the case that seen_a holds;    # `a` anywhere in the trace
```

Declaration order matters — no forward or circular references — and computed
signals are not stored in `.rdb` files, because they are a property of the
specification rather than of the recording.

## The freeze operator

REF implements TPTL's freeze quantifier, which binds a name to the state at
which a formula is evaluated so an inner formula can refer back to it:

```text
G(t@(Ss(a, t.b)) == b);
```

Inside `t@(...)`, `t.b` is `b` **at the frozen state** — constant for the whole
of the inner scan — while a bare `b` is `b` at whichever state the scan has
reached. That difference is the entire point of the operator, and it is also why
a freeze changes how the expression can be lowered. See
[The REF language](language.md) for the semantics and
[Architecture](architecture.md) for the consequences.

## Further reading

- [Dwyer specification patterns in REF](specification-patterns.md) — the English
  layer over these operators
- [Requirements cookbook](cookbook.md) — which shape fits which requirement
- [The REF language](language.md) — full syntax, precedence, types
- [References](references.md) — Pnueli, Koymans, Alur & Henzinger, De Giacomo &
  Vardi
