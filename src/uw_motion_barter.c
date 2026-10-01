/* SPDX-License-Identifier: MIT */
/* THE TRADE TABLE'S MOUSE: the click path -- barter_click_npc_slot
 * and barter_click_player_slot, the hotspots of mode 4
 * over the two rows of four slots; barter_npc_slot_at,
 * barter_player_slot_at and barter_slot_at_either over the two
 * coordinate tables; barter_slot_click, what a
 * click does; barter_lift_from_slot, barter_drop_into_slot,
 * barter_slot_combine; barter_draw_slot and
 * barter_draw_slot_frame.
 *
 * THE WAITS. barter_slot_click stands in cursor_wait_for_drag, then in a
 * release wait after the lift and another before a placing; each is a pass
 * loop inside the original's click. The port leaves them to the shell as the
 * paperdoll's are left: `drag_wait` with `barter_wait` 1 is the drag wait,
 * which the shell ends with uw_motion_barter_drag (the cursor moved six) or
 * uw_motion_barter_click_end (the button up); a release wait begun with
 * `barter_wait` 2 or 3 is ended by the hotspot's `after` call
 * (uw_motion_barter_wait_end). A wait a rest begins with the button already
 * up ends at its first poll, and the rest runs on here without returning.
 * A stack's "Move how many? " is the shell's (stack_ask's path 3), answered
 * into barter_lift_take. */
#include "uw_motion_int.h"

#include <stdio.h>

enum {
    BT_NPC_SLOTS    = 0x485e,   /* barter_npc_slots: four object indices */
    BT_PLAYER_SLOTS = 0x486a,   /* barter_player_slots */
    BT_DEAL_DONE    = 0x4872,   /* barter_deal_done: the NPC's goods may be taken */
    BT_NPC_FLAGS    = 0x487a,   /* a byte per slot: offered */
    BT_PLAYER_FLAGS = 0x487e,
    BT_NPC_BG       = 0x489e,   /* imgbuf handles of the slots' backgrounds (barter_setup) */
    BT_CACHE_A      = 0x48a6,   /* the two cached words a slot's change resets to -1 */
    BT_CACHE_B      = 0x48ae,
    BT_PLAYER_BG    = 0x48b6,
    BT_PLAYER_XY    = 0x0d54,   /* (x, bottom y) of each slot's 16 x 16 icon */
    BT_PLAYER_MARK  = 0x0d64,   /* the offered mark's centre */
    BT_NPC_XY       = 0x0d74,
    BT_NPC_MARK     = 0x0d84,
};

static uint16_t slots_of(int whose) { return whose ? BT_PLAYER_SLOTS : BT_NPC_SLOTS; }
static uint16_t flags_of(int whose) { return whose ? BT_PLAYER_FLAGS : BT_NPC_FLAGS; }

/* barter_player_slot_at / barter_npc_slot_at (x, y): the slot whose icon
 * covers the point -- slot.x .. slot.x + 16 by slot.y - 16 .. slot.y, the
 * stored y the bottom edge -- or -1. */
int16_t barter_slot_at(uw_motion *m, int whose, int16_t x, int16_t y) {
    uint8_t *ds = m->ds;
    uint16_t table = whose ? BT_PLAYER_XY : BT_NPC_XY;
    int i;
    for (i = 0; i < 4; i++) {
        int16_t sx = rs(ds, (uint16_t)(table + i * 4)), sy = rs(ds, (uint16_t)(table + i * 4 + 2));
        if (x < sx || sx + 0x10 < x || sy < y || y < sy - 0x10) continue;
        return (int16_t)i;
    }
    return -1;
}

/* barter_draw_slot_frame(whose, slot): a plus of five pixels at
 * the slot's mark, colour 0x60 when the slot is offered, 0xf1 otherwise,
 * under cursor_hide/cursor_show. */
