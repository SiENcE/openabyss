/* SPDX-License-Identifier: MIT */
/* The rasteriser's vertex pipeline.
 *
 * The original is hand-written 8086 assembly and a port does not
 * transliterate it. It does have to reproduce what it does: the renderer's
 * behaviour is the game's look down to which pixel a wall edge lands on.
 * These are the pieces underneath the whole-frame renderer, each ported from
 * the original's instructions and tested on the properties those
 * instructions state.
 */
#ifndef UW_RAST_H
#define UW_RAST_H

#include "uw.h"

/* ---- rast_outcode ------------------------------------------
 *
 * The clip outcode of a camera-space vertex, by comparisons only -- no
 * division, no projection. Every set-A vertex opcode calls it and stores the
 * result at the vertex slot's +6.
 *
 * For z > 0 the four bits are half-space tests against the view cone:
 *
 *     bit 0 (0x01)   x < -z      left of the cone
 *     bit 1 (0x02)   x >  z      right of it
 *     bit 2 (0x04)   y >  z      above
 *     bit 3 (0x08)   y < -z      below
 *
 * and 0 means wholly inside. THE COMPARISONS ARE STRICT: a vertex exactly on
 * a plane, x == z, is inside. Writing `>=` moves every silhouette edge by a
 * pixel where it matters and by nothing where it does not, which is the
 * worst kind of wrong.
 *
 * BIT 7 (0x80) MEANS z <= 0 -- behind the eye or on the plane -- and the
 * four low bits keep their arithmetic meaning rather than their geometric
 * one. With z <= 0 the bound -z is non-negative, so `x < -z` and `x > z` can
 * both hold at once, which they never can in front of the eye. The nine
 * values the routine can return there are 0x85, 0x86, 0x87, 0x89, 0x8a,
 * 0x8b, 0x8d, 0x8e and 0x8f: every one carries at least one x bit and at
 * least one y bit, and 0x80, 0x88 and 0x8c never occur.
 *
 * ONE EDGE CASE IS IN THE FLAGS, not in the algebra. The routine begins
 * `mov ax,bp; neg ax; jge`, and `neg` overflows for exactly one input --
 * 0x8000, where the result is 0x8000 again and OF is set. `jge` is SF == OF,
 * so that input takes the z <= 0 branch (correctly, it is negative) and then
 * compares against a bound of -32768 rather than +32768. A port that
 * computes -z in 32 bits gets a different answer for that vertex.
 */
uint8_t uw_rast_outcode(int16_t x, int16_t y, int16_t z);

/* ---- the vertex fetch and the basis rotate -----------------------------
 *
 * rast_fetch_vertex_words and rast_fetch_vertex_bytes
 * read three coordinates out of the draw list and fall THROUGH
 * into rast_rotate. Every set-A vertex opcode arrives here.
 *
 * THE LIST STORES A VERTEX AS x, z, y. The fetch writes the three values to
 * BX, BP and CX in that order, and rast_rotate reads BX, CX, BP as x, y and
 * z -- so the middle coordinate in the list is the DEPTH. Reading the list
 * as x, y, z gives a model that is correct in one axis and transposed in the
 * other two, which looks like a rotation bug and is not.
 *
 * Each coordinate is `(v - origin) << shift`, and BOTH the origin and the
 * shift are PATCHED INTO THE INSTRUCTION STREAM by rast_origin_patch: the
 * image holds `sub ax,0x1234` three times over and `mov cl,5`. There are
 * three separate origin immediates, one per axis. The byte form adds a
 * fixed `cbw; shl ax,5` in front, so a byte coordinate is a signed 8-bit
 * value in units of 32.
 *
 * All of it is 16-bit and wraps. A coordinate that overflows the shift is
 * not clamped.
 */
void uw_rast_fetch_words(const int16_t list[3], const int16_t origin[3],
                         int shift, int16_t out_xyz[3]);
void uw_rast_fetch_bytes(const int8_t list[3], const int16_t origin[3],
                         int shift, int16_t out_xyz[3]);

