# Online monitoring — a live thermostat

`referee monitor` reads states one CSV row at a time from stdin and checks every
requirement as the trace unfolds, so an invariant violation is reported the
instant it happens instead of after the run. This example watches a thermostat
([`thermostat.ref`](thermostat.ref)): two safety invariants and one liveness
eventuality, chosen to show all three verdict shapes the monitor produces.

See [`docs/monitor.md`](../../docs/monitor.md) for the design and
[`docs/monitor-implementation.md`](../../docs/monitor-implementation.md) for how
it is built (this is the phase-1 evaluator).

## Watch it live

[`feed.sh`](feed.sh) emits one state per second, so the monitor's output appears
as each state arrives:

```bash
./examples/monitor/feed.sh | ./build/referee monitor examples/monitor/thermostat.ref
```

Speed it up with `DELAY`:

```bash
DELAY=0.3 ./examples/monitor/feed.sh | ./build/referee monitor examples/monitor/thermostat.ref
```

The scenario warms up to comfort, then leaves the heater on past 90 degrees and
lets the temperature run to 105. You will see:

```
__time__=3000  never_overheat=?  heater_off_hot=?  reaches_comfort=PASS
VIOLATION  heater_off_hot  @ __time__=5000  5000,90,true
__time__=5000  never_overheat=?  heater_off_hot=FAIL  reaches_comfort=PASS
VIOLATION  never_overheat  @ __time__=6000  6000,105,true
__time__=6000  never_overheat=FAIL  heater_off_hot=FAIL  reaches_comfort=PASS
```

- a **safety** requirement reads `?` while it holds and flips to `FAIL` the
  instant it breaks (with a `VIOLATION` line naming the offending state);
- a **liveness** requirement reads `?` until it is met, then settles `PASS` —
  it is never mistaken for a violation while merely unmet.

Run it in a real terminal and the verdicts are coloured (green/red); piped or
redirected, the output stays plain.

## Other ways to run it

Feed a fixed trace all at once (a clean run — everything passes):

```bash
./build/referee monitor examples/monitor/thermostat.ref < examples/monitor/nominal.csv
```

Halt at the first violation, for a supervisor stopping the system under test:

```bash
./examples/monitor/feed.sh | ./build/referee monitor examples/monitor/thermostat.ref --stop-at-first
```

Over a socket — the monitor speaks stdin/stdout, so bridge a socket to it with
`nc` (no extra code):

```bash
# terminal 1 — listen, and check whatever arrives
nc -l 9000 | ./build/referee monitor examples/monitor/thermostat.ref

# terminal 2 — push states from anywhere
./examples/monitor/feed.sh | nc localhost 9000
```

## The exit code

The monitor exits non-zero if any requirement failed by end of stream, so it
drops straight into a pipeline or a CI check.
