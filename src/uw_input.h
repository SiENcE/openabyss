/* SPDX-License-Identifier: MIT */
/* The analogue joystick, as arithmetic.
 *
 * A port replaces the *timing* half of the original's joystick code
 * outright: `joy_read_raw` fires four one-shots at port `0x201` and
 * counts loop iterations until each axis bit falls, because on a PC an
 * analogue axis value IS the time an RC pair takes to discharge. There is no
 * portable equivalent and no reason to want one.
 *
 * What a port does keep is the shape of the answer: the range, the trim, the
 * dead zone and where in the pipeline each is applied. A host that hands you
 * a float in [-1, 1] still has to land on the same numbers the rest of the
 * game was tuned against, and the two places this is easy to get wrong are
 * both here.
 *
 * The original is `joy_read_scaled`.
 */
#ifndef UW_INPUT_H
#define UW_INPUT_H

#include "uw.h"

/* One axis, scaled the way the original scales it.
 *
 *   raw     the reading, in whatever unit the timing loop produced
 *   centre  the same axis read at rest -- and this doubles as the
 *           axis-present flag: `joy_detect` leaves it ZERO for an axis whose
 *           timing fell outside 0x10..0x3e8, and the caller skips those. It
 *           is also the DIVISOR, so a zero here would be a division by zero
 *           that only the caller's gate prevents.
 *   trim    a calibration offset, subtracted after scaling
 *
 * Returns -0x7f..0x7f.
 */
int uw_joy_scale_axis(int raw, int centre, int trim);

/* The dead zone, on its own, because it is the part that surprises.
 *
 * It does not pass large values through untouched: every value is moved four
 * units TOWARD zero and stopped there. So 10 becomes 6, not 10. A port that
 * writes the usual `if (|v| < dz) v = 0;` gets the centre right and every
 * other reading wrong by four. */
int uw_joy_dead_zone(int v);

#endif