/* rast_rotate: v' = M v with the basis's nine words,
 *
 *     x' = hi(x*m[0]) + hi(y*m[3]) + hi(z*m[6])
 *     y' = hi(x*m[1]) + hi(y*m[4]) + hi(z*m[7])
 *     z' = hi(x*m[2]) + hi(y*m[5]) + hi(z*m[8])
 *
 * where `hi` is the HIGH WORD OF A SIGNED 16x16 IMUL -- an arithmetic shift
 * right by 16, which floors. A port that writes `(x * m) / 65536` truncates
 * toward zero instead and is off by one for every negative product, which is
 * half the vertices in any frame. The three terms are summed in a 16-bit
 * register and wrap.
 *
 * THESE BYTES ARE REPLACED AT RUN TIME. rast_select_rotate_set copies either
 * rast_rotate_general_template (this, 0x54 bytes) or rast_rotate_axis_template
 * (0x26 bytes) over them; the shipped image holds the general form, which is
 * what this is.
 */
void uw_rast_rotate(const int16_t v_xyz[3], const int16_t m[9],
                    int16_t out_xyz[3]);

/* ---- the rotate SET ----------------------------------------------------
 *
 * rast_select_rotate_set looks at the basis and installs one of
 * two implementations of the whole vertex family -- and it is the AXIS set,
 * not the general one, that is in place in ordinary play. The general form
 * above is what the image ships; it is not what a
 * frame runs.
 *
 * The test is five words: m[3], m[1], m[7] and m[5] all zero, and m[4] (the
 * y axis's own scale) at least 0x7ffb, signed. That is "the
 * camera has no pitch and no roll" -- yaw leaves all five alone, which is
 * why rast_basis_rotate_y keeps the axis set and the x and z turns do not.
 *
 * The axis set is not the general one with zeros multiplied in. It drops
 * y's multiply for a SHIFT:
 *
 *     rast_rotate_axis_template:
 *         x' = hi(x*m[0]) + hi(z*m[6])
 *         z' = hi(x*m[2]) + hi(z*m[8])
 *         y' = y >> 1                                  (`sar cx,1`)
 *
 * and hi(y*32765) is NOT y >> 1: for every positive even y it is one less.
 * That single unit is the "y off by one" a port running the general rotate
 * sees across a whole frame's slots.
 *
 * The set is sticky. The rotating sub-list calls select after they turn the
 * basis and do NOT call it again after they restore it, so a general set
 * installed inside a pitched sub-list stays installed afterwards -- which is
 * harmless arithmetic but different rounding, and a port has to keep the
 * same state to round the same way. */
int  uw_rast_select_rotate_set(const int16_t m[9]);   /* 1: the axis set */
void uw_rast_rotate_axis(const int16_t v_xyz[3], const int16_t m[9],
                         int16_t out_xyz[3]);

/* ---- the vertex slots and the opcodes that fill them --------------------
 *
 * The slot array is eight bytes a record: x at +0, y at +2,
 * z at +4, the outcode at +6. THE OPCODE OPERAND IS A BYTE OFFSET, not a
 * slot number -- `lodsw; mov di,ax; mov [di+array],bx` -- so the second
 * record is slot operand 8. Treating the operand as an index divides every
 * model by eight and puts most of it in slot 0.
 *
 * The handlers:
 *
 *   emit_vertex_word (0x7a)   three words through the fetch and the rotate,
 *                             then a word slot. Four operand words.
 *   emit_vertex      (0xb6)   the same with three signed bytes and a byte
 *                             slot -- WHICH IS AN INDEX, NOT AN OFFSET.
 *                             `lodsb; xor ah,ah; shl ax,3`:
 *                             the byte form scales, the word form does not.
 *                             The shipped lists use both, and a reader that
 *                             treats them alike puts every byte-emitted
 *                             vertex at an eighth of its address -- the
 *                             pillars scene emits into indices 200..203,
 *                             which as offsets would be four records
 *                             overlapping at 200.
 *   vertex_add_u/v/w (0x2a..) (src, scale, dest): dest = src + the high
 *                             words of `scale << [0x2880]` times basis row
 *                             0, 1 or 2 -- m[0..2], m[3..5], m[6..8]. Note
 *                             this is a ROW where rast_rotate reads
 *                             COLUMNS.
 *   vertex_sum       (0x8c)   (a, b, dest): componentwise, no scaling.
 *
 * Every one of them recomputes the outcode of the result and stores it at
 * +6. All arithmetic is 16-bit and wraps.
 */
