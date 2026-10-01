/* SPDX-License-Identifier: MIT */
/* spells and effects: a missile's launch,
 * the effect table and effect_dispatch, the area effects of classes 6,
 * 7 and 8, the timed effects, casting from the rune shelf and Detect
 * Monster.
 *
 * One of src/uw_motion*.c: see uw_motion.h and uw_motion_int.h. */
#include "uw_motion_int.h"

/* ---- a missile's launch ------------------------------------------------ */

/* The spell and trap effect table, verbatim: 53 records of four
 * bytes, byte 0 bits 3..7 the handler class and byte 3 its argument.
 * It lives in its own segment, not the data segment. */
static const uint8_t EFFECT_TABLE[0x35 * 4] = {
    0x00,0x78,0x21,0x83, 0x10,0x12,0x05,0x02, 0x29,0x38,0x39,0x01, 0x40,0x97,0x21,0x01, 0x18,0xf8,0x48,0x02, 0x08,0xf8,0x51,0x01,
    0x18,0x58,0x02,0x01, 0x08,0x6f,0x44,0x02, 0x20,0x2c,0x20,0x02, 0x58,0x98,0x59,0x01, 0x38,0x58,0x40,0x01, 0x40,0x38,0x21,0x03,
    0x58,0x6f,0x46,0x00, 0x18,0x4b,0x06,0x03, 0x00,0x78,0x41,0x85, 0x29,0xd8,0x38,0x02, 0x5a,0x38,0x49,0x02, 0x10,0x58,0x22,0x43,
    0x08,0xf8,0x5d,0x44, 0x20,0x98,0x21,0x04, 0x08,0xf8,0x1d,0x03, 0x38,0x98,0x35,0x04, 0x18,0xb8,0x48,0x46, 0x5a,0x38,0x01,0x03,
    0x29,0xb8,0x3c,0x03, 0x38,0x4c,0x00,0x02, 0x5a,0xd7,0x3a,0x04, 0x18,0x4f,0x1a,0x05, 0x5a,0xf8,0x12,0x05, 0x58,0xb8,0x01,0x06,
    0x20,0x0c,0x55,0x0f, 0x30,0xc6,0x55,0x42, 0x58,0x2f,0x56,0x0a, 0x38,0x8f,0x00,0x05, 0x00,0x0b,0x55,0x86, 0x5a,0xf7,0x39,0x08,
    0x08,0xef,0x54,0x05, 0x38,0x91,0x21,0x03, 0x40,0x98,0x29,0x04, 0x18,0x4b,0x56,0x44, 0x30,0x16,0x54,0x03, 0x30,0x10,0x38,0x81,
    0x10,0xb2,0x22,0x45, 0x58,0xf7,0x55,0x09, 0x58,0xf6,0x39,0x07, 0x30,0xf8,0x14,0x44, 0x58,0x78,0x02,0x0b, 0x58,0x42,0x55,0x0c,
    0x30,0x18,0x63,0x05, 0x28,0x18,0x63,0x04, 0x58,0x18,0x63,0x0d, 0x50,0x18,0x63,0x03, 0x50,0x18,0x63,0x09,
};

int is_tracked(uw_motion *m, uint16_t obj) {
    return obj == rw(m->ds, TRACKED_OBJECT);
}

/* motion_state_init: a fresh mobile made steppable at tile
 * (tx, ty) -- heading into +9, mode 2, quality 0x3f, the schedule nibble one
 * past the update phase, and a non-creature's whole fine position. */
void motion_state_init(uw_motion *m, uint16_t o, uint16_t tx, uint16_t ty) {
    uint8_t *ds = m->ds, *ls = m->lseg;
    ls[(uint16_t)(o + 9)] = (uint8_t)(((rw(ls, (uint16_t)(o + 2)) & 0x380) >> 7) << 5);
    ls[(uint16_t)(o + 0x18)] &= 0xe0;
    ls[(uint16_t)(o + 0x14)] = (uint8_t)((ls[(uint16_t)(o + 0x14)] & 7) | 0x80);
    ls[(uint16_t)(o + 0x13)] = (uint8_t)((ls[(uint16_t)(o + 0x13)] & 0x7f)
                                         | (((prop(m, obj_id(m, o), 3) >> 3) & 1) ? 0 : 0x80));
    ww(ls, (uint16_t)(o + 0x16), (uint16_t)((rw(ls, (uint16_t)(o + 0x16)) & 0x3ff) | ((tx & 0x3f) << 10)));
    ww(ls, (uint16_t)(o + 0x16), (uint16_t)((rw(ls, (uint16_t)(o + 0x16)) & 0xfc0f) | ((ty & 0x3f) << 4)));
    ls[(uint16_t)(o + 0xa)] = (uint8_t)((ls[(uint16_t)(o + 0xa)] & 0xf0) | ((ds[MOBILE_PHASE] + 1) & 0xf));
    ls[(uint16_t)(o + 0x14)] = (uint8_t)((ls[(uint16_t)(o + 0x14)] & 0xf8) | 2);
    ls[(uint16_t)(o + 0x13)] &= 0x80;
    ww(ls, o, (uint16_t)(rw(ls, o) & 0xbfff));
    ls[(uint16_t)(o + 8)] = 0x3f;
    ls[(uint16_t)(o + 0xa)] &= 0x8f;
    if (((rw(ls, o) & 0x1c0) >> 6) != 1) {
        uint16_t w2 = rw(ls, (uint16_t)(o + 2));
        ww(ls, (uint16_t)(o + 0xb), (uint16_t)((tx << 8) + (((w2 & 0xe000) >> 13) << 5) + 0xf));
        ww(ls, (uint16_t)(o + 0xd), (uint16_t)((ty << 8) + (((w2 & 0x1c00) >> 10) << 5) + 0xf));
        ww(ls, (uint16_t)(o + 0xf), (uint16_t)((w2 & 0x7f) << 3));
        ls[(uint16_t)(o + 0x12)] = 0;
    }
}

/* projectile_collision_query: may the new missile stand where it
 * was put, a radius beyond the firer along its heading? The query struct is
 * the function's own stack local at bp - 0x18. Refused on a wall or floor
 * (terrain bits 8..9) or when the sorted objects block; otherwise the
 * missile is moved to the offset point. */
static int projectile_collision_query(uw_motion *m, uint16_t o, uint16_t firer, uint16_t bp) {
    uint8_t *ds = m->ds, *ls = m->lseg;
    uint16_t q = (uint16_t)(bp - 0x18), id = obj_id(m, o);
    uint16_t w16 = rw(ls, (uint16_t)(o + 0x16)), w2 = rw(ls, (uint16_t)(o + 2));
    ww(ds, SQ_PTR, q);
    ww(ds, (uint16_t)(q + 0xa), obj_index_of(m, o));
    ds[(uint16_t)(q + 8)] = (uint8_t)(prop(m, id, 1) & 7);
    ds[(uint16_t)(q + 9)] = prop(m, id, 0);
    ww(ds, q, (uint16_t)(((w16 >> 10) << 3) + ((w2 & 0xe000) >> 13)));
    ww(ds, (uint16_t)(q + 2), (uint16_t)((((w16 & 0x3f0) >> 4) << 3) + ((w2 & 0x1c00) >> 10)));
    angle_to_offset(m, (uint16_t)((((w2 & 0x380) >> 7) << 5) + (ls[(uint16_t)(o + 0x18)] & 0x1f)),
                    (int16_t)((prop(m, obj_id(m, firer), 1) & 7) + (prop(m, id, 1) & 7) + 4),
                    q, (uint16_t)(q + 2));
    ww(ds, (uint16_t)(q + 4), (uint16_t)(rw(ls, (uint16_t)(o + 2)) & 0x7f));
    sq_gather(m, 0, 1);
    sq_terrain(m, 0);
    if ((rw(ds, (uint16_t)(q + 0xc)) | rw(ds, (uint16_t)(q + 0xe))) & 0x300) return 0;
    if (ds[(uint16_t)(q + 0x14)]) {
        sq_sort(m);
        if (ds[(uint16_t)(q + 0x15)]) return 0;
    }
    ww(ls, (uint16_t)(o + 0x16), (uint16_t)((rw(ls, (uint16_t)(o + 0x16)) & 0x3ff) | (((rs(ds, q) >> 3) & 0x3f) << 10)));
    ww(ls, (uint16_t)(o + 0x16), (uint16_t)((rw(ls, (uint16_t)(o + 0x16)) & 0xfc0f)
                                            | (((rs(ds, (uint16_t)(q + 2)) >> 3) & 0x3f) << 4)));
    ww(ls, (uint16_t)(o + 2), (uint16_t)((rw(ls, (uint16_t)(o + 2)) & 0x1fff) | ((rw(ds, q) & 7) << 13)));
    ww(ls, (uint16_t)(o + 2), (uint16_t)((rw(ls, (uint16_t)(o + 2)) & 0xe3ff) | ((rw(ds, (uint16_t)(q + 2)) & 7) << 10)));
    return 1;
}

/* launch_projectile, from the instructions: a mobile object from
 * the projectile globals -- the item, the firer's heading (its fine low five
 * bits when from_firer_tile) plus aim_x, its position, a launch height from
 * the firer's height property and aim_z, the collision test, then the fine
 * position, owner, vertical velocity (aim_z + 16), mode 1 and speed, into its
 * tile's chain with a sound. `bp` is its frame. The offset, or 0. */
