/* SPDX-License-Identifier: MIT */
/* An OPL2, from the chip's behaviour as the die analyses describe it: see
 * uw_opl.h. */
#include "uw_opl.h"
#include <math.h>
#include <string.h>

static uint16_t logsin[256];     /* -log2(sin) of a quarter wave, 8.8 fixed */
static uint16_t exprom[256];     /* 2^(1 - i/256) * 1024 */
static int tables_made;

static void make_tables(void) {
    int i;
    if (tables_made) return;
    for (i = 0; i < 256; i++) {
        double s = sin((i + 0.5) * 3.14159265358979323846 / 512.0);
        logsin[i] = (uint16_t)lround(-log2(s) * 256.0);
        exprom[i] = (uint16_t)lround(pow(2.0, (255 - i) / 256.0) * 1024.0);
    }
    tables_made = 1;
}

/* the multiplier, doubled: 1/2, 1, 2 .. 10, 10, 12, 12, 15, 15 */
static const uint8_t mult_x2[16] = { 1, 2, 4, 6, 8, 10, 12, 14, 16, 18, 20, 20, 24, 24, 30, 30 };
/* the key-scale level against the F-number's top four bits */
static const uint8_t ksl_rom[16] = { 0, 32, 40, 45, 48, 51, 53, 55, 56, 58, 59, 60, 61, 62, 63, 64 };
static const uint8_t ksl_shift[4] = { 8, 1, 2, 0 };
/* the rates 12..15's extra steps, by the rate's low two bits and the
 * envelope timer's */
static const uint8_t eg_step[4][4] = { { 0, 0, 0, 0 }, { 1, 0, 0, 0 }, { 1, 0, 1, 0 }, { 1, 1, 1, 0 } };

enum { EG_ATTACK, EG_DECAY, EG_SUSTAIN, EG_RELEASE };

void uw_opl_reset(uw_opl *o) {
    int c, s;
    make_tables();
    memset(o, 0, sizeof *o);
    for (c = 0; c < 9; c++)
        for (s = 0; s < 2; s++) {
            o->ch[c].slot[s].env = 511;
            o->ch[c].slot[s].level = 511;
            o->ch[c].slot[s].gen = EG_RELEASE;
        }
}

/* the operator a register's low five bits name, as (channel, slot): offsets
 * 0..5, 8..13 and 16..21 in threes of modulators and carriers */
static uw_opl_slot *slot_of(uw_opl *o, uint8_t off, uw_opl_channel **ch) {
    static const int8_t chan[32] = { 0, 1, 2, 0, 1, 2, -1, -1, 3, 4, 5, 3, 4, 5, -1, -1,
                                     6, 7, 8, 6, 7, 8, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 };
    int c = chan[off & 0x1f], s = ((off & 0x1f) % 8) >= 3;
    if (c < 0) return NULL;
    if (ch) *ch = &o->ch[c];
    return &o->ch[c].slot[s];
}

