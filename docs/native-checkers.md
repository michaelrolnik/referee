# Design: ahead-of-time compiled checkers

**Status:** stages 1-3 built. A checker accepts `.rdb`, `.csv` and `.yaml`. Dropping the `--explain` companions from the object remains.
**Scope:** `referee build spec.ref -o checker` producing a native executable that validates traces without compiling anything.

## What is built (stages 1 and 2)

```bash
referee build          spec.ref -o spec.o     # a native object
referee build --shared spec.ref -o spec.so    # a shared object, dlopen-able
referee execute --checker spec.so trace.rdb   # run it -- no .ref, no compile
```

`referee build` compiles the module `Referee::compile` already produces and
runs it through `addPassesToEmitFile` at PIC. The object exports one symbol,
`referee_module`, returning the `referee_module_v1` table
(`runtime/referee_checker.h`): version, the requirements as `{label, eval}`
pairs, `__prepare__`, the embedded schema, and the string-literal table. The
requirement functions keep their source-position names internally; the human
label rides as data, so an ELF symbol never holds a space or a colon.
`--shared` adds a `cc -shared` link.

`referee execute --checker spec.so trace.rdb` `dlopen`s the object, checks the
trace's schema against the one the checker carries (decoded from the same
tagged-binary form a `.rdb` embeds), and drives the table -- no `.ref`, no
LLVM, no compile. Verdicts match `referee execute` (proved on `pass.ref`,
including a string requirement).

Two things were load-bearing and are worth recording:

**Strings stay a pointer compare.** A literal is interned, like every string,
so equality is a pointer compare -- but interning has to happen where the code
*runs*, not where it was *compiled*. A literal compiles to a mutable slot
holding, at first, a pointer to its own bytes; before any requirement runs,
`internModuleStrings` re-interns each slot through the running process's
`Strings` table. The JIT does this too (the slots start raw there as well), so
one mechanism serves both and equality never became a `strcmp`.

**The return type is one byte.** A requirement returns `i1`, which lives in
`AL`; the ABI struct declares `eval` returning `bool`, not `int`, because
reading four bytes would see undefined high bits and turn a `false` into a
pass.

`referee build` needs **no trace**, because `T[]` is now a runtime descriptor
(ragged arrays) rather than a per-run fixed extent -- the doc's open question 3,
moot.

## The standalone executable (stage 3)

```bash
referee build --executable spec.ref -o door-checker
./door-checker run-*.rdb          # no referee, no LLVM, no ANTLR, no .ref
```

`emitExecutable` links the compiled specification against `libreferee_rt.a` --
the JIT-free half of the codebase (the `.rdb` Reader, the type classes, string
interning, the schema codec) plus a small driver carrying `main()`
(`runtime/checker.cpp`). The result's shared-library dependencies are libfmt,
libstdc++ and libc: **no LLVM, no ANTLR**, which the test asserts with `ldd`.
Verdicts match `referee execute` across all 196 requirements of `pass.ref`,
strings included.

The runtime library is located at build time via `$REFEREE_RT_DIR`, then next
to the referee binary, then the build tree. It is the same driver
`referee execute --checker` runs in-process by dlopen, linked instead -- so a
machine that only validates logs needs neither referee nor its dependencies,
which is the whole point of the feature.

## CSV and YAML through a checker

A checker takes `.csv` and `.yaml` traces too, not only `.rdb`. It carries its
schema (the embedded types), so it rebuilds a `Module` from that and packs the
trace against it -- the same `ingestWithModule` the JIT path uses, minus the
`.ref` parse. That is why `ingest` was split: `ingestWithModule` is the
LLVM-and-ANTLR-free half, and the runtime library links it along with the CSV
and YAML loaders (so a checker's shared-library set gains libyaml-cpp).

`referee execute --checker spec.so trace.csv --conf conf.csv` and the
standalone `./checker --conf conf.csv trace.csv` both work; verdicts match
`referee execute` on all 196 requirements of `pass.ref`, strings and conf
included.

**Not yet:** dropping the `--explain` companions from the object as dead
weight, and matching `execute`'s report *order* (the checker walks the table in
compile order, so verdicts match but line order can differ).

---


## The problem

Every run compiles from scratch. `referee execute` parses the `.ref`, lowers it to LLVM IR, runs the O2 pipeline and JITs the result, then reads the trace. For a large requirement set that work is repeated on every invocation, and it is identical every time — the specification has not changed.

### Measured, before deciding anything

`pass.ref` (196 requirements) against traces of two sizes:

| trace | wall time |
| --- | --- |
| 26 rows | 700 ms |
| 10,400 rows | 1904 ms |

