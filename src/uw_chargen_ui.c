/* SPDX-License-Identifier: MIT */
/* See uw_chargen_ui.h. */
#include "uw_chargen_ui.h"
#include "uw_boot.h"
#include "uw_image.h"
#include "uw_strings.h"

#include <stdio.h>
#include <string.h>

/* the record's nine words, by the offsets chargen_list_draw uses */
enum {
    W_PROMPT = 0,   /* a block 2 string id, 0 for a list with no heading */
    W_FIELD  = 1,   /* the name step: its text buffer (a flag here) */
    W_COUNT  = 4,
    W_ART    = 5,   /* into the art table: the row's image */
    W_ROWS   = 6,
    W_COLS   = 7,
    W_GAP    = 8
};

enum {
    CG_COLOUR   = 0x49,   /* the text colour chargen_screen sets */
    CG_BAR      = 6,      /* CHRBTNS.GR's 145 x 16 heading bar */
    CG_PORTRAIT = 0x11,   /* the big portraits: 0x11 + sex * 5 + the choice */
    CG_NAME_MAX = 0x1d
};

static uint16_t rw_(const uint8_t *p, uint16_t at) { return (uint16_t)(p[at] | (p[(uint16_t)(at + 1)] << 8)); }

/* ---- the screen's pieces ------------------------------------------------ */

static const uint8_t *art(uw_chargen_ui *u, int index, int *w, int *h) {
    char data[600];
    snprintf(data, sizeof data, "%s/DATA", u->dir);
    return uw_gr_file_image(data, "CHRBTNS", index, w, h);
}

/* gfx_blit of an art table entry, colour 0 skipped while `masked`. */
static void art_blit(uw_chargen_ui *u, int index, int x, int y, int masked) {
    int w = 0, h = 0;
    const uint8_t *px = art(u, index, &w, &h);
    if (!px) { u->m->pixels_not_drawn++; return; }
    u->m->span_variant = (uint8_t)(masked ? 1 : 0);
    uw_motion_blit(u->m, px, w, x, y, h, w);
    u->m->span_variant = 0;
}

static void art_size(uw_chargen_ui *u, int index, int *w, int *h) {
    *w = 0; *h = 0;
    if (!art(u, index, w, h)) UW_NOT_CARRIED(u->not_carried);
}

/* A fill in colour mode 0x106: the rectangle from the other page, which is
 * CHARGEN.BYT's image. */
static void restore(uw_chargen_ui *u, int x0, int y0, int x1, int y1) {
    int sy, sx;
    if (!u->m->screen || !u->backdrop.data || u->backdrop.size < 64000) { UW_NOT_CARRIED(u->not_carried); return; }
    for (sy = 199 - y0; sy <= 199 - y1; sy++) {
        if (sy < 0 || sy >= 200) continue;
        for (sx = x0; sx <= x1; sx++) {
            if (sx < 0 || sx >= 320) continue;
            u->m->screen[sy * 320 + sx] = u->backdrop.data[sy * 320 + sx];
            if (u->m->screen_written) u->m->screen_written[sy * 320 + sx] = 1;
        }
    }
}

static int text_width(uw_chargen_ui *u, const char *s) {
    return uw_motion_string_width(u->font.data, u->font.size, s);
}

static void text_draw(uw_chargen_ui *u, const char *s, int x, int y) {
    uw_motion_draw_string(u->m, u->font.data, u->font.size, s, x, y, CG_COLOUR);
}

/* get_string(id | 0x400): the generation's strings are block 2's. */
static void cg_string(uw_chargen_ui *u, uint16_t packed, char *out, int cap) {
    out[0] = 0;
    if (u->m->strings) uw_strings_by_id(u->m->strings, packed, out, cap);
    else UW_NOT_CARRIED(u->not_carried);
}

static int font_height(const uw_chargen_ui *u) {
    if (!u->font.data || u->font.size < 12) return 6;
    return (int16_t)rw_(u->font.data, 6);
}

