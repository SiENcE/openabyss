/* SPDX-License-Identifier: MIT */
/* the container stack -- the
 * C runtime's far heap, the open containers' nodes, the page and its
 * paging, open_container, inventory_examine and paperdoll_click.
 *
 * One of src/uw_motion*.c: see uw_motion.h and uw_motion_int.h. */
#include "uw_motion_int.h"

/* ---- the container stack ---------------------------------------------- */

enum {
    CONTAINER_STACK_ROOT = 0x1722,  /* far: the first container opened */
    CONTAINER_PANEL_BG   = 0x5a5a,  /* the other set of element backgrounds */
    GAME_MODE_MASK       = 0x565e,
    BRKLVL               = 0x00a4,  /* far: the C runtime's break */
    HEAPBASE             = 0x00a0,
    HEAPTOP              = 0x00a8,
    PSP_SEG              = 0x0092,
    DOS_CHUNKS           = 0x1f0e   /* the 64-paragraph chunks DOS has given the program */
};

/* The bytes at a far address when they lie in the level segment's window --
 * the far heap grows past the block the level was loaded into,
 * so what farmalloc hands out after it is there -- or NULL. */
uint8_t *far_bytes(uw_motion *m, uint16_t off, uint16_t seg, uint16_t n) {
    uint32_t base = (uint32_t)rw(m->ds, 0x3122) * 16, at = (uint32_t)seg * 16 + off;
    if (at < base || at + n > base + 0x10000) return NULL;
    return m->lseg + (at - base);
}

/* rt_brk_far of a linear address, as rt_sbrk_far
 * makes its move too: refused below heapbase or past heaptop; the FBRK
 * module's helper then counts the 64-paragraph chunks the new
 * break needs from the PSP and, when they are not the chunks DOS has given,
 * asks DOS to set the program's block to them (dos_setblock),
 * clamped at the memory's top (heaptop's segment) -- which DOS grants, the
 * program's block being the last in memory. The chunks recorded, brklvl set,
 * normalised. 1 when the break moved. */
static int rt_brk_far(uw_motion *m, uint32_t to) {
    uint8_t *ds = m->ds;
    uint16_t chunks;
    if (to < (uint32_t)rw(ds, (uint16_t)(HEAPBASE + 2)) * 16 + rw(ds, HEAPBASE)
        || to > (uint32_t)rw(ds, (uint16_t)(HEAPTOP + 2)) * 16 + rw(ds, HEAPTOP))
        return 0;
    chunks = (uint16_t)((uint16_t)((uint16_t)(to >> 4) - rw(ds, PSP_SEG) + 0x40) >> 6);
    if (chunks != rw(ds, DOS_CHUNKS)) {
        uint16_t paras = (uint16_t)(chunks * 0x40);
        if (rw(ds, (uint16_t)(HEAPTOP + 2)) < (uint16_t)(paras + rw(ds, PSP_SEG)))
            paras = (uint16_t)(rw(ds, (uint16_t)(HEAPTOP + 2)) - rw(ds, PSP_SEG));
        ww(ds, DOS_CHUNKS, (uint16_t)(paras >> 6));
    }
    ww(ds, BRKLVL, (uint16_t)(to & 0xf));
    ww(ds, (uint16_t)(BRKLVL + 2), (uint16_t)(to >> 4));
    return 1;
}

/* The open containers' running weights (+0xa), from the top node back along
 * +4, each less `w` -- more, for a negated one. */
void container_stack_less_weight(uw_motion *m, uint16_t w) {
    uint16_t off = rw(m->ds, CONTAINER_STACK_TOP), seg = rw(m->ds, (uint16_t)(CONTAINER_STACK_TOP + 2));
    while (off | seg) {
        uint8_t *node = far_bytes(m, off, seg, 0xc);
        if (!node) {
            UW_NOT_CARRIED(m->not_carried);
            return;
        }
        ww(node, 0xa, (uint16_t)(rw(node, 0xa) - w));
        off = rw(node, 4);
        seg = rw(node, 6);
    }
}

/* farmalloc(size), Turbo C++ 1.01's (FARHEAP), with its words
 * in the runtime library's code segment (uw_motion.farheap): a block of
 * (size + 0x13) >> 4 paragraphs, the four-byte header its paragraphs and the
 * segment of the block before it, the pointer returned at +4. With no free
 * block (__rover 0) the heap grows at the break, rt_sbrk_far
 * moving it on by the block; the header written at the old break, which
 * becomes __last. The heap's first block and a free list to search are
 * counted, and give 0, as a header outside the window is counted. */
