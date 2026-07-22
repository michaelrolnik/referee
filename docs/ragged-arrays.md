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

## The shape, decided

1. **`T[]` means unbounded** — each record carries its own length. The natural
   reading wins.
2. **The row holds `{count, pointer}`.** Where the elements live is a loader
   detail: inline in the row where the capacity is known from the trace's
   shape, pooled where it is not. Nothing above the loader sees the
   difference.
3. **Quantifiers lower to a runtime loop**, not to unrolled conjuncts. No cap
   is needed anywhere, and `T[<= N]` is not introduced.

Coherent, and the cleanest of the options — no bookkeeping cap in the
specification, no second array kind for consumers to branch on, and `.count`
always meaning length.

There is precedent for the storage: a string is already an 8-byte pointer into
an interned pool with the characters outside the row, so out-of-row data with
a fixed-size handle is not new here.

### This is a breaking change, and the migration is not cosmetic

Every existing `T[]` changes meaning. `examples/mctp` declares `pkt : byte[]`
and `payload : byte[]`; `loadsized.ref`, `nested_extents.ref` and
`arrays_pinned.ref` all rely on `T[]` being one fixed extent for the run.

They do not merely need re-checking — several are *about* the old meaning.
`loadsized.ref` exists to assert that one specification holds against traces of
different sizes because the extent is fixed per run. Under the new meaning that
is no longer the property being tested.

### CSV: the array is a count column plus element columns

A CSV has fixed columns, so a record cannot carry three elements while its
neighbour carries five. The extent is a property of the header — which is why
`inferSizes` reads it from column names today.

The way through is to encode the array as what it is: two fields, the second
of which is the elements.

```csv
__time__,pkt.count,pkt[0],pkt[1],pkt[2],pkt[3]
0,3,0xDE,0xAD,0xBE,0x00
1000,2,0x01,0x02,0x00,0x00
```

The element columns give the **capacity** — four, from the header, as now — and
`pkt.count` gives the **length**, per record. The first row has three elements;
the second has two. Columns stay fixed and lengths vary, which is what was
wanted.

This is the pattern the MCTP examples already write by hand:

```text
data len : byte;            #  becomes pkt.count
data pkt : byte[];          #  becomes pkt's element columns
```

with every requirement carrying `i >= len => …` itself. Promoting it into the
loader is what makes the guard automatic.

#### And it removes the need for a pool, for CSV at least

If capacity is fixed by the header, the elements can stay **inline in the row**
and the descriptor's pointer simply points at them. Rows stay fixed-stride and
`.rdb` needs no data section.

That matters more than it first looks: **where the elements live is a loader
detail, invisible above it.** Code generation sees `{count, pointer}` and does
not care whether the pointer addresses the row or a pool. So a format that
genuinely carries variable-length data — YAML, or an `.rdb` written from one —
can pool its elements, and CSV can keep them inline, without two paths anywhere
except the loader.

One naming question left: `pkt.count` as a column name collides with a struct
field actually called `count`, and `.count` is already a pseudo-member on
arrays. Worth choosing deliberately rather than discovering later.

### Quantifiers as runtime loops

Removing the cap removes the guarded-expansion trick, and with it the constant
folding that makes expansion cheap. Two things to think about:

* A quantifier **inside a temporal operator** becomes a loop inside a loop.
  `G(all v in pkt: v > 0)` is O(N × len) rather than O(N × constant), which is
  the same shape as the accumulator cost measured in
  [`accumulator-cost.md`](accumulator-cost.md) and worth measuring rather than
  assuming.
* The counting forms — `one`, `at least`, `at most` — currently expand to a sum
  of indicators. As a loop they need an accumulator, which is the same
  machinery `Sum` uses and which does not yet have a linear lowering.

## Suggested spelling

```text
data payload : byte[][];          #  rows, each its own length
G(all v in payload[0]: v != 0);   #  a runtime loop over row 0's count
G(payload[0].count <= 64);        #  a load, not a constant
G(payload.count == 3);            #  the outer array has a count too
```

No cap appears anywhere, which was the point.

`docs/quantifiers.md` sketched `T[<= N]` for the single-row case; this is the
same idea one dimension up, and the two should land together or not at all.
