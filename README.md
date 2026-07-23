# Referee

Referee is a C++ compiler toolchain for the REF language, created to make formal requirement verification practical for real systems. It parses REF source files with ANTLR4, builds an abstract syntax tree, and lowers programs to LLVM IR with optimization passes.

The language and tooling are inspired by temporal-logic-based verification workflows (for example LTL/TPTL-style reasoning and requirement patterns), with the goal of expressing behavior constraints clearly, unambiguously, and in a way that is suitable for automated checking over traces and logs.

In short: Referee connects human-readable requirement intent to executable verification infrastructure through a dedicated domain language, compiler pipeline, and runtime-oriented architecture.

## Status

**What is implemented**

- A REF language front end: lexer, parser, and grammar (`core/referee.g4`) covering typed declarations (`type`, `data`, `conf`, `import`), structs, enums, arrays, and Dwyer-style property specification patterns (e.g. `globally, ...`, `before ..., ... eventually holds after N milliseconds`, `between ... and ..., it is always the case that ...`).
- A typed AST and a set of semantic visitors (`canonic`, `negated`, `rewrite`, `typecalc`, `printer`, `csvHeaders`).
- Lowering of temporal formulas (LTL/TPTL/MTL-flavoured, including strong/weak next `Xs`/`Xw`, bounded until/release, freeze variables, past operators) to **LLVM IR**, followed by standard LLVM optimization passes. `Us`/`Uw`/`Rs`/`Rw`/`Ss`/`Sw`/`Ts`/`Tw` are lowered to linear passes over the trace rather than the naive nested scan, bounded forms included — see *Temporal lowering* below.
- **`import`** — split a specification across files: shared definition files, or one index file pulling in a directory of small requirement files. Resolved relative to the importing file plus `-I` search paths, imported once per real path, with file-qualified requirement labels. See *Splitting a specification across files* below.
- **Quantifiers** over array elements — `all` / `some` / `none` / `one`, plus `at least N` / `at most N` — and `xs.count` for the element count. Over a sized array they expand at compile time; over an unbounded array they lower to a runtime loop. See *Quantifiers* below; the design notes are in `docs/quantifiers.md`.
- **Unbounded (ragged) arrays** — `T[]` is a `{count, pointer}` descriptor whose length arrives with each record, so rows may differ in extent. `.count`, indexing, slicing and quantifiers all work through the descriptor. See *Unbounded arrays* below and `docs/ragged-arrays.md`.
- **`byte` and bitwise operators** (`&`, `|`, `^`, `~`, `<<`, `>>`) for reasoning about binary protocols octet by octet. See *`byte`, and reasoning about a wire* below.
- **Short-circuiting and bounds checking** — `&&` / `||` / `=>` / `? :` evaluate only what they must, so a guard actually guards; an index outside its array is a compile-time error for a written extent and a reported requirement failure for a runtime one.
- **External functions** (`func name : (types…) -> type;`) resolved from a `.so` at run time, with a generated header and stub, `::` namespacing, overloading, slice arguments, and a whole-state `(__state__)` calling convention. See *External functions* below and `docs/external-functions.md`.
- **Accumulators** `Sum` / `Cnt` / `Itg`, each folded in a single linear pass over the trace. See *Expressions* and *Computational complexity* below.
- **Computed signals** (`data Name = expression;`) — named derived signals, including temporal ones, evaluated once per state for the whole trace by a generated `__prepare__` pass and then read like any other signal. See *Computed signals* below.
- **Run traces** — `referee execute … --explain run.ndjson` records per-requirement columns, subexpression rows, scope intervals and computed vacuity for a viewer (`tools/view.py`, `tools/view_bokeh.py`) or for CI. See *Run traces* below.
- A JIT-based test harness (`test/logic.cpp`) that compiles REF files, JITs them against a synthetic trace (`state_t[]` + `conf_t`), and asserts that each requirement evaluates to `true` (pass) or `false` (fail) over that trace. See `test/logic/pass.ref` and `test/logic/fail.ref` for the intended execution model.
- A CLI with two subcommands:
  - `referee compile file.ref [-I dir]…` — emits LLVM IR for a given `.ref` file.
  - `referee execute file.ref trace.{csv,yaml,rdb}… [--success …] [--failure …] [--conf conf.{csv,yaml}] [-v 0..2] [-I dir]…` — JIT-compiles the requirements and evaluates them against one or more traces, reporting `PASS`/`FAIL` per requirement (and exiting non-zero if any requirement fails). Several traces are compiled **once** and checked in turn; `--failure` declares traces that must be rejected. See *Checking several traces* below. Column names in the CSV/YAML must match the layout produced by `csvHeaders` (e.g. `__time__`, `pos.x`, `limits[0]`, …); see `test/logic/data.csv` and `test/logic/conf.csv` for working examples. `.rdb` traces (see below) are read with no per-row processing — only pointer fix-up.
- An **editor plugin** (`editors/vscode/`) giving syntax highlighting, bracket/comment handling and specification-pattern snippets for `.ref` files in VS Code and its forks (Cursor, Antigravity, VSCodium). Its keyword lists are generated from `core/referee.g4` rather than hand-written, and the grammar is tested against the same TextMate engine the editors use. See *Editor support* below.
- A companion `rdb` binary for packing CSV/YAML traces into the on-disk **RDB** format consumed directly by `referee execute`:
  - `rdb build spec.ref trace.csv [--conf conf.csv] [-I dir]… -o trace.rdb` — packs a CSV/YAML trace into a `.rdb` whose state-buffer section is byte-for-byte the layout the JIT consumes (see *Referee Database* below).
  - `rdb dump trace.rdb` — pretty-prints the schema, conf, and per-state rows using the AST types embedded in the file.

**What is missing**

- **Ahead-of-time compiled checkers.** An object file, a loadable `.so`, or a standalone executable, so a machine that validates logs needs neither LLVM nor the `.ref` sources. Designed in `docs/native-checkers.md`, which also records the measurements above — the motivation is deployment and embedding rather than speed, since most of the speed is available from the previous item.
- **Streaming / online ingestion.** `referee execute` currently materialises the whole trace before invoking the JIT functions (CSV/YAML are parsed top-to-bottom into per-state blobs; `.rdb` is read into a single buffer). A streaming driver that feeds records to the JIT-compiled requirement functions as they arrive (and reports violation locations live) is not implemented yet — the on-disk `.rdb` layout is already mmap-friendly, so this is a Reader-side change rather than a format change.
- **A language server.** The editor plugin does highlighting only, so there are no in-editor diagnostics, completion, hover or go-to-definition — errors still come from running `referee compile`. The compiler already produces positioned diagnostics (`file:row:col`), so the missing piece is the LSP plumbing rather than the analysis.
- **Product-specific model exporters/importers** to adapt arbitrary system logs into the canonical trace format.

## Design rationale

The usual way to check behavioural requirements against a log is to hand-write a checker per requirement — a state machine with flags, counters, and event handlers. Referee exists because that approach degrades badly as the requirement set grows. The design choices below are each a response to a specific way it degrades.

1. **Declarative, not imperative.** A requirement is written once, in a notation meant for it. "Between `called` and `opened`, a transition to `atfloor` occurs at most twice" is a single line of REF. Written as a checker it is a class whose flags and counters have to be re-derived, by hand, every time the requirement is reworded — and the requirement and the code that checks it drift apart silently, because nothing connects them.
2. **Patterns instead of raw formulas.** Engineers write Dwyer-style English-like patterns; the compiler translates them into the underlying temporal-logic formulas, handling finite-trace semantics, strong/weak next, and bounded operators uniformly. Hand-written checkers re-implement each pattern ad hoc, and the off-by-one and end-of-trace mistakes that come with that are made once per requirement rather than once per compiler.
3. **Strong types, not just booleans.** REF has integers, numbers, strings, enums, structs, and multi-dimensional arrays, so a requirement can talk about signal values directly (`lock.ON`, `abc.x[2][3].a`). Without that, every requirement sits behind a hand-maintained layer that flattens real signals into booleans — a layer that is itself untested and a reliable source of bugs.
4. **All requirements evaluated together.** Every property is compiled into one module and evaluated over the same trace, so mutually inconsistent requirements become observable: they show up as a trace that no requirement set can satisfy. A collection of independent checker scripts cannot surface a contradiction between two of them at all.
5. **Native performance.** Requirements compile to optimized LLVM IR and run as native code via the ORC JIT, with the unbounded temporal operators lowered to a linear pass over the trace. Checking a long trace against hundreds of properties is the normal case, not the stress case.
6. **Separation of concerns.** Recording what the system did and checking that it was allowed to are fully decoupled. The same compiled requirements apply to real logs, simulated traces, offline batches, or (once streaming lands) a live feed, with no change to the requirement source.
7. **Reviewable by the people who own the requirements.** Reading and writing a REF file needs an understanding of the system, not programming fluency. A requirement that a test engineer can review is a requirement that gets reviewed.

