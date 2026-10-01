/* SPDX-License-Identifier: MIT */
/* The title screen: main_menu and the menu widget under it,
 * as a state a host drives -- the keys, the button and the
 * idle tick a pass -- and draws from.
 *
 * WHAT THE ORIGINAL DOES. main_menu(first):
 *   the four 16-byte records in the data segment copied to the stack (the table
 *   menu_set_item fills: two image pointers, then x, y, width, height,
 *   with y the BOTTOM edge in the game's bottom-up coordinates);
 *   enumerate_save_games: with no save, three items and the cursor on
 *   item 1; with one, four items and the cursor on item 3;
 *   main_menu_intro_if_no_saves(first): cutscene 0 when `first` and no
 *   save exists;
 *   cursor_shape_push(0x106c); cursor_show;
 *   loop:
 *     music_restart_current;
 *     when the last answer was 0..3 (or none yet): DATA\opscr.byt read
 *     whole and blitted, OPBTN.GR loaded through menu_set_item (image 2i
 *     and 2i+1 are item i's normal and highlighted faces, the even one
 *     measured), and unless the last answer was 3: menu_draw_items with
 *     the cursor highlighted, palette_read(2) and palette_fade_in over 2;
 *     answer = menu_select(count, table, 0, cursor);
 *     -1 (0x278, Alt-x): game_shutdown(0); exit(1)
 *      0  Introduction:     cutscene_play(0)
 *      1  Create Character: palette_fade_out(2); new_game(); when a
 *         character was made: SAVE0 emptied, DATA\lev.ark copied to
 *         SAVE0\lev.ark, bglobals_create, level_load(1),
 *         place_player_in_tile(0x20, 2, 1), level_transition_effects(1, 0),
 *         and the loop ends
 *      2  Acknowledgements: cutscene_play(10), post_event(0x7ffe)
 *      3  Journey Onward:   main_menu_save_list -- 1 ends the loop, -1
 *         draws opscr.byt again with "Error: Bad save file" (block 1,
 *         0xa9) centred at y 0x5a in FONTBIG colour 0xa2 and waits for a
 *         key, 0 loops
 *   on the way out: cursor_shape_pop(3), cursor_show, game_set_mode(1),
 *   post_event(0x7ffe), options_panel_active = 0.
 *
 * menu_select: the items drawn with the cursor highlighted
 * (FONTBIG opened around the drawing), then input_poll until it answers;
 * keys 0x8d, 0x8f, 0xa6, 0x162, 0x170 move up, 0x91, 0x93, 0x166, 0x16e
 * down, 0x8c, 0x8e, 0xa5, 0x23c to the first, 0x92, 0x94, 0x23e to the
 * last, 0xa7..0xac through a six-word table (first, up, down, last, down,
 * last), Return picks the cursor, Escape answers -1 only when `flag` is
 * set (it is not here), 0x278 always; the buttons hand the press to
 * menu_run, whose answer is the pick when it is an item, and a cursor move
 * of (answer - count) when it is count or more. The cursor is clamped
 * after every key. menu_run: while the button is held the
 * item under the cursor is highlighted as it changes -- the hit test is
 * x..x+w-1 by y-h+1..y -- and the release answers that item, or, when the
 * cursor left every item after being over one, item + count.
 *
 * main_menu_save_list: opscr.byt again (no palette change),
 * the existing slots' descriptions (their trailing spaces cut) as a TEXT
 * menu -- each line centred, FONTBIG, colour 0xa2 highlighted and 0xaa
 * not, line i's baseline at y 100 - 0x16 * i -- and on a pick "You reenter
 * the Abyss . . ." (block 1, 0x101) over opscr.byt, then
 * savegame_restore_progress(slot) and weapons_load_colourmap.
 *
 * menu_idle_tick: every 14 ticks palette_rotate_range(0x40,
 * 0x40, 1) over the WORKING palette -- the game's, not the title's, which
 * only ever reached the DAC through the fade -- and vga_set_palette of
 * those 64 entries. So the title screen's animated band is the working
 * palette's, rotating, over the title palette's rest.
 *
 * enumerate_save_games: SAVE1..SAVE4's `desc`, the first line
 * (at most 0x27 characters) into a 0x28-byte row each, the slot's bit set;
 * a slot with no file reads "<not used yet>". */
#ifndef UW_MENU_H
#define UW_MENU_H

#include "uw_motion.h"
#include "uw_font.h"