void uw_opl_write(uw_opl *o, uint8_t reg, uint8_t v) {
    uw_opl_channel *ch;
    uw_opl_slot *s;
    o->reg[reg] = v;
    switch (reg & 0xe0) {
    case 0x00:
        if (reg == 0x01) o->wave_enable = (v >> 5) & 1;
        else if (reg == 0x08) o->note_sel = (v >> 6) & 1;
        return;
    case 0x20:
        if (!(s = slot_of(o, reg, NULL))) return;
        s->am = v >> 7; s->vib = (v >> 6) & 1; s->egt = (v >> 5) & 1; s->ksr = (v >> 4) & 1; s->mult = v & 0xf;
        return;
    case 0x40:
        if (!(s = slot_of(o, reg, NULL))) return;
        s->ksl = v >> 6; s->tl = v & 0x3f;
        return;
    case 0x60:
        if (!(s = slot_of(o, reg, NULL))) return;
        s->ar = v >> 4; s->dr = v & 0xf;
        return;
    case 0x80:
        if (!(s = slot_of(o, reg, NULL))) return;
        s->sl = v >> 4; s->rr = v & 0xf;
        return;
    case 0xe0:
        if (!(s = slot_of(o, reg, NULL))) return;
        s->ws = v & 3;
        return;
    default: break;
    }
    if (reg >= 0xa0 && reg <= 0xa8) {
        ch = &o->ch[reg - 0xa0];
        ch->fnum = (uint16_t)((ch->fnum & 0x300) | v);
    } else if (reg >= 0xb0 && reg <= 0xb8) {
        int key;
        ch = &o->ch[reg - 0xb0];
        ch->fnum = (uint16_t)((ch->fnum & 0xff) | ((v & 3) << 8));
        ch->block = (v >> 2) & 7;
        key = (v >> 5) & 1;
        /* the envelope takes the key up at its next step */
        ch->key = ch->slot[0].key = ch->slot[1].key = (uint8_t)key;
    } else if (reg == 0xbd) {
        o->deep_am = v >> 7;
        o->deep_vib = (v >> 6) & 1;
    } else if (reg >= 0xc0 && reg <= 0xc8) {
        ch = &o->ch[reg - 0xc0];
        ch->fb = (v >> 1) & 7;
        ch->cnt = v & 1;
    }
}

/* The envelope one sample on, and the attenuation the operator sounds
 * this sample: the one before the step. A key held in release is the key
 * going on: a step at the attack's rate that zeroes the phase (and, at rate
 * 15, the envelope), then the attack; a key up is the release. */
static void envelope(uw_opl *o, uw_opl_channel *ch, uw_opl_slot *s, int trem) {
    int rate, ks, r, hi, lo, shift = 0, inc = 0, off, env = s->env, att;
    int ksl = (ksl_rom[ch->fnum >> 6] << 2) - ((8 - ch->block) << 5);
    if (ksl < 0) ksl = 0;
    att = env + (s->tl << 2) + (ksl >> ksl_shift[s->ksl]) + (s->am ? trem : 0);
    s->level = (uint16_t)(att > 511 ? 511 : att);
    s->reset = s->key && s->gen == EG_RELEASE;
    switch (s->reset ? EG_ATTACK : s->gen) {
    case EG_ATTACK:  rate = s->ar; break;
    case EG_DECAY:   rate = s->dr; break;
    case EG_SUSTAIN: rate = s->egt ? 0 : s->rr; break;
    default:         rate = s->rr; break;
    }
    ks = (ch->block << 1) | ((ch->fnum >> (o->note_sel ? 8 : 9)) & 1);
    if (!s->ksr) ks >>= 2;
    r = ks + (rate << 2);
    hi = r >> 2;
    if (hi & 0x10) hi = 15;
    lo = r & 3;
    if (rate) {
        if (hi < 12) {
            if (o->eg_half) {
                switch (hi + o->eg_add) {
                case 12: shift = 1; break;
                case 13: shift = (lo >> 1) & 1; break;
                case 14: shift = lo & 1; break;
                default: break;
                }
            }
        } else {
            shift = (hi & 3) + eg_step[lo][o->eg_low];
            if (shift & 4) shift = 3;
            if (!shift) shift = o->eg_half;
        }
    }
    if (s->reset && hi == 15) env = 0;             /* the instant attack */
    off = (s->env & 0x1f8) == 0x1f8;
    if (s->gen != EG_ATTACK && !s->reset && off) env = 511;
    switch (s->gen) {
    case EG_ATTACK:
        if (!s->env) s->gen = EG_DECAY;
        else if (s->key && shift > 0 && hi != 15) inc = ~(int)s->env >> (4 - shift);
        break;
    case EG_DECAY:
        if ((s->env >> 4) == (s->sl == 15 ? 31 : s->sl)) s->gen = EG_SUSTAIN;
        else if (!off && !s->reset && shift > 0) inc = 1 << (shift - 1);
        break;
    default:
        if (!off && !s->reset && shift > 0) inc = 1 << (shift - 1);
        break;
    }
    s->env = (uint16_t)((env + inc) & 0x1ff);
    if (s->reset) s->gen = EG_ATTACK;
    if (!s->key) s->gen = EG_RELEASE;
}