void uw_motion_barter_draw_slot_frame(uw_motion *m, int whose, int slot) {
    uint8_t *ds = m->ds;
    uint16_t at = (uint16_t)((whose ? BT_PLAYER_MARK : BT_NPC_MARK) + slot * 4);
    int x = rs(ds, at), y = rs(ds, (uint16_t)(at + 2));
    uint8_t c = ds[(uint16_t)(flags_of(whose) + slot)] == 1 ? 0x60 : 0xf1;
    cursor_hide(m);
    uw_motion_fill_rect(m, x, y, x, y, c);
    uw_motion_fill_rect(m, x - 1, y, x - 1, y, c);
    uw_motion_fill_rect(m, x + 1, y, x + 1, y, c);
    uw_motion_fill_rect(m, x, y - 1, x, y - 1, c);
    uw_motion_fill_rect(m, x, y + 1, x, y + 1, c);
    cursor_show(m);
}

/* barter_draw_slot(whose, slot): the slot's background put back
 * from barter_setup's buffer and, with an object in it, its art drawn at
 * the slot's place with gfx_span_variant set (colour 0 skipped) and, for a
 * stack of more than one, its count in font4x5p.sys, colour 0x60, at the
 * slot's x + 3 and y - 1, font5x6p.sys reopened after;
 * under cursor_hide/cursor_show. The variant goes back to 0 only on the
 * object's path: an empty slot jumps to the cursor_show and
 * leaves it set. */
void uw_motion_barter_draw_slot(uw_motion *m, int whose, int slot) {
    uint8_t *ds = m->ds, *ls = m->lseg;
    uint16_t idx = rw(ds, (uint16_t)(slots_of(whose) + slot * 2)), o = 0, q = 0;
    uint16_t xy = (uint16_t)((whose ? BT_PLAYER_XY : BT_NPC_XY) + slot * 4);
    cursor_hide(m);
    m->span_variant = 1;
    imgbuf_restore(m, rw(ds, (uint16_t)((whose ? BT_PLAYER_BG : BT_NPC_BG) + slot * 2)));
    if (idx) {
        o = obj_at(m, idx);
        uw_motion_gr_draw_art(m, (uint16_t)(rw(ls, o) & 0x1ff), rs(ds, xy), rs(ds, (uint16_t)(xy + 2)));
        if ((rw(ls, o) & 0x8000) && !((rw(ls, (uint16_t)(o + 6)) >> 6) & 0x200))
            q = (uint16_t)(rw(ls, (uint16_t)(o + 6)) >> 6);
        m->span_variant = 0;
    }
    if (q > 1) {
        char digits[8];
        snprintf(digits, sizeof digits, "%u", q);
        ds[FONT_LOADED] = 1;
        uw_motion_draw_string(m, m->font_small, m->font_small_size, digits,
                              (int16_t)(rs(ds, xy) + 3), (int16_t)(rs(ds, (uint16_t)(xy + 2)) - 1), 0x60);
    }
    cursor_show(m);
}

/* barter_lift_from_slot(slot, slots, keep_rest): the slot's
 * object onto the cursor as held_object_ptr, the slot emptied -- or, with
 * keep_rest, given the next of the object's chain -- and the cursor's shape
 * the object's art, the shape held before popped first. */
static void barter_lift_from_slot(uw_motion *m, int slot, uint16_t slots, int keep_rest) {
    uint8_t *ds = m->ds, *ls = m->lseg;
    int was = rw(ds, 0x5b06) || rw(ds, 0x5b08);
    uint16_t idx = rw(ds, (uint16_t)(slots + slot * 2)), o = idx ? obj_at(m, idx) : 0;
    ww(ds, 0x5b06, o);
    ww(ds, 0x5b08, o ? rw(ds, (uint16_t)(MOBILE_BASE + 2)) : 0);
    ww(ds, (uint16_t)(slots + slot * 2), 0);
    if (!o) return;
    if (keep_rest) ww(ds, (uint16_t)(slots + slot * 2), obj_index_of(m, deref_link(m, (uint16_t)(o + 4))));
    cursor_hide(m);
    if (was) cursor_shape_pop(m, 0);
    cursor_shape_push(m, (uint16_t)(rw(ls, o) & 0x1ff));
    cursor_show(m);
}