/* ---- the list widget (chargen_list_draw, _move_highlight, _run) --------- */

/* chargen_list_draw: the list's geometry worked out into the
 * record -- the columns that fit 0xc4 rows of (height + 4), the rows that
 * takes, and the gap that spreads the columns over 0xa0 -- then the heading
 * (centred, or on CHRBTNS.GR's bar with the name field's prompt) and each
 * row's image with its string centred on it. The portrait step (art 3)
 * blits the portrait its list's first word counts from over each row. */
static void list_draw(uw_chargen_ui *u, int step) {
    int16_t *rec = u->rec[step];
    int w = 0, h = 0, head = rec[W_PROMPT] != 0, y, i, x = 0;
    char s[0x80];
    art_size(u, rec[W_ART], &w, &h);
    if (h <= 0) return;
    rec[W_COLS] = (int16_t)((rec[W_COUNT] * (h + 4) - 4) / (0xc4 - (head ? 0x14 : 0)) + 1);
    rec[W_ROWS] = (int16_t)((rec[W_COUNT] + rec[W_COLS] - 1) / rec[W_COLS]);
    rec[W_GAP]  = (int16_t)((0xa0 - rec[W_COLS] * w) / (rec[W_COLS] + 1));
    y = 0xc5 - (0xc4 - (rec[W_ROWS] + head) * (h + 4) + 4) / 2;
    if (head) {
        cg_string(u, (uint16_t)(0x400 | rec[W_PROMPT]), s, sizeof s);
        if (rec[W_FIELD]) {
            art_blit(u, CG_BAR, 0xa4, y, 0);
            text_draw(u, s, 0xa8, y - 3);
        } else {
            text_draw(u, s, 0xa4 + (0x91 - text_width(u, s)) / 2, y - 3);
        }
    } else {
        y += h + 4;
    }
    for (i = 0; i < rec[W_COUNT]; i++) {
        if (i % rec[W_COLS] == 0) { x = 0xa0 - w; y -= h + 4; }
        x += rec[W_GAP] + w;
        art_blit(u, rec[W_ART], x, y, 0);
        if (rec[W_ART] == 0) {
            cg_string(u, (uint16_t)(0x400 | u->item[step][i]), s, sizeof s);
            text_draw(u, s, x + (w - text_width(u, s)) / 2, y - 3);
        } else if (rec[W_ART] == 3) {
            art_blit(u, u->item[step][0] + i, x, y, 1);
        }
    }
}

/* The first row's y: chargen_list_move_highlight's own, which is
 * chargen_list_draw's less a row when the list has a heading. */
static int list_top(const uw_chargen_ui *u, int step, int h, int head) {
    const int16_t *rec = u->rec[step];
    (void)u;
    return 0xc5 - (0xc4 - (rec[W_ROWS] - (head ? 1 : 0)) * (h + 4) + 4) / 2;
}

/* chargen_list_move_highlight: the two rows that change, `from`
 * put back with the art past the row's and `to` drawn with the one past
 * that, both with colour 0 skipped so the row's own text stays. A row at or
 * past the count is not drawn, which is how the caller says "none" with
 * 0xff. */
static void move_highlight(uw_chargen_ui *u, int step, int to, int from) {
    int16_t *rec = u->rec[step];
    int w = 0, h = 0, head = rec[W_PROMPT] != 0, k, y0, x0;
    if (to == from) return;
    art_size(u, rec[W_ART], &w, &h);
    if (h <= 0 || rec[W_COLS] <= 0) return;
    x0 = rec[W_GAP] + 0xa0;
    y0 = list_top(u, step, h, head);
    for (k = 0; k < 2; k++) {
        int v = k ? to : from;
        if (v < 0 || v >= rec[W_COUNT]) continue;
        art_blit(u, rec[W_ART] + k + 1, x0 + (v % rec[W_COLS]) * (w + rec[W_GAP]),
                 y0 - (v / rec[W_COLS]) * (h + 4), 1);
    }
}

