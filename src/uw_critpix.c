/* SPDX-License-Identifier: MIT */
#include "uw_critpix.h"
#include "uw_crit.h"
#include "uw_image.h"
#include <string.h>

/* Where uw_crit's sections end: two header bytes, the slot map, the entry
 * count byte, then entry_count x 8 frame bytes. */
static size_t sections_end(const uw_critpix *p) {
    const uint8_t *d = p->file.data;
    if (p->file.size < 3) return p->file.size;
    size_t slot_count = d[1];
    size_t ec_at = 2 + slot_count;
    if (ec_at >= p->file.size) return p->file.size;
    return ec_at + 1 + (size_t)d[ec_at] * UW_CRIT_FRAME_SLOTS;
}

bool uw_critpix_open(uw_critpix *p, const char *path) {
    memset(p, 0, sizeof *p);
    p->file = uw_read_file(path);
    if (!p->file.data) return false;
    p->why = "";
    size_t s = sections_end(p);
    if (s + 4 > p->file.size) {
        p->why = "no pixel section: the file ends with the frame lists";
        return true;
    }
    const uint8_t *d = p->file.data;
    p->naux = d[s];
    size_t after = s + 1 + (size_t)p->naux * UW_CRITPIX_AUX_ENTRIES;
    if (after + 4 > p->file.size) { p->why = "no pixel section"; return true; }
    p->aux_at = s + 1;
    p->nframes  = d[after];
    p->constant = d[after + 1];
    p->table_at = after + 2;
    if (p->nframes == 0 ||
        p->table_at + 2 * (size_t)p->nframes > p->file.size) {
        p->why = "implausible frame count";
        return true;
    }
    /* The table's own length is recoverable from its first entry, which is
     * what lets `nframes` be CHECKED rather than trusted. */
    int first = uw_u16(d + p->table_at);
    p->derived = (int)(((size_t)first - p->table_at) / 2);
    p->has_pixels = true;
    return true;
}

void uw_critpix_close(uw_critpix *p) { uw_free(&p->file); }

const uint8_t *uw_critpix_aux(const uw_critpix *p, int i) {
    if (!p->has_pixels || i < 0 || i >= p->naux) return NULL;
    return p->file.data + p->aux_at + (size_t)i * UW_CRITPIX_AUX_ENTRIES;
}

bool uw_critpix_frame_at(const uw_critpix *p, int k, uw_critpix_frame *out) {
    if (!p->has_pixels || k < 0 || k >= p->nframes) return false;
    const uint8_t *d = p->file.data;
    size_t o = uw_u16(d + p->table_at + (size_t)k * 2);
    if (o + UW_CRITPIX_FRAME_HEADER > p->file.size) return false;
    memset(out, 0, sizeof *out);
    out->index = k;
    out->at = (int)o;
    out->end = (k + 1 < p->nframes)
        ? (int)uw_u16(d + p->table_at + (size_t)(k + 1) * 2)
        : (int)p->file.size;
    out->width  = d[o + 0];
    out->height = d[o + 1];
    out->hot_x  = d[o + 2];
    out->hot_y  = d[o + 3];
    out->type   = d[o + 4];
    out->count  = uw_u16(d + o + 5);
    return true;
}

bool uw_critpix_pixels(const uw_critpix *p, int k, uint8_t *out, size_t cap) {
    uw_critpix_frame f;
    if (!uw_critpix_frame_at(p, k, &f)) return false;
    int bits = f.type == 8 ? 4 : f.type == 6 ? 5 : 0;
    if (!bits) return false;
    size_t body_at = (size_t)f.at + UW_CRITPIX_FRAME_HEADER;
    if (body_at > (size_t)f.end || (size_t)f.end > p->file.size) return false;
    size_t want = (size_t)f.width * (size_t)f.height;

    /* One code per byte. The largest retail frame is well inside this. */
    static uint8_t code[1 << 16];
    static uint8_t vals[1 << 16];
    if ((size_t)f.count > sizeof code) return false;
    size_t n = uw_bit_codes(p->file.data + body_at, (size_t)f.end - body_at,
                            (size_t)f.count, bits, code, sizeof code);
    size_t room = want + 8 < sizeof vals ? want + 8 : sizeof vals;
    size_t made = uw_rle_decode(code, n, vals, room, want, NULL, NULL);
    /* Short is a real failure. Long is not: a record may finish past the
     * last pixel, and the caller clips. */
    if (made < want) return false;
    for (size_t i = 0; i < want && i < cap; i++) out[i] = vals[i];
    return true;
}