enum {
    UW_MENU_NONE = -2,        /* nothing chosen yet */
    UW_MENU_QUIT = -1,        /* Alt-x: game_shutdown(0), exit(1) */
    UW_MENU_INTRO = 0,
    UW_MENU_CHARGEN = 1,
    UW_MENU_ACK = 2,
    UW_MENU_JOURNEY = 3
};

typedef struct {
    const uint8_t *face[2];   /* +0, +4: the normal and highlighted images (OPBTN.GR) */
    int x, y, w, h;           /* +8..+0xe: y the bottom edge, bottom-up */
} uw_menu_item;

typedef struct {
    uw_motion *m;
    char       dir[512];
    char       saves[512];
    uw_blob    opscr;                 /* DATA/OPSCR.BYT, 64000 raw pixels */
    uw_font    font;                  /* FONTBIG.SYS */
    int        have_font;
    uint8_t    title_palette[768];    /* PALS.DAT's palette 2 */
    uint8_t    screen[64000];         /* the title's own page */
    uw_menu_item item[4];
    int        count;                 /* 3 with no save, 4 with one */
    int        cursor;                /* menu_select's cursor */
    int        hover;                 /* menu_run's remembered entry, -1 none */
    int        held;                  /* menu_run is running: a button is down */
    int        last;                  /* the last answer, for the loop's redraw rule */
    uint32_t   idle_stamp;            /* menu_idle_tick's last stamp */
    /* the saves: the mask of slots with a file, and each slot's row */
    unsigned   saves_mask;
    char       desc[4][0x28];
    /* the save list up (main_menu_save_list): the rows shown, their slots */
    int        list_up;
    int        list_count;
    int        list_slot[4];
    char       list_text[4][0x28];
    int        list_cursor, list_hover;
    int        choice;                /* UW_MENU_*, or a slot 1..4 from the list */
    long       not_carried;
} uw_menu;

/* main_menu's entry: the records and the images, the saves enumerated, the
 * count and the cursor. `saves` is the directory SAVE1..SAVE4 live in. */
bool uw_menu_open(uw_menu *u, uw_motion *m, const char *dir, const char *saves);
void uw_menu_close(uw_menu *u);

/* True when no save exists (main_menu_intro_if_no_saves's test). */
int  uw_menu_no_saves(const uw_menu *u);

/* The loop's top: opscr.byt and the items onto `screen`, the cursor
 * highlighted (menu_draw_items). The host fades palette 2 in after it. */
void uw_menu_draw(uw_menu *u);

/* menu_idle_tick at `clock`: the working palette's 0x40..0x7f rotated up
 * once every 14 ticks; true when it moved, so the host can carry those
 * entries into the DAC it presents with. */
int  uw_menu_idle(uw_menu *u, uint32_t clock);

/* A key as keyboard_read codes it; the choice lands in `choice`. */
void uw_menu_key(uw_menu *u, uint16_t code);
/* menu_run: the button's state each pass, the cursor at (x, y) bottom-up. */
void uw_menu_button(uw_menu *u, int down, int x, int y);

/* main_menu_save_list: the list drawn over opscr.byt. */
void uw_menu_list_open(uw_menu *u);
/* The list's answer taken: "You reenter the Abyss . . ." drawn; the slot
 * 1..4 is in `choice`. */
void uw_menu_list_picked(uw_menu *u);
/* The list left with Escape or Alt-x (menu_select's -1): main_menu_save_list
 * returns 0 and main_menu's loop shows the menu again. */
void uw_menu_list_cancel(uw_menu *u);
/* The restore failed: opscr.byt again with "Error: Bad save file"; the host
 * waits for a key. */
void uw_menu_draw_error(uw_menu *u);

/* victory_screen, from the instructions, over `page` (win2.byt,
 * 64000 pixels): in FONTCHAR.SYS and colour 0x5c, the Avatar's name centred
 * at y 0xb4, then a line down each: "A level N <class>", "Banished the
 * Slasher of Veils", "after N days in the Abyss" (the clock in 256ths over
 * 0x1c2000 and 12); a line down, six attribute rows in two columns at x
 * 0x50 and 0xbe, the value at +0x2d (Str, Dex, Int from the class row, Vit
 * its +4, Mana the record's +0x38, Exp the experience over 10); two lines
 * down, the twenty skills in three columns at x (i % 3) * 0x4a + 0x32, a
 * line down every three, the value right-aligned at x + 0x46. The name is
 * the record's own bytes, which the original reaches through the run-time
 * string block dungeon_mode_setup registers for it. */
bool uw_victory_draw(uw_motion *m, const char *dir, uint8_t *page);

#endif
