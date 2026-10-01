/* SPDX-License-Identifier: MIT */
/* the spatial query: the 3x3 of
 * tiles around a point, their corners and slopes, the objects gathered
 * and sorted, collision_check and pick_support.
 *
 * One of src/uw_motion*.c: see uw_motion.h and uw_motion_int.h. */
#include "uw_motion_int.h"

/* ==== the spatial query ============================================== */

static uint16_t sqp(uw_motion *m) { return rw(m->ds, SQ_PTR); }
static uint8_t sqb(uw_motion *m, int off) { return m->ds[(uint16_t)(sqp(m) + off)]; }
static void sqbset(uw_motion *m, int off, uint8_t v) { m->ds[(uint16_t)(sqp(m) + off)] = v; }
static int16_t sqw(uw_motion *m, int off) { return rs(m->ds, (uint16_t)(sqp(m) + off)); }
static void sqwset(uw_motion *m, int off, uint16_t v) { ww(m->ds, (uint16_t)(sqp(m) + off), v); }
static uint8_t *corner(uw_motion *m, int c) { return m->ds + (uint16_t)(SQ_CORNERS + c * 5); }

/* tile_ptr_from_xy: the tile's offset in the level segment,
 * or 0 off the map. */
uint16_t tile_ptr(uw_motion *m, uint16_t x, uint16_t y) {
    if (((x & 0xffc0) + (y & 0xffc0)) & 0xffff) return 0;
    return (uint16_t)(rw(m->ds, TILEMAP_PTR) + (x + y * 64) * 4);
}

/* A tile's cached word: its first byte, and its floor texture's kind << 4. */
static uint16_t tile_word(uw_motion *m, uint16_t t) {
    uint8_t t0 = m->lseg[t], t1 = m->lseg[(uint16_t)(t + 1)];
    return (uint16_t)(t0 + ((rw(m->ds, (uint16_t)(TERRAIN_KINDS + ((t1 >> 2) & 0xf) * 2)) & 0xff) << 4));
}

/* tile_height_at_corner */
static uint8_t height_at_corner(uw_motion *m, int c, int8_t *diag) {
    uint8_t *r = corner(m, c);
    uint16_t w = rw(m->ds, (uint16_t)(SQ_TILE_WORDS + r[0] * 2));
    uint8_t cl = (uint8_t)((w & 0xf0) >> 1), fx = r[1], fy = r[2];
    *diag = 0;
    switch (w & 0xf) {
    case 0: cl = 0x80; break;
    case 1: break;
    case 2: if (!(fy < fx)) cl = 0x80; *diag = 1; break;
    case 3: if (!(fx + fy < 7)) cl = 0x80; *diag = 1; break;
    case 4: if (!(fx + fy > 7)) cl = 0x80; *diag = 1; break;
    case 5: if (!(fy > fx)) cl = 0x80; *diag = 1; break;
    case 6: cl = (uint8_t)(cl + (fy & 7)); break;
    case 7: cl = (uint8_t)(cl + (7 - (fy & 7))); break;
    case 8: cl = (uint8_t)(cl + (fx & 7)); break;
    case 9: cl = (uint8_t)(cl + (7 - (fx & 7))); break;
    default: break;
    }
    return cl;
}

/* tile_slope_height */
uint16_t tile_slope_height(uw_motion *m, int16_t x, int16_t y) {
    uint16_t w = rw(m->ds, (uint16_t)(SQ_TILE_WORDS + 8));
    int16_t dx = (int16_t)(x & 0xff), dy = (int16_t)(y & 0xff), si = 0;
    switch ((int)(w & 0xf) - 6) {
    case 0: si = dy; break;
    case 1: si = (int16_t)(0xff - dy); break;
    case 2: si = dx; break;
    case 3: si = (int16_t)(0xff - dx); break;
    default: break;
    }
    return (uint16_t)((si >> 2) + ((w & 0xf0) << 2));
}

