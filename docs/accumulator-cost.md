# Accumulators are quadratic under a temporal scope

**Status:** open, measured, not fixed. A performance defect in shipped code, not a missing feature.
**Found:** by asking whether the O(N²) cost identified for run traces also applied to the compiled path. It does, for one family of operators.

## The measurement

Two requirements over the same traces, one using `Us` and one using `Cnt`:

```text
G(Us(a, !a) || true);        G(Cnt(a) >= 0);
```

| N | `Us` under `G` | `Cnt` under `G` |
| ---: | ---: | ---: |
| 20 000 | 0.24 s | 0.45 s |
| 40 000 | 0.47 s | 1.16 s |
| 80 000 | 0.90 s | 4.36 s |

`Us` doubles with N. `Cnt` roughly quadruples — it is O(N²).

Smaller traces hide this: below about N = 8000 the fixed compilation cost
(~50 ms here, ~700 ms for a large specification) dominates and both look
linear. The corpus use case is where it bites, and that is precisely where
`Cnt` over a message is the natural thing to write.

## The cause

`isLoopTemporal` in `core/visitors/compile.cpp` decides which operators get
the O(N) recurrence:

```cpp
return dynamic_cast<ExprUs*>(expr) || dynamic_cast<ExprUw*>(expr) ||
       dynamic_cast<ExprRs*>(expr) || dynamic_cast<ExprRw*>(expr) ||
       dynamic_cast<ExprSs*>(expr) || dynamic_cast<ExprSw*>(expr) ||
       dynamic_cast<ExprTs*>(expr) || dynamic_cast<ExprTw*>(expr);
```

Until, release, since and triggered are there. `ExprSum` and `ExprInt` are
not — `Cnt` desugars to `ExprSum`, so all three accumulators are affected.

Each accumulator evaluation therefore walks forward from its own state to the
end of the trace or the end of its window. Under `G`, that is N walks of
length N. The walk itself is correct and about as tight as it can be; there is
simply one per state.

This was never a decision. The accumulators were added after the linear
lowering existed and were not considered for it.

## The fix

The same recurrence the boolean operators use, with an additive accumulator:

```text
Sum(c, v)[s]  =  (c[s] ? v[s] : 0) + Sum(c, v)[s+1]
Cnt(c)[s]     =  (c[s] ? 1 : 0)    + Cnt(c)[s+1]
```

which is exactly the shape of

```text
Us(p, q)[s]   =  q[s] || (p[s] && Us(p, q)[s+1])
```

One backward pass, O(N) total. At N = 80 000 that is roughly 4.36 s → 0.9 s,
and the gap widens with every doubling.

Adding `ExprSum` to `isLoopTemporal` is the first line of the change and not
the whole of it: `compileTemporalLoopInline` builds a boolean recurrence with
`select` over `i1`. An accumulator needs an `i64` or `double` carrier and an
`add`, so it is a parallel implementation in that function rather than an
extra case in the existing one — which is why this is written down rather than
attempted at the end of a long session.

### The windowed form is harder

`Sum[lo:hi](c, v)` is a sliding window, not a suffix, so a single backward
fold does not give it. That is the monotone-pointer technique
`compileTemporalLoopBounded` already uses for bounded operators: both window
ends advance monotonically across the trace, so the total work is amortised
O(N). The machinery exists; it has not been applied here.

`Itg` is the same shape again, weighted by each step's duration.

## Before rewriting: the boundaries are pinned

A recurrence is got wrong at its *ends*, not in its middle. `test/logic/accumulate.ref`
now asserts those ends against the current forward walk, so a rewrite has
something to preserve rather than something to hope for:

* the **last state**, where the suffix is the state itself — the seed;
* the **first state**, where the fold must have run the whole trace;
* **one state in**, asserting the recurrence directly: the fold at *s* equals
  the fold at *s+1* plus this state's contribution;
* a condition **false everywhere**, which must fold to the identity and not to
  a stale carrier;
* a window **one unit wide**, which must contain exactly the current state,
  and a window of **zero width**, which must contain nothing.

All of them hold today. If the linear version breaks one, it is wrong at an
end, which is where it would otherwise be wrong silently — the fixtures with
mid-trace values would not notice.

## Why it matters beyond speed

A quadratic checker discourages exactly the requirements worth writing. Faced
with a slow `Cnt` over a long capture, the reachable workarounds are to shorten
the trace or narrow the window — both of which reduce what the specification
actually checks, and neither of which looks like a compromise in the source.
The requirement still passes; it simply covers less.

That is the same failure this project keeps meeting from a different
direction: a check that quietly stops checking, while still reporting green.