/* chargen_list_run for one position: the row the cursor is in,
 * or -1 -- the row's own rectangle, not the gap around it. */
static int list_at(uw_chargen_ui *u, int step, int x, int y) {
    int16_t *rec = u->rec[step];
    int w = 0, h = 0, head = rec[W_PROMPT] != 0, x0, y0, sel, dx, dy;
    art_size(u, rec[W_ART], &w, &h);
    if (h <= 0 || rec[W_COLS] <= 0) return -1;
    x0 = rec[W_GAP] + 0xa0;
    y0 = 0xc5 - (0xc4 - (rec[W_ROWS] + head) * (h + 4) + 4) / 2 - (head ? h + 4 : 0);
    sel = ((y0 - y) / (h + 4)) * rec[W_COLS] + (x - x0) / (w + rec[W_GAP]);
    if (sel < 0 || sel >= rec[W_COUNT] || y > y0 || x < x0) return -1;
    dx = x - (x0 + (w + rec[W_GAP]) * (sel % rec[W_COLS]));
    dy = (y0 - y) - (h + 4) * (sel / rec[W_COLS]);
    if (dx >= w || dy >= h) return -1;
    return sel;
}

/* ---- the left panel (chargen_draw_attributes, _draw_skills) ------------- */

/* chargen_draw_attributes: the critter row's strength,
 * dexterity, intelligence and vitality under their labels, each
 * right-aligned at 0x8c. */
static void draw_attributes(uw_chargen_ui *u) {
    uint8_t *ds = u->m->ds;
    uint16_t row = rw_(ds, 0x7272);
    static const int at[4] = { 5, 6, 7, 4 };
    static const int y[4] = { 0x96, 0x84, 0x72, 0x60 };
    int i;
    restore(u, 0x5d, 0x96, 0x8c, 0x4e);
    for (i = 0; i < 4; i++) {
        char label[0x10], value[0x10];
        snprintf(label, sizeof label, "%s", (const char *)(ds + 0xdc0 + i * 5));
        snprintf(value, sizeof value, "%u", ds[(uint16_t)(row + at[i])]);
        text_draw(u, label, 0x5d, y[i]);
        text_draw(u, value, 0x8c - text_width(u, value), y[i]);
    }
}

/* chargen_draw_skills: the first six skills the record holds
 * (its +0x21 array, twenty of them), each named by block 2 string
 * index + 0x1f, eleven rows apart from 0x43 and right-aligned at 0x7d. */
static void draw_skills(uw_chargen_ui *u) {
    uint8_t *ds = u->m->ds;
    uint16_t rec = rw_(ds, 0x7270);
    int i, k = 0;
    restore(u, 0x1e, 0x43, 0x7d, 0xc);
    for (i = 0; i < 0x14 && k < 6; i++) {
        char name[0x40], value[0x10];
        if (!ds[(uint16_t)(rec + 0x21 + i)]) continue;
        cg_string(u, (uint16_t)(0x400 | (i + 0x1f)), name, sizeof name);
        snprintf(value, sizeof value, "%u", ds[(uint16_t)(rec + 0x21 + i)]);
        text_draw(u, name, 0x1e, 0x43 - k * 0xb);
        text_draw(u, value, 0x7d - text_width(u, value), 0x43 - k * 0xb);
        k++;
    }
}

/* ---- the steps (chargen_run_steps) ------------------------------------- */

static void enter_step(uw_chargen_ui *u) {
    u->sel = 0;
    u->prev = 0;
    u->accepted = 0;
    /* the hidden page's backdrop: the right half comes back clean, the left
     * panel keeps what the steps have drawn on it */
    restore(u, 0xa0, 0xc7, 0x13f, 0);
    list_draw(u, u->step);
    move_highlight(u, u->step, 0, 0xff);
    if (u->step == 6) {
        char s[0x40];
        u->name_len = 0;
        u->empty = 1;
        u->name[0] = 0;
        cg_string(u, (uint16_t)(0x400 | u->rec[6][W_PROMPT]), s, sizeof s);
        u->name_x = 0xa8 + text_width(u, s);
    }
}