/* spatial_query_classify_corner */
static int classify_corner(uw_motion *m, int c, uint8_t tol) {
    int8_t diag;
    uint8_t h = height_at_corner(m, c, &diag);
    uint16_t si = 0;
    int16_t z = sqw(m, 4);
    if (h == 0x80) si |= 0x200;
    else if (z + tol < h) si |= 0x100;
    else if (z - tol > h) si |= 0x800;
    else si |= (uint16_t)(8 << ((rw(m->ds, (uint16_t)(SQ_TILE_WORDS + corner(m, c)[0] * 2)) & 0x300) >> 8));
    corner(m, c)[3] = (uint8_t)si;
    corner(m, c)[4] = (uint8_t)(si >> 8);
    if (sqb(m, 0x11) < h) sqbset(m, 0x11, h);
    return diag == 0;
}

/* spatial_query_classify_centre */
static int classify_centre(uw_motion *m, uint8_t tol) {
    uint16_t w = rw(m->ds, (uint16_t)(SQ_TILE_WORDS + 8));
    int8_t diag;
    uint8_t h;
    int16_t z = sqw(m, 4);
    sqwset(m, 0xc, (uint16_t)((w & 0x300) >> 8));
    h = height_at_corner(m, 4, &diag);
    sqbset(m, 0x10, h);
    if (h == 0x80) sqwset(m, 0xc, (uint16_t)(sqw(m, 0xc) | 0x200));
    else if (z + tol < h) sqwset(m, 0xc, (uint16_t)(sqw(m, 0xc) | 0x100));
    else if (z - tol > h) sqwset(m, 0xc, (uint16_t)(sqw(m, 0xc) | 0x800));
    else sqwset(m, 0xc, (uint16_t)(sqw(m, 0xc) | 4 | (8 << ((w & 0x300) >> 8))));
    if ((w & 0xf) >= 6) sqwset(m, 0xc, (uint16_t)(sqw(m, 0xc) | 0x2000));
    return diag == 0;
}

/* spatial_query_terrain */
void sq_terrain(uw_motion *m, uint8_t tol) {
    uint8_t *ds = m->ds;
    uint16_t tp;
    int16_t fx, fy;
    int8_t idx = 4;
    int c;
    memset(ds + SQ_TILE_WORDS, 0x11, 0x12);
    tp = tile_ptr(m, (uint16_t)(sqw(m, 0) >> 3), (uint16_t)(sqw(m, 2) >> 3));
    if (!tp) UW_NOT_CARRIED(m->not_carried);
    ww(ds, SQ_TILE_PTR, tp);
    fx = (int16_t)(sqw(m, 0) & 7);
    fy = (int16_t)(sqw(m, 2) & 7);
    corner(m, 4)[0] = (uint8_t)idx;
    corner(m, 4)[1] = (uint8_t)fx;
    corner(m, 4)[2] = (uint8_t)fy;
    if (rw(ds, (uint16_t)(SQ_TILE_WORDS + 8)) == 0x1111)
        ww(ds, (uint16_t)(SQ_TILE_WORDS + 8), tile_word(m, tp));
    classify_centre(m, tol);
    sqwset(m, 0xe, (uint16_t)sqw(m, 0xc));
    sqbset(m, 0x11, sqb(m, 0x10));
    if (sqb(m, 8) == 0) return;
    fy = (int16_t)(fy - sqb(m, 8));
    while (fy < 0) { idx = (int8_t)(idx - 3); fy += 8; }
    fx = (int16_t)(fx - sqb(m, 8));
    while (fx < 0) { idx--; fx += 8; }
    corner(m, 0)[0] = (uint8_t)idx; corner(m, 0)[1] = (uint8_t)fx; corner(m, 0)[2] = (uint8_t)fy;
    fx = (int16_t)(fx + sqb(m, 8) * 2);
    while (fx > 7) { idx++; fx -= 8; }
    corner(m, 1)[0] = (uint8_t)idx; corner(m, 1)[1] = (uint8_t)fx; corner(m, 1)[2] = (uint8_t)fy;
    fy = (int16_t)(fy + sqb(m, 8) * 2);
    while (fy > 7) { idx = (int8_t)(idx + 3); fy -= 8; }
    corner(m, 2)[0] = (uint8_t)idx; corner(m, 2)[1] = (uint8_t)fx; corner(m, 2)[2] = (uint8_t)fy;
    fx = (int16_t)(fx - sqb(m, 8) * 2);
    while (fx < 0) { idx--; fx += 8; }
    corner(m, 3)[0] = (uint8_t)idx; corner(m, 3)[1] = (uint8_t)fx; corner(m, 3)[2] = (uint8_t)fy;
    for (c = 0; c < 4; c++) {
        uint8_t n = corner(m, c)[0];
        uint16_t t = (uint16_t)(tp + (int8_t)ds[(uint16_t)(NEIGHBOUR_OFF + n)] * 4);
        if (rw(ds, (uint16_t)(SQ_TILE_WORDS + n * 2)) == 0x1111)
            ww(ds, (uint16_t)(SQ_TILE_WORDS + n * 2), tile_word(m, t));
    }
    ds[SQ_VALID] = 1;
    for (c = 0; c < 4; c++) {
        int k;
        uint8_t masks[5];
        if (classify_corner(m, c, tol)) continue;
        if ((corner(m, c)[3] | (corner(m, c)[4] << 8)) & 0x300) continue;
        memcpy(masks, ds + DIAG_MASKS, 5);
        for (k = 0; k < 2; k++) {
            int other = (k * 2 + c + 1) & 3;
            uint16_t tt;
            if (corner(m, other)[0] == corner(m, c)[0]) continue;
            tt = (uint16_t)(rw(ds, (uint16_t)(SQ_TILE_WORDS + corner(m, c)[0] * 2)) & 0xf);
            if (masks[k + c] & ds[(uint16_t)(TILE_TYPE_FLAGS + tt)]) {
                corner(m, c)[3] = 0x00;
                corner(m, c)[4] = 0x02;
                sqbset(m, 0x11, 0x80);
                k = 2;
            }
        }
        ds[SQ_VALID] = 0;
    }
    sqwset(m, 0xe, (uint16_t)(rw(ds, SQ_CORNERS + 3) | rw(ds, SQ_CORNERS + 8)
                              | rw(ds, SQ_CORNERS + 13) | rw(ds, SQ_CORNERS + 18) | sqw(m, 0xe)));
}