## The REF Language

REF is a small domain-specific language for describing system requirements as properties over timed traces. The authoritative grammar lives in `core/referee.g4`; this section is a tour of the surface syntax.

A REF program is a semicolon-terminated sequence of three kinds of statements:

- **Declarations** — define the vocabulary used by the requirements.
- **Expressions** — plain temporal-logic formulas that must hold on the trace.
- **Specification patterns** — English-like Dwyer-style phrasings that desugar to temporal-logic formulas.

Every non-declaration statement in a REF file is a first-class **requirement**: the compiler emits one LLVM function per statement, and each such function must evaluate to `true` on every valid trace of the system. Declarations produce no runtime functions of their own — they only introduce names and shapes that the requirement expressions refer to.

Comments use `//`, `#`, or C-style `/* ... */`. Identifiers follow the usual C convention (`[a-zA-Z_][a-zA-Z0-9_]*`). Whitespace is insignificant; statements are terminated by `;`.

### Lexical details

- **Boolean literals:** `true`, `false`.
- **Integer literals:** decimal (`42`), binary (`0b1010`), octal (`0o755`), and hexadecimal (`0xFF` / `0xff`). Integers are arbitrary-width at the source level and lowered to 64-bit at runtime.
- **Floating-point literals:** `1.5`, `.25`, `3.14e-2`, `1E6`. Used wherever the `number` type is expected.
- **String literals:** `"..."` containing ASCII letters, digits, `_`, `.`, `?`, `!`, `/`, `-`, and spaces (the last three so that `import` targets can name paths). Strings are first-class values of type `string` and participate in `==` / `!=` comparisons.
- **Signed literals:** a leading `+` / `-` in front of a numeric literal is part of the literal, not a separate unary operator, so `-3` is an integer constant while `- x` is unary negation on `x`.
- **Reserved keywords.** In addition to the temporal-logic operator names (`G`, `F`, `Xs`, `Xw`, `Us`, `Uw`, `Rs`, `Rw`, `H`, `O`, `Ys`, `Yw`, `Ss`, `Sw`, `Ts`, `Tw`) and the accumulators (`Itg`, `Sum`, `Cnt`), the spec-pattern vocabulary is reserved: `after`, `afterwards`, `always`, `and`, `at`, `becomes`, `been`, `before`, `between`, `by`, `case`, `continually`, `eventually`, `every`, `followed`, `for`, `globally`, `has`, `have`, `holding`, `holds`, `if`, `in`, `interruption`, `is`, `it`, `least`, `less`, `long`, `must`, `never`, `occurred`, `once`, `remains`, `repeatedly`, `response`, `run`, `satisfied`, `so`, `than`, `that`, `the`, `then`, `until`, `while`, `within`, `without`, plus the time units `nanoseconds`, `microseconds`, `milliseconds`, `seconds`, `minutes`.

### Declarations and Types

Four declaration kinds introduce names into the program:

- `type Name : T;` — a named type alias. It defines a new, reusable type but does not reserve any runtime storage.
- `data Name : T;` — a **time-varying signal** sampled once per trace record. It is effectively a field of the per-timestamp `state_t` struct.
- `conf Name : T;` — a **configuration value** that is constant for the whole trace. It is effectively a field of the `conf_t` struct shared by all records.
- `import "path.ref";` — pull another REF file into this one (see *Splitting a specification across files* below).

Splitting `data` from `conf` is a deliberate modeling choice: signals that change per event (sensor readings, state machine outputs) live in `data`, while things that are set at the start of a run and never change (thresholds, limits, operating mode) live in `conf`. The compiler uses that distinction to generate a correct trace/config memory layout (see `core/visitors/csvHeaders.cpp`, which derives CSV column names from `data` declarations).

#### Splitting a specification across files

`import` folds another REF file into the current one, at the point of the import:

```text
import "common/types.ref";
import "reqs/door.ref";
```

Everything the imported file declares — types, signals, configuration — becomes visible to the statements that follow, and any requirements it contains are compiled and checked alongside the importing file's own. That covers both of the shapes this exists for: a *definitions* file that several specifications share, and an *index* file that pulls a directory of small requirement files into one run.

**Resolution.** A relative path is resolved against the directory of the file containing the import, so a tree of specifications can be moved as a unit. If that misses, each `-I <dir>` given on the command line is tried in order:

```bash
./build/referee execute -I ./shared spec.ref trace.csv
./build/rdb     build   -I ./shared spec.ref trace.csv -o trace.rdb
```

`rdb build` takes the same flag because it has to resolve the same imports to derive the schema.

**Each file is imported once**, keyed on its real path. A diamond — two requirement files that both import the same definitions — is therefore the ordinary case rather than a duplicate-declaration error, and symlinks and `..` round-trips to the same file are recognised as the same file. Two *different* files declaring the same name is still an error, and says so.

An import that leads back to a file already being read is reported as a cycle rather than quietly skipped, because the importer would otherwise carry on referring to declarations that have not been processed yet.

**Requirement labels become file-qualified.** A requirement that came in through an import is reported (and named in the emitted IR) as `path/to/file.ref:row:col .. row:col`, with the path relative to the root file. Requirements in the root file itself stay unqualified, so a single-file specification is labelled exactly as it was before imports existed:

```text
13:0 .. 13:21                            PASS
reqs/one.ref:5:0 .. 5:10                 PASS
reqs/two.ref:5:0 .. 5:10                 PASS
```

The qualification is load-bearing, not cosmetic: the last two requirements sit at the same line and column in different files, and would otherwise collide into one name.

#### Unbounded arrays

An array declared without an extent is **unbounded**: each record carries its own length.

```text
data readings : integer[];      // however many this record holds
data grid     : integer[][];    // ragged in both dimensions
```

The value of an unbounded array is a `{count, pointer}` descriptor rather than inline storage. `readings.count` is a load of that count — so it answers a size the specification never wrote down, and answers a *different* size at each state if the records differ:

```text
G(readings.count <= 64);                    // a bound on the length
G(all v in readings: v >= 0);               // a loop over however many there are
G(readings.count > 0 => readings[0] >= 0);  // guard the access with the count
```

The same specification holds against traces whose records are different lengths — a message with three octets and one with seven, in the same run — which is the point of leaving the extent out. `T[N]` is unchanged and still expands at compile time; only `T[]` is new, so nothing that compiled before compiles differently.

In CSV a ragged trace sizes its header to the widest record and marks a cell that is not an element with `-` (or leaves it empty); a present element after an absent one is rejected, since an array has no holes:

```text
__time__,pkt[0],pkt[1],pkt[2]
0,0xDE,0xAD,0xBE       # three octets
1000,0x01,0x02,-       # two
2000,-,-,-             # none
```

Design notes are in `docs/ragged-arrays.md`.

#### Quantifiers

A requirement over an array can range over its elements instead of naming each one:

```text
data limits : integer[4];

all limit in limits: limit < max;       // every element
some limit in limits: limit < max;      // at least one
none limit in limits: limit < max;      // not any
one  limit in limits: limit < max;      // exactly one
at least 2 limit in limits: limit < max;
at most  2 limit in limits: limit < max;
```

The body runs to the end of the statement, so parenthesise it when something follows: `!(all x in v: x > 2)`.

**Binders.** One name binds the *element*. A second binds the index, and `_` discards either:

```text
all x    in v: x > 0;                   // element
all x, i in v: x * 10 == w[i];          // element and index — correlates two arrays
all _, i in v: w[i] > v[i];             // index only
```

**Nesting** handles multiple dimensions, rather than a third binder:

```text
all row in g: all p in row: p > 0;
```

**Quantifiers and temporal operators compose in both orders**, and mean different things:

```text
all p in xs: G(P(p));       // each element satisfies P at every state
G(all p in xs: P(p));       // at every state, every element satisfies P
```

Over an array of **known size**, a quantifier expands while the AST is built — `all` to a conjunction, `some` to a disjunction, the counted forms to a sum of indicators compared against the bound. Nothing reaches runtime and nothing changes in the trace format.

Over an **unbounded** array (`T[]`) there is no size to expand over, so the quantifier lowers to a **runtime loop**: it counts the elements whose body holds and compares that against the length, which is the same reduction the counted forms already use. `all v in pkt: v > 0` is then O(length) at each state it is evaluated, rather than a constant. A temporal operator *inside* such a quantifier is rejected — the buffers that make it linear are built once per node, before the loop's index exists — so quantify over the values and put the temporal operator outside. A quantifier may also appear in a computed signal (`data any_big = some x in v: x > 3;`).

The domain must be an array; quantifying over anything else is rejected at the quantifier itself.

**An array's element count** is available as `xs.count`:

```text
data limits : integer[4];
data grid   : integer[3][2];

limits.count == 4;
grid.count == 3;            // the outer dimension
grid[0].count == 2;         // the inner one
all _, i in limits: i < limits.count;
```

