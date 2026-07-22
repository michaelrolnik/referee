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

### CSV: columns to the widest row, absent elements marked

A CSV has fixed columns, so the header is sized to the **widest row in the
file** and a cell that is not an element is marked absent:

```csv
__time__,pkt[0],pkt[1],pkt[2],pkt[3]
0,0xDE,0xAD,0xBE,-
1000,0x01,0x02,-,-
```

`pkt` has three elements in the first record and two in the second. The count
is derived by the loader, not declared.

That is better than a companion `pkt.count` column, and the reason is not
tidiness: **a count column is a second source of truth.** It can say three
while four cells hold values, and nothing detects the disagreement — one of the
two is simply wrong and the loader has to pick. A marker has one source, so the
question cannot arise.

It also reads as what it is. The ragged shape is visible in the file rather
than inferred by comparing a number against a row.

#### Three things to settle before implementing

**The marker must not be a valid value.** `-` is safe for integers, bytes,
numbers, booleans and enums — none of them can spell it. It is **not** safe for
a `string` array, where `-` is a perfectly good string. Either strings use a
different marker, or an empty cell means absent and `-` means the one-character
string, or string arrays are not carried in CSV. This is the only real hole in
the scheme and it should be closed deliberately.

**Absence must be trailing.** An array has no holes, so `1,-,3` is not a
length-two array with a gap — it is an error. Accepting it silently would mean
inventing a semantics for something the type system cannot express.

**Empty cells.** `1,2,,` and `1,2,-,-` should mean the same thing or clearly
different things; either is defensible, deciding neither is not.

#### It also removes the need for a pool, for CSV

Capacity is still fixed by the header, so the elements stay **inline in the
row** and the descriptor points at them. Rows stay fixed-stride and `.rdb`
needs no data section.

Which generalises: **where the elements live is a loader detail, invisible
above it.** Code generation sees `{count, pointer}` and does not care whether
the pointer addresses the row or a pool. A format carrying genuinely
variable-length data can pool; CSV keeps them inline; and there is no branch
anywhere except the loader.

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
