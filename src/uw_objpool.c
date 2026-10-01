/* SPDX-License-Identifier: MIT */
/* See uw_objpool.h. */
#include "uw_objpool.h"

static uint16_t rw(const uint8_t *m, uint16_t at) {
    return (uint16_t)(m[at] | (m[(uint16_t)(at + 1)] << 8));
}
static void ww(uint8_t *m, uint16_t at, uint16_t w) {
    m[at] = (uint8_t)w;
    m[(uint16_t)(at + 1)] = (uint8_t)(w >> 8);
}

void uw_objpool_attach(uw_objpool *p, uint8_t *seg) {
    p->seg = seg;
    p->active_end = (uint16_t)(UW_POOL_ACTIVE + rw(seg, UW_POOL_TRAILER));
    p->mobile_sp = (uint16_t)(UW_POOL_MOBILE_FLOOR + rw(seg, UW_POOL_TRAILER + 2) * 2);
    p->static_sp = (uint16_t)(UW_POOL_STATIC_FLOOR + rw(seg, UW_POOL_TRAILER + 4) * 2);
    p->not_carried = 0;
}

void uw_objpool_store(const uw_objpool *p) {
    /* `rt_ldiv(sp - floor, 2)` on a signed long: an empty stack's sp is the
     * floor less 2, and its depth is stored as -1. */
    ww(p->seg, UW_POOL_TRAILER, (uint16_t)(p->active_end - UW_POOL_ACTIVE));
    ww(p->seg, UW_POOL_TRAILER + 2,
       (uint16_t)(((int32_t)p->mobile_sp - UW_POOL_MOBILE_FLOOR) / 2));
    ww(p->seg, UW_POOL_TRAILER + 4,
       (uint16_t)(((int32_t)p->static_sp - UW_POOL_STATIC_FLOOR) / 2));
    ww(p->seg, UW_POOL_TRAILER + 6, 0x7577);
}

uint16_t uw_obj_index(uint16_t obj) {
    if (obj == 0) return 0;
    if (obj < UW_POOL_STATIC_BASE) return (uint16_t)((obj - UW_POOL_MOBILE_BASE) / 27);
    return (uint16_t)((obj - UW_POOL_STATIC_BASE) / 8 + 0x100);
}

uint16_t uw_obj_at(uint16_t index) {
    if (index == 0) return 0;
    if (index < 0x100) return (uint16_t)(UW_POOL_MOBILE_BASE + index * 27);
    return (uint16_t)(UW_POOL_STATIC_BASE + (index - 0x100) * 8);
}

uint16_t uw_tile_link(int x, int y) {
    return (uint16_t)(4 + (y * 64 + x) * 4 + 2);
}

static void active_add(uw_objpool *p, uint16_t index) {
    p->seg[p->active_end] = (uint8_t)index;          /* one BYTE */
    p->active_end = (uint16_t)(p->active_end + 1);
}

static void active_remove(uw_objpool *p, uint16_t index) {
    uint16_t at;
    for (at = UW_POOL_ACTIVE; at < p->active_end; at++)
        if (p->seg[at] == (uint8_t)index) {
            p->active_end = (uint16_t)(p->active_end - 1);
            if (at < p->active_end) p->seg[at] = p->seg[p->active_end];
            return;
        }
}

uint16_t uw_obj_alloc(uw_objpool *p, int mobile) {
    uint16_t index;
    if (mobile) {
        if (p->mobile_sp < UW_POOL_MOBILE_FLOOR) {    /* `cmp; jae` */
            /* obj_reclaim_distant(3, 5), then the one retry obj_alloc makes */
            if (!p->reclaim) UW_NOT_CARRIED(p->not_carried);
            else p->reclaim(p->reclaim_user, p, 3, 5);
            if (p->mobile_sp < UW_POOL_MOBILE_FLOOR) return 0;
        }
        active_add(p, rw(p->seg, p->mobile_sp));
        index = rw(p->seg, p->mobile_sp);
        p->mobile_sp = (uint16_t)(p->mobile_sp - 2);
        return (uint16_t)(UW_POOL_MOBILE_BASE + index * 27);
    }
    if (p->static_sp < UW_POOL_STATIC_FLOOR) {
        /* obj_reclaim_distant(3, 10), then the retry */
        if (!p->reclaim) UW_NOT_CARRIED(p->not_carried);
        else p->reclaim(p->reclaim_user, p, 3, 10);
        if (p->static_sp < UW_POOL_STATIC_FLOOR) return 0;
    }
    index = rw(p->seg, p->static_sp);
    p->static_sp = (uint16_t)(p->static_sp - 2);
    return (uint16_t)(UW_POOL_STATIC_BASE + (uint16_t)((index - 0x100) << 3));
}

