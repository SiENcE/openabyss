/* SPDX-License-Identifier: MIT */
#include "uw_rast.h"

uint8_t uw_rast_outcode(int16_t x, int16_t y, int16_t z) {
    /* `neg ax` in 16 bits: -(-32768) is -32768, not 32768. The whole point
     * of doing this in int16_t rather than in int. */
    int16_t neg = (int16_t)(0u - (uint16_t)z);

    if (z > 0) {
        /* In front of the eye: the four half-spaces, strictly. */
        if (x < neg) {
            if (y > z)   return UW_OUT_LEFT | UW_OUT_ABOVE;   /* 0x05 */
            if (y < neg) return UW_OUT_LEFT | UW_OUT_BELOW;   /* 0x09 */
            return UW_OUT_LEFT;                               /* 0x01 */
        }
        if (x > z) {
            if (y > z)   return UW_OUT_RIGHT | UW_OUT_ABOVE;  /* 0x06 */
            if (y < neg) return UW_OUT_RIGHT | UW_OUT_BELOW;  /* 0x0a */
            return UW_OUT_RIGHT;                              /* 0x02 */
        }
        if (y > z)   return UW_OUT_ABOVE;                     /* 0x04 */
        if (y < neg) return UW_OUT_BELOW;                     /* 0x08 */
        return 0;                                             /* inside */
    }

    /* z <= 0. The bound is non-negative here, so both x tests can hold, and
     * the routine's branch order -- not a bitwise OR of four predicates --
     * is what decides which of the nine values comes out. Transcribed from
     * the instructions so the order is the original's. */
    if (x < neg) {
        if (x > z) {
            if (y > z) {
                if (y < neg) return 0x8f;
                return 0x87;
            }
            return 0x8b;
        }
        if (y > z) {
            if (y < neg) return 0x8d;
            return 0x85;
        }
        return 0x89;
    }
    if (y > z) {
        if (y < neg) return 0x8e;
        return 0x86;
    }
    return 0x8a;
}

/* ---- the vertex fetch --------------------------------------------------- */

/* `sub ax,imm` then `shl ax,cl`, both 16-bit and both wrapping. */
static int16_t bias_shift(int16_t v, int16_t origin, int shift) {
    uint16_t a = (uint16_t)((uint16_t)v - (uint16_t)origin);
    /* The 8086 shifts by CL & 31 on a 186 and by CL on an 8086; the shipped
     * value is 5 and rast_origin_patch never writes more than 15, so the
     * difference cannot arise here. Masked anyway rather than left to be
     * undefined behaviour in C. */
    a = (uint16_t)(a << (shift & 15));
    return (int16_t)a;
}

void uw_rast_fetch_words(const int16_t list[3], const int16_t origin[3],
                         int shift, int16_t out_xyz[3]) {
    int16_t bx = bias_shift(list[0], origin[0], shift);   /* -> BX, x */
    int16_t bp = bias_shift(list[1], origin[1], shift);   /* -> BP, z */
    int16_t cx = bias_shift(list[2], origin[2], shift);   /* -> CX, y */
    out_xyz[0] = bx;
    out_xyz[1] = cx;
    out_xyz[2] = bp;
}

void uw_rast_fetch_bytes(const int8_t list[3], const int16_t origin[3],
                         int shift, int16_t out_xyz[3]) {
    /* cbw; shl ax,5 -- a byte coordinate is signed, in units of 32. */
    int16_t w[3];
    for (int i = 0; i < 3; i++)
        w[i] = (int16_t)((uint16_t)((int16_t)list[i]) << 5);
    uw_rast_fetch_words(w, origin, shift, out_xyz);
}

/* ---- the basis rotate ---------------------------------------------------- */

/* The high word of a signed 16x16 multiply: DX after `imul`. An arithmetic
 * shift, so it FLOORS -- see the header. */
static int16_t hi(int16_t a, int16_t b) {
    int32_t p = (int32_t)a * (int32_t)b;
    return (int16_t)(p >> 16);
}