#define UW_RAST_SLOT_BYTES 8

/* THE ARRAY IS ADDRESSED BY AN UNMASKED 16-BIT OFFSET, and 0x800 -- one past
 * the 256th record -- IS AN OFFSET THE SHIPPED MODELS USE. `vertex_sum` at
 * 0x69d6 in the static model program reads `60 00 00 08 80 00`, which is
 * slot 0x60 plus slot 0x800 into slot 0x80: the second operand points at the
 * byte after the array, where the rasteriser keeps something else. A port
 * that masks the operand to 0x7f8 turns that into slot 0 and diverges from
 * the first model that does it.
 *
 * So the window is twice the array and the host fills the tail from the
 * data segment. What lies there is zeros in most states and (0, 0x100, 0) in
 * one, which is why it cannot be assumed. */
#define UW_RAST_SLOT_WINDOW 0x1000

typedef struct {
    /* Byte-addressed, exactly as the original indexes it. */
    int16_t x[UW_RAST_SLOT_WINDOW / 2];
    uint8_t outcode[UW_RAST_SLOT_WINDOW];
    const int16_t *basis;    /* nine words */
    int16_t origin[3];       /* the three patched `sub` immediates */
    int     fetch_shift;     /* the patched CL */
    int     add_shift;       /* the vertex_add scale shift */
    int     axis;            /* which rotate set is installed */
} uw_rast_slots;

/* Whichever rotate `s->axis` says is installed -- what rast_rotate runs. */
void uw_rast_rotate_live(const uw_rast_slots *s, const int16_t v_xyz[3],
                         int16_t out_xyz[3]);

void uw_rast_slot_set(uw_rast_slots *s, int off, const int16_t xyz[3]);
void uw_rast_slot_get(const uw_rast_slots *s, int off, int16_t xyz[3]);

/* emit_vertex_word: three list words, then the slot. */
void uw_rast_emit_vertex_word(uw_rast_slots *s, const int16_t list[3],
                              int slot_off);
/* emit_vertex: three signed list bytes, then a slot INDEX which the handler
 * scales by 8. Takes the index, as the opcode does. */
void uw_rast_emit_vertex(uw_rast_slots *s, const int8_t list[3],
                         int slot_index);
/* vertex_add_u/v/w, `row` 0, 1 or 2 -- the GENERAL handlers. Opcodes 0x2a, 0x1c and 0x2c always reach these. */
void uw_rast_vertex_add(uw_rast_slots *s, int src_off, int16_t scale,
                        int row, int dest_off);
/* The same three from the AXIS set, which have no names of their own
 * because the image's dispatch table never points at them. rast_select_rotate_set writes them into the live
 * table for opcodes 0x86, 0x88 and 0x8a (and the template at 0xea..0xee).
 *
 *   u and w add through m[0]/m[2] and m[6]/m[8] only -- the same numbers as
 *   the general form when the basis passes the test -- but compute the
 *   outcode INLINE rather than through rast_outcode, and the two differ at
 *   the edges (a zero depth, a sum that overflows).
 *
 *   v adds `(scale << shift) >> 1` to y alone, copies x and z, and keeps the
 *   SOURCE's outcode but for bits 2 and 3, which it recomputes from y:
 *   above if y > z, below if y + z < 0, both if both. */
void uw_rast_vertex_add_axis(uw_rast_slots *s, int src_off, int16_t scale,
                             int row, int dest_off);
/* vertex_add_uv, _uw and _vw (0x90, 0x92, 0x94): (scale, scale, source,
 * dest) -- the scales FIRST, where the single-axis adds take the source
 * first. Rows: uv adds row 0 then row 1, uw row 0 then row 2, and vw row 2
 * then row 1, whatever the name's order. The general
 * handlers multiply each scale through its whole row; the axis ones
 * take x and z from rows 0 and 2 alone, copy y, and
 * add a row-1 scale to y as `(scale << shift) >> 1` -- and unlike the
 * single-axis v, all three call rast_outcode for the result. */
void uw_rast_vertex_add_pair(uw_rast_slots *s, int src_off, int16_t scale_a,
                             int row_a, int16_t scale_b, int row_b,
                             int dest_off, int axis);