uint16_t uw_object_create(uw_objpool *p, uint16_t item_id, int mobile,
                          const uint8_t *comobj) {
    uint16_t o = uw_obj_alloc(p, mobile), w0, w1, w3;
    uint8_t cls;
    if (!o) return 0;
    p->seg[(uint16_t)(o + 4)] = (uint8_t)((p->seg[(uint16_t)(o + 4)] & 0xc0) | 0x28);
    w0 = (uint16_t)((rw(p->seg, o) & 0xfe00) | (item_id & 0x1ff));
    w1 = (uint16_t)(rw(p->seg, (uint16_t)(o + 2)) & 0xff80);   /* z */
    w0 &= 0xdfff; w0 &= 0xbfff; w0 &= 0xefff; w0 &= 0xf7ff;
    w1 = (uint16_t)((w1 & 0x1fff) | 0x6000);                  /* x in tile 3 */
    w1 = (uint16_t)((w1 & 0xe3ff) | 0x0c00);                  /* y in tile 3 */
    w1 &= 0xfc7f;                                              /* heading */
    w0 &= 0xe1ff;
    ww(p->seg, o, w0);
    ww(p->seg, (uint16_t)(o + 2), w1);
    ww(p->seg, (uint16_t)(o + 4), (uint16_t)(rw(p->seg, (uint16_t)(o + 4)) & 0x3f));
    p->seg[(uint16_t)(o + 6)] &= 0xc0;
    cls = comobj ? (uint8_t)(comobj[(uint32_t)item_id * 11 + 3] >> 6) : 1;
    w3 = rw(p->seg, (uint16_t)(o + 6));
    if (cls == 0 || cls == 2) {
        ww(p->seg, (uint16_t)(o + 6), (uint16_t)((w3 & 0x3f) | 0x40));
        ww(p->seg, o, (uint16_t)(rw(p->seg, o) | 0x8000));
    } else {
        ww(p->seg, o, (uint16_t)(rw(p->seg, o) & 0x7fff));
        ww(p->seg, (uint16_t)(o + 6), (uint16_t)(w3 & 0x3f));
    }
    return o;
}

void uw_obj_free(uw_objpool *p, uint16_t obj) {
    if (obj < UW_POOL_STATIC_BASE) {
        uint16_t index = (uint16_t)((obj - UW_POOL_MOBILE_BASE) / 27);
        p->mobile_sp = (uint16_t)(p->mobile_sp + 2);
        ww(p->seg, p->mobile_sp, index);
        active_remove(p, index);
    } else {
        p->static_sp = (uint16_t)(p->static_sp + 2);
        ww(p->seg, p->static_sp, (uint16_t)((obj - UW_POOL_STATIC_BASE) / 8 + 0x100));
    }
}

void uw_object_list_insert(uw_objpool *p, uint16_t link, uint16_t obj) {
    uint16_t head = (uint16_t)((rw(p->seg, link) >> 6) & 0x3ff);
    ww(p->seg, (uint16_t)(obj + 4),
       (uint16_t)((rw(p->seg, (uint16_t)(obj + 4)) & 0x3f) | (head << 6)));
    ww(p->seg, link, (uint16_t)((rw(p->seg, link) & 0x3f)
                                | ((uw_obj_index(obj) & 0x3ff) << 6)));
}

void uw_object_list_append(uw_objpool *p, uint16_t link, uint16_t obj) {
    uint16_t cur;
    /* obj_deref_link until it gives nothing: `link` ends on the last word. */
    while ((cur = uw_obj_at((uint16_t)(rw(p->seg, link) >> 6))) != 0)
        link = (uint16_t)(cur + 4);
    ww(p->seg, (uint16_t)(obj + 4), (uint16_t)(rw(p->seg, (uint16_t)(obj + 4)) & 0x3f));
    ww(p->seg, link, (uint16_t)((rw(p->seg, link) & 0x3f)
                                | ((uw_obj_index(obj) & 0x3ff) << 6)));
}

void uw_object_list_remove(uw_objpool *p, uint16_t link, uint16_t obj) {
    int steps = 0;
    if (obj == 0) return;
    for (;;) {
        uint16_t cur = uw_obj_at((uint16_t)(rw(p->seg, link) >> 6));
        if (cur == 0) return;
        if (steps++ >= 0x401) return;                 /* `cmp ax,0x401; jl` */
        if (cur == obj) {
            uint16_t next = (uint16_t)((rw(p->seg, (uint16_t)(obj + 4)) >> 6) & 0x3ff);
            ww(p->seg, link, (uint16_t)((rw(p->seg, link) & 0x3f) | (next << 6)));
            ww(p->seg, (uint16_t)(obj + 4),
               (uint16_t)(rw(p->seg, (uint16_t)(obj + 4)) & 0x3f));
            return;
        }
        link = (uint16_t)(cur + 4);
    }
}
