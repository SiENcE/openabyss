/* SPDX-License-Identifier: MIT */
/* Palettes, .tr textures and .gr image sets -- the pixel formats.
 *
 * UW2 carries the same seven decoders byte-identical at the head of its
 * rasteriser.
 *
 * WHAT A PORT MUST NOT DO HERE. A 4-bit image's sixteen values are indices
 * into an AUXILIARY palette (ALLPALS.DAT), not into the main one. Decoding
 * with an identity aux palette produces a picture -- the right shape, the
 * wrong sixteen colours -- and nothing complains. So `aux` is a
 * parameter with no default, and the caller has to say.
 */
#ifndef UW_IMAGE_H
#define UW_IMAGE_H

#include "uw.h"

/* ---- PALS.DAT: 8 palettes x 256 colours, 6-bit VGA ---------------------- */

typedef struct { uint8_t r, g, b; } uw_rgb;

typedef struct {
    uw_blob file;
    int     count;          /* file size / 768 */
} uw_palettes;

bool   uw_palettes_open(uw_palettes *p, const char *path);
void   uw_palettes_close(uw_palettes *p);
/* The stored components are 0..63; these are scaled to 0..255 the way the
 * tool does it, `v * 255 / 63`, which is exact at both ends. */
uw_rgb uw_palette_colour(const uw_palettes *p, int palette, int index);

/* ---- ALLPALS.DAT: auxiliary palettes, 16 bytes each --------------------- */

#define UW_AUXPAL_SIZE 16

typedef struct {
    uw_blob file;
    int     count;          /* file size / 16 */
} uw_auxpals;

bool uw_auxpals_open(uw_auxpals *a, const char *path);
void uw_auxpals_close(uw_auxpals *a);
/* UW_AUXPAL_SIZE bytes, each a main-palette index; NULL if `i` is out of
 * range. */
const uint8_t *uw_auxpal(const uw_auxpals *a, int i);

/* ---- .tr: square textures, 8-bit indices -------------------------------- */

typedef struct {
    uw_blob file;
    int     dim;            /* 64, 32 or 16 */
    int     count;
} uw_tr;

bool uw_tr_open(uw_tr *t, const char *path);
void uw_tr_close(uw_tr *t);
uint32_t uw_tr_offset(const uw_tr *t, int i);
/* dim*dim bytes, or NULL when the offset does not fit the file. */
const uint8_t *uw_tr_texture(const uw_tr *t, int i);

/* ---- .gr: image sets ---------------------------------------------------- */

#define UW_GR_UNCOMPRESSED 0x04
#define UW_GR_RLE          0x08
#define UW_GR_PACKED4      0x0A

typedef struct {
    uw_blob file;
    int     count;
} uw_gr;

typedef struct {
    int    type;            /* 0 when the slot is empty or unreadable */
    int    width, height;
    int    auxpal;          /* -1 for type 4 */
    int    count_word;      /* the header's count: type 4's pixel count at +3
                             * (== width*height in every retail image), type
                             * 8's nibble count or type 0x0a's byte count at
                             * +4; -1 when there is no header */
    size_t produced;        /* pixels the decoder made, BEFORE clipping to
                             * width*height. Type 8 overproduces by 0..2 --
                             * record padding the blitter clips -- and that
                             * is an invariant worth asserting rather than
                             * a detail worth hiding. */
    bool   truncated;       /* the record ran past the end of the file */
    bool   too_deep;        /* type 8: nested repeats past the depth cap */
    bool   overproduced;    /* type 8: production ran away; malformed */
} uw_gr_info;

bool uw_gr_open(uw_gr *g, const char *path);
void uw_gr_close(uw_gr *g);
uint32_t uw_gr_offset(const uw_gr *g, int i);
/* An entry's length the way the engine's gr_read_entry takes it: to the next
 * offset, the last one to the end of the file. ZERO MEANS EMPTY. An empty
 * entry's offset is the next image's, so reading a header there finds a copy
 * of that image -- which is how this port once counted 647 type-4 and 857
 * type-8 images in UW1, where 617 and 769 ship. */
size_t uw_gr_entry_size(const uw_gr *g, int i);
/* The offset gr_open reads after the table: the file size, in every shipped
 * file. 0 when the file is too short to hold one. */
uint32_t uw_gr_sentinel(const uw_gr *g);

/* Decodes image `i` as 8-bit main-palette indices into `out`, writing at
 * most `cap`. `aux` is the image's UW_AUXPAL_SIZE-byte auxiliary palette
 * and may be NULL only for type 4, which does not use one.
 *
 * TYPE 4'S PIXELS START AT +5, after the pixel-count word
 * (gr_bitmap_pixels returns `header + 5`); read from +3, every
 * uncompressed image shifts by two pixels while every count still agrees.
 *
 * Returns false for an empty slot (a zero offset or a zero-length entry) or
 * a type this does not decode; `info` is filled in
 * either way, so a caller can count types without decoding. */