/* spatial_query_corner_walk */
void sq_corner_walk(uw_motion *m) {
    uint8_t *ds = m->ds;
    int si = 0, di = 0, c;
    int8_t b1 = 0, b2 = 0, b3 = 0, b4 = 0;
    for (c = 0; c < 4; c++) {
        uint16_t f = (uint16_t)(corner(m, c)[3] | (corner(m, c)[4] << 8));
        if (!(f & 0xf8)) {
            b2 = (int8_t)(b2 + (int8_t)ds[(uint16_t)(CORNER_UNITS + c)]);
            b1 = (int8_t)(b1 + (int8_t)ds[(uint16_t)(CORNER_UNITS + ((c + 3) & 3))]);
            di++;
        }
        if (f & 0x300) {
            b4 = (int8_t)(b4 + (int8_t)ds[(uint16_t)(CORNER_UNITS + c)]);
            b3 = (int8_t)(b3 + (int8_t)ds[(uint16_t)(CORNER_UNITS + ((c + 3) & 3))]);
            si++;
        }
    }
    if (si == 0) {
        sqbset(m, 0x12, 9);
    } else {
        sqbset(m, 0x12, ds[(uint16_t)(DIR_TABLE + (b4 / si) * 3 + (b3 / si))]);
        if (si == 1 && (sqb(m, 0x12) % 2) && ds[SQ_VALID]) {
            uint8_t b5 = (uint8_t)((9 - sqb(m, 0x12)) & 7);
            uint8_t b6 = (uint8_t)((uint16_t)sqw(m, 6) >> 13);
            switch ((b6 - b5) & 7) {
            case 0: case 1:
                sqbset(m, 0x12, 9);
                break;
            case 2: case 3:
                sqbset(m, 0x12, (uint8_t)((sqb(m, 0x12) + 1) & 7));
                break;
            case 4: case 5: {
                uint8_t cc = (uint8_t)((sqb(m, 0x12) - 1) >> 1);
                uint8_t b8 = 0, b9 = 0;
                sqbset(m, 0x12, (uint8_t)(sqb(m, 0x12) - 1));
                switch (cc) {
                case 0: b8 = corner(m, cc)[1]; b9 = corner(m, cc)[2]; break;
                case 1: b8 = corner(m, cc)[1]; b9 = (uint8_t)(8 - corner(m, cc)[2]); break;
                case 2: b8 = corner(m, cc)[2]; b9 = corner(m, cc)[1]; break;
                case 3: b8 = (uint8_t)(8 - corner(m, cc)[1]); b9 = corner(m, cc)[2]; break;
                default: break;
                }
                if (b8 < b9) sqbset(m, 0x12, (uint8_t)((sqb(m, 0x12) + 2) & 7));
                if (b8 == b9) sqbset(m, 0x12, (uint8_t)(sqb(m, 0x12) + 1));
                break;
            }
            default:
                sqbset(m, 0x12, (uint8_t)((sqb(m, 0x12) + 7) & 7));
                break;
            }
        }
    }
    if (di == 1 || di == 2)
        sqbset(m, 0x13, ds[(uint16_t)(DIR_TABLE + (b2 / -di) * 3 + (b1 / -di))]);
    else
        sqbset(m, 0x13, 9);
}

