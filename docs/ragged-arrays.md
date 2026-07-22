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

## The shape, settled

Every array is `{count, T*}`. `count` is the length and nothing else — there is
no capacity in the type, because the elements do not live in the row.

That last point is what makes it work, and there is precedent for it here
already: a string is stored as an 8-byte pointer into an interned pool, with
the characters outside the row. Arrays would do the same. Rows stay fixed-size
whatever the lengths, `.rdb` keeps its fixed-stride state table, and a ragged
row is simply a descriptor whose neighbour has a different length.

Index access is checked against `count`, always, because `count` is always
there.

### A declared size is for quantifiers, and only for quantifiers

`all x in xs: P` expands at compile time — one conjunct per element — so a
length known only at run time gives it no number to expand to. That is the
single genuine constraint, and it is not about storage.

So a declaration may carry a maximum, and it means exactly one thing: *this may
be quantified over*.

```text
data payload : byte[];          #  index it, pass it, ask its .count
data frame   : byte[<= 64];     #  all of the above, and quantify over it
```

`all v in frame: P` expands over 64 and guards each conjunct on `frame.count`,
which is what slicing already does for `pkt[0:len]`. Without the maximum the
array is still perfectly usable — indexed, passed to a `func`, measured with
`.count` — it simply cannot be the domain of a quantifier, and the diagnostic
says so.

That separation is worth having on its own: the cap is an affordance, not a
property of the data, and a specification only pays for it where it uses it.

### What it costs

**An indirection per element access.** Strings already pay it. Measurable
inside a hot temporal loop, invisible elsewhere.

**Sixteen bytes of descriptor per array.** For `byte[4]` that is four times the
payload. Which argues for keeping the inline form where the extent *is* a
compile-time constant and using the descriptor only where it is not — two
representations, and therefore a real trade rather than a free win. Worth
measuring before deciding, on a trace with many small fixed arrays.

**Nothing at the external-function boundary.** An array already crosses as
`{count, data}`, so a ragged row is the descriptor and passes through
unmarshalled.

## Suggested spelling

```text
data payload : byte[<= 64][];     #  rows of at most 64 octets, each its own length
G(all v in payload[0]: v != 0);   #  expands over 64, guarded by row 0's count
G(payload[0].count <= 64);        #  a load, not a constant
G(payload.count == 3);            #  and the outer array has one too
```

`docs/quantifiers.md` sketched `T[<= N]` for the single-row case; this is the
same idea one dimension up, and the two should land together or not at all.
