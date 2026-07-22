# Design: external functions

**Status:** proposed, not built.
**Scope:** declaring a native function in a `.ref`, generating a C header from the specification's types, calling the function from a requirement, and an optional whole-state calling convention.

## The problem

Some requirements need arithmetic the language cannot express and should not grow syntax for. The motivating one is concrete: MCTP over SMBus ends every packet with a PEC, a CRC-8 over the transaction. It is the single highest-yield mechanical check on a capture, and it is unwritable today — REF has no fold over an array and no function abstraction, so the only spelling is the fully unrolled polynomial, eight shift/xor steps per octet across every octet.

The same shape recurs: checksums, parity, message-type-specific decode, a lookup table, a domain calculation someone already has in C. Each is small, each is different, and none of them is a language feature.

There are two ways out. Add the missing operations one at a time — a fold, then CRC, then popcount, then whatever the next protocol needs — or open a hole and let the specification call code. The first never finishes. This document proposes the second, and then argues in *Standard library* below that a small first tier of built-ins is still worth having, because the escape hatch has costs the built-ins do not.

## What is proposed

Three pieces, in the order they would be built.

### 1. `func` declarations

```text
func crc8    : (byte[], integer) -> byte;
func decode  : (byte[], integer) -> integer;
func in_range: (number, number, number) -> boolean;
```

A `func` is a declaration, like `data` and `conf`, and lives with them at the top of a file — so it can be `import`ed, and a project can keep one `crc.ref` that every specification shares.

Parameter and return types are REF types. Arrays pass as a pointer plus an extent, so `(byte[], integer)` becomes two C parameters for the array and one for the integer:

```c
uint8_t crc8(const uint8_t *arg0, int64_t arg0_count, int64_t arg1);
```

Passing the extent rather than relying on the declared one matters: the array's extent is fixed for a run, but the *meaningful* length usually is not — that is what the `len` companion signal is for in all three MCTP examples. The C function needs the real length, and the caller passes it.

Only value types cross the boundary. No struct returns in v1: they raise ABI questions (sret, alignment, padding) for very little gain, and a struct result can be split into two calls or one call returning a packed integer.

### 2. Generated header

```bash
referee header spec.ref -o spec.h
```

emits, from the same code that lays out the trace:

- a `typedef enum` per REF `enum`, with the member values REF actually stores (the member index — this is exactly the thing a hand-written header gets wrong);
- a `struct` per REF `struct`, matching REF's layout and alignment;
- a prototype per `func`.

This is the load-bearing piece. C has no way to check that the function you linked matches the signature the specification declared — a mismatch is undefined behaviour, not a diagnostic. Generating the header is the only mechanism that makes the two agree, so the workflow should make it the obvious path: generate, `#include`, compile.

The header should also emit a version symbol the loader checks at startup, so a stale object built against an older specification fails loudly instead of reading the wrong offsets.

### 3. Calling

A call is an ordinary expression:

```text
data pec_ok = crc8(pkt, len - 1) == pkt[len - 1];

globally, it is always the case that pec_ok holds;
```

Note where the call went: into a **computed signal**, not into the requirement. That should be the documented idiom. A computed signal is evaluated once per state by `__prepare__`; the same call written inside a temporal formula is evaluated once per state *per enclosing loop iteration*, which for a bounded operator is quadratic. Nothing prevents the direct form, and the cost is not obvious from reading it, so the documentation has to say so.

Calls are pure expressions as far as the compiler is concerned, so they participate in the existing loop-invariance analysis and can be hoisted when their arguments do not depend on the loop variable.

## The whole-state convention

The second half of the request: pass a const pointer to the entire state, including time, and let the function read whatever it needs.

```text
func packet_ok : (state) -> boolean;
```

```c
bool packet_ok(const ref_state_t *s);
```

This is genuinely convenient — one signature regardless of how many signals a function reads, and adding a signal does not change it. **I would not ship it first, and I would not ship it in the same shape as the value form.** Three reasons:

**It exposes an internal layout.** A state row today is `{ int64_t time; void *prop[N]; }` — a time followed by one pointer per property, with the property's data living elsewhere. That is an implementation choice, not a contract, and it will change: per-record extents (`T[<= N]`) will change it, and any move to a columnar layout would change it completely. The value form has no such coupling — `(const uint8_t*, int64_t)` stays true whatever the trace layout becomes.

**It converts a narrow contract into a broad one.** `crc8(pkt, len)` can only be wrong about the octets it was handed. `packet_ok(s)` is handed a pointer into the middle of a large buffer and can read any state in the trace by walking off the end of this one — accidentally, and with nothing to catch it. The instruction "the function should not touch anything outside the state" is exactly right, and exactly what C cannot enforce.

