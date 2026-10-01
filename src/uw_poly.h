/* SPDX-License-Identifier: MIT */
/* Flat polygons: gfx_draw_polygon and what it runs after the
 * viewport clip -- gfx_polygon_to_spans and the fill
 * gfx_set_colour_mode_spans installs for a solid colour, gfx_fill_span_list
 * -- for the model faces a draw list emits after set_colour_lit.
 *
 * THE SPANS ARE RECORDS IN THE MODULE'S SEGMENT, NOT A PER-CALL ARRAY. Six
 * bytes a row, counting down from a base as y rises: a row word the module pre-fills with y,
 * the left x, the right x. A polygon's rows run from its greatest y DOWN,
 * which is UP the table, and the record after its last row has bit 15 of its
 * row word set as the terminator -- a word remembers which, so the next
 * polygon can clear it. Nothing else is cleared: a row one chain writes and
 * the other does not keeps the other's x from whichever polygon last wrote
 * it. A convex polygon never shows that; the port keeps the table so that
 * one that is not shows what the original shows.
 *
 * THE TWO CHAINS ARE NOT LEFT AND RIGHT. From the first vertex with the
 * greatest y, the "left" edges are walked BACKWARD round the ring and the
 * "right" ones forward, each stopping at the first edge that climbs. Which
 * of the two is on the left of the screen is the polygon's winding, and the
 * fill does not sort them: a row whose left x is past its right x is filled
 * from right + 1 to left, and one whose left is exactly right + 1 is not
 * filled at all. Only the LAST row is sorted.
 *
 * THE EDGE STEP is 16.16 built from two divides -- `idiv` for the integer
 * part, then the remainder shifted up fifteen, divided again and doubled --
 * with the fraction accumulator starting at 0x7fff, not 0x8000. An edge
 * writes its top row's x unstepped and then dy - 1 stepped ones; the row it
 * ends on is the next edge's first. */
#ifndef UW_POLY_H
#define UW_POLY_H

#include "uw.h"
#include "uw_clip.h"
#include "uw_texmap.h"

/* Rows -2 .. 253. The records below row 0 are real -- row -1's word is the
 * terminator a polygon reaching row 0 sets -- and those past about 209 run
 * into the clip's own variables, which no clipped polygon
 * reaches. */
#define UW_SPAN_Y_MIN  (-2)
#define UW_SPAN_ROWS   256

typedef struct {
    uint16_t row[UW_SPAN_ROWS];     /* +0, bit 15 the terminator */
    int16_t  left[UW_SPAN_ROWS];    /* +2 */
    int16_t  right[UW_SPAN_ROWS];   /* +4 */
    int      term;                  /* the terminator, as a row */
    int16_t  x_left, x_right;       /* each chain's last edge's far x,
                                     * which outlive a call */
    long     overrun;               /* records addressed outside the table */
} uw_spans;

/* The table as the module leaves it before any polygon: each row word its
 * own row, row -1 already cleared of a terminator and row -2 holding one. */
void uw_spans_init(uw_spans *s);

/* The table out of the graphics module's segment image (64K), for a harness
 * that wants the stale x's a state really has. */
void uw_spans_load(uw_spans *s, const uint8_t *gfx_ds);

/* gfx_polygon_to_spans. `v` is `n` screen vertices after the viewport clip.
 * Returns the row the fill starts at -- the polygon's greatest y. */
int uw_gfx_polygon_to_spans(uw_spans *s, const uw_pt *v, int n);

/* gfx_fill_span_list: from row `y` down to the terminator, `colour` into
 * `fb`. No clipping of its own -- `mov bx,[0x3df2]` loads the left bound
 * and nothing reads it. Returns the pixels written. */
long uw_gfx_fill_span_list(const uw_fb *fb, uw_spans *s, int y,
                           uint8_t colour);

/* gfx_draw_polygon: more than 99 vertices draws nothing (`cmp cx,0x63; ja`),
 * two or fewer is a line or a point that this module does not draw (returns
 * -1), and anything else is clipped to `r`, scanned and filled. `v` must
 * have room for UW_CLIP_MAX_VERTS + 1 entries and is clipped in place.
 * Returns the pixels written. */
long uw_gfx_draw_polygon(const uw_fb *fb, uw_spans *s, uw_pt *v, int n,
                         const uw_clip_rect *r, uint8_t colour);

/* ---- gfx_polygon_fill_general -------------------------------
 *
 * What fill_general (0x40) installs: an edge-table scan converter for a
 * polygon of any shape, clipped through the UNBIASED entry. An edge is
 * (top row, x, x at the bottom, bottom row, step) in x << 5 -- a 32nd of a
 * pixel -- with the step `((x_bottom - x_top) << 5) / rows` truncated and x
 * pre-decremented by one step, so the first row's add lands on the vertex.
 * Where a chain passes THROUGH a vertex, one of the two edges gives up the
 * shared row: an edge whose top vertex is followed by one no lower loses
 * its top row, and one whose bottom vertex is followed by one no higher
 * gains a row at the bottom -- without its x being moved, so the shortened
 * edge's first x is its vertex's, a row early. A horizontal edge is a
 * one-row edge of zero step at an extremum, two where a chain steps
 * sideways, and nothing at all otherwise; a run of them is judged from its
 * two ends.
 *
 * The rows run down from the highest top. Two active edges take a fast path
 * that orders them by their next x (then their bottom x), falls to the
 * general path if they cross by the bottom, and swaps them the moment the
 * left passes the right; an odd number spans the least to the greatest x;
 * an even number is bubble-sorted each row and spanned in pairs. Every span
 * is widened half a pixel each side -- `(left - 0x10) >> 5`, `(right +
 * 0x10) >> 5` -- and the list goes to gfx_fill_span_list. Returns the
 * pixels written; `v` needs UW_CLIP_MAX_VERTS + 4 entries. */
long uw_gfx_polygon_fill_general(const uw_fb *fb, uw_pt *v, int n,
                                 const uw_clip_rect *r, uint8_t colour);

#endif