uint16_t launch_projectile(uw_motion *m, uint16_t bp) {
    uint8_t *ds = m->ds, *ls = m->lseg;
    uw_objpool pool;
    uint16_t o, firer = rw(ds, PROJ_FIRER), fw2, h;
    memset(&pool, 0, sizeof pool);
    pool.seg = ls;
    pool.mobile_sp = rw(ds, 0x2756);
    pool.static_sp = rw(ds, 0x274a);
    pool.active_end = rw(ds, ACTIVE_END);
    o = uw_obj_alloc(&pool, 1);
    m->not_carried += pool.not_carried;
    ww(ds, 0x2756, pool.mobile_sp);
    ww(ds, ACTIVE_END, pool.active_end);
    if (!o) return 0;
    ww(ls, (uint16_t)(o + 4), (uint16_t)(rw(ls, (uint16_t)(o + 4)) & 0x3f));
    ww(ls, o, (uint16_t)((rw(ls, o) & 0x7fff) | 0x8000));
    ww(ls, (uint16_t)(o + 6), (uint16_t)(rw(ls, (uint16_t)(o + 6)) & 0x3f));
    ww(ls, (uint16_t)(o + 6), (uint16_t)(rw(ls, (uint16_t)(o + 6)) | 0x40));
    ww(ls, o, (uint16_t)((rw(ls, o) & 0xfe00) | (rw(ds, PROJ_ITEM) & 0x1ff)));
    if (rw(ds, PROJ_HEADING)) ww(ds, PROJ_HEADING, (uint16_t)(ls[(uint16_t)(firer + 0x18)] & 0x1f));
    fw2 = rw(ls, (uint16_t)(firer + 2));
    ww(ds, PROJ_HEADING, (uint16_t)(rw(ds, PROJ_HEADING) + (((fw2 & 0x380) >> 7) << 5)));
    ww(ds, PROJ_HEADING, (uint16_t)(rw(ds, PROJ_HEADING) + rw(ds, PROJ_AIM_X)));
    ww(ds, PROJ_HEADING, (uint16_t)((rw(ds, PROJ_HEADING) + 0x100) & 0xff));
    motion_state_init(m, o, rw(ds, PROJ_TARGET_X), rw(ds, PROJ_TARGET_Y));
    h = rw(ds, PROJ_HEADING);
    ww(ls, (uint16_t)(o + 2), (uint16_t)((rw(ls, (uint16_t)(o + 2)) & 0xfc7f) | ((((int16_t)h >> 5) & 7) << 7)));
    ls[(uint16_t)(o + 0x18)] = (uint8_t)((ls[(uint16_t)(o + 0x18)] & 0xe0) | (h & 0x1f));
    ls[(uint16_t)(o + 9)] = (uint8_t)h;
    ww(ls, o, (uint16_t)(rw(ls, o) & 0xdfff));
    ww(ls, (uint16_t)(o + 2), (uint16_t)((rw(ls, (uint16_t)(o + 2)) & 0xff80) | (fw2 & 0x7f)));
    ww(ls, (uint16_t)(o + 2), (uint16_t)((rw(ls, (uint16_t)(o + 2)) & 0x1fff) | (fw2 & 0xe000)));
    ww(ls, (uint16_t)(o + 2), (uint16_t)((rw(ls, (uint16_t)(o + 2)) & 0xe3ff) | (fw2 & 0x1c00)));
    if (prop(m, obj_id(m, firer), 0)) {
        uint16_t si = (uint16_t)(rw(ls, (uint16_t)(o + 2)) & 0x7f);
        uint16_t hgt = prop(m, obj_id(m, firer), 0);
        uint16_t aim2 = (uint16_t)(rw(ds, PROJ_AIM_Z) << 1);
        ww(ls, (uint16_t)(o + 2), (uint16_t)((rw(ls, (uint16_t)(o + 2)) & 0xff80)
                                              | ((si + (uint16_t)(hgt * 5) / 6 + aim2) & 0x7f)));
        if (is_tracked(m, firer) && ds[(uint16_t)(rw(ds, PLAYER_RECORD_PTR) + 0xb9)] > 0x50)
            ww(ls, (uint16_t)(o + 2), (uint16_t)((rw(ls, (uint16_t)(o + 2)) & 0xff80)
                                                  | ((si + aim2 + (uint16_t)(hgt - (ds[(uint16_t)(rw(ds, PLAYER_RECORD_PTR) + 0xb9)] >> 3))) & 0x7f)));
        if (!projectile_collision_query(m, o, firer, (uint16_t)(bp - 0xa - 8 - 4 - 2))) {
            pool.mobile_sp = rw(ds, 0x2756);
            pool.active_end = rw(ds, ACTIVE_END);
            uw_obj_free(&pool, o);
            m->not_carried += pool.not_carried;
            ww(ds, 0x2756, pool.mobile_sp);
            ww(ds, ACTIVE_END, pool.active_end);
            return 0;
        }
    }
    if (((rw(ls, o) & 0x1c0) >> 6) != 1) {
        uint16_t w16 = rw(ls, (uint16_t)(o + 0x16)), w2 = rw(ls, (uint16_t)(o + 2));
        int16_t owner = 0;
        ww(ls, (uint16_t)(o + 0xb), (uint16_t)(((w16 >> 10) << 8) + (((w2 & 0xe000) >> 13) << 5) + 0xf));
        ww(ls, (uint16_t)(o + 0xd), (uint16_t)((((w16 & 0x3f0) >> 4) << 8) + (((w2 & 0x1c00) >> 10) << 5) + 0xf));
        ww(ls, (uint16_t)(o + 0xf), (uint16_t)((w2 & 0x7f) << 3));
        if (((rw(ls, firer) & 0x1c0) >> 6) == 1) {
            owner = (int16_t)obj_index_of(m, firer);
            if (owner >= 0x100) owner = 0;
        }
        ls[(uint16_t)(o + 0x12)] = (uint8_t)owner;
        ls[(uint16_t)(o + 0x15)] &= 0x7f;
    }
    ls[(uint16_t)(o + 0x14)] = (uint8_t)((ls[(uint16_t)(o + 0x14)] & 7) | (((ds[PROJ_AIM_Z] + 0x10) & 0x1f) << 3));
    ls[(uint16_t)(o + 0x14)] = (uint8_t)((ls[(uint16_t)(o + 0x14)] & 0xf8) | 1);
    ls[(uint16_t)(o + 0x13)] = (uint8_t)((ls[(uint16_t)(o + 0x13)] & 0x80) | (ds[PROJ_SPEED] & 0x7f));
    if (prop(m, rw(ds, PROJ_ITEM), 7) & 0x80)
        ls[(uint16_t)(o + 6)] &= 0xc0;
    {
        uint16_t w16 = rw(ls, (uint16_t)(o + 0x16));
        uw_object_list_insert(&pool, (uint16_t)(tile_ptr(m, (uint16_t)(w16 >> 10), (uint16_t)((w16 & 0x3f0) >> 4)) + 2), o);
    }
    play_sound_effect_at_object(m, 10, o, 0);
    return o;
}

/* projectile_cast: a missile of `kind` from a caster -- its
 * speed from the missile table, straight ahead from a mobile's own tile, at
 * the effect target from a static object; the player's aims at the cursor. */

static int projectile_cast(uw_motion *m, uint16_t firer, uint16_t kind, uint16_t bp) {
    uint8_t *ds = m->ds, *ls = m->lseg;
    ww(ds, PROJ_ITEM, (uint16_t)(kind + 0x10));
    ww(ds, PROJ_SPEED, ds[(uint16_t)(MISSILE_PROPS + kind * 3)]);
    ww(ds, PROJ_TARGET_X, (uint16_t)(rw(ls, (uint16_t)(firer + 0x16)) >> 10));
    ww(ds, PROJ_TARGET_Y, (uint16_t)((rw(ls, (uint16_t)(firer + 0x16)) & 0x3f0) >> 4));
    ww(ds, PROJ_HEADING, 1);
    ww(ds, PROJ_FIRER, firer);
    ww(ds, (uint16_t)(PROJ_FIRER + 2), rw(ds, (uint16_t)(MOBILE_BASE + 2)));
    if (is_tracked(m, firer)) {
        /* aimed at the cursor, whatever the aim answers, and launched */
        projectile_aim_from_cursor(m);
        return launch_projectile(m, (uint16_t)(bp - 4 - 2)) != 0;
    }
    if (firer >= rw(ds, STATIC_BASE)) {
        ww(ds, PROJ_TARGET_X, ds[EFFECT_TARGET_X]);
        ww(ds, PROJ_TARGET_Y, ds[EFFECT_TARGET_Y]);
        ww(ds, PROJ_AIM_Z, 0);
        ww(ds, PROJ_HEADING, 0);
    }
    ww(ds, PROJ_AIM_X, 0);
    return launch_projectile(m, (uint16_t)(bp - 4 - 2)) != 0;
}

/* effect_dispatch and effect_dispatch_2 as a
 * creature casts: the class and argument from the effect table, refused on
 * a no-magic tile or on level 9, then by class. Class 5 at anything but the
 * player is effect_cast_projectile: the missile kind from the
 * four missile-kind bytes by the argument. `bp` is effect_dispatch's frame. */
/* ==== area effects: effect classes 6, 7 and 8 =========================== */

enum {
    AREA_HANDLERS      = 0x09ab,   /* far callbacks, four bytes an entry: class 6 at +0, 7 at +0x10 */
    TILE_DAMAGE_COUNT  = 0x09d3,   /* damage_objects_in_tile's dice by kind - 1 */
    TILE_DAMAGE_SIDES  = 0x09d5,
    TILE_DAMAGE_TYPE   = 0x09d7
};
/* The callbacks as the handler tables hold them: far pointers in the runtime
 * spelling, segment and offset as the program is loaded. */
#define SPELL_SHEET_LIGHTNING_FAR 0x393e066eu
#define SPELL_FLAME_WIND_FAR      0x393e06fbu
#define SPELL_ROCKFALL_FAR        0x393e1211u   /* effect_spawn_boulder */

/* level_effect_add through src/uw_effects.c, over the list the
 * data segment holds, written back. */
int level_effect_add(uw_motion *m, uint16_t index, int16_t timer, uint8_t seed, uint8_t x, uint8_t y) {
    uint8_t *ds = m->ds;
    static uw_effects e;
    uw_objpool pool;
    int i, r;
    uw_effects_from_ds(&e, ds);
    e.drawn_flag = ds[0x2e1c];
    pool_from_ds(m, &pool);
    e.pool = &pool;
    r = uw_effect_add(&e, m->lseg, index, timer, seed, x, y);
    pool_to_ds(m, &pool);
    ds[0x3656] = (uint8_t)e.count;
    ds[0x2e1c] = (uint8_t)e.drawn_flag;
    for (i = 0; i < UW_EFFECTS_MAX; i++) {
        uint16_t at = (uint16_t)(0x369c + i * 6);
        ww(ds, at, e.rec[i].word0);
        ww(ds, (uint16_t)(at + 2), (uint16_t)e.rec[i].timer);
        ds[(uint16_t)(at + 4)] = e.rec[i].tile_x;
        ds[(uint16_t)(at + 5)] = e.rec[i].tile_y;
    }
    m->not_carried += e.unsupported;
    return r;
}

/* create_object_in_tile(id, tile): object_create, its z the
 * tile's floor (height * 8) plus rand() % (0x80 - that) while under 0x80. */
static uint16_t create_object_in_tile(uw_motion *m, uint16_t id, uint16_t tile) {
    uint8_t *ls = m->lseg;
    uint16_t o = create_object(m, id, 0);
    int16_t z = (int16_t)(((ls[tile] >> 4) & 0xf) << 3);
    if (z < 0x80) z = (int16_t)(z + (int16_t)rt_rand(m) % (0x80 - z));
    if (!o) {
        UW_NOT_CARRIED(m->not_carried);
        return 0;
    }
    ww(ls, (uint16_t)(o + 2), (uint16_t)((rw(ls, (uint16_t)(o + 2)) & 0xff80) | (z & 0x7f)));
    return o;
}

