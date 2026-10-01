/* SPDX-License-Identifier: MIT */
/* ADLIB.ADV's voice layer: see uw_adlib.h. The routine each function is
 * read from is named at it; the voice fields carry the driver's own table
 * offsets in uw_adlib.h. */
#include "uw_adlib.h"
#include <string.h>

/* the eight parameters, in the driver's order */
enum { P_FREQ, P_MOD_LEVEL, P_CAR_LEVEL, P_PRIORITY, P_FEEDBACK, P_MOD_MULT, P_CAR_MULT, P_WAVES };

static uint16_t w16(const uint8_t *p, size_t at, size_t len) {
    return at + 2 <= len ? (uint16_t)(p[at] | p[at + 1] << 8) : 0;
}

/* 0x1925 and 0x190e: a register written, and kept in the shadow; an
 * operator's register by its slot */
static void reg(uw_adlib *a, uint8_t r, uint8_t v) {
    a->shadow[r] = v;
    if (a->write) a->write(a->user, r, v);
}

static void slot_reg(uw_adlib *a, uint8_t slot, uint8_t base, uint8_t v) {
    reg(a, (uint8_t)(a->slot_reg[slot] + base), v);
}

int uw_adlib_init(uw_adlib *a, const uint8_t *d, size_t len, uw_adlib_write_fn write, void *user) {
    int i;
    memset(a, 0, sizeof *a);
    a->write = write;
    a->user = user;
    if (!d || len < 0xe09) return 0;
    for (i = 0; i < 192; i++) a->fnum[i] = (uint16_t)(d[0xaa0 + i * 2] | d[0xaa1 + i * 2] << 8);
    memcpy(a->block, d + 0xc20, 96);
    memcpy(a->row, d + 0xc80, 96);
    memcpy(a->init + 1, d + 0xce0, 0xf5);
    memcpy(a->vel, d + 0xdd5, 16);
    memcpy(a->slot_m, d + 0xde5, 9);
    memcpy(a->slot_c, d + 0xdee, 9);
    memcpy(a->slot_reg, d + 0xdf7, 18);
    for (i = 0; i < 9; i++) if (a->slot_m[i] >= 18 || a->slot_c[i] >= 18) return 0;
    a->tables_ok = 1;
    /* 0x1acc: the chip's registers 1..0xf5 from the image */
    for (i = 1; i <= 0xf5; i++) reg(a, (uint8_t)i, a->init[i]);
    /* 0x1e77: the cache empty, no channel with a timbre, every voice and
     * chip channel free */
    for (i = 0; i < 16; i++) {
        a->timbre_of[i] = 0xff;
        a->program[i] = 0xff;
    }
    for (i = 0; i < UW_ADLIB_VOICES; i++) a->v[i].opl = 0xff;
    for (i = 0; i < 9; i++) a->chip_owner[i] = 0xff;
    memset(a->drum, 0xff, sizeof a->drum);
    return 1;
}

/* 0x1aef: the cache entry for (bank, program), or 0xff */
static uint8_t lookup(uw_adlib *a, uint8_t bank, uint8_t program) {
    int i;
    for (i = 0; i < UW_ADLIB_TIMBRES; i++)
        if (a->timbre[i].used && a->timbre[i].bank == bank && a->timbre[i].program == program) return (uint8_t)i;
    return 0xff;
}

void uw_adlib_timbre(uw_adlib *a, uint8_t bank, uint8_t program, const uint8_t *patch, size_t len) {
    uint8_t t = lookup(a, bank, program);
    int c;
    if (t == 0xff) {
        /* a free entry -- the cache here is as large as the host's memory,
         * where the driver's evicts the least recently used (0x1c83) */
        for (t = 0; t < UW_ADLIB_TIMBRES && a->timbre[t].used; t++) { }
        if (t == UW_ADLIB_TIMBRES) t = 0;
        a->timbre[t].used = 1;
        a->timbre[t].bank = bank;
        a->timbre[t].program = program;
        a->timbre[t].data = patch;
        a->timbre[t].len = len;
    }
    for (c = 0; c < 16; c++)
        if (a->program[c] == program && a->bank[c] == bank) a->timbre_of[c] = t;
}

/* ---- the registers ------------------------------------------------------ */

/* 0x207e's scale: the channel's volume and expression, then the note's
 * velocity, each (a * b * 2) >> 8 and one up unless zero */
