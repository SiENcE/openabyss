/* SPDX-License-Identifier: MIT */
#include "uw_combat.h"

int uw_combat_cell(int vx, int vy) {
    /* action_combat, all seventy bytes of it:
     *
     *   combat_swing(x * 3 / (view_pixel_width  + 2)
     *              + (y * 3 / (view_pixel_height + 2)) * 3 + 1)
     *
     * The `+ 2` on each divisor is not a rounding trick. It is the work
     * buffer's per-row padding -- 174 x 113 = 19,662 bytes for a 172 x 113
     * viewport -- leaking into the hit test, and both divisors were read out
     * of a running game rather than assumed. A port that divides by the
     * visible width instead will mis-assign clicks in the last column.
     *
     * The cell is 1..9 and never 0, so 0 is free to mean "not in the view". */
    if (vx < 0 || vy < 0) return 0;
    if (vx >= UW_VIEW_PIX_W || vy >= UW_VIEW_PIX_H) return 0;
    int col = vx * 3 / (UW_VIEW_PIX_W + 2);
    int row = vy * 3 / (UW_VIEW_PIX_H + 2);
    return col + row * 3 + 1;
}

int uw_combat_cell_from_screen(int sx, int sy) {
    /* The viewport's y is recorded ENGINE-side, from the bottom of the
     * screen. Verified end to end: a click at guest (137, 76) predicts cell
     * 5, and the engine's own saved swing type read 5. */
    int engine_y = (UW_SCREEN_H - 1) - sy;
    return uw_combat_cell(sx - UW_VIEW_X0, engine_y - UW_VIEW_ENGINE_Y0);
}

int uw_combat_swing_kind(int cell) {
    /* combat_swing_kind -- ten bytes indexed by the cell, and
     * the three entries of each row are the same value. THE COLUMN IS
     * DISCARDED: only how high in the viewport you click picks bash, slash
     * or thrust. */
    static const int kind[10] = {-1, 2, 2, 2, 0, 0, 0, 1, 1, 1};
    if (cell < 1 || cell > 9) return -1;
    return kind[cell];
}

int uw_combat_attack_rating(const uw_player_combat *p, int weapon_skill) {
    int skill = weapon_skill;
    if (skill > 5 || skill < 2) skill = 2;
    int a = p->rec_21 / 2 + p->skill[skill] + p->rec_1f / 7;
    if (p->rec_b4) a += 7;
    return a;
}

int uw_combat_damage_rating(const uw_player_combat *p, const uw_weapon *w,
                            int kind, int strength) {
    int skill = w->skill;
    if (skill > 5 || skill < 2) skill = 2;     /* 2 = unarmed */
    if (skill == 2)
        /* Bare hands. Fists reach this branch because their +6 is 6, out of
         * range and clamped -- not because anything tests for fists. */
        return p->rec_23 * 2 / 5 + strength / 6 + 4;
    if (kind < 0 || kind > 2) kind = 0;
    return w->damage[kind] + strength / 9;
}

int uw_combat_apply_damage(uw_rng *r, int rating, int scale, int armour) {
    if (rating < 2) rating = 2;
    /* nd6 + 1d(rem). The remainder die has ZERO sides whenever the rating is
     * a multiple of six, and roll_dice returns its `count` unchanged when
     * either argument is non-positive -- so that term contributes 1, not 0.
     * See uw_rng.c; it is the kind of thing only writing the C finds. */
    int rolled = uw_roll_dice(r, rating / 6, 6)
               + uw_roll_dice(r, 1, rating % 6);
    int final = (rolled * scale) >> 7;
    final -= armour;
    return final < 0 ? 0 : final;
}