void uw_rast_rotate(const int16_t v[3], const int16_t m[9],
                    int16_t out[3]) {
    int16_t x = v[0], y = v[1], z = v[2];
    /* Summed in 16 bits, in the order the instructions do it. */
    uint16_t xp = (uint16_t)hi(x, m[0]);
    xp = (uint16_t)(xp + (uint16_t)hi(y, m[3]));
    xp = (uint16_t)(xp + (uint16_t)hi(z, m[6]));

    uint16_t yp = (uint16_t)hi(x, m[1]);
    yp = (uint16_t)(yp + (uint16_t)hi(y, m[4]));
    yp = (uint16_t)(yp + (uint16_t)hi(z, m[7]));

    /* z' starts from the z term, not the x term: `mov ax,bp; imul [0x1612]`
     * comes first at 510c. The order matters only through the wrap, and it
     * does matter through the wrap. */
    uint16_t zp = (uint16_t)hi(z, m[8]);
    zp = (uint16_t)(zp + (uint16_t)hi(x, m[2]));
    zp = (uint16_t)(zp + (uint16_t)hi(y, m[5]));

    out[0] = (int16_t)xp;
    out[1] = (int16_t)yp;
    out[2] = (int16_t)zp;
}

int uw_rast_select_rotate_set(const int16_t m[9]) {
    /* The routine's tests: `test [0x1608]`, `[0x1604]`, `[0x1610]`, `[0x160c]`, each
     * `jne` to the general set, then `cmp [0x160a],0x7ffb; jl` to it too. */
    return m[3] == 0 && m[1] == 0 && m[7] == 0 && m[5] == 0
        && m[4] >= 0x7ffb;
}

void uw_rast_rotate_axis(const int16_t v[3], const int16_t m[9],
                         int16_t out[3]) {
    /* The rotation, with BX, CX, BP = x, y, z:
     *     mov ax,bx; imul [1602]; mov di,dx
     *     mov ax,bp; imul [160e]; add di,dx          x'
     *     mov ax,bx; mov bx,di; imul [1606]
     *     mov ax,bp; mov bp,dx; imul [1612]; add bp,dx   z'
     *     sar cx,1                                   y' */
    int16_t x = v[0], y = v[1], z = v[2];
    out[0] = (int16_t)(uint16_t)((uint16_t)hi(x, m[0]) + (uint16_t)hi(z, m[6]));
    out[2] = (int16_t)(uint16_t)((uint16_t)hi(x, m[2]) + (uint16_t)hi(z, m[8]));
    out[1] = (int16_t)(y >> 1);
}

void uw_rast_rotate_live(const uw_rast_slots *s, const int16_t v[3],
                         int16_t out[3]) {
    if (s->axis) uw_rast_rotate_axis(v, s->basis, out);
    else         uw_rast_rotate(v, s->basis, out);
}

/* ---- the vertex slots ---------------------------------------------------- */

void uw_rast_slot_set(uw_rast_slots *s, int off, const int16_t xyz[3]) {
    if (off < 0 || off + 6 > UW_RAST_SLOT_WINDOW) return;
    s->x[off / 2 + 0] = xyz[0];
    s->x[off / 2 + 1] = xyz[1];
    s->x[off / 2 + 2] = xyz[2];
    s->outcode[off + 6] = uw_rast_outcode(xyz[0], xyz[1], xyz[2]);
}

void uw_rast_slot_get(const uw_rast_slots *s, int off, int16_t xyz[3]) {
    if (off < 0 || off + 6 > UW_RAST_SLOT_WINDOW)
        { xyz[0] = xyz[1] = xyz[2] = 0; return; }
    xyz[0] = s->x[off / 2 + 0];
    xyz[1] = s->x[off / 2 + 1];
    xyz[2] = s->x[off / 2 + 2];
}

void uw_rast_emit_vertex_word(uw_rast_slots *s, const int16_t list[3],
                              int slot_off) {
    int16_t v[3], r[3];
    uw_rast_fetch_words(list, s->origin, s->fetch_shift, v);
    uw_rast_rotate_live(s, v, r);
    uw_rast_slot_set(s, slot_off, r);
}

void uw_rast_emit_vertex(uw_rast_slots *s, const int8_t list[3],
                         int slot_index) {
    int16_t v[3], r[3];
    uw_rast_fetch_bytes(list, s->origin, s->fetch_shift, v);
    uw_rast_rotate_live(s, v, r);
    /* `lodsb; xor ah,ah; shl ax,3` -- zero-extended and scaled, so the
     * operand addresses all 256 records and the word form's does not need
     * to. */
    uw_rast_slot_set(s, (slot_index & 0xff) << 3, r);
}