static uint8_t level_scale(uw_adlib *a, uw_adlib_voice *v) {
    unsigned t = (unsigned)(a->volume[v->channel & 0xf] * a->expression[v->channel & 0xf]) * 2u >> 8;
    t = t ? t + 1 : 0;
    t = (t & 0xff) * v->velocity * 2u >> 8;
    return (uint8_t)(t ? t + 1 : 0);
}

/* 0x2235..0x22e7: the note, the patch's transposition and the bend -- in
 * sixteenths of a semitone -- to an F-number and a block */
static uint16_t pitch(uw_adlib *a, uw_adlib_voice *v) {
    int ch = v->channel & 0xf, n, semi, blk;
    int16_t bend, x, f;
    bend = (int16_t)((((a->bend_msb[ch] & 0x7f) << 7) | (a->bend_lsb[ch] & 0x7f)) - 0x2000);
    bend = (int16_t)((bend >> 5) * 12);
    n = v->note + v->transpose - 24;
    do n += 12; while (n < 0);
    n += 12;
    do n -= 12; while (n > 0x5f);
    x = (int16_t)(bend + (n << 8));
    x = (int16_t)((int16_t)(x + 8) >> 4);
    x = (int16_t)(x - 0xc0);
    do x = (int16_t)(x + 0xc0); while (x < 0);
    x = (int16_t)(x + 0xc0);
    do x = (int16_t)(x - 0xc0); while (x > 0x5ff);
    semi = x >> 4;
    f = (int16_t)a->fnum[a->row[semi] * 16 + (x & 0xf)];
    blk = a->block[semi] - 1;
    if (f < 0) blk++;
    if (blk < 0) { blk++; f = (int16_t)(f >> 1); }
    return (uint16_t)((((blk << 2) | ((f >> 8) & 3)) << 8) | (f & 0xff));
}

/* 0x1f98: the register groups the voice's dirty bits name, on its chip
 * channel -- the multipliers and flags (0x80, with the modulation wheel's
 * vibrato), the levels (0x40), the envelopes (0x20), the waveforms (0x10),
 * the feedback (0x08) and the frequency and key (0x01) */
static void update(uw_adlib *a, uw_adlib_voice *v) {
    uint8_t c = v->opl, sm, sc, ch = v->channel & 0xf;
    if (c == 0xff || !a->tables_ok) return;
    sm = a->slot_m[c];
    sc = a->slot_c[c];
    if (v->dirty & 0x80) {
        uint8_t vib = a->modulation[ch] >= 0x40 ? 0x40 : 0;
        slot_reg(a, sm, 0x20, (uint8_t)((v->value[P_MOD_MULT] >> 12) | vib | v->flags[0]));
        slot_reg(a, sc, 0x20, (uint8_t)((v->value[P_CAR_MULT] >> 12) | vib | v->flags[1]));
        v->dirty &= 0x7f;
    }
    if (v->dirty & 0x40) {
        uint8_t s = level_scale(a, v), l;
        l = (uint8_t)(v->value[P_MOD_LEVEL] >> 10);
        if (v->connection & 1) l = (uint8_t)(l * s / 0x7f);
        slot_reg(a, sm, 0x40, (uint8_t)((~l & 0x3f) | v->ksl[0]));
        l = (uint8_t)(v->value[P_CAR_LEVEL] >> 10);
        l = (uint8_t)(l * s / 0x7f);
        slot_reg(a, sc, 0x40, (uint8_t)((~l & 0x3f) | v->ksl[1]));
        v->dirty &= 0xbf;
    }
    if (v->dirty & 0x20) {
        slot_reg(a, sm, 0x60, v->ad[0]);
        slot_reg(a, sc, 0x60, v->ad[1]);
        slot_reg(a, sm, 0x80, v->sr[0]);
        slot_reg(a, sc, 0x80, v->sr[1]);
        v->dirty &= 0xdf;
    }
    if (v->dirty & 0x10) {
        slot_reg(a, sc, 0xe0, (uint8_t)v->value[P_WAVES]);
        slot_reg(a, sm, 0xe0, (uint8_t)(v->value[P_WAVES] >> 8));
        v->dirty &= 0xef;
    }
    if (v->dirty & 0x08) {
        reg(a, (uint8_t)(0xc0 + c), (uint8_t)(((v->value[P_FEEDBACK] >> 12) & 0x0e) | v->connection));
        v->dirty &= 0xf7;
    }
    if (v->dirty & 0x01) {
        uint16_t f;
        if (v->effect == 2) {
            f = (uint16_t)(v->value[P_FREQ] >> 6);
        } else if (!(v->keybits & 0x20)) {
            reg(a, (uint8_t)(0xb0 + c), (uint8_t)(v->b0 & 0xdf));
            v->dirty &= 0xfe;
            return;
        } else {
            f = pitch(a, v);
        }
        reg(a, (uint8_t)(0xa0 + c), (uint8_t)f);
        v->b0 = (uint8_t)((f >> 8) | v->keybits);
        reg(a, (uint8_t)(0xb0 + c), v->b0);
        v->dirty &= 0xfe;
    }
}

