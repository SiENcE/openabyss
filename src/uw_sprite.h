/* SPDX-License-Identifier: MIT */
/* Sprites in the 3-D view: emit_sprite's art decode, rast_op_draw_sprite's
 * placement and gfx_draw_sprite_scaled.
 *
 * A sprite is not a textured quad. The list names an art id and a light
 * level; the art is decoded with its auxiliary palette lit through that
 * LIGHT.DAT row, a vertex slot is offset IN PLACE by the record's anchor, two
 * corners are projected for a position and a size, and the graphics module
 * scales the image onto the screen with nearest-neighbour code it compiles
 * for the scale -- then copies each row with 0 transparent and 0xfb..0xff
 * as translucency codes through XFER.DAT. */
#ifndef UW_SPRITE_H
#define UW_SPRITE_H

#include "uw.h"
#include "uw_rast.h"
#include "uw_texmap.h"

/* ---- the art, decoded as emit_sprite decodes it ------------------------
 *
 * Header bytes: type, width, height, auxiliary palette, then a count word at
 * +4 and the data at +6 -- the decoders are entered with DS:BP = image + 4.
 * A palette byte of 0x20 or more becomes (byte - 8) * 2. The palette's 16
 * colours are lit through LIGHT.DAT row `light` unless it is 0xff
 * (rast_gr_xlat_build); type 4's pixels are lit one by one and always.
 *
 * `auxpals` is the 16-bytes-a-palette table, `light` the
 * 4096-byte table. Returns the pixel count written (width * height), 0 for a
 * type this does not decode. */
size_t uw_sprite_decode(const uint8_t *art, size_t art_len,
                        const uint8_t *auxpals, size_t auxpals_len,
                        const uint8_t *light, uint8_t level,
                        uint8_t *out, size_t cap, int *width, int *height);

/* ---- a creature frame, as emit_creature walks its page ------
 *
 * `page` is a whole CRIT\CRxxPAGE.Nyy file -- the pager reads it straight
 * into two EMS pages -- `frame` the list's frame word, `dir` its fourth word
 * and `variant` the second byte of the creature's ASSOC entry. The walk:
 * page[0] is the file's first frame number, page[1] a slot count and then
 * one animation per slot (0xff: no frame, and nothing is drawn); a count of
 * animations and eight frame indices each, selected by `dir` (0xff reads as
 * 0); a count of 32-colour palettes, of which `variant` must be one; the
 * frame count, a spare byte and the frame offsets. Fills the frame's offset
 * and the palette's, and returns 1 when a frame is found. */
typedef struct {
    size_t frame_at, palette_at;
    int    width, height, hot_x, hot_y, type;
} uw_creature_frame;

int uw_creature_find(const uint8_t *page, size_t len, uint16_t frame,
                     uint16_t dir, uint8_t variant, uw_creature_frame *out);

/* The frame's pixels through its palette, lit by `level` (0xff: unlit) --
 * type 6 is 5-bit codes and all 32 colours, type 8 4-bit and the first 16 --
 * run-length decoded to width * height. Returns that count, or 0. */
size_t uw_creature_decode(const uint8_t *page, size_t len,
                          const uw_creature_frame *f, const uint8_t *light,
                          uint8_t level, uint8_t *out, size_t cap);

/* ---- the sprite descriptor, and the clip bounds ------------------------- */
typedef struct {
    uint16_t src_offset;     /* 0b1a */
    uint16_t src_w, src_h;   /* 0b1e, 0b20 */
    int16_t  x, y;           /* 0b22, 0b24: the BOTTOM row's left pixel */
    int16_t  height, width;  /* 0b26, 0b28 */
} uw_sprite_desc;

typedef struct {
    int16_t left, bottom, right, top;
} uw_clip_bounds;

/* ---- the placement ----------------------------------------
 *
 * `rec` is the sprite's seven-word record: the decoded image's
 * segment, its width and height, and four anchor bytes at +6, +8, +0xa and
 * +0xc. The slot's x and y are MOVED, so the sprite scratch slot 255 keeps
 * the moved values. `scale_x` is the horizontal scale; `m4` a basis
 * word. Returns 1 when gfx_draw_sprite_scaled would run: neither corner
 * rejected by its outcode (0x82 for the first, 0x89 for the second) and no
 * divide overflowed. */
int uw_rast_sprite_place(uw_rast_slots *s, int slot_off, const int16_t rec[7],
                         int16_t scale_x, int16_t m4, const uw_rast_proj *p,
                         uw_sprite_desc *out);

/* ---- gfx_draw_sprite_scaled --------------------------------
 *
 * `pixels` is the decoded image, `src_w` wide, top row first. `xfer` is
 * XFER.DAT as loaded (five rows used); may be NULL, in which
 * case translucent pixels are skipped and counted in `*translucent`.
 * Returns the number of pixels written. */
long uw_gfx_draw_sprite_scaled(const uw_fb *fb, const uw_sprite_desc *d,
                               const uint8_t *pixels, size_t n_pixels,
                               const uw_clip_bounds *clip,
                               const uint8_t *xfer, long *translucent);
/* The same through gfx_row_blitter_slot's other rows (chosen by
 * gfx_select_row_blitter): 1 gfx_row_copy_translucent, as above; 2
 * gfx_row_fill_masked, every pixel but 0 written as `fill` --
 * the silhouette drawlist_begin_frame selects for the pick map, in the
 * fill colour held when the sprite was drawn; 3
 * gfx_row_copy_transparent, every pixel but 0 as itself. */
long uw_gfx_draw_sprite_rows(const uw_fb *fb, const uw_sprite_desc *d,
                             const uint8_t *pixels, size_t n_pixels,
                             const uw_clip_bounds *clip,
                             const uint8_t *xfer, long *translucent,
                             int blitter, uint8_t fill);

#endif