/* vertex_sum. */
void uw_rast_vertex_sum(uw_rast_slots *s, int a_off, int b_off,
                        int dest_off);

#define UW_OUT_LEFT   0x01
#define UW_OUT_RIGHT  0x02
#define UW_OUT_ABOVE  0x04
#define UW_OUT_BELOW  0x08
#define UW_OUT_BEHIND 0x80

/* ---- the clip buffer, the four plane passes, the projection ------------
 *
 * rast_draw_face is what every emit_poly* opcode ends in. Its
 * input is a polygon of 12-byte vertices (x, y, z, outcode, u, v) in one of
 * two buffers -- 200 vertices each -- with a start and an end, and the OR
 * and AND of the vertices' outcodes already accumulated by the emitter.
 *
 * It decides in that order:
 *
 *   - the no-clip flag set (the no-clip projection block): project at once,
 *     without even looking at the outcodes -- a branch that never runs,
 *     because rast_projection_select can only clear that flag (its first
 *     instruction is `xor ax,ax`) and it is never set;
 *   - AND non-zero: every vertex is outside one plane, so the face cannot
 *     be visible -- return without drawing;
 *   - OR zero: wholly inside, project;
 *   - otherwise one Sutherland-Hodgman pass per set bit, in the order
 *     y>z, y<-z, x<-z, x>z,
 *     re-testing AND after each and
 *     running the four AGAIN if anything is still outside. STILL outside
 *     after the second round and the face is dropped, not clipped further.
 *
 * A pass reads one buffer and writes the other, so the two alternate. It
 * first copies the polygon's FIRST TWO vertices onto the end of its own
 * source -- the first closes the ring, the second is what the last
 * iteration's lookahead reads -- which is why the buffers have slack.
 *
 * THE NEW VERTEX'S THIRD COORDINATE IS NOT INTERPOLATED. On the y > z
 * plane the crossing has y == z, so the routine stores the interpolated y
 * twice; on y < -z it stores the negation; on the x planes it stores the
 * interpolated x, negated for x < -z. Interpolating z like the others would
 * be right to within a rounding error and wrong in the bit that matters,
 * because the outcode of the new vertex is then computed FROM THAT z and
 * has to come out clean.
 *
 * Every interpolation is `a + round(delta * d_a / (d_a - d_b))` computed as
 * `imul; shl/rcl; idiv; sar; adc` -- the product doubled, divided, then
 * halved with the bit that fell out added back when the result is
 * non-negative. That is round-half-away-from-zero, and it is not what
 * `(int32_t)delta * d / den` gives.
 *
 * AND THE DIVIDE SATURATES RATHER THAN TRAPPING. rast_draw_face arms the
 * divide trap with rast_div_saturate_handler_face, which skips the
 * faulting `idiv` leaving AX = 0x7fff and DX = 0 -- for a NEGATIVE overflow
 * as well. A long edge crossing a plane near its far end overflows here in
 * the ordinary way, and the original's answer is `a + 0x4000`, not a trap
 * and not a clamped endpoint.
 */
#define UW_CLIP_MAX 200          /* (0xc69 - 0x309) / 12 */

typedef struct {
    int16_t x, y, z;
    uint16_t outcode;            /* a word at +6; only the low byte is read */
    int16_t u, v;
} uw_rast_cvert;

typedef struct {
    /* +2 for the closing duplicate and the lookahead scratch the pass
     * writes past the polygon's end, exactly as the original does. */
    uw_rast_cvert buf[2][UW_CLIP_MAX + 2];
    int src;                     /* which buffer the start points at */
    int count;
    uint8_t or_code, and_code;
} uw_rast_clip;

/* Recompute the OR and AND outcodes over the current polygon, as the emitter does
 * before it calls rast_draw_face. */
void uw_rast_clip_accumulate(uw_rast_clip *c);

/* One Sutherland-Hodgman pass. `bit` is one of UW_OUT_LEFT, _RIGHT, _ABOVE,
 * _BELOW; anything else is a programming error and the pass does nothing. */
void uw_rast_clip_plane(uw_rast_clip *c, uint8_t bit);

