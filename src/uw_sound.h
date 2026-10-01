/* SPDX-License-Identifier: MIT */
/* The four sound formats, so the port carries every file the audio side
 * will need before any of it can be heard.
 *
 * They divide along a line worth stating: UW1's sound
 * EFFECTS are not samples. Each one is a single MIDI note on a General MIDI
 * patch (SOUNDS.DAT), played through the same music driver the score goes
 * to -- which is why the whole sound engine fits in a few hundred bytes and
 * needs no mixer. The `.voc` files are the cutscene VOICES and nothing else.
 *
 * So a port's audio work is a synthesiser, not a decoder: it has to produce
 * the notes, from the score (.XMI) and from the effect table, with the
 * timbres the bank gives it. These readers are what feeds that -- the driver
 * model (uw_ail.c), its voice layer (uw_adlib.c) and the chip (uw_opl.c).
 */
#ifndef UW_SOUND_H
#define UW_SOUND_H

#include "uw.h"

/* ---- SOUNDS.DAT: the effect table ---------------------------------------
 *
 * `load_sounds_dat` reads ONE byte of count and then that many
 * five-byte records. The duration is BIG-endian: the loader does not read a word, it reads the fourth and
 * fifth bytes and combines them by hand as `byte4 * 256 + byte5`.
 */
#define UW_SOUND_STRIDE 5

typedef struct {
    uint8_t  program;      /* MIDI program */
    uint8_t  note;         /* MIDI note */
    uint8_t  velocity;     /* before the caller's adjustment */
    uint16_t duration;     /* 1/256 s, big-endian in the file */
} uw_sound_effect;

typedef struct uw_sounds {
    uw_blob file;
    int     count;
} uw_sounds;

bool uw_sounds_open(uw_sounds *s, const char *path);
void uw_sounds_close(uw_sounds *s);
bool uw_sound_effect_at(const uw_sounds *s, int i, uw_sound_effect *out);

/* ---- UW.AD / UW.MT / UW.OPL: the timbre bank ---------------------------
 *
 * `snd_bank_find_patch` walks six-byte directory entries --
 * { byte program, byte bank, dword offset } -- to a terminator whose bank is
 * 0xff, then seeks to the offset and reads a WORD length that INCLUDES
 * itself.
 */
typedef struct uw_bank {
    uw_blob file;
    int     count;         /* directory entries before the terminator */
} uw_bank;

bool uw_bank_open(uw_bank *b, const char *path);
void uw_bank_close(uw_bank *b);
bool uw_bank_entry_at(const uw_bank *b, int i, uint8_t *program, uint8_t *bank);
/* The patch for (program, bank): its bytes and length through `len`, or NULL
 * when the directory has no such pair. The length word is part of it. */
const uint8_t *uw_bank_patch(const uw_bank *b, uint8_t program, uint8_t bank, size_t *len);

/* ---- SOUND/NN.VOC: the cutscene voices ---------------------------------
 *
 * Creative's format, which UW does not parse: `voc_play_file`
 * stages the file in EMS and hands raw bytes to the digital driver. What is
 * engine knowledge is the four things the player assumes without checking,
 * and all 42 of UW1's files satisfy all four:
 * samples at byte 32, one type-1 block, pack byte 0, under 96 KB.
 */
#define UW_VOC_SAMPLES_AT 32
#define UW_VOC_STAGE      (6 * 16 * 1024)

typedef struct {
    uw_blob  file;
    uint16_t data_offset;  /* 26 in every shipped file */
    uint16_t version;      /* 0x010a */
    int      blocks;       /* blocks before the type-0 terminator */
    int      sound_blocks; /* of them, type 1 */
    size_t   samples_at;   /* where the first type-1 block's samples start */
    size_t   samples;      /* how many */
    uint8_t  time_constant;
    uint8_t  pack;         /* 0 = 8-bit PCM */
} uw_voc;

bool uw_voc_open(uw_voc *v, const char *path);
void uw_voc_close(uw_voc *v);
/* 1000000 / (256 - time_constant), the rate the driver plays it at. */
long uw_voc_rate(const uw_voc *v);

/* ---- SOUND .XMI files: the score --------------------------------------------
 *
 * IFF with big-endian lengths: FORM XDIR with an INFO count, then a
 * CAT XMID of one FORM XMID per sequence, each with a TIMB list of
 * (program, bank) pairs and an EVNT stream.
 *
 * A TICK IS 1/120 SECOND, absolute: every music driver carries 120 in the
 * last word of its description block, and the sequencer decrements a delay
 * once per timer call at tempo 100. The `ff 51` tempo metas set only the
 * beat threshold that feeds the bar counters, so there is no tempo map to
 * apply -- which is what XMIDI is for.
 */
#define UW_XMI_TICK_HZ 120

typedef struct {
    uw_blob  file;
    size_t   timb_at;      /* the TIMB pairs */
    int      timbres;
    size_t   evnt_at, evnt_end;
    int      sequences;
    int      declared;     /* INFO's count */
} uw_xmi;

bool uw_xmi_open(uw_xmi *x, const char *path);
void uw_xmi_close(uw_xmi *x);
bool uw_xmi_timbre_at(const uw_xmi *x, int i, uint8_t *program, uint8_t *bank);

/* What a walk of the event stream counts. `ticks` is the delays summed --
 * the track's length in 120ths of a second. */
typedef struct {
    long notes, intervals, controllers, programs, metas, sysex, others;
    long ticks;
    size_t stop;           /* where the stream ended */
    bool   ended;          /* on an end-of-track meta */
} uw_xmi_events;

/* Walks the stream. False when a status byte is not one of the nine forms
 * or the stream runs off the chunk -- which is the check: a note-on's
 * duration is a variable-length quantity with no note-off to match it, so a
 * walk that reaches the chunk's last byte could not have missed one. */
bool uw_xmi_walk(const uw_xmi *x, uw_xmi_events *out);

#endif