void uw_rast_vertex_add(uw_rast_slots *s, int src_off, int16_t scale,
                        int row, int dest_off) {
    int16_t v[3];
    uw_rast_slot_get(s, src_off, v);
    /* `mov cl,[0x2880]; shl ax,cl` -- 16-bit and wrapping, before the
     * multiply rather than after it. */
    int16_t k = (int16_t)((uint16_t)scale << (s->add_shift & 15));
    const int16_t *m = s->basis + row * 3;
    uint16_t x = (uint16_t)((uint16_t)v[0] + (uint16_t)hi(k, m[0]));
    uint16_t y = (uint16_t)((uint16_t)v[1] + (uint16_t)hi(k, m[1]));
    uint16_t z = (uint16_t)((uint16_t)v[2] + (uint16_t)hi(k, m[2]));
    int16_t out[3] = {(int16_t)x, (int16_t)y, (int16_t)z};
    uw_rast_slot_set(s, dest_off, out);
}

void uw_rast_vertex_add_pair(uw_rast_slots *s, int src_off, int16_t scale_a,
                             int row_a, int16_t scale_b, int row_b,
                             int dest_off, int axis) {
    int16_t v[3], out[3];
    uint16_t d[3] = { 0, 0, 0 };
    const int16_t sc[2] = { scale_a, scale_b };
    const int rows[2] = { row_a, row_b };
    int j, i;
    uw_rast_slot_get(s, src_off, v);
    for (j = 0; j < 2; j++) {
        int16_t k = (int16_t)((uint16_t)sc[j] << (s->add_shift & 15));
        const int16_t *m = s->basis + (rows[j] & 3) * 3;
        if (axis && rows[j] == 1) {
            d[1] = (uint16_t)(d[1] + (uint16_t)(int16_t)(k >> 1));   /* sar ax,1 */
        } else if (axis) {
            d[0] = (uint16_t)(d[0] + (uint16_t)hi(k, m[0]));
            d[2] = (uint16_t)(d[2] + (uint16_t)hi(k, m[2]));
        } else {
            for (i = 0; i < 3; i++)
                d[i] = (uint16_t)(d[i] + (uint16_t)hi(k, m[i]));
        }
    }
    for (i = 0; i < 3; i++) out[i] = (int16_t)(uint16_t)((uint16_t)v[i] + d[i]);
    uw_rast_slot_set(s, dest_off, out);
}

void uw_rast_vertex_add_axis(uw_rast_slots *s, int src_off, int16_t scale,
                             int row, int dest_off) {
    int16_t v[3];
    int16_t k = (int16_t)((uint16_t)scale << (s->add_shift & 15));
    if (dest_off < 0 || dest_off + 7 > UW_RAST_SLOT_WINDOW) return;
    uw_rast_slot_get(s, src_off, v);
    if (row == 1) {
        /* Row 1: `shl ax,cl; sar ax,1`, then x copied (`movsw`), y
         * added, z copied, and the source's outcode byte `and al,0xf3`:
         *     cmp cx,dx; jle a      ; y' > z:  add cx,dx; jge -> |4, else |0xc
         *  a: add cx,dx; jge store  ; y' <= z: |8 when y' + z < 0 */
        int16_t y = (int16_t)(uint16_t)((uint16_t)v[1]
                                        + (uint16_t)(int16_t)(k >> 1));
        uint8_t oc = (uint8_t)(src_off >= 0 && src_off + 7 <= UW_RAST_SLOT_WINDOW
                               ? s->outcode[src_off + 6] : 0);
        oc &= 0xf3;
        if (y > v[2]) oc |= ((int32_t)y + v[2] >= 0) ? 0x04 : 0x0c;
        else if ((int32_t)y + v[2] < 0) oc |= 0x08;
        s->x[dest_off / 2 + 0] = v[0];
        s->x[dest_off / 2 + 1] = y;
        s->x[dest_off / 2 + 2] = v[2];
        s->outcode[dest_off + 6] = oc;
        return;
    }
    {
        /* Row 0 (m[0], m[2]) and row 2 (m[6], m[8]). y is
         * untouched, and the outcode is built in place:
         *     xor al,al; add bp,dx; jg; mov al,0x80    -- behind unless z > 0
         *     cmp bx,bp; jle; inc ax; inc ax            -- right  if x > z
         *     cmp cx,bp; jle; add al,4                  -- above  if y > z
         *     add bx,bp; jge; inc ax                    -- left   if x + z < 0
         *     add cx,bp; jge; add al,8                  -- below  if y + z < 0
         * Every `jg`/`jge` after an `add` reads the TRUE sum's sign, which is
         * why these are 32-bit here. */
        const int16_t *m = s->basis + row * 3;
        int16_t dz = hi(k, m[2]);
        int16_t x = (int16_t)(uint16_t)((uint16_t)v[0] + (uint16_t)hi(k, m[0]));
        int16_t z = (int16_t)(uint16_t)((uint16_t)v[2] + (uint16_t)dz);
        int32_t z_true = (int32_t)v[2] + dz;
        uint8_t oc = z_true > 0 ? 0 : 0x80;
        if (x > z) oc = (uint8_t)(oc + 2);
        if (v[1] > z) oc = (uint8_t)(oc + 4);
        if ((int32_t)x + z < 0) oc = (uint8_t)(oc + 1);
        if ((int32_t)v[1] + z < 0) oc = (uint8_t)(oc + 8);
        s->x[dest_off / 2 + 0] = x;
        s->x[dest_off / 2 + 1] = v[1];
        s->x[dest_off / 2 + 2] = z;
        s->outcode[dest_off + 6] = oc;
    }
}