/* damage_objects_in_tile(x, y, kind, attacker): kind 0 does
 * nothing; else every object in the tile takes roll_dice from the kind's
 * dice (10d6 of type 0x0b for 1, 6d5 of type 3 for 2) through apply_damage,
 * the attacker by its index. `bp` is this routine's frame: eight bytes of
 * locals and SI, DI under it, apply_damage's sixteen bytes of arguments,
 * and apply_damage's no locals and fourteen bytes of arguments name
 * object_damage_debris's frame, 0x22 and 0x14 below, for the debris a
 * broken thing is placed as (flame2's 024: the query at 0x945e). */
void damage_objects_in_tile(uw_motion *m, int16_t x, int16_t y, uint8_t kind, uint8_t attacker, uint16_t bp) {
    uint8_t *ds = m->ds;
    uint16_t o, debris_bp = m->debris_bp;
    if (kind-- == 0) return;
    if (((uint16_t)x & 0xffc0) || ((uint16_t)y & 0xffc0)) {
        /* tile_ptr_from_xy's null for a tile off the map, and the link read
         * through it at address 2 -- the interrupt table's. */
        UW_NOT_CARRIED(m->not_carried);
        return;
    }
    m->debris_bp = (uint16_t)(bp - 8 - 4 - 16 - 4 - 2 - 14 - 4 - 2);
    o = deref_link(m, (uint16_t)(tile_ptr(m, (uint16_t)x, (uint16_t)y) + 2));
    while (o) {
        uint16_t next = deref_link(m, (uint16_t)(o + 4));
        int16_t dmg = roll_dice(m, ds[(uint16_t)(TILE_DAMAGE_COUNT + kind)], ds[(uint16_t)(TILE_DAMAGE_SIDES + kind)]);
        apply_damage(m, o, obj_at(m, attacker), x, y, (uint8_t)dmg, ds[(uint16_t)(TILE_DAMAGE_TYPE + kind)]);
        o = next;
    }
    m->debris_bp = debris_bp;
}

/* spell_sheet_lightning, an area callback: a splash (0x1c5)
 * made in the tile, the tile's objects struck (kind 2), and the splash an
 * effect with countdown 4 -- freed when the list is full, else put at the
 * head of the tile. */
static int spell_sheet_lightning(uw_motion *m, uint8_t x, uint8_t y, uint16_t obj, uint16_t tile, uint8_t arg, uint16_t bp) {
    uw_objpool pool;
    uint16_t splash = create_object_in_tile(m, 0x1c5, tile);
    uint8_t seed;
    (void)obj;
    damage_objects_in_tile(m, x, y, 2, arg, (uint16_t)(bp - 4 - 8 - 4 - 2));
    seed = (uint8_t)((int16_t)rt_rand(m) % 4);
    if (!splash) return 1;
    if (level_effect_add(m, obj_index_of(m, splash), 4, seed, x, y) == -1) {
        pool_from_ds(m, &pool);
        uw_obj_free(&pool, splash);
        pool_to_ds(m, &pool);
    } else {
        pool_from_ds(m, &pool);
        uw_object_list_insert(&pool, (uint16_t)(tile + 2), splash);
        pool_to_ds(m, &pool);
    }
    return 1;
}

/* spawn_animo_copies through src/uw_effects.c, with the pools and
 * the RNG the data segment holds, written back. */
void spawn_animo_copies(uw_motion *m, uint16_t src, uint8_t x, uint8_t y) {
    uint8_t *ds = m->ds;
    static uw_effects e;
    uw_objpool pool;
    uw_rng rng;
    int i;
    uw_effects_from_ds(&e, ds);
    e.drawn_flag = ds[0x2e1c];
    pool_from_ds(m, &pool);
    e.pool = &pool;
    rng.state = (uint32_t)rw(ds, RAND_SEED) | ((uint32_t)rw(ds, (uint16_t)(RAND_SEED + 2)) << 16);
    uw_spawn_animo_copies(&e, src, x, y, &rng);
    ww(ds, RAND_SEED, (uint16_t)rng.state);
    ww(ds, (uint16_t)(RAND_SEED + 2), (uint16_t)(rng.state >> 16));
    pool_to_ds(m, &pool);
    ds[0x3656] = (uint8_t)e.count;
    ds[0x2e1c] = (uint8_t)e.drawn_flag;
    for (i = 0; i < UW_EFFECTS_MAX; i++) {
        uint16_t at = (uint16_t)(0x369c + i * 6);
        ww(ds, at, e.rec[i].word0);
        ww(ds, (uint16_t)(at + 2), (uint16_t)e.rec[i].timer);
        ds[(uint16_t)(at + 4)] = e.rec[i].tile_x;
        ds[(uint16_t)(at + 5)] = e.rec[i].tile_y;
    }
    m->not_carried += e.unsupported;
}

/* spell_flame_wind, an area callback: an explosion (0x1c2)
 * made in the tile, fire (kind 1: 10d6 of type 0x0b) on the tile and its
 * four neighbours -- east, west, north, south -- and the explosion an effect
 * with countdown 4 and seed 0, freed when the list is full, else put at the
 * head of the tile and scattered with spawn_animo_copies. */
static int spell_flame_wind(uw_motion *m, uint8_t x, uint8_t y, uint16_t obj, uint16_t tile, uint8_t arg, uint16_t bp) {
    uw_objpool pool;
    uint16_t boom = create_object_in_tile(m, 0x1c2, tile);
    uint16_t bp_d = (uint16_t)(bp - 4 - 8 - 4 - 2);     /* damage_objects_in_tile's frame: four locals, four words of arguments */
    (void)obj;
    damage_objects_in_tile(m, x, y, 1, arg, bp_d);
    damage_objects_in_tile(m, (int16_t)(x + 1), y, 1, arg, bp_d);
    damage_objects_in_tile(m, (int16_t)(x - 1), y, 1, arg, bp_d);
    damage_objects_in_tile(m, x, (int16_t)(y + 1), 1, arg, bp_d);
    damage_objects_in_tile(m, x, (int16_t)(y - 1), 1, arg, bp_d);
    if (!boom) return 1;
    if (level_effect_add(m, obj_index_of(m, boom), 4, 0, x, y) == -1) {
        pool_from_ds(m, &pool);
        uw_obj_free(&pool, boom);
        pool_to_ds(m, &pool);
    } else {
        pool_from_ds(m, &pool);
        uw_object_list_insert(&pool, (uint16_t)(tile + 2), boom);
        pool_to_ds(m, &pool);
        spawn_animo_copies(m, boom, x, y);
    }
    return 1;
}

/* effect_area_b's callbacks, class 7, runtime */
#define SPELL_CAUSE_FEAR_FAR   0x393e098fu
#define SPELL_PARALYSE_FAR     0x393e09abu
#define SPELL_POISON_FAR       0x393e0834u
#define SPELL_ALLY_FAR         0x393e08f8u
#define SPELL_SMITE_UNDEAD_FAR 0x393e07eau
#define SPELL_CONFUSION_FAR    0x393e0973u   /* effect_area_a's */
#define SPELL_REVEAL_FAR       0x393e05c3u   /* effect_area_a's */

/* creature_set_goal_for(npc, goal, gtarg): creature_set_goal on
 * that creature, current_npc_ptr aimed at it and put back. */
static void creature_set_goal_for(uw_motion *m, uint16_t npc_obj, uint8_t goal, int gtarg) {
    uint8_t *ds = m->ds;
    uint16_t off = rw(ds, 0x245a), seg = rw(ds, 0x245c);
    ww(ds, 0x245a, npc_obj);
    ww(ds, 0x245c, rw(ds, (uint16_t)(MOBILE_BASE + 2)));
    creature_set_goal(m, goal, gtarg);
    ww(ds, 0x245a, off);
    ww(ds, 0x245c, seg);
}

/* The mind spells' shared body, spell_set_creature_goal(goal,
 * attitude, target, x, y), from the instructions: when compute_damage(target,
 * 1, 3) lets it through, the visible effect (spawn_class7_object(target, 7,
 * 4, 0, 7, x, y)), creature_set_goal_for(target, goal, 1) and, unless the
 * attitude is -1, the attitude (+0x0d bits 14..15); 1 either way. Cause fear
 * is goal 6 (flee), paralyse goal 7 at attitude 1, confusion (class 6's)
 * goal 2, wander, at attitude 1. */
static int spell_set_creature_goal(uw_motion *m, uint8_t goal, int8_t attitude, uint16_t target, uint8_t x, uint8_t y) {
    uint8_t *ls = m->lseg;
    if (!compute_damage(m, target, 1, 3)) return 1;
    spawn_class7(m, target, 7, 4, 0, 7, x, y);
    creature_set_goal_for(m, target, goal, 1);
    if (attitude != -1)
        ww(ls, (uint16_t)(target + 0xd), (uint16_t)((rw(ls, (uint16_t)(target + 0xd)) & 0x3fff) | ((attitude & 3) << 14)));
    return 1;
}

/* The three 28-byte callbacks over it, each
 * spell_set_creature_goal with its goal and attitude and the walk's
 * (x, y, obj): spell_confusion wander (2) at attitude 1, spell_cause_fear
 * flee (6) with the attitude left, spell_paralyse stand still (7) at 1. */
static int spell_confusion(uw_motion *m, uint16_t o, uint8_t x, uint8_t y) { return spell_set_creature_goal(m, 2, 1, o, x, y); }
static int spell_cause_fear(uw_motion *m, uint16_t o, uint8_t x, uint8_t y) { return spell_set_creature_goal(m, 6, -1, o, x, y); }
static int spell_paralyse(uw_motion *m, uint16_t o, uint8_t x, uint8_t y) { return spell_set_creature_goal(m, 7, 1, o, x, y); }

/* spell_ally(x, y, target), from the instructions: through
 * compute_damage(target, 1, 3), the effect, goal 2 (wander) unless the ally
 * flag (+0x19 bit 6) is up, then the flag and attitude 3; 1 either way. */
static int spell_ally(uw_motion *m, uint16_t target, uint8_t x, uint8_t y) {
    uint8_t *ls = m->lseg;
    if (!compute_damage(m, target, 1, 3)) return 1;
    spawn_class7(m, target, 7, 4, 0, 7, x, y);
    if (!(ls[(uint16_t)(target + 0x19)] & 0x40)) creature_set_goal_for(m, target, 2, 0);
    ls[(uint16_t)(target + 0x19)] |= 0x40;
    ww(ls, (uint16_t)(target + 0xd), (uint16_t)(rw(ls, (uint16_t)(target + 0xd)) | 0xc000));
    return 1;
}

/* An area callback's caster, obj_ptr_from_index of the index it was given:
 * a mobile's record, the zeroth for 0. */
static uint16_t area_caster(uw_motion *m, uint8_t index) {
    return (uint16_t)(rw(m->ds, MOBILE_BASE) + index * 0x1b);
}

/* spell_poison(x, y, target, ..., caster), from the
 * instructions: the effect and apply_damage(target, caster, x, y, 5d4, type
 * 0x13); 1. spell_smite_undead: compute_damage(target, 1, 0x80)
 * answering 0 -- undead -- is apply_damage of 0xff, type 3, and 1; else 0. */