/* spatial_query_add */
static void sq_add(uw_motion *m, uint16_t o, uint16_t idx, int8_t tx, int8_t ty, int critter) {
    uint8_t *ds = m->ds;
    uint16_t id = obj_id(m, o), w1 = rw(m->lseg, (uint16_t)(o + 2));
    int8_t b1, b2, b3, b4;
    uint16_t r;
    if (sqb(m, 0x14) > 8) return;
    if ((prop(m, id, 1) & 7) == 4) {
        b1 = (int8_t)(tx * 8); b3 = (int8_t)(b1 + 7);
        b2 = (int8_t)(ty * 8); b4 = (int8_t)(b2 + 7);
    } else {
        int cx = prop(m, id, 1) & 7;
        b1 = (int8_t)(tx * 8 + (w1 >> 13));
        b2 = (int8_t)(ty * 8 + ((w1 & 0x1c00) >> 10));
        if (((rw(m->lseg, o) & 0x1c0) >> 6) == 1 && cx > 0 && critter) cx--;
        b3 = (int8_t)(b1 + cx); b4 = (int8_t)(b2 + cx);
        b1 = (int8_t)(b1 - cx); b2 = (int8_t)(b2 - cx);
    }
    if (b3 < (int8_t)ds[SQ_BOX]) return;
    if (b1 > (int8_t)ds[SQ_BOX + 1]) return;
    if (b4 < (int8_t)ds[SQ_BOX + 3]) return;
    if (b2 > (int8_t)ds[SQ_BOX + 4]) return;
    r = (uint16_t)(RESULTS + sqb(m, 0x14) * 6);
    sqbset(m, 0x14, (uint8_t)(sqb(m, 0x14) + 1));
    ds[(uint16_t)(r + 1)] = (uint8_t)(w1 & 0x7f);
    ds[r] = (uint8_t)(ds[(uint16_t)(r + 1)] + prop(m, id, 0));
    if (prop(m, id, 0) == 0) ds[r] = (uint8_t)(ds[r] + 1);
    ww(ds, (uint16_t)(r + 2), (uint16_t)((rw(ds, (uint16_t)(r + 2)) & 0x3f) | ((idx & 0x3ff) << 6)));
    ds[(uint16_t)(r + 2)] = (uint8_t)((ds[(uint16_t)(r + 2)] & 0xc0) | 9);
    if (!((int8_t)ds[SQ_FX] < b1) && !((int8_t)ds[SQ_FX] > b3)
        && !((int8_t)ds[SQ_BOX + 2] < b2) && !((int8_t)ds[SQ_BOX + 2] > b4))
        ds[(uint16_t)(r + 2)] = (uint8_t)((ds[(uint16_t)(r + 2)] & 0xc0) | ((ds[(uint16_t)(r + 2)] & 0x3f) | 0x10));
    ww(ds, (uint16_t)(r + 4), (uint16_t)(ty * 64 + tx));
}

