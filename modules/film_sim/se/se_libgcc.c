/*
 * se_libgcc.c - the one compiler helper Magic Lantern does not provide.
 *
 * Magic Lantern links modules with -nostdlib, so libgcc is not on the link
 * line; anything the compiler emits from libgcc has to come from Magic
 * Lantern's own exports or from here. The 6D firmware exports the __aeabi_*
 * arithmetic helpers, and we resolve against those, but it has no __powisf2
 * and soup-engine's grade.c reaches for it (gcc lowers a float base with an
 * integer exponent to this).
 *
 * The contract is libgcc's, so this is written to match it exactly: binary
 * exponentiation, then a reciprocal for a negative exponent. Kept out of the
 * engine directory so the vendored sources stay untouched upstream.
 */

#include "engine/se.h"

float __powisf2(float a, int b)
{
    unsigned e = (b < 0) ? (unsigned)-(long)b : (unsigned)b;
    float    r = 1.0f;

    while (e) {
        if (e & 1u) r *= a;
        a *= a;
        e >>= 1;
    }
    return (b < 0) ? 1.0f / r : r;
}
