/* SPDX-License-Identifier: MIT */
/* The Miles AIL 2 XMIDI driver the game's music and sound effects go
 * through, as ADLIB.ADV implements it: the sequencer, the channel locks and
 * the timbre cache -- everything between the engine's ail_* calls and the
 * synthesiser.
 *
 * The engine never looks inside a score: it registers
 * the file with the driver, feeds it the timbres the driver asks for, starts
 * it and polls its status. What it asks of the driver that shows in its own
 * state is small but exact -- which channel ail_lock_channel hands a sound
 * effect (snd_channel_alloc_mask, sfx_slot_channel) and when a track has
 * run out (ail_sequence_status, music_track_finished) -- and both depend on
 * the sequencer: the lock takes the channel with the fewest of the score's
 * notes sounding, and the end is the score's own end-of-track meta. So the
 * sequencer is carried as the driver runs it, from ADLIB.ADV's instructions:
 *
 *   the timer service (0x33b5..0x357d), 120 calls a second: the tempo
 *     percent into an accumulator, one sub-step per 100 of it; each
 *     sub-step counts every sounding note down (a note of duration d sounds
 *     d sub-steps) and then the delay, and at the delay's end reads events
 *     up to the next interval byte, whose value is the new delay;
 *   the note-on (0x3131): the duration stored less one in one of 32 slots,
 *     the physical channel's note count up, the note on -- skipped whole on
 *     a locked channel; the note-off at the count's end, the count down;
 *   the controllers (0x2f5b): 7 scaled by the sequence volume, 116/117 the
 *     four-deep FOR/NEXT loop stack (a count of 0 loops for ever), 111 the
 *     score's lock protect, 110 a lock of the score's own, 115 the indirect
 *     prefix, the rest cached and passed on unless the channel is locked;
 *   meta 0x2f (0x3246): the sequence's notes off, its channels restored,
 *     status 2;
 *   start (service 0xaa) resets the sequence and plays it from the top;
 *     stop (0xab) sends its note-offs, status 0; set_sequence_volume (0xb1)
 *     sets or fades the volume percent, a step every ms * 10 / |delta|
 *     tenths of a millisecond at 83 of them a timer call;
 *   lock_channel (0xbf, 0x3e2d): physical channels 8 down to 1, not locked
 *     and not protected, the fewest score notes sounding (a tie to the
 *     higher channel), again ignoring protection when none; sustain off and
 *     its notes off, locked, returned 1-based; release_channel (0xc1).
 *
 * What the synthesiser does with the messages -- ADLIB.ADV's voice layer
 * and the OPL2 behind it -- is the `synth` callback's: src/uw_adlib.c and
 * src/uw_opl.c, as src/tools/uwshell.c wires them.
 */
#ifndef UW_AIL_H
#define UW_AIL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define UW_AIL_CHANNELS  16
#define UW_AIL_NOTES     32
#define UW_AIL_LOOPS      4
#define UW_AIL_SEQUENCES  2
#define UW_AIL_TICK_HZ  120   /* the timer service's rate: the drivers' description blocks */

typedef struct {
    void *user;
    /* a channel voice message on a physical channel (status | channel) */
    void (*message)(void *user, uint8_t status, uint8_t d1, uint8_t d2);
    /* a timbre installed in the driver's cache: the bank's patch bytes */
    void (*timbre)(void *user, uint8_t bank, uint8_t program, const uint8_t *patch, size_t len);
    /* the end of every timer call (0x3412's call to 0x60c): the voices'
     * own service -- the effects stepped, the priorities looked at */
    void (*tick)(void *user);
} uw_ail_synth;