static uint32_t farmalloc(uw_motion *m, uint32_t size) {
    uint8_t *ds = m->ds, *hdr;
    uint16_t paras, seg;
    if (!size || size + 0x13 > 0xfffff) return 0;
    paras = (uint16_t)((size + 0x13) >> 4);
    if (!m->farheap_given || !m->farheap[0] || m->farheap[2]) {
        UW_NOT_CARRIED(m->not_carried);
        return 0;
    }
    seg = rw(ds, (uint16_t)(BRKLVL + 2));
    if (!rt_brk_far(m, (uint32_t)seg * 16 + rw(ds, BRKLVL) + (uint32_t)paras * 16)) return 0;
    hdr = far_bytes(m, 0, seg, 4);
    if (hdr) {
        ww(hdr, 0, paras);
        ww(hdr, 2, m->farheap[1]);
    } else {
        UW_NOT_CARRIED(m->not_carried);
    }
    m->farheap[1] = seg;
    return (uint32_t)seg << 16 | 4;
}

/* farfree(off, seg): the heap's last block (__last) given back
 * to the break when the block before it is in use -- its +2, the
 * segment before it, not 0 -- which becomes __last, and rt_brk_far moves the
 * break down to the freed block's segment. A block before the last going onto
 * the free list, the last block with a free one before it taken
 * back too, and the heap's only block emptying it are counted. */
static void farfree(uw_motion *m, uint16_t off, uint16_t seg) {
    uint8_t *hdr, *prev = NULL;
    (void)off;
    if (!seg) return;
    if (!m->farheap_given || seg != m->farheap[1] || seg == m->farheap[0]) {
        UW_NOT_CARRIED(m->not_carried);
        return;
    }
    hdr = far_bytes(m, 0, seg, 4);
    if (hdr) prev = far_bytes(m, 0, rw(hdr, 2), 4);
    if (!prev || !rw(prev, 2)) {
        UW_NOT_CARRIED(m->not_carried);
        return;
    }
    m->farheap[1] = rw(hdr, 2);
    rt_brk_far(m, (uint32_t)seg * 16);
}

/* container_view_refresh: under a hidden cursor the eight boxes
 * (elements 0x0c..0x13) redrawn; the page can go back when its
 * first slot (0x14) is not the open container's first visible thing (word 0
 * bit 14 clear), and forward when its last (0x1b) holds one; the two
 * arrow buttons (0x15, 0x16) redrawn. */
void container_view_refresh(uw_motion *m) {
    uint8_t *ds = m->ds, *ls = m->lseg;
    uint16_t p;
    cursor_hide(m);
    inventory_panel_redraw(m, 0xc, 0x13);
    p = deref_link(m, (uint16_t)(inventory_slot_object(m, 0x13) + 6));
    while (p && (rw(ls, p) & 0x4000)) p = deref_link(m, (uint16_t)(p + 4));
    ds[CONTAINER_CAN_PAGE_BACK] = obj_index_of(m, p) != ((rw(ds, (uint16_t)(INVENTORY_SLOTS + 0x14 * 2)) >> 6) & 0x3ff);
    ds[CONTAINER_CAN_PAGE_FORWARD] = ((rw(ds, (uint16_t)(INVENTORY_SLOTS + 0x1b * 2)) >> 6) & 0x3ff) != 0;
    inventory_slot_click(m, 0x15);
    inventory_slot_click(m, 0x16);
    cursor_show(m);
}

/* container_set_closed(node): the container a stack node names
 * (+8) stepped back from its open art, an odd nibble below 0xc down one. */
static void container_set_closed(uw_motion *m, const uint8_t *node) {
    uint8_t *ls = m->lseg;
    uint16_t obj = obj_at(m, (uint16_t)((rw(node, 8) >> 6) & 0x3ff)), w0 = rw(ls, obj);
    if ((w0 & 0xf) < 0xc && (w0 & 1)) ww(ls, obj, (uint16_t)((w0 & 0xfff0) | (((w0 & 0xf) - 1) & 0xf)));
}

/* container_stack_clear: from the top node back along +4, each
 * container closed and its node freed (farfree); root and top cleared. The
 * panel is left as it is. */
static void container_stack_clear(uw_motion *m) {
    uint8_t *ds = m->ds, *node;
    uint16_t poff, pseg;
    if (!(rw(ds, CONTAINER_STACK_TOP) | rw(ds, (uint16_t)(CONTAINER_STACK_TOP + 2)))) return;
    for (;;) {
        node = far_bytes(m, rw(ds, CONTAINER_STACK_TOP), rw(ds, (uint16_t)(CONTAINER_STACK_TOP + 2)), 0xc);
        if (!node) {
            UW_NOT_CARRIED(m->not_carried);
            return;
        }
        poff = rw(node, 4);
        pseg = rw(node, 6);
        if (!(poff | pseg)) break;
        container_set_closed(m, node);
        farfree(m, rw(ds, CONTAINER_STACK_TOP), rw(ds, (uint16_t)(CONTAINER_STACK_TOP + 2)));
        ww(ds, CONTAINER_STACK_TOP, poff);
        ww(ds, (uint16_t)(CONTAINER_STACK_TOP + 2), pseg);
    }
    ww(ds, CONTAINER_STACK_ROOT, 0);
    ww(ds, (uint16_t)(CONTAINER_STACK_ROOT + 2), 0);
    container_set_closed(m, node);
    farfree(m, rw(ds, CONTAINER_STACK_TOP), rw(ds, (uint16_t)(CONTAINER_STACK_TOP + 2)));
    ww(ds, CONTAINER_STACK_TOP, 0);
    ww(ds, (uint16_t)(CONTAINER_STACK_TOP + 2), 0);
}

