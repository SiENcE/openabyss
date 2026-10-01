/* SPDX-License-Identifier: MIT */
/* CUTS/csXXX.n00 -- the cutscene scripts.
 *
 * A cutscene is NOT hard-coded: the .n00 file is
 * a script and `cutscene_process_data` interprets it through a
 * 16-entry far-pointer table. The animation frames live in the
 * other files of the same set, .n01 upwards.
 *
 * The record, from the interpreter's own instructions:
 *
 *     word frame        when this command is due
 *     word opcode       0..15; anything else ends the script
 *     word operand[n]   n comes from the handler
 *
 * and the interpreter is exactly
 *
 *     ptr += 4
 *     if (ptr[-1] >= 16) stop
 *     n = handler[ptr[-1]](ptr, state)     ; the handler RETURNS n
 *     ptr += 2 * n
 *     if (ptr[0] == 0) loop                ; the next command is due now too
 *
 * THE HANDLER'S RETURN VALUE IS THE OPERAND WORD COUNT, not a control code:
 * op1, op6 and op15 return 0, and the caller does `shl ax,1; add [ptr],ax`.
 * op6 does end the cutscene; it does so by clearing bit 3 of the state's
 * flag byte.
 *
 * FILENAMES ARE OCTAL IN BOTH HALVES. csXXX takes the three octal digits of
 * the cutscene number and .nXX the two of the file number, which is what op8
 * rebuilds with `(v >> 6) & 7`, `(v >> 3) & 7`, `v & 7`. That is why CS030,
 * which ships only a .n00, plays: its change_file operands name cutscene 0's
 * animation files.
 */
#ifndef UW_CUTS_H
#define UW_CUTS_H

#include "uw.h"

#define UW_CUT_OPCODES 16
#define UW_CUT_OP_DISPLAY_STRING  0
#define UW_CUT_OP_END             6
#define UW_CUT_OP_CHANGE_FILE     8
#define UW_CUT_OP_TEXT_AND_SOUND 13
#define UW_CUT_NO_VOICE         999

/* Operand word count per opcode -- each is that handler's return value. */
extern const int  uw_cut_operands[UW_CUT_OPCODES];
extern const char *uw_cut_names[UW_CUT_OPCODES];

typedef struct {
    size_t   at;
    uint16_t frame, opcode;
    int      operands;
    const uint8_t *operand;   /* `operands` words, little-endian */
} uw_cut_record;

typedef struct {
    uw_blob file;
    int     number;           /* from the filename, read as OCTAL */
    int     count;            /* records parsed */
    bool    tiles;            /* the records account for every byte */
    bool    ends;             /* the last record is op6 */
} uw_cut_script;

/* `path` is a full csXXX.n00 path; `number` is the cutscene number the
 * caller decoded from the name. */
bool uw_cut_open(uw_cut_script *s, const char *path, int number);
void uw_cut_close(uw_cut_script *s);
bool uw_cut_record_at(const uw_cut_script *s, int i, uw_cut_record *out);
uint16_t uw_cut_operand(const uw_cut_record *r, int i);

/* Builds "CS%03o.N%02o" -- both halves octal. */
void uw_cut_filename(int cutscene, int file, char *out, size_t cap);

/* ---- the animation files (.n01 and up) ---------------------------------
 *
 * An `LPF ` file: a 0xb00-byte header, then one 64 KB slot per page, each
 * page holding a frame-size table and its frames. `cutscene_read_header`
 * reads the header whole, `cutscene_read_frame` a
 * page, `cutscene_read_frame_chunk` a piece of one.
 *
 * THE PALETTE at +0x100 is 256 entries of FOUR bytes, B G R pad --
 * `palette_to_dac` steps its source by 4 and its destination by
 * 3, taking +2 to red and +0 to blue, each `>> 2` into the DAC's six bits.
 * Read as a 768-byte RGB table it decodes a frame into the right picture in
 * the wrong colours.
 */
#define UW_ANIM_HEADER  0xb00
#define UW_ANIM_PAGE    0x10000
#define UW_ANIM_SCREEN  (320 * 200)

typedef struct {
    uw_blob  file;
    int      page_count;
    uint8_t  last_page_short;   /* +0x1a: the last page plays one frame fewer */
    uint16_t frame_rate;        /* +0x44: frames a second */
} uw_anim;

typedef struct {
    uint16_t order;             /* pages play in ascending order of this */
    uint16_t frame_count;
    uint16_t frame_bytes;
} uw_anim_page;

bool uw_anim_open(uw_anim *a, const char *path);
void uw_anim_close(uw_anim *a);
bool uw_anim_page_at(const uw_anim *a, int page, uw_anim_page *out);
/* The page's frame_size table (its +8), `frame_count` words. */
const uint8_t *uw_anim_sizes(const uw_anim *a, int page);
/* Frame `i` of the page: its bytes, and its size through `len`. */
const uint8_t *uw_anim_frame(const uw_anim *a, int page, int i, size_t *len);
/* 768 bytes of R, G, B at the DAC's six-bit scale. */
void uw_anim_palette(const uw_anim *a, uint8_t out[768]);
/* Where the last page's frames end. A file ends there or is padded up to
 * the end of that page's 64 KB slot -- four UW2 files are. */
size_t uw_anim_end(const uw_anim *a);
size_t uw_anim_slot_end(const uw_anim *a);
/* Whether the page's record counts its own head and size table in
 * frame_bytes: four UW2 pages do and no UW1 page does. */
bool uw_anim_frame_bytes_inclusive(const uw_anim *a, int page);

/* (kind, body offset) for a frame. `kind` is 0 raw, 1 RLE, -1 when the
 * frame carries no body at all -- `skip` is 2 without the extra block and
 * ((extra_len + 1) & ~1) + 4 with it. */
int uw_anim_frame_body(const uint8_t *frame, size_t len, size_t *skip);

/* `rle_expand`. A code byte selects one of four forms and they move the
 * DESTINATION pointer, which is what makes a delta frame possible:
 *
 *     0x01..0x7f  copy that many bytes literally
 *     0x81..0xff  SKIP (code & 0x7f) bytes, leaving what is under them
 *     0x00        a run: a count byte, then the byte to repeat
 *     0x80        a word W: 0 ends the stream, W > 0 skips W bytes,
 *                 W & 0x7fff below 0x4000 is a long literal and otherwise
 *                 a run of (W & 0x3fff)
 *
 * Returns the source bytes consumed, or 0 when the stream runs off either
 * buffer; `*end` takes where the destination pointer finished, which is
 * what the original returns in DI. */
size_t uw_rle_expand(const uint8_t *src, size_t len, size_t at,
                     uint8_t *dst, size_t cap, size_t *end);

#endif