It is known when the AST is built, so it lowers to a literal — no load, no state access. `count` is resolved by the type it is applied to, so a struct field of the same name is unaffected, and an array has no other member.

#### Computed signals

`data` has a second form that gives a name to a derived signal instead of declaring a column in the trace:

```text
data Name = expression;
```

The type is inferred from the expression, and the expression may use anything already in scope — other `data` signals, `conf` values, and the full operator set including the temporal operators:

```text
data a : boolean;
data b : boolean;

data both      = a && b;          // point-wise
data seen_a    = O(a);            // "a has occurred at some point in the past"
data next_both = Xs(both);        // computed signals may build on each other
```

This exists to let a recurring sub-formula be named once and referred to everywhere, rather than pasted into a dozen requirements. It is also a performance lever: a computed signal is evaluated exactly once per state for the whole trace, whereas the same sub-formula written inline in ten requirements is evaluated ten times.

Two consequences worth knowing:

- **Declaration order matters.** A computed signal can only reference names declared before it, so forward and circular references cannot be written. Dependencies are resolved in declaration order.
- **Computed signals are not stored in `.rdb` files.** They are a property of the specification, not of the recording, so `rdb build` writes only the trace-backed signals and `referee execute` recomputes the derived ones from the `.ref` at run time. A `.rdb` therefore stays valid when a computed signal's defining expression changes — only the `.ref` needs to be re-read. See `test/logic/expr_data.ref` and `test/logic/expr_chain.ref` for worked examples.

The type grammar supports:

- **Primitive types:** `boolean`, `byte` (8-bit unsigned), `integer` (64-bit signed), `number` (floating point), `string`.
- **Enumerations:** `enum { A, B, ... }` — a finite set of named values referenced as `T.A`, `T.B`. Enums are nominal: two enum types with the same members are distinct.
- **Structures:** `struct { field1: T1; field2: T2; ... }` — record types with named, typed fields. Nesting is allowed (`struct { inner: struct { ... }; }`).
- **Arrays:** `T[N]`, a fixed extent written in the specification, which may be stacked to form multi-dimensional arrays `T[N][M]`. Dimensions read as they do in C, C++ and Kotlin: `integer[3][2]` is **three** arrays of **two**, the first subscript written is the outer one, and `g[2][1]` is the last element. Writing `T[]` instead makes the array **unbounded** — each record carries its own length, so `payload : byte[]` is a genuinely ragged array whose rows may differ in extent, and `T[][]` is ragged in both dimensions. `T[N]` and `T[]` are genuinely different types: the first is inline storage with a constant extent and its `.count` folds to a literal, the second is a `{count, pointer}` descriptor whose `.count` is a load and whose elements the loader places per record. See *Unbounded arrays* below.
- **Named type references:** any previously-declared `type Name : ...` can be used wherever a type is expected, anywhere more complex types are built (struct fields, array elements, other aliases).

```text
type Button : enum   { DEPRESSED, RELEASED };
type State  : enum   { ON, OFF };
type Door   : enum   { OPENED, CLOSED };
type Point  : struct { x: number; y: number; };
type Matrix : Point[3][3];

data button : Button;
data lock   : State;
data alarm  : State;
data door   : Door;
data pos    : Point;

conf xyz    : struct { a: integer; b: number; limits: integer[4]; };
conf grid   : Matrix;
```

Member access uses `.` (`lock.ON`, `pos.x`, `xyz.limits`) and array indexing uses `[...]` (`xyz.limits[2]`, `grid[1][2].x`). Accesses nest in the obvious way, so `data abc : struct { x: integer[2][3]; };` permits `abc.x[1][2]`.

#### `byte`, and reasoning about a wire

`byte` is a **storage width, not a value kind**. It occupies one octet in a trace row instead of eight, and every read of one widens to `integer`, so no arithmetic, comparison or accumulation rule has to know it exists — `flag & 0x80` type-checks against integer operands with no cast, and `Sum(true, payload[0])` totals bytes as integers. A cell outside `0..255` is refused at load time rather than truncated, since a payload octet quietly becoming a different value is precisely what a checker exists to catch.

It exists for binary protocols. A message arrives pre-framed — someone has already cut the stream into records — and the framing lives in individual bits of an octet:

```text
conf k       : struct { SOM: boolean; EOM: boolean; };
data flag    : byte;
data payload : byte[];          // extent from the trace

G(k.SOM <=> (flag & 0x01) != 0);        // start-of-message bit
G(k.EOM <=> (flag & 0x02) != 0);        // end-of-message bit
G(flag >> 4 <= 3);                      // sequence number, top nibble

data xorsum = payload[0] ^ payload[1] ^ payload[2] ^ payload[3];
```

Without `byte` a payload costs eight bytes per octet in every row; without the bitwise operators the framing bits are reachable only as `(flag / 128) % 2 == 1`. Neither half is much use alone. See `test/logic/bytes.ref`.

Enum members are reached **through the signal**, not through the type: `lock.ON` reads as "the current value of `lock` is `ON`" and is the form requirements use to turn a raw signal into a boolean. Writing the member against the type instead (`State.ON`, or `lock == State.ON`) is *not* accepted — the parser resolves the head of a dotted name as a signal, so a type name there fails to resolve.

### Expressions

REF expressions combine the familiar C-family operator set with dedicated temporal operators. Outside of the temporal layer, the non-temporal expression language is what requirement authors use most of the time.

**Operators, from tightest to loosest binding.** The ordering follows C++ and Kotlin, which agree on all of these: multiplicative, additive, shift, relational, equality, `&`, `^`, `|`, `&&`, `||`. `=>` and `<=>` have no C++ equivalent and sit where logic conventionally puts them — looser than `||`, tighter than the conditional. So `2 + 3 * 4` is 14, `a || b && c` is `a || (b && c)`, and `a && x <= 5` needs no parentheses.


| Category    | Operators                                  | Notes                                            |
| ----------- | ------------------------------------------ | ------------------------------------------------ |
| Postfix     | `.field`, `[index]`, `[lo:hi]`, `.count`   | Member access, array indexing, half-open slice, element count. |
| Unary       | `!`, `-`, `~`                              | Logical negation, arithmetic negation, bitwise complement. |
| Multiplicative | `*`, `/`, `%`                           | `%` is integer modulo.                           |
| Additive    | `+`, `-`                                   |                                                  |
| Shift       | `<<`, `>>`                                 | Integers only. `>>` is arithmetic — REF integers are signed. |
| Relational  | `<`, `<=`, `>`, `>=`                       |                                                  |
| Equality    | `==`, `!=`                                 | Works on booleans, numbers, strings, enums.      |
| Bitwise AND | `&`                                        | Integers only.                                   |
| XOR         | `^`                                        | Bitwise on integers, logical on booleans.        |
| Bitwise OR  | `\|`                                       | Integers only.                                   |
| Logical AND | `&&`                                       |                                                  |
| Logical OR  | `\|\|`                                      |                                                  |
| Implication | `=>`, `<=>`                                | Material implication and biconditional.          |
| Ternary     | `cond ? then : else`                       | Selects between two values of the same type.     |
| Grouping    | `(...)`                                    |                                                  |

The precedence is C's, exactly — including the part everyone trips over: `&`, `^` and `\|` bind *looser* than `==`, so `flag & 0x80 != 0` reads as `flag & (0x80 != 0)`. C computes that silently; REF rejects it, because `!=` yields a boolean and the bitwise operators take integers. Write `(flag & 0x80) != 0`.

`^` is the one operator that does double duty — bitwise xor on integers, logical xor on booleans — because there is no `^^` to pair with `&&` and `\|\|`. `&` and `\|` stay integers-only for that reason: booleans already have the doubled spellings.

`=>` and `<=>` are first-class operators, not macros: `p => q` is exactly `!p || q`, and `p <=> q` is `p == q` restricted to booleans, but writing them with the logical spelling makes requirement intent immediately readable ("whenever `p`, then `q`").

**`&&`, `||`, `=>` and `? :` short-circuit.** The right-hand side of `&&` is evaluated only where the left held, the right of `||` only where the left did not, and only the taken arm of a ternary. That is what lets a guard actually guard: `x != 0 => y / x > 1` does not divide where `x` is zero, and `n.count > 0 && n[0] > 0` does not index an empty array. The operators lower to branches, not to a `select` over both operands — so the guarded expression is never reached when the guard says not to.

**Indexing is bounds-checked.** A constant index outside a written extent (`v[7]` on a `T[3]`) is a compile-time error. A runtime index outside the length — into an unbounded array, or with a computed subscript — fails the requirement rather than reading past the row, and the report names the index and the count. Combined with short-circuiting, `n.count > i => n[i] == …` is the safe idiom.

**Types in expressions.** Arithmetic mixes `integer` and `number`, with the usual widening to `number`, and comparisons widen the same way — `i < n` with `i : integer` and `n : number` is fine. Otherwise comparisons are homogeneous: two strings, two booleans, or two values of the same enum type, but not, say, an integer against a string. The ternary operator requires both arms to have a common type. These rules are enforced by the `typecalc` visitor before LLVM IR is generated, so type errors surface at compile time.

