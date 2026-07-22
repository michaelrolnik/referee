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

Names may be namespaced with `::`, to any depth:

```text
func std::math::sqrt : (number) -> number;
func bits::popcount  : (integer) -> integer;
```

This is a **lexical convention, not a scoping construct** — `std::math::sqrt`
is a function whose name contains `::`. There are no resolution rules, no
`using`, nothing to learn, and nothing that blocks real namespaces later.

`.` could not have been used: it is member access, and the parser resolves the
head of a dotted name as a signal, so `math.sin` competes with a legitimate
`data math : struct { sin : … }` and does not parse at all today. `::` appears
nowhere else in the grammar, and reads usefully differently — `math::sqrt` is
a name from elsewhere, `pkt.len` is a part of this value.

The C symbol mangles `::` to `__`, since `:` is not an identifier character:
`std::math::sqrt` binds to `referee_std__math__sqrt`. Three places have to
agree on that — the code generator, the header emitter and the loader's
lookup — so it lives in one function they all call.

A `func` is a declaration, like `data` and `conf`, and lives with them at the top of a file — so it can be `import`ed, and a project can keep one `crc.ref` that every specification shares.

Parameter and return types are REF types.

#### Symbol naming

A `func` named `crc8` binds to the C symbol **`referee_crc8`**. Every generated type carries the same prefix — `referee_slice_byte`, `referee_state_t`.

The obvious alternative, plain linkage, puts the specification's namespace and C's global namespace into the same space, and that turns out to matter in two places:

**It removes a whole class of accidental binding.** `func read : (integer) -> integer;` under plain linkage looks for `read` and finds `read(2)`. The *Resolution is restricted to the loaded objects* rule below was written to stop exactly that, and it still should — but with the prefix it becomes a second line of defence rather than the only one. `referee_read` is not a symbol libc has any reason to define, so the failure mode is a clean "not found" even if something upstream misconfigures the symbol generator.

**It makes the duplicate-symbol rule precise instead of aggressive.** Referee only ever looks at `referee_*`, so two plugins that each happen to have a helper called `crc8` do not collide — only their declared entry points can. Without the prefix the duplicate check would have to reason about every symbol in every loaded object and would fire on incidental clashes between plugin internals, which are none of its business.

It also makes a plugin self-describing: `nm -D libmctp.so | grep referee_` is exactly the list of entry points, separable from the plugin's own machinery.

The cost is that the implementation writes `referee_crc8` rather than `crc8`. The generated header carries the prototype, so this is transcription rather than invention, and a typo produces a missing-symbol error at startup rather than anything subtle.

#### Enums

Given `type Dir : enum { M2S, S2M };` the header emits, in the preferred dialect:

```c
enum referee_Dir : uint8_t {
    referee_Dir_M2S = 1,
    referee_Dir_S2M = 2,
};
```

and, where a fixed underlying type is unavailable, falls back to:

```c
typedef uint8_t referee_Dir;
static const referee_Dir referee_Dir_M2S = 1;
static const referee_Dir referee_Dir_S2M = 2;
```

Four decisions, each with a reason:

**Explicit values, 1-based.** For the reason above: REF stores declaration position plus one, and reserves 0 for "matched nothing".

**Members are qualified and prefixed**, `referee_Dir_M2S` rather than `M2S`. REF enum members are short and generic by nature — `ON`, `OFF`, `SOM`, `EOM`, `A`, `B` — and the header is included into someone else's translation unit. Emitting bare `SOM` into a firmware codebase that already has one is not a hypothetical.

**Constants, not `#define`.** A macro ignores scope and leaks into every header included after this one, which for names this generic is worse than the collision it causes.

**`enum E : uint8_t` where the dialect allows it** (C23, or C++11), because it keeps the one-byte width *and* the distinct type. The `typedef uint8_t` fallback keeps the width but loses the distinction — REF enums are nominal, so two enums with the same members are different types, and under a typedef C cannot tell them apart. Passing a `Two` where a `Dir` was wanted becomes a silent bug on the C side that REF itself would have refused.

**Reordering a REF enum renumbers every member.** The value is the declaration position, so inserting a member in the middle silently changes the meaning of every compiled plugin built against the old header. The layout-version symbol has to cover enum member order, not only struct layout and array extents.