/* spatial_query_gather */
void sq_gather(uw_motion *m, int props_filter, int use_filter) {
    uint8_t *ds = m->ds;
    int critter = 0;
    int8_t r, tx0, ty0, tx1, ty1, tx, ty;
    uint16_t tp = tile_ptr(m, (uint16_t)(sqw(m, 0) >> 3), (uint16_t)(sqw(m, 2) >> 3));
    uint16_t excl = (uint16_t)sqw(m, 0xa);
    if (excl) critter = ((rw(m->lseg, obj_at(m, excl)) & 0x1c0) >> 6) == 1;
    sqbset(m, 0x14, 0);
    ds[SQ_FX] = (uint8_t)(sqw(m, 0) & 7);
    ds[SQ_BOX + 2] = (uint8_t)(sqw(m, 2) & 7);
    if (props_filter && sqb(m, 9) == 0) {
        r = 0;
    } else {
        r = (int8_t)sqb(m, 8);
        if (critter && r > 0) r--;
    }
    ds[SQ_BOX + 1] = (uint8_t)(ds[SQ_FX] + r);
    ds[SQ_BOX + 4] = (uint8_t)(ds[SQ_BOX + 2] + r);
    ds[SQ_BOX] = (uint8_t)(ds[SQ_FX] - r);
    ds[SQ_BOX + 3] = (uint8_t)(ds[SQ_BOX + 2] - r);
    tx0 = (int8_t)(((int8_t)ds[SQ_BOX] - 11) / 8);
    ty0 = (int8_t)(((int8_t)ds[SQ_BOX + 3] - 11) / 8);
    tx1 = (int8_t)(((int8_t)ds[SQ_BOX + 1] + 4) / 8);
    ty1 = (int8_t)(((int8_t)ds[SQ_BOX + 4] + 4) / 8);
    for (tx = tx0; tx <= tx1; tx++)
        for (ty = ty0; ty <= ty1; ty++) {
            int si = 0;
            uint16_t link = (uint16_t)(tp + (ty * 64 + tx) * 4 + 2);
            uint8_t *seg = m->lseg;
            while (((rw(seg, link) >> 6) & 0x3ff) && si < 0x40) {
                uint16_t idx = (uint16_t)((rw(seg, link) >> 6) & 0x3ff);
                uint16_t o = obj_at(m, idx);
                if (idx != excl) {
                    uint16_t id = obj_id(m, o);
                    int skip = 0;
                    if (critter && ((prop(m, id, 3) >> 2) & 1)) skip = 1;
                    if (!skip && prop(m, id, 0) == 0 && o >= rw(ds, STATIC_BASE)) skip = 1;
                    if (!skip && o < rw(ds, STATIC_BASE) && ((rw(seg, o) & 0x1c0) >> 6) != 1
                        && (seg[(uint16_t)(o + 0x15)] & 0x80)) skip = 1;
                    if (!skip && use_filter && !(prop(m, id, 6) & 1)) skip = 1;
                    if (!skip) sq_add(m, o, idx, tx, ty, critter);
                }
                link = (uint16_t)(o + 4);
                si++;
            }
            if (si == 0x40) return;
        }
}

static void sq_swap(uw_motion *m, int b) {
    uint8_t tmp[6];
    memcpy(tmp, m->ds + RESULTS + b * 6, 6);
    memcpy(m->ds + RESULTS + b * 6, m->ds + RESULTS + (b + 1) * 6, 6);
    memcpy(m->ds + RESULTS + (b + 1) * 6, tmp, 6);
}

/* spatial_query_sort */
void sq_sort(uw_motion *m) {
    uint8_t *ds = m->ds;
    int8_t flat = sqb(m, 9) == 0, split = 0, b1, b2;
    if (sqb(m, 0x14) > 9) {
        /* more results than the table holds: a query record that was never
         * gathered (uninitialised scratch), which the original's loops
         * would run on for ever -- clamped, and counted */
        UW_NOT_CARRIED(m->not_carried);
        sqbset(m, 0x14, 9);
    }
    while (sqb(m, 0x14) > (uint8_t)split) {
        for (b1 = (int8_t)(sqb(m, 0x14) - 2); b1 >= split; b1--)
            if (ds[(uint16_t)(RESULTS + b1 * 6)] > ds[(uint16_t)(RESULTS + (b1 + 1) * 6)]) sq_swap(m, b1);
        if (ds[(uint16_t)(RESULTS + split * 6)] > sqw(m, 4)) break;
        split++;
    }
    for (b1 = split; sqb(m, 0x14) > (uint8_t)b1; b1++)
        for (b2 = (int8_t)(sqb(m, 0x14) - 2); b2 >= b1; b2--)
            if (ds[(uint16_t)(RESULTS + b2 * 6 + 1)] > ds[(uint16_t)(RESULTS + (b2 + 1) * 6 + 1)]) sq_swap(m, b2);
    sqbset(m, 0x16, (uint8_t)split);
    sqbset(m, 0x15, 0);
    while (split + sqb(m, 0x15) < sqb(m, 0x14)
           && sqw(m, 4) + sqb(m, 9) + flat > ds[(uint16_t)(RESULTS + (split + sqb(m, 0x15)) * 6 + 1)])
        sqbset(m, 0x15, (uint8_t)(sqb(m, 0x15) + 1));
}

