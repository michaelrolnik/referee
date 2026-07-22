# Ragged arrays: `{count, T[]}` per row

**Status:** designed, not built. The representation is half-present already.
**Shape:** an array of arrays where each inner array carries its own length, rather than every row having the same extent.

## Why the current arrays cannot do it

`T[N][M]` is *N* arrays of exactly *M*. The outer array's element type is one
type, so every row has the same extent and a ragged array cannot be spelled.
That is stated in the README and it is what forces the workaround the MCTP
examples use: pad to the maximum and carry a length beside it.

```text
data len : byte;            #  how much of pkt is real
data pkt : byte[];          #  padded to the largest frame
```

Every requirement then guards on `len`, and every quantifier expands over the
padded extent with a guard per conjunct. It works, and it puts a bookkeeping
signal in the specification that describes the encoding rather than the system.

## What already exists

`TypeArray` reserves `count == 0` for exactly this:

```cpp
unsigned const  count;      //  if count is 0, array is dynamic otherwise it's static

size_t alignment() override { if (count == 0) return 8; ... }
size_t size()      override { if (count == 0) return 16; ... }   // {i16 length, padding, ptr}
```

and the code generator builds the matching type:

```cpp
if (size == 0)
{
    elements.push_back(llvm::PointerType::get(*m_context, 0));
    m_type = llvm::StructType::create(*m_context, elements, m_name);   // {i16, ptr}
}
```

So the descriptor is already `{length, pointer}` — the shape proposed here —
and it is unreachable. Nothing produces it: the loader refuses `count == 0`
outright, and `T[]` is resolved to a concrete extent from the trace before
code generation ever sees it. It is a design that was started and left.

That is the useful finding: this is less a new representation than an
existing one that was never wired up.

## What has to change

| | |
| --- | --- |
| **loader** | `LoaderImpl::visit(TypeArray*)` throws for `count == 0`. It would emit a length and a pointer, with the elements somewhere the row can reference — which means the row is no longer self-contained. |
| **`.rdb`** | rows are fixed-size by design, so variable-length elements need a heap alongside the state table, and the schema encoding needs the dynamic form. |
| **`.count`** | currently a compile-time constant folded at the member access; it becomes a load of the length field. |
| **quantifiers** | expansion is compile-time. Over a runtime extent it cannot expand at all — it needs the guarded-expansion treatment slicing already uses, bounded by a declared maximum. |
| **external functions** | already right. An array crosses as `{count, data}`, so a ragged row *is* the descriptor and needs no marshalling at all. |

## The question that decides the shape

Quantifiers are the constraint, not storage. `all x in row: P` must expand at
compile time, so a genuinely unbounded row cannot be quantified over.

Which leaves two designs, and they are different features:

**Bounded ragged** — `T[<= N][]`: each row has its own length, capped by a
maximum written in the specification. Quantifiers expand over `N` and guard on
the row's length, exactly as slicing does now. Rows stay fixed-size, so `.rdb`
is unaffected. This removes the companion `len` signal, which is the actual
complaint, and is a much smaller change than it first appears.

**Unbounded ragged** — no cap. Rows are variable-size, `.rdb` needs a heap,
and quantifiers over a row become impossible rather than merely guarded.

The first is worth building. The second changes what a trace *is*, and should
not be reached for until something concrete needs it — the MCTP work wanted
per-record extents, not unbounded ones.

## Suggested spelling

```text
data payload : byte[<= 64][];     #  rows of at most 64 octets, each its own length
G(all v in payload[0]: v != 0);   #  expands over 64, guarded by row 0's length
G(payload[0].count <= 64);        #  a load, not a constant
```

`docs/quantifiers.md` sketched `T[<= N]` for the single-row case; this is the
same idea one dimension up, and the two should land together or not at all.
