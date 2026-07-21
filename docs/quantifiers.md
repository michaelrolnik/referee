# Design: bounded quantifiers over arrays

**Status:** implemented, except where noted. `all` / `some` / `none` / `one`, `at least N` / `at most N` and the `.count` pseudo-member are in; everything under *arrays whose size is not in the `.ref`* is not.
**Scope:** `all` / `some` / `none` / `one`, plus `at least N` / `at most N`, quantifying over the elements of an array.

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
            | 'none'
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

Cost: four new reserved words (`all`, `some`, `none`, `one`). `in`, `at` and `least` are **already reserved**, so the counting forms add none.

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

Each form is defined by expansion over the array's `count`, which is known by the time the body is built. For `xs : T[n]`:

| Form | Expands to |
| --- | --- |
| `all x in xs: P` | `P(xs[0]) && P(xs[1]) && … && P(xs[n-1])` |
| `some x in xs: P` | `P(xs[0]) \|\| … \|\| P(xs[n-1])` |
| `none x in xs: P` | `!(P(xs[0]) \|\| … \|\| P(xs[n-1]))` |
| `one x in xs: P` | exactly one disjunct true — the pairwise-exclusive expansion |
| `at least k …` | `(P₀ ? 1 : 0) + … + (Pₙ₋₁ ? 1 : 0) >= k` |
| `at most k …` | the same sum, `<= k` |

`one` is sugar for `at least 1 && at most 1`, and `none` for `at most 0`. Both are kept for the reason the Dwyer patterns give "it is never the case that P" its own phrasing rather than negating universality: the negated form is what a requirement actually says, and spelling it that way is what makes it reviewable. `none v in valves: v.stuck` reads as the requirement; `!(some v in valves: v.stuck)` reads as its encoding.

An empty array (`n = 0`) gives `all` = true, `some` = false, following the usual vacuous-truth convention.

### Why every form is boolean

An integer-valued `count x in xs: P` was considered and rejected. It cannot be parsed unambiguously with a maximally-extending body:

```text
count limit in limits: limit > 5 > 2
```

is either `count(limit > 5) > 2` or `count(limit > 5 > 2)`, and no precedence rule distinguishes them — the body has to extend maximally for the boolean forms to work. Fixing it needs delimiters (`count(limit in limits: P) > 2`) for that one form only, which is an inconsistency in exchange for a capability that `at least` / `at most` already covers.

If a value-returning count is ever genuinely needed, it can be added later in delimited form without disturbing any of this.

### Counting without combinatorial blow-up

An expansion over `C(n,k)` subsets was the obvious reading of "at least k" and is exponential in the worst case. It is not needed: REF already has the conditional operator and integer arithmetic, so the count is a sum of indicators — `(P₀ ? 1 : 0) + … + (Pₙ₋₁ ? 1 : 0)` — compared against the bound. That is linear in the element count, introduces no node type the language lacks, and makes `one` fall out as `sum == 1`.

## Interaction with the temporal layer: none

This is the property that makes the feature cheap, and it is worth stating explicitly.

Because the element count is known when the AST is built, **quantifiers expand entirely during AST construction**. Nothing reaches `TypeCalc`, `Rewrite`, or `Compile` that those visitors do not already handle: the result is an ordinary tree of `ExprAnd` / `ExprOr` over `ExprIndx` nodes with constant subscripts, which the existing lowering compiles as it always has.

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

## Interaction with arrays whose size is not in the `.ref`

There are two different things "unknown array size" can mean, and they differ enormously in cost. It is worth separating them before either is attempted.

### Sized at load time — cheap, and mostly already possible

The size is not written in the `.ref`, but it is **fixed for a whole run** and known once the trace has been read:

```text
data readings : integer[];      // however many the trace carries
```