/* container_panel_close: with a container open, the stack
 * cleared and slot 0x13 emptied (its low six bits kept); the eight boxes
 * pointed back at the backpack slots 0xb..0x12 and the two background tables'
 * eight swapped back; under a hidden cursor, in the dungeon or a
 * conversation with the inventory the panel shown, the background saved as
 * element 1's (imgbuf_restore) put back and inventory_panel_container_button;
 * the paging flags cleared and the arrows (0x15, 0x16) redrawn. */
void container_panel_close(uw_motion *m) {
    uint8_t *ds = m->ds;
    int16_t i;
    if (!(rw(ds, CONTAINER_STACK_TOP) | rw(ds, (uint16_t)(CONTAINER_STACK_TOP + 2)))) return;
    container_stack_clear(m);
    ww(ds, (uint16_t)(INVENTORY_SLOTS + 0x13 * 2), (uint16_t)(rw(ds, (uint16_t)(INVENTORY_SLOTS + 0x13 * 2)) & 0x3f));
    for (i = 0xb; i <= 0x12; i++) ds[(uint16_t)(INVENTORY_SEARCH_ORDER + 1 + i)] = (uint8_t)i;
    for (i = 0xc; i <= 0x13; i++) {
        uint16_t t = rw(ds, (uint16_t)(CONTAINER_PANEL_BG + i * 2));
        ww(ds, (uint16_t)(CONTAINER_PANEL_BG + i * 2), rw(ds, (uint16_t)(INVENTORY_PANEL_BG + i * 2)));
        ww(ds, (uint16_t)(INVENTORY_PANEL_BG + i * 2), t);
    }
    cursor_hide(m);
    if ((rw(ds, GAME_MODE_MASK) == 1 || rw(ds, GAME_MODE_MASK) == 4) && !ds[PANEL_MODE]) {
        imgbuf_restore(m, rw(ds, (uint16_t)(INVENTORY_PANEL_BG + 2)));
        inventory_panel_container_button(m);
    }
    cursor_show(m);
    ds[CONTAINER_CAN_PAGE_BACK] = 0;
    ds[CONTAINER_CAN_PAGE_FORWARD] = 0;
    inventory_slot_click(m, 0x15);
    inventory_slot_click(m, 0x16);
}

/* container_page_fill: the page slots 0x14..0x1b refilled from
 * the open container's contents, an invisible thing (word 0 bit 14) written
 * and then written over. With a page slot filled, from the thing the first
 * filled one holds -- nothing changed when the contents no longer hold it.
 * With all eight empty, from the first thing, and then, while things are
 * left, the page moved on four (0x14..0x17 taking 0x18..0x1b's links) and
 * 0x18..0x1b refilled: the last page. */
void container_page_fill(uw_motion *m) {
    uint8_t *ds = m->ds, *ls = m->lseg;
    uint16_t p;
    int16_t si;
#define PAGE_SLOT(i) ((uint16_t)(INVENTORY_SLOTS + (i) * 2))
#define PAGE_PUT(i, o) ww(ds, PAGE_SLOT(i), (uint16_t)((rw(ds, PAGE_SLOT(i)) & 0x3f) | (obj_index_of(m, o) & 0x3ff) << 6))
    for (si = 0x14; si <= 0x1b; si++)
        if ((rw(ds, PAGE_SLOT(si)) >> 6) & 0x3ff) break;
    p = deref_link(m, (uint16_t)(inventory_slot_object(m, 0x13) + 6));
    if (si <= 0x1b) {
        while (inventory_slot_object(m, si) != p) {
            if (!p) {
                UW_NOT_CARRIED(m->not_carried);
                return;
            }
            p = deref_link(m, (uint16_t)(p + 4));
            if (!p) return;
        }
        for (si = 0x14; si <= 0x1b; si++) {
            PAGE_PUT(si, p);
            if (p) {
                if (rw(ls, p) & 0x4000) si--;
                p = deref_link(m, (uint16_t)(p + 4));
            }
        }
        return;
    }
    for (si = 0x14; si <= 0x1b; si++) {
        PAGE_PUT(si, p);
        if (p) {
            if (rw(ls, p) & 0x4000) si--;
            p = deref_link(m, (uint16_t)(p + 4));
        }
    }
    while (p) {
        for (si = 0x14; si < 0x18; si++)
            ww(ds, PAGE_SLOT(si), (uint16_t)((rw(ds, PAGE_SLOT(si)) & 0x3f) | ((rw(ds, PAGE_SLOT(si + 4)) >> 6) & 0x3ff) << 6));
        for (; si <= 0x1b; si++) {
            PAGE_PUT(si, p);
            if (p) {
                if (rw(ls, p) & 0x4000) si--;
                p = deref_link(m, (uint16_t)(p + 4));
            }
        }
    }
#undef PAGE_PUT
#undef PAGE_SLOT
}

