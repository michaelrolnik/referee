# Design: trace expectations

**Status:** implemented — whole-trace and per-requirement expectations, multi-trace checking, named requirements and the suite manifest.
**Scope:** `referee execute spec.ref --success good1.csv good2.csv --failure bad1.csv bad2.csv`, exiting 0 when every trace behaved as declared.

## The problem

A specification is only as good as its ability to *reject* things. A requirement set that passes every trace it is shown may be correct, or may be vacuous — a requirement with a typo that makes it trivially true passes exactly as convincingly as one that holds.

The usual defence is a corpus of traces that are known to violate the requirements, checked alongside the ones that are known to satisfy them. That works today only by inverting the exit code per invocation:

```bash
referee execute spec.ref good.csv   || exit 1
! referee execute spec.ref bad.csv  || exit 1      # must fail
```

which is awkward, and gets worse with every trace added.

The project already relies on this idea internally. `test/logic/pass.ref` exists to pass and `fail.ref` exists to fail, and the harness asserts each:

```cpp
EXPECT_TRUE (allPass);      // Rdb.ExecuteRdbPass
EXPECT_FALSE(allPass);      // Rdb.ExecuteRdbFail
```

The concept is established; the CLI simply does not expose it.

## Shape

```bash
referee execute spec.ref --success good1.csv good2.rdb --failure bad1.csv bad2.yml
```

Both options take one or more traces, in any mixture of `.rdb`, `.csv` and `.yml`. The exit code is 0 when every trace behaved as declared, and non-zero otherwise.

| declared | observed | verdict |
| --- | --- | --- |
| `--success` | all requirements hold | ok |
| `--success` | some requirement violated | **failure** |
| `--failure` | some requirement violated | ok |
| `--failure` | all requirements hold | **failure — unexpected pass** |

The fourth row is the one that earns the feature. A trace that was supposed to be rejected and was not means the specification has stopped catching the thing that trace exists to demonstrate — usually because a requirement was weakened or a signal renamed. Nothing else in the tool notices that, because from the requirements' point of view everything is fine.

### Why this needs multi-trace first

For a single trace the feature is redundant — `!` in a shell already inverts the verdict. The value appears only when one invocation covers a mixed set and produces a single answer, which means it depends on `referee execute` accepting several traces (see `docs/native-checkers.md`, where the measurements make multi-trace the first thing to build regardless).

The two should be designed together: multi-trace settles how a per-trace report is formatted, and expectations add a per-trace verdict to it.

### CLI details worth settling

`execute` currently takes two required positionals, `reffile` and `datafile`. With expectations the trace positional has to become optional, and the sensible rule is that the flags and the positional are mutually exclusive — either all traces are declared, or none are:

```bash
referee execute spec.ref trace.csv                    # as today, no expectations
referee execute spec.ref --success a.csv --failure b.csv
referee execute spec.ref trace.csv --failure b.csv    # rejected: mixed forms
```

CLI11 stops a multi-value option at the next flag, so `--success a b --failure c` parses as intended without a terminator.

`--failure` on its own is legitimate and useful — a corpus consisting entirely of traces that must be rejected.

## Granularity: the part that matters

Whole-trace expectation is weak, and it is worth being explicit about why before building only that.

`--failure bad.csv` is satisfied when the trace violates **anything**. It passes when the trace violates the requirement it was written to demonstrate; it also passes when the trace violates something else entirely — a mistyped column, a timestamp out of order, an unrelated requirement that has since been added. The check goes green while the thing it was protecting has quietly stopped being tested.

The stronger form declares *which* requirement a trace is expected to violate:

```
bad/stuck-valve.csv    fails  door.ref:12:0 .. 12:30
bad/late-alarm.csv     fails  alarm.ref:8:0 .. 8:24, alarm.ref:9:0 .. 9:31
good/nominal.rdb       passes
```

This is feasible because **requirement labels are already stable and file-qualified**. They are `[file:]row:col .. row:col`, the file part relative to the root `.ref`, and they are already the identity used for the LLVM function and the report line. Referring to one from outside is well defined.

It is not free, though: labels move when the specification is edited. A requirement that gains a line above it changes label, and every manifest entry naming it has to change too. That is the same brittleness any line-based reference has, and it is the argument for a third option — naming requirements explicitly:

```text
@id door-closes-within-2s
globally, if door.OPENED, then in response door.CLOSED within 2 seconds;
```

which is what was built. It is spelled with a bare `@` rather than a keyword: reserving a word like `id` would have taken it away as a signal name, and `data id : integer;` is a plausible declaration. There is no ambiguity with the freeze operator, which needs an identifier to its left and so cannot begin a statement. Names must be unique across the whole program, imports included, since a name replaces the source position as the requirement's identity.

### What was built

Named requirements first, then expectations keyed on the names, so the corpus never referred to a line number. A trace that fails for a requirement other than the one it names is reported as `WRONG REQUIREMENT` and fails the run — which is the case a bare `fails` would have called fine.

## Report

The report gains a per-trace verdict, and a summary. The shape matters because CI parses it:

```text
good1.csv          expected pass   PASS      ok
bad1.csv           expected fail   FAIL      ok
                     alarm.ref:8:0 .. 8:24   FAIL
bad2.yml           expected fail   PASS      UNEXPECTED PASS

3 traces: 2 ok, 1 unexpected pass
```

The per-requirement lines stay as they are today so existing output remains recognisable; what is added is the declared expectation, the observed outcome and the verdict. An unexpected pass should be visually distinct from an ordinary failure, because it means something different: not "the system misbehaved" but "the specification no longer notices".

## Interaction with compiled checkers

A checker produced by `referee build` (see `docs/native-checkers.md`) should take the same flags and produce the same report. The expectation logic lives entirely in the driver — it compares a boolean against a declaration and never touches the compiled requirements — so it costs the runtime nothing and should not be a reason to keep the two paths apart.

## Open questions

1. **Should a manifest file be supported from the start?** A corpus of any size outgrows a command line quickly, and CI would rather commit a file than a shell invocation. The counter-argument is that a manifest is the natural home for per-requirement expectations, so introducing it early risks fixing its format before the harder half is designed.
2. **What does an ingestion error count as?** A `--failure` trace that cannot be parsed at all has not demonstrated anything about the specification, and treating a malformed file as a satisfied expectation would hide real breakage. It should be a hard error regardless of the declaration.
3. **Should `--success` be the default for bare positionals?** `referee execute spec.ref a.csv b.csv` reads naturally as "all of these should pass", which would make the positional form a shorthand rather than a separate mode, and remove the mutual-exclusion rule above.
