/* SPDX-License-Identifier: MIT */
/* THE CHARACTER GENERATION SCREEN: chargen_screen and the loops
 * under it -- chargen_run_steps, chargen_select_from_list,
 * chargen_list_draw, chargen_list_run,
 * chargen_list_move_highlight, chargen_draw_attributes and
 * chargen_draw_skills -- over the game's memory as
 * uw_boot_new_game_begin leaves it, for the shell (src/tools/uwshell.c).
 *
 * The original runs it MODALLY inside new_game: CHRBTNS.GR into one EMS
 * frame and CHARGEN.BYT into another, palette 3 faded in, then eight steps,
 * each a list drawn on the right half of the screen and
 * chargen_select_from_list's own input loop waiting on it. The port keeps
 * the state between the shell's passes instead -- `step` says which step is
 * up and uw_chargen_ui_key and _click answer it -- and writes the player's
 * record as each step is taken, which is what the original does and what
 * makes the attributes and skills on the left panel the ones the game
 * starts with.
 *
 * WHAT A PAGE FLIP IS HERE. The original draws on two pages: a step's list
 * goes to the hidden one, which is then shown, and the other page is
 * re-blitted with CHARGEN.BYT for the next step; the fills in colour 0x106
 * (gfx_set_colour's mode above 255) copy a rectangle from the other page
 * rather than filling it, which is how the left panel's attributes carry
 * from step to step while the right half comes back clean. Over one page
 * that is: the left panel is left alone, and the right half -- and the two
 * rectangles chargen_draw_attributes and chargen_draw_skills clear -- are
 * put back from CHARGEN.BYT. */
#ifndef UW_CHARGEN_UI_H
#define UW_CHARGEN_UI_H

#include "uw_motion.h"
#include "uw_chargen.h"

#define UW_CG_STEPS 8
#define UW_CG_ITEMS 24        /* the longest list is the skill step's twenty */

typedef struct {
    uw_motion *m;
    char       dir[512];
    uw_blob    backdrop;              /* CHARGEN.BYT, 320 x 200 */
    uw_blob    font;                  /* FONTCHAR.SYS */
    uw_skills  sk;                    /* SKILLS.DAT */
    uw_chargen cg;                    /* CHRGEN.DAT */
    int        have_sk, have_cg;
    /* the eight records chargen_screen builds, nine words each: the prompt
     * string, the name field's flag, (the item pointer), the count, the art
     * index, and the rows, columns and gap chargen_list_draw works out */
    int16_t    rec[UW_CG_STEPS][9];
    uint16_t   item[UW_CG_STEPS][UW_CG_ITEMS];
    char       name[0x20];            /* the name buffer at the records' +0x6e */
    /* the step and its list's state (chargen_select_from_list's locals) */
    int        step, sel, prev, accepted, empty, name_len, name_x;
    /* the class's skill holds (chargen_run_steps' locals) */
    uint8_t    skills[6];
    int        skill_pos, applied;
    int        state;                 /* 0 running, 1 the character is made, -1 abandoned */
    long       not_carried;
} uw_chargen_ui;

/* chargen_screen's loads and its first list drawn: CHRBTNS.GR, SKILLS.DAT,
 * CHRGEN.DAT, FONTCHAR.SYS, CHARGEN.BYT and PALS.DAT's palette 3 (which the
 * caller's screen is presented through). False when a file is missing. */
bool uw_chargen_ui_open(uw_chargen_ui *u, uw_motion *m, const char *dir);
void uw_chargen_ui_close(uw_chargen_ui *u);

/* input_poll's answer to the step's loop: a key code, or a click at the
 * cursor (the game's coordinates, y up). Escape at the first step abandons
 * the generation (`state` -1); at any other it starts again, as the
 * original's does. When `state` turns 1 the record is the character's and
 * the dungeon's own palette is back. */
void uw_chargen_ui_key(uw_chargen_ui *u, uint16_t code);
void uw_chargen_ui_click(uw_chargen_ui *u, int16_t x, int16_t y);

#endif