/* ---- the voices and the chip's channels ----------------------------------- */

/* 0x1f51: the voice's chip channel keyed off and given back */
static void release_chip(uw_adlib *a, uw_adlib_voice *v) {
    if (v->opl == 0xff) return;
    v->keybits &= 0xdf;
    v->dirty |= 1;
    update(a, v);
    a->chip_voices[v->channel & 0xf]--;
    a->chip_owner[v->opl] = 0xff;
    v->opl = 0xff;
}

static void take_chip(uw_adlib *a, uw_adlib_voice *v, uint8_t c) {
    v->opl = c;
    a->chip_voices[v->channel & 0xf]++;
    a->chip_owner[c] = v->channel;
    v->dirty = 0xf9;
}

/* 0x2333: each voice's rank (its priority, or all of it with the
 * channel's voice protection, less the chip channels its MIDI channel
 * holds); with more voices than chip channels, the best voice without one
 * takes the worst voice's -- which, an FM voice, ends -- until the best
 * without is no better than the worst with */
static void rebalance(uw_adlib *a) {
    int i, active = 0, left;
    for (i = 0; i < UW_ADLIB_VOICES; i++) {
        uw_adlib_voice *v = &a->v[i];
        uint16_t r;
        if (!v->state) continue;
        active++;
        r = a->protect[v->channel & 0xf] >= 0x40 ? 0xffff : v->value[P_PRIORITY];
        v->rank = r < a->chip_voices[v->channel & 0xf] ? 0 : (uint16_t)(r - a->chip_voices[v->channel & 0xf]);
    }
    if (active <= 9) return;
    for (left = active; left; left--) {
        int best = -1, worst = -1;
        uint16_t hi = 0, lo = 0xffff;
        for (i = 0; i < UW_ADLIB_VOICES; i++) {
            uw_adlib_voice *v = &a->v[i];
            if (!v->state) continue;
            if (v->opl == 0xff) { if (v->rank >= hi) { hi = v->rank; best = i; } }
            else if (v->rank <= lo) { lo = v->rank; worst = i; }
        }
        if (best < 0 || worst < 0 || hi < lo) return;
        {
            uw_adlib_voice *w = &a->v[worst], *b = &a->v[best];
            uint8_t c = w->opl;
            release_chip(a, w);
            if (!w->effect) w->state = 0;
            take_chip(a, b, c);
            update(a, b);
        }
    }
}

/* 0x1f05: a free chip channel for the voice, its registers written; with
 * none, the priorities decide */
static void assign(uw_adlib *a, uw_adlib_voice *v) {
    uint8_t c;
    for (c = 0; c < 9 && a->chip_owner[c] != 0xff; c++) { }
    if (c == 9) { rebalance(a); return; }
    take_chip(a, v, c);
    update(a, v);
}

/* 0x4f5: after a voice ends, the first voice waiting for a chip channel
 * gets the free one -- its registers left to the next update -- and the
 * priorities are looked at again */
static void reassign(uw_adlib *a) {
    int i;
    uint8_t c;
    for (i = 0; i < UW_ADLIB_VOICES; i++) {
        uw_adlib_voice *v = &a->v[i];
        if (!v->state || v->opl != 0xff) continue;
        for (c = 0; c < 9 && a->chip_owner[c] != 0xff; c++) { }
        if (c == 9) return;
        take_chip(a, v, c);
        rebalance(a);
        return;
    }
}

/* ---- the timbres ------------------------------------------------------------ */

