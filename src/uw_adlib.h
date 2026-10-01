/* SPDX-License-Identifier: MIT */
/* ADLIB.ADV's voice layer: what the AIL driver does with a channel voice
 * message and a timbre, as register writes to an OPL2.
 *
 * The other half of the driver, the sequencer and the channel locks, is
 * src/uw_ail.c; this is the half under the message call (0x26ac), read
 * from the driver's instructions:
 *
 *   sixteen virtual voices over the chip's nine channels, a note-on taking
 *     a free voice and then an OPL channel, and every fifth of a second --
 *     and whenever a voice finds none free -- the highest-priority voice
 *     without a channel taking the channel of the lowest-priority voice
 *     with one (0x2333);
 *   a timbre of fourteen bytes is an FM voice (0x2423): the note byte, the
 *     modulator's five, the feedback, the carrier's five -- turned into the
 *     same sixteen-bit parameters the effects use, so one register writer
 *     (0x1f98) serves both;
 *   a longer timbre -- bank 1's, the sound effects' instruments -- is a
 *     time-variant effect (0x893): eight parameters (the frequency, the two
 *     levels, the priority, the feedback, the two multipliers, the
 *     waveforms), each with a command stream in the timbre that sets it,
 *     ramps it for a count, jumps, or sets a register's bits, stepped at 60
 *     Hz (0x60c), with a second set of streams for the release;
 *   the pitch from the note, the patch's transposition and a bend of
 *     twelve semitones, in sixteenths of a semitone through the F-number
 *     table; the levels scaled by the channel's volume and expression and
 *     the note's velocity (the velocity table); the modulation wheel's
 *     vibrato; controller 114 the timbre bank a program change looks in.
 *
 * The tables -- the F-numbers, the blocks, the velocity scale, the
 * operator slots and the chip's initial registers -- are read from the
 * driver file itself (uw_adlib_init). */
#ifndef UW_ADLIB_H
#define UW_ADLIB_H

#include <stddef.h>
#include <stdint.h>

#define UW_ADLIB_VOICES   16
#define UW_ADLIB_TIMBRES 192

typedef void (*uw_adlib_write_fn)(void *user, uint8_t reg, uint8_t value);

typedef struct {
    uint8_t  state;          /* 0x163e: 0 free, 1 on, 2 an effect released */
    uint8_t  effect;         /* 0x164e: 0 an FM voice; an effect's type */
    uint8_t  opl;            /* 0x165e: the chip channel, 0xff none */
    uint8_t  channel;        /* 0x166e */
    uint8_t  note;           /* 0x167e: the note played */
    uint8_t  key;            /* 0x168e: the note-on's own note, for its note-off */
    int8_t   transpose;      /* 0x169e: the patch's note byte */
    uint8_t  velocity;       /* 0x16ae: the velocity table's scale */
    uint8_t  held;           /* 0x16be: released under the sustain pedal */
    uint8_t  dirty;          /* 0x16ce: which register groups to write */
    uint8_t  b0;             /* 0x16de: the last 0xB0 value */
    uint8_t  keybits;        /* 0x16ee: key-on (0x20) and an effect's block */
    uint8_t  connection;     /* 0x16fe */
    uint8_t  ksl[2];         /* 0x170e, 0x171e */
    uint8_t  flags[2];       /* 0x172e, 0x173e: AM, vibrato, EG type, KSR */
    uint8_t  ad[2], sr[2];   /* 0x174e/0x175e, 0x176e/0x177e */
    const uint8_t *timbre;   /* 0x15de */
    size_t   timbre_len;
    uint16_t duration;       /* 0x161e: an effect's ticks before its release */
    /* the eight parameters: value, increment, counter, stream offset
     * (0x135.., 0x155.., 0x115.., 0xf5..) */
    uint16_t value[8], inc[8], count[8], stream[8];
    uint16_t rank;           /* 0x18d7: the priority less the channel's voices */
} uw_adlib_voice;

typedef struct {
    /* the driver file's tables */
    uint16_t fnum[192]; /* twelve rows of sixteen */
    uint8_t  block[96];
    uint8_t  row[96];
    uint8_t  init[256]; /* the chip's registers at reset */
    uint8_t  vel[16]; /* the velocity scale */
    uint8_t  slot_m[9], slot_c[9];
    uint8_t  slot_reg[18];
    int      tables_ok;
    /* the timbre cache (0x1391 bank, 0x1451 program, 0x1511 used) */
    struct { uint8_t used, bank, program; const uint8_t *data; size_t len; } timbre[UW_ADLIB_TIMBRES];
    /* per MIDI channel */
    uint8_t  modulation[16], volume[16], pan[16], expression[16], sustain[16], protect[16];
    uint8_t  timbre_of[16], bank[16], program[16], bend_lsb[16], bend_msb[16];
    uint8_t  drum[128]; /* the percussion timbre by note */
    uint8_t  chip_voices[16]; /* chip channels a MIDI channel holds */
    uint8_t  chip_owner[9]; /* the MIDI channel on a chip channel */
    uw_adlib_voice v[UW_ADLIB_VOICES];
    uint16_t effect_clock, rank_clock;
    uint8_t  level_flip; /* 0x40 every other effect step */
    uint8_t  shadow[256]; /* the chip's registers as last written */
    uw_adlib_write_fn write;
    void    *user;
} uw_adlib;

/* The driver's file (SOUND\ADLIB.ADV) for its tables; the chip's register
 * writes go to `write`. 0 when the file is not the driver. */
int  uw_adlib_init(uw_adlib *a, const uint8_t *driver, size_t len, uw_adlib_write_fn write, void *user);
/* 0x26ac: a channel voice message on a physical channel */
void uw_adlib_message(uw_adlib *a, uint8_t status, uint8_t d1, uint8_t d2);
/* 0x1d94: a timbre into the cache; the bytes must outlive it */
void uw_adlib_timbre(uw_adlib *a, uint8_t bank, uint8_t program, const uint8_t *patch, size_t len);
/* 0x60c, at the end of every timer call: the effects at 60 Hz, the
 * priorities at 5 */
void uw_adlib_service(uw_adlib *a);

#endif