/* chargen_run_steps' restart, which both the last step's NO and an Escape
 * reach: the left panel put back and the walk begun again. */
static void restart(uw_chargen_ui *u) {
    restore(u, 0x11, 0xc7, 0x8f, 0);
    u->skill_pos = 0;
    u->applied = 0;
    u->step = 0;
    memset(u->skills, 0x14, sizeof u->skills);
    enter_step(u);
}

/* The skill step's list from the class's next hold, and whether one is
 * offered at all (chargen_skill_choices writes the step's count and its
 * words, each skill shown as block 2 string v + 0x1f). */
static int skill_offer(uw_chargen_ui *u) {
    const uint8_t *offer = NULL;
    int offered = 0, i;
    if (!uw_chargen_skill_choices(u->m, &u->sk, &u->skill_pos, u->skills, &offer, &offered)) return 0;
    if (offered > UW_CG_ITEMS) offered = UW_CG_ITEMS;
    for (i = 0; i < offered; i++) u->item[3][i] = (uint16_t)(offer[i] + 0x1f);
    u->rec[3][W_COUNT] = (int16_t)offered;
    return 1;
}

/* game_change_mode's palette: the dungeon's own, which the mode's entry
 * loads over the generation's. */
static void palette_load(uw_chargen_ui *u, int which) {
    char path[600];
    uw_blob bl;
    snprintf(path, sizeof path, "%s/DATA/PALS.DAT", u->dir);
    bl = uw_read_file(path);
    if (bl.data && bl.size >= (size_t)(which + 1) * 768 && u->m->palette)
        memcpy(u->m->palette, bl.data + (size_t)which * 768, 768);
    else UW_NOT_CARRIED(u->not_carried);
    uw_free(&bl);
}

/* chargen_run_steps' eight cases: what the step's answer writes into the
 * record, what it draws, and where it goes next. */
