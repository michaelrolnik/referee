# Referee

Referee is a C++ compiler toolchain for the REF language, created to make formal requirement verification practical for real systems. It parses REF source files with ANTLR4, builds an abstract syntax tree, and lowers programs to LLVM IR with optimization passes.

The language and tooling are inspired by temporal-logic-based verification workflows (for example LTL/TPTL-style reasoning and requirement patterns), with the goal of expressing behavior constraints clearly, unambiguously, and in a way that is suitable for automated checking over traces and logs.

In short: Referee connects human-readable requirement intent to executable verification infrastructure through a dedicated domain language, compiler pipeline, and runtime-oriented architecture.

## Status

**What is implemented**

- A REF language front end: lexer, parser, and grammar (`core/referee.g4`) covering typed declarations (`type`, `data`, `conf`), structs, enums, arrays, and Dwyer-style property specification patterns (e.g. `globally, ...`, `before ..., ... eventually holds after N milliseconds`, `between ... and ..., it is always the case that ...`).
- A typed AST and a set of semantic visitors (`canonic`, `negated`, `rewrite`, `typecalc`, `printer`, `csvHeaders`).
- Lowering of temporal formulas (LTL/TPTL/MTL-flavoured, including strong/weak next `Xs`/`Xw`, bounded until/release, freeze variables, past operators) to **LLVM IR**, followed by standard LLVM optimization passes.
- A JIT-based test harness (`test/logic.cpp`) that compiles REF files, JITs them against a synthetic trace (`state_t[]` + `conf_t`), and asserts that each requirement evaluates to `true` (pass) or `false` (fail) over that trace. See `test/logic/pass.ref` and `test/logic/fail.ref` for the intended execution model.
- A CLI (`referee compile`) that emits LLVM IR for a given `.ref` file.

**What is missing**

- **Log ingestion.** The Python version read system logs directly and fed them to the checks. Here, the compiled IR already expects a trace in a well-defined shape (timestamped state records plus a configuration blob), but the actual log/CSV reader that builds that trace from real system output is not implemented yet. The skeleton is visible in `core/visitors/csvHeaders.*` (deriving the expected column layout from declared `data` fields) and in the commented-out `rdb::Writer` section of `test/logic.cpp`.
- **Runtime driver.** A standalone binary that streams records into the JIT-compiled requirement functions and reports pass/fail/violation locations — the current runtime lives only inside the gtest harness.
- **VSCode / LSP plugin** for editing REF files with diagnostics, as envisioned in the original design.
- **Product-specific model exporters/importers** to adapt arbitrary system logs into the canonical trace format.

## Why this is better than the original Python approach

1. **Declarative, not imperative.** Requirements are written once, in a language designed for them (temporal-logic patterns), instead of being re-expressed as hand-maintained Python state machines. The slide deck's elevator example ("between called and opened, transition to `atfloor` occurs at most 2 times") is a single line of REF; the Python equivalent is a class with flags, counters, and event handlers that must be kept in sync with the requirement as it evolves.
2. **Native performance.** Requirements compile to optimized LLVM IR and run as native code via the ORC JIT. Checking a long trace against hundreds of properties does not pay the interpreter tax of a Python runtime, which matters when validating large systems over long log windows.
3. **All requirements evaluated together — contradictions surface automatically.** Because every property is compiled into the same module and evaluated over the same trace, mutually inconsistent requirements are detected as soon as a trace exists that no property set can satisfy. In the Python approach each check was an isolated script, so contradictions between requirements typically went unnoticed until much later.
4. **Strong types, not just booleans.** REF supports integers, numbers, strings, enums, structs, and multi-dimensional arrays, so requirements can talk about real signal values (`lock.ON`, `abc.x[2][3].a`) rather than pre-extracted booleans. This removes an entire class of bugs in the "boolean extraction" layer that the Python version had to maintain by hand.
5. **Patterns instead of formulas.** Engineers write Dwyer-style English-like patterns; the compiler translates them into the underlying temporal-logic formulas (with correct handling of finite-trace semantics, strong/weak next, bounded operators). The original Python code effectively re-implemented each pattern ad hoc, with all the subtle off-by-one and finite-trace bugs that implies.
6. **Separation of concerns.** Trace generation (recording what the system did) and trace verification (checking that it satisfies the requirements) are fully decoupled. The same compiled requirements can be applied online, offline, to real logs, or to simulated traces, without changing the requirement source.
7. **Lower barrier for the test team.** Writing and reviewing a REF file requires understanding of the system, not programming fluency — which was one of the explicit goals and outcomes reported in the original deployment.

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
- **String literals:** `"..."` containing ASCII letters, digits, `_`, `.`, `?`, `!`. Strings are first-class values of type `string` and participate in `==` / `!=` comparisons.
- **Signed literals:** a leading `+` / `-` in front of a numeric literal is part of the literal, not a separate unary operator, so `-3` is an integer constant while `- x` is unary negation on `x`.
- **Reserved keywords.** In addition to the temporal-logic operator names (`G`, `F`, `Xs`, `Xw`, `Us`, `Uw`, `Rs`, `Rw`, `H`, `O`, `Ys`, `Yw`, `Ss`, `Sw`, `Ts`, `Tw`, `I`), the spec-pattern vocabulary is reserved: `after`, `afterwards`, `always`, `and`, `at`, `becomes`, `been`, `before`, `between`, `by`, `case`, `continually`, `eventually`, `every`, `followed`, `for`, `globally`, `has`, `have`, `holding`, `holds`, `if`, `in`, `interruption`, `is`, `it`, `least`, `less`, `long`, `must`, `never`, `occurred`, `once`, `remains`, `repeatedly`, `response`, `run`, `satisfied`, `so`, `than`, `that`, `the`, `then`, `until`, `while`, `within`, `without`, plus the time units `nanoseconds`, `microseconds`, `milliseconds`, `seconds`, `minutes`.