static int spell_poison(uw_motion *m, uint16_t target, uint8_t x, uint8_t y, uint8_t caster) {
    uint8_t dmg;
    spawn_class7(m, target, 7, 4, 0, 7, x, y);
    dmg = (uint8_t)roll_dice(m, 5, 4);
    apply_damage(m, target, area_caster(m, caster), x, y, dmg, 0x13);
    return 1;
}

static int spell_smite_undead(uw_motion *m, uint16_t target, uint8_t x, uint8_t y, uint8_t caster) {
    if (compute_damage(m, target, 1, 0x80)) return 0;
    apply_damage(m, target, area_caster(m, caster), x, y, 0xff, 3);
    return 1;
}

/* effect_spawn_boulder(x, y), the rockfall's tile callback,
 * from the instructions: a boulder (0x154 + rand() % 3: large, plain or
 * small), its height 0x6e, moved to the tile's centre at 0x6e -- up at the
 * ceiling, so it falls -- and, placed and mobile, its +0x13 low seven bits
 * 2..5, +9 a random byte, +0x0a's low nibble the update phase
 * plus 0..3, +0x14's low three bits 1..3. Always 1: every tile it is given
 * spends one of the count. `bp` is the area walk's callback frame. */
static int effect_spawn_boulder(uw_motion *m, uint8_t x, uint8_t y, uint16_t bp) {
    uint8_t *ds = m->ds, *ls = m->lseg;
    uint16_t id = (uint16_t)((int16_t)rt_rand(m) % 3 + 0x154), o = create_object(m, id, 0);
    if (!o) { UW_NOT_CARRIED(m->not_carried); return 1; }
    ww(ls, (uint16_t)(o + 2), (uint16_t)((rw(ls, (uint16_t)(o + 2)) & 0xff80) | 0x6e));
    if (object_move_to_coords(m, (int16_t)(x * 8 + 3), (int16_t)(y * 8 + 3), 0x6e, o, 0, 0, (uint16_t)(bp - 4 - 0xe - 4 - 2))
        && o < rw(ds, STATIC_BASE)) {
        ls[(uint16_t)(o + 0x13)] = (uint8_t)((ls[(uint16_t)(o + 0x13)] & 0x80) | (((rt_rand(m) & 3) + 2) & 0x7f));
        ls[(uint16_t)(o + 9)] = (uint8_t)rt_rand(m);
        ls[(uint16_t)(o + 0xa)] = (uint8_t)((ls[(uint16_t)(o + 0xa)] & 0xf0) | ((ds[0x248d] + (rt_rand(m) & 3)) & 0xf));
        ls[(uint16_t)(o + 0x14)] = (uint8_t)((ls[(uint16_t)(o + 0x14)] & 0xf8) | (((int16_t)rt_rand(m) % 3 + 1) & 7));
    }
    return 1;
}

/* spell_reveal(x, y, obj), class 6's object callback for Reveal
 * (effect 0x29, the table's second entry, every object in the area): an
 * object with contents that is not a trap or trigger and links a look
 * trigger (class 6 subclass 2 type 3) fires it -- trigger_chain(player,
 * obj, the trigger, 5) -- with the Search skill (record +0x2c) pinned at
 * 0x2d for the call; 1 when it did. `bp` is its frame, the port's. */
static int spell_reveal(uw_motion *m, uint16_t o, uint8_t x, uint8_t y, uint16_t bp) {
    uint8_t *ds = m->ds, *ls = m->lseg;
    uint16_t rec = rw(ds, PLAYER_RECORD_PTR), link = (uint16_t)(o + 6), t;
    uint8_t saved;
    (void)x; (void)y;
    if ((rw(ls, o) & 0x8000) || !(rw(ls, link) >> 6) || (rw(ls, o) & 0x1c0) == 0x180) return 0;
    t = object_find_matching(m, &link, 0, 6, 2, 3);
    if (!t) return 0;
    saved = ds[(uint16_t)(rec + 0x2c)];
    ds[(uint16_t)(rec + 0x2c)] = 0x2d;
    trigger_chain(m, rw(ds, TRACKED_OBJECT), o, t, 5, (uint16_t)(bp - 4 - 14 - 4 - 2));
    ds[(uint16_t)(rec + 0x2c)] = saved;
    return 1;
}

/* projectile_detonate(obj, x, y, attacker), from the
 * instructions: the two projectiles that explode -- the fireball (0x14) and
 * the lightning bolt (0x15), the first table -- become the
 * explosion (0x1c2) and the splash (0x1c5), from the second; an effect
 * record for it (level_effect_add(index, 4, 0, x, y); a full list answers
 * 0); the fireball scatters (spawn_animo_copies); and the tile is damaged
 * by the table's position (damage_objects_in_tile(x, y, 1 or 2, attacker)).
 * 1 when it went off. `bp` is its frame, the port's. */
int projectile_detonate(uw_motion *m, uint16_t obj, uint8_t x, uint8_t y, uint8_t attacker, uint16_t bp) {
    uint8_t *ds = m->ds, *ls = m->lseg;
    int i;
    for (i = 0; i < 2 && rw(ds, (uint16_t)(0x0ab0 + 2 * i)) != (rw(ls, obj) & 0x1ff); i++) {}
    if (i == 2) return 0;
    ww(ls, obj, (uint16_t)((rw(ls, obj) & 0xfe00) | (rw(ds, (uint16_t)(0x0ab4 + 2 * i)) & 0x1ff)));
    if (level_effect_add(m, obj_index_of(m, obj), 4, 0, x, y) == -1) return 0;
    if (i == 0) spawn_animo_copies(m, obj, x, y);
    damage_objects_in_tile(m, x, y, (uint8_t)(i + 1), attacker, (uint16_t)(bp - 0xa - 2 - 8 - 4 - 2));
    return 1;
}

/* spell_not_a_spell_error: "Not a spell." */
static void spell_not_a_spell_error(uw_motion *m) {
    char text[0x20];
    scroll_print(m, ds_text(m, 0x16e7, text, sizeof text));
}

/* area_match_callback: the flag raised by an object whose +0x0d
 * word has bit 8, unless it is the object area_contains_match asked about or
 * the player; the flag is the answer. */
static int area_match_callback(uw_motion *m, uint16_t o) {
    uint8_t *ds = m->ds;
    if ((rw(m->lseg, (uint16_t)(o + 0xd)) & 0x100) && o != rw(ds, AREA_MATCH_EXCEPT) && o != rw(ds, TRACKED_OBJECT))
        ds[AREA_MATCH_FLAG] = 1;
    return ds[AREA_MATCH_FLAG];
}

/* run_code_on_objects_in_area(count, arg, callback, mode, x0,
 * y0, w, h), from the instructions. The rectangle is clamped to the map --
 * and walked INCLUSIVELY, x0..x0+w and y0..y0+h. Mode 0x40 visits each
 * non-solid tile with probability count / (w * h + 3), a true callback
 * spending one of count, sweeping up to five times while count lasts; the
 * others walk each tile's chain -- 0x80 and 0xc0 every object, 0 the
 * creatures but the one whose index is arg -- the walk stepping past an
 * object the callback left in the chain. Callbacks not ported are counted. */
void run_code_on_objects_in_area(uw_motion *m, int8_t count, uint8_t arg, uint32_t callback,
                                 uint8_t mode, int8_t x0, int8_t y0, int8_t w, int8_t h, uint16_t bp) {
    uint8_t *ls = m->lseg;
    int passes = 0;
    /* a tile callback's frame: 0x14 of locals and SI, DI under this one,
     * and seven words pushed for the call (flame2's 024: 0x9508 to 0x94dc) */
    uint16_t bp_cb = (uint16_t)(bp - 0x14 - 4 - 14 - 4 - 2);
    int16_t x, y;
    uint16_t base;
    if (x0 >= 0x40 || x0 + w < 0 || y0 >= 0x40 || y0 + h < 0) return;
    if (x0 < 0) { w = (int8_t)(w + x0); x0 = 0; }
    else if (x0 + w >= 0x40) w = (int8_t)(w - (int8_t)(x0 + w - 0x40));
    if (y0 < 0) { h = (int8_t)(h + y0); y0 = 0; }
    else if (y0 + h > 0x40) h = (int8_t)(h - (int8_t)(y0 + h - 0x40));
    if (w <= 0 || h <= 0) return;
    base = tile_ptr(m, (uint16_t)x0, (uint16_t)y0);
    for (;;) {
        for (x = x0; x <= x0 + w; x++) {
            for (y = y0; y <= y0 + h; y++) {
                uint16_t tile, link, o;
                if (x < 0 || x >= 0x40 || y < 0 || y >= 0x40) continue;
                tile = (uint16_t)(base + ((x - x0) << 2) + (((y - y0) << 6) << 2));
                if (mode == 0x40) {
                    int r;
                    if (!(ls[tile] & 0xf)) continue;
                    if ((int16_t)rt_rand(m) % (int16_t)(w * h + 3) >= count) continue;
                    if (callback == SPELL_SHEET_LIGHTNING_FAR)
                        r = spell_sheet_lightning(m, (uint8_t)x, (uint8_t)y, 0, tile, arg, bp_cb);
                    else if (callback == SPELL_FLAME_WIND_FAR)
                        r = spell_flame_wind(m, (uint8_t)x, (uint8_t)y, 0, tile, arg, bp_cb);
                    else if (callback == SPELL_ROCKFALL_FAR)
                        r = effect_spawn_boulder(m, (uint8_t)x, (uint8_t)y, bp_cb);
                    else {
                        UW_NOT_CARRIED(m->not_carried);
                        r = 0;
                    }
                    if (r && --count == 0) return;
                    continue;
                }
                link = (uint16_t)(tile + 2);
                for (o = deref_link(m, link); o; o = deref_link(m, link)) {
                    uint16_t next = (uint16_t)((rw(ls, link) >> 6) & 0x3ff);
                    int call = mode == 0x80 || mode == 0xc0
                               || (mode == 0 && ((rw(ls, o) & 0x1c0) >> 6) == 1 && obj_index_of(m, o) != arg);
                    if (call) {
                        int r;
                        if (callback == AREA_MATCH_FAR)
                            r = area_match_callback(m, o);
                        else if (callback == HOSTILE_NEARBY_FAR)
                            r = hostile_nearby_probe(m, o);
                        else if (callback == HUNT_SLEEPER_FAR)
                            r = npc_hunt_sleeper_probe(m, o, bp_cb);
                        else if (callback == SPELL_CAUSE_FEAR_FAR)
                            r = spell_cause_fear(m, o, (uint8_t)x, (uint8_t)y);
                        else if (callback == SPELL_PARALYSE_FAR)
                            r = spell_paralyse(m, o, (uint8_t)x, (uint8_t)y);
                        else if (callback == SPELL_CONFUSION_FAR)
                            r = spell_confusion(m, o, (uint8_t)x, (uint8_t)y);
                        else if (callback == SPELL_REVEAL_FAR)
                            r = spell_reveal(m, o, (uint8_t)x, (uint8_t)y, bp_cb);
                        else if (callback == SPELL_ALLY_FAR)
                            r = spell_ally(m, o, (uint8_t)x, (uint8_t)y);
                        else if (callback == SPELL_POISON_FAR)
                            r = spell_poison(m, o, (uint8_t)x, (uint8_t)y, arg);
                        else if (callback == SPELL_SMITE_UNDEAD_FAR)
                            r = spell_smite_undead(m, o, (uint8_t)x, (uint8_t)y, arg);
                        else if (callback == NPC_WITNESS_CRIME_FAR)
                            r = npc_witness_crime(m, x, y, o);
                        else {
                            UW_NOT_CARRIED(m->not_carried);
                            r = 0;
                        }
                        if (r && --count < 1) return;
                    }
                    if (((rw(ls, link) >> 6) & 0x3ff) == next) link = (uint16_t)(o + 4);
                }
            }
        }
        if (mode != 0x40 || count <= 0 || passes++ >= 4) return;
    }
}

