/* SPDX-License-Identifier: MIT */
/* LEV.ARK -- the nine dungeon levels: tile maps and object pools.
 *
 * Two points of the format are easy to get wrong and
 * worth carrying into the C rather than rediscovering:
 *
 *   * bit 14 of the tile word is NO-MAGIC, not "door present", whatever
 *     the published UW format says.
 *     tile_no_magic_bit reads `(byte1 >> 6) & 1`, 216 tiles hold
 *     a door object and not one has the bit, and the 16 that do are two
 *     contiguous clusters on level 6 -- a zone, which is what the name means.
 *   * the two object pools have DIFFERENT STRIDES. 256 mobiles of 27 bytes
 *     then 768 statics of 8, exactly as obj_ptr_from_index computes them.
 *     Read as one array of 1024 8-byte records -- the obvious reading, and
 *     the wrong one -- 214 objects on level 1 alone come out reachable from
 *     more than one tile, which no linked structure can be.
 */
#ifndef UW_LEVEL_H
#define UW_LEVEL_H

#include "uw.h"

#define UW_TILES        64
#define UW_MOBILES      256
#define UW_STATICS      768
#define UW_OBJECTS      (UW_MOBILES + UW_STATICS)
#define UW_MOBILE_SIZE  27
#define UW_STATIC_SIZE  8
#define UW_BLOCK_SIZE   0x7C08
#define UW_TRAILER      0x7C00
#define UW_LEVEL_MAGIC  0x7577

typedef struct {
    uint8_t  type;        /* 0 solid, 1 open, 2..5 diagonal, 6..9 slope */
    uint8_t  floor;       /* height, 0..15 */
    uint8_t  floor_tex;   /* index into this level's texture block */
    uint8_t  wall_tex;
    bool     no_magic;    /* bit 14 */
    uint16_t first_obj;   /* 0 = none */
} uw_tile;

typedef struct {
    uint16_t item_id;     /* word0 bits 0..8 */
    uint8_t  flags;       /* word0 bits 9..11 */
    bool     enchanted, door_dir, invisible, is_quantity;
    uint8_t  z, heading, x, y;           /* word1 */
    uint8_t  quality;                    /* word2 bits 0..5 */
    uint16_t next;                       /* word2 bits 6..15 */
    uint8_t  owner;                      /* word3 bits 0..5 */
    uint16_t link;                       /* word3 bits 6..15 */
} uw_object;

typedef struct {
    uint8_t   raw[UW_BLOCK_SIZE];
    uw_tile   tile[UW_TILES][UW_TILES];
    uint16_t  active_count, mobile_free_depth, static_free_depth, magic;
} uw_level;

/* The three lists in the block's tail, and the trailer that says how much of
 * each is live. All three are stored at FULL WIDTH -- the shipped free stacks
 * look like obj_pool_init's initial fill (2..1023 ascending) whether or not
 * they are in use -- so only the live entries mean anything, and reading
 * them without the depths says nothing at all. A depth is (sp - floor) / 2
 * with sp addressing the TOP live entry, so a stack of depth d holds the
 * d + 1 entries 0..d. */
#define UW_MOBILE_FREE   0x7300      /* 254 uint16, indices 2..255 */
#define UW_STATIC_FREE   0x74FC      /* 768 uint16 */
#define UW_ACTIVE_LIST   0x7AFC      /* one BYTE per active mobile */

#define UW_OBJCHECK_ORPHANS 16

typedef struct {
    int duplicates;   /* an index on a free list AND in a chain, or twice in
                       * one -- the corruption objcheck_run exists to find */
    int orphans;      /* reachable from nothing: not free, not in a tile
                       * chain, not inside any container */
    int n_orphans;
    uint16_t orphan[UW_OBJCHECK_ORPHANS];
} uw_objcheck_result;

/* objcheck_run -- the debug validator UW ships and wires up only
 * through `objcheck_quiet`. Returns the duplicate count; fills `out` if it is
 * non-NULL. See the implementation for the one thing this does that the
 * original does not, and why. */
int uw_objcheck(const uw_level *l, uw_objcheck_result *out);

/* Byte `off` of mobile record `index` (0..255) -- the 19 bytes past the four
 * words every object has. The engine's own names for the ones this port
 * uses: +0x0a bit 7 is the attitude PIN (exempt from every race-wide
 * attitude change), and +0x0d bits 14..15 are the attitude itself, of which
 * 0 is hostile. */
uint8_t uw_mobile_byte(const uw_level *l, int index, int off);
/* True when the index sits on one of the two free stacks, among its
 * depth + 1 live entries. The stacks are stored at FULL WIDTH whether or not they
 * are in use, so reading them without the depths says nothing at all. */
bool uw_level_is_free(const uw_level *l, int index);

/* The .ARK container lives in uw.h -- CNV.ARK uses the same one. */

bool uw_level_load(uw_level *l, const uw_ark *a, int level);   /* level 0..8 */
/* The same from a block in memory (a level segment's, from +4). */
bool uw_level_from_block(uw_level *l, const uint8_t *blk, size_t n);
void uw_object_get(const uw_level *l, int index, uw_object *o);
bool uw_object_is_mobile(int index);

#endif
