/*
 *  Implementations behind the `func` declarations in test/logic/extfunc.ref.
 *
 *  Every entry point carries the `referee_` prefix: `func triple` in a .ref
 *  binds to `referee_triple` here. The prefix keeps the specification's
 *  namespace out of C's global one -- a `func` named `read` looks for
 *  `referee_read` and so cannot reach read(2) -- and it means referee inspects
 *  only `referee_*` when checking for duplicate definitions, so the private
 *  helper below does not collide with anything.
 */
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/*  Not an entry point: no prefix, so referee never looks at it. */
static uint8_t crc8_one(uint8_t crc, uint8_t byte)
{
    uint8_t c = crc ^ byte;
    for (int i = 0; i < 8; i++)
        c = (c & 0x80) ? (uint8_t)((c << 1) ^ 0x07) : (uint8_t)(c << 1);
    return c;
}

/*
 *  An array crosses the boundary as a descriptor. This is what
 *  `referee header` emits; declaring it by hand here keeps the test
 *  independent of build-time generation, and the static assertions pin the
 *  layout so the two cannot drift apart silently.
 */
typedef struct referee_slice_byte {
    size_t          count;
    const uint8_t*  data;
} referee_slice_byte;

_Static_assert(sizeof(referee_slice_byte) == 16, "descriptor is two words");
_Static_assert(offsetof(referee_slice_byte, count) == 0, "count first");
_Static_assert(offsetof(referee_slice_byte, data) == 8, "data second");

/*  SMBus PEC: CRC-8, polynomial 0x07, over the first n octets.
 *
 *  `count` is the array's extent, not the meaningful length, so the caller
 *  passes its own length -- and the descriptor is what makes it possible to
 *  check that length rather than trust it.
 */
uint8_t referee_crc8(referee_slice_byte s, int64_t n)
{
    if (n < 0 || (size_t)n > s.count)
        return 0xFF;

    uint8_t crc = 0;
    for (int64_t i = 0; i < n; i++)
        crc = crc8_one(crc, s.data[i]);

    return crc;
}

int64_t referee_capacity(referee_slice_byte s)  { return (int64_t)s.count; }
int64_t referee_first(referee_slice_byte s)     { return s.count ? s.data[0] : -1; }

int64_t referee_triple(int64_t x)               { return x * 3; }
double  referee_half(double x)                  { return x / 2.0; }
bool    referee_is_even(int64_t x)              { return (x % 2) == 0; }

/*  A byte parameter is one octet: referee narrows to i8 at the call and
 *  widens the result back, so the C side sees plain uint8_t.  */
uint8_t referee_crc8_step(uint8_t crc, uint8_t byte) { return crc8_one(crc, byte); }