/* 0x2423: a fourteen-byte patch into the voice's parameters */
static void fm_setup(uw_adlib_voice *v) {
    const uint8_t *d = v->timbre;
    v->keybits = 0x20;
    v->effect = 0;
    v->duration = 0xffff;
    v->value[P_PRIORITY] = 0x7fff;
    v->connection = d[8] & 1;
    v->value[P_FEEDBACK] = (uint16_t)(d[8] << 12);
    v->ksl[0] = d[4] & 0xc0;
    v->value[P_MOD_LEVEL] = (uint16_t)((~d[4] & 0x3f) << 10);
    v->ksl[1] = d[10] & 0xc0;
    v->value[P_CAR_LEVEL] = (uint16_t)((~d[10] & 0x3f) << 10);
    v->flags[0] = d[3] & 0xf0;
    v->value[P_MOD_MULT] = (uint16_t)(d[3] << 12);
    v->flags[1] = d[9] & 0xf0;
    v->value[P_CAR_MULT] = (uint16_t)(d[9] << 12);
    v->ad[0] = d[5];
    v->sr[0] = d[6];
    v->ad[1] = d[11];
    v->sr[1] = d[12];
    v->value[P_WAVES] = (uint16_t)(d[13] | d[7] << 8);
    v->dirty = 0xf9;
}

/* 0x546: the parameter's next command -- a zero count jumps by its value,
 * 0xffff sets the parameter, 0xfffe sets a register's bits (the key and
 * block on the frequency's stream, the key-scale bits on the levels', the
 * flags on the multipliers', the connection on the feedback's), anything
 * else a count and an increment; ten commands with no count end it */
static void effect_fetch(uw_adlib_voice *v, int p) {
    int k;
    for (k = 0; k < 10; k++) {
        uint16_t op = w16(v->timbre, v->stream[p], v->timbre_len), arg = w16(v->timbre, v->stream[p] + 2u, v->timbre_len);
        if (v->stream[p] + 4u > v->timbre_len) break;
        if (op == 0) { v->stream[p] = (uint16_t)(v->stream[p] + arg); continue; }
        v->stream[p] = (uint16_t)(v->stream[p] + 4);
        if (op == 0xffff) { v->value[p] = arg; continue; }
        if (op == 0xfffe) {
            switch (p) {
            case P_MOD_MULT: v->flags[0] = (uint8_t)arg; break;
            case P_CAR_MULT: v->flags[1] = (uint8_t)arg; break;
            case P_MOD_LEVEL: v->ksl[0] = (uint8_t)arg; break;
            case P_CAR_LEVEL: v->ksl[1] = (uint8_t)arg; break;
            case P_FREQ:
                v->keybits = (uint8_t)(arg >> 8);
                if (v->effect == 1) v->keybits &= 0xe0;
                break;
            case P_FEEDBACK: v->connection = (uint8_t)(arg >> 8); break;
            default: break;
            }
            continue;
        }
        v->count[p] = op;
        v->inc[p] = arg;
        return;
    }
    v->inc[p] = 0;
    v->count[p] = 0xffff;
}

/* 0x893: an effect's parameters from its timbre -- on the key-on its
 * starting values and streams, released (state 2) the release's streams --
 * and the envelopes, fixed (AR 15, DR 15, SL 0, RR 15) when the timbre
 * carries none */
static void effect_setup(uw_adlib_voice *v) {
    const uint8_t *t = v->timbre;
    size_t n = v->timbre_len;
    uint16_t car = 0xff0f, mod = 0xff0f;
    uint8_t type = n > 3 ? t[3] : 0;
    int p;
    v->connection = 0;
    v->ksl[0] = v->ksl[1] = 0;
    v->flags[0] = v->flags[1] = 0x20;
    v->effect = type;
    v->keybits = (uint8_t)(type == 1 ? 0x20 : 0x28);
    if ((uint16_t)(w16(t, 8, n) + 2) != 0x36) {
        car = w16(t, v->state == 2 ? 0x3a : 0x36, n);
        mod = w16(t, v->state == 2 ? 0x3c : 0x38, n);
    }
    v->ad[0] = (uint8_t)(mod >> 8);
    v->sr[0] = (uint8_t)mod;
    v->ad[1] = (uint8_t)(car >> 8);
    v->sr[1] = (uint8_t)car;
    if (v->state != 2) {
        static const uint8_t val_at[8] = { 0x06, 0x0c, 0x12, 0x18, 0x1e, 0x24, 0x2a, 0x30 };
        v->duration = type == 1 ? 0xffff : (uint16_t)(w16(t, 4, n) + 1);
        for (p = 0; p < 8; p++) {
            v->value[p] = w16(t, val_at[p], n);
            v->stream[p] = (uint16_t)(w16(t, val_at[p] + 2u, n) + 2);
        }
    } else {
        static const uint8_t rel_at[8] = { 0x0a, 0x10, 0x16, 0x1c, 0x22, 0x28, 0x2e, 0x34 };
        for (p = 0; p < 8; p++) v->stream[p] = (uint16_t)(w16(t, rel_at[p], n) + 2);
    }
    v->dirty = 0xf9;
    for (p = 0; p < 8; p++) {
        v->count[p] = 1;
        v->inc[p] = 0;
    }
}

