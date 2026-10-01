/* SPDX-License-Identifier: MIT */
/* The creature frames in CRIT's page files, past where uw_crit.h stops.
 *
 * `uw_crit` reads a page as far as its slot
 * map and frame lists; this is the remainder, and creature artwork cannot be
 * drawn at all without it.
 *
 * A page's pixel section:
 *
 *     byte  naux
 *     byte  aux[naux][32]     auxiliary palettes: 32 palette indices each
 *     byte  nframes
 *     byte  6                 constant across all 61 pages that have pixels
 *     word  offset[nframes]   ABSOLUTE file offsets, ascending
 *     ...   the frames
 *
 * The offset table needs no length field to be found -- offset[0] is where
 * the table ends -- so `nframes` can be DERIVED and then checked against the
 * byte that precedes it. All 61 pages with a pixel section agree; the three
 * without one end before it, their artwork living entirely in the .n01 file.
 *
 * A frame is seven bytes and then a compressed stream:
 *
 *     byte  width, height, hotspot x, hotspot y
 *     byte  TYPE -- 6 or 8, and nothing else in 1,621 frames
 *     word  count -- the number of CODES in the stream, not bytes
 *
 * TYPE 8 IS THE 4-BIT RLE the .gr images use. TYPE 6 IS
 * THE SAME RLE OVER 5-BIT CODES, and the tell was the auxiliary palettes:
 * they are 32 bytes, and 32 is 2**5. A 4-bit image needs sixteen entries and
 * a 5-bit one thirty-two, so a page carrying 32-entry palettes is carrying
 * 5-bit images.
 */
/*
 * Both readings rest on the data: every one of the 1,621 frames decodes to
 * exactly the dimensions its own header states, which no wrong reading
 * produces -- and that the pictures are right. Page 1's frame 0 is the page
 * ASSOC.ANM names `skela`, and it comes out a skeleton holding a sword.
 *
 * The pixels are indices into the AUXILIARY palette, not the main one. The
 * caller maps them through aux[variant] and then through the level's
 * palette: two indirections, and the middle one is what lets four goblins
 * share a page.
 */
#ifndef UW_CRITPIX_H
#define UW_CRITPIX_H

#include "uw.h"

#define UW_CRITPIX_AUX_ENTRIES 32   /* 2**5: what says type 6 is 5-bit */
#define UW_CRITPIX_FRAME_HEADER 7

typedef struct {
    int      index, at, end;
    int      width, height, hot_x, hot_y;
    int      type;        /* 6 or 8 */
    int      count;       /* codes in the stream, not bytes */
} uw_critpix_frame;

typedef struct {
    uw_blob file;
    bool    has_pixels;
    const char *why;      /* when it has none */
    int     naux, nframes, derived, constant;
    size_t  aux_at, table_at;
} uw_critpix;

/* `path` is a full CRnnPAGE.Nxx path. Always returns true if the file reads;
 * `has_pixels` says whether it carries a pixel section, since three pages do
 * not and that is data rather than an error. */
bool uw_critpix_open(uw_critpix *p, const char *path);
void uw_critpix_close(uw_critpix *p);
/* The 32 entries of auxiliary palette `i`, or NULL. */
const uint8_t *uw_critpix_aux(const uw_critpix *p, int i);
bool uw_critpix_frame_at(const uw_critpix *p, int k, uw_critpix_frame *out);
/* Decodes frame `k` into `out` as width*height auxiliary-palette indices.
 * False when the stream runs short of the frame's own size, which is a real
 * failure -- overproduction is not, and is clipped. */
bool uw_critpix_pixels(const uw_critpix *p, int k, uint8_t *out, size_t cap);

#endif