void uw_rast_vertex_sum(uw_rast_slots *s, int a_off, int b_off,
                        int dest_off) {
    int16_t a[3], b[3];
    uw_rast_slot_get(s, a_off, a);
    uw_rast_slot_get(s, b_off, b);
    int16_t out[3];
    for (int i = 0; i < 3; i++)
        out[i] = (int16_t)((uint16_t)a[i] + (uint16_t)b[i]);
    uw_rast_slot_set(s, dest_off, out);
}

/* ---- rast_draw_face and the four clip passes ---------------- */

/* `idiv r/m16` with rast_div_saturate_handler_face armed: the
 * handler skips the instruction and leaves AX = 0x7fff, DX = 0. It does that
 * for a divisor of zero and for a quotient of either sign, so a large
 * negative quotient comes back as +32767 and not as -32768. */
static int16_t idiv_sat(int32_t num, int16_t den) {
    int32_t q;
    if (den == 0) return 0x7fff;
    if (den == -1 && num == INT32_MIN) return 0x7fff;   /* #DE, and UB in C */
    q = num / den;                                      /* both truncate */
    if (q > 32767 || q < -32768) return 0x7fff;
    return (int16_t)q;
}

/* `imul bp; shl ax,1; rcl dx,1; idiv cx; sar ax,1; jnc; cwd; adc ax,dx` --
 * delta * num / den, rounded half away from zero. The doubling is a 32-bit
 * shift that DROPS the top bit, which is why it is spelled unsigned. */
static int16_t lerp(int16_t delta, int16_t num, int16_t den) {
    int32_t p = (int32_t)delta * (int32_t)num;
    int16_t q, half;
    p = (int32_t)((uint32_t)p << 1);
    q = idiv_sat(p, den);
    half = (int16_t)(q >> 1);                  /* SAR: floors */
    if (q & 1) {                               /* the bit that fell out */
        /* cwd; adc ax,dx: DX is 0 or -1 from the SHIFTED value's sign, and
         * CF is still the shifted-out bit, so this adds 1 when non-negative
         * and 0 when negative. */
        if (half >= 0) half = (int16_t)(half + 1);
    }
    return half;
}

/* rast_div_retry_handler: on a divide overflow, `sar dx,1; rcr
 * ax,1`, then 0x7fff if the halved high word equals the divisor, else the
 * `idiv` again -- which, overflowing, re-enters the handler. */
static int16_t idiv_retry(int32_t num, int16_t den) {
    int k;
    for (k = 0; k < 40; k++) {
        if (den != 0 && !(den == -1 && num == INT32_MIN)) {
            int32_t q = num / den;
            if (q >= -32768 && q <= 32767) return (int16_t)q;
        }
        num >>= 1;
        if ((int16_t)(uint16_t)((uint32_t)num >> 16) == den) return 0x7fff;
    }
    return 0x7fff;          /* a negative dividend over zero never gets there */
}