/* container_panel_up, the open container's icon clicked: with
 * one open, the root alone closes the panel (container_panel_close); a nested
 * one is closed, its node taken off the top and freed, and the one it was in
 * shown again -- slot 0x13 its node's +8 word whole, the page from its first
 * thing (container_page_fill), container_view_refresh and element 0x14
 * redrawn. */
void container_panel_up(uw_motion *m) {
    uint8_t *ds = m->ds, *node, *top;
    uint16_t off, sg;
    if (!(rw(ds, CONTAINER_STACK_ROOT) | rw(ds, (uint16_t)(CONTAINER_STACK_ROOT + 2)))) return;
    off = rw(ds, CONTAINER_STACK_TOP);
    sg = rw(ds, (uint16_t)(CONTAINER_STACK_TOP + 2));
    node = far_bytes(m, off, sg, 0xc);
    if (!node) {
        UW_NOT_CARRIED(m->not_carried);
        return;
    }
    if (!(rw(node, 4) | rw(node, 6))) {
        container_panel_close(m);
        return;
    }
    container_set_closed(m, node);
    ww(ds, CONTAINER_STACK_TOP, rw(node, 4));
    ww(ds, (uint16_t)(CONTAINER_STACK_TOP + 2), rw(node, 6));
    farfree(m, off, sg);
    top = far_bytes(m, rw(ds, CONTAINER_STACK_TOP), rw(ds, (uint16_t)(CONTAINER_STACK_TOP + 2)), 0xc);
    if (!top) {
        UW_NOT_CARRIED(m->not_carried);
        return;
    }
    ww(top, 0, 0);
    ww(top, 2, 0);
    ww(ds, (uint16_t)(INVENTORY_SLOTS + 0x13 * 2), rw(top, 8));
    ww(ds, (uint16_t)(INVENTORY_SLOTS + 0x14 * 2),
       (uint16_t)((rw(ds, (uint16_t)(INVENTORY_SLOTS + 0x14 * 2)) & 0x3f)
                  | ((rw(m->lseg, (uint16_t)(obj_at(m, (uint16_t)((rw(top, 8) >> 6) & 0x3ff)) + 6)) >> 6) & 0x3ff) << 6));
    container_page_fill(m);
    container_view_refresh(m);
    inventory_panel_redraw(m, 0x14, 0x14);
}

/* container_page_forward, while the page can go forward: the
 * page from slot 0x18's thing -- four on -- refilled and refreshed. */
void container_page_forward(uw_motion *m) {
    uint8_t *ds = m->ds;
    if (!(rw(ds, CONTAINER_STACK_ROOT) | rw(ds, (uint16_t)(CONTAINER_STACK_ROOT + 2))) || !ds[CONTAINER_CAN_PAGE_FORWARD])
        return;
    ww(ds, (uint16_t)(INVENTORY_SLOTS + 0x14 * 2), rw(ds, (uint16_t)(INVENTORY_SLOTS + 0x18 * 2)));
    container_page_fill(m);
    container_view_refresh(m);
}

/* container_page_back, while the page can go back: the contents
 * walked from the first thing in fours, invisible ones counted, keeping the
 * start of the four the page's first thing falls in or after; the page from
 * there, refilled and refreshed. A chain that ends first changes nothing. */
void container_page_back(uw_motion *m) {
    uint8_t *ds = m->ds;
    uint16_t cur, p, group;
    int si;
    if (!(rw(ds, CONTAINER_STACK_ROOT) | rw(ds, (uint16_t)(CONTAINER_STACK_ROOT + 2))) || !ds[CONTAINER_CAN_PAGE_BACK])
        return;
    {
        uint8_t *top = far_bytes(m, rw(ds, CONTAINER_STACK_TOP), rw(ds, (uint16_t)(CONTAINER_STACK_TOP + 2)), 0xc);
        if (!top) {
            UW_NOT_CARRIED(m->not_carried);
            return;
        }
        p = deref_link(m, (uint16_t)(obj_at(m, (uint16_t)((rw(top, 8) >> 6) & 0x3ff)) + 6));
    }
    group = p;
    cur = inventory_slot_object(m, 0x14);
    while (p != cur) {
        group = p;
        for (si = 0; si < 4; si++) {
            p = deref_link(m, (uint16_t)(p + 4));
            if (!p) return;
            if (p == cur) break;
        }
    }
    ww(ds, (uint16_t)(INVENTORY_SLOTS + 0x14 * 2),
       (uint16_t)((rw(ds, (uint16_t)(INVENTORY_SLOTS + 0x14 * 2)) & 0x3f) | (obj_index_of(m, group) & 0x3ff) << 6));
    container_page_fill(m);
    container_view_refresh(m);
}

