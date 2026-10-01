/* SPDX-License-Identifier: MIT */
/* DATA/CHRGEN.DAT and DATA/SKILLS.DAT -- the eight character-generation
 * steps, and the per-class skill choices behind the skill step.
 *
 * `chargen_screen` loads
 * SKILLS.DAT into a scratch buffer, loads CHRGEN.DAT DIRECTLY AFTER IT in
 * the same buffer, and walks the result as eight 18-byte records:
 *
 *     for (i = 0; i < 8; i++) {
 *         rec[i].choices = p;           // a far pointer, patched in
 *         while (*p++ != 0) ;           // ...at bytes 4..7 of the record
 *     }
 *
 * So CHRGEN.DAT is eight nine-word records and then eight NUL-terminated
 * word lists -- and BYTES 4..7 OF EVERY RECORD ARE OVERWRITTEN AT LOAD, so
 * whatever the file holds there is never read. This reader exposes word 4 as
 * the choice count anyway, because the highlight mover bounds the index with
 * it before the pointer patch matters.
 *
 * WORD 0 IS A STRING INDEX IN BLOCK 2, and that is what identifies the whole
 * table: the eight values are 1, 2, 3, 4, 0, 6, 7, 8, and block 2 reads
 * "Choose character sex:", "Select handedness:", "Pick a class:", "Pick a
 * skill:", "", "Choose difficulty:", "Name: ", "Keep this character?".
 *
 * The choice lists are block-2 indices too: [9, 10] is Male/Female, [11, 12]
 * Left/Right, [13, 14] Standard/Easy, [15, 16] Yes/No, and [23..30] is
 * Fighter, Mage, Bard, Tinker, Druid, Paladin, Ranger, Shepherd.
 *
 * THREE STEPS ARE DECIDED AT RUNTIME and carry placeholder lists rather than
 * empty ones -- step 3 has twenty copies of index 1. They are space, not
 * text: the real skill list depends on the class, which is what SKILLS.DAT
 * is for. So word 4 equals the list length for five steps and not for three,
 * and asserting exactly that split is what says the field was read right.
 *
 * SKILLS.DAT: the first 0x20 bytes are eight groups of four
 * starting attributes, and from 0x20 the file is length-prefixed byte
 * records `{ byte n; byte v[n] }`, walked `(class * 5) + step` times -- so
 * eight classes of five records, 40 with nothing left over. The walker reads
 * n == 0 as a fixed 0x14, n == 1 as v[0] taken outright, and n > 1 as a
 * choice list shown as string `v[i] + 0x1f`. Block 2 index 31 onwards is
 * Attack, Defense, Unarmed, Sword, Axe, Mace, Missile, Mana, Lore.
 */
#ifndef UW_CHARGEN_H
#define UW_CHARGEN_H

#include "uw.h"

#define UW_CHARGEN_STEPS      8
#define UW_CHARGEN_STRIDE    18       /* nine words */
#define UW_CHARGEN_BLOCK      2       /* the STRINGS.PAK block it indexes */
#define UW_SKILL_STRING_BASE 0x1f     /* v[i] + 0x1f is the skill's index */
#define UW_SKILL_CLASSES      8
#define UW_SKILL_PER_CLASS    5
#define UW_SKILL_TABLE_AT  0x20       /* the attribute table ends here */
#define UW_SKILL_FIXED     0x14       /* what n == 0 means to the walker */

typedef struct {
    uw_blob file;
    int     list_at[UW_CHARGEN_STEPS];   /* into the file */
    int     list_len[UW_CHARGEN_STEPS];
    size_t  consumed;                    /* where the last list ended */
} uw_chargen;

bool uw_chargen_open(uw_chargen *c, const char *path);
void uw_chargen_close(uw_chargen *c);
/* Word `w` of step `s` (0..8). Word 0 is the prompt, word 4 the count. */
uint16_t uw_chargen_word(const uw_chargen *c, int step, int w);
/* Choice `i` of step `s`, as a block-2 string index. */
uint16_t uw_chargen_choice(const uw_chargen *c, int step, int i);

typedef struct {
    uw_blob file;
    int     records;            /* walked from UW_SKILL_TABLE_AT */
    size_t  consumed;
} uw_skills;

bool uw_skills_open(uw_skills *s, const char *path);
void uw_skills_close(uw_skills *s);
/* Starting attribute `a` (0..3) of class `c` (0..7), from the first 0x20. */
int  uw_skills_attribute(const uw_skills *s, int cls, int a);
/* Record `(cls * 5) + step`: its length and its bytes. */
const uint8_t *uw_skills_record(const uw_skills *s, int cls, int step,
                                int *len);

#endif