Enums pass and return by value as their one-byte underlying type; nothing about them needs the by-pointer rule.

#### Passing arrays

An array crosses the boundary as a descriptor:

```c
typedef struct { size_t count; const uint8_t *data; } referee_slice_byte;

uint8_t referee_crc8(referee_slice_byte arg0, int64_t arg1);
```

This is a *synthesised* type, not a mirror of storage. In memory a REF array is `llvm::ArrayType::get(elem, N)` — flat, contiguous, exactly C's `T data[N]`, with `N` fixed for the run. The descriptor is built at the call: two stores, or nothing at all once the optimiser sees through it.

Flattening to a pointer-and-length parameter pair, which is the obvious alternative, fails on one case and it is not a rare one: **an array inside a struct has nowhere to put its count.** Passing `Pkt` — which holds `raw : byte[]` — cannot expand into extra parameters, so the descriptor has to be a type rather than a calling convention. Making it a type everywhere is the consistent choice.

Passing it **by value** is correct here, and does not contradict the by-pointer rule for structs below. A descriptor is two 8-byte scalars, which SysV classifies unambiguously as INTEGER,INTEGER and passes in two registers. The by-pointer rule exists for REF-declared structs, whose size and field types are arbitrary and whose classification is therefore fragile; a two-scalar synthesised aggregate has none of that risk.

##### `count` is capacity, not length

Worth being blunt about, because the descriptor invites the opposite reading. `count` is the array's *extent* — the number of elements it has, which for a per-record variable-length payload is the padded maximum. `raw : byte[]` is 64 octets in every record of a 64-octet capture, whether or not all 64 are meaningful.

So the descriptor does not remove the length argument:

```text
data pec_ok = crc8(pkt.raw, len - 1) == pkt.raw[len - 1];
```

`crc8` gets capacity 64 from the descriptor and the real length 63 from `len`. What the descriptor buys is that the callee can *check* — `assert(n <= s.count)` — which the flattened form cannot express at all, since there was nothing to check against.

The way to make `count` mean length is slicing (`pkt.raw[0:len]`), which is a language feature rather than an ABI one. It is listed under open questions; if it lands, the second argument goes away.

##### Fixed-width or `size_t`

`size_t` is what a C programmer expects and is 8 bytes on every host referee JITs for. It is 4 on ILP32, which matters only for an AOT checker cross-compiled to a 32-bit target — and there the generated header and the compiled object disagree silently. If AOT cross-compilation is ever in scope, this should be `uint64_t` and the header should say why.

#### Passing structs

Structs should be passed, and should be in v1 — they are the *cheapest* thing in this design, not the hardest. Two facts make that so.

**A REF struct is already a C struct in memory.** `TypeStruct::size()` and `alignment()` implement the C rule exactly — align each member to its own alignment, pad the total to the struct's — and `CompileTypeImpl::visit(TypeStruct*)` builds a **non-packed** `llvm::StructType`, so LLVM lays it out by the same ABI rule. The loader writes trace blobs with the same alignment arithmetic. Three independent pieces of the codebase already agree on a C layout, and the existing struct fixtures passing is evidence that they do.

**A struct-valued expression is already a pointer.** `visit(ExprData*)` loads only primitives; for a composite, `m_value` is the address of the storage. (This is exactly the fact that made enum equality compare pointers instead of values — the same property, in that case a bug, is here a gift.) So `func packet_ok : (Pkt) -> boolean` lowers to

```c
bool referee_packet_ok(const Pkt *arg0);
```

with no copy, no ABI classification, no `sret`, and no marshalling — the pointer the caller already holds is the argument.

So the rule is: **structs and arrays pass by `const` pointer; primitives pass by value.** By-*value* struct passing is the thing to avoid, because that is where the SysV register-classification rules live and where a mismatch between the header and the JIT would be silent. Passing by pointer sidesteps all of it, and is what a C programmer would write anyway.

**Struct returns stay out of v1.** They genuinely need `sret`, and the C answer is an out-parameter:

```text
func decode : (Pkt) -> Hdr;         // not v1
```

If it is wanted later, spell it as a call that writes through a pointer the caller owns. That does bend the purity contract — the function now writes — so it needs its own thought rather than falling out of the parameter design.

##### Two traps the generated header must avoid

