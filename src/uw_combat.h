/* SPDX-License-Identifier: MIT */
/* The damage pipeline, click to hit points.
 *
 *   click in the 3D view
 *     -> hotspot record 7        -> view_action_dispatch
 *     -> action_handlers[2]      -> action_combat     3x3 cell over the view
 *     -> combat_swing               kind = combat_swing_kind[cell]; charge
 *     -> combat_select_weapon       the melee_weapon_props row (15 = fists)
 *     -> combat_damage_calc         attack rating, damage rating
 *     -> combat_attack_roll         against the target's defence
 *     -> combat_apply_damage        nd6 + 1d(rem), x scale/128, minus armour
 */
#ifndef UW_COMBAT_H
#define UW_COMBAT_H

#include "uw.h"

/* Hotspot record 7 -- the 3D view. x 52..223 and ENGINE y 68..180, engine y
 * being measured from the bottom of the screen. */
#define UW_VIEW_X0      52
#define UW_VIEW_PIX_W   172
#define UW_VIEW_PIX_H   113
#define UW_VIEW_ENGINE_Y0 68
#define UW_SCREEN_H     200

/* The player record fields combat_damage_calc reads, named by their
 * offsets. */
typedef struct {
    int  rec_1f;      /* +0x1f */
    int  rec_21;      /* +0x21, and the twenty skill levels that follow it */
    int  rec_23;
    int  skill[8];    /* rec[0x21 + skill] for skill 2..5 */
    bool rec_b4;      /* the +7 attack bonus */
} uw_player_combat;

/* A melee_weapon_props row. Bytes 0..2 are the per-kind damage, and what
 * establishes that is combat_damage_calc INDEXING THE ROW BY THE ATTACK
 * KIND -- not anything about the table's shape. */
typedef struct {
    int damage[3];
    int skill;        /* +6; outside 2..5 means unarmed, and fists are 6 */
} uw_weapon;

/* action_combat's quantisation, in VIEWPORT-RELATIVE pixels -- which is what
 * it receives, out of the pending event record. */
int uw_combat_cell(int view_x, int view_y);
/* ...and from a guest screen pixel, through the y-up conversion. */
int uw_combat_cell_from_screen(int screen_x, int screen_y);

/* combat_swing_kind[cell] -- ten bytes indexed 1..9. */
int uw_combat_swing_kind(int cell);

int uw_combat_attack_rating(const uw_player_combat *p, int weapon_skill);
int uw_combat_damage_rating(const uw_player_combat *p, const uw_weapon *w,
                            int kind, int strength);
/* combat_apply_damage. `scale` is combat_damage_scale, a
 * multiplier in 128ths: 90 uncharged, 140 fully charged. */
int uw_combat_apply_damage(uw_rng *r, int rating, int scale, int armour);

#endif
