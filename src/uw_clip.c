/* SPDX-License-Identifier: MIT */
/* See uw_clip.h. Ported from gfx_clip_polygon_viewport. */

#include "uw_clip.h"

/* The retail values. */
const uw_clip_rect uw_clip_viewport = { 0, 0, 319, 199 };

/* The original does all of this in 16-bit registers, so every intermediate
 * wraps. Keeping that is the whole point of porting it at all. */
static int16_t w16(int32_t v)
{
    return (int16_t)(uint16_t)((uint32_t)v & 0xffffu);
}

uw_pt uw_clip_intersect(uw_pt a, uw_pt b, int axis, int16_t bound,
                        int16_t bias, const uw_clip_rect *r)
{
    int16_t num, run, den;
    int32_t den32;
    int16_t val;
    uw_pt out;

    if (axis) {
        /* Compare y, interpolate x -- gfx_clip_interp_x. */
        num   = w16((int32_t)b.x - (int32_t)a.x);
        run   = w16((int32_t)bound - (int32_t)a.y);
        den32 = (int32_t)b.y - (int32_t)a.y;
    } else {
        /* Compare x, interpolate y -- gfx_clip_interp_y. */
        num   = w16((int32_t)b.y - (int32_t)a.y);
        run   = w16((int32_t)bound - (int32_t)a.x);
        den32 = (int32_t)b.x - (int32_t)a.x;
    }

    /* `add cx,bp; jo` guards the SUBTRACTION that builds the denominator, not
     * the divide. On overflow the 17-bit true sum is (CF:cx), and `rcr cx,1`
     * shifts the carry back in as bit 15 -- so the halving is EXACT. `sar
     * dx,1` halves the other factor to compensate, and that one does lose a
     * bit. The retry jumps past the `jo`, so it happens at most once, which is
     * all it needs: a halved 17-bit value fits. */
    if (den32 > 32767 || den32 < -32768) {
        den = (int16_t)(den32 >> 1);
        run = (int16_t)(run >> 1);
    } else {
        den = (int16_t)den32;
    }

    /* The original runs a bare `idiv` here and lets `INT 00h` catch what goes
     * wrong -- and that vector is the *divide error*, so it covers a quotient
     * that will not fit as well as a zero denominator. gfx_execute_draw_list
     * installs a handler for exactly this. A portable program has neither the
     * vector nor a defined behaviour to catch.
     *
     * So the two cases part company here, and only one of them is faithful.
     * A zero denominator is unreachable from uw_clip_edge_pass -- equal
     * components put both endpoints on the same side, which is keep or drop,
     * never a crossing -- so the guard below only matters because
     * uw_clip_intersect is public, and 1 is as good as anything.
     * A quotient too wide for 16 bits is NOT unreachable, and this port
     * wraps it (see w16) where the original traps. What the original's
     * handler then does with the primitive is unread, so there is nothing
     * yet to be faithful to; when it is read, this is the line to revisit. */
    if (den == 0)
        den = 1;

    /* imul/idiv: a 32-bit product divided back to 16, truncating toward zero
     * exactly as C does. */
    val = w16((int32_t)((int32_t)num * (int32_t)run) / (int32_t)den);
    val = w16((int32_t)val + (int32_t)(axis ? a.x : a.y));

    /* The bias is GATED, not clamped: the bounds decide whether to add it,
     * and a value outside them is emitted untouched. */
    if (axis) {
        if (val > r->left && val < r->right)
            val = w16((int32_t)val + (int32_t)bias);
        out.x = val;
        out.y = bound;
    } else {
        if (val < r->bottom && val > r->top)
            val = w16((int32_t)val + (int32_t)bias);
        out.x = bound;
        out.y = val;
    }
    return out;
}

static int edge_pass(const uw_pt *in, int n, uw_pt *out, int axis,
                     int16_t bound, bool keep_above, int16_t enter_bias,
                     const uw_clip_rect *r);

int uw_clip_edge_pass(const uw_pt *in, int n, uw_pt *out,
                      int axis, int16_t bound, bool keep_above,
                      bool neg_bias, const uw_clip_rect *r)
{
    return edge_pass(in, n, out, axis, bound, keep_above,
                     (int16_t)(neg_bias ? -1 : 1), r);
}

