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

Give `ExprContext` a discriminator so that `(ExprContext*, name)` is unique per
compilation:

```cpp
class ExprContext final : public Visitable<ExprNullary, ExprContext>
{
public:
    ExprContext(std::string disc, std::string name);

    std::string const   disc;       //  the compilation this belongs to
    std::string const   name;
};
```

Six construction sites, in two files:

| site | source of `disc` |
| --- | --- |
| `antlr2ast.cpp` ×3 (`__curr__`, `__conf__`, freeze) | the unique module name the `Antlr2AST` already builds |
| `rewrite.cpp` ×3 | `expr->ctxt->disc` — the node being rewritten already carries it |

The `rewrite.cpp` sites are the awkward half: two of them construct a context
from `m_bind` (a freeze variable name) rather than from an existing context, so
the discriminator has to be carried on the visitor. That is the reason this was
not attempted in the session that diagnosed it — it is a contained change, but
not a small one, and doing it badly would be worse than the bug.

### Alternative considered

Keying the `ExprData` factory on the module directly would be more obviously
correct, but `RewriteImpl` does not hold a module, so it would have to be
threaded through the rewrite pass as well — more churn for the same result.

## Test to add with the fix

Revert the workarounds and let the fixtures collide naturally:

```bash
sed -i 's/\\bgg\\b/g/g' test/logic/nested_extents.ref test/logic/nested_extents.csv
```

and give `Diagnostics.RejectsBadCalls` back its natural names (`f`, `i` rather
than `cb_f`, `cb`). Both currently pass only because of the rename; with the
fix they should pass without it, and the suite should stay green in any test
order.