/* effect_area_apply(target, count, callback, mode, distance,
 * radius): from a mobile target its own tile and heading, from any other
 * the effect target tile and its octant only; the point `distance` tiles
 * ahead (angle_to_offset over two words of the frame); the square of
 * radius `radius` about it -- and the target's index, a mobile's, as the
 * callback's argument. `bp` is this routine's frame. */
void effect_area_apply(uw_motion *m, uint16_t target, int8_t count, uint32_t callback,
                              uint8_t mode, uint8_t distance, uint8_t radius, uint16_t bp) {
    uint8_t *ds = m->ds, *ls = m->lseg;
    uint16_t idx = obj_index_of(m, target), heading;
    uint8_t caster;
    if (idx < 0x100) {
        caster = (uint8_t)idx;
        heading = (uint16_t)((((rw(ls, (uint16_t)(target + 2)) & 0x380) >> 7) << 5) + (ls[(uint16_t)(target + 0x18)] & 0x1f));
        ww(ds, (uint16_t)(bp - 2), (uint16_t)(rw(ls, (uint16_t)(target + 0x16)) >> 10));
        ww(ds, (uint16_t)(bp - 4), (uint16_t)((rw(ls, (uint16_t)(target + 0x16)) & 0x3f0) >> 4));
    } else {
        caster = 0;
        heading = (uint16_t)(((rw(ls, (uint16_t)(target + 2)) & 0x380) >> 7) << 5);
        ww(ds, (uint16_t)(bp - 2), ds[EFFECT_TARGET_X]);
        ww(ds, (uint16_t)(bp - 4), ds[EFFECT_TARGET_Y]);
    }
    ds[(uint16_t)(bp - 7)] = caster;
    angle_to_offset(m, heading, distance, (uint16_t)(bp - 2), (uint16_t)(bp - 4));
    /* run_code_on_objects_in_area's frame: eight bytes of locals and SI
     * under this one, and nine words of arguments (flame2's 024: 0x952a to
     * 0x9508) */
    run_code_on_objects_in_area(m, count, caster, callback, mode,
                                (int8_t)(ds[(uint16_t)(bp - 2)] - radius), (int8_t)(ds[(uint16_t)(bp - 4)] - radius),
                                (int8_t)((radius << 1) + 1), (int8_t)((radius << 1) + 1),
                                (uint16_t)(bp - 8 - 2 - 18 - 4 - 2));
    ds[(uint16_t)(bp - 7)] = 0;
}

/* effect_area_a, class 6: 3d4 things through the callback the
 * argument's low six bits pick, in the argument's top two bits' mode, four
 * tiles ahead over a radius of two. `bp` is effect_dispatch_2's frame. */
static void effect_area_a(uw_motion *m, uint16_t target, uint8_t arg, uint16_t bp) {
    uint8_t *ds = m->ds;
    uint16_t bp_a = (uint16_t)(bp - 6 - 4 - 2);
    int8_t count = (int8_t)roll_dice(m, 3, 4);
    uint16_t at = (uint16_t)(AREA_HANDLERS + (arg & 0x3f) * 4);
    uint32_t callback = (uint32_t)rw(ds, at) | (uint32_t)rw(ds, (uint16_t)(at + 2)) << 16;
    ds[(uint16_t)(bp_a - 1)] = (uint8_t)count;
    effect_area_apply(m, target, count, callback, (uint8_t)(arg & 0xc0), 4, 2, (uint16_t)(bp_a - 2 - 16 - 4 - 2));
}

/* mobile_init_defaults: a
 * made creature's mobile record, a bitfield at a time -- tile (32, 32),
 * quality and owner 0x20, the critter row saved, hit points
 * max * (rand() % 24 + 16) / 32, the heading byte from the octant, goal 8,
 * attitude 2, and every AI field and flag cleared. Its one caller writes the
 * real tile over the placeholder at once. */
static void mobile_init_defaults(uw_motion *m, uint16_t o) {
    uint8_t *ds = m->ds, *ls = m->lseg;
    uint16_t row;
    int16_t r;
#define LSB(k) ls[(uint16_t)(o + (k))]
#define LSW_AND(k, mask) ww(ls, (uint16_t)(o + (k)), (uint16_t)(rw(ls, (uint16_t)(o + (k))) & (mask)))
    ww(ls, (uint16_t)(o + 0x16), (uint16_t)((rw(ls, (uint16_t)(o + 0x16)) & 0x3ff) | (0x20 << 10)));
    ww(ls, (uint16_t)(o + 0x16), (uint16_t)((rw(ls, (uint16_t)(o + 0x16)) & 0xfc0f) | (0x20 << 4)));
    LSB(4) = (uint8_t)((LSB(4) & 0xc0) | 0x20);
    LSB(6) = (uint8_t)((LSB(6) & 0xc0) | 0x20);
    row = (uint16_t)(CRITTER_BASE + (rw(ls, o) & 0x3f) * 0x30);
    ww(ds, 0x4a32, row);
    r = (int16_t)((int16_t)rt_rand(m) % 0x18 + 0x10);
    LSB(8) = (uint8_t)((int16_t)(ds[(uint16_t)(row + 4)] * r) / 0x20);
    LSB(9) = (uint8_t)(((rw(ls, (uint16_t)(o + 2)) & 0x380) >> 7) << 5);
    ww(ls, (uint16_t)(o + 0xb), (uint16_t)((rw(ls, (uint16_t)(o + 0xb)) & 0xfff0) | 8));
    LSW_AND(0xb, 0xf00f);
    LSW_AND(0xd, 0xfff0);
    LSW_AND(0xf, 0xffc0);
    LSW_AND(0xf, 0xf03f);
    LSW_AND(0xd, 0xff0f);
    LSW_AND(0xd, 0xfdff);
    LSW_AND(0xd, 0xfbff);
    LSW_AND(0xd, 0xf7ff);
    LSW_AND(0xd, 0xfeff);
    LSB(0x18) &= 0xdf;
    LSW_AND(0xf, 0x0fff);
    LSB(0xa) &= 0xf0;
    LSB(0x14) = (uint8_t)((LSB(0x14) & 0xf8) | 4);
    LSB(0x15) = (uint8_t)((LSB(0x15) & 0xc0) | 0x20);
    LSW_AND(0xb, 0x0fff);
    LSB(0x14) = (uint8_t)((LSB(0x14) & 7) | 0x80);
    LSB(0x13) &= 0x7f;
    LSB(0x13) &= 0x80;
    LSB(0x11) = 0;
    LSB(0x12) = 0;
    LSB(0x15) &= 0x7f;
    LSB(0x18) &= 0x7f;
    LSB(0x18) &= 0xbf;
    LSW_AND(0x16, 0xfff0);
    LSB(0x15) &= 0xbf;
    LSB(0x1a) = 0;
    LSB(0x19) &= 0xfe;
    LSB(0x19) &= 0xfd;
    LSB(0x19) &= 0xef;
    LSB(0x19) &= 0xdf;
    LSB(0x19) &= 0xbf;
    LSB(0x19) &= 0x7f;
    LSW_AND(0xd, 0xefff);
    LSW_AND(0xd, 0xdfff);
    ww(ls, (uint16_t)(o + 0xd), (uint16_t)((rw(ls, (uint16_t)(o + 0xd)) & 0x3fff) | 0x8000));
    LSB(0xa) &= 0x7f;
    LSB(0x19) &= 0xf3;
#undef LSB
#undef LSW_AND
}

/* effect_area_b, class 7: the Avatar's only -- one thing
 * through the callback of the class 7 table by the argument's low six
 * bits, in its top two bits' mode, four tiles ahead over a radius of two.
 * `bp` is effect_dispatch_2's frame. */
static void effect_area_b(uw_motion *m, uint16_t target, uint8_t arg, uint16_t bp) {
    uint8_t *ds = m->ds;
    uint16_t bp_b = (uint16_t)(bp - 6 - 4 - 2);
    uint16_t at = (uint16_t)(0x09bb + (arg & 0x3f) * 4);
    if (!is_tracked(m, target)) return;
    effect_area_apply(m, target, 1, (uint32_t)rw(ds, at) | (uint32_t)rw(ds, (uint16_t)(at + 2)) << 16,
                      (uint8_t)(arg & 0xc0), 4, 2, (uint16_t)(bp_b - 16 - 4 - 2));
}

/* effect_area_c, class 8, from the instructions: not an area but
 * one thing made at a point ahead -- the heading jittered by rand() % 27 -
 * 13, the fine position moved 12 (a summons) or 9 along it. Argument 3 is a
 * trap (counted), 1 food (0xb0 + rand() % 7), 4 a creature of a random
 * level-weighted kind; the thing must fit (item_fits_in_tile); a creature
 * gets mobile_init_defaults, its tile, a flier's height, and is the player's
 * ally or hunts the player; anything else full quality and a toss. `bp` is
 * effect_dispatch_2's frame. */