static int edge_pass(const uw_pt *in, int n, uw_pt *out, int axis,
                     int16_t bound, bool keep_above, int16_t enter_bias,
                     const uw_clip_rect *r)
{
    int i, w = 0;

    for (i = 0; i < n; i++) {
        uw_pt a = in[i], b = in[i + 1];       /* in[n] == in[0]: closed */
        int16_t ca = axis ? a.y : a.x;
        int16_t cb = axis ? b.y : b.x;
        /* Strict, and the direction is the opcode byte the caller patched in:
         * 0x7f JG is keep_above, 0x7c JL is not. */
        bool ain = keep_above ? (ca > bound) : (ca < bound);
        bool bin = keep_above ? (cb > bound) : (cb < bound);

        if (ain && bin) {                                  /* gfx_clip_case_keep */
            out[w++] = a;
        } else if (ain) {                                  /* ..._case_leave_bias */
            out[w++] = a;
            out[w++] = uw_clip_intersect(a, b, axis, bound, 0, r);
        } else if (bin) {                                  /* ..._case_enter_bias */
            out[w++] = uw_clip_intersect(a, b, axis, bound, enter_bias, r);
        }                                                  /* else ..._case_drop */
    }
    return w;
}

/* The pre-scan in front of each axis's pair of passes. Note it is INCLUSIVE
 * where the passes are strict: a polygon with a vertex exactly on an edge
 * skips the passes here, but would have been cut by them. That asymmetry is
 * in the original, and it is why a vertex at y == 199 survives untouched while
 * one at y == 198 next to a vertex at y == 250 does not. */
static bool all_within(const uw_pt *p, int n, int axis, int16_t lo, int16_t hi)
{
    int i;
    for (i = 0; i < n; i++) {
        int16_t c = axis ? p[i].y : p[i].x;
        if (c > hi || c < lo)
            return false;
    }
    return true;
}

static int clip_polygon(uw_pt *poly, int n, const uw_clip_rect *r, int biased);

int uw_clip_polygon(uw_pt *poly, int n, const uw_clip_rect *r)
{
    return clip_polygon(poly, n, r, 1);
}

int uw_clip_polygon_unbiased(uw_pt *poly, int n, const uw_clip_rect *r)
{
    return clip_polygon(poly, n, r, 0);
}

static int clip_polygon(uw_pt *poly, int n, const uw_clip_rect *r, int biased)
{
    uw_pt buf[2 * (UW_CLIP_MAX_VERTS + 1) + 1];
    int i;

    if (n <= 0)
        return 0;
    if (n > UW_CLIP_MAX_VERTS)
        return -1;

    /* Four passes in the original's order, each: axis, bound, keep_above,
     * neg_bias. The neg_bias column is which ENTRY POINT of the interpolator
     * the driver installs -- 0x3ecc/0x3e91 skip the leading `neg`, 0x3ec8 and
     * 0x3e8d do not -- so the two edges of an axis round opposite ways. */
    static const struct { int axis; bool keep_above, neg_bias; int which; }
    pass[4] = {
        { 1, false, false, 3 },   /* y < bottom */
        { 1, true,  true,  0 },   /* y > top    */
        { 0, true,  false, 1 },   /* x > left   */
        { 0, false, true,  2 },   /* x < right  */
    };

    for (i = 0; i < 4; i++) {
        int16_t bound;
        int m;

        /* The pre-scan covers an axis's PAIR of passes: skip both, or neither. */
        if ((i & 1) == 0) {
            bool skip = pass[i].axis
                ? all_within(poly, n, 1, r->top, r->bottom)
                : all_within(poly, n, 0, r->left, r->right);
            if (skip) {
                i++;                    /* the loop's own i++ skips the second */
                continue;
            }
        }

        switch (pass[i].which) {
        case 0:  bound = r->top;    break;
        case 1:  bound = r->left;   break;
        case 2:  bound = r->right;  break;
        default: bound = r->bottom; break;
        }

        poly[n] = poly[0];              /* close it, as the original does */
        m = edge_pass(poly, n, buf, pass[i].axis, bound, pass[i].keep_above,
                      (int16_t)(!biased ? 0 : pass[i].neg_bias ? -1 : 1), r);
        if (m == 0)
            return 0;
        if (m > UW_CLIP_MAX_VERTS)
            return -1;
        for (n = 0; n < m; n++)
            poly[n] = buf[n];
    }
    return n;
}