**Literals in expressions.** Besides the usual numeric literals described above, an enum-valued signal is tested by naming the member on the signal — `lock.ON`, `door.CLOSED`. There is no implicit coercion from strings or integers, and no way to write a bare enum constant detached from a signal.

Temporal operators come in both **future** and **past** flavours, and most come in **strong** and **weak** variants to give well-defined semantics on finite traces:

| Future | Past | Meaning |
| --- | --- | --- |
| `G(p)` | `H(p)` | `p` holds at every state (globally / historically) |
| `F(p)` | `O(p)` | `p` holds at some state (eventually / once) |
| `Xs(p)` / `Xw(p)` | `Ys(p)` / `Yw(p)` | strong/weak next / yesterday |
| `Us(p, q)` / `Uw(p, q)` | `Ss(p, q)` / `Sw(p, q)` | strong/weak until / since |
| `Rs(p, q)` / `Rw(p, q)` | `Ts(p, q)` / `Tw(p, q)` | strong/weak release / triggered |
| `Itg(v)` / `Itg(c, v)` | — | **integral** over time of a numeric `v`; the two-argument form integrates only while boolean `c` holds |
| `Sum(c, v)` | — | **total** of numeric `v` over the records where `c` holds |
| `Cnt(c)` | — | **count** of the records where `c` holds |

All temporal operators optionally accept a **time bound** `[lo:hi]`, `[:hi]`, or `[lo:]`, giving MTL-style bounded versions such as `G[100:1000](a)` or `Us[1:3](alpha, beta)`.

The three accumulators share one shape: a condition selects the states that contribute, states where it fails are skipped rather than ending the walk, and the extent is set by the `[lo:hi]` window. What differs is the weight a contributing state carries — `Itg` weights it by its duration, `Sum` by a value, `Cnt` by one. That is the difference between "how long was the valve open" and "how many bytes were in this message".

```text
G(k.SOM => Cnt[0:5000](k.MID) <= 8);        // at most 8 packets per message
G(k.SOM => Sum[0:5000](true, len) <= 4096); // and at most 4096 bytes
```

Accumulation runs forward from the current state, so the window is what bounds a per-message requirement — the condition chooses states, it does not delimit them. Without a window the walk reaches the end of the trace. `Cnt(c)` is `Sum(c, 1)`.

All three fold in a single backward pass — the same linear lowering the until/release family uses, weighted by the accumulator's own contribution — so an unbounded accumulator under `G` is O(N) across the trace rather than O(N²). See *Computational complexity* below.

### External functions

Some requirements need arithmetic the language cannot express and should not grow syntax for. The motivating one is concrete: MCTP over SMBus ends every packet with a PEC, a CRC-8 over the transaction. REF has no fold over an array and no function abstraction, so the only spelling in the language itself is the fully unrolled polynomial — eight shift/xor steps per octet, across every octet.

A `func` declares a native function; the implementation is a `.so` referee loads at run time.

```text
func crc8 : (byte[], integer) -> byte;

data pec_ok = crc8(pkt, len - 1) == pkt[len - 1];

globally, it is always the case that pec_ok holds;
```

```bash
referee header spec.ref -o spec.h                              # types + prototypes
referee header spec.ref --stub --header-name spec.h -o impl.c  # a skeleton to fill in
cc -shared -fPIC -I . impl.c -o plugins/libspec.so
referee execute spec.ref trace.csv -L plugins
```

The generated header is the load-bearing piece: C cannot diagnose a signature mismatch — it is undefined behaviour, not an error — so referee emits the header and, with `--stub`, the implementation skeleton too, from the same table it uses to compile the call. The specification alone is enough; no trace is needed.

- **Symbols carry a `referee_` prefix.** `func read` binds to `referee_read` and so cannot reach `read(2)`, and referee inspects only `referee_*`, so one plugin's private helpers cannot collide with another's.
- **Names may be namespaced** with `::` to any depth — `func std::math::sqrt`, mangled to `referee_std__math__sqrt`. It is a lexical convention, not a scoping construct. `.` could not be used: it is member access.
- **A slice gives a call the length it means.** `pkt[lo:hi]` is the elements from `lo` up to but not including `hi`, and its extent is a value rather than a compile-time constant — so `crc8(pkt[0:len])` passes exactly the octets that are real, with no second argument to keep in step.
- **Arrays cross as a `{count, data}` descriptor**, structs by `const` pointer, enums and other primitives by value. For an unbounded array `count` *is* the length; for a sized one it is the written extent. Either way the callee reads exactly `count` elements and needs no separate length argument.
- **A whole-state function** takes the state at the point of evaluation instead of individual arguments: `func packet_ok : (__state__) -> boolean;`. `__state__` is a parameter list, not a type. The callee reads signals through accessors the generated header defines — never by laying out the row itself, which is an implementation detail that has changed — and the object states which layout it was built against, checked before anything runs. Which state is passed *moves*: inside a temporal operator it is the state the walk has reached. Prefer a value signature where one will do; the whole-state form is a broad contract where a value one is narrow.
- **`-L` takes a `.so` or a directory of them**, repeatable. Loading is deterministic, a duplicate entry point is an error rather than a race, resolution never falls back to the host process, and a specification declaring no `func` never scans the path at all.

Everything resolves before the first trace row is read. See `docs/external-functions.md` for the design, and `examples/extfunc/` for a worked example.

A **freeze variable** `name@(... expression ...)` binds the current state to `name`, so subexpressions can reference data at that frozen point — e.g. `x@(F(x.abc.a == 3))` means "there is a future state whose `abc.a` equals the value `abc.a` had at the freeze point". A special `__time__` identifier refers to the current timestamp.

```text
// A problem must be followed by an alarm within 5 seconds
G(problem => t@(F(alarm && __time__ - t.__time__ <= 5)));
```

### Specification Patterns

On top of raw temporal-logic expressions, REF provides Dwyer's property specification patterns so requirements can be written in near-natural English. The scope comes first (one or more of `globally`, `before P`, `after P`, `while P`, `between P and Q`, `after P until Q`), followed by a pattern body:

- `it is always the case that P holds [time bound];`
- `it is never the case that P holds [time bound];`
- `P eventually holds [time bound];`
- `P holds after N units;` / `P holds in the long run;`
- `once P becomes satisfied it remains so for at least N units;`
- `once P becomes satisfied it remains so for less than N units;`
- `P holds repeatedly [every N units];`
- `if P, then in response S eventually holds [time bound];`
- `if P, then it must have been the case that S has occurred before it;`
- Response / precedence **chains** (`... followed by ...`), response **invariance** (`... holds continually ...`), and `P holds without interruption until S holds`.

Time bounds may be `within N units`, `after N units`, or `between N and M units`, where `units` is one of `nanoseconds`, `microseconds`, `milliseconds`, `seconds`, `minutes`.

Example — an elevator system:

```text
type Button : enum { DEPRESSED, RELEASED };
type State  : enum { ON, OFF };
type Door   : enum { OPENED, CLOSED };

data button : Button;
data lock   : State;
data alarm  : State;
data door   : Door;

before button.DEPRESSED, lock.ON eventually holds after 100 milliseconds;
globally, it is never the case that door.CLOSED && alarm.ON;
while door.OPENED, it is always the case that alarm.ON after 30 seconds;
globally, if button.DEPRESSED, then in response lock.ON after 100 milliseconds;
globally, once lock.ON becomes satisfied it remains so for at least 2 seconds;
globally, once lock.ON becomes satisfied it remains so for less than 3 seconds;
between door.CLOSED and lock.OFF, it is always the case that door.CLOSED;
after lock.ON, if door.OPENED, then it must have been the case that lock.OFF has occurred before it;
after lock.ON, if lock.OFF, then it must have been the case that button.DEPRESSED has occurred before;
```

Each of these lines compiles to a boolean-valued function over the trace; the runtime asserts that every such function returns `true` for every valid trace of the system.

### Evaluation Model

A REF program does not describe a computation that produces outputs; it describes **predicates over a trace**. The compiler and runtime together work as follows.

