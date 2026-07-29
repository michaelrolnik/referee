---
title: The REF language
---

# The REF language

REF is a small domain-specific language for describing system requirements as
properties over timed traces. This page is the whole surface syntax, in the
order the grammar defines it; the authoritative grammar is
[`src/core/referee.g4`](https://github.com/michaelrolnik/referee/blob/main/src/core/referee.g4).
Where a topic has a design note of its own — quantifiers, ragged arrays,
external functions, accumulator cost — it is linked from the relevant section.

## Program structure

A program is a semicolon-terminated sequence of statements:

```text
program   : (statement ';')* EOF
statement : ('@' name)? (declaration | expression | specPattern)
```

Three kinds of statement:

- **Declarations** (`type`, `data`, `conf`, `import`, `func`) — the vocabulary.
- **Expressions** — temporal-logic formulas that must hold on the trace.
- **Specification patterns** — Dwyer-style English phrasings that desugar to formulas.

Every non-declaration statement is a **requirement**: the compiler emits one
function per statement, and each must evaluate to `true` on every valid trace.
Declarations emit no functions of their own.

**Naming a requirement.** A leading `@` binds a stable name, so a trace corpus
can say which requirement it is meant to violate without citing a line number
that moves:

```text
@door_closed_when_moving  globally, it is never the case that door.OPENED && moving;
@"REQ-14.2"               G(speed <= limit);
```

Unnamed requirements are reported by source position (`13:0 .. 13:21`), and by
`file.ref:row:col .. row:col` when they arrive through an `import`.

## Lexical details

- **Comments:** `//`, `#`, and `/* ... */`.
- **Identifiers:** `[a-zA-Z_][a-zA-Z0-9_]*`.
- **Whitespace** is insignificant; `;` terminates a statement.
- **Boolean literals:** `true`, `false`.
- **Integer literals:** decimal `42`, binary `0b1010`, octal `0o755`, hex `0xFF`.
  Arbitrary-width at the source level, lowered to 64-bit.
- **Floating-point literals:** `1.5`, `.25`, `3.14e-2`, `1E6` — the `number` type.
- **String literals:** `"..."` over letters, digits, `_ . ? ! / -` and space (the
  last three so `import` can name a path). Strings compare with `==` / `!=`.
- **Signed literals:** a leading `+`/`-` on a numeric literal is part of the
  literal, so `-3` is a constant while `- x` is unary negation.
- **Reserved words:** the temporal operators (`G F Xs Xw Us Uw Rs Rw H O Ys Yw Ss
  Sw Ts Tw`), the accumulators (`Itg Sum Cnt`), the quantifiers (`all some none
  one most least`), and the pattern vocabulary: `after afterwards always and at
  becomes been before between by case continually eventually every followed for
  globally has have holding holds if in interruption is it least less long must
  never occurred once remains repeatedly response run satisfied so than that the
  then until while within without`, plus the units `nanoseconds microseconds
  milliseconds seconds minutes`.

## Declarations

```text
type Name : T;              // a named type; no runtime storage
data Name : T;              // a time-varying signal — a field of each trace record
data Name = expression;     // a computed signal — derived, not recorded
conf Name : T;              // a value constant for the whole run
func Name : (T, …) -> T;    // an external native function
import "path.ref";          // fold another file in here
```

`data` versus `conf` is a modeling choice that the compiler turns into layout:
per-event signals become columns of the trace record, run-constant values become
fields of the configuration.

### `import`

```text
import "common/types.ref";
import "reqs/door.ref";
```

A relative path resolves against the importing file's directory, then against
each `-I <dir>` on the command line. Each file is imported once, keyed on its
real path, so a diamond is ordinary rather than a redeclaration error; a cycle
is reported rather than silently skipped. Requirements from an imported file are
compiled alongside the root file's own and are labelled file-qualified.

### Computed signals

```text
data both      = a && b;      // point-wise
data seen_a    = O(a);        // temporal operators are allowed
data next_both = Xs(both);    // computed signals may build on each other
```

The type is inferred. A computed signal is evaluated once per state for the whole
trace, so a sub-formula used by ten requirements costs one pass, not ten — which
is also how a temporal operator keeps its whole-trace meaning, and its linear
cost, inside a scoped specification pattern (see
[Temporal operators in a pattern](#temporal-operators-in-a-pattern)). Two
consequences: **declaration order matters** (no forward or circular references),
and computed signals are **not stored in `.rdb` files** — they are a property of
the specification, recomputed from the `.ref` at run time.

### External functions

```text
func crc8 : (byte[], integer) -> byte;
func std::math::sqrt : (number) -> number;   // `::` namespacing, any depth
func packet_ok : (__state__) -> boolean;     // whole-state calling convention

data pec_ok = crc8(pkt[0:len], 0) == pkt[len - 1];
```

The implementation is a `.so` loaded at run time from the `-L` search path;
symbols carry a `referee_` prefix (`func crc8` binds to `referee_crc8`). Arrays
cross as a `{count, data}` descriptor, structs by `const` pointer, everything
else by value. `referee header` emits the matching C header, and with `--stub`
an implementation skeleton. See [External functions](external-functions.md).

## Types

```text
type      : 'struct' '{' (ID ':' type ';')* '}'
          | 'enum'   '{' (ID (',' ID)*)? '}'
          | typeID
          | type '[' size? ']'
```

- **Primitives:** `boolean`, `byte` (8-bit unsigned), `integer` (64-bit signed),
  `number` (floating point), `string`.
- **Enumerations:** `enum { A, B }` — nominal; two enums with the same members
  are distinct types. Members are reached **through a signal**, `lock.ON`, never
  through the type (`State.ON` does not parse as a value).
- **Structures:** `struct { x: number; y: number; }`, nestable.
- **Arrays:** `T[N]` is a fixed extent, stackable as `T[N][M]`. Dimensions read
  as in C: `integer[3][2]` is three arrays of two, and `g[2][1]` is the last
  element. `T[]` is **unbounded** — each record carries its own length — and
  `T[][]` is ragged in both dimensions. See [Ragged arrays](ragged-arrays.md).
- **Aliases:** any `type Name : …` is usable wherever a type is expected.

```text
type Button : enum   { DEPRESSED, RELEASED };
type Point  : struct { x: number; y: number; };
type Matrix : Point[3][3];

data  button : Button;
data  pos    : Point;
data  pkt    : byte[];
conf  xyz    : struct { a: integer; b: number; limits: integer[4]; };
```

Member access is `.`, indexing is `[...]`, and they nest: `abc.x[1][2]`,
`grid[1][2].x`, `xyz.limits[2]`.

**`byte` is a storage width, not a value kind.** It occupies one octet per row
and every read widens to `integer`, so `(flag & 0x80) != 0` type-checks with no
cast and `Sum(true, payload[0])` totals bytes as integers. A cell outside
`0..255` is refused at load time rather than truncated.

## Expressions

**Precedence, tightest first** — C++/Kotlin order, with `=>` and `<=>` where
logic conventionally puts them:

| Category | Operators | Notes |
| --- | --- | --- |
| Postfix | `.field`, `[i]`, `[lo:hi]`, `.count` | member, index, half-open slice, element count |
| Unary | `!` (`not`), `-`, `~` | logical, arithmetic, bitwise complement |
| Multiplicative | `*`, `/`, `%` | `%` is integer modulo |
| Additive | `+`, `-` | |
| Shift | `<<`, `>>` | integers only; `>>` is arithmetic |
| Relational | `<`, `<=`, `>`, `>=` | |
| Equality | `==`, `!=` | booleans, numbers, strings, enums |
| Bitwise | `&`, `^`, `\|` | integers only |
| Logical | `&&` (`and`), `^^` (`xor`), `\|\|` (`or`) | booleans only |
| Implication | `=>`, `<=>` | `=>` is right-associative |
| Ternary | `c ? a : b` | both arms need a common type |
| Quantifier | `all x in v: body` | loosest — body runs to `)` or `;` |
| Grouping | `(...)` | |

So `2 + 3 * 4` is 14, `a || b && c` is `a || (b && c)`, and `a => b => c` is
`a => (b => c)`.

**Bitwise and logical are separate families.** `a & b` on booleans is a type
error, and so is `a && b` on integers. Each logical operator has an English word
alias — `and`, `or`, `xor`, `not` — the same operator under a second spelling.

The precedence is C's exactly, including the part everyone trips over: `&`, `^`
and `|` bind looser than `==`, so `flag & 0x80 != 0` reads as
`flag & (0x80 != 0)`. C computes that silently; REF rejects it. Write
`(flag & 0x80) != 0`.

**`&&`, `||`, `=>` and `? :` short-circuit** — they lower to branches, not to a
`select` over both operands. That is what lets a guard guard: `x != 0 => y / x > 1`
does not divide by zero, and `n.count > 0 && n[0] > 0` does not index an empty
array.

**Indexing is bounds-checked.** A constant index past a written extent is a
compile-time error; a runtime index past the length fails the requirement and
names the index and the count.

**Types in expressions.** `integer` and `number` mix with the usual widening,
including in comparisons. Otherwise comparisons are homogeneous — two strings,
two booleans, two values of the same enum. Checking happens before IR generation,
so type errors are compile-time.

### `.count`

```text
limits.count == 4;          // sized: folds to a literal, no load
grid[0].count == 2;         // the inner dimension
pkt.count <= 64;            // unbounded: a load of the record's own length
```

`count` is resolved by the type it is applied to, so a struct field of the same
name is unaffected.

### Quantifiers

```text
all  limit in limits: limit < max;      // every element
some limit in limits: limit < max;      // at least one
none limit in limits: limit < max;      // not any
one  limit in limits: limit < max;      // exactly one
at least 2 limit in limits: limit < max;
at most  2 limit in limits: limit < max;
```

The body extends to the end of the statement, so parenthesise when something
follows: `!(all x in v: x > 2)`. One binder names the element, a second names the
index, and `_` discards either:

```text
all x, i in v: x * 10 == w[i];      // correlate two arrays
all _, i in v: w[i] > v[i];         // index only
all row in g: all p in row: p > 0;  // nest for more dimensions
```

Quantifiers and temporal operators compose in both orders and mean different
things: `all p in xs: G(P(p))` is per-element-forever, `G(all p in xs: P(p))` is
per-state-all-elements. Over a sized array the quantifier expands at AST-build
time and costs nothing at run time; over `T[]` it lowers to a runtime loop, and a
temporal operator *inside* such a quantifier is rejected. See
[Bounded quantifiers](quantifiers.md).

### Freeze

`name@(...)` binds the current state to `name`, so subexpressions can refer to
data at that frozen point. `__time__` is the current timestamp, `name.__time__`
the frozen one, and `name.elapsed` desugars to `__time__ - name.__time__`:

```text
// a problem must be followed by an alarm within 5 seconds
G(problem => t@(F(alarm && t.elapsed <= 5)));
```

### Temporal operators

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

`Xs` / `Xw` / `Ys` / `Yw` take an optional integer **repeat count** as a first
argument: `Xs(3, p)` is `p` three states ahead, and `Xs(p)` is `Xs(1, p)`.

**Time bounds.** Every operator except the nexts accepts `[lo:hi]`, `[:hi]` or
`[lo:]`, giving the MTL-style bounded forms `G[100:1000](a)`, `Us[1:3](a, b)`,
`Cnt[0:5000](c)`. The window is `[t + lo, t + hi]` against the trace's own clock,
`t` being the timestamp at the evaluation point. Only the bounded forms consult
timestamps at all; the unbounded operators step from sample to sample.

**Semantics.** A formula is true or false *at a state*, over the states from
there onward (future) or up to there (past); a requirement is the formula at the
first real state. The trace is finite, and the strong/weak distinction is
entirely about what happens when it ends before the operator is decided: a
strong operator seeds `false` past the end (an undischarged obligation is
unmet), a weak one seeds `true` (an untestable obligation is forgiven). So
`Xs(p)` is false at the last state and `Xw(p)` is true there; `Us(p,q)` requires
`q` to actually occur, `Uw(p,q)` forgives its absence. Release and trigger are
the duals — `Rw(p,q) = ¬Us(¬p,¬q)`, `Tw(p,q) = ¬Ss(¬p,¬q)` — and `G(p)`
canonicalizes to `Rw(false, p)`.

**Accumulators** share one shape: a condition selects the contributing states,
states where it fails are skipped rather than ending the walk, and `[lo:hi]` sets
the extent. What differs is the weight — duration for `Itg`, a value for `Sum`,
one for `Cnt` (so `Cnt(c)` is `Sum(c, 1)`). Without a window the walk runs to the
end of the trace.

```text
G(k.SOM => Cnt[0:5000](k.MID) <= 8);        // at most 8 packets per message
G(k.SOM => Sum[0:5000](true, len) <= 4096); // and at most 4096 bytes
```

See [Accumulator cost](accumulator-cost.md) for why an accumulator under a
temporal scope can go quadratic.

## Specification patterns

A pattern statement is a **scope**, then a **body**. Scopes nest to the left of
the body, separated by commas:

```text
globally, …
before P, …
after P, …
while P, …
between P and Q, …
after P until Q, …
```

The bodies, in grammar order (`?` marks optional words):

| Pattern | Form |
| --- | --- |
| Universality | `it is always the case that P holds? <bound>` |
| Absence | `it is never the case that P holds? <bound>` |
| Existence | `P eventually holds? <bound>` |
| Transient state | `P holds after N units` |
| Steady state | `P holds in the long run` |
| Minimum duration | `once P becomes satisfied? it remains so for at least N units` |
| Maximum duration | `once P becomes satisfied? it remains so for less than N units` |
| Recurrence | `P holds? repeatedly (every N units)?` |
| Precedence | `if P holds?, then it must have been the case that S has occurred? <interval> before it?` |
| Precedence chain 1-2 | `if S and afterwards T <upper> holds?, then it must have been the case that P has occurred? <interval> before it?` |
| Precedence chain 2-1 | `if P holds?, then it must have been the case that S and afterwards T <upper> have occurred? <interval> before it?` |
| Response | `if P has occurred?, then in response S eventually holds? <bound> <constraint>` |
| Response chain 1-2 | `if P has occurred?, then in response <bound> <constraint> S followed by T <bound> <constraint> eventually holds?` |
| Response chain 2-1 | `if S followed by T <bound> <constraint> have occurred?, then in response P eventually holds? <bound> <constraint>` |
| Response invariance | `if P has occurred?, then in response S holds? continually <bound>` |
| Until | `P holds? without interruption until S holds? <bound>` |

**Time bounds** are `within N units`, `after N units`, `between N and M units`,
or nothing at all. `units` is one of `nanoseconds`, `microseconds`,
`milliseconds`, `seconds`, `minutes`.

**Constraints** are `without Z holding in between`, or nothing.

```text
before button.DEPRESSED, lock.ON eventually holds after 100 milliseconds;
globally, it is never the case that door.CLOSED && alarm.ON;
while door.OPENED, it is always the case that alarm.ON after 30 seconds;
globally, if button.DEPRESSED, then in response lock.ON after 100 milliseconds;
globally, once lock.ON becomes satisfied it remains so for at least 2 seconds;
between door.CLOSED and lock.OFF, it is always the case that door.CLOSED;
after lock.ON, if door.OPENED, then it must have been the case that lock.OFF has occurred before it;
```

Each such line compiles to the same kind of boolean-valued function over the
trace as a raw formula does; the patterns are surface syntax for the temporal
logic, not a separate mechanism.

### Temporal operators in a pattern

A pattern's operands — the `P`, `S`, `T` and `Z` above — may be any expression,
temporal operators included, and so may the expressions in a scope. What changes
with the scope is *which states the operator sees*.

Under `globally`, or with no scope at all, a pattern body spans the whole trace
and behaves exactly like the formula it desugars to:

```text
globally, it is never the case that O(a) && !b;   // same as G(!(O(a) && !b))
```

Under a scope — `before`, `after`, `while`, `between … and …`, `after … until …`
— the body is re-evaluated over each segment the scope opens, and a temporal
operator in it **reads only that segment**. `O(...)` looks back no further than
the segment's first state, `F(...)` no further than its last, and an accumulator
totals the segment's states:

```text
after c, it is never the case that O(a) holds;
```

That holds on a trace where `a` occurs only before `c` — the segment starts at
`c`, so `a` is not in its past. Read the scope as choosing the stretch of trace
the pattern is about; the body then means over that stretch what it would mean
over a whole trace of the same shape. `Xs`/`Ys`/`Xw`/`Yw` have always worked
this way, and the unbounded operators now agree with them.

**Cost.** A temporal operator directly under `globally` is buffered as usual and
stays O(N) (see [Accumulator cost](accumulator-cost.md) for the accumulators'
exception). Inside a scope it cannot be — the segment bounds are not known until
the scope loop runs — so it falls back to the scan, which is O(N²) for that
subtree. Where the whole-trace reading is what you meant anyway, naming the
sub-formula as a computed signal keeps it linear and reads better:

```text
data seen_a = O(a);

after c, it is never the case that seen_a holds;   // `a` anywhere in the trace
```

Note the two spellings mean different things: the scoped `O(a)` above asks
whether `a` occurred since the scope opened, `seen_a` whether it occurred at
all.

## What a trace means between samples

A row is a **sample**: a timestamp plus a complete set of values for every `data`
signal. Between two samples the model is **sample-and-hold** — a signal is
piecewise-constant, and a row is where it may change.

- **Only bounded operators observe the clock.** `Xs(a)` means "at the next
  recorded sample", whether that is a millisecond or an hour later. The sampling
  rate is therefore part of what an unbounded requirement means.
- **Rows are not sparse.** An empty cell is *not* carried down from the row
  above; it reads as the type's zero. To say a value changes at time T, add a
  complete row at T.

## Further reading

- [Architecture](architecture.md) — how a `.ref` becomes running code
- [Bounded quantifiers](quantifiers.md) · [Ragged arrays](ragged-arrays.md) · [External functions](external-functions.md) · [Accumulator cost](accumulator-cost.md)
- [Online monitoring](monitor.md) — the same language, evaluated as the trace streams
- [References](references.md) — the temporal-logic and pattern literature
