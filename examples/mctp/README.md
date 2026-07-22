# Reasoning about MCTP over I2C: three trace shapes

The same protocol, specified three ways. What changes is not the requirements
but **what one trace record is**, and that choice decides which requirements
are expressible at all.

| | record = | file |
| --- | --- | --- |
| **A** | one MCTP packet | [`packet-records.ref`](packet-records.ref) |
| **B** | one octet on the wire | [`byte-stream.ref`](byte-stream.ref) |
| **C** | one MCTP message | [`message-records.ref`](message-records.ref) |

Each `.ref` runs against the `.csv` beside it:

```bash
referee execute examples/mctp/packet-records.ref  examples/mctp/packet-records.csv
referee execute examples/mctp/byte-stream.ref     examples/mctp/byte-stream.csv
referee execute examples/mctp/message-records.ref examples/mctp/message-records.csv
```

Every requirement in all three files was checked twice: that it holds against a
good trace, and that it **fails** against a trace with one defect injected.
A requirement that only ever passes is indistinguishable from one that is
vacuous, and two of the three files had a vacuous version before that check
caught it.

## What each form can say

| requirement | A packets | B octets | C messages |
| --- | :---: | :---: | :---: |
| header field values (`cmd == 0x0F`, version, EID) | ✅ | ✅ | ✅ |
| bit fields within an octet (`flags & 0x80`) | ✅ | ✅ | ✅ |
| padding discipline | ✅ | — | ✅ |
| SOM/EOM framing | ✅ | — | ✅ |
| sequence increments mod 4 | freeze + `Xs` | — | quantifier |
| exactly one SOM per message | — | — | ✅ |
| **total payload bytes of a message** | ❌ | ❌ | ✅ |
| reassembly timeout | ✅ | — | ❌ |
| inter-octet timing, NACK, clock stretch | ❌ | ✅ | ❌ |
| PEC / CRC-8 | ✅ via `func` | ✅ via `func` | ✅ via `func` |

The diagonal is the whole story: **the tighter the record, the more relations
become local and expressible — and the more timing is lost.** A message-shaped
record makes reassembly a quantifier and the payload total a sum, but there is
no longer an interval between SOM and EOM to put a timeout on.

## PEC

PEC is a CRC-8 over the transaction, which REF cannot express: there is no
fold over an array and no function abstraction, so the only spelling in the
language itself is the fully unrolled polynomial. It is now written as an
external function:

```text
func crc8 : (byte[], integer) -> byte;
data pec_ok = crc8(pkt, len - 1) == pkt[len - 1];
```

with the implementation in `pec.c`, loaded from the `-L` path. See
[`../extfunc/README.md`](../extfunc/README.md) for the three commands, and for
the one gap this leaves: if a type the signature depends on changes, the
compiled object still has the old layout and nothing detects it.

## Recommendation

**A + B together**, one run each, if the capture supports both:

```bash
referee execute mctp.ref --success good/*.csv --failure bad/*.csv
referee execute i2c.ref  --success good/*.csv --failure bad/*.csv
```

A carries the protocol requirements, B carries the electrical and timing ones.
Neither can do the other's job.

**C when messages, not packets, are the unit of interest** — when the questions
are about payload budgets, fragmentation correctness, or request/response
pairing between whole messages. C keeps the temporal layer intact and moves it
up a level, which is where message-to-message reasoning belongs.

**Not B alone.** A byte stream cannot frame itself. Measured on a trace whose
payload happens to contain a `0x0F`:

```text
Cnt(b == 0x0F)              == 4      # matches a payload octet too
Cnt(off == 1 && b == 0x0F)  == 3      # correct: three packets
```

So B needs an offset signal from the capture tool — which it must compute
anyway to recover START, STOP and the R/W bit.

If only one form can be captured, capture packets: a stream can always be
re-cut into packets by the tool, but packets cannot be un-cut back into a
stream with the timing intact.

## Language gaps these examples exposed

1. ~~Nested extents are not inferred.~~ **Fixed.** `struct { v : T[] }` was
   rejected because the extent table was keyed by declaration name; it is now
   keyed by path, so proposal C declares all three of its extents unsized.
2. **No numeric fold.** The quantifiers are all boolean, so the payload total
   in C is written one term per slot. A `sum x in xs:` form would fix the only
   ugly line in these files.
3. **Accumulators cannot be bounded by a terminator.** `Sum` and `Cnt` select
   states by condition and are bounded by a time window, so "bytes from SOM to
   EOM" is out of reach in A and B. This is what pushes toward C.
4. **Pattern bodies reject looping temporals.** `G`, `F`, `H`, `O`, the untils
   and releases, and the accumulators all raise "cannot be temporal" inside a
   Dwyer pattern. `Xs`/`Xw`/`Ys`/`Yw` and freeze are accepted, which is just
   enough for A's sequencing rules.
5. **`while` does not compose with a lookahead.** `while P, ... Xs(...)` fails
   where the equivalent `globally, it is always the case that P => Xs(...)`
   passes. Possibly a bug; worth a look.

## A trap worth knowing

A **bare** requirement is evaluated only at the first state. Written without a
`globally` scope, the whole of C passes against every broken trace here,
because the first record is a single-packet message where each multi-packet
guard is vacuously true. Every rule in these files is scoped deliberately.
