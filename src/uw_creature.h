/* SPDX-License-Identifier: MIT */
/* Creature heading, and the rule for turning toward something.
 *
 * Two things in `creature_turn_toward` are easy to port wrongly
 * and hard to notice afterwards, because both wrong versions look right on
 * screen. They are the whole reason this file exists; the trigonometry around
 * them is ordinary and is not reproduced here.
 *
 *   * The heading is an 8-bit value in a 256-unit circle, stored SPLIT ACROSS
 *     TWO FIELDS -- the high three bits in `word1` bits 7..9 and the low five
 *     in byte `+0x18` bits 0..4 -- because neither field had five spare bits.
 *   * A creature turns a FIXED eighth of a turn per call and the caller keeps
 *     calling. Turning by a fraction of the error is the obvious modern
 *     choice and changes how long facing a target takes, which is what the
 *     AI's timing rests on.
 */
#ifndef UW_CREATURE_H
#define UW_CREATURE_H

#include "uw.h"

/* One eighth of a 256-unit turn: the most a creature moves in one call, and
 * also the half-width of the arc it counts as already facing. */
#define UW_TURN_STEP 0x20

/* THIS GAME HAS THREE ANGLE REPRESENTATIONS AND THEY ARE ALL THE SAME NUMBER.
 *
 * `player_heading` is a 16-bit angle in which **0x10000 is one
 * full turn**. Three slices of it are taken, and each is a "heading"
 * somewhere else in the engine:
 *
 *   angle >> 13   the 3-bit, eight-way heading in an object's word1 bits 7..9
 *   angle >> 8    the 8-bit heading `uw_heading_get` reassembles
 *   angle         passed straight to `gfx_sincos_lerp`, whose argument is 8.8
 *                 over a 256-unit circle -- which is the same bits again
 *
 * Three readings fix the scale, and no one of them alone would:
 * `player_step_or_turn` turns by `dir << 13` and calls that forty-five
 * degrees; walking backwards steps along `(player_heading + 0x8000) >> 8`, so
 * half a turn is `0x8000`; and it snaps with `& 0xe000` after testing
 * `& 0x1fff`, which is a test for being OFF an eighth boundary and not the
 * width of the circle. Reading that mask as the width is wrong for every
 * heading except 0 -- the one value at which both readings agree.
 */
#define UW_TURN_FULL 0x10000L          /* the 16-bit angle's full circle */
#define UW_TURN_EIGHTH 0x2000          /* ...and one eighth of it */

/* The eight-way heading an object stores, from the 16-bit angle. */
uint8_t uw_angle_to_heading8(uint16_t angle);

/* The two fields `uw_heading_get` reassembles, from the same angle. Together
 * they hold `(angle >> 8) & 0xff` -- the high three bits in `word1` and the
 * low five in byte `+0x18`, which is WHY the split exists: word1 already had
 * a three-bit heading field and the extra five bits of precision had to go
 * somewhere. */
void uw_angle_to_heading(uint16_t angle, uint16_t *word1, uint8_t *byte18);

/* The heading, reassembled from the two fields it is stored in. */
uint8_t uw_heading_get(uint16_t word1, uint8_t byte18);

/* ...and put back. Both arguments are updated in place; every other bit of
 * both fields is preserved, which is the point. */
void uw_heading_set(uint16_t *word1, uint8_t *byte18, uint8_t heading);

/* Turn `*heading` toward `want` by at most UW_TURN_STEP, and return true when
 * it was already within that -- in which case `*heading` is not moved.
 *
 * The difference is taken in 8 bits, so the wrap is free and a target at 250
 * is two steps from a heading of 2 rather than 248. */
bool uw_turn_toward(uint8_t *heading, uint8_t want);

#endif