static int16_t lerp_retry(int16_t delta, int16_t num, int16_t den) {
    int32_t p = (int32_t)delta * (int32_t)num;
    int16_t q, half;
    p = (int32_t)((uint32_t)p << 1);
    q = idiv_retry(p, den);
    half = (int16_t)(q >> 1);
    if ((q & 1) && half >= 0) half = (int16_t)(half + 1);
    return half;
}

static int16_t sub16(int16_t a, int16_t b) {
    return (int16_t)((uint16_t)a - (uint16_t)b);
}

static int16_t add16(int16_t a, int16_t b) {
    return (int16_t)((uint16_t)a + (uint16_t)b);
}

static int16_t neg16(int16_t a) {
    return (int16_t)(0u - (uint16_t)a);        /* NEG: 0x8000 stays 0x8000 */
}

/* The signed distance each pass uses. The sign convention differs between
 * the planes -- x > z measures x - z where y > z measures z - y -- but only
 * the RATIO d_a / (d_a - d_b) is ever used, so the two agree. */
static int16_t plane_d(const uw_rast_cvert *v, uint8_t bit) {
    switch (bit) {
    case UW_OUT_ABOVE: return sub16(v->z, v->y);   /* y >  z */
    case UW_OUT_BELOW: return add16(v->z, v->y);   /* y < -z */
    case UW_OUT_RIGHT: return sub16(v->x, v->z);   /* x >  z */
    default:           return add16(v->z, v->x);   /* x < -z */
    }
}

/* The crossing on the edge a -> b. */
static uw_rast_cvert cross(const uw_rast_cvert *a, const uw_rast_cvert *b,
                           uint8_t bit) {
    uw_rast_cvert r;
    int16_t num = plane_d(a, bit);
    int16_t den = sub16(num, plane_d(b, bit));

    r.x = add16(a->x, lerp(sub16(b->x, a->x), num, den));
    r.y = add16(a->y, lerp(sub16(b->y, a->y), num, den));
    /* Not interpolated: taken from the coordinate the plane equates it to. */
    switch (bit) {
    case UW_OUT_ABOVE: r.z = r.y; break;
    case UW_OUT_BELOW: r.z = neg16(r.y); break;
    case UW_OUT_RIGHT: r.z = r.x; break;
    default:           r.z = neg16(r.x); break;
    }
    r.outcode = uw_rast_outcode(r.x, r.y, r.z);
    r.u = add16(a->u, lerp(sub16(b->u, a->u), num, den));
    r.v = add16(a->v, lerp(sub16(b->v, a->v), num, den));
    return r;
}

/* The shaded crossing: x and y through the retrying divide, the shade from
 * `cur` and `next` whichever edge it is on. */
static uw_rast_cvert cross_shaded(const uw_rast_cvert *a, const uw_rast_cvert *b,
                                  const uw_rast_cvert *cur,
                                  const uw_rast_cvert *next, uint8_t bit) {
    uw_rast_cvert r;
    int16_t num = plane_d(a, bit);
    int16_t den = sub16(num, plane_d(b, bit));
    int8_t ds = (int8_t)(uint8_t)((uint8_t)cur->u - (uint8_t)next->u);
    int16_t q;

    r.x = add16(a->x, lerp_retry(sub16(b->x, a->x), num, den));
    r.y = add16(a->y, lerp_retry(sub16(b->y, a->y), num, den));
    switch (bit) {
    case UW_OUT_ABOVE: r.z = r.y; break;
    case UW_OUT_BELOW: r.z = neg16(r.y); break;
    case UW_OUT_RIGHT: r.z = r.x; break;
    default:           r.z = neg16(r.x); break;
    }
    r.outcode = uw_rast_outcode(r.x, r.y, r.z);
    q = idiv_retry((int32_t)ds * num, den);
    r.u = (int16_t)(uint8_t)((uint8_t)q + (uint8_t)next->u);
    r.v = 0;
    return r;
}

