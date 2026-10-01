/* SPDX-License-Identifier: MIT */
/* CRIT/ -- ASSOC.ANM and the creature animation pages.
 *
 * Creature artwork lives in 32 pages,
 * `CRIT/CRnnPAGE.Nxx`, and ASSOC.ANM says which page each of the 64 creature
 * types uses. The demand pager that loads them at run time is
 * `rast_crit_page_map`.
 *
 * WHAT COMES FROM CODE, and it is only three things. `crit_pages_init`
 * is the only reader of any of this:
 *   * ASSOC.ANM's table starts at 0x100 and is 0x80 bytes -- it seeks there
 *     and reads exactly that;
 *   * a page number goes into the filename as TWO OCTAL DIGITS, `(n >> 3) &
 *     7` and `n & 7`, while the extension is two DECIMAL digits;
 *   * each page file opens with two bytes and the loop walking a page's
 *     files stops when byte0 + byte1 reaches 0xa0 -- so the pair is (first
 *     slot, slot count) over a 160-slot space, and a page's files partition
 *     it.
 *
 * WHAT IS INFERRED, AND SAID SO. Past those two bytes a page continues
 *
 *     byte slot_map[slot_count]   an entry index, or 0xff for empty
 *     byte entry_count            == max(slot_map) + 1
 *     byte entry[entry_count][8]  frame numbers, 0xff for unused
 *
 * which is NOT read from code -- what consumes it is the hand-written
 * assembly rasteriser. It is inferred from the data and then
 * asserted: all 64 files satisfy it, the `max(slot_map) + 1 == entry_count`
 * relation included, which is the part a wrong section size would break.
 * Everything past the entries is undecoded.
 */
#ifndef UW_CRIT_H
#define UW_CRIT_H

#include "uw.h"

#define UW_CRIT_PAGES        32
#define UW_CRIT_CREATURES    64
#define UW_CRIT_NAME_LEN     8
#define UW_CRIT_FIRST_ID     0x40
#define UW_CRIT_TABLE_AT     0x100    /* crit_pages_init seeks here */
#define UW_CRIT_TABLE_BYTES  0x80     /* and reads exactly this many */
#define UW_CRIT_SLOTS        0xA0     /* the bound its file loop stops at */
#define UW_CRIT_FRAME_SLOTS  8

typedef struct {
    uw_blob file;
} uw_crit_assoc;

bool uw_crit_assoc_open(uw_crit_assoc *a, const char *path);
void uw_crit_assoc_close(uw_crit_assoc *a);
/* The page's name, NUL-padded to 8 bytes; NULL when out of range. */
const char *uw_crit_page_name(const uw_crit_assoc *a, int page);
/* Creature type 0..63 (item 0x40 + type): low byte page, high byte variant. */
bool uw_crit_assoc_at(const uw_crit_assoc *a, int type, int *page, int *variant);

typedef struct {
    uw_blob file;
    int     first_slot, slot_count;   /* the two header bytes */
    int     entry_count;
    size_t  slot_map_at, entries_at;
} uw_crit_page;

/* `path` is a full CRnnPAGE.Nxx path. False when the file is too short for
 * the sections its own header describes. */
bool uw_crit_page_open(uw_crit_page *p, const char *path);
void uw_crit_page_close(uw_crit_page *p);
/* The entry index for slot `i` of this file's range, or -1 for an empty
 * slot (0xff in the map). */
int  uw_crit_page_slot(const uw_crit_page *p, int i);
/* Frame number `f` of entry `e`, or -1 for an unused frame slot. */
int  uw_crit_page_frame(const uw_crit_page *p, int e, int f);

/* Builds "CRnnPAGE.Nxx" into `out` -- nn OCTAL, xx DECIMAL, which is the
 * one piece of this format a reader gets wrong silently: pages 8..31 land
 * on plausible filenames either way and only the octal ones exist. */
void uw_crit_page_filename(int page, int file, char *out, size_t cap);

#endif