static void take_step(uw_chargen_ui *u, int n) {
    uint8_t *ds = u->m->ds;
    uint16_t rec = rw_(ds, 0x7270);
    char s[0x80];
    switch (u->step) {
    case 0:                                  /* the sex, and the portraits it chooses from */
        cg_string(u, (uint16_t)(0x400 | u->item[0][n]), s, sizeof s);
        ds[(uint16_t)(rec + 0x64)] = (uint8_t)((ds[(uint16_t)(rec + 0x64)] & 0xfd) | ((n & 1) << 1));
        u->item[4][0] = (uint16_t)(n ? 0xc : 7);
        text_draw(u, s, 0x11, 0xb2);
        u->step++;
        break;
    case 1:                                  /* the handedness */
        ds[(uint16_t)(rec + 0x64)] = (uint8_t)((ds[(uint16_t)(rec + 0x64)] & 0xfe) | (n & 1));
        u->step++;
        break;
    case 2:                                  /* the class: the attributes rolled, the skills begun */
        cg_string(u, (uint16_t)(0x400 | u->item[2][n]), s, sizeof s);
        ds[(uint16_t)(rec + 0x64)] = (uint8_t)((ds[(uint16_t)(rec + 0x64)] & 0x1f) | ((n & 7) << 5));
        uw_chargen_roll_attributes(u->m, &u->sk);
        if (!skill_offer(u)) u->step++;      /* no choice to make: the skill step is skipped */
        u->applied += uw_chargen_apply_skills(u->m, u->applied, u->skills);
        text_draw(u, s, 0x8f - text_width(u, s), 0xb2);
        draw_attributes(u);
        draw_skills(u);
        u->step++;
        break;
    case 3:                                  /* a skill chosen, and the next hold asked for */
        u->skills[u->skill_pos - 1] = (uint8_t)(u->item[3][n] - 0x1f);
        u->applied += uw_chargen_apply_skills(u->m, u->applied, u->skills);
        draw_skills(u);
        if (!skill_offer(u)) u->step++;
        break;
    case 4: {                                /* the portrait, drawn large on the left */
        int w = 0, h = 0, big = CG_PORTRAIT + ((ds[(uint16_t)(rec + 0x64)] >> 1) & 1) * 5 + n;
        art_size(u, big, &w, &h);
        art_blit(u, big, (0x38 - w) / 2 + 0x10, 0x9c - (0x4c - h) / 2, 1);
        ds[(uint16_t)(rec + 0x64)] = (uint8_t)((ds[(uint16_t)(rec + 0x64)] & 0xe3) | ((n & 7) << 2));
        u->step++;
        break;
    }
    case 5:                                  /* the difficulty */
        ds[(uint16_t)(rec + 0xb4)] = (uint8_t)n;
        u->step++;
        break;
    case 6:                                  /* the name, typed and then the record's */
        text_draw(u, u->name, (0x7e - text_width(u, u->name)) / 2 + 0x11, 0xbd);
        if (u->name[0]) {
            memset(ds + rec, 0, CG_NAME_MAX);
            memcpy(ds + rec, u->name, strlen(u->name) < CG_NAME_MAX ? strlen(u->name) : CG_NAME_MAX);
        }
        ds[(uint16_t)(rec + CG_NAME_MAX)] = 0;
        u->step++;
        break;
    default:                                 /* keep this character? */
        if (n != 0) { restart(u); return; }
        uw_chargen_keep(u->m);
        u->step++;
        break;
    }
    if (u->step >= UW_CG_STEPS) {
        /* the closing screen: the right half back to the backdrop, the name
         * and block 1's "enters the Abyss . . ." centred on it */
        char line[0x80];
        int fh = font_height(u);
        restore(u, 0xa0, 0xc7, 0x13f, 0);
        cg_string(u, 0x300, line, sizeof line);
        text_draw(u, u->name, (0xa0 - text_width(u, u->name)) / 2 + 0xa0, 0x62 + fh);
        text_draw(u, line, (0xa0 - text_width(u, line)) / 2 + 0xa0, 0x62);
        palette_load(u, 0);
        u->state = 1;
        return;
    }
    enter_step(u);
}

/* ---- the screen ------------------------------------------------------- */

bool uw_chargen_ui_open(uw_chargen_ui *u, uw_motion *m, const char *dir) {
    char path[600];
    int i, k;
    memset(u, 0, sizeof *u);
    u->m = m;
    snprintf(u->dir, sizeof u->dir, "%s", dir);
    snprintf(path, sizeof path, "%s/DATA/SKILLS.DAT", dir);
    u->have_sk = uw_skills_open(&u->sk, path);
    snprintf(path, sizeof path, "%s/DATA/CHRGEN.DAT", dir);
    u->have_cg = uw_chargen_open(&u->cg, path);
    snprintf(path, sizeof path, "%s/DATA/CHARGEN.BYT", dir);
    u->backdrop = uw_read_file(path);
    snprintf(path, sizeof path, "%s/DATA/FONTCHAR.SYS", dir);
    u->font = uw_read_file(path);
    if (!u->have_sk || !u->have_cg || !u->backdrop.data || u->backdrop.size < 64000 || !u->font.data) {
        uw_chargen_ui_close(u);
        return false;
    }
    /* chargen_screen's eight records, and the lists the loader walks past
     * them (their pointers are what it patches into words 2 and 3) */
    for (i = 0; i < UW_CG_STEPS; i++) {
        for (k = 0; k < 9; k++) u->rec[i][k] = (int16_t)uw_chargen_word(&u->cg, i, k);
        for (k = 0; k < UW_CG_ITEMS; k++) u->item[i][k] = uw_chargen_choice(&u->cg, i, k);
    }
    u->rec[6][W_FIELD] = 1;                  /* the name step's text buffer */
    memset(u->skills, 0x14, sizeof u->skills);
    palette_load(u, 3);
    if (m->screen) memcpy(m->screen, u->backdrop.data, 64000);
    if (m->screen_written) memset(m->screen_written, 1, 64000);
    enter_step(u);
    return true;
}