/* The phase one sample on -- zeroed on the key's step -- and the
 * operator's output at the phase before it, offset by `mod` */
static int16_t operator_out(uw_opl *o, uw_opl_channel *ch, uw_opl_slot *s, int mod) {
    uint16_t phase = (uint16_t)(s->phase >> 9), fnum = ch->fnum;
    int level, neg = 0, out;
    if (s->vib) {
        int range = (fnum >> 7) & 7, pos = (int)((o->timer >> 10) & 7);
        if (!(pos & 3)) range = 0;
        else if (pos & 1) range >>= 1;
        if (!o->deep_vib) range >>= 1;
        if (pos & 4) range = -range;
        fnum = (uint16_t)(fnum + range);
    }
    if (s->reset) s->phase = 0;
    s->phase = (s->phase + (((uint32_t)((fnum << ch->block) >> 1) * mult_x2[s->mult]) >> 1)) & 0x7ffff;
    phase = (uint16_t)((phase + mod) & 0x3ff);
    switch (o->wave_enable ? s->ws : 0) {
    case 0: level = logsin[(phase & 0x100) ? (~phase & 0xff) : (phase & 0xff)]; neg = phase & 0x200; break;
    case 1: if (phase & 0x200) return 0; level = logsin[(phase & 0x100) ? (~phase & 0xff) : (phase & 0xff)]; break;
    case 2: level = logsin[(phase & 0x100) ? (~phase & 0xff) : (phase & 0xff)]; break;
    default: if (phase & 0x100) return 0; level = logsin[phase & 0xff]; break;
    }
    level += s->level << 3;
    if (level > 0x1fff) level = 0x1fff;
    out = (exprom[level & 0xff] << 1) >> (level >> 8);
    /* the negative half by inverting the bits, as the chip forms it: a
     * silent sample there is -1 */
    return (int16_t)(neg ? ~out : out);
}

void uw_opl_render(uw_opl *o, int16_t *buf, int n) {
    int i, c;
    for (i = 0; i < n; i++) {
        int32_t sum = 0;
        int trem, pos = (int)((o->timer >> 6) % 210);
        trem = (pos < 105 ? pos : 210 - pos) >> (o->deep_am ? 2 : 4);
        for (c = 0; c < 9; c++) {
            uw_opl_channel *ch = &o->ch[c];
            uw_opl_slot *m = &ch->slot[0], *k = &ch->slot[1];
            m->fbmod = (int16_t)(ch->fb ? (m->prev + m->out) >> (9 - ch->fb) : 0);
            m->prev = m->out;
            envelope(o, ch, m, trem);
            m->out = operator_out(o, ch, m, m->fbmod);
            k->prev = k->out;
            envelope(o, ch, k, trem);
            k->out = operator_out(o, ch, k, ch->cnt ? 0 : m->out);
            sum += ch->cnt ? m->out + k->out : k->out;
        }
        if (sum > 32767) sum = 32767;
        else if (sum < -32768) sum = -32768;
        buf[i] = (int16_t)sum;
        /* the timers: the envelope's at half the rate, its trailing zeros
         * read on the half it moves */
        if (o->eg_half) {
            uint32_t low = (uint32_t)(o->eg_timer & 0x1fff);
            int z = 0;
            if (low) { while (!((low >> z) & 1)) z++; o->eg_add = (uint8_t)(z + 1); }
            else o->eg_add = 0;
            o->eg_low = (uint8_t)(o->eg_timer & 3);
        }
        if (o->eg_carry || o->eg_half) {
            if (o->eg_timer == 0xfffffffffull) { o->eg_timer = 0; o->eg_carry = 1; }
            else { o->eg_timer++; o->eg_carry = 0; }
        }
        o->eg_half ^= 1;
        o->timer++;
    }
}