/* container_panel_open(slot): the container (class 2 subclass
 * 0) in an inventory slot opened in the panel; the rune bag (nibble 0xf) sets
 * panel value 6 to 1 instead. One already on the stack (its +8 walked from
 * the root along +0) closes them all (container_panel_close), and a paperdoll
 * slot (below 0xb) closes what is open first (container_stack_clear). With
 * none open, under a hidden cursor, the container panel's art (0x2097 at 0xec,
 * 0x77) in the dungeon or a conversation (game mode 1 or 4) with the
 * inventory the panel shown; the eight boxes'
 * backgrounds captured off the screen once a session into
 * container_panel_bg -- imgbuf_alloc of the element's size, imgbuf_capture of
 * its rectangle -- then the eight boxes pointed at the page slots 0x14..0x1b
 * and the two background tables' eight swapped. A 12-byte node from
 * farmalloc goes on the stack (the old top's +0 pointing at it when there
 * is one): +0 next, +4 previous, +8 the container's link over bits 0..5 it
 * had, +0xa its
 * contents' weight (object_chain_weight). Slot 0x13 is the container; the
 * page slots take its contents, an invisible thing (word 0 bit 14) written
 * and then written over; a closed container (nibble even, below 0xc) steps to
 * its open art; container_view_refresh, element 0x14 redrawn, and the slot's
 * own element when that is below 0xb. */
static void container_panel_open(uw_motion *m, int16_t slot) {
    uint8_t *ds = m->ds, *ls = m->lseg, *node;
    uint16_t obj = inventory_slot_object(m, slot), w0, link, p, acc;
    uint32_t at;
    int16_t i;
    if (!obj) return;
    w0 = rw(ls, obj);
    if ((w0 & 0x1c0) != 0x80 || (w0 & 0x30)) return;
    if ((w0 & 0xf) == 0xf) {
        panel_set_value(m, 6, 1);
        return;
    }
    link = (uint16_t)((rw(ds, (uint16_t)(INVENTORY_SLOTS + slot * 2)) >> 6) & 0x3ff);
    if (rw(ds, CONTAINER_STACK_ROOT) | rw(ds, (uint16_t)(CONTAINER_STACK_ROOT + 2))) {
        uint16_t off = rw(ds, CONTAINER_STACK_ROOT), sg = rw(ds, (uint16_t)(CONTAINER_STACK_ROOT + 2));
        while (off | sg) {
            node = far_bytes(m, off, sg, 0xc);
            if (!node) {
                UW_NOT_CARRIED(m->not_carried);
                return;
            }
            if (((rw(node, 8) >> 6) & 0x3ff) == link) {
                container_panel_close(m);
                return;
            }
            off = rw(node, 0);
            sg = rw(node, 2);
        }
        if (slot < 0xb) container_stack_clear(m);
        goto push;
    }
    cursor_hide(m);
    if ((rw(ds, GAME_MODE_MASK) == 1 || rw(ds, GAME_MODE_MASK) == 4) && !ds[PANEL_MODE])
        uw_motion_gr_draw_art(m, 0x2097, 0xec, 0x77);
    if (!rw(ds, (uint16_t)(CONTAINER_PANEL_BG + 0xc * 2))) {
        for (i = 0xc; i <= 0x13; i++) {
            uint16_t e = (uint16_t)(INVENTORY_ELEM_PLACE + i * 0xe);
            ww(ds, (uint16_t)(CONTAINER_PANEL_BG + i * 2), imgbuf_alloc(m, ds[(uint16_t)(e + 4)], ds[(uint16_t)(e + 5)]));
            imgbuf_capture_rect(m, rw(ds, (uint16_t)(CONTAINER_PANEL_BG + i * 2)), (int16_t)rw(ds, e),
                                (int16_t)rw(ds, (uint16_t)(e + 2)), ds[(uint16_t)(e + 4)], ds[(uint16_t)(e + 5)]);
        }
    }
    cursor_show(m);
    for (i = 0x14; i <= 0x1b; i++) ds[(uint16_t)(INVENTORY_SEARCH_ORDER - 8 + i)] = (uint8_t)i;
    for (i = 0xc; i <= 0x13; i++) {
        uint16_t t = rw(ds, (uint16_t)(CONTAINER_PANEL_BG + i * 2));
        ww(ds, (uint16_t)(CONTAINER_PANEL_BG + i * 2), rw(ds, (uint16_t)(INVENTORY_PANEL_BG + i * 2)));
        ww(ds, (uint16_t)(INVENTORY_PANEL_BG + i * 2), t);
    }
push:
    at = farmalloc(m, 0xc);
    if (!at) return;
    node = far_bytes(m, (uint16_t)at, (uint16_t)(at >> 16), 0xc);
    if (!node) {
        UW_NOT_CARRIED(m->not_carried);
        return;
    }
    if (!(rw(ds, CONTAINER_STACK_ROOT) | rw(ds, (uint16_t)(CONTAINER_STACK_ROOT + 2)))) {
        ww(ds, CONTAINER_STACK_ROOT, (uint16_t)at);
        ww(ds, (uint16_t)(CONTAINER_STACK_ROOT + 2), (uint16_t)(at >> 16));
        ww(ds, CONTAINER_STACK_TOP, (uint16_t)at);
        ww(ds, (uint16_t)(CONTAINER_STACK_TOP + 2), (uint16_t)(at >> 16));
        ww(node, 4, 0);
        ww(node, 6, 0);
    } else {
        uint8_t *top = far_bytes(m, rw(ds, CONTAINER_STACK_TOP), rw(ds, (uint16_t)(CONTAINER_STACK_TOP + 2)), 0xc);
        if (!top) {
            UW_NOT_CARRIED(m->not_carried);
            return;
        }
        ww(top, 0, (uint16_t)at);
        ww(top, 2, (uint16_t)(at >> 16));
        ww(node, 4, rw(ds, CONTAINER_STACK_TOP));
        ww(node, 6, rw(ds, (uint16_t)(CONTAINER_STACK_TOP + 2)));
        ww(ds, CONTAINER_STACK_TOP, (uint16_t)at);
        ww(ds, (uint16_t)(CONTAINER_STACK_TOP + 2), (uint16_t)(at >> 16));
    }
    ww(node, 0, 0);
    ww(node, 2, 0);
    ww(node, 0xa, 0);
    ww(node, 8, (uint16_t)((rw(node, 8) & 0x3f) | link << 6));
    ww(ds, (uint16_t)(INVENTORY_SLOTS + 0x13 * 2), (uint16_t)((rw(ds, (uint16_t)(INVENTORY_SLOTS + 0x13 * 2)) & 0x3f) | link << 6));
    obj = inventory_slot_object(m, 0x13);
    p = deref_link(m, (uint16_t)(obj + 6));
    acc = rw(node, 0xa);
    object_chain_weight(m, (uint16_t)(obj + 6), &acc);
    ww(node, 0xa, acc);
    for (i = 0x14; i <= 0x1b; i++) {
        uint16_t sl = (uint16_t)(INVENTORY_SLOTS + i * 2);
        ww(ds, sl, (uint16_t)((rw(ds, sl) & 0x3f) | (obj_index_of(m, p) & 0x3ff) << 6));
        if (p) {
            if (rw(ls, p) & 0x4000) i--;
            p = deref_link(m, (uint16_t)(p + 4));
        }
    }
    w0 = rw(ls, obj);
    if ((w0 & 0xf) < 0xc && !(w0 & 1)) ww(ls, obj, (uint16_t)((w0 & 0xfff0) | (((w0 & 0xf) + 1) & 0xf)));
    container_view_refresh(m);
    inventory_panel_redraw(m, 0x14, 0x14);
    if ((int8_t)ds[(uint16_t)(INVENTORY_CLICK_ORDER + slot)] < 0xb)
        inventory_slot_click(m, (int8_t)ds[(uint16_t)(INVENTORY_CLICK_ORDER + slot)]);
}