void uw_rast_clip_plane_shaded(uw_rast_clip *c, uint8_t bit) {
    uw_rast_cvert *src = c->buf[c->src];
    uw_rast_cvert *dst = c->buf[c->src ^ 1];
    int n = c->count, out = 0, i;

    if (bit != UW_OUT_LEFT && bit != UW_OUT_RIGHT &&
        bit != UW_OUT_ABOVE && bit != UW_OUT_BELOW) return;
    if (n < 2 || n > UW_CLIP_MAX) return;

    c->or_code = 0;
    c->and_code = 0xff;
    src[n] = src[0];
    src[n + 1] = src[1];

    for (i = 1; i <= n; i++) {
        const uw_rast_cvert *cur = &src[i];
        uint8_t code = (uint8_t)cur->outcode;

        if (!(code & bit)) {
            c->or_code |= code;
            c->and_code &= code;
            dst[out++] = *cur;
            continue;
        }
        if (!((uint8_t)src[i - 1].outcode & bit)) {
            dst[out] = cross_shaded(&src[i - 1], cur, cur, &src[i + 1], bit);
            c->or_code |= (uint8_t)dst[out].outcode;
            c->and_code &= (uint8_t)dst[out].outcode;
            out++;
        }
        if (!((uint8_t)src[i + 1].outcode & bit)) {
            dst[out] = cross_shaded(cur, &src[i + 1], cur, &src[i + 1], bit);
            c->or_code |= (uint8_t)dst[out].outcode;
            c->and_code &= (uint8_t)dst[out].outcode;
            out++;
        }
    }
    c->src ^= 1;
    c->count = out;
}

void uw_rast_clip_accumulate(uw_rast_clip *c) {
    int i;
    c->or_code = 0;
    c->and_code = 0xff;
    for (i = 0; i < c->count; i++) {
        uint8_t k = (uint8_t)c->buf[c->src][i].outcode;
        c->or_code |= k;
        c->and_code &= k;
    }
}

void uw_rast_clip_plane(uw_rast_clip *c, uint8_t bit) {
    uw_rast_cvert *src = c->buf[c->src];
    uw_rast_cvert *dst = c->buf[c->src ^ 1];
    int n = c->count, out = 0, i;

    if (bit != UW_OUT_LEFT && bit != UW_OUT_RIGHT &&
        bit != UW_OUT_ABOVE && bit != UW_OUT_BELOW) return;
    if (n < 2 || n > UW_CLIP_MAX) return;

    c->or_code = 0;
    c->and_code = 0xff;
    /* `mov cx,0xc; rep movsw` -- two vertices, then `add [0x305],0xc`
     * counts only the first. The second is the lookahead's scratch. */
    src[n] = src[0];
    src[n + 1] = src[1];

    for (i = 1; i <= n; i++) {
        const uw_rast_cvert *cur = &src[i];
        uint8_t code = (uint8_t)cur->outcode;

        if (!(code & bit)) {                   /* inside: copied through */
            c->or_code |= code;
            c->and_code &= code;
            dst[out++] = *cur;
            continue;
        }
        /* Outside. The edge from the previous vertex leaves the half-space
         * if that one was inside; the edge to the next one re-enters it if
         * THAT one is inside -- which is why the entering crossing is
         * emitted here and not on the next iteration. */
        if (!((uint8_t)src[i - 1].outcode & bit)) {
            dst[out] = cross(&src[i - 1], cur, bit);
            c->or_code |= (uint8_t)dst[out].outcode;
            c->and_code &= (uint8_t)dst[out].outcode;
            out++;
        }
        if (!((uint8_t)src[i + 1].outcode & bit)) {
            dst[out] = cross(cur, &src[i + 1], bit);
            c->or_code |= (uint8_t)dst[out].outcode;
            c->and_code &= (uint8_t)dst[out].outcode;
            out++;
        }
    }
    c->src ^= 1;
    c->count = out;
}

int uw_rast_clip_face(uw_rast_clip *c) {
    /* The original's clipping order, which is not the bit order. */
    static const uint8_t order[4] = { UW_OUT_ABOVE, UW_OUT_BELOW,
                                      UW_OUT_LEFT, UW_OUT_RIGHT };
    int round, k;

    if (c->and_code) return 0;
    if (!c->or_code) return c->count;

    for (round = 0; round < 2; round++) {
        for (k = 0; k < 4; k++) {
            if (!(c->or_code & order[k])) continue;
            uw_rast_clip_plane(c, order[k]);
            if (c->and_code) return 0;
        }
        if (!c->or_code) return c->count;
    }
    return 0;                   /* still outside after the second round */
}