Both are cases where a *hand-written* header is quietly wrong, and where a naively generated one would be too. They are the strongest argument for generating it at all.

**Enums are one byte, C enums are four.** `TypeEnum::size()` is 1 and REF stores the member in an `i8`. A generated `typedef enum { A, B } E;` is `int` in C — four bytes, four-byte aligned. For `struct { e : E; f : E; }` REF says 2 bytes and C says 8, and every field after an enum shifts.

**Enum values are 1-based, and 0 means "no member matched".** This is the one most worth checking against the code rather than assuming, because both halves are surprising. `TypeEnum::index()` returns the declaration position **plus one**, and the loader writes `i + 1` for a matching cell and leaves `0` when nothing matched. So

```text
type Dir : enum { M2S, S2M };
```

stores `M2S` as **1** and `S2M` as **2**, with `0` reserved. A C enum written the obvious way gives `M2S = 0`, which is wrong twice over: every member is off by one, and `M2S` collides with the sentinel that means *this cell did not name any member*. A specification would then read a malformed trace as `M2S` everywhere.

The header must therefore emit **explicit values**, never implicit ones.

**Booleans must be `bool`, not `int`.** REF stores one byte; `_Bool` is one byte and matches. `int` does not.

### 2. Generated header

```bash
referee header spec.ref -o spec.h
```

emits, from the same code that lays out the trace:

- a one-byte typedef plus member constants per REF `enum` — *not* a C `enum`, for the reason given above;
- a `struct` per REF `struct`, matching REF's layout and alignment, with unsized arrays resolved against the trace it was generated from;
- a prototype per `func`, taking composites by `const` pointer;
- a layout-version symbol covering both the specification and the resolved extents.

This is the load-bearing piece. C has no way to check that the function you linked matches the signature the specification declared — a mismatch is undefined behaviour, not a diagnostic. Generating the header is the only mechanism that makes the two agree, so the workflow should make it the obvious path: generate, `#include`, compile.

The header should also emit a version symbol the loader checks at startup, so a stale object built against an older specification fails loudly instead of reading the wrong offsets.

#### The specification is enough

An earlier draft of this design had `referee header` require a trace, on the
grounds that an unsized array makes a type's C layout trace-dependent. That is
true of a *struct member*, and false of everything else — and it was the wrong
default: a header describes types and signatures, and an array **parameter**
carries its extent in the descriptor built at the call, so the common case
needs nothing but the `.ref`.

Header generation therefore reads declarations only. Unsized arrays stay
unsized, and a quantifier that cannot be expanded stands in for a formula
nothing will look at — expressions are never examined, so neither is an error
there.

```bash
referee header spec.ref -o spec.h                    # the normal case
referee header spec.ref --trace capture.csv -o spec.h  # only if a named type
                                                       # has an unsized member
```

`--trace` remains for the one case that genuinely needs it: a named type whose
member array is unsized has a real trace-dependent layout, and passing a trace
makes that extent concrete. When it matters, the layout-version symbol must
cover the resolved extents, or an object built against a 64-octet capture
links cleanly against a 32-octet one and reads past the end of every packet.

### 3. Calling

A call is an ordinary expression:

```text
data pec_ok = crc8(pkt, len - 1) == pkt[len - 1];

globally, it is always the case that pec_ok holds;
```

A call is an expression like any other, and may appear **anywhere an expression may** — in a requirement, inside a temporal operator, inside a quantifier body, inside a pattern body, as an argument to another call. There is no restriction, and it should not acquire one: a call is not temporal, so none of the reasons that bar `G`/`F`/`Sum` from a pattern body apply to it.

Note nonetheless where the call went above: into a **computed signal**. That is an idiom worth documenting, not a rule. A computed signal is evaluated once per state by `__prepare__`; the same call written inside a temporal formula is evaluated once per state *per enclosing loop iteration*, which for a bounded operator is quadratic. The cost is invisible in the source — the two spellings look equally cheap — so the documentation has to say so even though the compiler permits both.

Calls are pure expressions as far as the compiler is concerned, so they participate in the existing loop-invariance analysis and can be hoisted when their arguments do not depend on the loop variable.

## The whole-state convention

The second half of the request: pass a const pointer to the entire state, including time, and let the function read whatever it needs.

