/* SPDX-License-Identifier: MIT */
#include "uw_crit.h"
#include <stdio.h>
#include <string.h>

bool uw_crit_assoc_open(uw_crit_assoc *a, const char *path) {
    memset(a, 0, sizeof *a);
    a->file = uw_read_file(path);
    if (!a->file.data ||
        a->file.size < UW_CRIT_TABLE_AT + UW_CRIT_TABLE_BYTES) {
        uw_free(&a->file);
        return false;
    }
    return true;
}

void uw_crit_assoc_close(uw_crit_assoc *a) { uw_free(&a->file); }

const char *uw_crit_page_name(const uw_crit_assoc *a, int page) {
    if (page < 0 || page >= UW_CRIT_PAGES) return NULL;
    return (const char *)a->file.data + (size_t)page * UW_CRIT_NAME_LEN;
}

bool uw_crit_assoc_at(const uw_crit_assoc *a, int type, int *page,
                      int *variant) {
    if (type < 0 || type >= UW_CRIT_CREATURES) return false;
    uint16_t w = uw_u16(a->file.data + UW_CRIT_TABLE_AT + (size_t)type * 2);
    if (page) *page = w & 0xff;
    if (variant) *variant = w >> 8;
    return true;
}

void uw_crit_page_filename(int page, int file, char *out, size_t cap) {
    /* TWO OCTAL DIGITS for the page and two DECIMAL for the extension, which
     * is what crit_pages_init writes. */
    snprintf(out, cap, "CR%d%dPAGE.N%02d", (page >> 3) & 7, page & 7, file);
}

bool uw_crit_page_open(uw_crit_page *p, const char *path) {
    memset(p, 0, sizeof *p);
    p->file = uw_read_file(path);
    if (!p->file.data || p->file.size < 3) { uw_free(&p->file); return false; }
    p->first_slot = p->file.data[0];
    p->slot_count = p->file.data[1];
    p->slot_map_at = 2;
    size_t ec_at = p->slot_map_at + (size_t)p->slot_count;
    if (ec_at >= p->file.size) { uw_free(&p->file); return false; }
    p->entry_count = p->file.data[ec_at];
    p->entries_at = ec_at + 1;
    if (p->entries_at + (size_t)p->entry_count * UW_CRIT_FRAME_SLOTS
        > p->file.size) {
        uw_free(&p->file);
        return false;
    }
    return true;
}

void uw_crit_page_close(uw_crit_page *p) { uw_free(&p->file); }

int uw_crit_page_slot(const uw_crit_page *p, int i) {
    if (i < 0 || i >= p->slot_count) return -1;
    uint8_t v = p->file.data[p->slot_map_at + (size_t)i];
    return v == 0xff ? -1 : v;
}

int uw_crit_page_frame(const uw_crit_page *p, int e, int f) {
    if (e < 0 || e >= p->entry_count) return -1;
    if (f < 0 || f >= UW_CRIT_FRAME_SLOTS) return -1;
    uint8_t v = p->file.data[p->entries_at
                             + (size_t)e * UW_CRIT_FRAME_SLOTS + (size_t)f];
    return v == 0xff ? -1 : v;
}