int uw_rast_project(const uw_rast_clip *c, const uw_rast_proj *p,
                    uw_rast_svert *out) {
    const uw_rast_cvert *v = c->buf[c->src];
    int i;

    for (i = 0; i < c->count; i++) {
        out[i].x = v[i].x;
        out[i].y = v[i].y;
        out[i].z = v[i].z;
        out[i].u = v[i].u;
        out[i].v = v[i].v;
        out[i].sx = add16(idiv_sat((int32_t)v[i].x * (int32_t)p->scale_x,
                                   v[i].z), p->off_x);
        out[i].sy = add16(idiv_sat((int32_t)v[i].y * (int32_t)p->scale_y,
                                   v[i].z), p->off_y);
    }
    return c->count;
}

int uw_rast_draw_face(uw_rast_clip *c, const uw_rast_proj *p, int noclip,
                      uw_rast_svert *out) {
    if (!noclip && uw_rast_clip_face(c) == 0) return 0;
    return uw_rast_project(c, p, out);
}

/* ---- the gather and the subdivision ----------------------------------- */

void uw_rast_gather_poly(uw_rast_clip *c, const uw_rast_slots *s,
                         const uw_rast_texrec *tex,
                         const uw_rast_polyv *v, int n) {
    int i;

    c->src = 0;                                  /* `mov di,0x309` */
    c->count = n;
    c->or_code = 0;
    c->and_code = 0xff;
    for (i = 0; i < n && i < UW_CLIP_MAX; i++) {
        uw_rast_cvert *w = &c->buf[0][i];
        int16_t xyz[3];
        uint8_t code;

        uw_rast_slot_get(s, v[i].slot_off, xyz);
        code = s->outcode[(v[i].slot_off + 6) & (UW_RAST_SLOT_WINDOW - 1)];
        w->x = xyz[0];
        w->y = xyz[1];
        w->z = xyz[2];
        /* `mov ax,[bx+0x1624]; stosw; mov al,[bx+0x1626]; stosw` -- AH is
         * still the z coordinate's high byte when the second store runs. */
        w->outcode = (uint16_t)(((uint16_t)xyz[2] & 0xff00u) | code);
        c->or_code |= code;
        c->and_code &= code;
        w->u = (int16_t)((v[i].corner & 1)
                         ? (int16_t)((uint16_t)(((tex->width - 1) & 0xff) << 8)
                                     | 0xffu)
                         : 0);
        w->v = (int16_t)(v[i].corner >= 2 ? tex->v_max : 0);
    }
}

int16_t uw_rast_midpoint(int16_t a, int16_t b) {
    /* `add ax,bx` -- and the 8086's OF says whether the true sum needed a
     * seventeenth bit. It did: `rcr ax,1` rotates the carry back in as the
     * new top bit, which is the true average. It did not: the hidden
     * `sar ax,1` halves it, arithmetically, so the result floors. */
    uint16_t sum = (uint16_t)((uint16_t)a + (uint16_t)b);
    int of = (((uint16_t)a ^ sum) & ((uint16_t)b ^ sum) & 0x8000u) != 0;
    if (of) {
        /* CF is the carry out of the add, which for a signed overflow is
         * the true sum's bit 16. */
        uint16_t cf = (uint16_t)(((uint32_t)(uint16_t)a
                                  + (uint32_t)(uint16_t)b) >> 16);
        return (int16_t)((uint16_t)(sum >> 1) | (uint16_t)(cf << 15));
    }
    return (int16_t)((int16_t)sum >> 1);
}

static uw_rast_cvert mid_vert(const uw_rast_cvert *a, const uw_rast_cvert *b) {
    uw_rast_cvert r;
    r.x = uw_rast_midpoint(a->x, b->x);
    r.y = uw_rast_midpoint(a->y, b->y);
    r.z = uw_rast_midpoint(a->z, b->z);
    r.outcode = uw_rast_outcode(r.x, r.y, r.z);
    r.u = uw_rast_midpoint(a->u, b->u);
    r.v = uw_rast_midpoint(a->v, b->v);
    return r;
}

int uw_rast_should_subdivide(const uw_rast_clip *c, int shader,
                             int near_shift) {
    int i;
    int16_t bound = (int16_t)(1u << (near_shift & 15));

    if (c->count != 4 || shader != 0x545) return 0;
    if (c->or_code) return 1;                 /* outside any plane at all */
    for (i = 0; i < 4; i++)
        if (bound > c->buf[c->src][i].z) return 1;    /* `cmp ax,z; jg` */
    return 0;
}

