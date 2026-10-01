/* SPDX-License-Identifier: MIT */
/* The sound formats: SOUNDS.DAT, the timbre banks, .VOC and .XMI.
 * See uw_sound.h for where each reading comes from. */
#include "uw_sound.h"

#include <string.h>

/* ---- SOUNDS.DAT --------------------------------------------------------- */

bool uw_sounds_open(uw_sounds *s, const char *path) {
    memset(s, 0, sizeof *s);
    s->file = uw_read_file(path);
    if (!s->file.data || s->file.size < 1) { uw_free(&s->file); return false; }
    s->count = s->file.data[0];
    if (1 + (size_t)s->count * UW_SOUND_STRIDE > s->file.size) {
        uw_free(&s->file);
        return false;
    }
    return true;
}

void uw_sounds_close(uw_sounds *s) { uw_free(&s->file); s->count = 0; }

bool uw_sound_effect_at(const uw_sounds *s, int i, uw_sound_effect *out) {
    const uint8_t *r;
    if (i < 0 || i >= s->count) return false;
    r = s->file.data + 1 + (size_t)i * UW_SOUND_STRIDE;
    out->program = r[0];
    out->note = r[1];
    out->velocity = r[2];
    /* big-endian, and by hand in the loader: byte4 * 256 + byte5 */
    out->duration = (uint16_t)(r[3] * 256 + r[4]);
    return true;
}

/* ---- the timbre bank ---------------------------------------------------- */

bool uw_bank_open(uw_bank *b, const char *path) {
    size_t i = 0;
    memset(b, 0, sizeof *b);
    b->file = uw_read_file(path);
    if (!b->file.data) return false;
    while (i + 6 <= b->file.size && b->file.data[i + 1] != 0xff) {
        b->count++;
        i += 6;
    }
    if (i + 6 > b->file.size) { uw_free(&b->file); b->count = 0; return false; }
    return true;
}

void uw_bank_close(uw_bank *b) { uw_free(&b->file); b->count = 0; }

bool uw_bank_entry_at(const uw_bank *b, int i, uint8_t *program, uint8_t *bank) {
    if (i < 0 || i >= b->count) return false;
    *program = b->file.data[(size_t)i * 6];
    *bank = b->file.data[(size_t)i * 6 + 1];
    return true;
}

const uint8_t *uw_bank_patch(const uw_bank *b, uint8_t program, uint8_t bank, size_t *len) {
    int i;
    for (i = 0; i < b->count; i++) {
        const uint8_t *e = b->file.data + (size_t)i * 6;
        uint32_t at;
        if (e[0] != program || e[1] != bank) continue;
        at = uw_u32(e + 2);
        if ((size_t)at + 2 > b->file.size) return NULL;
        *len = uw_u16(b->file.data + at);      /* the word counts itself */
        if (at + *len > b->file.size) return NULL;
        return b->file.data + at;
    }
    return NULL;
}

/* ---- .VOC --------------------------------------------------------------- */

bool uw_voc_open(uw_voc *v, const char *path) {
    size_t at;
    memset(v, 0, sizeof *v);
    v->file = uw_read_file(path);
    if (!v->file.data || v->file.size < 26
        || memcmp(v->file.data, "Creative Voice File\x1a", 20) != 0) {
        uw_free(&v->file);
        return false;
    }
    v->data_offset = uw_u16(v->file.data + 20);
    v->version = uw_u16(v->file.data + 22);
    for (at = v->data_offset; at + 4 <= v->file.size; ) {
        uint8_t type = v->file.data[at];
        size_t n;
        if (type == 0) break;                  /* the terminator */
        n = (size_t)v->file.data[at + 1] | (size_t)v->file.data[at + 2] << 8
          | (size_t)v->file.data[at + 3] << 16;
        if (at + 4 + n > v->file.size) { uw_free(&v->file); return false; }
        v->blocks++;
        if (type == 1) {
            if (!v->sound_blocks) {
                v->time_constant = v->file.data[at + 4];
                v->pack = v->file.data[at + 5];
                v->samples_at = at + 6;
                v->samples = n - 2;
            }
            v->sound_blocks++;
        }
        at += 4 + n;
    }
    return true;
}

void uw_voc_close(uw_voc *v) { uw_free(&v->file); }

long uw_voc_rate(const uw_voc *v) {
    return 1000000L / (256 - (long)v->time_constant);
}

/* ---- .XMI --------------------------------------------------------------- */

/* IFF: a four-byte tag, a BIG-endian length, the data, padded to even. The
 * driver's own service 0x97 reads the length through `xchg al,ah` /
 * `xchg dl,dh`, which is where the order comes from. */
