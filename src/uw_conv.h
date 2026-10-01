/* SPDX-License-Identifier: MIT */
/* CNV.ARK -- the conversation bytecode, and the VM's instruction set.
 *
 * The container is the shared .ARK in uw.h;
 * what is here is the per-conversation block and the opcode table.
 *
 * The VM is a WORD-ADDRESSED stack machine, and everything asserted about it
 * was read out of the interpreter in UW.EXE rather than taken on faith:
 * opcodes are 0x00..0x29 and range-checked before dispatch, the instruction
 * pointer is a word index, execution refuses to start unless the first
 * opcode is START, and exactly eight opcodes take a following operand word.
 *
 * THE NAMES ARE THE GAME'S OWN. UW.EXE carries all 42 as a run of
 * NUL-terminated strings, read by a conversation-VM debug disassembler left
 * in the shipped binary, and that table agrees index for index with the one
 * here -- including the 0x25..0x29 shift the Python's header records as a
 * correction over references/, where everything from 0x25 up was off by one.
 */
#ifndef UW_CONV_H
#define UW_CONV_H

#include "uw.h"

#define UW_CONV_SIG      0x0828
#define UW_CONV_MAX_OP   0x29
#define UW_CONV_OPCODES  (UW_CONV_MAX_OP + 1)
#define UW_OP_START      0x22

typedef struct {
    const char *name;
    int         operands;     /* 0 or 1 following words */
} uw_conv_op;

extern const uw_conv_op uw_conv_ops[UW_CONV_OPCODES];

typedef struct {
    uint16_t    id, type, ret;
    int         name_len;
    const char *name;         /* NOT NUL-terminated; name_len bytes */
} uw_conv_import;

#define UW_CONV_TYPE_FUNCTION 0x0111
#define UW_CONV_TYPE_VARIABLE 0x010F

typedef struct {
    int             slot;
    const uint8_t  *block;
    size_t          block_len;
    uint16_t        sig, code_words, strblock, memslots, nimports;
    size_t          code_off;     /* into `block` */
    size_t          end;          /* header + imports + code */
    const uint16_t *code_raw;     /* unaligned: read with uw_conv_word */
} uw_conv;

/* Parses the block for `slot`. False when the slot is empty or the block is
 * too short for its own header and import table. */
bool     uw_conv_open(uw_conv *c, const uw_ark *a, int slot);
uint16_t uw_conv_word(const uw_conv *c, int i);
/* Import `i`, or false. The name points into the block. */
bool     uw_conv_import_at(const uw_conv *c, int i, uw_conv_import *out);

/* Steps the linear disassembly: reads the opcode at word `i`, writes the
 * operand to `*arg` (-1 when the opcode takes none) and returns the index of
 * the next instruction. A LINEAR sweep desynchronises wherever the code
 * embeds data, then re-synchronises -- which is why the tests hold the
 * out-of-range and bad-branch counts to a tolerance rather than to zero. */
int uw_conv_step(const uw_conv *c, int i, int *op, long *arg);
/* True for the five opcodes whose operand is a code address. */
bool uw_conv_is_branch(int op);

/* ---- BABGLOBS.DAT: the conversation variable template ------------------
 *
 * A flat sequence of records with no file header and no count:
 *
 *     uint16 slot     conversation slot in CNV.ARK, strictly ascending
 *     uint16 count    number of 16-bit variables
 *     uint16 data[count]   -- present in BGLOBALS.DAT, absent here
 *
 * `count` is the conversation header's memory-slot field, and THE TWO FILES
 * COME FROM DIFFERENT PARTS OF THE BUILD AND NEITHER REFERENCES THE OTHER,
 * so their agreeing is evidence for both format readings rather than
 * self-consistency. That is why the test asserts it instead of printing it.
 *
 * Records ascend because the engine's reader stops as soon as it sees a slot
 * greater than the one it wants.
 *
 * BGLOBALS.DAT does not ship: the game makes it at new-game time by copying
 * each 4-byte header and emitting `count` zero words, so its size is
 * predictable from the template alone. */
typedef struct {
    uw_blob file;
    int     count;          /* records; the template is headers only */
} uw_babglobs;

bool   uw_babglobs_open(uw_babglobs *g, const char *path);
void   uw_babglobs_close(uw_babglobs *g);
bool   uw_babglobs_record(const uw_babglobs *g, int i, int *slot, int *vars);
/* The size BGLOBALS.DAT will have when the game creates it from this. */
size_t uw_babglobs_live_size(const uw_babglobs *g);

/* ---- BGLOBALS.DAT: the live copy in SAVE0 ------------------------------
 *
 * bglobals_create makes it at a new game: each template header
 * copied and `count` zero words after it, from a 4K buffer memset to zero
 * (a count past 0x800 would copy what lies beyond; none is). conv_load's
 * bglobals_load_conv and conv_unload's bglobals_save_conv
 * walk it for conv_slot_wanted, stopping at the
 * first record past it, and read or write that record's words clamped to
 * the conversation's memory-slot count: the VM's first words in when a
 * conversation opens, back out when it ends. A save writes nothing of its
 * own here -- save_game copies SAVE0's file whole. */
size_t uw_bglobals_create(const uw_babglobs *g, uint8_t *out, size_t cap);
/* The record's words from `words`, in place; false when no record is the
 * slot's, or the words would run past the end (the original would grow the
 * file). */
bool   uw_bglobals_save_conv(uint8_t *file, size_t size, uint16_t slot,
                             const uint16_t *words, int max_words);
/* The record's words into `words`. The load does not stop at its record:
 * only a short read ends the walk, so it reads the next header too, and a
 * clamp below the record's count would read its remaining words as one. */
bool   uw_bglobals_load_conv(const uint8_t *file, size_t size, uint16_t slot,
                             uint16_t *words, int max_words);

#endif
