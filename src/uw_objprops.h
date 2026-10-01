/* SPDX-License-Identifier: MIT */
/* OBJECTS.DAT -- the per-type property tables the engine consults for every
 * object in the world.
 *
 * The file has NO
 * DIRECTORY: a two-byte header and then nine sections back to back, so a
 * section's offset is the sum of everything before it. The layout is not
 * inferred from the bytes -- it is read out of `obj_properties_load`,
 * which copies an eight-entry table of far pointers from the data segment
 * and calls each non-null entry to fread its own section.
 *
 * THE ITEM RANGES ARE A SEPARATE QUESTION from the file layout and do NOT
 * tile. They come from the accessor side, `obj_props_for_object`,
 * which computes `class = (item_id & 0x1c0) >> 6` and tail-calls
 * accessor_table[class]; each accessor sub-dispatches on `(item_id & 0x30)
 * >> 4` and indexes by `item_id & 0x0f`. Items 0xa0..0xaf and 0xc0..0x19f
 * have no property record at all.
 */
#ifndef UW_OBJPROPS_H
#define UW_OBJPROPS_H

#include "uw.h"

typedef enum {
    UW_SEC_MELEE = 0,      /* 16 x 8   items 0x000..0x00f */
    UW_SEC_MISSILE,        /* 16 x 3   items 0x010..0x01f */
    UW_SEC_ARMOUR,         /* 32 x 4   items 0x020..0x03f */
    UW_SEC_CRITTER,        /* 64 x 48  items 0x040..0x07f */
    UW_SEC_CONTAINER,      /* 16 x 3   items 0x080..0x08f */
    UW_SEC_LIGHT,          /* 16 x 2   items 0x090..0x09f */
    UW_SEC_FOOD,           /* 16 x 1   items 0x0b0..0x0bf */
    UW_SEC_TRIGGER,        /* 16 x 1   items 0x1a0..0x1af */
    UW_SEC_ANIMATION,      /* 16 x 4   items 0x1c0..0x1cf */
    UW_SEC_COUNT
} uw_objprop_section;

typedef struct {
    uint32_t offset;       /* into the file, after the 2-byte header */
    int      count, stride;
    uint16_t first_item;
    const char *name;
} uw_objprop_layout;

extern const uw_objprop_layout uw_objprop_sections[UW_SEC_COUNT];

#define UW_OBJPROPS_SIZE   3554     /* the sections' sizes, and the file's */
#define UW_CRITTER_COUNT   64
#define UW_CRITTER_STRIDE  0x30

/* Byte offsets inside a critter record. Only the fields
 * this port needs are named here; the rest are reachable through
 * uw_critter(). */
#define UW_CRITTER_MAX_HP     0x04
#define UW_CRITTER_ATTACKS    0x13   /* 3 x (score, damage, probability%) */
#define UW_CRITTER_XP         0x28   /* uint16 */
#define UW_CRITTER_CONST_2F   0x2f   /* 0x49 in every record */
#define UW_CRITTER_1D         0x1d   /* high nibble constant 8 when populated */

/* missile_props record, three bytes. */
#define UW_MISSILE_MAGNITUDE  0x00
#define UW_MISSILE_SPEED      0x01
#define UW_MISSILE_CLASS      0x02   /* 0 <= v < 0x10 marks a LAUNCHER */

typedef struct {
    uw_blob file;
} uw_objprops;

bool uw_objprops_open(uw_objprops *p, const char *path);
void uw_objprops_close(uw_objprops *p);

/* The section's first byte, and its length in *bytes*, or NULL. */
const uint8_t *uw_objprop_section_data(const uw_objprops *p,
                                       uw_objprop_section s, size_t *len);
/* Record `i` of a section, or NULL when `i` is out of range. */
const uint8_t *uw_objprop_record(const uw_objprops *p, uw_objprop_section s,
                                 int i);
/* Critter record for creature type 0..63 (item_id & 0x3f). */
const uint8_t *uw_critter(const uw_objprops *p, int type);
/* A record byte read as signed -- food nutrition is negative for alcohol. */
int uw_signed_byte(uint8_t b);

#endif