void uw_rast_subdivide(const uw_rast_clip *c, uw_rast_cvert out[4][4]) {
    const uw_rast_cvert *v = c->buf[c->src];
    uw_rast_cvert m[4], centre;
    int i;

    /* The path is only reachable with four -- `cmp [0xb00c],4`
     * is what emit_poly3 fails -- and the record layout it builds assumes
     * it. */
    if (c->count != 4) return;

    for (i = 0; i < 4; i++) m[i] = mid_vert(&v[i], &v[(i + 1) & 3]);
    /* The centre is the midpoint of two OPPOSITE EDGE MIDPOINTS, not of the
     * four corners: `[0xb01a] + [0xb04a]` is m01 and m23. For a planar quad
     * the two agree; for one whose corners are not coplanar they do not. */
    centre = mid_vert(&m[0], &m[2]);

    /* The original's order of the four sub-quads. Each keeps the
     * parent's winding. */
    out[0][0] = v[0]; out[0][1] = m[0]; out[0][2] = centre; out[0][3] = m[3];
    out[1][0] = m[0]; out[1][1] = v[1]; out[1][2] = m[1]; out[1][3] = centre;
    out[2][0] = m[3]; out[2][1] = centre; out[2][2] = m[2]; out[2][3] = v[3];
    out[3][0] = centre; out[3][1] = m[1]; out[3][2] = v[2]; out[3][3] = m[2];
}

void uw_rast_gather_poly_bytes(uw_rast_clip *c, const uw_rast_slots *s,
                               const uw_rast_texrec *tex,
                               const uint8_t *slot_index, int n) {
    int i;

    c->src = 0;
    c->count = n;
    c->or_code = 0;
    c->and_code = 0xff;
    for (i = 0; i < n && i < UW_CLIP_MAX; i++) {
        uw_rast_cvert *w = &c->buf[0][i];
        int off = (slot_index[i] & 0xff) << 3;   /* `lodsb; shl ax,3` */
        int16_t xyz[3];
        uint8_t code;
        /* CL runs n, n-1, ... 1, and the two `test bl,2` gates below are
         * what turn that into the four corners. */
        int cl = n - i;

        uw_rast_slot_get(s, off, xyz);
        code = s->outcode[(off + 6) & (UW_RAST_SLOT_WINDOW - 1)];
        w->x = xyz[0];
        w->y = xyz[1];
        w->z = xyz[2];
        w->outcode = (uint16_t)(((uint16_t)xyz[2] & 0xff00u) | code);
        c->or_code |= code;
        c->and_code &= code;
        w->u = (int16_t)((((cl - 2) & 2) == 0)
                         ? (int16_t)((uint16_t)(tex->width - 1) << 8) : 0);
        w->v = (int16_t)((((cl - 1) & 2) == 0) ? tex->v_max : 0);
    }
}

void uw_rast_gather_poly_uv(uw_rast_clip *c, const uw_rast_slots *s,
                            const uw_rast_texrec *tex,
                            const uint16_t *slot_u_v, int n) {
    int i;

    c->src = 0;
    c->count = n;
    c->or_code = 0;
    c->and_code = 0xff;
    for (i = 0; i < n && i < UW_CLIP_MAX; i++) {
        uw_rast_cvert *w = &c->buf[0][i];
        int off = slot_u_v[i * 3] & 0x7f8;
        uint32_t un = slot_u_v[i * 3 + 1], vn = slot_u_v[i * 3 + 2];
        int16_t xyz[3];
        uint8_t code;

        uw_rast_slot_get(s, off, xyz);
        code = s->outcode[(off + 6) & (UW_RAST_SLOT_WINDOW - 1)];
        w->x = xyz[0];
        w->y = xyz[1];
        w->z = xyz[2];
        w->outcode = (uint16_t)(((uint16_t)xyz[2] & 0xff00u) | code);
        c->or_code |= code;
        c->and_code &= code;
        /* `mul [bp+0]; mov al,ah; mov ah,dl` -- bits 8..23 of the product,
         * which is the width scaled into the 8.8 the span steps in. */
        w->u = (int16_t)(uint16_t)((un * (uint32_t)(uint16_t)tex->width)
                                   >> 8);
        /* `mul [bp+2]; mov ax,dx` -- the high word, so v is the record's
         * v_max scaled by a 0..1 fraction. */
        w->v = (int16_t)(uint16_t)((vn * (uint32_t)(uint16_t)tex->v_max)
                                   >> 16);
    }
}
