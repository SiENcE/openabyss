/* SPDX-License-Identifier: MIT */
/* The graphics module's arithmetic helpers: sine, cosine, arc tangent and a
 * square root.
 *
 * Small, and all of them are things a port gets subtly wrong if it reaches
 * for the C library instead. The original reaches them through the thunks
 * gfx_sincos, gfx_sincos_lerp, gfx_atan2_thunk and isqrt32.
 */
#ifndef UW_TRIG_H
#define UW_TRIG_H

#include "uw.h"

/* A quarter turn in 64 steps, so a full circle is 256 -- ONE table. Values are 1.15 fixed
 * point: 32767 is 1.0. The table ships in the graphics module's private
 * segment as ONE full-circle sine table of 256 entries plus 65
 * repeated; "the cosine table" is the same words a quarter turn
 * on. It is reproduced here verbatim -- NOT recomputed from sin(). A port
 * that substitutes the C library will be right to within a bit and will still
 * fail every differential test, because these exact values are what the rest
 * of the arithmetic is built on. Every angle's whole high byte is an index;
 * nothing masks it to a quadrant. */
#define UW_TRIG_STEPS 64

/* gfx_sincos(angle, &s, &c). Only the HIGH BYTE of `angle` is used. */
void uw_sincos(uint16_t angle, int16_t *sin_out, int16_t *cos_out);

/* gfx_sincos_lerp(angle, &s, &c). The angle is 8.8: the high byte indexes
 * the table and the low byte interpolates towards the next entry, as
 * `cur + ((next - cur) * frac) >> 8`. */
void uw_sincos_lerp(uint16_t angle, int16_t *sin_out, int16_t *cos_out);

/* isqrt32_newton. Newton-Raphson unrolled five times over a seed chosen by
 * magnitude, with no convergence test -- so it can sit one off the true
 * floor of the root, and reproducing that is the point. */
uint16_t uw_isqrt32(uint32_t n);

/* gfx_atan2(s, c), reached through gfx_atan2_thunk with s in AX
 * and c in BX: the angle whose sine and cosine are the 1.15 fractions s and
 * c, 0x10000 to the circle. Up to s = +-0x5a82 (sin 45) it reads the arc
 * sine of s and reflects for negative c; past that the arc cosine of c, a
 * half turn added for negative c and negated for negative s. Two 129-entry
 * tables in the graphics module's segment, interpolated. */
int16_t uw_atan2(int16_t s, int16_t c);

int16_t uw_trig_sin_table(int i);
int16_t uw_trig_cos_table(int i);

#endif