```text
func packet_ok : (__state__) -> boolean;
```

`__state__`, not `state` and not `__all__`.

`state` is a usable signal name today — `data state : boolean;` parses, and in this domain a signal called `state` is not hypothetical. Reserving it would take it away, which is the same reason requirement names are spelled `@name` rather than with an `id` keyword.

`__all__` says "everything", but what is passed is specifically the state *at the point the expression is being evaluated*, which in a temporal formula moves. `__state__` says that, and pairs with the `__time__` that already exists — they are the same kind of thing, and in fact `__state__.__time__` and `__time__` are the same value. `__all__` also collides with a well-known meaning in Python, where it is a module's export list.

One caveat: `__time__` is not a reserved word. It is a plain identifier resolved specially, and `data __state__ : boolean;` parses today. So `__state__` is either reserved — cheap, and almost certainly breaks nobody — or made meaningful only inside a `func` parameter list. Reserving is simpler and gives a better error.

```c
bool referee_packet_ok(const referee_state_t *s);
```

This is genuinely convenient — one signature regardless of how many signals a function reads, and adding a signal does not change it. **I would not ship it first, and I would not ship it in the same shape as the value form.** Three reasons:

**It exposes an internal layout.** A state row today is `{ int64_t time; void *prop[N]; }` — a time followed by one pointer per property, with the property's data living elsewhere. That is an implementation choice, not a contract, and it will change: per-record extents (`T[<= N]`) will change it, and any move to a columnar layout would change it completely. The value form has no such coupling — `(const uint8_t*, int64_t)` stays true whatever the trace layout becomes.

**It converts a narrow contract into a broad one.** `crc8(pkt, len)` can only be wrong about the octets it was handed. `packet_ok(s)` is handed a pointer into the middle of a large buffer and can read any state in the trace by walking off the end of this one — accidentally, and with nothing to catch it. The instruction "the function should not touch anything outside the state" is exactly right, and exactly what C cannot enforce.

**The value form already covers the motivating case.** PEC needs the octets and the length. Nothing about it wants the whole state.

If it is built, it should be: a **generated** `referee_state_t` (never hand-written), carrying a layout version the loader verifies; accessor macros or inline functions in the generated header rather than direct field access, so the layout can change without breaking source compatibility; and `const` throughout.

## Purity, and what it costs

The language's current guarantee is that a `.ref` plus a trace determines the verdict. External functions puncture that: the verdict now also depends on a binary. For the use case where REF looks strongest — a conformance artifact reviewed against clause numbers — that is a real loss, and it is worth pricing rather than waving through.

Two mitigations, neither complete:

- **State the contract and mark it in the header.** A `func` must be a pure function of its arguments: same inputs, same output, no I/O, no globals, no mutation. Unenforceable, so it belongs in the generated header as a comment and in the documentation as a rule.
- **Record what was linked.** The report and the `.rdb` should carry the path and content hash of every loaded object. A verdict that depends on a binary should say which binary, or it is not reproducible in the sense the rest of the tool means.

## The workflow

The whole feature is three commands and one directory.

```bash
# 1. referee emits the header from the specification
referee header mctp.ref -o build/mctp.h

# 2. you write and compile the implementation
cc -shared -fPIC -I build helpers/mctp.c -o plugins/libmctp.so

# 3. referee finds it
referee execute mctp.ref captures/*.csv -L plugins
```

`-L` mirrors `-I` exactly: repeatable, searched in the order given, one directory per occurrence, `->allow_extra_args(false)` so it does not swallow the trace positionals that follow, `->check(CLI::ExistingDirectory)`. It is registered per subcommand for the same reason `-I` is — that is where people type it. Anything the specification can be compiled by (`execute`, `compile`, `header`) takes it.

### What gets loaded

Every `*.so` in every `-L` directory, in a **deterministic order** — directories in the order given, files sorted by name within each. `readdir` order is not stable across filesystems, and load order decides which definition of a duplicate symbol wins; a verdict that depends on the order the kernel happened to return directory entries is exactly the class of silent wrong answer this tool exists to avoid.

Two rules make the scan safe rather than merely convenient:

**A duplicate symbol is an error, not a race.** If two objects both define `crc8`, referee refuses to run and names both files. First-wins is indefensible here: the two implementations may differ, and nothing in the report would show which was used.

