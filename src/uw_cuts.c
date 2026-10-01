/* SPDX-License-Identifier: MIT */
#include "uw_cuts.h"
#include <stdio.h>
#include <string.h>

const int uw_cut_operands[UW_CUT_OPCODES] = {
    2, 0, 2, 1, 2, 1, 0, 1, 2, 1, 1, 1, 1, 3, 2, 0,
};

const char *uw_cut_names[UW_CUT_OPCODES] = {
    "display_string", "set_flag",      "nop",            "pause",
    "play_to_frame",  "end_segment",   "end",            "repeat_segment",
    "change_file",    "fade_out",      "fade_in",        "hold_to_frame",
    "set_skippable",  "text_and_sound", "wait",          "klang",
};

void uw_cut_filename(int cutscene, int file, char *out, size_t cap) {
    snprintf(out, cap, "CS%03o.N%02o", cutscene, file);
}

/* The walk is the format's own, and it is what makes the operand counts a
 * finding rather than a guess: a wrong count leaves bytes over or runs off
 * the end. Records are re-walked per lookup rather than indexed into a
 * table -- the longest script has a few dozen, and a static table would
 * make this the one reader that cannot have two scripts open at once. */
static size_t walk(const uw_cut_script *s, int want, size_t *end_out) {
    size_t i = 0;
    int k = 0;
    while (i + 4 <= s->file.size) {
        uint16_t op = uw_u16(s->file.data + i + 2);
        if (op >= UW_CUT_OPCODES) break;
        int n = uw_cut_operands[op];
        if (i + 4 + 2 * (size_t)n > s->file.size) break;
        if (k == want) { if (end_out) *end_out = i; return i; }
        k++;
        i += 4 + 2 * (size_t)n;
    }
    if (end_out) *end_out = i;
    return (size_t)-1;
}

bool uw_cut_open(uw_cut_script *s, const char *path, int number) {
    memset(s, 0, sizeof *s);
    s->file = uw_read_file(path);
    if (!s->file.data) return false;
    s->number = number;
    size_t end = 0;
    walk(s, -1, &end);              /* -1: count them all, find no record */
    s->tiles = (end == s->file.size);
    size_t i = 0;
    while (i + 4 <= s->file.size) {
        uint16_t op = uw_u16(s->file.data + i + 2);
        if (op >= UW_CUT_OPCODES) break;
        int n = uw_cut_operands[op];
        if (i + 4 + 2 * (size_t)n > s->file.size) break;
        s->count++;
        s->ends = (op == UW_CUT_OP_END);
        i += 4 + 2 * (size_t)n;
    }
    return true;
}

void uw_cut_close(uw_cut_script *s) { uw_free(&s->file); s->count = 0; }

bool uw_cut_record_at(const uw_cut_script *s, int i, uw_cut_record *out) {
    if (i < 0 || i >= s->count) return false;
    size_t at = walk(s, i, NULL);
    if (at == (size_t)-1) return false;
    memset(out, 0, sizeof *out);
    out->at = at;
    out->frame  = uw_u16(s->file.data + at);
    out->opcode = uw_u16(s->file.data + at + 2);
    out->operands = uw_cut_operands[out->opcode];
    out->operand = s->file.data + at + 4;
    return true;
}

uint16_t uw_cut_operand(const uw_cut_record *r, int i) {
    if (i < 0 || i >= r->operands) return 0;
    return uw_u16(r->operand + (size_t)i * 2);
}

/* ---- the animation files ------------------------------------------------ */

bool uw_anim_open(uw_anim *a, const char *path) {
    memset(a, 0, sizeof *a);
    a->file = uw_read_file(path);
    if (!a->file.data || a->file.size < UW_ANIM_HEADER) { uw_free(&a->file); return false; }
    a->page_count = uw_u16(a->file.data + 6);
    a->last_page_short = a->file.data[0x1a];
    a->frame_rate = uw_u16(a->file.data + 0x44);
    if (a->page_count <= 0 || (size_t)(0x500 + a->page_count * 6) > UW_ANIM_HEADER) {
        uw_free(&a->file);
        return false;
    }
    return true;
}

void uw_anim_close(uw_anim *a) { uw_free(&a->file); a->page_count = 0; }

bool uw_anim_page_at(const uw_anim *a, int page, uw_anim_page *out) {
    const uint8_t *r;
    if (page < 0 || page >= a->page_count) return false;
    r = a->file.data + 0x500 + (size_t)page * 6;
    out->order = uw_u16(r);
    out->frame_count = uw_u16(r + 2);
    out->frame_bytes = uw_u16(r + 4);
    return true;
}

const uint8_t *uw_anim_sizes(const uw_anim *a, int page) {
    size_t at = UW_ANIM_HEADER + (size_t)page * UW_ANIM_PAGE + 8;
    uw_anim_page p;
    if (!uw_anim_page_at(a, page, &p)) return NULL;
    if (at + (size_t)p.frame_count * 2 > a->file.size) return NULL;
    return a->file.data + at;
}

