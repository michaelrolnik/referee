# Referee (REF)

Write a requirement a reviewer can sign off on, and compile it to a checker:

```text
@lock_engages  globally, if button.DEPRESSED, then in response lock.ON after 100 milliseconds;
```

**Referee (REF)** is a C++ **runtime-verification** toolchain for the REF
requirement-specification language. You write behavioural requirements as
**Dwyer-style property specification patterns** in structured English (or as raw
**LTL / MTL** formulas); Referee parses them with **ANTLR4** and lowers them to
**LLVM IR**, emitting a **monitor** that checks the requirement over a trace or log —
offline or as it streams. The English phrasing is the *source language of a compiler*,
not a form you paste a formula into.

📖 **[Documentation](https://michaelrolnik.github.io/referee/)** ·
[The REF language](https://michaelrolnik.github.io/referee/language.html) ·
[Specification patterns](https://michaelrolnik.github.io/referee/specification-patterns.html) ·
[Architecture](https://michaelrolnik.github.io/referee/architecture.html) ·
[Online monitoring](https://michaelrolnik.github.io/referee/monitor.html)

*(Written "Referee (REF)" to disambiguate from other projects called* referee*.)*

## Contents

- [Status](#status)
- [Architecture](#architecture)
- [Documentation](#documentation)
- [Design rationale](#design-rationale)
- [The REF language](#the-ref-language)
- [Installation](#installation)
- [Linux](#linux)
- [MacOS](#macos)
- [Checkout](#checkout)
- [Building](#building)
- [Regular](#regular)
  - [Overriding dependency locations](#overriding-dependency-locations)
  - [Run Tests](#run-tests)
  - [Coverage](#coverage)
- [Editor support](#editor-support)
- [Installing it](#installing-it)
- [Testing the grammar](#testing-the-grammar)
- [Language server (LSP)](#language-server-lsp)
  - [In Docker](#in-docker)
- [Running referee](#running-referee)
- [`referee compile` — emit LLVM IR](#referee-compile--emit-llvm-ir)
- [`referee execute` — JIT-compile and check a CSV trace](#referee-execute--jit-compile-and-check-a-csv-trace)
  - [Output](#output)
  - [Worked example](#worked-example)
  - [Building your own trace](#building-your-own-trace)
- [`referee monitor` — check a trace online, as it streams](#referee-monitor--check-a-trace-online-as-it-streams)
- [Checking several traces](#checking-several-traces)
  - [Naming a requirement](#naming-a-requirement)
  - [Declaring what a trace should do](#declaring-what-a-trace-should-do)
  - [A corpus in a file](#a-corpus-in-a-file)
  - [Output detail](#output-detail)
- [Run traces — `--explain`](#run-traces----explain)
  - [Visualizing traces with `view_bokeh.py`](#visualizing-traces-with-view_bokehpy)
- [Referee Database (RDB)](#referee-database-rdb)
- [On-disk layout](#on-disk-layout)
- [Producing `.rdb` files — `rdb build`](#producing-rdb-files--rdb-build)
- [Merging multi-rate sources — `rdb merge`](#merging-multi-rate-sources--rdb-merge)
  - [`rdb merge` Worked Examples](#rdb-merge-worked-examples)
- [Consuming `.rdb` files — `referee execute`](#consuming-rdb-files--referee-execute)
- [Inspecting `.rdb` files — `rdb dump`](#inspecting-rdb-files--rdb-dump)
- [Code Coverage](#code-coverage)
- [References](#references)

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
- A CLI with four subcommands (`compile`, `header`, `build`, `execute`):
  - `referee compile file.ref [-I dir]…` — emits LLVM IR for a given `.ref` file.
  - `referee execute file.ref trace.{csv,yaml,rdb}… [--success …] [--failure …] [--conf conf.{csv,yaml}] [-v 0..2] [-I dir]…` — JIT-compiles the requirements and evaluates them against one or more traces, reporting `PASS`/`FAIL` per requirement (and exiting non-zero if any requirement fails). Several traces are compiled **once** and checked in turn; `--failure` declares traces that must be rejected. See *Checking several traces* below. Column names in the CSV/YAML must match the layout produced by `csvHeaders` (e.g. `__time__`, `pos.x`, `limits[0]`, …); see `test/logic/data.csv` and `test/logic/conf.csv` for working examples. `.rdb` traces (see below) are read with no per-row processing — only pointer fix-up.
- An **editor plugin** (`editors/vscode/`) giving syntax highlighting, bracket/comment handling and specification-pattern snippets for `.ref` files in VS Code and its forks (Cursor, Antigravity, VSCodium). Its keyword lists are generated from `core/referee.g4` rather than hand-written, and the grammar is tested against the same TextMate engine the editors use. See *Editor support* below.
- A companion `rdb` binary for packing CSV/YAML traces into the on-disk **RDB** format consumed directly by `referee execute`:
  - `rdb build spec.ref trace.csv [--conf conf.csv] [-I dir]… -o trace.rdb` — packs a CSV/YAML trace into a `.rdb` whose state-buffer section is byte-for-byte the layout the JIT consumes (see *Referee Database* below).
  - `rdb dump trace.rdb` — pretty-prints the schema, conf, and per-state rows using the AST types embedded in the file.

**What is missing**

- **Streaming / online monitoring.** `referee monitor spec.ref` reads states one CSV row at a time from stdin and checks every requirement as the trace unfolds, reporting a violation the instant an invariant breaks (rather than after the run). It reuses the same compiled requirement functions as `execute`; for a spec of invariants it takes an O(1)-per-state fast path via the single-state `__atom__` companions the code generator emits, and falls back to re-checking the prefix for anything else, so online and offline verdicts agree at every prefix. See *`referee monitor`* below, `examples/monitor/`, and `docs/monitor.md`.
- **A fuller language server.** `referee-lsp` now provides live in-editor **diagnostics** (parse + type errors), **completion** (names in scope + keywords, narrowing to members after a `.`), **hover** (a name's declaration), **go-to-definition** (following `import`s across files), **document symbols** (the outline view), **find-references** (every use of a name, across imports), **rename** (rewrite a name and all its uses, across imports), and **signature help** (the parameters of the call you are typing, `func`s and `std::…` built-ins alike), wired into the VS Code extension — see *Language server (LSP)*. Find-references and rename are **type-aware**: a struct field or enum case is distinguished from a signal of the same name, and a field is matched only where the accessed value has its owning type. A full IDE feature set is in place.
- **Product-specific model exporters/importers** to adapt arbitrary system logs into the canonical trace format.

## Architecture

The pipeline is: REF source → ANTLR4 parse → typed AST → semantic visitors (typecalc, rewrite, canonic, negated) → **LLVM IR** → O2 → either the in-process ORC JIT (`execute`, `monitor`) or an ahead-of-time object exporting the `referee_module` ABI (`build`). Independently, a trace (CSV/YAML) is ingested into a `.rdb` — a `state_t[]` buffer the compiled code reads directly. The code generator emits several function shapes per requirement (`(frst,last,conf)`, the per-state `__col__`/`__atom__` companions, `__prepare__`, the `referee_module` table).

**[docs/architecture.md](docs/architecture.md)** is the full map — the pipeline, the function shapes and `state_t` layout, the JIT and AOT backends, the trace format, and the online monitor.

## Documentation

Published: **<https://michaelrolnik.github.io/referee/>**. Sources under `docs/`:

- **Start here**
  - [getting-started.md](docs/getting-started.md) — install, build, write a first requirement, check a first trace.
  - [cookbook.md](docs/cookbook.md) — worked requirements for real systems: watchdogs, startup ordering, bounded response, mode invariants, debounce, acknowledgement.
- **[architecture.md](docs/architecture.md)** — the whole pipeline (parse → AST → LLVM → JIT/AOT), the per-requirement function shapes, the `state_t` layout, the trace format, and the monitor.
- **Language & semantics**
  - [language.md](docs/language.md) — the whole surface syntax in one place: statements, declarations, types, operators, temporal operators, specification patterns.
  - [specification-patterns.md](docs/specification-patterns.md) — Dwyer's property specification patterns in REF: the pattern × scope grid (absence, existence, universality, response, precedence, chains) mapped to the LTL/MTL each desugars to.
  - [temporal-operators.md](docs/temporal-operators.md) — the operator set: `G`/`F`/`X`/`U`/`R`, past-time `H`/`O`/`Y`/`S`/`T`, strong vs weak on finite traces, MTL `[lo:hi]` windows, accumulators, and the TPTL freeze.
  - [references.md](docs/references.md) — the temporal-logic and specification-pattern literature the language is built on.
  - [quantifiers.md](docs/quantifiers.md) — bounded quantifiers over array elements.
  - [ragged-arrays.md](docs/ragged-arrays.md) — unbounded `T[]` arrays that carry their own length.
  - [external-functions.md](docs/external-functions.md) — `func` declarations bound to `.so` plugins.
  - [accumulator-cost.md](docs/accumulator-cost.md) — why `Itg` / `Sum` / `Cnt` go quadratic under a temporal scope.
- **Traces & checking**
  - [run-traces.md](docs/run-traces.md) — run traces: recording requirement *coverage*, not just failures.
  - [run-trace-format.md](docs/run-trace-format.md) — the `--explain` NDJSON schema.
  - [trace-expectations.md](docs/trace-expectations.md) — a corpus of traces that declare which requirements they must violate.
  - [native-checkers.md](docs/native-checkers.md) — ahead-of-time compiled, LLVM-free checkers (`referee build`).
- **Online monitoring**
  - [monitor.md](docs/monitor.md) — the design: LTL₃ semantics, the verdict domain, streaming.
  - [monitor-implementation.md](docs/monitor-implementation.md) — how it is built: the phases, the single-state atom path.
- **Notes**
  - [signal-node-leak.md](docs/signal-node-leak.md) — a fixed AST-interning bug, kept as a record.

## Design rationale

The usual way to check behavioural requirements against a log is to hand-write a checker per requirement — a state machine with flags, counters, and event handlers. Referee exists because that approach degrades badly as the requirement set grows. The design choices below are each a response to a specific way it degrades.

1. **Declarative, not imperative.** A requirement is written once, in a notation meant for it. "Between `called` and `opened`, a transition to `atfloor` occurs at most twice" is a single line of REF. Written as a checker it is a class whose flags and counters have to be re-derived, by hand, every time the requirement is reworded — and the requirement and the code that checks it drift apart silently, because nothing connects them.
2. **Patterns instead of raw formulas.** Engineers write Dwyer-style English-like patterns; the compiler translates them into the underlying temporal-logic formulas, handling finite-trace semantics, strong/weak next, and bounded operators uniformly. Hand-written checkers re-implement each pattern ad hoc, and the off-by-one and end-of-trace mistakes that come with that are made once per requirement rather than once per compiler.
3. **Strong types, not just booleans.** REF has integers, numbers, strings, enums, structs, and multi-dimensional arrays, so a requirement can talk about signal values directly (`lock.ON`, `abc.x[2][3].a`). Without that, every requirement sits behind a hand-maintained layer that flattens real signals into booleans — a layer that is itself untested and a reliable source of bugs.
4. **All requirements evaluated together.** Every property is compiled into one module and evaluated over the same trace, so mutually inconsistent requirements become observable: they show up as a trace that no requirement set can satisfy. A collection of independent checker scripts cannot surface a contradiction between two of them at all.
5. **Native performance.** Requirements compile to optimized LLVM IR and run as native code via the ORC JIT, with the unbounded temporal operators lowered to a linear pass over the trace. Checking a long trace against hundreds of properties is the normal case, not the stress case.
6. **Separation of concerns.** Recording what the system did and checking that it was allowed to are fully decoupled. The same compiled requirements apply to real logs, simulated traces, offline batches, or (once streaming lands) a live feed, with no change to the requirement source.
7. **Reviewable by the people who own the requirements.** Reading and writing a REF file needs an understanding of the system, not programming fluency. A requirement that a test engineer can review is a requirement that gets reviewed.

## The REF language

A REF program declares the signals it talks about, then states requirements over
them. A requirement is either a temporal-logic formula or a **specification
pattern** — structured English, in the Dwyer tradition, that desugars to one.
An elevator, in full:

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

Each line compiles to a boolean-valued function over the trace, and the runtime
asserts that every one returns `true` for every valid trace of the system.
Because all requirements are evaluated over the same trace in the same module,
any inconsistency between them becomes observable: the set of requirements is
collectively checkable, not a collection of independent scripts.

The scope comes first — `globally`, `before P`, `after P`, `while P`,
`between P and Q`, `after P until Q` — then the pattern body. Sixteen bodies
cover the qualitative Dwyer catalogue (universality, absence, existence,
response, precedence and their chain variants) and its real-time extensions
(transient and steady state, minimum and maximum duration, recurrence, response
invariance, until). Time bounds are `within N units`, `after N units` or
`between N and M units`, where `units` is one of `nanoseconds`, `microseconds`,
`milliseconds`, `seconds`, `minutes`.

**The language reference lives under `docs/` and is the authoritative version:**

| | |
| --- | --- |
| [getting-started.md](docs/getting-started.md) | install → first requirement → first verdict, in ten minutes |
| [language.md](docs/language.md) | the whole surface syntax: statements, declarations, types, expressions, precedence |
| [specification-patterns.md](docs/specification-patterns.md) | all sixteen pattern bodies across the five scopes, with bounds and constraints |
| [temporal-operators.md](docs/temporal-operators.md) | `G`/`F`/`X`/`U`/`R`, past-time `H`/`O`/`Y`/`S`/`T`, strong vs weak on finite traces, MTL windows, accumulators, the TPTL freeze |
| [cookbook.md](docs/cookbook.md) | worked requirements: watchdogs, startup ordering, bounded response, mode invariants, debounce |
| [architecture.md](docs/architecture.md) | temporal lowering, the two recurrence families, and the per-construct cost table |

Every REF example in those pages is kept as a fixture under `test/logic/` and
run by `Cli.DocumentedExamplesCompileAndPass`, so a documented example that
stops compiling — or stops holding — fails the build.

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
- `build/referee-lsp` — the REF language server (LSP over stdio). Built by the default `ninja -C build`; see *Language server (LSP)* under *Editor support*.
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

`editors/vscode/` is an extension for `.ref` files: syntax highlighting, snippets, and — via a bundled language-server client — live diagnostics. It works in VS Code and its forks — Cursor, Antigravity, VSCodium — which all consume the same extension format.

It highlights the temporal operators (future and past scoped separately, and only where one is actually applied, so a stray capital is left alone), the whole Dwyer specification-pattern vocabulary, declarations with their declared names, freeze variables and `__time__`, and every literal form. The keyword lists are generated from `core/referee.g4` rather than written by hand, so they track the grammar exactly. There are also snippets for the common specification patterns.

Highlighting and snippets work with no setup; the **language server** adds live parse and type diagnostics once you point it at the `referee-lsp` binary — see *Language server (LSP)* below.

## Installing it

The extension is not published to a marketplace; build and install it from this checkout. It now carries a language-server client (see *Language server (LSP)* below), so it has a compiled entry point and a runtime dependency — build it first:

```bash
cd editors/vscode
npm install
npm run compile        # tsc: src/extension.ts -> out/extension.js
```

Then package a `.vsix` and install that — the recommended route (it bundles `out/` and the `vscode-languageclient` runtime, and registers the extension with the editor rather than relying on a directory scan, so it also works over SSH):

```bash
npx @vscode/vsce package --allow-missing-repository
```

then, in the editor, **Extensions** view → the **`⋯`** menu → **Install from VSIX…** → pick `referee-ref-0.2.0.vsix`, and **Reload Window**. This works the same in VS Code and its forks — Cursor, Antigravity, VSCodium — which all consume the same `.vsix`.

Open a `.ref` file and check the status bar reads **REF**. For diagnostics, set the server path (see *Language server (LSP)*); highlighting works with no further setup.

**Remote/SSH:** install the `.vsix` on the **remote** machine (the Extensions view does this when the window is remote), and make sure `referee-lsp` is built there too — both the client and the server run remote. If you install by hand instead of via `.vsix`, the extension directory per editor is:

| Editor | Local | Remote (SSH) |
| --- | --- | --- |
| VS Code | `~/.vscode/extensions/` | `~/.vscode-server/extensions/` |
| Cursor | `~/.cursor/extensions/` | `~/.cursor-server/extensions/` |
| Antigravity | `~/.antigravity/extensions/` | `~/.antigravity-ide-server/extensions/` |

A hand copy must include the built `out/` and the production `node_modules` (the `vscode-languageclient` runtime), not just the static assets — which is why the `.vsix` route is preferred.

## Testing the grammar

```bash
cd editors/vscode/test
npm install
node tokenize.cjs
```

This tokenizes a sample against the same TextMate engine the editors use and asserts the resulting scopes. It is worth running after any grammar edit, because Oniguruma is stricter than JavaScript's regex engine and an invalid pattern makes the editor drop the grammar **silently** — the file simply renders unhighlighted, with no error reported anywhere.

## Language server (LSP)

`build/referee-lsp` is a Language Server for `.ref`. It speaks LSP — JSON-RPC over stdio, `Content-Length`-framed — and reuses the compiler front-end (`Referee::diagnose`) to publish **live parse and type diagnostics** as you edit: the same errors `referee compile` would report, surfaced inline with no build step. It parses and type-checks only (no LLVM lowering), so it is fast, and since REF specs are small it re-checks the whole document on each change (full-text sync — ANTLR has no incremental parse). It also offers **completion** — a bare identifier lists the names in scope (signals, confs, types, functions, with imports folded in) plus the language keywords, and after a `.` it narrows to that type's struct fields or enum cases (`pt.` → `x` `y`; `k.` → the enum's cases). Keywords come from the lexer's own vocabulary, so they track the grammar. There is also **hover** (point at a name to see its declaration: `data pt : Point`, a struct/enum body, a field's type), **go-to-definition** (a name jumps to its `data`/`conf`/`type`/`func` declaration, a member to its field inside the owning `type`; **follows `import`s**, so a name declared in an imported file opens that file), **document symbols** — the outline / breadcrumbs list every declaration in source order, each struct/enum carrying its fields/cases as children — **find-references** (every whole-word use of the name under the caret, across the document and the files it imports, comments excluded), **rename** — retype a name and every use is rewritten across the document and its imports, guarded by a validity check on the new identifier — and **signature help**: inside a call's argument list, the parameters of the enclosing `func` (or `std::…` built-in) are shown with the active one highlighted, overloads included. Find-references and rename are **type-aware**: the field/enum-case namespace is kept distinct from signals/types/functions (a field `x` is never confused with a signal `x`), and a member reference is matched only where the left-hand value has the field's owning type, so two structs sharing a field name are not conflated. A left-hand side too complex to type (say `f(a).x`) is kept rather than dropped.

`referee-lsp` is a *server*, not a REPL: an editor's LSP client launches it and talks to it over stdio (run bare in a terminal it just waits for a framed message). Point any LSP client at the binary. Neovim, for example:

```lua
vim.lsp.start({
  name = 'referee-lsp',
  cmd  = { vim.fn.getcwd() .. '/build/referee-lsp' },              -- or an absolute path
  root_dir = vim.fs.dirname(vim.fs.find({ '.git' }, { upward = true })[1]),
})
```

The VS Code extension in `editors/vscode/` bundles a [`vscode-languageclient`](https://www.npmjs.com/package/vscode-languageclient) client that launches `referee-lsp` for you, so diagnostics work in VS Code and its forks (Cursor, Antigravity, VSCodium) alongside the highlighting — see *Installing it* above, then set `referee.lsp.path`:

```jsonc
// Settings (JSON), or a workspace .vscode/settings.json
{
  "referee.lsp.path": "/absolute/path/to/referee/build/referee-lsp"
}
```

The default is `referee-lsp` (found on `PATH`); point it at your build, or set it to `docker` with `referee.lsp.args` (below) for a containerized server. The command **REF: Restart Language Server** reloads it after a rebuild.

One note on imports: the server resolves a file's `import`s against the document's own filesystem path, so open a spec by its real path for cross-file imports to resolve. A single self-contained `.ref` needs nothing.

### In Docker

The repository's `Dockerfile` builds `referee`, `rdb`, and `referee-lsp` into a small runtime image:

```bash
docker build -t referee:lsp .
```

`referee-lsp` lands at `/usr/local/bin/referee-lsp`. Because it talks over stdio, override the entrypoint (the image defaults to `referee`) and keep stdin open with `-i`; mount the workspace **at the same path** so imports resolve:

```bash
docker run --rm -i --entrypoint referee-lsp \
  -v "$PWD":"$PWD" -w "$PWD" referee:lsp
```

An editor's LSP client can use that whole `docker run …` line as its server `cmd`.

# Running referee

`build/referee` is the main CLI, driven through four subcommands: `compile` (emit LLVM IR), `header` (C header/stub for external functions), `build` (ahead-of-time checker: object, `--shared`, `--executable`), and `execute`. Pass `--help` or `<subcommand> --help` for the full option list:

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
  - the first column is `__time__` (per-row timestamp, integer-valued, **in nanoseconds** — the unit keywords in a specification pattern scale to it, so `within 100 milliseconds` means 100,000,000 ticks of `__time__`);
  - one column per leaf field of every `data` declaration — for `data pos : struct { x: number; y: number; };` you get `pos.x` and `pos.y`; for `data limits : integer[3];` you get `limits[0]`, `limits[1]`, `limits[2]`; nesting expands the obvious way (`grid[1][2].x`);
  - enums are written as the bare member name (`ON`, `OFF`), booleans as `true`/`false` (or `1`/`0`), strings unquoted.
- **`--conf conf.csv`** *(optional)* — a single-row CSV carrying values for every `conf` declaration. Same column-naming rules as `data.csv`. If the spec has no `conf` declarations, omit it; if it has them and you omit the file, the configuration is zero-initialised, which is rarely what you want.

### Output

One line per requirement. A [named](#naming-a-requirement) requirement is
labelled by its name; an unnamed one by its source position, and the report is
sorted by that position:

```text
locks_promptly                           PASS
never_open_and_locked                    PASS
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

## `referee monitor` — check a trace online, as it streams

Where `execute` waits for a finished trace, `monitor` checks one as it arrives: it reads states one CSV row at a time from stdin and evaluates every requirement as the trace unfolds, printing a violation the instant an invariant breaks.

```bash
producer | ./build/referee monitor spec.ref [--conf conf.csv] [--stop-at-first]
```

Input is the same CSV `execute` accepts — a header row, then one state per line — so a live producer pipes straight in, or you feed a file with `< trace.csv`. It speaks stdin/stdout, so a socket bridges with `nc` (`nc -l 9000 | referee monitor spec.ref`); no network code of its own.

Per state it writes a line of three-valued verdicts, a column per requirement:

```
__time__=2000  never_overheat=?  heater_off_hot=?  reaches_comfort=PASS
VIOLATION  heater_off_hot  @ __time__=5000  5000,90,true
```

A **safety** requirement (an invariant) reads `?` while it holds and `FAIL` the instant it breaks — with a `VIOLATION` line naming the offending state and its time. A **liveness** requirement (an eventually) reads `?` until it is met, then settles `PASS`, and is never mistaken for a violation while merely unmet; it is finalised at end of stream. Output is coloured on a terminal, plain when piped. `--stop-at-first` exits non-zero at the first violation, for a supervisor halting the system under test; otherwise a single pass collects every violation and the exit code reflects the end-of-stream result.

Under the hood there are two routes over the *same* compiled code `execute` uses. When every requirement is a single-state atom — an invariant, a bare predicate, or an eventually — the monitor takes an **O(1)-per-state fast path**: it evaluates the `__atom__` companion the code generator emits on the one incoming state and folds the result into a per-requirement latch. Anything else — a bounded operator, a Dwyer scope, a computed signal, an `until` — falls back to re-checking the growing prefix. Either way the monitor's verdict agrees with `execute`'s at every prefix, which the tests pin.

A runnable demo is in [`examples/monitor/`](examples/monitor/) (a thermostat plus a feeder script); the design is [`docs/monitor.md`](docs/monitor.md) and the build [`docs/monitor-implementation.md`](docs/monitor-implementation.md).

## Checking several traces

`referee execute` takes any number of traces and compiles the specification once for all of them:

```bash
referee execute spec.ref run-*.csv --conf conf.csv
```

This matters more than it sounds. Compilation is a fixed cost — roughly 700 ms for a 196-requirement specification — while checking is about 0.12 ms per trace row. Twenty small traces one invocation at a time take ~13.7 s; the same twenty in one invocation take ~1.1 s, and the gap widens with the corpus.

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

Every column comes from a companion function the compiler emits from the *same* AST node the verdict comes from, so the picture cannot drift from the verdict — referee checks that the column's first-state value equals the verdict on every run. The format is the contract (`schema/run-trace.schema.json`, `docs/run-trace-format.md`); the viewers are replaceable. `examples/door/` ships a worked run trace with its rendered HTML (`examples/door/nominal.bokeh.html`).

### Visualizing traces with `view_bokeh.py`

`tools/view_bokeh.py` generates an interactive HTML dashboard using Bokeh:

```bash
# Install dependencies
pip install bokeh pandas

# Generate ndjson trace and build interactive Bokeh dashboard
./build/referee execute spec.ref trace.csv --explain run.ndjson
python3 tools/view_bokeh.py run.ndjson -o run.bokeh.html
```

#### Bokeh Viewer Features

- **Interactive Hover & Tooltips**: Hover over states or intervals to view timestamps, signal values, verdict states (`PASS`, `FAIL`, `VACUOUS`), and temporal witness spans.
- **Linked Zoom & Pan**: All requirement panels share a common time axis. Zooming or panning on one panel dynamically updates all requirement rows simultaneously.
- **Requirement Selector Dropdown**: Select a specific requirement from the "show" dropdown menu (e.g., `@door_closes_in_2s` or `spec.ref:12:0`) to isolate that requirement and automatically filter out unrelated background signals.
- **Offline & Self-Contained**: Uses `mode="inline"` to bundle BokehJS into the output file (~1.7 MB), ensuring `run.bokeh.html` can be viewed offline, attached to CI build artifacts, or shared directly without external dependencies.

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
| `sources…` | two or more `.csv` / `.yaml` / **`.rdb`** files, each carrying `__time__` and some of the signals |
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

A source may itself be a **`.rdb`** — an already-packed trace stands in
anywhere a CSV does. It is decoded back to a flat trace first (the same
operation `rdb dump` performs, but to CSV rather than YAML), so a packed
baseline can be merged with a freshly-logged signal without unpacking it by
hand.

The merge is a plain column-and-timestamp operation — no `.ref` needed for the
fold itself; the specification is used only to pack the result, so the same
schema check `referee execute` runs applies to the merged `.rdb`.

> **One clock.** Timestamps are compared across sources directly, so the
> sources must share an epoch and unit. Align them first if they do not.

### `rdb merge` Worked Examples

**Example 1: Merging multi-rate CSV sensor logs with backfilling**
Combine a 1 kHz accelerometer log with a 10 Hz temperature telemetry file, filling initial leading gaps with each signal's first recorded sample:

```bash
./build/rdb merge spec.ref sensors_1khz.csv temp_10hz.csv \
                --leading backfill \
                -o trace_merged.rdb
```

**Example 2: Overlaying a new signal onto an existing `.rdb` trace**
Merge a pre-packed baseline trace (`baseline.rdb`) with an additional debug log (`debug_overlay.csv`), resolving shared overlapping columns by merging records:

```bash
./build/rdb merge spec.ref baseline.rdb debug_overlay.csv \
                --overlap merge \
                -o trace_updated.rdb
```

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

## References

REF is built on the property-specification patterns of Dwyer, Avrunin and
Corbett, over a temporal-logic core drawn from the classical literature — LTL
and its past-time mirror, metric (time-bounded) operators, TPTL freeze
quantifiers, and finite-trace strong/weak semantics. The primary sources for
each are collected in **[docs/references.md](docs/references.md)**.

The pattern catalogue spans two lines of work: the qualitative patterns of
Dwyer, Avrunin and Corbett, and the real-time extensions — transient and steady
state, minimum and maximum duration, recurrence, response invariance, and the
`while` scope — from Konrad and Cheng's real-time specification patterns and the
structured-English grammar of Autili et al. All three are cited in
[docs/references.md](docs/references.md).

The paper that inspired the project: M. B. Dwyer, G. S. Avrunin, J. C. Corbett,
*Patterns in Property Specifications for Finite-State Verification*, ICSE 1999
([PDF](https://www.cs.colostate.edu/~france/CS614/Readings/Readings2011/propPatterns1-p7-dwyer.pdf)).
