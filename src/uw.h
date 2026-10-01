/* SPDX-License-Identifier: MIT */
/* Common types and helpers for the Ultima Underworld reimplementation.
 *
 * C11, freestanding of any host toolkit: everything here reads the retail
 * data files and computes, and nothing draws, plays sound or takes input.
 *
 * NAMING. Every function and variable is named for the routine or datum of
 * the original game it stands for, so a name here means the same thing
 * wherever it appears. */
#ifndef UW_H
#define UW_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* UW_NOT_CARRIED(counter): a place the port does not do what the original
 * does -- a call into something not ported yet, data the host did not
 * give, or a read the original makes through one of its own faults, which
 * the port refuses instead. The development build counts each one in the
 * module's `not_carried`, and the runs that drive the port pin those
 * counts; a build with UW_NO_ACCOUNTING compiles every site away and the
 * counters stay 0. */
#ifdef UW_NO_ACCOUNTING
#define UW_NOT_CARRIED(counter) ((void)sizeof(counter))   /* named, never evaluated */
#else
#define UW_NOT_CARRIED(counter) ((void)++(counter))
#endif

/* The retail files are little-endian throughout and this reads them by
 * offset rather than by casting a struct over them: the records are not
 * aligned, several are bit-packed, and a struct overlay would be
 * byte-order- and padding-dependent for no gain. */
static inline uint16_t uw_u16(const uint8_t *p) {
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}
static inline uint32_t uw_u32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8)
         | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/* A file, slurped. `data` is NULL if it could not be read; `why` then says
 * what happened. Nothing here recovers from a missing data file -- the
 * caller is a test or a tool, and both want to stop. */
typedef struct {
    uint8_t *data;
    size_t   size;
    const char *why;
} uw_blob;

uw_blob uw_read_file(const char *path);
void    uw_free(uw_blob *b);

/* ---- the .ARK container, shared by LEV.ARK and CNV.ARK -----------------
 *
 * A slot count, a table of uint32 offsets (0 = unused), and the blocks. The
 * container stores NO LENGTHS, so a block's length is a gap -- and which gap
 * is a question easily answered wrongly. See uw_ark_block. */
typedef struct {
    uw_blob   file;
    int       slot_count;
    uint32_t *slot_off;   /* 0 = empty */
} uw_ark;

bool   uw_ark_open(uw_ark *a, const char *path);
void   uw_ark_close(uw_ark *a);
/* The block's bytes and its length, or 0 for an empty slot. */
size_t uw_ark_block(const uw_ark *a, int slot, const uint8_t **out);
/* True when the populated offsets ascend with slot number -- the condition
 * under which the two candidate length rules agree. Both shipped UW1
 * archives satisfy it; an archive the engine's own writer has rewritten need
 * not. */
bool   uw_ark_offsets_ascend(const uw_ark *a);
/* ark_write_block and ark_close over the archive in
 * memory. An empty slot's block is appended at the end; a block whose gap
 * (the uw_ark_block rule) is EXACTLY `len` is overwritten in place; any
 * other is cut out, every block past it slides down by the old gap and the
 * new bytes land at the end -- so block order in the file is not stable.
 * The offset table at +2 is kept current, as ark_close rewrites it; the
 * count never is. A slot at or past the count fails here; the original's
 * `count < slot` test lets slot == count through, to a table entry past the
 * end. */
bool   uw_ark_write_block(uw_ark *a, int slot, const uint8_t *data, size_t len);

/* ---- the random number generator -----------------------------------------
 *
 * Borland's LCG, and UW1 NEVER SEEDS IT: the state ships as 1 and the only
 * code that writes it is barter, which uses it as a hash. Do not seed this
 * from the clock: a run is reproducible only while nobody does. */
typedef struct { uint32_t state; } uw_rng;

void uw_rng_init(uw_rng *r);              /* state = 1, as shipped */
void uw_srand(uw_rng *r, uint16_t seed);  /* rt_srand: 16 bits, high half 0 */
int  uw_rand(uw_rng *r);                  /* rt_rand: 0..0x7fff */
long uw_rng_locate(uint32_t state, long limit);   /* draws from the start */

/* roll_dice(count, sides). Returns
 * count..count*sides, and returns `count` unchanged when either argument is
 * non-positive. See uw_rng.c: it is a scaled multiply, not a modulus, and
 * the difference is every value. */
int uw_roll_dice(uw_rng *r, int count, int sides);

#endif /* UW_H */
