---
title: "Getting started with Referee"
description: "Install Referee, build it, write your first temporal-logic requirement in REF, and check it against a CSV trace — from an empty directory to a passing verdict."
---

# Getting started with Referee

From an empty directory to a checked requirement. Ten minutes, most of it
dependency installation.

Every example on this page is kept as a test fixture
(`test/logic/docs_getting_started.ref`), so it cannot drift from the language.

## Install the dependencies

Referee builds with [Meson](https://mesonbuild.com/) and
[Ninja](https://ninja-build.org/).

### Linux

```bash
sudo apt-get install clang-format g++ gcc libantlr4-runtime-dev libcli11-dev \
                     libfmt-dev libgtest-dev libspdlog-dev libyaml-cpp-dev \
                     llvm llvm-dev meson ninja-build
```

Ubuntu's `antlr4` package (4.9.2) is **incompatible** with this grammar — the
C++ runtime it ships is 4.10, and generated sources from 4.9.2 will not build
against it. Fetch the matching 4.10 generator and point Meson at it:

```bash
curl -L -o ~/antlr-4.10.1-complete.jar https://www.antlr.org/download/antlr-4.10.1-complete.jar
meson setup build -Dantlr4_jar=~/antlr-4.10.1-complete.jar
```

Meson also looks in `/usr/local/lib/antlr-4.10.1-complete.jar`,
`/usr/local/share/antlr/antlr-4.10.1-complete.jar` and
`/usr/share/java/antlr-complete.jar`, so dropping the jar in one of those needs
no option at all.

### macOS

```bash
brew install antlr antlr4-cpp-runtime clang-format cli11 fmt \
             googletest llvm meson ninja spdlog
```

## Build

```bash
git clone git@github.com:michaelrolnik/referee.git
cd referee
git submodule update --init --recursive

meson setup build
ninja -C build
```

Executables land in `build/`. Confirm the build:

```bash
meson test -C build --print-errorlogs
```

## Your first requirement

A REF program declares the signals it talks about, then states requirements over
them. Put this in `door.ref`:

```text
data button_pressed : boolean;
data door_open      : boolean;
data locked         : boolean;

@never_open_and_locked
globally, it is never the case that door_open && locked;

@locks_promptly
globally, if button_pressed, then in response locked within 100 milliseconds;
```

Four things are happening:

- `data` declares a **time-varying signal** — one column of each trace record.
- `globally,` is the **scope**: this requirement is about the whole trace.
  Others narrow it — `before P,`, `after P,`, `while P,`, `between P and Q,`,
  `after P until Q,`.
- `it is never the case that …` and `if …, then in response …` are
  **specification patterns**: structured English that desugars to temporal logic
  (`G(!p)` and `G(p -> F(q))` here). The catalogue is on
  [Dwyer specification patterns in REF](specification-patterns.md).
- `@never_open_and_locked` names the requirement. Named requirements are
  reported **by name**, so verdicts survive edits that move line numbers.

## Your first trace

A trace is a CSV, one row per state. The first column is `__time__`; the rest
are the leaf fields of your `data` declarations, matched by name. Booleans are
written `true` / `false`.

**`__time__` is in nanoseconds.** The unit keywords in a requirement scale to
it, so `within 100 milliseconds` means 100,000,000 ticks of `__time__`. A
mismatch here is the most common reason a bounded requirement fails against a
trace that looks correct.

Put this in `door.csv`:

```text
__time__,button_pressed,door_open,locked
0,false,true,false
100000000,true,false,false
150000000,false,false,true
300000000,false,false,true
```

The door is open only while the lock is off, and the button goes down at 100 ms
with the lock engaging at 150 ms — 50 ms later, inside the bound.

**Every row must be complete.** An empty cell reads as the type's zero; it does
*not* carry forward from the row above, and it does so silently. If your logging
only emits a signal when it changes, expand it into full rows first.

## Run it

```bash
./build/referee execute door.ref door.csv
```

```text
never_open_and_locked                    PASS
locks_promptly                           PASS
```

The process exits `0` if every requirement held, `1` if any failed — usable in
CI as it stands:

```bash
./build/referee execute door.ref door.csv || echo "spec violated"
```

Requirements you did **not** name are reported by source position instead:

```text
5:0 .. 5:55                              PASS
7:0 .. 7:76                              PASS
```

## Make it fail

Tighten the bound past what the trace does:

```text
@locks_promptly
globally, if button_pressed, then in response locked within 10 milliseconds;
```

The lock takes 50 ms, so the verdict flips and the exit status goes to `1`:

```text
never_open_and_locked                    PASS
locks_promptly                           FAIL
```

To see *why*, ask for a run trace:

```bash
./build/referee execute door.ref door.csv --explain run.json
```

That emits NDJSON recording which states witnessed what — see
[Run traces](run-traces.md) and [Run-trace format](run-trace-format.md).

## The CSV column layout

The header is derived from the `data` declarations, in declaration order:

- the first column is **`__time__`**, the per-row timestamp, in nanoseconds;
- then one column per **leaf field** of every `data` declaration.

Composite types expand:

| Declaration | Columns |
| --- | --- |
| `data locked : boolean;` | `locked` |
| `data pos : struct { x: number; y: number; };` | `pos.x`, `pos.y` |
| `data limits : integer[3];` | `limits[0]`, `limits[1]`, `limits[2]` |
| `data g : integer[3][2];` | `g[0][0]`, `g[0][1]`, `g[1][0]`, … — outer dimension slowest |
| `data grid : Point[2];` | `grid[0].x`, `grid[0].y`, `grid[1].x`, … |

Values are written plainly: enums as the **bare member name** (`ON`, `OFF` — not
`State.ON`), booleans as `true`/`false` or `1`/`0`, numbers and strings
unquoted.

For a non-trivial schema the fastest way to get the header right is to let the
compiler tell you: start from a CSV with only `__time__` and one row, run
`referee execute`, and the column-not-found error names the next column it
expects. Repeat until it runs. The same expansion is performed by
`core/visitors/csvHeaders.cpp` if you would rather read the rules in code.

If your spec uses `conf` declarations — values constant for the whole run — pass
a one-row configuration CSV with the same column-naming rules:

```bash
./build/referee execute spec.ref data.csv --conf conf.csv
```

Omit it when the spec has `conf` declarations and the configuration is
zero-initialised silently, which is rarely what you want.

## Try the bundled fixtures

The repository ships working examples that the test suite keeps honest:

```bash
./build/referee execute test/logic/pass.ref test/logic/data.csv --conf test/logic/conf.csv
./build/referee execute test/logic/fail.ref test/logic/data.csv --conf test/logic/conf.csv
```

The first exits `0`; the second exits `1`, because several of its requirements
contradict the recorded data on purpose.

## The other modes

`referee execute` JIT-compiles and checks a stored trace. Its siblings:

```bash
./build/referee compile spec.ref        # emit the LLVM IR and stop
./build/referee monitor spec.ref        # check a trace as it streams, from stdin
./build/referee build    spec.ref -o c.o  # ahead-of-time native checker
./build/referee header   spec.ref -o c.h  # C header for the spec's types
```

`monitor` evaluates the same requirements incrementally rather than over a
stored log — see [Online monitoring](monitor.md). To deploy without the
toolchain present, [native checkers](native-checkers.md) emit a standalone
object.

## Where to go next

- [Requirements cookbook](cookbook.md) — worked requirements from real systems
- [Dwyer specification patterns in REF](specification-patterns.md) — the full
  pattern catalogue, scopes and time bounds
- [Temporal operators](temporal-operators.md) — `G`, `F`, `O`, `H`, until and
  since, bounded windows, accumulators
- [The REF language](language.md) — the complete surface syntax
- [Architecture](architecture.md) — what happens between `.ref` and machine code
