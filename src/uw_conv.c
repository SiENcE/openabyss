/* SPDX-License-Identifier: MIT */
#include "uw_conv.h"
#include <string.h>

/* Names and operand counts. Behaviour was checked against the interpreter
 * where the Python marks it; the rest are named by position in a sequence
 * whose endpoints are confirmed -- and the whole table then agreed, index
 * for index, with the mnemonic run UW.EXE itself carries. */
const uw_conv_op uw_conv_ops[UW_CONV_OPCODES] = {
    {"NOP", 0},        {"OPADD", 0},      {"OPMUL", 0},      {"OPSUB", 0},
    {"OPDIV", 0},      {"OPMOD", 0},      {"OPOR", 0},       {"OPAND", 0},
    {"OPNOT", 0},      {"TSTGT", 0},      {"TSTGE", 0},      {"TSTLT", 0},
    {"TSTLE", 0},      {"TSTEQ", 0},      {"TSTNE", 0},      {"JMP", 1},
    {"BEQ", 1},        {"BNE", 1},        {"BRA", 1},        {"CALL", 1},
    {"CALLI", 1},      {"RET", 0},        {"PUSHI", 1},      {"PUSHI_EFF", 1},
    {"POP", 0},        {"SWAP", 0},       {"PUSHBP", 0},     {"POPBP", 0},
    {"SPTOBP", 0},     {"BPTOSP", 0},     {"ADDSP", 0},      {"FETCHM", 0},
    {"STO", 0},        {"OFFSET", 0},     {"START", 0},      {"SAVE_REG", 0},
    {"PUSH_REG", 0},   {"STRCMP", 0},     {"EXIT_OP", 0},    {"SAY_OP", 0},
    {"RESPOND_OP", 0}, {"OPNEG", 0},
};

bool uw_conv_is_branch(int op) {
    return op >= 0x0f && op <= 0x13;      /* JMP BEQ BNE BRA CALL */
}

bool uw_conv_open(uw_conv *c, const uw_ark *a, int slot) {
    memset(c, 0, sizeof *c);
    const uint8_t *b;
    size_t n = uw_ark_block(a, slot, &b);
    if (!n || n < 16) return false;
    c->slot = slot;
    c->block = b;
    c->block_len = n;
    c->sig        = uw_u16(b + 0);
    c->code_words = uw_u16(b + 4);
    c->strblock   = uw_u16(b + 10);
    c->memslots   = uw_u16(b + 12);
    c->nimports   = uw_u16(b + 14);

    size_t p = 16;
    for (int i = 0; i < c->nimports; i++) {
        if (p + 2 > n) return false;
        uint16_t len = uw_u16(b + p);
        p += 2;
        /* The name is not NUL-terminated, so its length is the only thing
         * that says where the eight trailing bytes start. */
        if (p + len + 8 > n) return false;
        p += (size_t)len + 8;
    }
    c->code_off = p;
    c->end = p + (size_t)c->code_words * 2;
    c->code_raw = NULL;
    return true;
}

uint16_t uw_conv_word(const uw_conv *c, int i) {
    if (i < 0 || i >= c->code_words) return 0;
    size_t at = c->code_off + (size_t)i * 2;
    if (at + 2 > c->block_len) return 0;
    return uw_u16(c->block + at);
}

bool uw_conv_import_at(const uw_conv *c, int i, uw_conv_import *out) {
    if (i < 0 || i >= c->nimports) return false;
    size_t p = 16;
    for (int k = 0; k <= i; k++) {
        if (p + 2 > c->block_len) return false;
        uint16_t len = uw_u16(c->block + p);
        p += 2;
        if (p + len + 8 > c->block_len) return false;
        if (k == i) {
            out->name_len = len;
            out->name = (const char *)(c->block + p);
            out->id   = uw_u16(c->block + p + len + 0);
            out->type = uw_u16(c->block + p + len + 4);
            out->ret  = uw_u16(c->block + p + len + 6);
            return true;
        }
        p += (size_t)len + 8;
    }
    return false;
}