static void effect_area_c(uw_motion *m, uint16_t target, uint8_t arg, uint16_t bp) {
    uint8_t *ds = m->ds, *ls = m->lseg;
    uint16_t bp_c = (uint16_t)(bp - 6 - 4 - 2), pl = rw(ds, TRACKED_OBJECT);
    uint16_t w2 = rw(ls, (uint16_t)(target + 2)), w16 = rw(ls, (uint16_t)(target + 0x16));
    uint16_t heading, tile, id = 0, o;
    int16_t r, fx, fy, tx, ty, z;
    r = (int16_t)((int16_t)rt_rand(m) % 0x1b - 13);
    heading = (uint16_t)((uint16_t)((((w2 & 0x380) >> 7) << 5) + (ls[(uint16_t)(target + 0x18)] & 0x1f) + r) % 0xff);
    ww(ds, (uint16_t)(bp_c - 0xa), heading);
    ww(ds, (uint16_t)(bp_c - 0xc), (uint16_t)(((w16 >> 10) << 3) + ((w2 & 0xe000) >> 13)));
    ww(ds, (uint16_t)(bp_c - 0xe), (uint16_t)((((w16 & 0x3f0) >> 4) << 3) + ((w2 & 0x1c00) >> 10)));
    angle_to_offset(m, heading, arg == 4 ? 0xc : 9, (uint16_t)(bp_c - 0xc), (uint16_t)(bp_c - 0xe));
    fx = rs(ds, (uint16_t)(bp_c - 0xc));
    fy = rs(ds, (uint16_t)(bp_c - 0xe));
    tx = (int16_t)(fx >> 3);
    ty = (int16_t)(fy >> 3);
    if (arg == 3) {
        /* the Rune of Warding: a move trigger and a ward trap (kind 9) at
         * the point, "The Rune of Warding is placed." (0x114) or "There is
         * no room to create that." (0x115) to the Avatar */
        uint16_t made = trap_create(m, tx, ty, 9);
        if (target == pl) print_message(m, made ? 0x114 : 0x115);
        return;
    }
    tile = tile_ptr(m, (uint16_t)tx, (uint16_t)ty);
    if (!tile) {
        UW_NOT_CARRIED(m->not_carried);
        return;
    }
    z = (int16_t)(((ls[tile] >> 4) & 0xf) << 3);
    if (arg == 1) {
        id = (uint16_t)(0xb0 + (int16_t)rt_rand(m) % 7);
    } else if (arg == 4) {
        uint8_t base = target == pl ? ds[(uint16_t)(rw(ds, PLAYER_RECORD_PTR) + 0x2a)]
                                    : (uint8_t)(rw(ds, CURRENT_LEVEL_WORD) << 2);
        if (base < 2) base = 2;
        for (;;) {
            uint16_t row;
            id = (uint16_t)(base + (int16_t)rt_rand(m) % base + 0x40);
            row = (uint16_t)((id & 0xfe3f) * 0x30);
            if (!ds[(uint16_t)(CRITTER_BASE + 4 + row)]) continue;
            if ((ds[(uint16_t)(CRITTER_BASE + 0xa + row)] >> 1) & 1) continue;
            if (id == 0x7b || id == 0x7c) continue;
            if ((ds[(uint16_t)(CRITTER_BASE + 0xa + row)] >> 6) & 1) continue;
            break;
        }
    } else {
        UW_NOT_CARRIED(m->not_carried);
        return;
    }
    if (!item_fits_in_tile(m, id, 0, fx, fy, z, 1, 8, (uint16_t)(bp_c - 0x1e - 14 - 4 - 2))) {
        if (target == pl) print_message(m, 0x115);
        return;
    }
    o = create_object(m, id, arg == 4);
    if (!o) {
        UW_NOT_CARRIED(m->not_carried);
        return;
    }
    ww(ls, (uint16_t)(o + 2), (uint16_t)((rw(ls, (uint16_t)(o + 2)) & 0x1fff) | ((fx & 7) << 13)));
    ww(ls, (uint16_t)(o + 2), (uint16_t)((rw(ls, (uint16_t)(o + 2)) & 0xe3ff) | ((fy & 7) << 10)));
    if (arg == 4) {
        mobile_init_defaults(m, o);         /* with current_object_ptr swapped to it */
        ww(ls, (uint16_t)(o + 0x16), (uint16_t)((rw(ls, (uint16_t)(o + 0x16)) & 0x3ff) | ((tx & 0x3f) << 10)));
        ww(ls, (uint16_t)(o + 0x16), (uint16_t)((rw(ls, (uint16_t)(o + 0x16)) & 0xfc0f) | ((ty & 0x3f) << 4)));
        if ((ds[(uint16_t)(CRITTER_BASE + 0xa + (id & 0xfe3f) * 0x30)] >> 7) & 1)
            z = (int16_t)((z + 0x80) / 2);
        if (target == pl) {
            ls[(uint16_t)(o + 0x19)] = (uint8_t)((ls[(uint16_t)(o + 0x19)] & 0xbf) | 0x40);
        } else {
            uint16_t pw16 = rw(ls, (uint16_t)(pl + 0x16));
            ww(ls, (uint16_t)(o + 0xd), (uint16_t)(rw(ls, (uint16_t)(o + 0xd)) & 0x3fff));
            ls[(uint16_t)(o + 0x19)] = (uint8_t)((ls[(uint16_t)(o + 0x19)] & 0xfe) | 1);
            ww(ls, (uint16_t)(o + 0xf), (uint16_t)((rw(ls, (uint16_t)(o + 0xf)) & 0xffc0) | ((pw16 >> 10) & 0x3f)));
            ww(ls, (uint16_t)(o + 0xf), (uint16_t)((rw(ls, (uint16_t)(o + 0xf)) & 0xf03f) | ((((pw16 & 0x3f0) >> 4) & 0x3f) << 6)));
        }
    } else {
        ls[(uint16_t)(o + 4)] = (uint8_t)((ls[(uint16_t)(o + 4)] & 0xc0) | 0x3f);
    }
    ww(ls, (uint16_t)(o + 2), (uint16_t)((rw(ls, (uint16_t)(o + 2)) & 0xff80) | (z & 0x7f)));
    {
        uw_objpool pool;
        pool_from_ds(m, &pool);
        uw_object_list_insert(&pool, (uint16_t)(tile + 2), o);
        pool_to_ds(m, &pool);
    }
    if (arg != 4)
        placed_object_collision(m, o, (uint16_t)tx, (uint16_t)ty, 1, (uint16_t)(bp_c - 0x1e - 10 - 4 - 2));
}

static int add_timed_effect(uw_motion *m, uint8_t effect, uint8_t sub, uint8_t code);

/* effect_cast_projectile(caster, arg):
 * the four missile kinds copied into the frame (`bp` its BP),
 * projectile_cast of the argument's; for the Avatar, a missile that found no
 * room is "There is not enough room to release that spell." (0xff), one
 * launched spends the mana spell_cast_valid deferred, which is
 * cleared either way. */
void effect_cast_projectile(uw_motion *m, uint16_t obj, int8_t arg, uint16_t bp) {
    uint8_t *ds = m->ds;
    int ok;
    memcpy(ds + (uint16_t)(bp - 4), ds + CAST_MISSILES, 4);
    ok = projectile_cast(m, obj, ds[(uint16_t)(bp - 5 + arg)], (uint16_t)(bp - 6 - 6 - 4 - 2));
    ds[(uint16_t)(bp - 5)] = (uint8_t)ok;
    if (!is_tracked(m, obj)) return;
    if (!ok) print_message(m, 0xff);
    else if (ds[0x09aa]) ds[(uint16_t)(rw(ds, PLAYER_RECORD_PTR) + 0x37)] = (uint8_t)(ds[(uint16_t)(rw(ds, PLAYER_RECORD_PTR) + 0x37)] - ds[0x09aa]);
    ds[0x09aa] = 0;
}

static void cast_detect_monster(uw_motion *m, int16_t range, uint8_t skill);

/* effect_player_special(obj, duration code, case), effect class
 * 11: the Avatar only, by the case through a thirteen-entry table --
 *   0, 8, 11  add_timed_effect(0x0b, 2, 3 or 0, the duration code);
 *   1         cast_detect_monster(10, 0x2d), Wis Mani's detection;
 *   2..5      door_and_trap_spells armed as the pending action (action_state
 *             2, the case, the Avatar, their handler) and the cursor 0x1076;
 *   6         the poison cleared (record +0x5f bits 2..5), An Nox;
 *   7         Roaming Sight: add_timed_effect(0x0b, 1, code) -- whose recalc
 *             sets movement_input_override and full light -- the free camera
 *             high above the Avatar looking down (debug_camera_goto(0)) and
 *             shown (debug_camera_set_target(-1)), action_state + 8, which
 *             keeps the view's clicks off; the held left button flies the
 *             camera until the effect ends (player_update_step's expiry);
 *   9         the rockfall: effect_area_apply(obj, 8d3, effect_spawn_boulder,
 *             mode 0x40, 5 ahead, radius 3), the view shaken
 *             (player_start_status_effect(0x40, 0x28)), sound 0x12 at the
 *             caster;
 *   10        the moonstone: with its level in the record's +0x5e low
 *             nibble, teleport_to_moonstone as the arrival callback, the
 *             teleport there (trap_teleport(Avatar, 0x3f, 0x3f, level)),
 *             place_player_in_tile(0, 0) -- out of its tile until the
 *             teleport lands it -- and every event; else "The moonstone is
 *             not available." (0x111);
 *   12        Armageddon: the Avatar's possessions freed
 *             (inventory_chain_free), the world emptied (game_world_reset),
 *             the rune bag and shelf cleared, the record's +0x60 bit 4 set --
 *             every level entered after is emptied too -- the silver tree's
 *             level (+0x5e high nibble) forgotten, one more word
 *             cleared, player_state_recalc and panel_redraw.
 * `bp2` is effect_dispatch_2's frame. */
static void effect_player_special(uw_motion *m, uint16_t obj, uint8_t code, int8_t which, uint16_t bp2) {
    uint8_t *ds = m->ds;
    uint16_t rec = rw(ds, PLAYER_RECORD_PTR), bp = (uint16_t)(bp2 - 8 - 4 - 2);
    if (obj != rw(ds, TRACKED_OBJECT) || which < 0 || which > 0xc) return;
    switch (which) {
    case 7:
        add_timed_effect(m, 0x0b, 1, code);
        debug_camera_goto(m, 0);
        debug_camera_set_target(m, -1);
        ww(ds, ACTION_STATE_WORD, (uint16_t)(rw(ds, ACTION_STATE_WORD) + 8));
        break;
    case 9:
        effect_area_apply(m, obj, (int8_t)roll_dice(m, 8, 3), SPELL_ROCKFALL_FAR, 0x40, 5, 3,
                          (uint16_t)(bp - 0x10 - 4 - 2));
        player_start_status_effect(m, 0x40, 0x28);
        play_sound_effect_at_object(m, 0x12, obj, 0);
        break;
    case 10:
        if (ds[(uint16_t)(rec + 0x5e)] & 0xf) {
            ww(ds, 0x13ba, 0x34);           /* the arrival callback: teleport_to_moonstone */
            ww(ds, 0x13bc, 0x629a);
            trap_teleport(m, rw(ds, TRACKED_OBJECT), 0x3f, 0x3f, (int16_t)(ds[(uint16_t)(rec + 0x5e)] & 0xf),
                          (uint16_t)(bp - 0xa - 4 - 2));
            place_player_in_tile(m, 0, 0, (uint16_t)(bp - 6 - 4 - 2));
            ww(ds, PENDING_EVENTS, (uint16_t)(rw(ds, PENDING_EVENTS) | 0x7ffe));   /* post_event(0x7ffe) */
        } else {
            print_message(m, 0x111);
        }
        break;
    case 12:
        inventory_chain_free(m, (uint16_t)(rw(ds, TRACKED_OBJECT) + 6));
        game_world_reset(m);
        rune_bag_clear(m);
        rune_shelf_clear(m);
        ds[(uint16_t)(rec + 0x60)] |= 0x10;
        ds[(uint16_t)(rec + 0x5e)] &= 0x0f;
        ww(ds, 0x72d2, 0);
        player_state_recalc(m);
        panel_redraw(m);
        break;
    case 0: add_timed_effect(m, 0x0b, 2, code); break;
    case 8: add_timed_effect(m, 0x0b, 3, code); break;
    case 11: add_timed_effect(m, 0x0b, 0, code); break;
    case 1: cast_detect_monster(m, 10, 0x2d); break;
    case 2: case 3: case 4: case 5:
        ww(ds, 0x26ac, 2);                  /* action_state */
        ww(ds, 0x2690, (uint16_t)which);
        ww(ds, 0x2688, rw(ds, TRACKED_OBJECT));
        ww(ds, 0x268a, rw(ds, (uint16_t)(MOBILE_BASE + 2)));
        ww(ds, 0x2684, 0x1560);
        ww(ds, 0x2686, 0x393e);
        cursor_shape_push(m, 0x1076);
        break;
    case 6:
        ds[(uint16_t)(rec + 0x5f)] &= 0xc3;
        break;
    default:
        break;
    }
}