/* "The <name> is locked.\n", as open_container and use_door_furniture_or_switch
 * print it: three scroll_prints -- "The ", the name
 * (format_object_name without an article, "UNNAMED" when it has
 * none) and " is locked.\n". */
void object_locked_message(uw_motion *m, uint16_t obj) {
    uint8_t *ls = m->lseg;
    char name[0x40], text[0x20];
    if (!format_object_name(m, name, sizeof name, rw(ls, obj), ls[(uint16_t)(obj + 0x1a)], 0, 0))
        strcpy(name, ds_text(m, 0x09fe, text, sizeof text));
    scroll_print(m, ds_text(m, 0x0a2a, text, sizeof text));
    scroll_print(m, name);
    scroll_print(m, ds_text(m, 0x0a2f, text, sizeof text));
}

/* container_empty(obj, verbose): spill_inventory, the owner an
 * ownable thing's (obj_properties +7 bit 7) own +6 low six bits; when
 * nothing spilled and `verbose`, "The ", the name
 * (format_object_name, no article) and " is empty.\n" as one line;
 * post_event(2) either way. The frame is the port's, as spill_inventory's
 * own are. */
void container_empty(uw_motion *m, uint16_t obj, int verbose) {
    uint8_t *ds = m->ds, *ls = m->lseg;
    uint16_t w0 = rw(ls, obj), owner = 0;
    if (ds[(uint16_t)(OBJ_PROPERTIES + (w0 & 0x1ff) * 11 + 7)] & 0x80)
        owner = (uint16_t)(ls[(uint16_t)(obj + 6)] & 0x3f);
    if (!spill_inventory(m, obj, owner, (uint16_t)(FRAME_BP - 0x80)) && verbose) {
        char line[0x50], text[0x20];
        strcpy(line, ds_text(m, 0x0a2a, text, sizeof text));
        format_object_name(m, line + strlen(line), sizeof line - strlen(line), w0, ls[(uint16_t)(obj + 0x1a)], 0, 0);
        strcat(line, ds_text(m, 0x0a3c, text, sizeof text));
        scroll_print(m, line);
    }
    ww(ds, PENDING_EVENTS, (uint16_t)(rw(ds, PENDING_EVENTS) | 2));      /* post_event(2) */
}

