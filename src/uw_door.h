/* SPDX-License-Identifier: MIT */
/* Does a closed door block this move?
 *
 * The whole rule is one sentence -- **a closed door blocks exactly the moves
 * that would cross its panel** -- and no case in the switch is special, the
 * diagonal doors included.
 *
 * The original is a six-way jump table whose handlers are reached only
 * through the indirect jump.
 */
#ifndef UW_DOOR_H
#define UW_DOOR_H

#include "uw.h"

/* Which pair of tile edges the move runs between. The names are the ones the
 * handler table is organised by; the numbers are the switch's own indices. */
typedef enum {
    UW_MOVE_HORIZONTAL,   /* 1, 4  -- straight, west <-> east   */
    UW_MOVE_VERTICAL,     /* 7, 10 -- straight, north <-> south */
    UW_MOVE_WEST_NORTH,   /* 0, 9  -- the NW quadrant */
    UW_MOVE_WEST_SOUTH,   /* 2, 6  -- SW */
    UW_MOVE_EAST_NORTH,   /* 3, 11 -- NE */
    UW_MOVE_EAST_SOUTH,   /* 5, 8  -- SE */
} uw_move;

/* `heading` is the door object's word-1 bits 7..9 taken `& 3`, which is what
 * the handlers use: it collapses the eight-way heading to which axis the door
 * lies on, 0 and 2, plus the two diagonals 1 and 3. `x` and `y` are the
 * door's position within its tile, 0..7. */
bool uw_door_blocks(uw_move move, int heading, int x, int y);

/* The `& 3` the handlers apply, spelled out. */
int uw_door_heading(uint16_t word1);

#endif