1. **Declarations shape the trace.** The set of `data` declarations determines the schema of each trace record (a `state_t` in generated code), and the set of `conf` declarations determines the static configuration (`conf_t`). `csvHeaders` walks these declarations and produces the matching flat column layout used for CSV ingestion — e.g. `data pos : struct { x: number; y: number; };` becomes two columns, `pos.x` and `pos.y`. Array fields expand to one column per element (`limits[0]`, `limits[1]`, …), the outer dimension varying slowest — a `data g : integer[3][2];` becomes `g[0][0]`, `g[0][1]`, `g[1][0]`, `g[1][1]`, `g[2][0]`, `g[2][1]`.
2. **A trace is an ordered list of states plus a configuration.** Each state carries a timestamp (implicitly exposed to REF as the `__time__` pseudo-identifier inside freeze scopes) and values for every declared `data` field. The configuration carries values for every declared `conf` field. The trace is assumed to be **finite**; strong/weak variants of the temporal operators exist specifically so requirements behave correctly at the end of that finite trace.
3. **Every requirement statement becomes one function.** For a statement `R` at file position `P`, the compiler emits an LLVM function named after `P` with the signature `bool eval(const state_t* states, size_t n, const conf_t* conf)` (more precisely, a pointer to the first state and the current-index bookkeeping that the temporal operators need). That function returns `true` iff the trace satisfies `R`.
4. **The module is optimized, then JIT-compiled.** `Referee::compile` runs a fixed pipeline of LLVM passes (instruction combining, reassociation, GVN, CFG simplification, loop strength reduction, loop data prefetch, plus a custom pass that lowers `llvm.smax/smin/umax/umin` intrinsics into `icmp`+`select` to keep the ORC JIT happy) and pins the module's data layout to the host so struct field offsets match the C ABI used by the driver.
5. **Computed signals are filled in first.** Alongside the requirement functions the compiler emits a `__prepare__` function, which the driver calls once before any requirement runs. It walks the trace once *per computed signal*, in declaration order, writing that signal's value at every state. Per-signal rather than per-state ordering is what makes `data y = Xs(x);` work: `x` is materialised across the whole trace before anything reading `x` at a later state is evaluated. Specifications with no computed signals get an empty `__prepare__`.
6. **Verification is just iteration.** The runtime driver loads every requirement function by name, calls each one against the trace, and reports pass/fail. Two front ends use this loop today: the gtest harness (`test/logic.cpp`) builds a synthetic trace in C++, while the `referee execute` CLI ingests a CSV trace whose column layout is the one `csvHeaders` derives from the `data` declarations (and, optionally, a single-row `conf.csv` for `conf` declarations).

Because all requirements are evaluated over the same trace in the same module, any inconsistency between them becomes observable: a trace that satisfies one requirement may violate another, and the set of requirements is collectively checkable — not a collection of independent scripts.

### What a trace means between samples

A trace row is a **sample**: a timestamp plus a complete set of values for every declared `data` signal. Referee's model of what happens *between* two samples is **sample-and-hold** — each sample's values are taken to hold from its own timestamp up to, but not including, the next one. A signal is a piecewise-constant function of time, and a row is the point at which it may change.

Two consequences are worth being explicit about, because they pull in different directions.

**Only time-bounded operators observe the clock.** The unbounded operators (`Xs`, `Ys`, `G`, `F`, `Us`, `Ss`, …) step from sample to sample and never look at a timestamp. `Xs(a)` means "at the next recorded sample", whether that sample is a millisecond later or an hour later:

```text
__time__,a          __time__,a
0,false             0,false
1,true              1000000,true
```

`Xs(a)` holds at the first state of **both** of these traces. The sampling rate is therefore part of what an unbounded requirement means — `Xs` is "next record", not "next instant". Only the bounded forms (`G[0:2500]`, `Us[1000:3000]`, …) interpret the hold, by intersecting their window with each sample's `[t_i, t_{i+1})` interval:

```text
__time__,a
0,false
1000,true

F[0:500](a);      // FAIL — the window closes at t=500, and `a` is held false to t=1000
F[0:1500](a);     // PASS — the window reaches t=1000, where the sample says true
```

**Rows are not sparse, and empty cells are not carried forward.** Every declared column must carry a value in every row. An empty cell is *not* filled from the row above — it reads as the type's zero (`false`, `0`, `0.0`, the empty string), and it does so silently:

```text
__time__,a,i
0,true,42
1000,,            # NOT a=true,i=42 held over — this row reads a=false, i=0
2000,false,7
```

So sample-and-hold is what happens between rows, not within them. If you are generating a trace from a system that only reports signals when they change, you have to materialise the held values into complete rows before handing it to Referee; writing only the changed cell will silently zero everything else. Equivalently: **to say "this value changes at time T", add a complete row at T.**

Finally, the packer brackets the trace with sentinel states one time unit outside it (at `firstT - 1` and `lastT + 1`), which is what gives the last real sample a non-empty interval to occupy. A bounded operator evaluated at the final sample therefore sees a one-unit-wide window rather than an open-ended one.

### Temporal lowering

The binary temporal operators come in two families that are duals of each other, and the distinction drives both the generated code and its correctness:

| Family | Operators | Recurrence |
| ------ | --------- | ---------- |
| Disjunctive | `Us`, `Uw` (until), `Ss`, `Sw` (since) | `val[i] = rhs[i] \|\| (lhs[i] && val[i±1])` |
| Conjunctive | `Rs`, `Rw` (release), `Ts`, `Tw` (trigger) | `val[i] = rhs[i] && (lhs[i] \|\| val[i±1])` |

`U`/`R` recur forward (`val[i+1]`), `S`/`T` recur backward (`val[i-1]`). The strong/weak distinction is the base case: strong variants seed the recurrence with `false` at the end of the finite trace, weak variants with `true`. Several surface operators canonicalize into this family — `G(x)` becomes `Rw(false, x)`, for instance — so the conjunctive form is on the hot path even for specifications that never mention release or trigger by name.

For the **unbounded** operators the compiler emits that recurrence directly as a single linear pass over the trace into a `bool[numStates]` buffer, then each use of the operator becomes an array load. Evaluating a requirement containing `k` distinct unbounded temporal operators therefore costs `O(k·N)` rather than the `O(N²)` of a nested scan, and a sub-formula shared between operators is computed once.

**Bounded** operators (`G[0:2500](a)`, `Us[1000:3000](a, c)`, …) are also lowered linearly, by a different route. The recurrence above does not apply to them — `timeLo`/`timeHi` derive from the timestamp at the *evaluation point*, so `val[i]` and `val[i±1]` are quantified over different windows and the neighbour's result is not a reusable sub-result. What does carry over is that timestamps increase strictly, so the states the scan examines at evaluation point `i` form a contiguous index range `[S(i), E(i)]`, and its answer is the outcome at the first *decisive* index in that range — the first `j` with `rhs[j] == rhsV` or `lhs[j] == lhsV`. Decisiveness depends only on `j`, never on `i`, so one pass serves every evaluation point:

```text
decIdx[j] = nearest decisive index from j, in the scan direction   // one pass
val[i]    = decIdx[S(i)] within E(i) ? outcome(decIdx[S(i)]) : endV
```

Both window ends move monotonically with `i`, so one forward-only pointer each covers the whole trace, giving two linear passes in place of the nested scan. Expressing the ends as "last index whose timestamp is `<= X`" rather than searching outward from `i` keeps each pointer a plain walk:

```text
UR:  S(i) = max(q, i),  q = last index with t[q] <= t[i]+lo
     E(i) = e,          e = last index <= N-2 with t[e] < t[i]+hi
ST:  S(i) = min(p, i),  p = first index with t[p] >= t[i]-lo
     E(i) = f,          f = first index with t[f] >  t[i]-hi
```

The requirement is that the bounds be **loop-invariant** — literals, or expressions over `conf`. The grammar admits an arbitrary expression there, and a bound reading a `data` signal or a frozen state makes the window non-monotone in `i`, which breaks the pointer walk; those operators stay on the nested scan. `test/logic/bounded.ref` exercises the whole matrix over an irregularly spaced trace.

**Freeze (`@`) bodies** are excluded from the buffered path regardless. A buffer is indexed by state, which is only meaningful for an operator whose value is a function of the state index, and inside a freeze an operator that names the frozen state denotes different things at different evaluation points. An operator that merely *contains* a freeze is still eligible — it is evaluated once per state like any other — so `Us(t@(...), b)` is buffered while a temporal operator nested under that `t@` is not.

### Computational complexity

For a trace of **N** states and a requirement containing **k** distinct temporal
operators, the table below details the evaluation complexity. The design intent is that checking a long trace against a large
specification is the ordinary case, so every operator that can be made linear
in the trace length has been.

| Construct | Cost per requirement | Notes |
| --- | --- | --- |
| State formula (`&&`, `==`, arithmetic, member access) | O(N) | one pass; short-circuit operators branch rather than evaluate both sides |
| `G` `F` `H` `O` `Xs` `Xw` `Ys` `Yw` | O(N) each | canonicalize into the until/release recurrence |
| `Us` `Uw` `Rs` `Rw` `Ss` `Sw` `Ts` `Tw`, unbounded | O(N) each | single linear pass into a `bool[N]` buffer; a shared sub-formula is computed once, so a whole requirement is **O(k·N)** |
| the same, **bounded** `[lo:hi]` | O(N) each | monotone two-pointer walk, provided the bounds are loop-invariant (literals or `conf`); a bound reading a `data` signal falls back to the nested O(N²) scan |
| `Sum` `Cnt` `Itg`, unbounded | O(N) each | one backward fold, the same recurrence weighted by value / one / duration |
| the same, **windowed** `[lo:hi]` | O(N × w) | linear in the trace, `w` = states per window |
| freeze `t@(…)` with a temporal body | O(N²) for that subtree | the frozen state is a different binding at each evaluation point, so it cannot be buffered |
| quantifier over `T[N]` (sized) | compile-time | expands to conjunction / disjunction / indicator sum; no runtime cost |
| quantifier over `T[]` (unbounded) | O(length) per state | a runtime loop; under `G`, O(N × length) |
| array index / slice / `.count` | O(1) | a load and, for an index, a bounds check LLVM often proves away |
| external `func` call | O(1) + callee | a native call; pure, so it hoists out of a loop when its arguments are loop-invariant |