/* collision_check, from the instructions: 2 stop, 0x10 stop and
 * report, 4 go on. Terrain (slot -1) and a plain object are carried, the
 * struck thing's use by the mover (object_use_dispatch(mover, hit, 0) -- a
 * door walked into), a trigger struck (trigger_chain), and a moving thing's
 * use on what it struck with the knockback after. */
uint16_t collision_check(uw_motion *m, int slot, uint16_t mover_index, uint16_t bp) {
    uint8_t *ds = m->ds;
    uint16_t mover = obj_at(m, mover_index), di = obj_id(m, mover);
    uint16_t hit = 0, hit_id = 0xffff;
    int stops;
    if (slot != -1) {
        uint16_t r = (uint16_t)(RESULTS + slot * 6);
        if (ds[(uint16_t)(r + 2)] & 0x20) return 2;
        ds[(uint16_t)(r + 2)] = (uint8_t)(ds[(uint16_t)(r + 2)] | 0x20);
    }
    ds[0x2763] = (uint8_t)(rs(ds, Q) >> 3);
    ds[0x2764] = (uint8_t)(rs(ds, Q + 2) >> 3);
    if (slot == -1) {
        stops = 1;
    } else {
        uint16_t r = (uint16_t)(RESULTS + slot * 6), idx = (uint16_t)((rw(ds, (uint16_t)(r + 2)) >> 6) & 0x3ff);
        int16_t t = (int16_t)(rw(ds, (uint16_t)(r + 4)) & 0x3f), local;
        hit = obj_at(m, idx);
        ds[0x2761] = (uint8_t)((ds[0x2763] + t) & 0x3f);
        local = (int16_t)(ds[0x2761] - ds[0x2763]);
        ds[0x2762] = (uint8_t)((ds[0x2764] + (int8_t)((int16_t)(rw(ds, (uint16_t)(r + 4)) - local) / 0x40)) & 0x3f);
        hit_id = obj_id(m, hit);
        stops = prop(m, hit_id, 6) & 1;
        if ((int16_t)mover_index < 0x100 && idx < 0x100) {
            if ((di >> 6) != 1 && (m->lseg[(uint16_t)(mover + 0x15)] & 0x80)) return 2;
            m->lseg[(uint16_t)(mover + 0x15)] = (uint8_t)((m->lseg[(uint16_t)(mover + 0x15)] & 0x7f) | 0x80);
        }
    }
    if (hit_id != 0xffff) {
        if ((prop(m, hit_id, 6) >> 1) & 1) {
            ww(ds, 0x269a, ds[0x2761]);
            ww(ds, 0x269c, ds[0x2762]);
            ds[0x2760] = 0;
            object_use_dispatch(m, mover, hit, 0);   /* the struck thing used by the mover: a creature opens a door */
        } else if ((hit_id >> 6) == 6) {
            /* A COLLISION IS A TRIGGER */
            return trigger_chain(m, mover, 0, hit, 0, (uint16_t)(bp - 0xc - 4 - 14 - 4 - 2));
        }
    }
    if (!stops) return 2;
    if ((prop(m, di, 6) >> 1) & 1) {
        ww(ds, 0x269a, ds[0x2763]);
        ww(ds, 0x269c, ds[0x2764]);
        ds[0x2760] = 1;
        object_use_dispatch(m, hit, mover, 0);
        if (rs(ds, 0x269a) < 0) return 0x10;
    }
    motion_knockback(m, hit);
    return 4;
}