/* ---- the messages ------------------------------------------------------------ */

/* 0x250a: the voices playing the note on the channel end -- held under
 * the sustain pedal, an effect sent to its release on the next step, an FM
 * voice's chip channel freed and given to a voice waiting for one */
static void note_off(uw_adlib *a, uint8_t ch, uint8_t note) {
    int i;
    for (i = 0; i < UW_ADLIB_VOICES; i++) {
        uw_adlib_voice *v = &a->v[i];
        if (v->state != 1 || v->key != note || v->channel != ch) continue;
        if (a->sustain[ch & 0xf] >= 0x40) {
            v->held = 1;
        } else if (!v->effect) {
            release_chip(a, v);
            v->state = 0;
            reassign(a);
        } else {
            v->duration = 1;
        }
    }
}

/* 0x2573: a free voice for the note with the channel's timbre -- the
 * percussion channel's by the note, bank 127 -- its pitch from the note
 * (a drum's from its patch), its velocity through the table, set up as an
 * FM voice or an effect, and a chip channel sought */
static void note_on(uw_adlib *a, uint8_t ch, uint8_t note, uint8_t velocity) {
    uint8_t t = a->timbre_of[ch & 0xf];
    uw_adlib_voice *v = NULL;
    const uint8_t *d;
    int i;
    if (ch == 9) {
        t = a->drum[note & 0x7f];
        if (t == 0xff) a->drum[note & 0x7f] = t = lookup(a, 0x7f, note);
    }
    if (t == 0xff || t >= UW_ADLIB_TIMBRES || !a->timbre[t].used) return;
    d = a->timbre[t].data;
    if (!d || a->timbre[t].len < 14) return;
    for (i = 0; i < UW_ADLIB_VOICES && a->v[i].state; i++) { }
    if (i == UW_ADLIB_VOICES) return;
    v = &a->v[i];
    v->channel = ch;
    v->key = note;
    if (ch == 9) { v->note = d[2]; v->transpose = 0; }
    else { v->note = note; v->transpose = (int8_t)d[2]; }
    v->velocity = a->vel[(velocity >> 3) & 0xf];
    v->timbre = d;
    v->timbre_len = a->timbre[t].len;
    v->state = 1;
    v->held = 0;
    if (w16(d, 0, v->timbre_len) == 0x0e) fm_setup(v);
    else effect_setup(v);
    v->opl = 0xff;
    assign(a, v);
}

/* 0x2671: the sustain pedal lifted: the notes it held end */
static void release_held(uw_adlib *a, uint8_t ch) {
    int i;
    for (i = 0; i < UW_ADLIB_VOICES; i++)
        if (a->v[i].state && a->v[i].channel == ch && a->v[i].held) note_off(a, ch, a->v[i].note);
}

/* 0x27ae: the channel's voices, the register groups marked and written */
static void touch(uw_adlib *a, uint8_t ch, uint8_t bits) {
    int i;
    for (i = 0; i < UW_ADLIB_VOICES; i++) {
        uw_adlib_voice *v = &a->v[i];
        if (!v->state || v->channel != ch) continue;
        v->dirty |= bits;
        update(a, v);
    }
}

