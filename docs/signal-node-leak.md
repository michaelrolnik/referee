# Bug: AST signal nodes leak between specifications

**Status:** open. Reproduces deterministically; the fix is scoped below but not built.
**Severity:** silent wrong answers. This is the only known open defect that produces an incorrect result rather than a missing feature.

## Reproduction

Two specifications compiled in one process, both declaring a signal called `g`
with different types. The second is type-checked against the *first* one's
declaration.

```bash
# passes alone
./build/tests --gtest_filter='Rdb.NestedArrayExtents'
    [       OK ] Rdb.NestedArrayExtents

# fails when preceded by a specification that declares `g` as integer[3][2]
./build/tests --gtest_filter='Rdb.Quantifiers:Rdb.NestedArrayExtents'
    C++ exception: "an array has no member 'v'; did you mean 'count'?"
    [  FAILED  ] Rdb.NestedArrayExtents
```

`test/logic/quantifiers.ref` declares `data g : integer[3][2]`.
`test/logic/nested_extents.ref` declares `data g : struct { v : integer[]; }[]`
— and is told `g[0]` has no member `v`, which is true of the *other* file's `g`.

Renaming the signal makes it pass. Both fixtures currently carry a rename to
keep the suite green, which is the reason this is worth fixing rather than
working around a third time: each workaround makes it less visible.

## Diagnosis

`Factory<T>::create(args...)` interns by its arguments in a **process-global**
`static std::map`. For a signal node the key is `(ExprContext*, name)`:

```cpp
m_expr = Factory<ExprData>::create(expr->where(), expr->ctxt, expr->name);
```

The context for an ordinary signal is `__curr__`, which is itself interned —
`Factory<ExprContext>::create("__curr__")` — so it is **one pointer for the
whole process**. The key therefore degenerates to the name alone, and two
specifications that use the same signal name share one `ExprData` node,
including the type that the first of them resolved onto it.

Module names are already made unique per compile for a related reason (see the
comments around `#compile:` and `#schema:` in `referee.cpp`), but that protects
only the `Module`. The node cache sits underneath it and is not keyed by
module.

Note it does **not** reproduce through `Referee::parseSchema` alone — an
earlier attempt at a minimal repro failed for that reason. It needs the full
compile path, where `Rewrite` rebuilds nodes through the factory.

## Proposed fix

Two candidates were considered. The second is better and is what should be
built; the first is recorded because it is smaller, and is a reasonable
stopgap if the second is not affordable.

### The real fix: intern nodes per compilation, types globally

The bug is a scope error, and the scope that is wrong is not the same for
everything the factory holds.

**Types are values.** `integer` is `integer` in every specification, forever.
Interning them process-wide is not merely acceptable, it is load-bearing:
every rule in `typecalc.cpp` compares types by pointer (`type == typeInteger`,
`lhs != rhs`, the enum-equality check), and that only works because there is
one canonical object per type. Types also get built outside any module --
`rdb/database.cpp` decodes them from a `.rdb` schema -- so scoping them would
create a second problem. **Leave types global.**

**Expression nodes are not values.** An `ExprData("g")` refers to a
declaration in a particular module, and it carries mutable state:

```cpp
Type*   type()              {return m_type;}
void    type(Type* type)    {m_type = type;}    //  stamped by TypeCalc
```

That mutability is what makes sharing harmful rather than merely surprising.
Two specifications' `g` are different things that happen to share a spelling,
and the first one to be type-checked stamps the node the second then reads.

So: **nodes interned per compilation, types interned globally.** The mistake
was never interning; it was interning the mutable thing. `std::is_base_of_v<Expr, T>`
distinguishes the two cleanly at compile time.

This also fixes something not otherwise on the list: the factory's storage is
`static` and never freed, so today every compilation leaks its entire AST for
the life of the process. Per-compilation arenas make nodes die with the
compilation, which matters for exactly the long-running cases where the
sharing bug bites.

#### The lifetime constraint, which sets the shape

An RAII guard placed inside `parseSchema` or `compile` **does not work**, and
this is the trap worth recording: both return a `Schema` whose `Module` points
at the nodes just built. A guard that freed them on function exit would hand
back a `Schema` full of dangling pointers.

The arena must therefore be *owned by the thing that owns the compilation* --
`Schema`, and through it `JitWithSpecs` -- and die when that does. Which means
this is not "add a `reset()`" or "add a scope guard"; it is:

  * a per-arena, type-erased store (the maps cannot be `static` any more);
  * a thread-local *current arena* pointer that `Factory<T>::create` consults
    when `T` derives from `Expr`;
  * the arena held as a member of `Schema` / `JitWithSpecs`, movable, so the
    existing move-construction of those types keeps working.

Call sites do not change -- all 269 of them keep the `Factory<T>::create(...)`
spelling -- which is the reason to prefer this over injecting a factory
reference into every visitor constructor. The cost is that the arena is
implicit at the point of use; the mitigation is that its lifetime is explicit
at the point of ownership, which is more than can be said for the current
design, where the answer is "forever".

### The stopgap: a discriminator on `ExprContext`

Smaller, fixes the observable bug, does nothing about the leaked memory.

Give `ExprContext` a discriminator so that `(ExprContext*, name)` is unique per
compilation:

```cpp
ExprContext(std::string disc, std::string name);
```

Six construction sites, in two files:

| site | source of `disc` |
| --- | --- |
| `antlr2ast.cpp` x3 (`__curr__`, `__conf__`, freeze) | the unique module name `Antlr2AST` already builds |
| `rewrite.cpp` x3 | `expr->ctxt->disc` -- the node being rewritten carries it |

Two of the `rewrite.cpp` sites construct a context from `m_bind`, a freeze
variable name, with no existing context to inherit from, so the discriminator
has to be carried on the visitor.

### What a `Factory::reset()` would and would not do

Clearing the interned maps between tests removes cross-test interference and
would let both fixture renames go. It does **not** fix the defect: two
specifications compiled in one process still collide, which is a service, a
watch mode, or the AOT path. It is worth having for test hygiene -- test order
should not matter -- but only alongside a real fix, never instead of one,
because a green suite is exactly what has kept this bug invisible so far.

It is also not free of hazard. The factory owns its objects by `unique_ptr`,
so a reset destroys every interned type and node; it must run only after every
`Schema` and `JitWithSpecs` has been destroyed. A `TearDown` firing while a
fixture still holds a JIT is a use-after-free, not a failed assertion.