/* barter_slot_combine(held, whose, slot, slots): a drop onto an
 * occupied slot. A container (kind 2, no special) is refused, nothing moved.
 * Two stacks of one item merge -- both quantities, neither a link special,
 * the total under 999 -- the held one freed; anything else swaps, the
 * occupant lifted onto the cursor and the held index written in. 1 when
 * merged, which is when the cursor is empty after. */
static int barter_slot_combine(uw_motion *m, uint16_t held, int whose, int slot, uint16_t slots) {
    uint8_t *ds = m->ds, *ls = m->lseg;
    uint16_t o = obj_at(m, rw(ds, (uint16_t)(slots + slot * 2)));
    uint16_t w0 = rw(ls, o), h0 = rw(ls, held);
    uint16_t qo = (uint16_t)(rw(ls, (uint16_t)(o + 6)) >> 6), qh = (uint16_t)(rw(ls, (uint16_t)(held + 6)) >> 6);
    int merged = 0;
    if ((w0 & 0x1c0) == 0x80 && !(w0 & 0x30)) return 0;
    if ((h0 & 0x8000) && (w0 & 0x8000) && !(qh & 0x200) && !(qo & 0x200)
        && (h0 & 0x1ff) == (w0 & 0x1ff) && qh + qo < 999) {
        ww(ls, (uint16_t)(o + 6), (uint16_t)((rw(ls, (uint16_t)(o + 6)) & 0x3f) | ((qo + qh) << 6)));
        uw_motion_obj_free(m, held);
        merged = 1;
    } else {
        uint16_t idx = obj_index_of(m, held);
        barter_lift_from_slot(m, slot, slots, 0);
        ww(ds, (uint16_t)(slots + slot * 2), idx);
    }
    uw_motion_barter_draw_slot(m, whose, slot);
    return merged;
}

/* barter_drop_into_slot(whose, slot, slots): an empty slot takes
 * the held object's index; an occupied one combines, and a swap leaves the
 * cursor loaded with the occupant. */
static void barter_drop_into_slot(uw_motion *m, int whose, int slot, uint16_t slots) {
    uint8_t *ds = m->ds;
    if (!rw(ds, (uint16_t)(slots + slot * 2))) {
        ww(ds, (uint16_t)(slots + slot * 2), obj_index_of(m, rw(ds, 0x5b06)));
    } else if (!barter_slot_combine(m, rw(ds, 0x5b06), whose, slot, slots)) {
        return;
    }
    ww(ds, 0x5b06, 0);
    ww(ds, 0x5b08, 0);
}

/* barter_slot_click's end: the cursor loaded at entry or by the lift and
 * empty now, its shape popped and action_state cleared. */
static void barter_finish(uw_motion *m) {
    uint8_t *ds = m->ds;
    if (m->barter_loaded && !(rw(ds, 0x5b06) || rw(ds, 0x5b08))) {
        cursor_shape_pop(m, 3);
        ww(ds, ACTION_STATE_WORD, 0);
    }
    m->barter_loaded = 0;
    m->barter_wait = 0;
}

/* The PLACING branch past its release wait: action_state 1; the NPC's side
 * refused (the thing stays on the cursor, and the click ends there); else
 * the drop, the slot redrawn, offered, its mark drawn and its cached words
 * reset. */
static void barter_place_end(uw_motion *m) {
    uint8_t *ds = m->ds;
    int whose = m->barter_whose, slot = m->barter_slot;
    m->barter_wait = 0;
    ww(ds, ACTION_STATE_WORD, 1);
    if (whose == 0) { m->barter_loaded = 0; return; }
    barter_drop_into_slot(m, whose, slot, slots_of(whose));
    uw_motion_barter_draw_slot(m, whose, slot);
    ds[(uint16_t)(flags_of(whose) + slot)] = 1;
    uw_motion_barter_draw_slot_frame(m, whose, slot);
    ww(ds, (uint16_t)(BT_CACHE_A + slot * 2), 0xffff);
    ww(ds, (uint16_t)(BT_CACHE_B + slot * 2), 0xffff);
    barter_finish(m);
}

/* barter_slot_click(whose, slot) from a hotspot: nothing held
 * and the slot empty, nothing; nothing held, cursor_wait_for_drag -- the
 * shell's drag wait, ended by uw_motion_barter_drag or
 * uw_motion_barter_click_end; something held, the placing's release wait. */
