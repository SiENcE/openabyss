/* SPDX-License-Identifier: MIT */
/* Sutherland-Hodgman polygon clipping against the viewport.
 *
 * Ported from `gfx_clip_polygon_viewport` and the seven small
 * routines it dispatches through. The algorithm itself is textbook; three
 * things about THIS implementation are not, and each of them is visible on
 * screen if you get it wrong.
 *
 *   * THE COMPARISONS ARE STRICT, and the four of them do not point the same
 *     way: inside means `y < bottom`, `y > top`, `x > left`, `x < right`. A
 *     vertex sitting exactly on an edge counts as OUTSIDE and is replaced by
 *     an interpolated one. Writing the natural `<=` drops the tie-breaking
 *     the next point depends on.
 *
 *   * A +/-1 BIAS is added to an interpolated coordinate, but only when that
 *     coordinate lands strictly inside the other axis's bounds. Its sign
 *     flips between the two edges of an axis, because the original installs
 *     two different entry points of the same interpolator -- one four bytes
 *     later, past a `neg`. That is the fill rule: a polygon lying on a
 *     boundary rounds one way at the near edge and the other at the far edge,
 *     so it cannot double-cover a pixel with its neighbour.
 *
 *     Note what the bias is NOT: the bounds only GATE it. Nothing is clamped.
 *     A vertex interpolated outside the other axis's range is emitted exactly
 *     as computed.
 *
 *   * ON DIVIDE OVERFLOW the original does not trap and does not give up: it
 *     halves numerator and denominator and retries, as many times as it
 *     takes. We reproduce that rather than widening to 32 bits, because the
 *     precision it loses is precision the original lost too.
 *
 * The one thing here that CANNOT be ported is how the original chose its four
 * comparisons: `gfx_clip_edge_pass` writes the caller's `al` over the opcode
 * byte of its own two conditional jumps and then far-jumps to the next
 * instruction to flush the prefetch queue. Self-modifying code. It is a
 * parameter here, which is the same function and a different program.
 */
#ifndef UW_CLIP_H
#define UW_CLIP_H

#include "uw.h"

/* The original's two ping-pong buffers are 0x190 bytes each: 100 vertices of
 * (x, y). A caller may not exceed that, and neither may the clip -- see
 * uw_clip_polygon. */
#define UW_CLIP_MAX_VERTS 100

/* The viewport. In the retail build these read 0, 0, 319, 199 out of UW.EXE,
 * which is what identifies the rectangle as the 320x200 screen rather than
 * some arbitrary window. */
typedef struct {
    int16_t top, left, right, bottom;
} uw_clip_rect;

/* The retail viewport, for callers that just want it. */
extern const uw_clip_rect uw_clip_viewport;

typedef struct {
    int16_t x, y;
} uw_pt;

/* Clip `poly` (`n` vertices, in place) to `r`, returning the new vertex count.
 *
 * Returns 0 for a polygon that is entirely outside, and -1 if the clip would
 * need more than UW_CLIP_MAX_VERTS. The original guards this ONE LEVEL UP:
 * its sole caller `gfx_draw_polygon` opens with `if (cx > 99) return`, which
 * is the same 100-vertex buffer seen from the other side. It does not
 * re-check after a pass, though, and a pass can emit two vertices per edge.
 *
 * `poly` must have room for UW_CLIP_MAX_VERTS + 1 entries: the polygon is
 * closed in place by appending vertex 0, exactly as the original does.
 */
int uw_clip_polygon(uw_pt *poly, int n, const uw_clip_rect *r);

/* The same clip through the second entry, gfx_clip_polygon_viewport_unbiased,
 * which installs the unbiased case pair instead of the biased one: its
 * "enter" case jumps into the middle of the "leave" case and so interpolates
 * with a bias of 0 on both. gfx_polygon_fill_general is its caller. */
int uw_clip_polygon_unbiased(uw_pt *poly, int n, const uw_clip_rect *r);

/* One pass, over one edge -- `gfx_clip_edge_pass`. Exposed because the four
 * passes are where the strictness and the bias sign live, and testing them
 * one at a time is the only way to pin those down separately.
 *
 * `axis` is 1 to compare y (and interpolate x), 0 to compare x. `keep_above`
 * selects the strict comparison: true means inside is `> bound` (the original
 * patching in JG), false means inside is `< bound` (JL). `neg_bias` flips the
 * sign of the +1 applied to an interpolated coordinate.
 *
 * Returns the number of vertices written to `out`, which must have room for
 * 2 * n + 1.
 */
int uw_clip_edge_pass(const uw_pt *in, int n, uw_pt *out,
                      int axis, int16_t bound, bool keep_above,
                      bool neg_bias, const uw_clip_rect *r);

/* The crossing point of the segment a->b with `bound` on `axis`, biased and
 * gated exactly as `gfx_clip_interp_x` / `gfx_clip_interp_y` do it.
 *
 * Exposed for the overflow test: `bias` is added only when the interpolated
 * coordinate lands strictly inside the other axis's bounds. */
uw_pt uw_clip_intersect(uw_pt a, uw_pt b, int axis, int16_t bound,
                        int16_t bias, const uw_clip_rect *r);

#endif
