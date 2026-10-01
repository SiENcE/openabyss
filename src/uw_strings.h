/* SPDX-License-Identifier: MIT */
/* STRINGS.PAK -- every line of text in the game, Huffman-coded.
 * The engine's own lookup is what fixes the packed-id split.
 */
#ifndef UW_STRINGS_H
#define UW_STRINGS_H

#include "uw.h"

typedef struct { uint8_t symbol, parent, left, right; } uw_huff_node;

typedef struct {
    uw_blob        file;
    uw_huff_node  *nodes;
    int            node_count;
    int            root;
    uint16_t      *block_id;      /* block_count entries */
    uint32_t      *block_off;
    int            block_count;
    /* Decodes that ended without hitting the terminator. Counted rather than
     * ignored: the two ways out of the decoder other than `|` are the 4,096
     * byte limit and running off the end of the file, and the second one
     * returns a short, plausible string. A truncated STRINGS.PAK read as
     * valid until the Python reader started counting these. */
    int            unterminated;
} uw_strings;

bool uw_strings_open(uw_strings *s, const char *path);
void uw_strings_close(uw_strings *s);

int  uw_strings_block_count(const uw_strings *s, int block_index);
/* Writes at most `cap` bytes including the NUL; returns the length written. */
int  uw_strings_get(uw_strings *s, int block_index, int index,
                    char *out, int cap);
/* Engine addressing: block = id >> 9, index = id & 0x1ff -- get_string's
 * arithmetic, which is what pins the packed form. Note that most of this
 * project's prose cites a BLOCK and an INDEX separately ("block 1 string
 * 0x172"), which is not a packed id: 0x172 packed is block 0. */
int  uw_strings_by_id(uw_strings *s, uint16_t packed, char *out, int cap);
/* Table position of a block id, or -1. Blocks are keyed by id, and the ids
 * are not the positions. */
int  uw_strings_find_block(const uw_strings *s, uint16_t block_id);

/* The engine's own lookup with its run-time blocks in front of the file:
 * get_string, string_block_add, string_block_set and string_block_reset.
 *
 * TWO SLOTS of 0x200 string pointers, each claimed by a block id, that the
 * game appends its own strings to -- the player's name (block 0x7d) and a
 * conversation's run-time strings (0x7c: the typed answer, copies, appends).
 * An id whose block holds a slot is answered from the slot, NULL where
 * nothing was stored; any other is read out of STRINGS.PAK, a block half of 0
 * meaning the default block (the conversation's own), into a RING
 * of eight 0x200-byte buffers (strings_read_block) -- so a returned string
 * stays valid for seven more reads, an index past its block's count comes
 * back empty WITHOUT advancing the ring (the next read overwrites it), and
 * callers that write into what they were given (conv_bi_contains lower-cases
 * in place) change the slot's string or the ring's copy. The pointers are
 * the caller's: nothing here allocates or frees a slot's text. */
#define UW_STRCACHE_RING 8
#define UW_STRCACHE_LEN  0x200
typedef struct {
    uw_strings *pak;
    uint16_t    default_block;                    /* the block a 0 block half means */
    int         nslots;                           /* the slots in use */
    struct { uint16_t block, count; char *text[0x200]; uint16_t seg[0x200]; } slot[2];
    char        ring[UW_STRCACHE_RING][UW_STRCACHE_LEN];
    int         ring_at;
    uint16_t    ring_seg;                         /* the ring's segment (strings_read_block's constant) */
} uw_strcache;

char    *uw_strcache_get(uw_strcache *c, uint16_t id);
/* The id `index | block << 9`, or 0 when both slots belong to other blocks. */
uint16_t uw_strcache_add(uw_strcache *c, char *text, uint16_t block);
/* The id back, or 0 when its block holds no slot. */
uint16_t uw_strcache_set(uw_strcache *c, char *text, uint16_t id);
void     uw_strcache_reset(uw_strcache *c, uint16_t block);
/* The segment of a string this cache handed out, as the original's far
 * pointer has it -- the ring's, or the one recorded with a slot's text -- or
 * 0 for a pointer it does not know. What a builtin that leaves a far
 * pointer's segment in AX needs. */
uint16_t uw_strcache_segment(const uw_strcache *c, const char *p);

#endif