int uw_conv_step(const uw_conv *c, int i, int *op, long *arg) {
    int o = uw_conv_word(c, i);
    *op = o;
    /* An out-of-range opcode has no operand count, so the sweep advances by
     * one and re-synchronises on the next word rather than guessing. */
    int nargs = (o <= UW_CONV_MAX_OP) ? uw_conv_ops[o].operands : 0;
    if (nargs && i + 1 < c->code_words) *arg = uw_conv_word(c, i + 1);
    else *arg = -1;
    return i + 1 + nargs;
}

/* ---- BABGLOBS.DAT --------------------------------------------------------
 *
 * The template ships with headers only, so a record is four bytes and the
 * record count is the file size over four. A live BGLOBALS.DAT interleaves
 * `count` words after each header and cannot be walked this way; this reader
 * is deliberately the template's alone, and says so rather than half-working
 * on the other file. */
bool uw_babglobs_open(uw_babglobs *g, const char *path) {
    memset(g, 0, sizeof *g);
    g->file = uw_read_file(path);
    if (!g->file.data || g->file.size % 4) { uw_free(&g->file); return false; }
    g->count = (int)(g->file.size / 4);
    return g->count > 0;
}

void uw_babglobs_close(uw_babglobs *g) { uw_free(&g->file); g->count = 0; }

bool uw_babglobs_record(const uw_babglobs *g, int i, int *slot, int *vars) {
    if (i < 0 || i >= g->count) return false;
    const uint8_t *p = g->file.data + (size_t)i * 4;
    if (slot) *slot = uw_u16(p);
    if (vars) *vars = uw_u16(p + 2);
    return true;
}

size_t uw_bglobals_create(const uw_babglobs *g, uint8_t *out, size_t cap) {
    size_t n = 0;
    for (int i = 0; i < g->count; i++) {
        int vars;
        uw_babglobs_record(g, i, NULL, &vars);
        if (n + 4 + 2 * (size_t)vars > cap) return 0;
        memcpy(out + n, g->file.data + (size_t)i * 4, 4);
        memset(out + n + 4, 0, 2 * (size_t)vars);
        n += 4 + 2 * (size_t)vars;
    }
    return n;
}

bool uw_bglobals_save_conv(uint8_t *file, size_t size, uint16_t slot,
                           const uint16_t *words, int max_words) {
    size_t pos = 0;
    while (pos + 4 <= size) {
        uint16_t s = uw_u16(file + pos);
        int n = (int16_t)uw_u16(file + pos + 2);
        pos += 4;
        if (s > slot) return false;
        if (s == slot) {
            if (max_words < n) n = max_words;
            if (n < 0 || pos + 2 * (size_t)n > size) return false;
            for (int k = 0; k < n; k++) {
                file[pos + 2 * (size_t)k] = (uint8_t)words[k];
                file[pos + 2 * (size_t)k + 1] = (uint8_t)(words[k] >> 8);
            }
            return true;
        }
        pos += (uint16_t)(n << 1);
    }
    return false;
}

bool uw_bglobals_load_conv(const uint8_t *file, size_t size, uint16_t slot,
                           uint16_t *words, int max_words) {
    size_t pos = 0;
    bool found = false;
    while (pos + 4 <= size) {
        uint16_t s = uw_u16(file + pos);
        int n = (int16_t)uw_u16(file + pos + 2);
        pos += 4;
        if (s > slot) break;
        if (s == slot) {
            size_t want, got;
            if (max_words < n) n = max_words;
            want = (uint16_t)(n << 1);
            got = size - pos < want ? size - pos : want;
            for (size_t k = 0; k + 1 < got; k += 2) words[k / 2] = uw_u16(file + pos + k);
            pos += got;
            found = true;
            if (got < want) break;
        } else {
            pos += (uint16_t)(n << 1);
        }
    }
    return found;
}

size_t uw_babglobs_live_size(const uw_babglobs *g) {
    size_t n = 4 * (size_t)g->count;
    for (int i = 0; i < g->count; i++) {
        int vars;
        uw_babglobs_record(g, i, NULL, &vars);
        n += 2 * (size_t)vars;
    }
    return n;
}
