/* SPDX-License-Identifier: MIT */
#include "uw_level.h"
#include <stdlib.h>
#include <string.h>

static void decode_tile(uint32_t w, uw_tile *t) {
    t->type      = (uint8_t)(w & 0xF);
    t->floor     = (uint8_t)((w >> 4) & 0xF);
    t->floor_tex = (uint8_t)((w >> 10) & 0xF);
    t->no_magic  = (w >> 14) & 1;          /* NOT bit 9, and NOT "door" */
    t->wall_tex  = (uint8_t)((w >> 16) & 0x3F);
    t->first_obj = (uint16_t)((w >> 22) & 0x3FF);
}

bool uw_level_load(uw_level *l, const uw_ark *a, int level) {
    const uint8_t *blk;
    size_t n = uw_ark_block(a, level, &blk);
    return uw_level_from_block(l, blk, n);
}

bool uw_level_from_block(uw_level *l, const uint8_t *blk, size_t n) {
    if (!blk || n < UW_BLOCK_SIZE) return false;
    memset(l, 0, sizeof *l);
    memcpy(l->raw, blk, UW_BLOCK_SIZE);
    for (int y = 0; y < UW_TILES; y++)
        for (int x = 0; x < UW_TILES; x++)
            decode_tile(uw_u32(l->raw + ((size_t)y * UW_TILES + x) * 4),
                        &l->tile[y][x]);
    l->active_count      = uw_u16(l->raw + UW_TRAILER + 0);
    l->mobile_free_depth = uw_u16(l->raw + UW_TRAILER + 2);
    l->static_free_depth = uw_u16(l->raw + UW_TRAILER + 4);
    l->magic             = uw_u16(l->raw + UW_TRAILER + 6);
    return true;
}

bool uw_object_is_mobile(int index) { return index < UW_MOBILES; }

void uw_object_get(const uw_level *l, int index, uw_object *o) {
    memset(o, 0, sizeof *o);
    if (index < 0 || index >= UW_OBJECTS) return;
    /* obj_ptr_from_index: mobiles at base + index*0x1b below
     * 0x100, statics at static_base + index*8 - 0x800 above. The two regions
     * abut exactly -- 0x4000 + 256*27 = 0x5b00, 0x5b00 + 768*8 = 0x7300 --
     * which is the arithmetic that proves the strides differ. */
    const uint8_t *p = index < UW_MOBILES
        ? l->raw + 0x4000 + (size_t)index * UW_MOBILE_SIZE
        : l->raw + 0x5B00 + (size_t)(index - UW_MOBILES) * UW_STATIC_SIZE;
    uint16_t w0 = uw_u16(p), w1 = uw_u16(p + 2);
    uint16_t w2 = uw_u16(p + 4), w3 = uw_u16(p + 6);
    o->item_id     = w0 & 0x1FF;
    o->flags       = (uint8_t)((w0 >> 9) & 7);
    o->enchanted   = (w0 >> 12) & 1;
    o->door_dir    = (w0 >> 13) & 1;
    o->invisible   = (w0 >> 14) & 1;
    o->is_quantity = (w0 >> 15) & 1;
    o->z       = (uint8_t)(w1 & 0x7F);
    o->heading = (uint8_t)((w1 >> 7) & 7);
    o->y       = (uint8_t)((w1 >> 10) & 7);
    o->x       = (uint8_t)((w1 >> 13) & 7);
    o->quality = (uint8_t)(w2 & 0x3F);
    o->next    = (uint16_t)((w2 >> 6) & 0x3FF);
    o->owner   = (uint8_t)(w3 & 0x3F);
    o->link    = (uint16_t)((w3 >> 6) & 0x3FF);
}