That gives a marginal cost of about **0.12 ms per row** and a fixed cost of about **700 ms**, essentially all of it compilation — `referee compile` alone, which stops after the O2 pipeline, accounts for 351 ms of it.

So checking is cheap and compiling is not. For a hundred small traces:

| | total |
| --- | --- |
| today, one invocation each | ~70 s |
| compile once, check many | ~1 s |
| pre-compiled checker | ~0.6 s |

**Most of the available win comes from not recompiling, not from compiling ahead of time.** Accepting several traces per invocation —

```bash
referee execute spec.ref run-*.rdb
```

— captures the great majority of it, needs no new artefact, and is a small change: `buildJitFromRef` is already separate from the per-trace work, so the JIT is built once and the trace loop runs inside it. **That should be built first, and this document's motivation is not primarily speed.**

### What remains after that

Three things multi-trace does not give:

- **No toolchain on the checking machine.** A host validating logs still needs `referee`, and therefore LLVM. That is a large dependency to deploy, and in some environments not deployable at all.
- **No specification on the checking machine.** Shipping a checker currently means shipping the requirements. They are often the thing least intended to be handed over.
- **Better code.** A JIT pays its compile time on every run, which caps how much optimisation is reasonable. A build-time compile can afford O3, cross-requirement LTO and target-specific tuning, because the cost is paid once. This is the only remaining *performance* argument, and it is about the quality of the generated code rather than the compile.

Those are deployment and embedding concerns. They are real, but they should be stated as the motivation rather than the compile time, which multi-trace addresses more cheaply.

## Shape

Three artefacts, in increasing order of independence and of difficulty. Each is the previous one plus a step, so they are stages rather than alternatives.

### 1. An object file

```bash
referee build -c spec.ref -o spec.o
```

The plain output of `addPassesToEmitFile` over the module `Referee::compile` already produces. Composable with any build system, and the primitive the other two are built from. Cross-compilation falls out of it, since `TargetMachine` takes a target triple.

### 2. A shared object

```bash
referee build --shared spec.ref -o spec.so

referee execute --checker spec.so run-*.csv     # or dlopen it from a host
```

The compiled requirements, the requirement table and the schema, with nothing else — the runtime stays in whatever loads it. This is the **embeddable** form, and the one that fits a host application: a test harness or a log pipeline can `dlopen` it and push records through without spawning a process.

It exports exactly one symbol, which keeps the ELF-naming problem from arising at all:

```c
//  The only exported symbol. Everything else is internal, so requirement
//  labels stay data rather than becoming symbol names.
extern "C" referee_module_v1 const* referee_module(void);

struct referee_module_v1 {
    uint32_t                version;        // rejects a .so built by another release
    uint8_t  const*         schema;         // .rdb type encoding
    size_t                  schemaBytes;
    size_t                  count;
    struct requirement {
        char const*         label;          // "reqs/one.ref:5:0 .. 5:10"
        bool              (*eval)(state_t const*, state_t const*, conf_t const*);
    } const*                requirements;
    void                  (*prepare)(state_t*, state_t*, conf_t const*);
};
```

Linking a `.so` is also markedly simpler than linking an executable — no C runtime startup, no `main`, no libc entry.

### 3. A standalone executable

```bash
referee build spec.ref -o door-checker
./door-checker run-*.rdb run-*.csv
```

Stage 2 plus a driver and a static link of the runtime. Fully independent: no `referee`, no LLVM, no `.ref`. Output, exit code and labels identical to `referee execute`, so a CI job can switch between them without touching anything that reads the report.

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

Instead, emit the static table shown under *A shared object* above: the functions get ordinary internal symbol names and the table carries the human-readable label as data. The same table serves all three artefacts. The driver walks the table in order, so the sort that `buildJitFromRef` does at run time happens once at build time instead. The report comes out byte-identical.

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

A shared object is an easier case than an executable and worth reaching first: no C runtime startup, no `main`, no libc entry, so `ld -shared` over the emitted object and the table is close to the whole story.

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
2. **How is a multi-trace report formatted?** This is now the first question rather than a later one, and it should be settled together with `docs/trace-expectations.md`, which adds a per-trace verdict to the same report, because accepting several traces per invocation is the change that should land first — before any of these artefacts. Either each block is prefixed with its trace, or there is a summary, and either way it is a format decision affecting anything that parses the report. Whatever is chosen, the compiled artefacts should match it rather than invent a second convention.
3. **Does `referee build` need to run the trace at all?** With load-sized arrays (see `docs/quantifiers.md`) the element count comes from the trace, so a specification using them cannot be compiled without one — which conflicts with the whole premise. Either such specifications are rejected by `build`, or `build` takes a representative trace to fix the sizes, and the resulting checker only accepts traces matching them. This interaction should be settled before either feature is finished.
