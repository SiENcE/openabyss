/* SPDX-License-Identifier: MIT */
#include "uw_creature.h"

/* creature_turn_toward's reassembly:
 *
 *     (char)((word1 & 0x380) >> 7) * 0x20 + (byte18 & 0x1f)
 *
 * The multiply by 0x20 is a shift left by five, so the three bits land above
 * the five. Written as shifts here because the original's `* ' '` is a
 * compiler artefact of the same thing.
 */
uint8_t uw_heading_get(uint16_t word1, uint8_t byte18)
{
    return (uint8_t)((((word1 & 0x0380u) >> 7) << 5) | (byte18 & 0x1fu));
}

void uw_heading_set(uint16_t *word1, uint8_t *byte18, uint8_t heading)
{
    *word1 = (uint16_t)((*word1 & 0xfc7fu) | ((uint16_t)(heading >> 5) << 7));
    *byte18 = (uint8_t)((*byte18 & 0xe0u) | (heading & 0x1fu));
}

bool uw_turn_toward(uint8_t *heading, uint8_t want)
{
    /* IN EIGHT BITS. The original does `sub al,bl` on bytes and then tests
     * `< 0x20 || > 0xe0`, which is "within an eighth of a turn either way"
     * with the wrap already handled by the truncation. Doing this in `int`
     * and comparing against +/-32 needs an explicit wrap and gets the
     * antipodal cases wrong when it is forgotten. */
    uint8_t delta = (uint8_t)(want - *heading);

    /* within an eighth the heading is the wanted one (creature_turn_toward
     * writes it), and the turn is over */
    if (delta < UW_TURN_STEP || delta > (uint8_t)(0x100 - UW_TURN_STEP)) {
        *heading = want;
        return true;
    }
    *heading = (uint8_t)(*heading +
                         (delta < 0x80 ? UW_TURN_STEP : (uint8_t)-UW_TURN_STEP));
    return false;
}

uint8_t uw_angle_to_heading8(uint16_t angle)
{
    return (uint8_t)((angle >> 13) & 7u);
}

void uw_angle_to_heading(uint16_t angle, uint16_t *word1, uint8_t *byte18)
{
    /* the original's, verbatim:
     *     obj[2]    = (obj[2] & 0xfc7f) | ((angle >> 0xd) & 7) << 7
     *     obj[0x18] = (obj[0x18] & 0xe0) | (angle >> 8) & 0x1f
     * Two writes of two slices of one angle, and every other bit preserved. */
    *word1 = (uint16_t)((*word1 & 0xfc7fu)
                        | (uint16_t)(((angle >> 13) & 7u) << 7));
    *byte18 = (uint8_t)((*byte18 & 0xe0u) | ((angle >> 8) & 0x1fu));
}