/* The same pass as rast_draw_shaded runs it on EIGHT-byte records whose +7 is a shade -- carried here in `u`, low
 * byte. x, y, z and the outcode come out as uw_rast_clip_plane's, but for
 * the divide-overflow handler: rast_div_retry_handler, which
 * halves the dividend and divides again rather than saturating. The shade
 * of a crossing is `next + (cur - next) * d / den` in eight bits -- `mov
 * al,[si+7]; sub al,[si+0xf]; cbw; imul bp; idiv cx; add al,[si+0xf]` --
 * for BOTH crossings, with d and den the crossing's own: so the one that
 * enters, on the edge from the previous vertex, takes its shade from the
 * current and the NEXT, and neither is rounded. The original does that in
 * all four passes; a port that interpolates between the edge's own ends
 * draws a different gradient wherever a Gouraud face is clipped. */
void uw_rast_clip_plane_shaded(uw_rast_clip *c, uint8_t bit);

/* rast_draw_face's decision, up to but not including the projection.
 * Returns the number of vertices left to draw, 0 if the face is dropped. */
int uw_rast_clip_face(uw_rast_clip *c);

/* The projection: a scale and an offset for x and for y, and the divisor is
 * the vertex's own z. */
typedef struct {
    int16_t scale_x, off_x, scale_y, off_y;
} uw_rast_proj;

/* The graphics module's vertex descriptor: FOURTEEN bytes,
 * not sixteen -- `stosw` twice, `movsw` twice, `add di,6`. */
typedef struct {
    int16_t sx, sy;              /* +0, +2  screen */
    int16_t u, v;                /* +4, +6  copied through untouched */
    int16_t x, y, z;             /* +8, +0xa, +0xc  the camera-space vertex */
} uw_rast_svert;

/* Projects the current polygon into `out`, which must hold `c->count`
 * records, and returns that count. */
int uw_rast_project(const uw_rast_clip *c, const uw_rast_proj *p,
                    uw_rast_svert *out);

/* rast_draw_face whole: clip unless `noclip` is set, then
 * project. Returns the vertex count handed to the shader, 0 for a face that
 * never reaches it. */
int uw_rast_draw_face(uw_rast_clip *c, const uw_rast_proj *p, int noclip,
                      uw_rast_svert *out);

/* ---- the gather, and the subdivision of a near wall --------------------
 *
 * The emit_poly* opcodes share one body (three vertices, four through
 * gfx_texture_poly_wall, four through gfx_texture_poly_affine). It reads a
 * leading texture index -- which selects an 8-byte record of which the
 * gather uses two words -- and then one (slot byte offset, corner flags)
 * pair per vertex.
 *
 * Each vertex is copied out of the slot array into a 12-byte clip vertex,
 * and its texture coordinates come from the FLAGS rather than the list:
 * bit 0 takes u from the record as `(width - 1) << 8 | 0xff`, a value of 2
 * or more takes v from its second word, and the rest is zero. So the four
 * flag values are the texture's four corners and nothing else -- the
 * shipped lists carry 1, 2, 3 and 0.
 *
 * THIS ONE HAS NO VERTEX-FOR-VERTEX ORACLE, unlike the clip pass. Once a
 * frame is drawn the slot array has been overwritten by later
 * opcodes -- one of a four-vertex polygon's slots survives, not four -- so
 * the gather cannot be replayed against a buffer. What the original's state
 * does show is the RULE'S OUTPUTS: its clip buffers hold u values of 0x1fff and
 * 0x3fff and v values of 0x03ff, 0x0fff and 0x17ff, which are exactly
 * `(width - 1) << 8 | 0xff` for the widths 32 and 64 in the record table
 * and the second words of the records themselves. A coordinate that were
 * anything else would have no reason to land on those.
 *
 * THE OUTCODE IS STORED AS A WORD WHOSE HIGH BYTE IS THE Z COORDINATE'S:
 * `mov ax,[bx+0x1624]` then `mov al,[bx+0x1626]` overwrites only AL. No
 * reader ever looks at it -- every test is `test byte ptr [..],bit` -- but
 * a port that zeroes it writes a buffer that does not compare equal to the
 * original's.
 *
 * And then, for FOUR vertices with shader 0x545 only, a quad that is
 * outside any plane or has a vertex nearer than `1 << near_shift` is
 * SUBDIVIDED rather than drawn: the four edge midpoints, a centre that is
 * the midpoint of two opposite edge midpoints, and rast_draw_face over four
 * sub-quads. That is how the perspective mapper's error is kept down on a
 * near wall, and a port that draws the whole quad instead is wrong by
 * however much the mapper is.
 *
 * The midpoint is `add; jno +3; rcr; sar` -- the 16-bit signed midpoint
 * that recovers the sum's lost bit from the carry. The `sar` IS NOT IN ANY
 * LISTING: the `jno` lands one byte inside a `test ax,imm16` whose two
 * immediate bytes are it.
 */
