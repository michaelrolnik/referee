# Design: ahead-of-time compiled checkers

**Status:** proposal, not implemented.
**Scope:** `referee build spec.ref -o checker` producing a native executable that validates traces without compiling anything.

## The problem

Every run compiles from scratch. `referee execute` parses the `.ref`, lowers it to LLVM IR, runs the O2 pipeline and JITs the result, then reads the trace. For a large requirement set that work is repeated on every invocation, and it is identical every time — the specification has not changed.

That cost lands where it is least wanted:

- **CI**, which checks the same requirements against a new trace on every commit.
- **Deployed checking**, where a machine that validates logs should not need a compiler toolchain, an LLVM runtime, or the `.ref` sources on it at all.
- **Batch validation** of many traces, where the compile is paid once conceptually and N times in practice.

There is also a distribution argument. Shipping a checker currently means shipping the specification, the compiler and its LLVM dependency. Shipping one executable is a different proposition, especially where the requirements themselves are not to be handed over.

## Shape

```bash
referee build spec.ref -o door-checker [-I dir]…      # once, at spec-change time
./door-checker trace.rdb                              # per trace, no compiler
./door-checker run-*.rdb                              # many traces
```

Output, exit code and per-requirement labels are identical to `referee execute`, so the two are interchangeable in a pipeline and a CI job can switch to the compiled form without touching anything that reads its output.

## Why this is unusually cheap here

Three things are already true, and each is the part that normally makes AOT compilation awkward.

**The runtime contract already exists and is versioned.** The `.rdb` state-buffer section *is* the `state_t[]` layout the compiled code consumes — it is read with pointer fix-up and no per-row processing. There is no marshalling layer to design, and no risk of the compiled code and the data format drifting apart, because they are already the same thing.

**Object emission is already linked.** `meson.build` lists `x86codegen`, `x86desc`, `x86info` and the AArch64 equivalents in `llvm_modules`. Emitting an object file is `TargetMachine::addPassesToEmitFile` over the module `Referee::compile` already produces, not a new dependency.

**The runtime half of the codebase has no LLVM and no ANTLR.** Only `core/visitors/compile.hpp` includes LLVM anywhere under `core/` or `rdb/`, and only `core/antlr2ast.{hpp,cpp}` touch ANTLR:

| Component | LLVM | ANTLR | Needed at check time |
| --- | --- | --- | --- |
| `rdb/database.cpp` — Reader, schema decode | no | no | yes |
| `core/syntax.cpp` — the type classes | no | no | yes |
| `core/strings.cpp` — interning | no | no | yes |
| `core/utils.cpp` | no | no | yes |
| `core/loaders/*`, `visitors/loader.cpp`, `visitors/csvHeaders.cpp` | no | no | yes |
| `rdb/ingest.cpp` — packing CSV/YAML | no | no | yes |
| `core/visitors/compile.*` | **yes** | no | no |
| `core/antlr2ast.*` | no | **yes** | no |

So the produced executable links a small runtime and neither LLVM nor ANTLR. It does not parse `.ref` at check time — there is nothing left to parse.

## What the executable contains

1. **The compiled requirement functions**, from the same `llvm::Module` `Referee::compile` builds today, emitted as an object file instead of handed to the JIT.
2. **`__prepare__`**, unchanged — computed signals are filled exactly as they are now.
3. **A requirement table**, described below.
4. **The embedded schema**, so the checker can reject a trace it was not built for, and so it can derive CSV/YAML column names without a `.ref`.
5. **The driver**, which is `runAgainstRdb` with the JIT removed, plus the extension dispatch `referee execute` already has.

### The requirement table

Requirements are currently found by name: `Compile::make` emits one function per statement called `[file:]row:col .. row:col`, and `buildJitFromRef` looks each up through the JIT, sorts them and calls them.

That does not survive into an object file. Those names are fine as JIT symbols and poor as ELF symbols — they contain spaces, colons and slashes. Mangling them would make the labels unrecoverable, which matters because the label *is* the report.

Instead, emit a static table alongside the functions:

```c
struct requirement {
    char const* label;      // "reqs/one.ref:5:0 .. 5:10"
    bool      (*eval)(state_t const* frst, state_t const* last, conf_t const*);
};
```

with the functions given ordinary internal symbol names and the table carrying the human-readable label as data. The driver walks the table in order, so the sort that `buildJitFromRef` does at run time happens once at build time instead. The report comes out byte-identical.

### The embedded schema

`runAgainstRdb` cross-checks the `.rdb`'s embedded schema against the AST's `data` / `conf` declarations before running anything, and refuses on a mismatch. A compiled checker must do the same, so it needs the schema it was built against.

The `.rdb` writer already serialises types — `TypeEnum`, `TypeStruct`, `TypeArray` and the primitives, via a tag-dispatched constructor table. Emitting that same encoding into a data section, and decoding it with the same code at startup, reuses the format rather than inventing a second one. `typesEqual` then runs unchanged.

