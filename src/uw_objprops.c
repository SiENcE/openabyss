/* SPDX-License-Identifier: MIT */
#include "uw_objprops.h"
#include <string.h>

/* Offsets are the running sum of the sizes before each section, which is
 * exactly how the loader reads them: fread after fread with no seek. The
 * check that this is right is that the sizes total the file's length. */
const uw_objprop_layout uw_objprop_sections[UW_SEC_COUNT] = {
    {0x0002, 16, 0x08, 0x000, "melee_weapon_props"},
    {0x0082, 16, 0x03, 0x010, "missile_props"},
    {0x00b2, 32, 0x04, 0x020, "armour_props"},
    {0x0132, 64, 0x30, 0x040, "critter_properties"},
    {0x0d32, 16, 0x03, 0x080, "container_props"},
    {0x0d62, 16, 0x02, 0x090, "light_burn_rates"},
    {0x0d82, 16, 0x01, 0x0b0, "food_props"},
    {0x0d92, 16, 0x01, 0x1a0, "trigger_props"},
    {0x0da2, 16, 0x04, 0x1c0, "animation_props"},
};

bool uw_objprops_open(uw_objprops *p, const char *path) {
    memset(p, 0, sizeof *p);
    p->file = uw_read_file(path);
    return p->file.data != NULL && p->file.size >= UW_OBJPROPS_SIZE;
}

void uw_objprops_close(uw_objprops *p) { uw_free(&p->file); }

const uint8_t *uw_objprop_section_data(const uw_objprops *p,
                                       uw_objprop_section s, size_t *len) {
    if (s < 0 || s >= UW_SEC_COUNT) return NULL;
    const uw_objprop_layout *l = &uw_objprop_sections[s];
    size_t n = (size_t)l->count * (size_t)l->stride;
    if (l->offset + n > p->file.size) return NULL;
    if (len) *len = n;
    return p->file.data + l->offset;
}

const uint8_t *uw_objprop_record(const uw_objprops *p, uw_objprop_section s,
                                 int i) {
    const uw_objprop_layout *l = &uw_objprop_sections[s];
    const uint8_t *d = uw_objprop_section_data(p, s, NULL);
    if (!d || i < 0 || i >= l->count) return NULL;
    return d + (size_t)i * (size_t)l->stride;
}

const uint8_t *uw_critter(const uw_objprops *p, int type) {
    return uw_objprop_record(p, UW_SEC_CRITTER, type);
}

int uw_signed_byte(uint8_t b) { return b > 127 ? (int)b - 256 : (int)b; }