/* motion_query_sync */
void query_sync(uw_motion *m) {
    uint8_t *ds = m->ds;
    ww(ds, SQ_PTR, Q);
    ww(ds, 0x276e, (uint16_t)cws(m, 0x1e));
    ds[0x2770] = cb(m, 0x22);
    ds[0x2771] = cb(m, 0x23);
    ww(ds, 0x2772, (uint16_t)cws(m, 0x20));
    qww(m, 0, (uint16_t)(cws(m, 0) >> 5));
    qww(m, 2, (uint16_t)(cws(m, 2) >> 5));
    qww(m, 4, (uint16_t)(cws(m, 4) >> 3));
    gset(m, MR_FRAC_X, (uint16_t)((cws(m, 0) & 0x1f) << 8));
    gset(m, MR_FRAC_Y, (uint16_t)((cws(m, 2) & 0x1f) << 8));
    gset(m, MR_FRAC_Z, (uint16_t)((cws(m, 4) & 7) << 8));
}

/* motion_pick_support */
void pick_support(uw_motion *m, int arg) {
    uint8_t *ds = m->ds;
    uint8_t count = ds[0x277c], under = ds[0x277d];
    int8_t split;
    (void)arg;
    ds[MR_TERRAIN_BOUND] = 1;
    sq_sort(m);
    count = ds[0x277c]; under = ds[0x277d]; split = (int8_t)ds[0x277e];
    ds[MR_HIT_SLOT] = 0xff;
    gset(m, MR_SUPPORT, 0x7f);
    if (cws(m, 0xa) > 0) {
        if (count > 0 && split + under < count && split >= 0) {
            int idx = split + under;
            gset(m, MR_Z_BOUND, (uint16_t)(rec_z(m, idx) - cb(m, 0x23)));
            ds[MR_HIT_SLOT] = (uint8_t)(split + under);
        } else {
            gset(m, MR_Z_BOUND, (uint16_t)(0x80 - cb(m, 0x23)));
        }
        ds[MR_TERRAIN_BOUND] = 0;
        if (qws(m, 4) + cb(m, 0x22) < ds[0x2779]) {
            gset(m, MR_Z_BOUND, ds[0x2779]);
            ds[MR_TERRAIN_BOUND] = 1;
        }
    } else if (cws(m, 0xa) == 0) {
        int i, local1;
        gset(m, MR_Z_BOUND, ds[0x2778]);
        gset(m, MR_Z_BOUND, (qws(m, 4) + cb(m, 0x24) < ds[0x2779]) ? ds[0x2778] : ds[0x2779]);
        local1 = under == 0 && split > 0 && (uint8_t)split <= count;
        for (i = 0; i < count; i++) {
            uint16_t o = obj_at(m, (uint16_t)((rec_link(m, i) >> 6) & 0x3ff));
            if (!(prop(m, obj_id(m, o), 6) & 1)) continue;
            if (split <= i) {
                int rz = rec_z(m, i) - 1;
                if (rz < gw(m, MR_SUPPORT)) gset(m, MR_SUPPORT, (uint16_t)rz);
                if (!(split + under > i)) continue;
                if (!(rec_top(m, i) > gw(m, MR_Z_BOUND))) continue;
                if (!(rec_top(m, i) < 0x80 - cb(m, 0x23))) continue;
                ds[MR_HIT_SLOT] = (uint8_t)i;
                gset(m, MR_Z_BOUND, rec_top(m, i));
            } else {
                if (!local1) continue;
                if (rec_top(m, i) < gw(m, MR_Z_BOUND)) continue;
                gset(m, MR_Z_BOUND, rec_top(m, i));
                ds[MR_HIT_SLOT] = (uint8_t)i;
            }
        }
    } else {
        gset(m, MR_Z_BOUND, ds[0x2779]);
        if (split > 0 && (uint8_t)split <= count && rec_top(m, split - 1) > gw(m, MR_Z_BOUND)) {
            gset(m, MR_Z_BOUND, rec_top(m, split - 1));
            ds[MR_HIT_SLOT] = (uint8_t)(split - 1);
        }
        ds[MR_TERRAIN_BOUND] = 0;
    }
    if (ds[MR_HIT_SLOT] != 0xff) {
        uint16_t o = obj_at(m, (uint16_t)((rec_link(m, (int8_t)ds[MR_HIT_SLOT]) >> 6) & 0x3ff));
        gset(m, MR_HIT_ITEM, obj_id(m, o));
    }
}
