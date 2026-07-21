# Design: bounded quantifiers over arrays

**Status:** proposal, not implemented.
**Scope:** `all` / `some` / `one`, plus `at least N` / `at most N`, quantifying over the elements of an array.

## The problem

REF has no way to say "for every element". Arrays are declared with a static size and indexed by an expression, but the size is a literal in the *type* and is not reachable as a value:

```antlr
type        : type '[' size ']'     # TypeArray
size        : integer
```

So a requirement over a four-element array is written four times:

```text
data limits : integer[4];

limits[0] < max;
limits[1] < max;
limits[2] < max;
limits[3] < max;
```

Twenty sensors with three requirements each is sixty hand-written lines, and the failure mode is the one the project exists to avoid: the README's design rationale objects to hand-maintained checkers because they *drift* from the requirement they encode. Sixty copy-pasted requirements drift in exactly the same way — someone edits four of five and nothing complains.

There is also no way to express a cardinality constraint at all: "exactly one valve is open" is a common safety requirement and is currently unwritable except as an explicit pairwise expansion.

## Syntax

```antlr
quantifier  : quant ID (',' ID)? 'in' expression ':' expression
            ;
quant       : 'all'
            | 'some'
            | 'one'
            | 'at' 'least' integer
            | 'at' 'most'  integer
            ;
```

```text
all      limit in limits:  limit < max;
some     limit in limits:  limit < max;
one      limit in limits:  limit < max;          // exactly one
at least 2 limit in limits: limit < max;
at most  2 limit in limits: limit < max;
```

The body extends maximally — to the enclosing `)` or the terminating `;` — as a lambda body does. There is no closing keyword.

### Naming

`all` / `some` rather than `forall` / `exists`. The language's stated audience is people who own the requirements rather than programmers, and its house style is the Dwyer patterns' near-English. `all limit in limits` reads like that; `forall` reads like logic notation.

Cost: two new reserved words. `in`, `at` and `least` are **already reserved**, so the counting forms add none.

### Binder forms

| Written | `x` binds | `i` binds |
| --- | --- | --- |
| `all x in xs:` | element | — |
| `all x, i in xs:` | element | index, an `integer` |
| `all _, i in xs:` | discarded | index |

One binder is the **element**, not the index. The two are syntactically indistinguishable — `all x in xs` and `all i in xs` are the same production — so the one-binder form has to mean one thing, and element is the right choice for two reasons:

- It is what `all limit in limits` obviously means to a reader. The singular/plural convention makes the intent unambiguous to a human even though it is ambiguous to a parser.
- It makes the one-binder form a strict **prefix** of the two-binder form. Dropping the tail leaves the element, which is the common case. Index-first would invert that: `all limit in limits` would silently bind an integer.

This goes against Kotlin (`forEachIndexed { index, element -> }`) and agrees with JavaScript (`forEach((element, index))`). The prefix property decides it.

`_` is accepted as a binder name meaning "discard", which covers the index-only case without a third syntactic form.

### Multi-dimensional arrays

Nest, rather than adding a third binder:

```text
all row in grid:  all p in row:  p.x < 10;
```

`all p, i, j in grid` would raise "is it row-major?" every time it is read, for no gain over nesting.

## Semantics

Each form is defined by expansion over the array's `count`, which is known at compile time. For `xs : T[n]`:

| Form | Expands to |
| --- | --- |
| `all x in xs: P` | `P(xs[0]) && P(xs[1]) && … && P(xs[n-1])` |
| `some x in xs: P` | `P(xs[0]) \|\| … \|\| P(xs[n-1])` |
| `one x in xs: P` | exactly one disjunct true — the pairwise-exclusive expansion |
| `at least k …` | disjunction over the `C(n,k)` k-subsets |
| `at most k …` | negation of `at least k+1` |

`one` is sugar: `at least 1 && at most 1`. It is kept because "exactly one" is common enough in safety requirements to deserve reading well.

An empty array (`n = 0`) gives `all` = true, `some` = false, following the usual vacuous-truth convention.

### Why every form is boolean

An integer-valued `count x in xs: P` was considered and rejected. It cannot be parsed unambiguously with a maximally-extending body:

```text
count limit in limits: limit > 5 > 2
```

is either `count(limit > 5) > 2` or `count(limit > 5 > 2)`, and no precedence rule distinguishes them — the body has to extend maximally for the boolean forms to work. Fixing it needs delimiters (`count(limit in limits: P) > 2`) for that one form only, which is an inconsistency in exchange for a capability that `at least` / `at most` already covers.

If a value-returning count is ever genuinely needed, it can be added later in delimited form without disturbing any of this.

### `at least k` blow-up

The subset expansion is `C(n,k)` terms, which is fine for the sizes REF arrays actually have (a handful) and bad in principle. If it ever matters, the standard fix is a sorting network or a counter chain over the `n` predicates, which is `O(n·k)` and can be substituted without changing the surface language. Not worth building until a real spec needs it.

## Interaction with the temporal layer: none

This is the property that makes the feature cheap, and it is worth stating explicitly.

