/* SPDX-License-Identifier: MIT */
#include "uw_door.h"

int uw_door_heading(uint16_t word1) {
    /* `and ax,0x380; shr ax,7; and al,3` -- bits 7..9, then the low two.
     *
     * Not bits 13..15: those are x-in-tile, which object_move_to_coords
     * writes as `obj[2] = (obj[2] & 0x1fff) | (x << 13)`. */
    return ((word1 >> 7) & 7) & 3;
}

bool uw_door_blocks(uw_move move, int heading, int x, int y) {
    /* 4 is the midpoint of an eight-unit tile. A corner turn enters through
     * one edge and leaves through another, so it cuts exactly one QUADRANT --
     * and it is blocked when the door stands in that quadrant.
     *
     * THE SECOND MISTAKE WAS HERE, and it is the one to guard against: the
     * corner rule was first written inverted -- "allowed only when the door
     * sits in the half the turn goes around" -- and nothing caught it,
     * because an inverted rule still explains why doors block. Only the
     * disassembly settled it. A door in that half is precisely what blocks. */
    const bool north = y >= 4, west = x < 4;

    switch (move) {
    case UW_MOVE_HORIZONTAL:
        /* `mov al,0; jmp 103a` returns blocked; falling through continues
         * the object-chain walk. So: blocked unless the door lies along x. */
        return heading != 0;
    case UW_MOVE_VERTICAL:
        return heading != 2;

    /* The four corners. Heading 0 -- a door along x -- is crossed by a turn
     * whose quadrant it stands in vertically; heading 2 horizontally. The
     * diagonals are unconditional, and which one blocks follows the same
     * rule: heading 1 is a panel on the NW-SE diagonal, heading 3 on SW-NE. */
    case UW_MOVE_WEST_NORTH:
        if (heading == 0) return north;
        if (heading == 2) return west;
        return heading == 1;
    case UW_MOVE_WEST_SOUTH:
        if (heading == 0) return !north;
        if (heading == 2) return west;
        return heading == 3;
    case UW_MOVE_EAST_NORTH:
        if (heading == 0) return north;
        if (heading == 2) return !west;
        return heading == 3;
    case UW_MOVE_EAST_SOUTH:
        if (heading == 0) return !north;
        if (heading == 2) return !west;
        return heading == 1;
    }
    return false;
}