This is far less invasive than it first appears, because of an ordering that already holds: **the trace is opened before the specification is compiled.** `Referee::executeRdb` constructs the `Reader`, and only then does `runAgainstRdb` call `buildJitFromRef`, which calls `Referee::compile`. By the time the `.ref` is parsed, the schema of the data it will run against is in hand.

So the sizes can be supplied *at parse time*, the same way include paths already are, and `visitTypeArray` builds a concrete `TypeArray` immediately. Nothing downstream ever sees an unsized type, which means:

- The LLVM type stays a plain `ArrayType`. No `{length, ptr}` fat pointer, and `visit(ExprIndx*)` — which casts the base to `llvm::ArrayType` — is unchanged.
- `.rdb` rows stay fixed-size; the format already serialises `TypeArray` with a count, so a resolved array records itself with no format change.
- Quantifiers still expand at AST-construction time, because the count is concrete by then. Everything in this document still holds.
- There is no out-of-range question, because there is no per-state variation.

The work is roughly:

1. Grammar: `size : integer | /* empty */`.
2. `Antlr2AST` takes a size table (declaration name → counts) alongside `includePaths`, consulted when a `[]` is seen.
3. `ingest()` opens the trace document *before* `parseSchema` rather than after, infers each unsized array's extent from the flattened column names (`readings[0]`, `readings[1]`, … → highest index plus one), and passes the table down. This is the one place the current data flow has to reverse: today the specification determines the expected columns, and here the columns determine part of the specification.
4. Executing an existing `.rdb` reads the counts from its embedded schema instead.

Two loose ends worth deciding rather than discovering:

- **`referee compile file.ref` has no trace**, so an unsized array cannot be resolved. Either it is a clear error ("array size unknown; compile requires a trace or an explicit size") or the subcommand grows a way to supply one.
- **Multi-dimensional unsized arrays.** `T[][]` can in principle be inferred from the column names too, but it is worth restricting to a single unsized dimension until something needs more.

### Varying per state — expensive, and it undoes this design

The length changes from record to record. `TypeArray` already anticipates this: `count == 0` is documented as dynamic, with a `{i16 length, ptr}` layout, and `CompileTypeImpl::visit(TypeArray*)` lowers that case to a struct. Support stops there — `Loader` and the `.rdb` reader both reject it explicitly, `csvHeaders` emits a `#size` placeholder and then expands as though the length were 1, and `visit(ExprIndx*)` would fail its `cast<llvm::ArrayType>`.

This variant removes the property the whole quantifier design rests on. A quantifier could no longer expand; it would become a runtime loop nested inside the per-state loops the temporal operators already generate. It changes the generated IR, the memory layout and the trace format, and it raises a question the other variants never have to answer: what `xs[i]` *means* when `i` is past the current length, in a language where every expression must have a value.

If per-state variation is genuinely needed, **bounded-dynamic** is the cheap approximation — a static capacity with a runtime length:

```text
data readings : integer[8];     // capacity
data n        : integer;        // logical length
```

with quantifiers expanding to the capacity under a guard:

```text
all x, i in readings: P(x)
    ==>  (0 < n => P(readings[0])) && (1 < n => P(readings[1])) && …
```

Layout stays fixed, the trace keeps fixed columns, expansion stays compile-time, and the temporal lowering is untouched. It would need a declaration form associating the length signal with the array rather than a naming convention. The `#size` column `csvHeaders` already emits suggests something along these lines was the original intent.

## Addressing an array's size from a requirement

There is currently no way to mention an array's extent in an expression. The size is a literal inside the *type* (`size : integer` in the grammar) and never becomes a value, so `limits` has four elements but nothing in a requirement can say "four".

That is tolerable while every size is written in the `.ref`. It stops being tolerable with load-sized arrays: if the specification does not state the size and cannot ask for it, no requirement can constrain it — "at least three sensors are configured" becomes unwritable, and two arrays cannot be required to agree in length.

### The syntax already parses

`limits.size` is accepted by the parser today. It fails later, in the type checker:

```
$ referee compile sz.ref
exception: bad type at [3:0 .. 3:11]
```

`TypeCalcImpl::visit(ExprMmbr*)` opens with `dynamic_cast<TypeComposite*>` on the base type, and `TypeArray` derives from `Visitable<Type, TypeArray>` rather than from `TypeComposite`, so the cast yields null and the member access is rejected before the member name is ever examined.

So a pseudo-member needs **no grammar change at all**. Two branches implement it:

1. `TypeCalcImpl::visit(ExprMmbr*)` — before the composite cast, if the base is a `TypeArray` and the member is the size name, the expression's type is `integer`.
2. `CompileExprImpl::visit(ExprMmbr*)` — the same case emits `ConstantInt(count)`.

Because the count is known when the AST is built — whether written in the `.ref` or resolved from the trace — it lowers to a **literal**. No runtime cost, no load from the state buffer, and it behaves identically for load-sized arrays.

Multi-dimensional access falls out without extra rules: `grid.count` is the outer extent, `grid[0].count` the inner one.

This also composes with the quantifiers rather than competing with them. Quantifiers cover iteration; the size covers the cases iteration cannot express — constraining the extent itself, or requiring two arrays to agree.

### Choosing the name

`.count` was chosen. `.size`, `.length` and `.count` all parse identically; the choice was readability against a maintenance trap.

`Type::size()` already exists in the implementation and means **size in bytes**. Naming the language feature `.size` puts two different meanings of the word one keystroke apart, in a file where a maintainer implementing this would be reading both. `.count` matches `TypeArray::count` exactly, which is the field the value comes from.

Against that, `.size` and `.length` are what a reader expects for "number of elements", and the audience for a `.ref` file is not reading `syntax.hpp`.

`.count` is the safer choice and `.length` the friendlier one. `.size` has the readability of `.length` and the ambiguity of `.count`, which is the worst combination of the three.

Whichever is chosen, it is only a member *name* — it is resolved by the type of the base, so it does not become a reserved word and does not prevent a struct field called `size` elsewhere.

### Alternatives considered

- **`size(limits)`** — a keyword-with-parens form, the shape `G(…)` and `I(…)` already use. It parses cleanly but needs a grammar rule and a new reserved word, and it reads less like member access than the thing it is.
- **`#limits`** — impossible. `#` begins a line comment, so `#limits == 4;` silently compiles to a file containing no requirements at all. Worth recording precisely because it *looks* like it works.
- **`|limits|`** — conflicts with `||`.

## Explicitly out of scope

**Counting occurrences over time.** The README's design rationale opens with:

> "Between `called` and `opened`, a transition to `atfloor` occurs at most twice" is a single line of REF.

That is not expressible today — there is no bounded-occurrence pattern among the sixteen in `psbody` — and **this proposal does not make it expressible**. That count is over trace positions, not array elements. It needs runtime counter state and therefore touches the lowering, not just the parser. It is a genuinely useful feature and a genuinely separate one; conflating the two under the word "count" would be a mistake.

**Quantifying over enum members.** `all s in State:` is tempting for state machines, but an enum member is not a value that can be bound — only compared against. Leaving it out keeps the rule "the domain is something with static elements" clean.

## Open questions

1. Should the body be delimited after all (`all x in xs { … }`)? Maximal extension is consistent with the Dwyer patterns, which also run to the `;`, but it makes `all x in xs: P && all y in ys: Q` parse in a way that has to be read carefully.
2. Does `one` mean "exactly one" or "at most one"? This document assumes exactly one. "At most one" is the more common safety phrasing ("no two valves open simultaneously") and is `at most 1`, so the sugar may be pointed at the wrong case.
3. Should quantifiers be allowed in a computed signal (`data any_high = some x in xs: x > 5;`)? Nothing prevents it — the expansion is an ordinary expression — but it is worth deciding deliberately rather than discovering it works.
