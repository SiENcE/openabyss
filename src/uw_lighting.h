/* SPDX-License-Identifier: MIT */
/* The lighting tables: LIGHT.DAT, MONO.DAT, SHADES.DAT, XFER.DAT.
 *
 * Every one of them is a flat table with no header, and the shape came from
 * the consumer rather than from the bytes.
 */
#ifndef UW_LIGHTING_H
#define UW_LIGHTING_H

#include "uw.h"

#define UW_LIGHT_ROWS  16
#define UW_LIGHT_ROW   256
#define UW_LIGHT_SIZE  (UW_LIGHT_ROWS * UW_LIGHT_ROW)   /* 0x1000 */
#define UW_SHADE_LEVELS 8
#define UW_SHADE_WORDS  6

typedef struct {
    uint8_t ramp[UW_LIGHT_ROWS][UW_LIGHT_ROW];
} uw_light;

/* Row L maps each palette entry to the entry it is drawn as at light level L.
 * shade_set_level reads exactly 0x1000 bytes, and swaps WHICH FILE it reads:
 * level 5 is the monochrome one and every other level uses the colour ramp. */
bool uw_light_load(uw_light *l, const char *path);
int  uw_light_identity_count(const uw_light *l, int row);
int  uw_light_distinct(const uw_light *l, int row);

typedef struct { int16_t w[UW_SHADE_LEVELS][UW_SHADE_WORDS]; } uw_shades;
bool uw_shades_load(uw_shades *s, const char *path);

#endif