Two costs sit outside the per-requirement column:

- **Compilation** is a fixed cost paid once — roughly 700 ms for a
  233-requirement specification — independent of trace length. It dominates
  below a few thousand states, which is why several traces are compiled once
  and checked in turn (*Checking several traces* above).
- **Checking** is about 0.12 ms per trace row per the same specification, so a
  corpus is essentially `compile-once + Σ (rows × constant)`.

`--explain` is the one deliberately super-linear path: a temporal requirement's
per-state column re-walks its operator, O(N²), which is why it is opt-in and
single-trace. The verdict it instruments stays O(N).

# Installation

The project is built with [Meson](https://mesonbuild.com/) and [Ninja](https://ninja-build.org/).

## Linux
Install the following tools:
```bash
sudo apt-get install clang-format
sudo apt-get install g++
sudo apt-get install gcc
sudo apt-get install libantlr4-runtime-dev
sudo apt-get install libcli11-dev
sudo apt-get install libfmt-dev
sudo apt-get install libgtest-dev
sudo apt-get install libspdlog-dev
sudo apt-get install libyaml-cpp-dev
sudo apt-get install llvm
sudo apt-get install llvm-dev
sudo apt-get install meson
sudo apt-get install ninja-build
```

> **Note on ANTLR4 version.** Ubuntu's `antlr4` package (installed via `apt-get install antlr4`) ships the 4.9.2 generator, which is too old for this project's grammar. The C++ runtime (`libantlr4-runtime-dev`) on Ubuntu Noble is 4.10, so download the matching generator jar and pass it to Meson:
> ```bash
> curl -L -o ~/antlr-4.10.1-complete.jar https://www.antlr.org/download/antlr-4.10.1-complete.jar
> meson setup build -Dantlr4_jar=~/antlr-4.10.1-complete.jar
> ```

## MacOS
```bash
brew install antlr
brew install antlr4-cpp-runtime
brew install clang-format
brew install cli11
brew install fmt
brew install googletest
brew install llvm
brew install meson
brew install ninja
brew install spdlog
```

# Checkout
```bash
git clone git@github.com:michaelrolnik/referee.git
cd referee
git submodule update --init --recursive
```

# Building

## Regular
From the project root, configure a build directory and compile:
```bash
meson setup build
ninja -C build
```

Executables land in `build/`:
- `build/referee` — the main CLI. Two subcommands:
  - `build/referee compile file.ref` — emits LLVM IR for the given `.ref` file to stdout.
  - `build/referee execute file.ref trace.{csv,yaml,rdb} [--conf conf.{csv,yaml}]` — JIT-compiles the requirements and evaluates them against the trace. Tabular `.csv` / `.yaml` inputs are parsed and re-encoded into the JIT's `state_t[]` buffer on the fly; `.rdb` inputs are *already* in that exact layout, so loading is just pointer fix-up (see *Referee Database* below). Prints one `PASS`/`FAIL` line per requirement and exits non-zero if any requirement fails. Working examples: `test/logic/pass.ref` + `test/logic/data.csv` + `test/logic/conf.csv`.
- `build/rdb` — the RDB CLI: pack CSV/YAML traces into `.rdb` and pretty-print existing `.rdb` files.
- `build/tests` — the GoogleTest suite.

### Overriding dependency locations

- `-Dantlr4_jar=/path/to/antlr-<version>-complete.jar` — pass this to `meson setup` if neither an `antlr` launcher nor a Homebrew-style jar path is auto-detected.
- On macOS, Homebrew's LLVM is keg-only; the build falls back to invoking `/opt/homebrew/opt/llvm/bin/llvm-config` automatically.

### Run Tests

Either go through Meson:
```bash
meson test -C build --print-errorlogs
```

Or run the gtest binary directly — it resolves test-data paths via a compile-time absolute path, so it works from any working directory:
```bash
./build/tests
```

The suite covers the compiler in-process and also drives the two CLI binaries as subprocesses, since argument parsing, subcommand dispatch and exit codes live in `main()` and no in-process test reaches them.

### Coverage

To configure coverage builds and render HTML/XML reports, see the dedicated [Code Coverage](#code-coverage) section below.

# Editor support

`editors/vscode/` is a syntax-highlighting extension for `.ref` files. It works in VS Code and its forks — Cursor, Antigravity, VSCodium — which all consume the same extension format.

It highlights the temporal operators (future and past scoped separately, and only where one is actually applied, so a stray capital is left alone), the whole Dwyer specification-pattern vocabulary, declarations with their declared names, freeze variables and `__time__`, and every literal form. The keyword lists are generated from `core/referee.g4` rather than written by hand, so they track the grammar exactly. There are also snippets for the common specification patterns.

Highlighting is all it does — see *What is missing* above.

## Installing it

The extension is not published to a marketplace; install it from this checkout. Copy it into your editor's extension directory and reload the window:

```bash
cd editors/vscode
DEST=~/.vscode/extensions/michaelrolnik.referee-ref-0.1.0      # adjust per the table below
mkdir -p "$DEST"
cp -r package.json language-configuration.json syntaxes snippets README.md "$DEST/"
```

Then run **Developer: Reload Window** from the command palette, open a `.ref` file, and check that the status bar reads **REF**.

**Which directory** depends on the editor, and on whether you are working locally or over SSH. For a remote window the extension has to live on the **remote** machine, not on your laptop:

| Editor | Local | Remote (SSH) |
| --- | --- | --- |
| VS Code | `~/.vscode/extensions/` | `~/.vscode-server/extensions/` |
| Cursor | `~/.cursor/extensions/` | `~/.cursor-server/extensions/` |
| Antigravity | `~/.antigravity/extensions/` | `~/.antigravity-ide-server/extensions/` |

If you are unsure, pick whichever already exists and holds your installed extensions.

Alternatively, package a `.vsix` and install that — the more reliable route on a remote window, since it registers the extension with the server rather than relying on a directory scan:

```bash
cd editors/vscode
npx @vscode/vsce package --allow-missing-repository
```

then Extensions view → `…` → **Install from VSIX…**.

## Testing the grammar

```bash
cd editors/vscode/test
npm install
node tokenize.cjs
```

This tokenizes a sample against the same TextMate engine the editors use and asserts the resulting scopes. It is worth running after any grammar edit, because Oniguruma is stricter than JavaScript's regex engine and an invalid pattern makes the editor drop the grammar **silently** — the file simply renders unhighlighted, with no error reported anywhere.

# Running referee

`build/referee` is the main CLI and is driven through two subcommands, `compile` and `execute`. Pass `--help` or `<subcommand> --help` for the full option list:

```bash
./build/referee --help
./build/referee compile --help
./build/referee execute --help
```

## `referee compile` — emit LLVM IR

Lower a `.ref` file to LLVM IR and write it to stdout. Useful for inspecting what the compiler generates, piping into `opt` / `llc`, or saving the IR for offline analysis.

```bash
./build/referee compile path/to/spec.ref            # IR to stdout
./build/referee compile path/to/spec.ref > spec.ll  # IR to a file
```

The emitted IR contains one function per requirement statement, named after the source position (`<startRow>:<startCol> .. <endRow>:<endCol>`), and one `extern "C" debug(i64)` declaration the runtime uses to print debug values.

## `referee execute` — JIT-compile and check a CSV trace

Compile every requirement, JIT it, and evaluate it against a CSV trace.

```bash
./build/referee execute spec.ref data.csv [--conf conf.csv]
```

- **`spec.ref`** — the requirement source file.
- **`data.csv`** — the trace. One row per state. The column layout must match what `csvHeaders` derives from the `data` declarations in `spec.ref`:
  - the first column is `__time__` (per-row timestamp, integer-valued; the unit is whatever the spec uses);
  - one column per leaf field of every `data` declaration — for `data pos : struct { x: number; y: number; };` you get `pos.x` and `pos.y`; for `data limits : integer[3];` you get `limits[0]`, `limits[1]`, `limits[2]`; nesting expands the obvious way (`grid[1][2].x`);
  - enums are written as the bare member name (`ON`, `OFF`), booleans as `true`/`false` (or `1`/`0`), strings unquoted.
- **`--conf conf.csv`** *(optional)* — a single-row CSV carrying values for every `conf` declaration. Same column-naming rules as `data.csv`. If the spec has no `conf` declarations, omit it; if it has them and you omit the file, the configuration is zero-initialised, which is rarely what you want.

### Output

One line per requirement, sorted by source position:

```text
<startRow>:<startCol> .. <endRow>:<endCol>      PASS
<startRow>:<startCol> .. <endRow>:<endCol>      FAIL
…
```

The process exits with status `0` if every requirement passed and `1` if any failed (or if a requirement function could not be resolved in the JIT, in which case the line is tagged `ERROR`). This makes `referee` directly usable in CI:

```bash
./build/referee execute spec.ref data.csv --conf conf.csv || echo "spec violated"
```

### Worked example

The repo ships a complete pair of fixtures under `test/logic/`. They are the same inputs the gtest `LogicTest.Pass` / `LogicTest.Fail` cases use, so they are guaranteed to stay in sync with the language and runtime:

```bash
./build/referee execute test/logic/pass.ref test/logic/data.csv --conf test/logic/conf.csv
./build/referee execute test/logic/fail.ref test/logic/data.csv --conf test/logic/conf.csv
```

The first command exits `0` (every requirement holds against the trace); the second exits `1` because a few requirements in `fail.ref` are deliberately written to contradict the recorded data. Expected output for `fail.ref` against this trace:

```text
39:0 .. 39:15                            FAIL
41:0 .. 41:88                            FAIL
47:0 .. 47:78                            FAIL
48:0 .. 48:78                            FAIL
```

### Building your own trace

The general recipe is:

1. Write your `.ref` file with the `data` and `conf` declarations matching the signals in your log.
2. Inspect the column layout the compiler will expect — for non-trivial structs/arrays the easiest path is to start from a CSV with just the `__time__` column plus one row of zeros, run `referee execute`, and let any column-not-found error tell you the next expected name. The same expansion is performed by `core/visitors/csvHeaders.cpp` if you'd rather read the rules in code.
3. Produce your CSV with that header, one row per timestamped state, plus a one-row `conf.csv` if the spec uses `conf` declarations.
4. Run `./build/referee execute spec.ref data.csv --conf conf.csv`.

> **Every row must be complete.** Values are held between samples but *not* within a row: an empty cell reads as the type's zero rather than carrying forward from the row above, and does so silently. If your source only emits a signal when it changes, expand it into full rows before handing it to Referee. See *What a trace means between samples* above — it also covers why the spacing of your samples changes what unbounded operators like `Xs` mean.

> **Limitation.** The driver currently reads the entire trace into memory and runs each requirement function across the whole trace. Streaming / online evaluation (consuming records as they arrive, reporting per-violation timestamps) is on the roadmap — see the *What is missing* section above.

## Checking several traces

`referee execute` takes any number of traces and compiles the specification once for all of them:

```bash
referee execute spec.ref run-*.csv --conf conf.csv
```

This matters more than it sounds. Compilation is a fixed cost — roughly 700 ms for a 233-requirement specification — while checking is about 0.12 ms per trace row. Twenty small traces one invocation at a time take ~13.7 s; the same twenty in one invocation take ~1.1 s, and the gap widens with the corpus.

### Naming a requirement

A requirement may be given a stable name, written `@name` before it:

```text
@door_closes_in_2s
globally, if door.OPENED, then in response door.CLOSED within 2 seconds;

@"late-alarm-check"                 # quoted, so the name may contain hyphens
G(alarm => F(ack));
```

The name replaces the source position as the requirement's label — in the report, and in the generated function. That is the point: a corpus of traces can then say which requirement each one is meant to violate, and go on being right when the specification is edited and every line moves. Names must be unique across the whole program, imports included.

Unnamed requirements keep their `[file:]row:col .. row:col` label, so nothing existing changes.

### Declaring what a trace should do

A specification that passes everything it is shown may be correct, or may be vacuous — a requirement mistyped into triviality passes exactly as convincingly as one that holds. The defence is a corpus of traces that *must* be rejected:

```bash
referee execute spec.ref \
    --success good/nominal.csv good/restart.rdb \
    --failure bad/stuck-valve.csv bad/late-alarm.yml
```

Traces given bare, or under `--success`, must satisfy every requirement. Traces under `--failure` must violate at least one. The run exits 0 only if every trace behaved as declared:

| declared | observed | verdict |
| --- | --- | --- |
| `--success` | all requirements hold | ok |
| `--success` | something violated | failure |
| `--failure` | something violated | ok |
| `--failure` | all requirements hold | **unexpected pass** |

The last row is the one that earns the feature. A trace that was supposed to be rejected and no longer is means the specification has stopped catching what that trace demonstrates — usually because a requirement was weakened or a signal renamed. It is reported distinctly from an ordinary failure, because it means something different: not "the system misbehaved" but "the specification no longer notices".

### A corpus in a file

A command line outgrows a real corpus quickly, so the same thing can be committed as a manifest:

```text
# suite.txt — what each trace is meant to demonstrate
good/nominal.csv      passes
bad/stuck-valve.csv   fails   door-closes-in-2s
bad/late-alarm.yml    fails   late-alarm-check, alarm-within-5s
```

```bash
referee execute spec.ref --suite suite.txt
```

Paths are relative to the manifest, so a suite moves as a unit.

Naming the requirements after `fails` is what makes the corpus honest. A bare `fails` is satisfied by the trace violating **anything** — including something nobody intended, like a mistyped column or an unrelated requirement added later. The check stays green while what it was protecting has quietly stopped being tested. Naming them catches that:

```text
bad_a.csv  expected failure  FAIL  WRONG REQUIREMENT
    expected to violate b-always-holds, but it held
    a_always_holds                           FAIL
```

The trace did fail — just not for the reason it exists to demonstrate.

### Output detail

`-v 0` prints a closing tally, `-v 1` adds a line per trace, `-v 2` adds the requirement table for every trace. Regardless of the level, a trace that did not behave as declared always shows its violated requirements, since that is what a reader needs to act on. A single trace with no expectations defaults to the full table, which is what it has always printed.

```text
good/nominal.csv      expected success  PASS  ok
bad/stuck-valve.csv   expected failure  FAIL  ok
bad/late-alarm.yml    expected failure  PASS  UNEXPECTED PASS

3 traces: 2 ok, 1 unexpected pass
```

## Run traces — `--explain`

A verdict says *which* requirement failed; a run trace says *why*, and — more usefully — makes **vacuity** visible. `--explain` writes a newline-delimited JSON record of what was evaluated where:

```bash
referee execute spec.ref trace.csv --explain run.ndjson
python3 tools/view.py       run.ndjson -o run.html          # static timing diagram
python3 tools/view_bokeh.py run.ndjson -o run.bokeh.html     # hover and zoom, offline
```

Per requirement, the file records:

- **its per-state column** — the value at every state, not only the first-state verdict, marked `state` (a fact about the instant) or `temporal` (a claim about the suffix), so a viewer does not draw one as the other;
- **subexpression rows** — for `G(a && b)`, the columns of `a` and `b` beneath it, so a compound requirement shows which side gave way and when;
- **scope intervals** — where a Dwyer pattern's scope was actually open (`before P` open until the first `P`, `while P` over each run where it holds, and so on);
- **vacuity** — whether the requirement passed *without proving anything*: an implication whose antecedent never fired (`antecedent_never_true`), or a scope that never opened (`scope_never_opened`). A requirement can be `pass` and `vacuous` at once, and that combination is the point — it is the coverage gap a green report otherwise hides, and it is computed by referee so it shows up in CI where a picture cannot.

Every column comes from a companion function the compiler emits from the *same* AST node the verdict comes from, so the picture cannot drift from the verdict — referee checks that the column's first-state value equals the verdict on every run. The format is the contract (`schema/run-trace.schema.json`, `docs/run-trace-format.md`); the viewers are replaceable. `examples/door/` ships a worked run trace with its rendered HTML.

> **Cost.** A temporal requirement's column is O(N²) — each state re-walks its operator — so `--explain` is opt-in and single-trace. It is affordable for one trace; the verdict path it instruments stays O(N).

# Referee Database (RDB)

`.rdb` is a packed binary format whose state-buffer section is *exactly* the `state_t[]` layout the JIT-compiled requirement functions consume:

```c
struct state_t {
    int64_t  time;
    void*    prop[numProps];   // one pointer per `data` declaration
};
```

`referee execute spec.ref trace.rdb` does no per-row parsing. It `read()`s the file once, walks the embedded schema, and rewrites two kinds of `int64` disk offsets into host pointers in place:

- each row's `prop[pi]` slot — an offset into the prop-blobs section (`-1` for null) — becomes a `void*` into the in-memory copy of the file;
- each `TypeString` slot inside any blob — an offset into the string pool — becomes a `char const*` interned through `Strings::instance()`.

After fix-up, `&states[0]` *is* the `state_t*` the JIT walks; `confPtr()` *is* the configuration pointer it reads. Nothing else is touched.

## On-disk layout

The file is a fixed-size header followed by five sections. Every section is described by a `Section { uint64 fileOffs; uint64 fileSize; uint64 itemNmbr }` record in the header, so a future writer can re-order or extend the layout without breaking readers.

| Section       | Holds                                                                                                                                        | `itemNmbr`                  |
| ------------- | -------------------------------------------------------------------------------------------------------------------------------------------- | --------------------------- |
| `header`      | magic `"REF-RDB1"`, version, flags, the five `Section` records, and `rowBytes` (stride of `states`, equal to `8 + 8·numProps`)               | n/a                         |
| `schema`      | tagged-binary tree of every `data` and `conf` AST type — primitives, enums (with item names), structs (with member names), fixed-size arrays | number of `data` decls       |
| `conf`        | the concatenated, member-aligned conf blob — byte-identical to what `Loader::load` produces from `conf.csv` / `conf.yaml`                    | number of `conf` decls       |
| `states`      | `itemNmbr × rowBytes` bytes; each row is `{ i64 time; i64 propOffs[numProps] }` on disk and `{ i64 time; void* prop[numProps] }` after load | number of state rows        |
| `prop-blobs`  | the heterogeneous body pool the row pointers point into; each blob is per-prop-type aligned and byte-identical to `Loader::load` output     | 0 (variable shape)          |
| `string-pool` | `\0`-terminated unique strings; offset 0 is reserved for the empty string                                                                    | 0 (variable shape)          |

The first and last `states` rows are sentinels (zero blobs, time outside the data window), so a `.rdb` produced from N CSV rows has `numStates = N + 2` — identical to the in-memory layout `referee execute` builds for CSV/YAML traces.

> **Why split `states` and `prop-blobs`?** The JIT iterates the trace by adding `rowBytes` to a `state_t*`, which only works if rows have a *uniform stride*. Prop blobs are heterogeneous (a string is 8 bytes; a struct of strings can be 80) and per-type aligned, so they live in their own section while `states` carries only the time + per-prop pointer table.

> **Cross-process strings.** Host pointers into `Strings::instance()` aren't stable across processes, so writers store every `TypeString` slot as a pool offset and the reader walks the schema to re-intern them. Producer and consumer must therefore agree on the schema — the embedded one is checked structurally against the `.ref` at load time and a mismatch is a hard error.

> **Large traces.** `referee::db::Reader` currently slurps the whole file into one `std::vector<uint8_t>`. For traces too large to fit in process memory, `mmap()` the file `MAP_PRIVATE` (or back it with shared memory if multiple consumers share the dataset) — the in-place pointer fix-up walk is identical either way; only the storage backing changes.

## Producing `.rdb` files — `rdb build`

```bash
./build/rdb build spec.ref data.csv  --conf conf.csv  -o trace.rdb
./build/rdb build spec.ref data.yaml --conf conf.yaml -o trace.rdb
```

| Argument | Required | Description |
|----------|----------|-------------|
| `spec.ref` | yes | REF source file whose `data`/`conf` declarations define the binary schema. Computed signals (`data x = expr;`) are excluded — they are recomputed at execute time, not stored. |
| `data.{csv,yml,yaml}` | yes | Trace file. One row per timestamped state. Column names must match the layout `csvHeaders` derives from the `data` declarations (same rules as `referee execute`). |
| `--conf conf.{csv,yml,yaml}` | no | Single-row configuration file for `conf` declarations. Omit if the spec has no `conf` declarations; the blob is zero-initialised when absent. |
| `-o / --out trace.rdb` | yes | Output path for the packed `.rdb` file. |

The CSV / YAML column schema is the same one `referee execute` accepts (see *Building your own trace* above). Both pipelines share the same `loader::Row` ingestor and `Loader::load` byte-layout, so a `.rdb` packed from CSV is **byte-identical** to one packed from equivalent YAML — the test suite asserts this in `Rdb.CsvAndYamlAgree`.

## Merging multi-rate sources — `rdb merge`

Signals for one specification often come from different sources at different
sample rates — one file logs a fast sensor, another a slow status word, each on
its own clock. `rdb merge` folds them into a single trace of complete rows:

```bash
./build/rdb merge spec.ref fast.csv slow.csv status.yaml -o merged.rdb
```

The operation is forced by REF's model. Values are held *between* rows, but an
empty cell reads as the type's zero *within* one (see *What a trace means
between samples*), so a merge cannot leave gaps for the reader to fill — it has
to materialise the hold itself. It takes the **union of every source's
timestamps**, and at each one every signal takes the **most recent value its own
source reported at or before that time**:

```text
fast.csv          slow.csv           merged (held forward)
__time__,fast     __time__,slow      __time__,fast,slow
0,10              50,5               50,10,5
100,11            250,7              100,11,5      ← slow still 5, held from t=50
200,12                               200,12,5
                                     250,12,7      ← slow now 7; fast still 12
```

| Argument | Description |
| --- | --- |
| `spec.ref` | the specification, for the schema and column order of the `.rdb` |
| `sources…` | two or more `.csv` / `.yaml` files, each carrying `__time__` and some of the signals |
| `-o merged.rdb` | the packed output |
| `--conf conf.csv` | optional configuration, as for `rdb build` |
| `--leading trim\|zero\|backfill` | what to do before a signal's first sample (default `trim`) |
| `--overlap error\|merge` | a column present in two sources (default `error`) |

**`--leading`** decides the leading gap, before a signal has reported at all:
`trim` drops rows until every signal has a value (invents nothing, loses the
earliest fast samples); `zero` keeps every row and reads the type's zero there;
`backfill` uses the signal's earliest real value. **`--overlap`** decides what
happens when the same column appears in more than one source: `error` treats it
as a mistake and says so; `merge` unions the two sources' samples of that one
signal, last-write-wins on an exact-timestamp tie.

The merge is a plain column-and-timestamp operation — no `.ref` needed for the
fold itself; the specification is used only to pack the result, so the same
schema check `referee execute` runs applies to the merged `.rdb`.

## Consuming `.rdb` files — `referee execute`

```bash
./build/referee execute spec.ref trace.rdb
```

`--conf` is *not* used with `.rdb` inputs — the configuration is already inside the file. Output, exit code, and per-requirement formatting are identical to the CSV path; before invoking the JIT, the executor cross-checks the file's embedded schema against the `.ref`'s AST and refuses to run on a mismatch. That check covers the trace-backed signals only — computed signals are not part of the file's schema, so changing a `data x = ...;` expression does not invalidate an existing `.rdb`.

## Inspecting `.rdb` files — `rdb dump`

```bash
./build/rdb dump trace.rdb
```

Pretty-prints a YAML document in this order:

- `rdb:` — top-level block with `path`, `numStates`, `numProps`, and `rowBytes`.
- `schema:` — nested `data:` and `conf:` lists; each entry has a `name` and a `type` rendered in YAML.
- `conf:` — one `name: value` entry per `conf` declaration, decoded from the binary blob.
- `states:` — one block per state row with `index`, `time`, and a `props:` map of decoded values (`null` for an unset slot).

Useful for sanity-checking an `rdb build` result without re-running the JIT, and for diffing two `.rdb` files at a logical level rather than byte level.

# Code Coverage

Coverage is driven by `gcovr`. Install it first:

```bash
# Linux
sudo apt-get install gcovr

# MacOS
brew install gcovr
```

Configure a dedicated build directory with `b_coverage=true` and a debug build type, build, and run the tests so instrumentation is produced:

```bash
meson setup build-cov -Db_coverage=true --buildtype=debug
ninja -C build-cov
meson test -C build-cov --print-errorlogs
```

Then render the coverage reports. The project wires three convenience targets that invoke `gcovr` directly (bypassing Meson's built-in lcov pipeline, which is unreliable on macOS with Apple Clang) and filter out ANTLR-generated files:

```bash
ninja -C build-cov coverage-gcovr-html   # HTML:   build-cov/meson-logs/coveragereport/index.html
ninja -C build-cov coverage-gcovr-text   # Text:   build-cov/meson-logs/coverage.txt
ninja -C build-cov coverage-gcovr-xml    # Cobertura XML: build-cov/meson-logs/coverage.xml
```

Open the HTML report with `open build-cov/meson-logs/coveragereport/index.html` (macOS) or `xdg-open ...` (Linux).

Reset counters between runs by deleting `build-cov/**/*.gcda`, or wipe `build-cov/` entirely and re-run `meson setup` for a clean slate.

> Meson also ships stock `ninja coverage`, `coverage-html`, `coverage-text`, and `coverage-xml` targets. They work on Linux, but on macOS `coverage-html` goes through `lcov`, which fails on Apple Clang's `.gcno` format with `inconsistent` function-end-line errors. Use the `coverage-gcovr-*` targets above for portable behaviour.
