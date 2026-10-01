/* SPDX-License-Identifier: MIT */
/* The object pools as the game mutates them: obj_alloc,
 * obj_free, object_list_insert, object_list_append,
 * object_list_remove and the active mobile roster
 * (active_mobile_add, active_mobile_remove), over a
 * level segment laid out as the draw list addresses it: the level block
 * from +4 (tile_ptr_from_xy(0, 0)), so mobile objects are 27 bytes from
 * +0x4004 and static ones 8 bytes from +0x5b04.
 *
 * THE FREE STACKS' POINTERS ADDRESS THEIR TOP ENTRY. obj_alloc tests
 * `sp < floor` for empty, reads `es:[sp]` and then subtracts 2; obj_free
 * adds 2 and writes. level_block_load sets `sp = floor + depth
 * * 2` from the trailer and level_block_save writes `(sp - floor) / 2`
 * back, so a stack of depth d holds d + 1 indices, not d.
 *
 * The active mobile roster is one byte per mobile index from +0x7b00 to an
 * end pointer, the trailer's first word its length. Removing an index moves
 * the last one into its place.
 *
 * obj_alloc's answer to an empty stack -- obj_reclaim_distant,
 * which frees far-away objects and tries again -- is reached through the
 * `reclaim` hook the motion port installs, since the sweep needs the player
 * and the tile map. Not carried, and counted: debug_camera_show, which
 * obj_free calls when the freed object is the one the camera follows. */
#ifndef UW_OBJPOOL_H
#define UW_OBJPOOL_H

#include "uw.h"

#define UW_POOL_MOBILE_BASE  0x4004
#define UW_POOL_STATIC_BASE  0x5b04
#define UW_POOL_MOBILE_FLOOR 0x7304
#define UW_POOL_STATIC_FLOOR 0x7500
#define UW_POOL_ACTIVE       0x7b00
#define UW_POOL_TRAILER      0x7c04

typedef struct uw_objpool uw_objpool;
struct uw_objpool {
    uint8_t *seg;           /* the level segment, 64K */
    uint16_t mobile_sp;     /* mobile_freelist_sp */
    uint16_t static_sp;     /* static_freelist_sp */
    uint16_t active_end;    /* the active mobile roster's end */
    long     not_carried;   /* a reclaim or a camera show the port skipped */
    /* obj_reclaim_distant, which obj_alloc runs on an empty stack before it
     * gives up. It needs the player and the tile map, which are not in this
     * layer, so the motion port installs it; without one an exhausted pool
     * counts the call and returns 0. It must leave the depths in `p`. */
    int    (*reclaim)(void *user, uw_objpool *p, int margin, int max);
    void    *reclaim_user;
};

/* level_block_load's three pointers from the trailer at +0x7c04. */
void uw_objpool_attach(uw_objpool *p, uint8_t *seg);
/* level_block_save's trailer: the roster's length, both depths, the magic. */
void uw_objpool_store(const uw_objpool *p);

/* obj_alloc(mobile): the object's offset in the segment, or 0 when its stack
 * is empty. The record is not cleared. A mobile joins the active roster. */
uint16_t uw_obj_alloc(uw_objpool *p, int mobile);
/* obj_free: push the index; a mobile leaves the roster. */
void     uw_obj_free(uw_objpool *p, uint16_t obj);

/* object_create: obj_alloc, then the general record reset --
 * the item id, z and heading 0, the fine position (3, 3), quality 0x28, no
 * next, owner 0 -- and, for a class-0 or class-2 item (COMOBJ.DAT byte 3's
 * top two bits), the quantity flag with a count of one. `comobj` is the
 * 11-byte records obj_properties points at (COMOBJ.DAT past its header). */
uint16_t uw_object_create(uw_objpool *p, uint16_t item_id, int mobile,
                          const uint8_t *comobj);

/* obj_index_from_ptr and obj_ptr_from_index over the segment. */
uint16_t uw_obj_index(uint16_t obj);
uint16_t uw_obj_at(uint16_t index);
/* The link word of tile (x, y): tile_ptr_from_xy + 2. */
uint16_t uw_tile_link(int x, int y);

/* The chain operations, on the word at `link` whose top ten bits name the
 * first object (a tile's +2, an object's +4 `next` or +6 `link`). */
void uw_object_list_insert(uw_objpool *p, uint16_t link, uint16_t obj);
void uw_object_list_append(uw_objpool *p, uint16_t link, uint16_t obj);
void uw_object_list_remove(uw_objpool *p, uint16_t link, uint16_t obj);

#endif