/* effect_hp_scaled(obj, magnitude): a
 * creature (class 1) healed by magnitude d8, or 0xff for 0x0f, through
 * change_health. */
static void effect_hp_scaled(uw_motion *m, uint16_t obj, uint8_t magnitude) {
    if ((rw(m->lseg, obj) & 0x1c0) != 0x40) return;
    change_health(m, obj, magnitude == 0x0f ? 0xff : (uint8_t)roll_dice(m, (int8_t)magnitude, 8));
}

/* effect_dispatch_2(class, arg, obj, other), as far as a
 * creature's, a trap's and the Avatar's casts reach it; `other` is the far
 * pointer the caller passes last, 0 where it passes none; `bp2` is its
 * frame. */
int effect_dispatch_2(uw_motion *m, uint8_t cls, uint8_t arg, uint16_t obj, uint16_t other, uint16_t bp2) {
    uint8_t *ds = m->ds, *ls = m->lseg;
    if (obj >= rw(ds, STATIC_BASE) && cls <= 0xb) {
        if (tile_no_magic(m, ds[EFFECT_TARGET_X], ds[EFFECT_TARGET_Y])) return 0;
    } else {
        uint16_t w16 = rw(ls, (uint16_t)(obj + 0x16));
        if (tile_no_magic(m, (uint16_t)(w16 >> 10), (uint16_t)((w16 & 0x3f0) >> 4))) return 0;
        if (rw(ds, CURRENT_LEVEL_WORD) == 9) return 0;
    }
    if (cls <= 3) {
        /* The timed effects are the player's only (add_timed_effect with the
         * argument's low six bits and its duration code, 0 when three are
         * running); class 1's levitation and slow fall start
         * player_start_vertical_motion on the caster first. */
        if (cls == 1 && ((arg & 0x3f) == 3 || (arg & 0x3f) == 5))
            player_start_vertical_motion(m, obj);
        if (!is_tracked(m, obj)) return 0;
        return add_timed_effect(m, cls, (uint8_t)(arg & 0x3f), (uint8_t)(arg & 0xc0));
    }
    if (cls == 4) {
        /* effect_hp_scaled on `other`, when there is one: the Avatar's cast
         * passes itself; a creature's cast passes none, and its healing
         * spell does nothing. */
        if (!other) return 0;
        effect_hp_scaled(m, other, arg);
        return 1;
    }
    /* classes 6 and 8 whoever casts: the Avatar's area from its own tile
     * and heading, as a creature's */
    if (cls == 6) {
        effect_area_a(m, obj, arg, bp2);
        return 1;
    }
    if (cls == 8) {
        effect_area_c(m, obj, arg, bp2);
        return 1;
    }
    if (cls == 7) {
        effect_area_b(m, obj, arg, bp2);
        return 1;
    }
    if (cls == 9) {
        effect_nonlethal_damage(m, obj, arg);
        return 1;
    }
    if (cls == 10) {
        player_restore_mana(m, obj, (int8_t)arg);
        return 1;
    }
    if (cls == 11) {
        effect_player_special(m, obj, (uint8_t)(arg & 0xc0), (int8_t)(arg & 0x3f), bp2);
        return 1;
    }
    if (cls == 12 || cls > 14) {
        /* class 12's table entry is the shared tail, and a class past 14
         * skips the table: nothing, and 1 */
        return 1;
    }
    if (cls == 13) {
        /* two unrelated arms, whoever casts: 3 resets the bullfrog puzzle
         * (trap_bullfrog(4)); 5 is "Your vision distorts and you feel light
         * headed." (0xe4) and the record's +0x61 bits 2..3 set to 3, which
         * player_state_recalc reads back as the impairment */
        if (arg == 3) trap_bullfrog(m, 4);
        else if (arg == 5) {
            uint16_t rec = rw(ds, PLAYER_RECORD_PTR);
            print_message(m, 0xe4);
            ds[(uint16_t)(rec + 0x61)] = (uint8_t)((ds[(uint16_t)(rec + 0x61)] & 0xf3) | 0x0c);
            player_state_recalc(m);
        }
        return 1;
    }
    if (cls == 14) {
        /* cutscene_play(arg) -- the port's, after the pass -- then every
         * level effect advanced four units (level_effects_tick(4)) */
        uw_motion_cutscene_request(m, arg);
        level_effects_tick(m, 4, (uint16_t)(FRAME_BP - 0x80));
        return 1;
    }
    if (cls == 5 && is_tracked(m, obj)) {
        /* the Avatar's targeted cast armed: action_state 3, the caster and
         * the argument pending, the targeting cursor -- the view's click
         * casts it (effect_cast_at_click) */
        ww(ds, 0x26ac, 3);                  /* action_state */
        ww(ds, 0x2688, obj);
        ww(ds, 0x268a, rw(ds, (uint16_t)(MOBILE_BASE + 2)));
        ww(ds, 0x2690, arg);
        cursor_shape_push(m, 0x1075);
        return 1;
    }
    effect_cast_projectile(m, obj, (int8_t)arg, (uint16_t)(bp2 - 6 - 4 - 2));
    return 1;
}

/* effect_dispatch(effect, obj, other): the effect table's class
 * and argument for effect_dispatch_2. */
int effect_dispatch(uw_motion *m, uint8_t effect, uint16_t obj, uint16_t other, uint16_t bp) {
    if (effect >= 0x35) return 0;
    return effect_dispatch_2(m, (uint8_t)(EFFECT_TABLE[effect * 4] >> 3), EFFECT_TABLE[effect * 4 + 3], obj, other,
                             (uint16_t)(bp - 0xc - 4 - 2));
}

/* add_timed_effect(effect, subtype, duration code):
 * none past three running (player record +0x5f bits 6..9);
 * otherwise the next of the record's words from +0x3e takes effect +
 * subtype * 0x10 in its low byte and in its high the duration -- code 0x40
 * 2d8 + 6, 0 2d3, 1 one, 0x80 3d20 + 0x18 (another leaves the stack's
 * byte, counted) -- the count goes up and player_state_recalc. */
static int add_timed_effect(uw_motion *m, uint8_t effect, uint8_t sub, uint8_t code) {
    uint8_t *ds = m->ds;
    uint16_t rec = rw(ds, PLAYER_RECORD_PTR), n = (uint16_t)((rw(ds, (uint16_t)(rec + 0x5f)) >> 6) & 0xf);
    uint16_t at = (uint16_t)(rec + n * 2 + 0x3e);
    uint8_t dur = 0;
    if (n == 3) return 0;
    ww(ds, at, (uint16_t)((rw(ds, at) & 0xff00) + effect + sub * 0x10));
    if (code == 0x40) dur = (uint8_t)(roll_dice(m, 2, 8) + 6);
    else if (code == 0) dur = (uint8_t)roll_dice(m, 2, 3);
    else if (code == 1) dur = 1;
    else if (code == 0x80) dur = (uint8_t)(roll_dice(m, 3, 0x14) + 0x18);
    else UW_NOT_CARRIED(m->not_carried);
    ww(ds, at, (uint16_t)((rw(ds, at) & 0xff) + dur * 0x100));
    ww(ds, (uint16_t)(rec + 0x5f), (uint16_t)((rw(ds, (uint16_t)(rec + 0x5f)) & 0xfc3f) | (((n + 1) & 0xf) << 6)));
    player_state_recalc(m);
    return 1;
}

/* The spells: four bytes each, 48 in eight circles of six -- the
 * effect class << 3, the runes as a base-32 word (first * 0x400 + second *
 * 0x20 + third, 0x18 empty), and the effect's argument. */
static const uint8_t SPELL_TABLE[48 * 4] = {
    0x00, 0x78, 0x21, 0x83, 0x10, 0x12, 0x05, 0x02, 0x29, 0x38, 0x39, 0x01, 0x40, 0x97, 0x21, 0x01, 0x18, 0xf8, 0x48, 0x02, 0x08, 0xf8, 0x51, 0x01,
    0x18, 0x58, 0x02, 0x01, 0x08, 0x6f, 0x44, 0x02, 0x20, 0x2c, 0x20, 0x02, 0x58, 0x98, 0x59, 0x01, 0x38, 0x58, 0x40, 0x01, 0x40, 0x38, 0x21, 0x03,
    0x58, 0x6f, 0x46, 0x00, 0x18, 0x4b, 0x06, 0x03, 0x00, 0x78, 0x41, 0x85, 0x29, 0xd8, 0x38, 0x02, 0x5a, 0x38, 0x49, 0x02, 0x10, 0x58, 0x22, 0x43,
    0x08, 0xf8, 0x5d, 0x44, 0x20, 0x98, 0x21, 0x04, 0x08, 0xf8, 0x1d, 0x03, 0x38, 0x98, 0x35, 0x04, 0x18, 0xb8, 0x48, 0x46, 0x5a, 0x38, 0x01, 0x03,
    0x29, 0xb8, 0x3c, 0x03, 0x38, 0x4c, 0x00, 0x02, 0x5a, 0xd7, 0x3a, 0x04, 0x18, 0x4f, 0x1a, 0x05, 0x5a, 0xf8, 0x12, 0x05, 0x58, 0xb8, 0x01, 0x06,
    0x20, 0x0c, 0x55, 0x0f, 0x30, 0xc6, 0x55, 0x42, 0x58, 0x2f, 0x56, 0x0a, 0x38, 0x8f, 0x00, 0x05, 0x00, 0x0b, 0x55, 0x86, 0x5a, 0xf7, 0x39, 0x08,
    0x08, 0xef, 0x54, 0x05, 0x38, 0x91, 0x21, 0x03, 0x40, 0x98, 0x29, 0x04, 0x18, 0x4b, 0x56, 0x44, 0x30, 0x16, 0x54, 0x03, 0x30, 0x10, 0x38, 0x81,
    0x10, 0xb2, 0x22, 0x45, 0x58, 0xf7, 0x55, 0x09, 0x58, 0xf6, 0x39, 0x07, 0x30, 0xf8, 0x14, 0x44, 0x58, 0x78, 0x02, 0x0b, 0x58, 0x42, 0x55, 0x0c,
};