void barter_click_slot(uw_motion *m, int whose, int slot) {
    uint8_t *ds = m->ds;
    int held = rw(ds, 0x5b06) || rw(ds, 0x5b08);
    m->barter_whose = (uint8_t)whose;
    m->barter_slot = (int16_t)slot;
    m->barter_loaded = (uint8_t)held;
    if (!held && !rw(ds, (uint16_t)(slots_of(whose) + slot * 2))) return;
    if (!held) {
        mouse_sample_buttons(m);
        m->drag_wait = 1;
        m->barter_wait = 1;
        return;
    }
    input_wait_button_release(m, 1);
    if (m->buttons) {
        m->barter_wait = 3;
        return;
    }
    /* the button up already (a call from a click's rest): the wait's first
     * poll ends it */
    barter_place_end(m);
}

/* barter_click_player_slot_at_cursor: the same over the live
 * cursor, with no offset -- inventory_panel_activate's element 0x18, the
 * table area a thing lifted from the paperdoll is let go over. */
void barter_click_player_slot_at_cursor(uw_motion *m) {
    uint8_t *ds = m->ds;
    int16_t slot = barter_slot_at(m, 1, rs(ds, CURSOR_X), rs(ds, CURSOR_Y));
    if (slot >= 0) barter_click_slot(m, 1, slot);
}

/* barter_click_npc_slot and barter_click_player_slot:
 * the pending event's position, relative to the hotspot, put back into
 * screen coordinates and the slot under it clicked. */
void barter_click_npc_slot(uw_motion *m) {
    uint8_t *ds = m->ds;
    uint16_t ev = rw(ds, 0x00e2);
    int16_t slot = barter_slot_at(m, 0, (int16_t)(rs(ds, ev) + 0x52), (int16_t)(rs(ds, (uint16_t)(ev + 2)) + 0x98));
    if (slot >= 0) barter_click_slot(m, 0, slot);
}

void barter_click_player_slot(uw_motion *m) {
    uint8_t *ds = m->ds;
    uint16_t ev = rw(ds, 0x00e2);
    int16_t slot = barter_slot_at(m, 1, (int16_t)(rs(ds, ev) + 0x8b), (int16_t)(rs(ds, (uint16_t)(ev + 2)) + 0x98));
    if (slot >= 0) barter_click_slot(m, 1, slot);
}

/* The lift past the stack's question (`rest` the split's remainder, or the
 * object itself): a piece too heavy to carry is merged back and freed with
 * "That is too heavy to take."; else the rest linked after the object, the
 * object lifted (the rest staying in the slot), the slot redrawn and
 * unoffered; a stack's question answered -- the whole stack taken as much
 * as a part: the answer is tested only for null -- leaves what was taken
 * on the cursor with action_state 1, anything else waits for the button
 * and places where it comes up. */
void barter_lift_take(uw_motion *m, uint16_t obj, uint16_t rest, int answered) {
    uint8_t *ds = m->ds, *ls = m->lseg;
    int whose = m->barter_whose, slot = m->barter_slot;
    uint16_t slots = slots_of(whose);
    if (!(uw_motion_inventory_can_carry(m, obj) & 0xff)) {
        if (rest != obj) {
            uint16_t total = (uint16_t)(((rw(ls, (uint16_t)(obj + 6)) >> 6) + (rw(ls, (uint16_t)(rest + 6)) >> 6)) & 0x3ff);
            ww(ls, (uint16_t)(obj + 6), (uint16_t)((rw(ls, (uint16_t)(obj + 6)) & 0x3f) | (total << 6)));
            uw_motion_object_list_remove(m, (uint16_t)(obj + 4), rest);
            uw_motion_obj_free(m, rest);
        }
        print_message(m, 0xfc);
        m->barter_wait = 0;
        return;
    }
    /* uw_motion_stack_answer linked the rest after the object already */
    m->barter_loaded = 1;
    barter_lift_from_slot(m, slot, slots, rest != obj);
    uw_motion_barter_draw_slot(m, whose, slot);
    ds[(uint16_t)(flags_of(whose) + slot)] = 0;
    ww(ds, (uint16_t)(BT_CACHE_A + slot * 2), 0xffff);
    ww(ds, (uint16_t)(BT_CACHE_B + slot * 2), 0xffff);
    uw_motion_barter_draw_slot_frame(m, whose, slot);
    if (!(rw(ds, 0x5b06) || rw(ds, 0x5b08))) { m->barter_wait = 0; return; }
    if (answered) {
        ww(ds, ACTION_STATE_WORD, 1);
        m->barter_wait = 0;
        return;
    }
    input_wait_button_release(m, 1);
    m->barter_wait = 2;
}