void uw_adlib_message(uw_adlib *a, uint8_t status, uint8_t d1, uint8_t d2) {
    uint8_t ch = status & 0xf;
    int i;
    switch (status & 0xf0) {
    case 0xb0:
        switch (d1) {
        case 0x72: a->bank[ch] = d2; break;
        case 0x70: a->protect[ch] = d2; break;
        case 0x71: break;                   /* a timbre's protection in a cache that evicts nothing */
        case 0x01: a->modulation[ch] = d2; touch(a, ch, 0x80); break;
        case 0x07: a->volume[ch] = d2; touch(a, ch, 0x40); break;
        case 0x0b: a->expression[ch] = d2; touch(a, ch, 0x40); break;
        case 0x0a: a->pan[ch] = d2; touch(a, ch, 0x40); break;
        case 0x40:
            a->sustain[ch] = d2;
            if (d2 < 0x40) release_held(a, ch);
            break;
        case 0x79:                          /* reset all controllers */
            a->sustain[ch] = 0;
            release_held(a, ch);
            a->modulation[ch] = 0;
            a->expression[ch] = 0x7f;
            a->bend_lsb[ch] = 0;
            a->bend_msb[ch] = 0x40;
            touch(a, ch, 0xc1);
            break;
        case 0x7b:                          /* all notes off */
            for (i = 0; i < UW_ADLIB_VOICES; i++)
                if (a->v[i].state == 1 && a->v[i].channel == ch) note_off(a, ch, a->v[i].note);
            break;
        default: break;
        }
        break;
    case 0xc0:
        a->program[ch] = d1;
        a->timbre_of[ch] = lookup(a, a->bank[ch], d1);
        break;
    case 0xe0:
        a->bend_lsb[ch] = d1;
        a->bend_msb[ch] = d2;
        touch(a, ch, 0x01);
        break;
    case 0x80:
        note_off(a, ch, d1);
        break;
    case 0x90:
        if (ch < 1 || ch > 9) break;
        if (!d2) note_off(a, ch, d1);
        else note_on(a, ch, d1, d2);
        break;
    default: break;
    }
}

/* 0x60c..0x890: an effect voice one step on -- each parameter's increment
 * added and its count run down to its next command (the levels clamped at
 * zero once released, and written every other step), the registers, and
 * the key-on's duration down to the release, or the released voice ended
 * once both levels are nearly silent */
static void effect_step(uw_adlib *a, uw_adlib_voice *v) {
    static const uint8_t order[8] = { P_FREQ, P_FEEDBACK, P_MOD_MULT, P_CAR_MULT, P_MOD_LEVEL, P_CAR_LEVEL, P_WAVES, P_PRIORITY };
    static const uint8_t add_bits[8] = { 0x01, 0x08, 0x80, 0x80, 0, 0, 0x10, 0 };
    static const uint8_t fetch_bits[8] = { 0x01, 0x08, 0x80, 0x80, 0x40, 0x40, 0x10, 0 };
    int k;
    for (k = 0; k < 8; k++) {
        int p = order[k];
        uint16_t inc = v->inc[p];
        if (inc || p == P_PRIORITY) {
            uint16_t old = v->value[p];
            v->value[p] = (uint16_t)(v->value[p] + inc);
            if (p == P_MOD_LEVEL || p == P_CAR_LEVEL) {
                if (inc) {
                    if (v->state == 2 && ((v->value[p] ^ old) & 0x8000) && !((v->value[p] ^ inc) & 0x8000))
                        v->value[p] = 0;
                    v->dirty |= a->level_flip;
                }
            } else {
                if (inc) v->dirty |= add_bits[k];
            }
        }
        if (--v->count[p] == 0) {
            effect_fetch(v, p);
            v->dirty |= fetch_bits[k];
        }
    }
    if (v->dirty & 0xf9) update(a, v);
    if (v->state != 2) {
        if (--v->duration == 0) {
            v->state = 2;
            effect_setup(v);
        }
    } else if (v->value[P_MOD_LEVEL] < 0x400 && v->value[P_CAR_LEVEL] < 0x400) {
        release_chip(a, v);
        v->state = 0;
        reassign(a);
    }
}

void uw_adlib_service(uw_adlib *a) {
    int i;
    a->effect_clock = (uint16_t)(a->effect_clock + 0x3c);
    if (a->effect_clock >= 0x78) {
        a->effect_clock = (uint16_t)(a->effect_clock - 0x78);
        a->level_flip ^= 0x40;
        for (i = 0; i < UW_ADLIB_VOICES; i++)
            if (a->v[i].state && a->v[i].effect) effect_step(a, &a->v[i]);
    }
    a->rank_clock = (uint16_t)(a->rank_clock + 5);
    if (a->rank_clock >= 0x78) {
        a->rank_clock = (uint16_t)(a->rank_clock - 0x78);
        rebalance(a);
    }
}
