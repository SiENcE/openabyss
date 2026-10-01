/* SPDX-License-Identifier: MIT */
#include "uw_comobj.h"
#include <string.h>

bool uw_comobj_open(uw_comobj *c, const char *path) {
    memset(c, 0, sizeof *c);
    c->file = uw_read_file(path);
    if (!c->file.data ||
        c->file.size < UW_COMOBJ_HEADER + UW_COMOBJ_PAYLOAD) {
        uw_free(&c->file);
        return false;
    }
    return true;
}

void uw_comobj_close(uw_comobj *c) { uw_free(&c->file); }

const uint8_t *uw_comobj_record(const uw_comobj *c, int item) {
    if (item < 0 || item >= UW_COMOBJ_ITEMS) return NULL;
    return c->file.data + UW_COMOBJ_HEADER + (size_t)item * UW_COMOBJ_STRIDE;
}

int uw_comobj_height(const uw_comobj *c, int item) {
    const uint8_t *r = uw_comobj_record(c, item);
    return r ? r[0] : 0;
}

int uw_comobj_weight(const uw_comobj *c, int item) {
    const uint8_t *r = uw_comobj_record(c, item);
    return r ? uw_u16(r + 1) >> 4 : 0;
}

bool uw_comobj_has_shape(const uw_comobj *c, int item) {
    const uint8_t *r = uw_comobj_record(c, item);
    return r && (uw_u16(r + 1) & 8);
}

int uw_comobj_radius(const uw_comobj *c, int item) {
    const uint8_t *r = uw_comobj_record(c, item);
    return r ? uw_u16(r + 1) & 7 : 0;
}

bool uw_comobj_raises_floor(const uw_comobj *c, int item) {
    const uint8_t *r = uw_comobj_record(c, item);
    return r && (r[3] & 2);
}

int uw_comobj_storage_class(const uw_comobj *c, int item) {
    const uint8_t *r = uw_comobj_record(c, item);
    return r ? r[3] >> 6 : 0;
}

bool uw_comobj_ownable(const uw_comobj *c, int item) {
    const uint8_t *r = uw_comobj_record(c, item);
    return r && (r[7] & 0x80);
}