/* The shape record at index * 8 into the record table. The gather reads the
 * first two words; the texture mappers read the other two
 * (gfx_texture_poly_affine), which is what the eight bytes are for. */
typedef struct {
    int16_t  width;     /* +0: u runs 0 .. (width - 1) << 8 | 0xff */
    int16_t  v_max;     /* +2: v runs 0 .. this, stored ready to use */
    uint16_t tex_seg;   /* +4: the texture's segment -- a pointer in a port */
    uint16_t v_mask;    /* +6: masks v's high word into a row offset */
} uw_rast_texrec;

typedef struct {
    int      slot_off;  /* a byte offset into the slot array, as emitted */
    uint8_t  corner;    /* bit 0 -> u, >= 2 -> v */
} uw_rast_polyv;

/* Fills the clip buffer from the slot array and rebuilds the OR and AND.
 * Always writes buffer 0, as the handler does (`mov di,0x309`). */
void uw_rast_gather_poly(uw_rast_clip *c, const uw_rast_slots *s,
                         const uw_rast_texrec *tex,
                         const uw_rast_polyv *v, int n);

/* The byte-operand gather, behind emit_poly4b_* and its siblings: four vertices of ONE BYTE each, the byte a slot INDEX the
 * handler scales by 8 -- and the texture corners are NOT operands. They come
 * out of the loop counter: `bl = cl - 2; test bl,2` gates u and
 * `bl = cl - 1; test bl,2` gates v, so the four vertices take (0,0),
 * (umax,0), (umax,vmax), (0,vmax) in that order and nothing else is
 * possible.
 *
 * AND ITS umax IS ONE UNIT SHORT OF THE WORD FORM'S. 0x36 builds
 * `(width - 1) << 8 | 0xff` -- `dec; xchg dl,dh; mov dl,0xff`; this one
 * stops at `dec; xchg al,ah`, so it is `(width - 1) << 8`. A 1/256th of a
 * texel, in a coordinate the span steps in 8.8, and the two opcodes are in
 * the same lists. */
void uw_rast_gather_poly_bytes(uw_rast_clip *c, const uw_rast_slots *s,
                               const uw_rast_texrec *tex,
                               const uint8_t *slot_index, int n);

/* The THIRD gather, behind emit_poly_shape_* / _record_* / _selected: a count and then (slot, u, v) per vertex, where u and v are
 * NORMALISED and the record scales them -- `mul [bp+0]` then take bits
 * 8..23 for u, `mul [bp+2]` then take the high word for v. Both multiplies
 * are UNSIGNED, so a coordinate with the top bit set is a large positive
 * fraction and not a negative one.
 *
 * Three gathers for one job, and they differ in where the texture
 * coordinates come from: 0x36's are corner FLAGS, 0xa2's are implied by the
 * vertex's position in the quad, and these are operands. */
void uw_rast_gather_poly_uv(uw_rast_clip *c, const uw_rast_slots *s,
                            const uw_rast_texrec *tex,
                            const uint16_t *slot_u_v, int n);

/* The 16-bit signed midpoint: `add ax,bx; jno; rcr ax,1 / sar ax,1`. */
int16_t uw_rast_midpoint(int16_t a, int16_t b);

/* Does this gathered polygon take the subdivision path? `shader` is the
 * word the entry point chose and `near_shift` the vertex_add scale shift. */
int uw_rast_should_subdivide(const uw_rast_clip *c, int shader,
                             int near_shift);

/* The four sub-quads, in the order the handler draws them. `out` receives
 * 4 x 4 clip vertices. */
void uw_rast_subdivide(const uw_rast_clip *c, uw_rast_cvert out[4][4]);

#endif