/* spell_cast_valid(index), from the instructions: the circle is
 * index / 6 + 1. Refused -- spell_fail_message's sound and message 0xd2 +
 * code -- past (level + 1) / 2 (0), with under three mana a circle (1), or
 * when check_skill_roll(Casting + 5, circle * 2) answers 0 (2). -1
 * backfires: message 0xd6 and class 9, nonlethal damage, of circle / 2.
 * Then the cooldown ((circle * 2 - level) * 4 + 0x40, and the play
 * time now), the mana spent unless the class is 5 (a targeted cast
 * spends it on the click, the amount held meanwhile), and
 * effect_dispatch_2 on the Avatar: sound 0x10 and 1, or (3) refused. */
static int spell_cast_valid(uw_motion *m, uint8_t idx) {
    uint8_t *ds = m->ds;
    uint16_t rec = rw(ds, PLAYER_RECORD_PTR), fail;
    uint8_t circle = (uint8_t)(idx / 6 + 1), cls = (uint8_t)(SPELL_TABLE[idx * 4] >> 3), arg;
    int r;
    if ((ds[(uint16_t)(rec + 0x3d)] + 1) / 2 < circle) {
        fail = 0;
    } else if (circle * 3 > ds[(uint16_t)(rec + 0x37)]) {
        fail = 1;
    } else {
        r = check_skill_roll(m, ds[(uint16_t)(rec + 0x2a)] + 5, circle * 2);
        if (r == 0) {
            fail = 2;
        } else {
            if (r == -1) {
                print_message(m, 0xd6);
                cls = 9;
                arg = (uint8_t)(circle / 2);
            } else {
                arg = SPELL_TABLE[idx * 4 + 3];
            }
            ds[0x16e2] = (uint8_t)((circle * 2 - ds[(uint16_t)(rec + 0x3d)]) * 4 + 0x40);
            ww(ds, 0x16e3, rw(ds, (uint16_t)(rec + 0xce)));
            ww(ds, 0x16e5, rw(ds, (uint16_t)(rec + 0xd0)));
            ds[0x09aa] = (uint8_t)(circle * 3);
            if (cls != 5) {
                ds[(uint16_t)(rec + 0x37)] = (uint8_t)(ds[(uint16_t)(rec + 0x37)] - circle * 3);
                ds[0x09aa] = 0;
            }
            /* spell_cast_valid's frame under the key's handler's */
            if (effect_dispatch_2(m, cls, arg, rw(ds, TRACKED_OBJECT), rw(ds, TRACKED_OBJECT), 0x954e)) {
                play_sound_effect(m, 0x10, 0x40, 0);
                return 1;
            }
            ds[0x09aa] = 0;
            fail = 3;
        }
    }
    play_sound_effect(m, 0x16, 0x40, 0);   /* spell_fail_message's */
    print_message(m, (uint16_t)(0xd2 + fail));
    return 0;
}

/* spell_cast_from_shelf(from_key):
 * nothing while action_state is set; the right button's click (event +6
 * bit 1) with no key only waits for its release. The cast-from-shelf flag
 * set; before the cooldown's play time (its word plus its byte) sound 0x15
 * and no cast. Otherwise the release wait, and the shelf's three runes
 * (record +0x47..0x49) looked up: "Not a spell\n" or spell_cast_valid. */

/* ... up to its release wait; 1 when it waits with the cast to follow. */
int spell_cast_from_shelf_wait(uw_motion *m, int16_t from_key) {
    uint8_t *ds = m->ds;
    uint16_t rec = rw(ds, PLAYER_RECORD_PTR);
    uint32_t due, play;
    if (rw(ds, ACTION_STATE_WORD)) return 0;
    if ((rw(ds, (uint16_t)(rw(ds, 0x00e2) + 6)) & 2) && !from_key) {
        input_wait_button_release(m, 1);
        return 0;
    }
    ds[SHELF_SPELL_CAST] = 1;
    due = ((uint32_t)rw(ds, 0x16e3) | (uint32_t)rw(ds, 0x16e5) << 16) + ds[0x16e2];
    play = (uint32_t)rw(ds, (uint16_t)(rec + 0xce)) | (uint32_t)rw(ds, (uint16_t)(rec + 0xd0)) << 16;
    if (play < due) {
        play_sound_effect(m, 0x15, 0x40, 0);
        return 0;
    }
    input_wait_button_release(m, 1);
    return 1;
}

void spell_cast_from_shelf(uw_motion *m, int16_t from_key) {
    if (spell_cast_from_shelf_wait(m, from_key)) spell_cast_from_shelf_rest(m);
}

/* ... and past the wait: the shelf's runes looked up and cast. */
void spell_cast_from_shelf_rest(uw_motion *m) {
    uint8_t *ds = m->ds;
    uint16_t rec = rw(ds, PLAYER_RECORD_PTR), code;
    int i;
    code = (uint16_t)(ds[(uint16_t)(rec + 0x47)] * 0x400 + ds[(uint16_t)(rec + 0x48)] * 0x20 + ds[(uint16_t)(rec + 0x49)]);
    for (i = 0; i < 48 && (uint16_t)(SPELL_TABLE[i * 4 + 1] | SPELL_TABLE[i * 4 + 2] << 8) != code; i++)
        ;
    if (i == 48) spell_not_a_spell_error(m);
    else spell_cast_valid(m, (uint8_t)i);
}

/* delta_to_direction(dx, dy): the
 * compass direction, clockwise from north (0), of a tile delta -- |dy| under
 * half |dx| east (2) or west (6), |dx| under half |dy| north (0) or south
 * (4), else the diagonal between. */
int delta_to_direction(int8_t dx, int8_t dy) {
    int ax = dx < 0 ? -dx : dx, ay = dy < 0 ? -dy : dy;
    if (ay < ax / 2) return dx < 1 ? 6 : 2;
    if (ax < ay / 2) return dy < 1 ? 4 : 0;
    if (dx < 0) return (dy > 0) * 2 + 5;
    return (dy < 0) * 2 + 1;
}

/* report_detected_creatures(direction, count), through
 * report_direction_to with no levels and the direction given:
 * block 1's "You detect a creature ", "... a few creatures " or "... the
 * activity of many creatures " by a count of 1, up to 4, or more; "to the
 * North" .. "to the Northwest" (0x24 + direction); and the full stop. */
static void report_detected_creatures(uw_motion *m, uint8_t dir, uint8_t count) {
    char stop[8];
    print_message(m, (uint16_t)(0x3b + (count > 1) + (count > 4)));
    print_message(m, (uint16_t)(0x24 + dir));
    scroll_print(m, ds_text(m, 0x1dca, stop, sizeof stop));
}

/* cast_detect_monster(range, skill):
 * the creatures (class 1) of the active mobile list within `range` tiles of
 * the Avatar on both axes that pass check_skill_roll(skill, 15 less their
 * critter row's +0x1d low nibble) counted into the eight compass bins. None:
 * "You detect no monster activity." (0x3e). Otherwise the first fullest bin
 * is reported, then from a random bin (rand() & 7) on, the first other bin
 * holding more than that count held to 3 is reported too. */
static void cast_detect_monster(uw_motion *m, int16_t range, uint8_t skill) {
    uint8_t *ds = m->ds, *ls = m->lseg;
    uint8_t bins[8], best = 0, cap, r, k;
    uint16_t pw = rw(ls, (uint16_t)(rw(ds, TRACKED_OBJECT) + 0x16)), at;
    memset(bins, 0, sizeof bins);
    for (at = rw(ds, ACTIVE_LIST); at < rw(ds, ACTIVE_END); at++) {
        uint16_t o = (uint16_t)(rw(ds, MOBILE_BASE) + ls[at] * 0x1b), w = rw(ls, (uint16_t)(o + 0x16));
        int8_t dx = (int8_t)((uint8_t)(w >> 10) - (uint8_t)(pw >> 10));
        int8_t dy = (int8_t)((uint8_t)((w & 0x3f0) >> 4) - (uint8_t)((pw & 0x3f0) >> 4));
        if ((rw(ls, o) & 0x1c0) != 0x40) continue;
        if ((dx < 0 ? -dx : dx) >= range || (dy < 0 ? -dy : dy) >= range) continue;
        if (check_skill_roll(m, skill, 0xf - (ds[(uint16_t)(CRITTER_BASE + 0x1d + (rw(ls, o) & 0x3f) * 0x30)] & 0xf)) > 0)
            bins[delta_to_direction(dx, dy)]++;
    }
    for (k = 0; k < 8; k++)
        if (best < bins[k]) best = bins[k];
    if (!best) {
        print_message(m, 0x3e);
        return;
    }
    cap = best < 4 ? best : 3;
    for (k = 0; k < 8; k++) {
        if (bins[k] == best) {
            report_detected_creatures(m, k, bins[k]);
            best = cap;
            cap = k;
            break;
        }
    }
    r = (uint8_t)(rt_rand(m) & 7);
    for (k = 0; k < 8; k++, r++) {
        if ((r & 7) != cap && best < bins[r & 7]) {
            report_detected_creatures(m, (uint8_t)(r & 7), bins[r & 7]);
            return;
        }
    }
}

/* announce_skill_by_index(n), F9's with 2, through
 * announce_skill_change for the Avatar: kind n + 10 and the
 * record's skill byte +0x2b + n -- 10 and 11 silent, 12 (Track)
 * cast_detect_monster(8, skill), any other the skill's name (block 2, kind
 * + 0x1f) and a newline. */
void announce_skill_by_index(uw_motion *m, int16_t n) {
    uint8_t *ds = m->ds;
    uint8_t kind = (uint8_t)(n + 10), skill = ds[(uint16_t)(rw(ds, PLAYER_RECORD_PTR) + n + 0x2b)];
    char tail[0x20];
    if (kind == 10 || kind == 11) return;
    if (kind == 12) {
        cast_detect_monster(m, 8, skill);
        return;
    }
    print_string(m, (uint16_t)((kind + 0x1f) | 0x400));
    scroll_print(m, ds_text(m, 0x1c90, tail, sizeof tail));
}
