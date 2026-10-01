/* SPDX-License-Identifier: MIT */
#include "uw_strings.h"
#include <stdlib.h>
#include <string.h>

#define TERMINATOR '|'
#define DECODE_LIMIT 4096

bool uw_strings_open(uw_strings *s, const char *path) {
    memset(s, 0, sizeof *s);
    s->file = uw_read_file(path);
    if (!s->file.data) return false;
    const uint8_t *d = s->file.data;
    if (s->file.size < 4) return false;

    s->node_count = uw_u16(d);
    if (s->file.size < (size_t)(2 + s->node_count * 4 + 2)) return false;
    s->nodes = malloc(sizeof(uw_huff_node) * (size_t)s->node_count);
    if (!s->nodes) return false;
    for (int i = 0; i < s->node_count; i++) {
        const uint8_t *p = d + 2 + i * 4;
        s->nodes[i] = (uw_huff_node){p[0], p[1], p[2], p[3]};
    }
    /* The LAST node is the root. Not the first, and not node 0 -- the tree is
     * stored leaves-first, which is why a reader that starts at 0 decodes
     * plausible-looking rubbish rather than failing. */
    s->root = s->node_count - 1;

    size_t p = 2 + (size_t)s->node_count * 4;
    s->block_count = uw_u16(d + p);
    if (s->file.size < p + 2 + (size_t)s->block_count * 6) return false;
    s->block_id = malloc(sizeof(uint16_t) * (size_t)s->block_count);
    s->block_off = malloc(sizeof(uint32_t) * (size_t)s->block_count);
    if (!s->block_id || !s->block_off) return false;
    for (int i = 0; i < s->block_count; i++) {
        const uint8_t *e = d + p + 2 + (size_t)i * 6;
        s->block_id[i] = uw_u16(e);
        s->block_off[i] = uw_u32(e + 2);
    }
    return true;
}

void uw_strings_close(uw_strings *s) {
    free(s->nodes); free(s->block_id); free(s->block_off);
    uw_free(&s->file);
    memset(s, 0, sizeof *s);
}

int uw_strings_block_count(const uw_strings *s, int bi) {
    if (bi < 0 || bi >= s->block_count) return 0;
    return uw_u16(s->file.data + s->block_off[bi]);
}

static int decode(uw_strings *s, size_t base, size_t byte_off,
                  char *out, int cap) {
    const uint8_t *d = s->file.data;
    long bit = (long)byte_off * 8;
    int node = s->root, n = 0;
    while (n < DECODE_LIMIT) {
        size_t idx = base + (size_t)(bit >> 3);
        if (idx >= s->file.size) { s->unterminated++; break; }
        int b = (d[idx] >> (7 - (bit & 7))) & 1;
        bit++;
        node = b ? s->nodes[node].right : s->nodes[node].left;
        if (node < 0 || node >= s->node_count) { s->unterminated++; break; }
        if (s->nodes[node].left == 0xFF && s->nodes[node].right == 0xFF) {
            char c = (char)s->nodes[node].symbol;
            if (c == TERMINATOR) break;
            if (n < cap - 1) out[n] = c;
            n++;
            node = s->root;
        }
    }
    if (cap > 0) out[n < cap ? n : cap - 1] = '\0';
    return n;
}

int uw_strings_get(uw_strings *s, int bi, int index, char *out, int cap) {
    if (cap > 0) out[0] = '\0';
    if (bi < 0 || bi >= s->block_count) return -1;
    size_t off = s->block_off[bi];
    int cnt = uw_u16(s->file.data + off);
    if (index < 0 || index >= cnt) return -1;
    size_t base = off + 2 + (size_t)cnt * 2;
    size_t so = uw_u16(s->file.data + off + 2 + (size_t)index * 2);
    return decode(s, base, so, out, cap);
}

int uw_strings_find_block(const uw_strings *s, uint16_t block_id) {
    for (int i = 0; i < s->block_count; i++)
        if (s->block_id[i] == block_id) return i;
    return -1;
}

int uw_strings_by_id(uw_strings *s, uint16_t packed, char *out, int cap) {
    int bi = uw_strings_find_block(s, (uint16_t)(packed >> 9));
    if (bi < 0) { if (cap > 0) out[0] = '\0'; return -1; }
    return uw_strings_get(s, bi, packed & 0x1FF, out, cap);
}

static int strcache_slot(const uw_strcache *c, uint16_t block) {
    int i;
    for (i = 0; i < c->nslots; i++)
        if (c->slot[i].block == block) return i;
    return -1;
}

char *uw_strcache_get(uw_strcache *c, uint16_t id) {
    uint16_t block = (uint16_t)(id >> 9);
    int k = strcache_slot(c, block);
    char *out;
    if (k >= 0) return c->slot[k].text[id & 0x1ff];
    if (block == 0) block = c->default_block;
    out = c->ring[c->ring_at / UW_STRCACHE_LEN];
    out[0] = '\0';
    if (c->pak) {
        int bi = uw_strings_find_block(c->pak, block);
        /* strings_read_block advances only past a string it decoded. */
        if (bi >= 0 && uw_strings_get(c->pak, bi, id & 0x1ff, out, UW_STRCACHE_LEN) >= 0)
            c->ring_at = (c->ring_at + UW_STRCACHE_LEN) % (UW_STRCACHE_RING * UW_STRCACHE_LEN);
    }
    return out;
}

uint16_t uw_strcache_add(uw_strcache *c, char *text, uint16_t block) {
    int k = strcache_slot(c, block);
    uint16_t index;
    if (k < 0) {
        if (c->nslots > 1) return 0;
        k = c->nslots++;
        memset(&c->slot[k], 0, sizeof c->slot[k]);
        c->slot[k].block = block;
    }
    /* A 513th string lands on the count in the original; here it is refused
     * rather than written past the table. */
    index = c->slot[k].count;
    if (index >= 0x200) return 0;
    c->slot[k].text[index] = text;
    c->slot[k].count++;
    return (uint16_t)(index | block << 9);
}

uint16_t uw_strcache_set(uw_strcache *c, char *text, uint16_t id) {
    int k = strcache_slot(c, (uint16_t)(id >> 9));
    if (k < 0) return 0;
    c->slot[k].text[id & 0x1ff] = text;
    return id;
}

uint16_t uw_strcache_segment(const uw_strcache *c, const char *p) {
    int k, i;
    if (p >= c->ring[0] && p < c->ring[0] + sizeof c->ring) return c->ring_seg;
    for (k = 0; k < c->nslots; k++)
        for (i = 0; i < 0x200; i++)
            if (c->slot[k].text[i] == p) return c->slot[k].seg[i];
    return 0;
}

void uw_strcache_reset(uw_strcache *c, uint16_t block) {
    int k = strcache_slot(c, block);
    if (k < 0) return;
    memset(c->slot[k].text, 0, sizeof c->slot[k].text);
    c->slot[k].count = 0;
}