**Resolution is restricted to the loaded objects.** Not the process. ORC's default generator will happily resolve a symbol out of the host binary or libc, so `func read : (integer) -> integer;` would silently bind to `read(2)` and a typo'd name would bind to whatever else happens to be exported. Only symbols from `-L` objects count; anything else is "not found", named at startup.

**Nothing is loaded unless something is declared.** With no `func` in the specification, `-L` is inert — the directory is not even scanned. A specification with no external functions keeps the property that a `.ref` plus a trace determines the verdict, and pays nothing.

### Failure before any trace is read

All of it resolves at JIT-setup time, before the first row is loaded:

- a `func` with no matching symbol — names the function, the expected prototype, and the directories searched;
- a duplicate symbol — names both objects;
- a layout-version mismatch — names the object and both versions.

None of these should be discoverable halfway through a corpus.

### Why a directory rather than named libraries

Naming each object (`--library libmctp.so`) or naming it in the declaration (`func crc8 : … from "mctp";`) would make the mapping explicit and remove the duplicate-symbol question. It also puts a build-layout detail into the specification, or into every invocation, for no gain once the two rules above are in place: the scan is deterministic and collisions are refused. A plugin directory is the right shape — drop the `.so` in, and every specification in the project can use it.

The one thing it costs is that a specification does not record which object it needs. That is what the hash in the report is for (below).

## AOT checkers

For a compiled checker (see `native-checkers.md`) there is no scan and no loader: the objects are ordinary link inputs and the linker enforces both rules for free — an undefined symbol and a duplicate definition are both link errors. The runtime path is the one that has to reimplement what `ld` already does, which is an argument for the AOT path rather than against this one.

## Standard library

Yes — and the `func` mechanism makes the case for one rather than replacing it.

Once specifications can call C, every project will write the same four functions: a CRC, a popcount, a byte-swap, a sum over an array. Each copy is a chance to get the polynomial or the bit order wrong, each needs a build step, each breaks the reproducibility property above, and none of them is domain-specific. They should be built in.

I would split it in three tiers, by *mechanism* rather than by topic:

**Tier 1 — syntax, not functions.** A numeric fold belongs in the language, not a library:

```text
sum x in xs: x.len
```

It cannot be a `func`, because it needs the quantifier's compile-time expansion over an extent — the same machinery `all` and `one` already use. It is the single most-missed thing in all three MCTP examples: it is why `message_body_is_bounded` is written out one term per packet slot, and why "bytes from SOM to EOM" needs either a time window or a quadratic freeze. This is the highest-value item in this document and it does not depend on external functions at all.

**Tier 2 — built-in pure functions**, namespaced under `std::`, implemented
inside referee rather than shipped as a `.so`. **Built** for `std::math`.

A user should not need a plugin directory to call `sqrt`. Built in, they need
no `-L`, no install, no ABI and no header, and they keep the property that a
`.ref` plus a trace determines the verdict. They lower to LLVM intrinsics, or
to a couple of instructions where no intrinsic exists, so they behave
identically under a JIT and an ahead-of-time checker.

```text
std::math::abs     std::math::min      std::math::max        (integer)
std::math::fabs    std::math::fmin     std::math::fmax       (number)
std::math::sqrt    std::math::pow      std::math::exp
std::math::log     std::math::log2     std::math::log10
std::math::sin     std::math::cos
std::math::floor   std::math::ceil     std::math::round      std::math::trunc
```

`floor` / `ceil` / `round` / `trunc` return **integer**, not number. That is
the point of including them: REF has no cast, so number-to-integer conversion
was not expressible at all, and rounding is where a specification actually
wants it -- to compare against an integer signal, or to index with.

Separate integer and number spellings (`abs` / `fabs`) are C's workaround for
having no overloading, and should not survive: structural-hash symbols already
make overloading possible, since two signatures hash differently. What is left
is to let `Module::addFunc` hold more than one signature per name and to
resolve a call against them. Then `abs` serves both and `fabs` retires.

**Built:** `std::string` -- `len`, `nth`, `compare`, `starts`, `ends`, `find`.
These are host functions linked into referee and registered with the JIT, the
same arrangement `debug` uses, because there is no intrinsic for them. Still
no `.so` and no `-L`.