**The value form already covers the motivating case.** PEC needs the octets and the length. Nothing about it wants the whole state.

If it is built, it should be: a **generated** `ref_state_t` (never hand-written), carrying a layout version the loader verifies; accessor macros or inline functions in the generated header rather than direct field access, so the layout can change without breaking source compatibility; and `const` throughout.

## Purity, and what it costs

The language's current guarantee is that a `.ref` plus a trace determines the verdict. External functions puncture that: the verdict now also depends on a binary. For the use case where REF looks strongest — a conformance artifact reviewed against clause numbers — that is a real loss, and it is worth pricing rather than waving through.

Two mitigations, neither complete:

- **State the contract and mark it in the header.** A `func` must be a pure function of its arguments: same inputs, same output, no I/O, no globals, no mutation. Unenforceable, so it belongs in the generated header as a comment and in the documentation as a rule.
- **Record what was linked.** The report and the `.rdb` should carry the path and content hash of every loaded object. A verdict that depends on a binary should say which binary, or it is not reproducible in the sense the rest of the tool means.

## Linking

At JIT time, `referee execute spec.ref trace.csv --library libmctp_helpers.so`. ORC already resolves symbols from the process and from loaded objects, so this is a `DynamicLibrarySearchGenerator` and an error path. A missing symbol should fail before any trace is read, naming the function and the expected prototype.

For AOT checkers (see `native-checkers.md`) the object is an ordinary link input, which is simpler and is an argument for that path.

## Standard library

Yes — and the `func` mechanism makes the case for one rather than replacing it.

Once specifications can call C, every project will write the same four functions: a CRC, a popcount, a byte-swap, a sum over an array. Each copy is a chance to get the polynomial or the bit order wrong, each needs a build step, each breaks the reproducibility property above, and none of them is domain-specific. They should be built in.

I would split it in three tiers, by *mechanism* rather than by topic:

**Tier 1 — syntax, not functions.** A numeric fold belongs in the language, not a library:

```text
sum x in xs: x.len
```

It cannot be a `func`, because it needs the quantifier's compile-time expansion over an extent — the same machinery `all` and `one` already use. It is the single most-missed thing in all three MCTP examples: it is why `message_body_is_bounded` is written out one term per packet slot, and why "bytes from SOM to EOM" needs either a time window or a quadratic freeze. This is the highest-value item in this document and it does not depend on external functions at all.

**Tier 2 — built-in pure functions**, implemented inside referee and lowered directly to IR: `crc8`, `crc16`, `crc32` with named polynomials, `popcount`, `bswap`, `min`, `max`, `abs`. These keep the determinism guarantee (no external binary), work identically in the JIT and in an AOT checker, and cover most of what people would otherwise reach for `func` to do. They need no header generation and no ABI.

**Tier 3 — the escape hatch**, which is this document. For the genuinely domain-specific: a vendor's decode, a lookup table, a calculation that already exists.

The tiers should be built in that order, because each one removes reasons to need the next. Tier 1 is language work with no downside; tier 2 is bounded and safe; tier 3 is the one that costs a guarantee, and should be reached for last.

## What I would build, and in what order

1. **`sum x in xs:`** — largest gain, no cost to any existing guarantee, and independent of everything else here.
2. **Built-in CRC and bit intrinsics** — removes the motivating case for external functions entirely.
3. **`func` with value parameters, plus `referee header`** — the escape hatch, with the generated header from day one rather than added later.
4. **The whole-state convention** — only if 3 proves insufficient in practice, and only with a generated, versioned struct and accessors.

## Open questions

1. **Should a `func` be callable from a pattern body?** Accumulators and looping temporals are barred there ("cannot be temporal"). A call is not temporal, so it would be allowed by default — but a call inside a scoped pattern is evaluated per scope instance, and the cost is invisible. Probably allow, and document.
2. **Failure signalling.** What does a `func` do when its input is malformed — return a sentinel, or is there an error channel? A sentinel that collides with a valid value is a silent wrong verdict. The simplest answer is that `func` results are total: the specification guards the call, not the callee.
3. **Should `--library` be recorded in the `.rdb`?** Traces are meant to outlive the specification. If a verdict depended on a binary, the trace arguably should say which — but a trace is a recording and a library is not part of it.
4. **Extents for multi-dimensional arrays.** `(byte[][], integer)` would pass two extents plus a pointer; the flattening convention needs stating before anything relies on it.
5. **Does `referee header` need to emit the reverse** — a stub `.c` with empty bodies — so the first build is a copy-paste rather than transcription? Cheap, and it is where the signature mismatches would otherwise happen.
