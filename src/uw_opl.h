/* SPDX-License-Identifier: MIT */
/* An OPL2 (Yamaha YM3812), the chip behind the AdLib: the registers the
 * driver writes, and samples out at the chip's own rate.
 *
 * Written for this port from the chip's behaviour as the die analyses
 * describe it (the OPLx decapsulation of the YM3812 and YMF262) rather than
 * taken from an emulator. The operator: a quarter-wave log-sine ROM and an
 * exponent ROM -- computed here, and equal to the chip's -- the four
 * waveforms of register 0xE0, the negative half formed by inverting the
 * bits; the phase a 19-bit accumulator, vibrato on the F-number; feedback
 * from the modulator's last two outputs; the two connections. The envelope:
 * every operator steps every sample, keyed or not, silent or not; a key-on
 * is a step at the attack's rate that zeroes the phase and goes to the
 * attack; the attack moves by the envelope's own complement shifted, the
 * others by powers of two; the steps come from a timer running at half the
 * sample rate, the trailing zeros of its count against the rate; the
 * attenuation an operator sounds is the one latched before its envelope
 * moves; an envelope within eight of silence is silent. Tremolo and vibrato
 * from the sample counter. Melodic mode only: ADLIB.ADV drives the drums as
 * melodic voices with their own timbres (bank 127), and never sets register
 * 0xBD's rhythm bit.
 *
 * Checked sample for sample against Nuked OPL3 in its OPL2 mode, operator
 * by operator. The mix is the nine
 * channels' sum, clipped; the YM3014 DAC's floating-point steps are not
 * modelled.
 *
 * The rate is the chip's: its 3.579545 MHz clock over 72, 49716 Hz. The
 * host resamples. */
#ifndef UW_OPL_H
#define UW_OPL_H

#include <stdint.h>

#define UW_OPL_RATE 49716

typedef struct {
    uint32_t phase;          /* 19 bits: the phase generator */
    int16_t  out, prev;      /* the last two outputs (feedback) */
    int16_t  fbmod;          /* the feedback's phase offset this sample */
    uint16_t env;            /* 0 loud .. 511 silent */
    uint16_t level;          /* the attenuation sounding: latched each step */
    uint8_t  gen;            /* 0 attack, 1 decay, 2 sustain, 3 release */
    uint8_t  key, reset;
    uint8_t  am, vib, egt, ksr, mult;   /* register 0x20 */
    uint8_t  ksl, tl;                   /* 0x40 */
    uint8_t  ar, dr, sl, rr;            /* 0x60, 0x80 */
    uint8_t  ws;                        /* 0xE0 */
} uw_opl_slot;

typedef struct {
    uw_opl_slot slot[2];     /* the modulator and the carrier */
    uint16_t fnum;
    uint8_t  block, key;
    uint8_t  fb, cnt;
} uw_opl_channel;

typedef struct {
    uw_opl_channel ch[9];
    uint8_t  reg[256];
    uint8_t  wave_enable;    /* register 1 bit 5 */
    uint8_t  deep_am, deep_vib, note_sel;
    uint32_t timer;          /* samples: the two LFOs */
    uint64_t eg_timer;       /* the envelope's timer, 36 bits, at half rate */
    uint8_t  eg_half, eg_add, eg_low, eg_carry;
} uw_opl;

void uw_opl_reset(uw_opl *o);
void uw_opl_write(uw_opl *o, uint8_t reg, uint8_t value);
/* n samples, mono, at UW_OPL_RATE, added to nothing: written */
void uw_opl_render(uw_opl *o, int16_t *out, int n);

#endif