int uw_objcheck(const uw_level *l, uw_objcheck_result *out) {
    /* objcheck_run, the debug validator UW ships and wires up
     * only through `objcheck_quiet`. It callocs one counter per object index,
     * marks both free lists -- a second mark there is a free list with a
     * duplicate in it -- and then walks all 4,096 tile chains.
     *
     * Ported because it was a TEST, and it is a good independent check on
     * this reader's model of the pools: get the two strides wrong, or ignore
     * the free-stack depths, and indices are double-counted or missed in
     * bulk rather than subtly.
     *
     * WITH ONE ADDITION THE ORIGINAL DOES NOT MAKE, and the difference is
     * the interesting part. The tile walk follows `next` only, so it never
     * reaches what is INSIDE a container or carried by a creature -- those
     * hang off `link`. On the shipped levels that leaves 643 objects
     * unaccounted for, which looks like a broken reader and is not: following
     * `link` transitively from the objects in use (and never when
     * `is_quantity` is set, because then the field is a count and not an
     * index) accounts for every one, and no shipped level has an orphan.
     */
    uint8_t seen[UW_OBJECTS];
    memset(seen, 0, sizeof seen);
    if (out) memset(out, 0, sizeof *out);

    /* From sp down to the floor INCLUSIVE: the trailer's
     * depth is (sp - floor) / 2 and sp addresses the top live entry, so a
     * stack of depth d holds d + 1 indices. */
    int depth = (int16_t)l->mobile_free_depth;    /* -1 is an empty stack */
    for (int i = 0; i <= depth && i < 254; i++) {
        uint16_t idx = uw_u16(l->raw + UW_MOBILE_FREE + (size_t)i * 2);
        if (idx < UW_OBJECTS && seen[idx] < 255) seen[idx]++;
    }
    depth = (int16_t)l->static_free_depth;
    for (int i = 0; i <= depth && i < UW_STATICS; i++) {
        uint16_t idx = uw_u16(l->raw + UW_STATIC_FREE + (size_t)i * 2);
        if (idx < UW_OBJECTS && seen[idx] < 255) seen[idx]++;
    }
    for (int y = 0; y < UW_TILES; y++)
        for (int x = 0; x < UW_TILES; x++) {
            int idx = l->tile[y][x].first_obj;
            int steps = 0;
            while (idx && idx < UW_OBJECTS && steps++ <= UW_OBJECTS) {
                uw_object o;
                if (seen[idx] < 255) seen[idx]++;
                uw_object_get(l, idx, &o);
                idx = o.next;
            }
        }

    /* Duplicates are the corruption the original reports. Count them before
     * the `link` pass, because that pass marks and would hide one. */
    int dup = 0;
    for (int i = 2; i < UW_OBJECTS; i++)
        if (seen[i] > 1) dup++;

    /* Now the contents. A worklist rather than recursion: a container may
     * hold a container, and the depth is not bounded by anything the file
     * promises. */
    /* From the objects in use only: a free object's `link` is whatever it
     * held when it was freed. */
    int stack[UW_OBJECTS], sp = 0;
    for (int i = 2; i < UW_OBJECTS; i++)
        if (seen[i] && !uw_level_is_free(l, i)) stack[sp++] = i;
    while (sp) {
        uw_object o;
        uw_object_get(l, stack[--sp], &o);
        if (o.is_quantity) continue;      /* the field is a count, not a link */
        int idx = o.link;
        int steps = 0;
        while (idx && idx < UW_OBJECTS && !seen[idx] && steps++ <= UW_OBJECTS) {
            uw_object c;
            seen[idx]++;
            if (sp < UW_OBJECTS) stack[sp++] = idx;
            uw_object_get(l, idx, &c);
            idx = c.next;                  /* contents are a chain */
        }
    }

    int orphan = 0;
    for (int i = 2; i < UW_OBJECTS; i++)
        if (!seen[i]) {
            if (out && out->n_orphans < UW_OBJCHECK_ORPHANS)
                out->orphan[out->n_orphans++] = (uint16_t)i;
            orphan++;
        }
    if (out) { out->duplicates = dup; out->orphans = orphan; }
    return dup;
}

uint8_t uw_mobile_byte(const uw_level *l, int index, int off) {
    if (index < 0 || index >= UW_MOBILES) return 0;
    if (off < 0 || off >= UW_MOBILE_SIZE) return 0;
    return l->raw[0x4000 + (size_t)index * UW_MOBILE_SIZE + (size_t)off];
}

bool uw_level_is_free(const uw_level *l, int index) {
    /* A depth is `(sp - floor) / 2` stored in a word: -1 is empty. */
    for (int i = 0; i <= (int16_t)l->mobile_free_depth && i < 254; i++)
        if (uw_u16(l->raw + UW_MOBILE_FREE + (size_t)i * 2) == index)
            return true;
    for (int i = 0; i <= (int16_t)l->static_free_depth && i < UW_STATICS; i++)
        if (uw_u16(l->raw + UW_STATIC_FREE + (size_t)i * 2) == index)
            return true;
    return false;
}