Every one returns a number or a boolean. None returns a *string*, deliberately:
with no allocator and no ownership model, a function building a new string
would have nobody to free it. So there is no `substr`, `concat` or `to_upper`
-- adding them means deciding who owns the result, which is a bigger question
than the functions are worth.

`nth` rather than the obvious `at`: `at` is reserved, being part of the
`at least` / `at most` quantifier vocabulary, so it cannot be an identifier.

`crc8` / `crc16` / `crc32` with named polynomials belong here too, and would
remove the motivating case for external functions entirely.

**Tier 3 — the escape hatch**, which is this document. For the genuinely domain-specific: a vendor's decode, a lookup table, a calculation that already exists.

The tiers should be built in that order, because each one removes reasons to need the next. Tier 1 is language work with no downside; tier 2 is bounded and safe; tier 3 is the one that costs a guarantee, and should be reached for last.

## What I would build, and in what order

1. **`sum x in xs:`** — largest gain, no cost to any existing guarantee, and independent of everything else here.
2. **Built-in CRC and bit intrinsics** — removes the motivating case for external functions entirely.
3. **`referee header`** — before the calling machinery, not after. It is what makes a signature mismatch a diagnostic instead of undefined behaviour, and the trace-dependence above means its shape needs settling first.
4. **`func` declarations, `-L` loading, and calls** — the escape hatch, against a header that already exists.
5. **The whole-state convention** — only if 4 proves insufficient in practice, and only with a generated, versioned struct and accessors.

## Open questions

1. ~~Should a `func` be callable from a pattern body?~~ **Settled: yes, and from any expression.** A call is not temporal, so the rule that bars looping operators from pattern bodies does not reach it. The cost of a call in a hot position is a documentation problem, not a grammar one.
2. **Failure signalling.** What does a `func` do when its input is malformed — return a sentinel, or is there an error channel? A sentinel that collides with a valid value is a silent wrong verdict. The simplest answer is that `func` results are total: the specification guards the call, not the callee.
3. **Should `--library` be recorded in the `.rdb`?** Traces are meant to outlive the specification. If a verdict depended on a binary, the trace arguably should say which — but a trace is a recording and a library is not part of it.
4. **Multi-dimensional arrays.** A descriptor of descriptors does not exist in memory — storage is flat, so `byte[][]` would need an array of inner descriptors materialised per call, which is O(rows) work for something rare in this domain. The alternatives are a strided descriptor (`{count, stride, data}`) or leaving 2-D out of v1. Leaving it out is probably right until something needs it.
5. ~~Slicing.~~ **Built, for calls.** `pkt[lo:hi]` lowers to the same
   descriptor an array does, except both fields are values: the count is
   `hi - lo` and the pointer is offset to element `lo`. So a call can be given
   exactly the octets that are real, with no length argument to keep in step.

   Not yet built for quantifiers, and it needs a different lowering there:
   expansion is compile-time, so a runtime extent cannot drive it. The way in
   is to desugar — `all x in pkt[0:len]: P` becomes expansion over the full
   extent with each conjunct guarded by `i < len`, which is exactly what
   people write by hand today.

   Bounds are not checked. A slice wider than its array reads past it, as an
   out-of-range index already does; there is no bounds checking to be
   consistent with, and adding it for this one construct would surprise.
5. **Should `-L` accept a file as well as a directory?** `-L plugins/libmctp.so` is unambiguous and would suit a build that produces one object. It costs nothing to allow, but two ways to say the same thing is its own tax.
6. ~~What names a symbol?~~ **Settled: `referee_`.** See *Symbol naming* above.
7. **May a `func` shadow a built-in?** Once tier 2 exists, `crc8` is both a built-in name and a plausible `func` name. Silently preferring one is a trap either way. Reserving the built-in names is the simpler rule, and `func` can always pick another.
8. ~~Does `referee header` need to emit the reverse — a stub `.c`?~~ **Built.**
   `referee header spec.ref --stub --header-name spec.h -o impl.c` writes the
   skeleton: every declared function with its exact signature, parameters
   voided so `-Wextra` stays quiet, and a zero return of the right kind. The
   generated pair compiles together untouched at `-Wall -Wextra -Werror`. The
   prototype spelling lives in one function the header and the stub both call,
   so they cannot drift.