/* open_container(actor, obj, from_inventory):
 * door_unlock_attempt with no skill, and a locked one is "The <name> is
 * locked." (object_locked_message). From the inventory it opens in the panel
 * -- container_panel_open of inventory_find_object's slot, a container
 * inside another counted -- and in the world it is emptied
 * (container_empty, verbose when the actor is the player). */
void open_container(uw_motion *m, uint16_t a, uint16_t b, int flag) {
    int16_t slot;
    if (!door_unlock_attempt(m, a, b, 0)) {
        object_locked_message(m, b);
        return;
    }
    if (!flag) {
        container_empty(m, b, a == rw(m->ds, TRACKED_OBJECT));
        return;
    }
    slot = inventory_find_object(m, b);
    if (slot < 0) {
        UW_NOT_CARRIED(m->not_carried);
        return;
    }
    container_panel_open(m, slot);
}

/* inventory_examine, a plain click on an inventory slot: the
 * look trigger over the pick, the slot's object as the pick, and how much
 * the look tells -- word 1 bits 7..9, bit 2 of them meaning identified and
 * the low two how well; without it an appraisal check (the record's +0x29
 * skill at 10) one better, at least 1 and at least what was known, written
 * back with the identified bit. A door, a trigger or a thing whose
 * properties +9 low pair is 2 is simply looked at (1). Then look_at, and
 * paperdoll_click(-1) for the waits it ends with. */
static void inventory_examine(uw_motion *m) {
    uint8_t *ds = m->ds, *ls = m->lseg;
    uint16_t obj = rw(ds, CURSOR_PICK_OBJECT), w0;
    int16_t lore = 1;
    if (obj) trigger_object_link_port(m, rw(ds, TRACKED_OBJECT), obj, 5);   /* trigger_object_link(player, obj, 5, the target tile) */
    if (!obj) {
        obj = inventory_click_take(m);
        ww(ds, CURSOR_PICK_OBJECT, obj);
        ww(ds, (uint16_t)(CURSOR_PICK_OBJECT + 2), obj ? rw(ds, (uint16_t)(MOBILE_BASE + 2)) : 0);
    }
    w0 = rw(ls, obj);
    if ((w0 & 0x1c0) != 0x140 && (w0 & 0x1c0) != 0x180 && (prop(m, (uint16_t)(w0 & 0x1ff), 9) & 3) != 2) {
        uint16_t field = (uint16_t)((rw(ls, (uint16_t)(obj + 2)) & 0x380) >> 7);
        if (field & 4) {
            lore = (int16_t)(field & 3);
        } else {
            lore = (int16_t)(check_skill_roll(m, ds[(uint16_t)(rw(ds, PLAYER_RECORD_PTR) + 0x29)], 10) + 1);
            if (lore == 0) lore = 1;
            if (lore < (int16_t)(field & 3)) lore = (int16_t)(field & 3);
            ww(ls, (uint16_t)(obj + 2),
               (uint16_t)((rw(ls, (uint16_t)(obj + 2)) & 0xfc7f) | (((lore & 7) | 4) << 7)));
        }
    }
    look_at(m, obj, lore);
    paperdoll_click(m, -1);
}

/* paperdoll_click(param), from the instructions, the inventory
 * panel's click through inventory_click_dispatch, for no button down in the
 * pass -- its release waits end at their first poll, and cursor_wait_for_drag
 * answers at once that the click was no drag.
 * The element under the event's position (+0xf0, +0x52): an empty slot with
 * nothing held waits, the weapon hand's after weapon_toggle; a filled one
 * lifts (not carried). Then the wait, the element under the live cursor, and
 * the wait again. Holding something: action_state 1 and, unless in a
 * conversation's barter hold outside the view, a slot takes it
 * (inventory_drop_on_slot) and the view or barter area gets
 * inventory_panel_activate; nothing held, the element's activation
 * (inventory_panel_activate). The hand emptied, the cursor's shape
 * popped and action_state 0. */