const uint8_t *uw_anim_frame(const uw_anim *a, int page, int i, size_t *len) {
    uw_anim_page p;
    const uint8_t *sizes = uw_anim_sizes(a, page);
    size_t at;
    int k;
    if (!sizes || !uw_anim_page_at(a, page, &p) || i < 0 || i >= (int)p.frame_count) return NULL;
    at = UW_ANIM_HEADER + (size_t)page * UW_ANIM_PAGE + 8 + (size_t)p.frame_count * 2;
    for (k = 0; k < i; k++) at += uw_u16(sizes + (size_t)k * 2);
    *len = uw_u16(sizes + (size_t)i * 2);
    if (at + *len > a->file.size) return NULL;
    return a->file.data + at;
}

/* palette_to_dac: source stride 4, destination 3, +2 to red and
 * +0 to blue, each `>> 2` -- an arithmetic shift in the original, over bytes
 * that are never negative here. */
void uw_anim_palette(const uw_anim *a, uint8_t out[768]) {
    int i;
    for (i = 0; i < 256; i++) {
        const uint8_t *e = a->file.data + 0x100 + (size_t)i * 4;
        out[i * 3]     = (uint8_t)(e[2] >> 2);
        out[i * 3 + 1] = (uint8_t)(e[1] >> 2);
        out[i * 3 + 2] = (uint8_t)(e[0] >> 2);
    }
}

/* Four UW2 pages count the page's own head and size table in frame_bytes
 * where every other page of both games counts only its frames; the size
 * table is what the frames are walked by either way. */
bool uw_anim_frame_bytes_inclusive(const uw_anim *a, int page) {
    uw_anim_page p;
    const uint8_t *sizes = uw_anim_sizes(a, page);
    long sum = 0;
    int i;
    if (!sizes || !uw_anim_page_at(a, page, &p)) return false;
    for (i = 0; i < (int)p.frame_count; i++) sum += uw_u16(sizes + (size_t)i * 2);
    return sum + 8 + (long)p.frame_count * 2 == (long)p.frame_bytes;
}

size_t uw_anim_end(const uw_anim *a) {
    uw_anim_page p;
    int i = a->page_count - 1;
    size_t nbytes;
    if (!uw_anim_page_at(a, i, &p)) return 0;
    nbytes = p.frame_bytes;
    if (uw_anim_frame_bytes_inclusive(a, i)) nbytes -= 8 + (size_t)p.frame_count * 2;
    return UW_ANIM_HEADER + (size_t)i * UW_ANIM_PAGE + 8
         + (size_t)p.frame_count * 2 + nbytes;
}

size_t uw_anim_slot_end(const uw_anim *a) {
    return UW_ANIM_HEADER + (size_t)a->page_count * UW_ANIM_PAGE;
}

/* A frame with nothing after `kind` and the byte beside it is the "nothing
 * changes" frame, and the two games spell it differently: UW1 writes a
 * frame of size 0, or one whose whole stream is the terminator, and UW2 a
 * four-byte `42 00 01 00` -- header, kind 1, no stream. Decoding one of
 * those would read the next frame's bytes as codes. */
int uw_anim_frame_body(const uint8_t *frame, size_t len, size_t *skip) {
    size_t s;
    if (len < 2) { *skip = 0; return -1; }
    s = frame[1] ? (size_t)(((uw_u16(frame + 2) + 1) & ~1) + 4) : 2;
    *skip = s;
    if (len <= s + 2) return -1;
    return frame[s];
}

size_t uw_rle_expand(const uint8_t *src, size_t len, size_t at,
                     uint8_t *dst, size_t cap, size_t *end) {
    size_t di = 0, start = at;
    for (;;) {
        uint8_t c;
        if (at >= len) return 0;
        c = src[at++];
        if (c == 0) {                        /* a run: count, value */
            size_t n;
            uint8_t v;
            if (at + 2 > len) return 0;
            n = src[at]; v = src[at + 1]; at += 2;
            if (di + n > cap) return 0;
            memset(dst + di, v, n);
            di += n;
        } else if (c == 0x80) {              /* the word form */
            uint16_t w;
            if (at + 2 > len) return 0;
            w = uw_u16(src + at); at += 2;
            if (w == 0) { if (end) *end = di; return at - start; }
            if (!(w & 0x8000)) {
                if (di + w > cap) return 0;
                di += w;                     /* a long skip */
            } else {
                size_t k = w & 0x7fff;
                if (k < 0x4000) {            /* a long literal */
                    if (at + k > len || di + k > cap) return 0;
                    memcpy(dst + di, src + at, k);
                    at += k; di += k;
                } else {                     /* a long run */
                    uint8_t v;
                    k &= 0x3fff;
                    if (at >= len) return 0;
                    v = src[at++];
                    if (di + k > cap) return 0;
                    memset(dst + di, v, k);
                    di += k;
                }
            }
        } else if (c & 0x80) {               /* a skip */
            size_t n = c & 0x7fu;
            if (di + n > cap) return 0;
            di += n;
        } else {                             /* a literal */
            size_t n = c;
            if (at + n > len || di + n > cap) return 0;
            memcpy(dst + di, src + at, n);
            at += n; di += n;
        }
    }
}
