/* SPDX-License-Identifier: MIT */
#include "uw_input.h"

/* The joystick axis scaling, per axis:
 *
 *     ax = raw - centre
 *     imul 0x80 / idiv centre        -- 32-bit product, 16-bit quotient
 *     ax -= trim
 *     <dead zone>
 *     clamp to -0x7f..0x7f
 *
 * The multiply-then-divide is done in 32 bits by the hardware (`IMUL BX`
 * leaves DX:AX), so the intermediate does not wrap at 16 bits and the C must
 * not either. Ordering matters twice over: the trim comes off AFTER scaling,
 * and so does the dead zone -- four units of the SCALED range, not of the raw
 * timing, which is why a stick whose centre has drifted still rests at zero.
 */

int uw_joy_dead_zone(int v)
{
    if (v >= 0) {
        v -= 4;
        return v >= 0 ? v : 0;
    }
    v += 4;
    return v >= 0 ? 0 : v;
}

int uw_joy_scale_axis(int raw, int centre, int trim)
{
    int32_t v;

    if (centre == 0) {
        /* The original never reaches the divide with a zero centre -- the
         * caller tests the same cell first, and it is the same cell, not a
         * parallel flag array. Returning centred is the only defined thing
         * left to do if a port ever loses that ordering. */
        return 0;
    }
    v = ((int32_t)(raw - centre) * 0x80) / centre;
    v -= trim;
    v = uw_joy_dead_zone((int)v);
    if (v > 0x7f)  return 0x7f;
    if (v < -0x7f) return -0x7f;
    return (int)v;
}