typedef struct {
    bool     used;
    int      status;                     /* 0 stopped, 1 playing, 2 done: ail_sequence_status */
    bool     started;
    const uint8_t *timb;                 /* the TIMB pairs */
    int      timbres;
    const uint8_t *evnt;                 /* the EVNT stream */
    size_t   evnt_len, pos;
    int32_t  delay;                      /* +0x1e */
    int      volume, volume_target;      /* +0x24, +0x26: percent */
    int32_t  volume_acc, volume_period;  /* +0x28, +0x2c: tenths of a millisecond */
    int      tempo_acc, tempo, tempo_target;   /* +0x30, +0x32, +0x34 */
    int32_t  tempo_time, tempo_period;   /* +0x36, +0x3a: tenths of a millisecond */
    uint8_t  map[UW_AIL_CHANNELS];       /* +0x68: logical to physical */
    uint8_t  program[UW_AIL_CHANNELS];   /* +0x78 */
    uint8_t  bend_lsb[UW_AIL_CHANNELS], bend_msb[UW_AIL_CHANNELS];
    uint8_t  indirect[UW_AIL_CHANNELS];  /* +0xa8 */
    uint8_t  volume_cache[UW_AIL_CHANNELS];    /* +0xb8: controller 7 as the score set it */
    uint8_t  sustain[UW_AIL_CHANNELS];   /* +0xf8: controller 64 */
    uint8_t  locked[UW_AIL_CHANNELS];    /* +0x118: controller 110, a lock of the score's own */
    uint8_t  protect[UW_AIL_CHANNELS];   /* +0x128: controller 111, lock protect */
    uint8_t  voice_protect[UW_AIL_CHANNELS];   /* +0x138: controller 112 */
    struct { uint8_t channel, note; int32_t left; } note[UW_AIL_NOTES];   /* +0x148.. */
    int      active;                     /* +0x20 */
    struct { int16_t count; size_t at; } loop[UW_AIL_LOOPS];            /* +0x60, +0x50 */
} uw_ail_sequence;

typedef struct uw_ail {
    uint8_t  flags[UW_AIL_CHANNELS];     /* 0x80 locked, 0x40 protected */
    uint8_t  notes[UW_AIL_CHANNELS];     /* the sequences' notes sounding */
    uint8_t  installed[256][128 / 8];    /* the timbre cache, (bank, program) */
    /* every channel as the sequences last set it, by logical channel: the
     * nine cached controllers, the program and the
     * bend -- what release_channel sends again */
    uint8_t  cache[9][UW_AIL_CHANNELS];
    uint8_t  cache_program[UW_AIL_CHANNELS], cache_bend_lsb[UW_AIL_CHANNELS], cache_bend_msb[UW_AIL_CHANNELS];
    uw_ail_sequence seq[UW_AIL_SEQUENCES];
    uw_ail_synth synth;
    long     ticks;                      /* timer calls served */
} uw_ail;

/* The driver installed (service 0x66, 0x3615): its state cleared, and
 * physical channels 1..9 given the nine cached controllers' defaults --
 * volume and expression 0x7f, pan 0x40, the rest 0 -- and a centred bend,
 * sent to the synthesiser and kept as what release_channel restores. */
void uw_ail_init(uw_ail *a, const uw_ail_synth *synth);

/* ail_register_sequence (0x97): sequence `index` of an XMI file, whose
 * bytes must outlive it; a handle, or -1 when the file is not one. */
int  uw_ail_register_sequence(uw_ail *a, const uint8_t *xmi, size_t len, int index);
void uw_ail_release_sequence(uw_ail *a, int handle);
/* ail_timbre_request (0x9b): the next of the sequence's timbres the cache
 * lacks, bank << 8 | program, or 0xffff when it has them all. */
int  uw_ail_timbre_request(uw_ail *a, int handle);
bool uw_ail_timbre_installed(const uw_ail *a, int bank, int program);
void uw_ail_install_timbre(uw_ail *a, int bank, int program, const uint8_t *patch, size_t len);

void uw_ail_start_sequence(uw_ail *a, int handle);
void uw_ail_stop_sequence(uw_ail *a, int handle);
int  uw_ail_sequence_status(const uw_ail *a, int handle);
void uw_ail_set_sequence_volume(uw_ail *a, int handle, int volume, int ms);
void uw_ail_set_sequence_tempo(uw_ail *a, int handle, int tempo, int ms);

/* ail_send_channel_voice_message (0xba), ail_lock_channel (0xbf) -> 1..16
 * or 0, ail_release_channel (0xc1) of a 1-based channel. */
void uw_ail_send_voice(uw_ail *a, uint8_t status, uint8_t d1, uint8_t d2);
int  uw_ail_lock_channel(uw_ail *a);
void uw_ail_release_channel(uw_ail *a, int channel);

/* The driver's timer service, UW_AIL_TICK_HZ times a second. */
void uw_ail_tick(uw_ail *a);

#endif
