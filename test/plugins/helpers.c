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

/*  Not an entry point: no prefix, so referee never looks at it. */
static uint8_t crc8_one(uint8_t crc, uint8_t byte)
{
    uint8_t c = crc ^ byte;
    for (int i = 0; i < 8; i++)
        c = (c & 0x80) ? (uint8_t)((c << 1) ^ 0x07) : (uint8_t)(c << 1);
    return c;
}

int64_t referee_triple(int64_t x)               { return x * 3; }
double  referee_half(double x)                  { return x / 2.0; }
bool    referee_is_even(int64_t x)              { return (x % 2) == 0; }

/*  A byte parameter is one octet: referee narrows to i8 at the call and
 *  widens the result back, so the C side sees plain uint8_t.  */
uint8_t referee_crc8_step(uint8_t crc, uint8_t byte) { return crc8_one(crc, byte); }