Because array sizes are static, **quantifiers expand entirely during AST construction**. Nothing reaches `TypeCalc`, `Rewrite`, or `Compile` that those visitors do not already handle: the result is an ordinary tree of `ExprAnd` / `ExprOr` over `ExprIndx` nodes with constant subscripts, which the existing lowering compiles as it always has.

Consequently:

- No change to the evaluation model, the `.rdb` format, or `csvHeaders`.
- No runtime cost — nothing is looped at execution time.
- Quantifiers and temporal operators compose in both orders, for free, and mean different things:

```text
all x in xs:  G(P(x));      // each element satisfies P at every state
G(all x in xs: P(x));       // at every state, every element satisfies P
```

- `i` is a compile-time constant in each unrolled copy, so `xs[i]` is a constant subscript. `visit(ExprIndx*)` already handles that; no new IR is emitted.

## Implementation

The binding machinery already exists. `Antlr2AST::visitExprAt` binds a freeze variable by pushing a name into scope, visiting the body's **parse tree**, and popping:

```cpp
module->pushContext(name);
auto expr = ctx->expression()->accept(this);
module->popContext();
```

A quantifier is that, run once per element. Visiting the parse tree repeatedly with a different binding each time avoids writing an AST substitution pass, which would otherwise be the fiddly part of this feature.

Sketch:

1. `visitQuantifier` resolves the domain expression's type, requires a `TypeArray`, and reads `count`.
2. For each `k` in `0 … count-1`, bind the element name to an `ExprIndx(domain, k)` and the index name (if present) to `ExprConstInteger(k)`, then `accept` the body subtree.
3. Fold the `count` resulting `Expr*` into the shape from the table above.
4. Type-check as usual — the folded tree is ordinary.

Touched: `core/referee.g4` (one rule, two keywords), `core/antlr2ast.{hpp,cpp}` (one visitor method plus a small scope map for value bindings, which `pushContext` does not currently provide — it records names, not bound expressions). Nothing else.

Errors worth reporting at the quantifier rather than after expansion, so the message points at the requirement the author wrote:

- domain is not an array
- domain is a dynamic array (see below)
- `k` in `at least k` exceeds the array size
- binder name shadows a signal, type or freeze variable

## Interaction with dynamic arrays

`TypeArray` already models `count == 0` as "dynamic", with a `{i16 length, ptr}` layout, and `CompileTypeImpl::visit(TypeArray*)` lowers that case to a struct rather than an LLVM array. The support stops there: `Loader` and the `.rdb` reader both reject dynamic arrays explicitly, `csvHeaders` emits a `#size` placeholder and then expands as if the length were 1, and `visit(ExprIndx*)` does `cast<llvm::ArrayType>` on the base type, which a dynamic array is not.

**Dynamic arrays would remove the property this whole design rests on.** If the length is not known at compile time, a quantifier cannot expand — it becomes a runtime loop, inside the temporal evaluation, nested with the per-state loops the temporal operators already generate. That is a different and much larger feature: it changes the generated IR, the memory layout, the trace format, and it raises a semantic question the static version never has to answer — what `xs[i]` *means* when `i` is past the current length, in a language where every expression must have a value.

If dynamic arrays are wanted, the cheap version is **bounded-dynamic**: a static maximum with a runtime length.

```text
data readings : integer[8];     // capacity 8
data n        : integer;        // logical length
```

Quantifiers then expand to the static bound with a guard:

```text
all x, i in readings: P(x)
    ==>  (0 < n => P(readings[0])) && (1 < n => P(readings[1])) && …
```

Layout stays fixed, the trace format keeps fixed columns, the expansion stays compile-time, and nothing about the temporal lowering changes. The `#size` column `csvHeaders` already emits suggests this was the original intent.

That would need a way to associate the length signal with the array — a declaration form rather than a convention — which is a separate proposal.

## Explicitly out of scope

**Counting occurrences over time.** The README's design rationale opens with:

> "Between `called` and `opened`, a transition to `atfloor` occurs at most twice" is a single line of REF.

That is not expressible today — there is no bounded-occurrence pattern among the sixteen in `psbody` — and **this proposal does not make it expressible**. That count is over trace positions, not array elements. It needs runtime counter state and therefore touches the lowering, not just the parser. It is a genuinely useful feature and a genuinely separate one; conflating the two under the word "count" would be a mistake.

**Quantifying over enum members.** `all s in State:` is tempting for state machines, but an enum member is not a value that can be bound — only compared against. Leaving it out keeps the rule "the domain is something with static elements" clean.

## Open questions

1. Should the body be delimited after all (`all x in xs { … }`)? Maximal extension is consistent with the Dwyer patterns, which also run to the `;`, but it makes `all x in xs: P && all y in ys: Q` parse in a way that has to be read carefully.
2. Does `one` mean "exactly one" or "at most one"? This document assumes exactly one. "At most one" is the more common safety phrasing ("no two valves open simultaneously") and is `at most 1`, so the sugar may be pointed at the wrong case.
3. Should quantifiers be allowed in a computed signal (`data any_high = some x in xs: x > 5;`)? Nothing prevents it — the expansion is an ordinary expression — but it is worth deciding deliberately rather than discovering it works.
