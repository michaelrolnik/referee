# External functions: a worked example

The MCTP PEC check, which is the case external functions exist for. It was
listed under *what this form cannot say* in `examples/mctp/packet-records.ref`
for as long as there was no way to write it: PEC is a CRC-8 over the SMBus
transaction, and REF has no fold over an array and no function abstraction, so
the only spelling in the language itself is the fully unrolled polynomial —
eight shift/xor steps per octet, across every octet.

## Run it

Three commands, from the repository root.

```bash
mkdir -p build/plugins

# 1. referee emits the header from the specification and a trace
./build/referee header examples/mctp/packet-records.ref \
                --like examples/mctp/packet-records.csv \
                -o build/mctp.h

# 2. you compile the implementation against it
cc -shared -fPIC -Wall -Wextra -Werror -I build \
   examples/mctp/pec.c -o build/plugins/libpec.so

# 3. referee finds it
./build/referee execute examples/mctp/packet-records.ref \
                examples/mctp/packet-records.csv -L build/plugins
```

The last line ends with

```text
smbus_pec_is_correct                     PASS
```

## What each piece looks like

The declaration and the call, in `packet-records.ref`:

```text
func crc8 : (byte[], integer) -> byte;

data pec_ok = crc8(pkt, len - 1) == pkt[len - 1];

@smbus_pec_is_correct
globally, it is always the case that pec_ok holds;
```

The array parameter is written **unsized**. Its extent travels in the
descriptor built at the call, so one declaration serves any trace shape — a
signature is the one place `T[]` does not need a trace to resolve it.

The call goes into a **computed signal**, not into the requirement. Both are
legal — a call is an ordinary expression and may appear anywhere one may — but
`__prepare__` evaluates a computed signal once per state, where the same call
inside a temporal operator is evaluated once per state *per loop iteration*.
The two spellings look equally cheap in the source, which is why it is worth
saying.

What `referee header` emits:

```c
typedef struct referee_slice_byte {
    size_t          count;
    const uint8_t*  data;
} referee_slice_byte;

uint8_t referee_crc8(referee_slice_byte arg0,
                     int64_t arg1);
```

And the implementation, in `examples/mctp/pec.c`:

```c
uint8_t referee_crc8(referee_slice_byte s, int64_t n)
{
    if (n < 0 || (size_t) n > s.count)
        return 0xFF;                    /*  refuse rather than read out of bounds  */
    ...
}
```

## Three things worth noticing

**The symbol is `referee_crc8`, not `crc8`.** The prefix keeps the
specification's namespace out of C's global one, so a `func` named `read`
looks for `referee_read` and cannot reach `read(2)`. It also means referee
inspects only `referee_*` when checking for duplicate definitions, so a
plugin's private helpers cannot collide with another plugin's.

**`count` is the extent, not the length.** `pkt` is padded to the run's
maximum frame size, so `s.count` is that maximum and the real length is passed
separately. What the descriptor buys is that the callee can *check* the length
it was handed rather than trust it — which the bounds test above does, and
which a bare pointer would have given it nothing to check against.

**It discriminates.** Flip a single bit anywhere in a frame and the check
fails, and nothing else does:

```text
smbus_pec_is_correct                     FAIL
```

A requirement that only ever passes is indistinguishable from one that is
vacuous, so this is the test that matters.

## Failure modes

All of them resolve before the first trace row is read.

| what | what referee says |
| --- | --- |
| no `-L` given | `declares 1 external function(s) but no .so was found` |
| symbol missing | `'referee_crc8' was not found in: …/libpec.so` |
| two objects define it | `defined more than once as 'referee_crc8': a.so, b.so` |
| no `func` declared | the path is never scanned; `-L` is inert |

## The gap this example does not close

If a type the signature depends on changes — a field added to a struct, a
member inserted into an enum — the compiled `.so` still has the old layout and
**nothing detects it**. The stale object keeps returning values that mean
something different, and the checker reports a confident wrong verdict:

```text
type Dir : enum { M2S, S2M };            # .so built here: M2S = 1
type Dir : enum { UNKNOWN, M2S, S2M };   # now 1 means UNKNOWN
```

Same `.so`, no error, `G(d.M2S)` silently goes from PASS to FAIL. Symbol
mangling — a structural hash of the signature in the symbol name — would turn
that into a resolution error naming both layouts. It is designed in
`docs/external-functions.md` and not yet built.
