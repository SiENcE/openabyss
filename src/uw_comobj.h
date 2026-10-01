/* SPDX-License-Identifier: MIT */
/* COMOBJ.DAT -- the common per-item-type property table.
 *
 * Every object in the world carries a 9-bit item id and this file has one
 * record for each of the 512 possible ids. It is separate from OBJECTS.DAT,
 * which holds the class-specific tables that uw_objprops.h reads.
 *
 *     2-byte header, then 512 records of 11 bytes
 *
 * The file is 5,634 bytes, 2 + 512 * 11 exactly; `obj_properties_load` opens
 * DATA\\comobj.dat and reads 0x1600 bytes after the header, which is
 * 512 x 11, and consumers index with `(obj[0] & 0x1ff) * 0xb`.
 *
 * THE RECORD PAYLOAD STARTS AT 0x5b6e, after the header, not two bytes
 * later: obj_properties_load freads the 2-byte header into a local FIRST
 * and then reads 0x1600 bytes to 0x5b6e, and a field read as
 * `[id * 0xb + 0x5b6e + 3]` is byte 3.
 */
#ifndef UW_COMOBJ_H
#define UW_COMOBJ_H

#include "uw.h"

#define UW_COMOBJ_ITEMS   512
#define UW_COMOBJ_STRIDE  11
#define UW_COMOBJ_HEADER  2
#define UW_COMOBJ_PAYLOAD (UW_COMOBJ_ITEMS * UW_COMOBJ_STRIDE)   /* 0x1600 */

typedef struct {
    uw_blob file;
} uw_comobj;

bool uw_comobj_open(uw_comobj *c, const char *path);
void uw_comobj_close(uw_comobj *c);
/* The 11 bytes for item id 0..511, or NULL. */
const uint8_t *uw_comobj_record(const uw_comobj *c, int item);

/* +0: HEIGHT, in the 1/8-of-a-floor-step units an object's z uses. */
int uw_comobj_height(const uw_comobj *c, int item);
/* +1: a word. Bits 4..15 are the WEIGHT in tenths of a stone -- which the
 * shipped values settle without argument: dagger 8, longsword 24, axe 32,
 * battle axe 40, arrow 1, coin 1, sack 2, key 1, scenery 0. */
int uw_comobj_weight(const uw_comobj *c, int item);
/* Bit 3 of the same word: the object has a PHYSICAL SHAPE, and bits 0..2 are
 * its RADIUS in eighths of a tile. item_fits_in_tile writes that
 * radius and byte +0's height into the spatial query as a bounding cylinder.
 * Bits 0..2 were once called a weapon-animation group because
 * combat_select_weapon reads them; it reads the same radius. */
bool uw_comobj_has_shape(const uw_comobj *c, int item);
int  uw_comobj_radius(const uw_comobj *c, int item);
/* +3 bit 1: THE OBJECT RAISES THE WALKABLE FLOOR -- traverse_multiple_tiles
 * does `raised = ((obj.z & 0x7f) + height) >> 3` when it is set. The fifteen
 * ids that have it are bench, three boulders, shrine, table, beam, moongate,
 * barrel, chair, chest, nightstand, lotus, bridge: every one a thing you can
 * stand on, and NOT the door. Doors block through a separate walk. */
bool uw_comobj_raises_floor(const uw_comobj *c, int item);
/* +3 bits 6..7: the storage class object_create dispatches on. Classes 0 and
 * 2 get the word-0 is_quantity bit and a quantity of 1; class 2 is dead in
 * the shipped table. */
int  uw_comobj_storage_class(const uw_comobj *c, int item);
/* +7 bit 7: the item type is OWNABLE -- the object's own word3 low six bits
 * then name its owner, and taking it makes report_crime anger that owner's
 * race. The rest of the byte is undecoded. */
bool uw_comobj_ownable(const uw_comobj *c, int item);

#endif