bool uw_gr_image(const uw_gr *g, int i, const uint8_t *aux,
                 uint8_t *out, size_t cap, uw_gr_info *info);

/* ---- the RLE decoder, on its own ---------------------------------------- */

/* Recursion is the `count == 2` escape: decode the next record k times. The
 * engine does it by self-modifying the run entry to a RET so each nested
 * call decodes one record, which has no depth limit at all -- a malformed
 * file would run the 8086 out of stack. This stops instead. */
#define UW_RLE_MAX_DEPTH 32

/* The extended-count escape shifts by FOUR at every code width, which is not
 * an assumption anybody would make from the 4-bit case alone -- it was
 * measured on the 5-bit creature frames, where shifting by five instead
 * costs 24 frames that otherwise land exactly. */
#define UW_RLE_EXT_SHIFT 4

/* Returns the number of values the stream produces, writing the first `cap`
 * of them. `code` is one value per byte -- nibbles for the .gr images, 5-bit
 * codes for the creature frames.
 *
 * `want` STOPS THE DECODE, and it is a terminating condition rather than an
 * optimisation. The .gr images pass SIZE_MAX and let the stream end, which
 * overproduces by 0..2 pixels of record padding the blitter clips. The
 * creature frames pass their own width*height, because running to the end of
 * the stream instead makes 145 of the 631 five-bit frames overproduce by 128
 * or 256 pixels -- far too round to be a decoding error, and what settled it
 * was rendering one clipped and finding a goblin holding a club. The
 * trailing codes are not garbage to tolerate; they are not part of the
 * frame.
 *
 * NOT RE-ENTRANT: uw_gr_image() unpacks into file-static scratch rather than
 * onto the stack, because the largest retail stream is tens of kilobytes of
 * codes and a port that runs two decodes at once wants to pass its own
 * buffers. Said here rather than discovered there. */
size_t uw_rle_decode(const uint8_t *code, size_t n, uint8_t *out, size_t cap,
                     size_t want, bool *too_deep, bool *overproduced);

/* Unpacks `count` big-endian `bits`-wide codes from `data`, one per byte of
 * `out`. Reads past the end as zero, which is what the decoder's own
 * past-the-end rule does. Returns the number written. */
size_t uw_bit_codes(const uint8_t *data, size_t bytes, size_t count, int bits,
                    uint8_t *out, size_t cap);

/* ---- the interface's art ------------------------------------------------ */

/* The artwork gr_draw_art names by id, as gr_load_all loads it
 * and gr_index_for_art_id finds it:
 *   ids from 0x2000: the images decoded last, into video memory, in its
 *     order -- LFTI, FLASKS, COMPASS, DRAGONS, INV, POWER, EYES, CHAINS,
 *     SPELLS, SCRLEDGE, OPTB;
 *   ids 0x1000..: BUTTONS, CURSORS, 3DWIN, in that order;
 *   ids below 0x1000: the object art, through gr_objart_map -- OBJECTS.GR
 *     by id, and 0x170..0x17f TMFLAT.GR (the map read as that identity;
 *     bow10's bow and arrows match on the screen).
 * Loads them from `data_dir` once, with ALLPALS.DAT's aux palettes; the
 * number of images loaded. */
int uw_art_load(const char *data_dir);

/* An art id's image as 8-bit indices, width and height out; NULL for an id
 * not loaded. Shaped for uw_motion's `art` callback (`user` unused). */
const uint8_t *uw_art(void *user, uint16_t id, int *w, int *h);

/* Image `index` of `data_dir`/`name`.GR (ALLPALS.DAT's aux palettes), decoded
 * once and kept: what gr_load_file puts in a buffer for gfx_blit. NULL when
 * it does not decode. */
const uint8_t *uw_gr_file_image(const char *data_dir, const char *name, int index, int *w, int *h);
/* uw_gr_file_image from the directory uw_art_load was given, in the shape of
 * uw_motion's `gr_file`. A name ending "#raw" ("WEAPONS#raw") gives a 4-bit
 * image's nibbles without its auxiliary palette. */
const uint8_t *uw_gr_file(void *user, const char *name, int index, int *w, int *h);

/* A .GR entry's BYTES, with no header and no decode: what
 * gr_load_to_buffer hands its caller, which supplies the
 * dimensions itself. PANELS.GR is the file that needs it -- its four
 * entries are raw pixels, 83 x 114 for the three panel backgrounds and the
 * edge-on card for the fourth. NULL for a slot that is
 * empty or shorter than `len`. */
const uint8_t *uw_gr_file_entry(const char *data_dir, const char *name, int index, size_t len);
/* The same from the directory uw_art_load was given, in the shape of
 * uw_motion's `gr_entry`. */
const uint8_t *uw_gr_entry(void *user, const char *name, int index, size_t len);

#endif
