# Ragged arrays: `{count, T[]}` per row

**Status:** built, on branch `ragged-arrays`. `T[]` means unbounded: the loader
reads however many cells a row held and writes a `{count, pointer}` descriptor,
`.count` is a load, and a quantifier is a loop. One open consequence, below.
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
| **loader** | **done.** The descriptor is reserved where the array sits and the elements are placed after the fixed layout, so members that follow it do not shift. The pointer is written as an offset *relative to the descriptor*, which is what lets the blob be appended into a file and then mapped at another address entirely. |
| **`.rdb`** | **done, and it needed no heap.** The elements go in the prop blob after the fixed part, so blobs are variable-length but still self-contained. The offset becomes a host pointer on the way in — exactly what already happened to strings one level down, and the same walk does both. |
| **`.count`** | **done.** A written extent still folds to a constant; an array without one extracts the count from its descriptor. |
| **quantifiers** | **done.** Over a runtime extent there is nothing to expand, so the body is built once with a binder standing in for the index and lowers to a loop. Every form -- `all`, `some`, `none`, `one`, `at least`, `at most` -- is a comparison on how many elements satisfied it, which is the reduction the counting forms already used. A written extent still unrolls. A temporal operator in the body is rejected: its linear buffers are built once per node, before the binder has a value, and expansion used to give each element its own node and so its own buffer. |
| **external functions** | already right. An array crosses as `{count, data}`, so a ragged row *is* the descriptor and needs no marshalling at all. |

## The shape, decided

0. **`T[N]` is unchanged.** A written extent keeps the inline representation it
   has today: `.count` folds to a constant, indexing is a fixed offset,
   quantifiers unroll. Nothing that compiles today compiles differently.
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

### Two kinds, and the migration is gentler than it looks

`T[N]` and `T[]` become genuinely different types rather than two states of
one: the first inline with a constant extent, the second a descriptor with a
length. Consumers branch on which they hold — but only `T[]` is new, so the
existing path is untouched and every `T[N]` in the tree keeps working
unchanged. That is what makes this an addition.

`T[]` does change meaning, and an earlier revision of this document overstated
what that costs. For a trace whose records all happen to be the same length —
which is every CSV that exists today, since the header fixes the width —
behaviour is **identical**: `.count` returns the same number, a quantifier
visits the same elements, and only the lowering differs. A loop instead of
unrolled conjuncts gives the same answer more slowly.

The difference appears only on a genuinely ragged trace, which no existing
fixture has. So `examples/mctp`, `nested_extents.ref` and `arrays_pinned.ref`
should keep passing untouched.

`loadsized.ref` is the one to look at, and not because it breaks. It exists to
assert that a specification holds against traces of *different sizes* because
the extent is fixed per run. That property still holds, but it stops being
interesting — under the new meaning it is what every unbounded array does, so
the fixture is testing something that has become unremarkable rather than
something that has become false.

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

#### Capacity is not a concept

Nothing above the loader ever sees how many cells a header happens to have.
Indexing is `base + i × elemsize` from the descriptor's pointer; a bounds check
is against `count`. There is no operation that reads a capacity, so it does not
belong in the type, the schema, a requirement, or the descriptor.

It is an allocation figure and nothing else — how many bytes this row needs,
which is already the loader's business and has always been. The widest row in
the file decides it, the loader reserves that much, and the word does not need
to appear anywhere else.

That distinction is easy to lose. An earlier revision of this document had
capacity in the *type*, which forced the capacity-versus-length split that the
`{count, T*}` shape exists to remove — and it crept back in here as a property
of the CSV encoding. It is neither. `count` is the only length anything can
observe.

#### It also removes the need for a pool, for CSV

The elements stay **inline in the row** and the descriptor points at them, so
rows stay fixed-stride and `.rdb` needs no data section.

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


## An index past the end was fatal, and `&&` now guards against it

The language has never bounds-checked an index, and said so. On a padded array
that was survivable: `pkt[7]` on a four-element array read a pad byte, a wrong
answer in a place a specification should not have looked. On a ragged array
whose elements are themselves arrays, the same read produces a **descriptor
that was never written**, and the next step dereferences it.

`&&`, `||`, `=>` and `? :` did not protect it, because they lowered to
`select` -- a value, not a branch. Both operands were materialised into the
same block and the operator chose between them afterwards. While every
expression is total that is not a shortcut but the *better* lowering:
branchless, schedulable, no join. REF's expressions were total, so nothing was
wrong with it.

Two things had stopped being total, and only one of them is about arrays.
Integer division by zero raised SIGFPE the same way, and had done since long
before any of this -- `G(x != 0 => y / x > 1)` aborted the process on the state
where `x` was zero.

They branch now, so the right-hand side sits in a block reached only when the
left decides nothing:

```llvm
  %1 = icmp sgt i64 %desc_rows.unpack, 1
  br i1 %1, label %and.rhs, label %and.done
and.rhs:
  ...                                       ; the dereference, reached only here
  br label %and.done
and.done:
  %and = phi i1 [ false, %entry ], [ %2, %and.rhs ]
```

**It costs nothing measurable.** 200 000 states of `G(a && b || !a)`: 2.12 s
before, 2.09 s after. Where the right-hand side is cheap and safe, LLVM folds
the branch straight back into the `select` it used to be -- so the lowering now
says what it means and the optimiser decides how to run it, which is the right
division of labour and was not available while the front end had already
committed.

The guard guards what follows it, and only that. `(y / x > 1) && x != 0` still
aborts, and should: the reading of `&&` that makes the others work is the same
reading that leaves this one the author's mistake.

Bounds checking itself is still absent by design, so an index past the end of a
*scalar* array still reads a neighbouring byte rather than being caught. What
changed is that a specification can now say "only where there is one" and be
believed.