void paperdoll_click(uw_motion *m, int16_t param) {
    uint8_t *ds = m->ds;
    uint16_t ev = rw(ds, 0x00e2);
    int held = rw(ds, 0x5b06) || rw(ds, 0x5b08);
    int16_t region = inventory_panel_hit_test(m, (int16_t)(rs(ds, ev) + 0xf0), (int16_t)(rs(ds, (uint16_t)(ev + 2)) + 0x52));
    m->drag_param = param;
    m->drag_param_set = 1;
    if (m->buttons) {
        if (!held && region > 0 && region < 0x15) {
            int16_t slot = (int8_t)ds[(uint16_t)(0x1888 + region)];
            if (slot != -1 && slot != 0x13 && (rw(ds, (uint16_t)(INVENTORY_SLOTS + slot * 2)) >> 6)) {
                /* cursor_wait_for_drag's first sample with the button down:
                 * its loop's passes pair as any, and uw_motion_inventory_drag
                 * runs on from its answer */
                mouse_sample_buttons(m);
                m->drag_wait = 1;
                return;
            }
        }
        /* a button still down: the release wait, whose passes pair as any,
         * and the rest -- uw_motion_inventory_release with something held,
         * uw_motion_inventory_click_end with nothing -- runs on from its end */
        input_wait_button_release(m, 1);
        return;
    }
    if (region > 0 && region < 0x15) {
        int16_t slot = (int8_t)ds[(uint16_t)(0x1888 + region)];
        if (!held && !(rw(ds, (uint16_t)(INVENTORY_SLOTS + slot * 2)) >> 6)) {
            if (8 - (ds[(uint16_t)(rw(ds, PLAYER_RECORD_PTR) + 0x64)] & 1) == slot)
                weapon_toggle(m);
            input_wait_button_release(m, 1);
            return;
        }
        if (!held && slot != -1 && slot != 0x13 && cursor_wait_for_drag(m, 1)) {
            UW_NOT_CARRIED(m->not_carried);
            return;
        }
        input_wait_button_release(m, 1);
        region = inventory_panel_hit_test(m, rs(ds, CURSOR_X), rs(ds, CURSOR_Y));
    }
    paperdoll_click_rest(m, param, region, held);
}

/* paperdoll_click past its release wait: the wait again;
 * nothing held (or a use aimed), the element's look (-2) or activation;
 * something held, action_state 1 and, unless in a conversation's barter hold
 * outside the view, a slot takes it (inventory_drop_on_slot) and the view or
 * barter area gets inventory_panel_activate. Then, `flag` set -- held on
 * entry, or lifted -- and the hand empty, the cursor's shape popped and
 * action_state 0. */
void paperdoll_click_rest(uw_motion *m, int16_t param, int16_t region, int flag) {
    uint8_t *ds = m->ds;
    input_wait_button_release(m, 1);
    if (!(rw(ds, 0x5b06) || rw(ds, 0x5b08)) || rw(ds, ACTION_STATE_WORD) == 2) {
        if (region > 0) {
            if (param < 0) {
                if (param == -2) inventory_examine(m);
            } else {
                inventory_panel_activate(m, region);
                flag = 0;
            }
        }
    } else {
        ww(ds, ACTION_STATE_WORD, 1);
        if (ds[PANEL_MODE] && region != 0x17) return;
        if (region > 0) {
            if (region < 0x15) {
                inventory_drop_on_slot(m, (int8_t)ds[(uint16_t)(0x1888 + region)]);
            } else {
                inventory_panel_activate(m, region);
                flag = 0;
            }
        }
    }
    if (flag && !(rw(ds, 0x5b06) || rw(ds, 0x5b08))) {
        cursor_shape_pop(m, 3);
        ww(ds, ACTION_STATE_WORD, 0);
    }
}

/* inventory_pick_up_from_slot(slot, rest_stays), from the
 * instructions: the slot's object unlinked onto the cursor
 * (inventory_unlink_object, no count) as held_object_ptr; with rest_stays
 * the slot keeps the next object of its chain (and player_state_recalc
 * runs); then under a hidden cursor its image pushed as the cursor's shape
 * -- the shape before popped when something was held already -- and
 * player_state_recalc. */
void inventory_pick_up_from_slot(uw_motion *m, int16_t slot, int rest) {
    uint8_t *ds = m->ds, *ls = m->lseg;
    int was = rw(ds, 0x5b06) || rw(ds, 0x5b08);
    uint16_t next = 0, o;
    if (rest) next = obj_index_of(m, deref_link(m, (uint16_t)(inventory_slot_object(m, slot) + 4)));
    o = inventory_unlink_object(m, -1, -1, -1, slot, 0);
    ww(ds, 0x5b06, o);
    ww(ds, 0x5b08, o ? rw(ds, (uint16_t)(MOBILE_BASE + 2)) : 0);
    if (!o) return;
    if (rest) {
        uint16_t at = (uint16_t)(INVENTORY_SLOTS + slot * 2);
        ww(ds, at, (uint16_t)((rw(ds, at) & 0x3f) | ((next & 0x3ff) << 6)));
        player_state_recalc(m);
    }
    cursor_hide(m);
    if (was) cursor_shape_pop(m, 0);
    cursor_shape_push(m, (uint16_t)(rw(ls, o) & 0x1ff));
    cursor_show(m);
    player_state_recalc(m);
}