This is what makes the checker safe to hand to someone: given the wrong trace it says so, rather than reading whatever bytes happen to be there.

## Build pipeline

```
spec.ref ──parse──> AST ──lower──> llvm::Module ──O2──> object file ─┐
                     │                                               ├─link─> checker
                     └──serialise──> schema + requirement table ─────┤
                                                    libreferee_rt.a ─┘
```

Steps 1–3 are what `Referee::compile` already does. Step 4 is `addPassesToEmitFile`. The new work is the table and schema emission, and the link.

### The linking problem

This is the one genuinely awkward part, and it should be decided rather than discovered.

Producing an executable needs a linker and a C runtime. Options, in the order I would try them:

1. **Invoke the system linker** (`cc` as the driver, so it handles crt and libc). Simple, and correct on any machine with a toolchain. Fails on machines without one — which includes some of the deployment targets that motivate the feature.
2. **Emit an object file and stop**, leaving the link to the caller: `referee build -c spec.ref -o spec.o`. Honest, composable, and pushes the problem to a build system that already has a compiler. Worth supporting regardless as the primitive the executable mode is built on.
3. **Link in-process with LLD**, if it is available as a library. Removes the external toolchain dependency at the cost of another LLVM component.

I would implement (2) first because it is unavoidable anyway, then (1) on top, and treat (3) as an optimisation.

### Cross-compilation

Falls out of (2) almost free: `TargetMachine` is constructed from a triple, so `referee build --target aarch64-linux-gnu` emits an object for that target. The link then has to happen there, or with a cross-linker. Worth exposing the flag even before the link story is settled, because emitting an object for another architecture is genuinely useful on its own.

## What the checker accepts

Any trace `referee execute` accepts — `.rdb`, `.csv`, `.yml`/`.yaml` — dispatched on extension exactly as it is today. A checker that only ate `.rdb` would push an ingestion step onto whoever runs it, which defeats the purpose of handing them a single executable.

The whole ingestion path is already free of both heavy dependencies, so this costs the runtime very little:

| Component | LLVM | ANTLR |
| --- | --- | --- |
| `core/loaders/{csv,yml,row}.cpp` | no | no |
| `core/visitors/loader.cpp` — blob building | no | no |
| `core/visitors/csvHeaders.cpp` — column derivation | no | no |
| `rdb/ingest.cpp` — packing | no | no |

There is one thing in the way. `ingest()` derives its schema by calling `Referee::parseSchema`, which parses the `.ref` and therefore needs ANTLR:

```cpp
auto    schema = Referee::parseSchema(refIn, refName, includePaths);
```

A compiled checker has no `.ref` to parse — it carries the schema instead. So `ingest()` should be split in two: a thin wrapper that parses a specification and calls the second half, and the second half taking an already-built `::Module*`. The checker calls the second half directly with its embedded schema, and ANTLR stays out of the runtime.

That split is worth making regardless of this feature. It separates "what the schema is" from "how a trace is packed", which is the more honest factoring, and it removes the current oddity that packing a trace requires a parser.

With that done, the checker's flow for a CSV is the same as `Referee::execute`'s: ingest into an in-memory `.rdb`, then run against the Reader. For a `.rdb` it skips straight to the second step. The two paths already converge on `runAgainstRdb` today, so the checker inherits that convergence rather than reimplementing it.

## Consequences worth stating

**The `.ref` becomes a build-time input.** Requirement labels still name files and lines, so a report references sources that may not be present where the checker runs. That is the same situation as any compiled program's debug information and is fine, but it should be documented rather than surprising.

**Two code paths must not diverge.** `referee execute` and a compiled checker have to agree, or the feature is worse than useless. They share the lowering, `__prepare__` and the schema check, so the risk is concentrated in the driver. The test for this writes itself: build a checker from each existing fixture, run it, and require the output to match `referee execute` byte for byte.

**The optimisation pipeline can be turned up.** A JIT pays its compile time on every run, which is why the O2 pipeline is a reasonable ceiling now. A build-time compile can afford O3, LTO across the requirement functions, and target-specific tuning (`-march=native` equivalent), because the cost is paid once. This is a real performance argument for the feature beyond skipping the compile: the emitted code can be better than what the JIT would produce.

## Open questions

1. **Is the executable self-contained or does it link a shared runtime?** Static is the simpler distribution story and the one the motivation implies. Accepting CSV and YAML pulls in yaml-cpp and rapidcsv, so "small" here means free of LLVM and ANTLR rather than free of everything.
2. **Should the checker report which trace failed** when handed several? `referee execute` takes one. Multiple traces need either a per-trace prefix in the output or a summary, and that is a format decision affecting anything that parses the report.
3. **Does `referee build` need to run the trace at all?** With load-sized arrays (see `docs/quantifiers.md`) the element count comes from the trace, so a specification using them cannot be compiled without one — which conflicts with the whole premise. Either such specifications are rejected by `build`, or `build` takes a representative trace to fix the sizes, and the resulting checker only accepts traces matching them. This interaction should be settled before either feature is finished.