void uw_chargen_ui_close(uw_chargen_ui *u) {
    if (u->have_sk) uw_skills_close(&u->sk);
    if (u->have_cg) uw_chargen_close(&u->cg);
    uw_free(&u->backdrop);
    uw_free(&u->font);
    u->have_sk = 0;
    u->have_cg = 0;
}

/* chargen_select_from_list's first half: the name field. A
 * printable character is drawn where the caret stands and appended, a
 * backspace takes the last one back and puts the bar's pixels over it, and
 * Return with a character or Escape ends the step. */
static void name_key(uw_chargen_ui *u, uint16_t code) {
    char one[2];
    if ((code == 0x0d && !u->empty) || code == 0x1b) {
        u->name[u->name_len] = 0;
        take_step(u, 0);
        return;
    }
    one[1] = 0;
    if (code >= 0x20 && code < 0x7f && u->name_x < 0x12e && u->name_len < CG_NAME_MAX) {
        one[0] = (char)code;
        text_draw(u, one, u->name_x, 0x6b - 3);
        u->name_x += text_width(u, one);
        u->name[u->name_len++] = (char)code;
        u->name[u->name_len] = 0;
        u->empty = 0;
    } else if (code == 8 || code == 0x91) {
        if (u->name_len > 0) {
            int w = 0, h = 0;
            const uint8_t *px;
            u->name_len--;
            one[0] = u->name[u->name_len];
            u->name[u->name_len] = 0;
            u->name_x -= text_width(u, one);
            px = art(u, CG_BAR, &w, &h);
            /* the bar's own pixels from the character's column on */
            if (px && u->name_x - 0xa4 >= 0 && u->name_x - 0xa4 < w)
                uw_motion_blit(u->m, px + (u->name_x - 0xa4), w, u->name_x, 0x6b, h, w - (u->name_x - 0xa4));
            else UW_NOT_CARRIED(u->not_carried);
        } else {
            u->empty = 1;
        }
    }
}

void uw_chargen_ui_key(uw_chargen_ui *u, uint16_t code) {
    int16_t *rec;
    if (u->state) return;
    if (u->step == 6) { name_key(u, code); return; }
    rec = u->rec[u->step];
    switch (code) {
    case 0x0d: u->accepted = 1; break;
    case 0x1b: case 0x278:
        if (u->step == 0) { u->state = -1; return; }
        /* the screen put back from the page saved before the first step */
        if (u->m->screen && u->backdrop.data) memcpy(u->m->screen, u->backdrop.data, 64000);
        if (u->m->screen_written) memset(u->m->screen_written, 1, 64000);
        restart(u);
        return;
    case 0x8c: case 0x8e: case 0xa5: case 0xa7: case 0x23c: u->sel = 0; break;
    case 0x8f: case 0xa8: case 0x162: u->sel--; break;
    case 0x91: case 0xa9: case 0x166: u->sel++; break;
    case 0x92: case 0x94: case 0xaa: case 0xac: case 0x23e: u->sel = rec[W_COUNT] - 1; break;
    case 0x93: case 0xab: case 0x16e: u->sel += rec[W_COLS]; break;
    case 0x8d: case 0xa6: case 0x170: u->sel -= rec[W_COLS]; break;
    default: return;
    }
    if (u->sel < 0) u->sel = 0;
    else if (u->sel >= rec[W_COUNT]) u->sel = rec[W_COUNT] - 1;
    else move_highlight(u, u->step, u->sel, u->prev);
    u->prev = u->sel;
    if (u->accepted) take_step(u, u->sel);
}

void uw_chargen_ui_click(uw_chargen_ui *u, int16_t x, int16_t y) {
    int row;
    if (u->state || u->step == 6) return;
    row = list_at(u, u->step, x, y);
    if (row < 0) return;
    move_highlight(u, u->step, row, u->prev);
    u->sel = row;
    u->prev = row;
    take_step(u, row);
}