/* cursor_wait_for_drag's answer of a drag: the LIFTING branch -- the NPC's
 * goods stay until the deal is done; a stack of more than one asks how
 * many (stack_ask, path 3); else the lift. */
void uw_motion_barter_drag(uw_motion *m) {
    uint8_t *ds = m->ds, *ls = m->lseg;
    int whose = m->barter_whose, slot = m->barter_slot;
    uint16_t o, q;
    m->barter_wait = 0;
    if (whose == 0 && !ds[BT_DEAL_DONE]) return;
    o = obj_at(m, rw(ds, (uint16_t)(slots_of(whose) + slot * 2)));
    q = (uint16_t)(rw(ls, (uint16_t)(o + 6)) >> 6);
    if ((rw(ls, o) & 0x8000) && !(q & 0x200) && q != 1) {
        stack_ask(m, o, 3, (int16_t)slot);
        return;
    }
    barter_lift_take(m, o, o, 0);
}

/* cursor_wait_for_drag's answer of a click: with the right button a look
 * at the slot's object -- lore level 2 for the NPC's goods when
 * check_skill_roll(lore, 0x14) succeeds, else 1; 1 + check_skill_roll(lore,
 * 0xf) for the player's -- with the left its offered flag toggled and the
 * mark redrawn. */
void uw_motion_barter_click_end(uw_motion *m) {
    uint8_t *ds = m->ds;
    int whose = m->barter_whose, slot = m->barter_slot;
    uint16_t ev = rw(ds, 0x00e2);
    m->barter_wait = 0;
    if (!(rw(ds, (uint16_t)(ev + 6)) & 1)) {
        uint16_t o = obj_at(m, rw(ds, (uint16_t)(slots_of(whose) + slot * 2)));
        int16_t level = 1;
        uint8_t lore = ds[(uint16_t)(rw(ds, PLAYER_RECORD_PTR) + 0x29)];
        if (whose == 0) {
            if (check_skill_roll(m, lore, 0x14) > 0) level = 2;
        } else {
            level = (int16_t)(check_skill_roll(m, lore, 0xf) + 1);
        }
        look_at(m, o, level);
    } else {
        ds[(uint16_t)(flags_of(whose) + slot)] = (uint8_t)(ds[(uint16_t)(flags_of(whose) + slot)] ? 0 : 1);
        uw_motion_barter_draw_slot_frame(m, whose, slot);
    }
    barter_finish(m);
}

/* The hotspot's `after`: the release wait ended. After the lift (2): the
 * slot under the cursor on either side takes the placing, or, off both,
 * action_state 1 and paperdoll_click(-1) has the drop; before a placing
 * (3): the placing. */
void uw_motion_barter_wait_end(uw_motion *m) {
    uint8_t *ds = m->ds;
    if (m->barter_wait == 2) {
        int16_t x = rs(ds, CURSOR_X), y = rs(ds, CURSOR_Y), slot;
        if ((slot = barter_slot_at(m, 1, x, y)) >= 0) m->barter_whose = 1;
        else if ((slot = barter_slot_at(m, 0, x, y)) >= 0) m->barter_whose = 0;
        else {
            m->barter_wait = 0;
            ww(ds, ACTION_STATE_WORD, 1);
            paperdoll_click(m, -1);
            return;
        }
        m->barter_slot = slot;
        /* the placing's wait, with the button already up: its first poll */
        input_wait_button_release(m, 1);
        barter_place_end(m);
    } else if (m->barter_wait == 3) {
        barter_place_end(m);
    }
}