### Declarations and Types

Three declaration kinds introduce names into the program:

- `type Name : T;` — a named type alias. It defines a new, reusable type but does not reserve any runtime storage.
- `data Name : T;` — a **time-varying signal** sampled once per trace record. It is effectively a field of the per-timestamp `state_t` struct.
- `conf Name : T;` — a **configuration value** that is constant for the whole trace. It is effectively a field of the `conf_t` struct shared by all records.

Splitting `data` from `conf` is a deliberate modeling choice: signals that change per event (sensor readings, state machine outputs) live in `data`, while things that are set at the start of a run and never change (thresholds, limits, operating mode) live in `conf`. The compiler uses that distinction to generate a correct trace/config memory layout (see `core/visitors/csvHeaders.cpp`, which derives CSV column names from `data` declarations).

The type grammar supports:

- **Primitive types:** `boolean`, `integer` (64-bit signed), `number` (floating point), `string`.
- **Enumerations:** `enum { A, B, ... }` — a finite set of named values referenced as `T.A`, `T.B`. Enums are nominal: two enum types with the same members are distinct.
- **Structures:** `struct { field1: T1; field2: T2; ... }` — record types with named, typed fields. Nesting is allowed (`struct { inner: struct { ... }; }`).
- **Arrays:** `T[N]`, which may be stacked to form multi-dimensional arrays `T[N][M]`. Array sizes are statically known.
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

Enum members are referenced via their type (`State.ON`, `Door.CLOSED`). Comparing a `data` signal to an enum constant (`lock == State.ON`) is the primary way to turn raw signals into booleans inside requirements; a data declaration whose type is already `enum` also supports the short form `lock.ON`, which is syntactic sugar for "the current value of `lock` is `ON`".

### Expressions

REF expressions combine the familiar C-family operator set with dedicated temporal operators. Outside of the temporal layer, the non-temporal expression language is what requirement authors use most of the time.

**Operators (from tightest to loosest binding, roughly C-like):**

| Category    | Operators                                  | Notes                                            |
| ----------- | ------------------------------------------ | ------------------------------------------------ |
| Postfix     | `.field`, `[index]`                        | Member access and array indexing.                |
| Unary       | `!`, `-`                                   | Logical negation and arithmetic negation.        |
| Multiplicative | `*`, `/`, `%`                           | `%` is integer modulo.                           |
| Additive    | `+`, `-`                                   |                                                  |
| Relational  | `<`, `<=`, `>`, `>=`                       |                                                  |
| Equality    | `==`, `!=`                                 | Works on booleans, numbers, strings, enums.      |
| Logical AND | `&&`                                       |                                                  |
| Logical OR  | `\|\|`, `^`                                | `^` is logical XOR.                              |
| Implication | `=>`, `<=>`                                | Material implication and biconditional.          |
| Ternary     | `cond ? then : else`                       | Selects between two values of the same type.     |
| Grouping    | `(...)`                                    |                                                  |

`=>` and `<=>` are first-class operators, not macros: `p => q` is exactly `!p || q`, and `p <=> q` is `p == q` restricted to booleans, but writing them with the logical spelling makes requirement intent immediately readable ("whenever `p`, then `q`").

**Types in expressions.** Arithmetic mixes `integer` and `number`, with the usual widening to `number`. Comparisons are homogeneous — you can compare two integers, two numbers, two strings, two booleans, or two values of the same enum type, but not, say, an enum against a string. The ternary operator requires both arms to have a common type. These rules are enforced by the `typecalc` visitor before LLVM IR is generated, so type errors surface at compile time.

