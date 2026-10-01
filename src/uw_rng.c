/* SPDX-License-Identifier: MIT */
/* rt_rand / rt_srand -- Borland Turbo C's LCG. The program never seeds it.
 */
#include "uw.h"

#define MUL 0x015A4E35u
#define ADD 1u

void uw_rng_init(uw_rng *r) {
    /* 1 in the shipped data segment -- the value the C standard says rand()
     * behaves as if srand(1) gave it.
     * Nothing in the program changes this before the first draw. */
    r->state = 1u;
}

void uw_srand(uw_rng *r, uint16_t seed) {
    /* rt_srand writes the low half and ZEROES the high half, so the reachable
     * seed space is 65,536 states rather than 2^32. Barter relies on that: it
     * seeds with an object index to get a jitter that is stable per object. */
    r->state = seed;
}

int uw_rand(uw_rng *r) {
    r->state = r->state * MUL + ADD;
    return (int)((r->state >> 16) & 0x7FFFu);
}

long uw_rng_locate(uint32_t state, long limit) {
    uint32_t s = 1u;
    for (long i = 1; i <= limit; i++) {
        s = s * MUL + ADD;
        if (s == state) return i;
    }
    return -1;
}

int uw_roll_dice(uw_rng *r, int count, int sides) {
    /* roll_dice(count, sides) -- fourteen callers.
     *
     * NOT `count * (rand() % sides + 1)`, which is what a reader who knows
     * what NdS means will write. The body multiplies and scales:
     *
     *     result = count
     *     if (sides > 0 && count > 0)
     *         repeat count times:  result += (rt_rand() * sides) / 0x8000
     *
     * so each die contributes a value in [0, sides) drawn by scaling the
     * generator's full 15-bit range, and the `count` it starts from is what
     * makes the total run count..count*sides. The distributions differ --
     * modulo would bias low for any `sides` that is not a power of two --
     * and, since the whole point of this port is differential testing
     * against a generator that is reproducible, so would every value.
     *
     * The non-positive case is a RETURN, not a clamp: `roll_dice(1, 0)`
     * gives 1. That matters, because combat_apply_damage rolls
     * `1d(rating % 6)` and every rating that is a multiple of six asks for a
     * zero-sided die -- a port that clamped `sides` to 1 would agree, and a
     * port that returned 0 would be one point light on one rating in six.
     */
    int result = count;
    if (sides <= 0 || count <= 0) return result;
    for (int i = 0; i < count; i++)
        result += (int)(((int32_t)uw_rand(r) * sides) / 0x8000);
    return result;
}
