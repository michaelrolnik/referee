# References

The logics and formalisms REF draws on, with the primary sources for each. The
specification-pattern layer is the project's original inspiration; the temporal
operators beneath it come from the classical temporal-logic literature.

## Specification patterns (the inspiration)

REF's `globally` / `before` / `after` / `between … and …` scopes and its
absence / existence / universality / response / precedence patterns are the
Dwyer–Avrunin–Corbett *property specification patterns*.

- **M. B. Dwyer, G. S. Avrunin, J. C. Corbett.** *Patterns in Property
  Specifications for Finite-State Verification.* Proceedings of the 21st
  International Conference on Software Engineering (ICSE 1999), pp. 411–420.
  [PDF](https://www.cs.colostate.edu/~france/CS614/Readings/Readings2011/propPatterns1-p7-dwyer.pdf)
- The Specification Patterns System (the pattern catalogue, mappings to LTL,
  CTL and other formalisms): <https://matthewbdwyer.github.io/psp/>

## Linear Temporal Logic (LTL)

The future fragment — `G` (globally), `F` (eventually), `X` (next), `U`
(until), `R` (release) — is Pnueli's linear temporal logic.

- **A. Pnueli.** *The Temporal Logic of Programs.* 18th Annual Symposium on
  Foundations of Computer Science (FOCS 1977), pp. 46–57.
- **Z. Manna, A. Pnueli.** *The Temporal Logic of Reactive and Concurrent
  Systems: Specification.* Springer, 1992. The reference for the strong/weak
  distinction and the until/release and since/trigger dualities REF lowers.

## Past-time temporal logic

The mirror operators — `H` (historically), `O` (once), `Y` (previous / yester),
`S` (since), `T` (trigger) — are past-time LTL.

- **O. Lichtenstein, A. Pnueli, L. Zuck.** *The Glory of the Past.* Logics of
  Programs 1985, LNCS 193, pp. 196–218.

## Metric Temporal Logic (MTL)

The time-bounded operators, written `G[a:b]`, `F[a:b]`, `U[a:b]`, … over the
trace's `__time__`, are metric temporal logic.

- **R. Koymans.** *Specifying Real-Time Properties with Metric Temporal Logic.*
  Real-Time Systems 2(4), 1990, pp. 255–299.

## Timed Propositional Temporal Logic (TPTL) and freeze quantifiers

REF's freeze operator (`x @ …`, binding the current time/value for use deeper
in a formula) is the freeze quantifier of TPTL.

- **R. Alur, T. A. Henzinger.** *A Really Temporal Logic.* Journal of the ACM
  41(1), 1994, pp. 181–203.

## Finite-trace semantics (LTLf) and strong vs. weak

A trace is finite, so every operator has to decide what happens at the end of
it — which is the strong/weak split (`Xs`/`Xw`, `Us`/`Uw`, `Rs`/`Rw`, and the
past `Ys`/`Yw`, `Ss`/`Sw`, `Ts`/`Tw`). This is LTL over finite traces.

- **G. De Giacomo, M. Y. Vardi.** *Linear Temporal Logic and Linear Dynamic
  Logic on Finite Traces.* Proceedings of IJCAI 2013, pp. 854–860.

See also the README's *Temporal lowering* and *Semantics* sections for how
these operators are compiled to linear passes over a trace and the recurrences
that define each one.