**Literals in expressions.** Besides the usual numeric literals described above, enum constants are written as `TypeName.MemberName`, which is the only form the parser accepts for enums; there is no implicit coercion from strings or integers.

Temporal operators come in both **future** and **past** flavours, and most come in **strong** and **weak** variants to give well-defined semantics on finite traces:

| Future | Past | Meaning |
| --- | --- | --- |
| `G(p)` | `H(p)` | `p` holds at every state (globally / historically) |
| `F(p)` | `O(p)` | `p` holds at some state (eventually / once) |
| `Xs(p)` / `Xw(p)` | `Ys(p)` / `Yw(p)` | strong/weak next / yesterday |
| `Us(p, q)` / `Uw(p, q)` | `Ss(p, q)` / `Sw(p, q)` | strong/weak until / since |
| `Rs(p, q)` / `Rw(p, q)` | `Ts(p, q)` / `Tw(p, q)` | strong/weak release / triggered |
| `I(p)` / `I(p, n)` | — | integral (sum of time `p` held, optionally per-unit `n`) |

All temporal operators optionally accept a **time bound** `[lo:hi]`, `[:hi]`, or `[lo:]`, giving MTL-style bounded versions such as `G[100:1000](a)` or `Us[1:3](alpha, beta)`.

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

Example — the elevator system from the slide deck:

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
globally, once lock.ON becomes satisfied it remains so for less than 2.2 seconds;
between door.CLOSED and lock.OFF, it is always the case that door.CLOSED;
after lock.ON, if door.OPENED, then it must have been the case that lock.OFF has occurred before it;
after lock.ON, if lock.OFF, then it must have been the case that button.DEPRESSED has occurred before;
```

Each of these lines compiles to a boolean-valued function over the trace; the runtime asserts that every such function returns `true` for every valid trace of the system.

### Evaluation Model

A REF program does not describe a computation that produces outputs; it describes **predicates over a trace**. The compiler and runtime together work as follows.

1. **Declarations shape the trace.** The set of `data` declarations determines the schema of each trace record (a `state_t` in generated code), and the set of `conf` declarations determines the static configuration (`conf_t`). `csvHeaders` walks these declarations and produces the matching flat column layout used for CSV ingestion — e.g. `data pos : struct { x: number; y: number; };` becomes two columns, `pos.x` and `pos.y`. Array fields expand to one column per element (`limits[0]`, `limits[1]`, …).
2. **A trace is an ordered list of states plus a configuration.** Each state carries a timestamp (implicitly exposed to REF as the `__time__` pseudo-identifier inside freeze scopes) and values for every declared `data` field. The configuration carries values for every declared `conf` field. The trace is assumed to be **finite**; strong/weak variants of the temporal operators exist specifically so requirements behave correctly at the end of that finite trace.
3. **Every requirement statement becomes one function.** For a statement `R` at file position `P`, the compiler emits an LLVM function named after `P` with the signature `bool eval(const state_t* states, size_t n, const conf_t* conf)` (more precisely, a pointer to the first state and the current-index bookkeeping that the temporal operators need). That function returns `true` iff the trace satisfies `R`.
4. **The module is optimized, then JIT-compiled.** `Referee::compile` runs a fixed pipeline of LLVM passes (instruction combining, reassociation, GVN, CFG simplification, loop strength reduction, loop data prefetch, plus a custom pass that lowers `llvm.smax/smin/umax/umin` intrinsics into `icmp`+`select` to keep the ORC JIT happy) and pins the module's data layout to the host so struct field offsets match the C ABI used by the driver.
5. **Verification is just iteration.** The current test harness loads all requirement functions by name, calls each one against a synthetic trace built in C++, and reports pass/fail. A future runtime driver is expected to do the same against real logs ingested through `csvHeaders`-derived schemas.

Because all requirements are evaluated over the same trace in the same module, any inconsistency between them becomes observable: a trace that satisfies one requirement may violate another, and the set of requirements is collectively checkable — not a collection of independent scripts.

# Installation

The project is built with [Meson](https://mesonbuild.com/) and [Ninja](https://ninja-build.org/).

## Linux
Install the following tools:
```bash
sudo apt-get install antlr4
sudo apt-get install clang-format
sudo apt-get install g++
sudo apt-get install gcc
sudo apt-get install libantlr4-runtime-dev
sudo apt-get install libcli11-dev
sudo apt-get install libfmt-dev
sudo apt-get install libgtest-dev
sudo apt-get install libspdlog-dev
sudo apt-get install llvm
sudo apt-get install llvm-dev
sudo apt-get install meson
sudo apt-get install ninja-build
```

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
- `build/referee` — the compiler CLI (`referee compile file.ref` emits LLVM IR).
- `build/rdb` — the database/CSV helper CLI.
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

## Code Coverage

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
