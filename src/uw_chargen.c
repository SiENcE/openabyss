/* SPDX-License-Identifier: MIT */
#include "uw_chargen.h"
#include <string.h>

bool uw_chargen_open(uw_chargen *c, const char *path) {
    memset(c, 0, sizeof *c);
    c->file = uw_read_file(path);
    size_t head = UW_CHARGEN_STEPS * UW_CHARGEN_STRIDE;
    if (!c->file.data || c->file.size < head) {
        uw_free(&c->file);
        return false;
    }
    size_t p = head;
    for (int s = 0; s < UW_CHARGEN_STEPS; s++) {
        c->list_at[s] = (int)p;
        int n = 0;
        while (p + 2 <= c->file.size) {
            uint16_t w = uw_u16(c->file.data + p);
            p += 2;
            if (!w) break;
            n++;
        }
        c->list_len[s] = n;
    }
    c->consumed = p;
    return true;
}

void uw_chargen_close(uw_chargen *c) { uw_free(&c->file); }

uint16_t uw_chargen_word(const uw_chargen *c, int step, int w) {
    if (step < 0 || step >= UW_CHARGEN_STEPS || w < 0 || w > 8) return 0;
    return uw_u16(c->file.data + (size_t)step * UW_CHARGEN_STRIDE
                  + (size_t)w * 2);
}

uint16_t uw_chargen_choice(const uw_chargen *c, int step, int i) {
    if (step < 0 || step >= UW_CHARGEN_STEPS) return 0;
    if (i < 0 || i >= c->list_len[step]) return 0;
    return uw_u16(c->file.data + (size_t)c->list_at[step] + (size_t)i * 2);
}

bool uw_skills_open(uw_skills *s, const char *path) {
    memset(s, 0, sizeof *s);
    s->file = uw_read_file(path);
    if (!s->file.data || s->file.size < UW_SKILL_TABLE_AT) {
        uw_free(&s->file);
        return false;
    }
    size_t p = UW_SKILL_TABLE_AT;
    while (p < s->file.size) {
        size_t n = s->file.data[p];
        if (p + 1 + n > s->file.size) break;
        p += 1 + n;
        s->records++;
    }
    s->consumed = p;
    return true;
}

void uw_skills_close(uw_skills *s) { uw_free(&s->file); }

int uw_skills_attribute(const uw_skills *s, int cls, int a) {
    if (cls < 0 || cls >= UW_SKILL_CLASSES || a < 0 || a > 3) return 0;
    return s->file.data[(size_t)cls * 4 + (size_t)a];
}

const uint8_t *uw_skills_record(const uw_skills *s, int cls, int step,
                                int *len) {
    if (cls < 0 || cls >= UW_SKILL_CLASSES ||
        step < 0 || step >= UW_SKILL_PER_CLASS) return NULL;
    int want = cls * UW_SKILL_PER_CLASS + step;
    size_t p = UW_SKILL_TABLE_AT;
    for (int k = 0; p < s->file.size; k++) {
        size_t n = s->file.data[p];
        if (p + 1 + n > s->file.size) break;
        if (k == want) {
            if (len) *len = (int)n;
            return s->file.data + p + 1;
        }
        p += 1 + n;
    }
    return NULL;
}
