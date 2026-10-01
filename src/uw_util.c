/* SPDX-License-Identifier: MIT */
#include "uw.h"
#include "uw_gamedir.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

uw_blob uw_read_file(const char *path) {
    uw_blob b = {NULL, 0, NULL};
    FILE *f = uw_fopen(path, "rb");     /* the path as it is on disk, its case aside */
    if (!f) { b.why = "cannot open"; return b; }
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); b.why = "cannot seek"; return b; }
    long n = ftell(f);
    if (n < 0) { fclose(f); b.why = "cannot tell"; return b; }
    rewind(f);
    b.data = malloc((size_t)n ? (size_t)n : 1);
    if (!b.data) { fclose(f); b.why = "out of memory"; return b; }
    b.size = fread(b.data, 1, (size_t)n, f);
    fclose(f);
    if (b.size != (size_t)n) { free(b.data); b.data = NULL; b.why = "short read"; }
    return b;
}

void uw_free(uw_blob *b) { free(b->data); b->data = NULL; b->size = 0; }

bool uw_ark_open(uw_ark *a, const char *path) {
    memset(a, 0, sizeof *a);
    a->file = uw_read_file(path);
    if (!a->file.data || a->file.size < 2) return false;
    a->slot_count = uw_u16(a->file.data);
    if (a->file.size < 2 + (size_t)a->slot_count * 4) return false;
    a->slot_off = malloc(sizeof(uint32_t) * (size_t)a->slot_count);
    if (!a->slot_off) return false;
    for (int i = 0; i < a->slot_count; i++)
        a->slot_off[i] = uw_u32(a->file.data + 2 + (size_t)i * 4);
    return true;
}

void uw_ark_close(uw_ark *a) {
    free(a->slot_off);
    uw_free(&a->file);
    memset(a, 0, sizeof *a);
}

size_t uw_ark_block(const uw_ark *a, int slot, const uint8_t **out) {
    *out = NULL;
    if (slot < 0 || slot >= a->slot_count) return 0;
    uint32_t off = a->slot_off[slot];
    if (!off || off >= a->file.size) return 0;
    /* The gap to the nearest LARGER offset anywhere in the table, or to
     * end of file. NOT "the next non-zero offset in slot order":
     * slot order and offset order are the same only
     * until the engine's writer rewrites a block that no longer fits where
     * it was and appends it at the end. Both shipped UW1 archives still
     * ascend -- uw_ark_offsets_ascend() says so, and a test asserts it -- so
     * the two rules agree on the retail data and differ on a saved one. */
    uint32_t end = (uint32_t)a->file.size;
    for (int i = 0; i < a->slot_count; i++) {
        uint32_t o = a->slot_off[i];
        if (o > off && o < end) end = o;
    }
    if (end <= off) return 0;
    *out = a->file.data + off;
    return end - off;
}


bool uw_ark_write_block(uw_ark *a, int slot, const uint8_t *data, size_t len) {
    uint32_t off, gap, size = (uint32_t)a->file.size;
    uint8_t *grown;
    if (slot < 0 || slot >= a->slot_count || len > 0xffff) return false;
    off = a->slot_off[slot];
    if (!off) {
        /* seek to the end and write; the offset recorded, the table dirty */
        grown = realloc(a->file.data, size + len);
        if (!grown) return false;
        memcpy(grown + size, data, len);
        a->file.data = grown;
        a->file.size = size + len;
        a->slot_off[slot] = size;
    } else {
        gap = size - off;
        for (int i = 0; i < a->slot_count; i++)
            if (a->slot_off[i] > off && a->slot_off[i] - off < gap) gap = a->slot_off[i] - off;
        if (gap == len) {
            memcpy(a->file.data + off, data, len);
            return true;
        }
        /* through _arc.tmp: the bytes before the block, the bytes after it,
         * then the new block; every later offset less the old gap */
        grown = malloc(size - gap + len ? size - gap + len : 1);
        if (!grown) return false;
        memcpy(grown, a->file.data, off);
        memcpy(grown + off, a->file.data + off + gap, size - off - gap);
        memcpy(grown + size - gap, data, len);
        for (int i = 0; i < a->slot_count; i++)
            if (a->slot_off[i] && a->slot_off[i] > off) a->slot_off[i] -= gap;
        a->slot_off[slot] = size - gap;
        free(a->file.data);
        a->file.data = grown;
        a->file.size = size - gap + len;
    }
    for (int i = 0; i < a->slot_count; i++) {
        uint32_t o = a->slot_off[i];
        a->file.data[2 + i * 4] = (uint8_t)o;
        a->file.data[3 + i * 4] = (uint8_t)(o >> 8);
        a->file.data[4 + i * 4] = (uint8_t)(o >> 16);
        a->file.data[5 + i * 4] = (uint8_t)(o >> 24);
    }
    return true;
}

bool uw_ark_offsets_ascend(const uw_ark *a) {
    uint32_t last = 0;
    for (int i = 0; i < a->slot_count; i++) {
        uint32_t o = a->slot_off[i];
        if (!o) continue;
        if (o <= last) return false;
        last = o;
    }
    return true;
}

