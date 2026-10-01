/* SPDX-License-Identifier: MIT */
#include "uw_font.h"
#include <string.h>

bool uw_font_open(uw_font *f, const char *path) {
    memset(f, 0, sizeof *f);
    f->file = uw_read_file(path);
    if (!f->file.data || f->file.size < UW_FONT_HEADER) {
        uw_free(&f->file);
        return false;
    }
    const uint8_t *d = f->file.data;
    f->magic         = uw_u16(d + 0);
    f->char_size     = uw_u16(d + 2);
    f->space_width   = uw_u16(d + 4);
    f->height        = uw_u16(d + 6);
    f->row_bytes     = uw_u16(d + 8);
    f->nominal_width = uw_u16(d + 10);
    f->stride        = (uint16_t)(f->char_size + 1);
    size_t body = f->file.size - UW_FONT_HEADER;
    f->count = f->stride ? (int)(body / f->stride) : 0;
    return true;
}

void uw_font_close(uw_font *f) { uw_free(&f->file); f->count = 0; }

int uw_font_width(const uw_font *f, int glyph) {
    if (glyph < 0 || glyph >= f->count) return 0;
    size_t at = UW_FONT_HEADER + (size_t)glyph * f->stride + f->char_size;
    if (at >= f->file.size) return 0;
    return f->file.data[at];
}

bool uw_font_pixel(const uw_font *f, int glyph, int x, int y) {
    if (glyph < 0 || glyph >= f->count) return false;
    if (y < 0 || y >= f->height) return false;
    if (x < 0 || x >= f->row_bytes * 8) return false;
    size_t base = UW_FONT_HEADER + (size_t)glyph * f->stride;
    size_t at = base + (size_t)y * f->row_bytes + (size_t)(x / 8);
    if (at >= f->file.size) return false;
    /* Most significant bit leftmost -- the order a shift-and-test renderer
     * gets right by accident and a byte-per-pixel one gets wrong. */
    return (f->file.data[at] >> (7 - (x % 8))) & 1;
}