static uint32_t be32(const uint8_t *p) {
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

static bool tag_is(const uint8_t *p, const char *t) { return memcmp(p, t, 4) == 0; }

bool uw_xmi_open(uw_xmi *x, const char *path) {
    size_t at, form_end, cat_end;
    memset(x, 0, sizeof *x);
    x->file = uw_read_file(path);
    if (!x->file.data || x->file.size < 12) { uw_free(&x->file); return false; }
    if (!tag_is(x->file.data, "FORM") || !tag_is(x->file.data + 8, "XDIR")) {
        uw_free(&x->file);
        return false;
    }
    form_end = 8 + be32(x->file.data + 4);
    for (at = 12; at + 8 <= form_end && at + 8 <= x->file.size; ) {
        uint32_t n = be32(x->file.data + at + 4);
        if (tag_is(x->file.data + at, "INFO") && n >= 2)
            x->declared = uw_u16(x->file.data + at + 8);
        at += 8 + n + (n & 1);
    }
    at = form_end + (be32(x->file.data + 4) & 1);
    if (at + 12 > x->file.size || !tag_is(x->file.data + at, "CAT ")
        || !tag_is(x->file.data + at + 8, "XMID")) {
        uw_free(&x->file);
        return false;
    }
    cat_end = at + 8 + be32(x->file.data + at + 4);
    if (cat_end > x->file.size) { uw_free(&x->file); return false; }
    for (at += 12; at + 8 <= cat_end; ) {
        uint32_t n = be32(x->file.data + at + 4);
        size_t seq_end = at + 8 + n, in;
        if (!tag_is(x->file.data + at, "FORM") || !tag_is(x->file.data + at + 8, "XMID")) break;
        x->sequences++;
        for (in = at + 12; in + 8 <= seq_end && in + 8 <= x->file.size; ) {
            uint32_t k = be32(x->file.data + in + 4);
            if (tag_is(x->file.data + in, "TIMB") && k >= 2) {
                x->timb_at = in + 10;
                x->timbres = uw_u16(x->file.data + in + 8);
            } else if (tag_is(x->file.data + in, "EVNT")) {
                x->evnt_at = in + 8;
                x->evnt_end = in + 8 + k;
            }
            in += 8 + k + (k & 1);
        }
        at = seq_end + (n & 1);
    }
    if (!x->evnt_at || x->evnt_end > x->file.size) { uw_free(&x->file); return false; }
    return true;
}

void uw_xmi_close(uw_xmi *x) { uw_free(&x->file); }

bool uw_xmi_timbre_at(const uw_xmi *x, int i, uint8_t *program, uint8_t *bank) {
    if (i < 0 || i >= x->timbres) return false;
    if (x->timb_at + (size_t)i * 2 + 2 > x->file.size) return false;
    *program = x->file.data[x->timb_at + (size_t)i * 2];
    *bank = x->file.data[x->timb_at + (size_t)i * 2 + 1];
    return true;
}

/* MIDI's variable-length quantity, which the driver reads with the same
 * `shl / rcl` seven-bit loop at 0x320c. */
static size_t vlq(const uint8_t *d, size_t at, size_t end, uint32_t *out) {
    uint32_t v = 0;
    while (at < end && (d[at] & 0x80)) { v = (v << 7) | (d[at] & 0x7fu); at++; }
    if (at >= end) return end + 1;
    *out = (v << 7) | d[at];
    return at + 1;
}

bool uw_xmi_walk(const uw_xmi *x, uw_xmi_events *out) {
    const uint8_t *d = x->file.data;
    size_t at = x->evnt_at, end = x->evnt_end;
    memset(out, 0, sizeof *out);
    while (at < end) {
        uint8_t b = d[at], hi;
        if (b < 0x80) {                        /* an interval: every byte below 0x80 a delay, summed */
            size_t start = at;                 /* (ADLIB.ADV 0x34b6 takes them one at a time) */
            while (at < end && d[at] < 0x80) at++;
            if (at >= end) return false;
            out->intervals++;
            while (start < at) out->ticks += d[start++];
            continue;
        }
        at++;
        hi = b & 0xf0u;
        if (hi == 0x90) {                      /* note on, then its duration */
            uint32_t dur;
            if (at + 2 > end) return false;
            at = vlq(d, at + 2, end, &dur);
            if (at > end) return false;
            out->notes++;
        } else if (hi == 0x80 || hi == 0xa0 || hi == 0xb0 || hi == 0xe0) {
            if (at + 2 > end) return false;
            if (hi == 0xb0) out->controllers++; else out->others++;
            at += 2;
        } else if (hi == 0xc0 || hi == 0xd0) {
            if (at + 1 > end) return false;
            if (hi == 0xc0) out->programs++; else out->others++;
            at += 1;
        } else if (b == 0xff) {
            uint32_t n = 0;
            uint8_t kind;
            if (at >= end) return false;
            kind = d[at];
            at = vlq(d, at + 1, end, &n);
            if (at > end || at + n > end) return false;
            out->metas++;
            at += n;
            if (kind == 0x2f) { out->stop = at; out->ended = true; return true; }
        } else if (b == 0xf0 || b == 0xf7) {
            uint32_t n = 0;
            at = vlq(d, at, end, &n);
            if (at > end || at + n > end) return false;
            out->sysex++;
            at += n;
        } else {
            return false;
        }
    }
    out->stop = at;
    return true;
}
