/* SPDX-License-Identifier: MIT */
/* DATA/FONT*.SYS -- UW's six bitmap fonts, all under one header.
 *
 * PROVENANCE: the layout is read from the DATA, not from the
 * renderer. What makes it more than a guess is that three identities hold
 * across all six files at once, and the test asserts each on each font:
 *
 *   * char_size == height * row_bytes
 *   * (filesize - 12) divides by char_size + 1
 *   * every glyph's width byte fits its row: width <= row_bytes * 8
 *
 * Any wrong field order breaks at least one of those on at least one font --
 * the six differ in every dimension, from 4x5 to fifteen rows of two bytes.
 *
 * THE LAST HEADER FIELD IS NOT A MAXIMUM WIDTH, though it is widely
 * documented as one. It is below the widest glyph in four of the six --
 * FONT5X6P calls it 5 and has nine glyphs of 6 -- and it equals the number
 * in the filename every time. It is the nominal design width; the real
 * bound on a glyph is the row it has to fit in.
 */
#ifndef UW_FONT_H
#define UW_FONT_H

#include "uw.h"

#define UW_FONT_HEADER 12

typedef struct {
    uw_blob  file;
    uint16_t magic;          /* always 1 */
    uint16_t char_size;      /* bitmap bytes per glyph */
    uint16_t space_width;    /* pixels for the space character */
    uint16_t height;         /* rows per glyph */
    uint16_t row_bytes;      /* bytes per row */
    uint16_t nominal_width;  /* the design width, NOT a maximum */
    uint16_t stride;         /* char_size + 1: bitmap plus the width byte */
    int      count;
} uw_font;

bool uw_font_open(uw_font *f, const char *path);
void uw_font_close(uw_font *f);

/* The glyph's width in pixels, from its trailing byte; 0 if out of range. */
int uw_font_width(const uw_font *f, int glyph);
/* Bit `x` of row `y`, most significant bit leftmost. Out of range reads 0,
 * which is what a renderer clipping at the edge wants. */
bool uw_font_pixel(const uw_font *f, int glyph, int x, int y);

#endif
