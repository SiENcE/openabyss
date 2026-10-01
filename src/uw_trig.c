/* SPDX-License-Identifier: MIT */
#include "uw_trig.h"

/* The original's sine table, verbatim: 321 words. The first 256 are sine
 * over the WHOLE circle, and the last 65 repeat the first 65 -- which is
 * exactly how far the cosine read can reach, because "the cosine table" is
 * not a second table at all. It is this one, 64 entries (a quarter turn) on:
 * cos(i) = sin(i + 64). The lookup indexes both with the angle's whole high
 * byte and never masks it, and the interpolating one reads entry i + 1 as
 * well, so entry 64 + 255 + 1 = 320 is the last it can touch. Masking the
 * index to 0..63 would fold every angle past a quarter turn back into the
 * first quadrant. */
static const int16_t UW_SINE[321] = {
         0,    804,   1608,   2411,   3212,   4011,   4808,   5602,
      6393,   7180,   7962,   8740,   9512,  10279,  11039,  11793,
     12540,  13279,  14010,  14733,  15447,  16151,  16846,  17531,
     18205,  18868,  19520,  20160,  20788,  21403,  22006,  22595,
     23170,  23732,  24279,  24812,  25330,  25833,  26320,  26791,
     27246,  27684,  28106,  28511,  28899,  29269,  29622,  29957,
     30274,  30572,  30853,  31114,  31357,  31581,  31786,  31972,
     32138,  32286,  32413,  32522,  32610,  32679,  32729,  32758,
     32767,  32758,  32729,  32679,  32610,  32522,  32413,  32286,
     32138,  31972,  31786,  31581,  31357,  31114,  30853,  30572,
     30274,  29957,  29622,  29269,  28899,  28511,  28106,  27684,
     27246,  26791,  26320,  25833,  25330,  24812,  24279,  23732,
     23170,  22595,  22006,  21403,  20788,  20160,  19520,  18868,
     18205,  17531,  16846,  16151,  15447,  14733,  14010,  13279,
     12540,  11793,  11039,  10279,   9512,   8740,   7962,   7180,
      6393,   5602,   4808,   4011,   3212,   2411,   1608,    804,
         0,   -804,  -1608,  -2411,  -3212,  -4011,  -4808,  -5602,
     -6393,  -7180,  -7962,  -8740,  -9512, -10279, -11039, -11793,
    -12540, -13279, -14010, -14733, -15447, -16151, -16846, -17531,
    -18205, -18868, -19520, -20160, -20788, -21403, -22006, -22595,
    -23170, -23732, -24279, -24812, -25330, -25833, -26320, -26791,
    -27246, -27684, -28106, -28511, -28899, -29269, -29622, -29957,
    -30274, -30572, -30853, -31114, -31357, -31581, -31786, -31972,
    -32138, -32286, -32413, -32522, -32610, -32679, -32729, -32758,
    -32767, -32758, -32729, -32679, -32610, -32522, -32413, -32286,
    -32138, -31972, -31786, -31581, -31357, -31114, -30853, -30572,
    -30274, -29957, -29622, -29269, -28899, -28511, -28106, -27684,
    -27246, -26791, -26320, -25833, -25330, -24812, -24279, -23732,
    -23170, -22595, -22006, -21403, -20788, -20160, -19520, -18868,
    -18205, -17531, -16846, -16151, -15447, -14733, -14010, -13279,
    -12540, -11793, -11039, -10279,  -9512,  -8740,  -7962,  -7180,
     -6393,  -5602,  -4808,  -4011,  -3212,  -2411,  -1608,   -804,
         0,    804,   1608,   2411,   3212,   4011,   4808,   5602,
      6393,   7180,   7962,   8740,   9512,  10279,  11039,  11793,
     12540,  13279,  14010,  14733,  15447,  16151,  16846,  17531,
     18205,  18868,  19520,  20160,  20788,  21403,  22006,  22595,
     23170,  23732,  24279,  24812,  25330,  25833,  26320,  26791,
     27246,  27684,  28106,  28511,  28899,  29269,  29622,  29957,
     30274,  30572,  30853,  31114,  31357,  31581,  31786,  31972,
     32138,  32286,  32413,  32522,  32610,  32679,  32729,  32758,
     32767,
};

int16_t uw_trig_sin_table(int i) { return UW_SINE[i & 0xff]; }
int16_t uw_trig_cos_table(int i) { return UW_SINE[(i & 0xff) + 64]; }

void uw_sincos(uint16_t angle, int16_t *s, int16_t *c) {
    /* `mov bl,bh; xor bh,bh; shl bx,1` is the high byte times
     * two, i.e. a word index. The low byte is simply not read on this path. */
    int i = angle >> 8;
    *s = UW_SINE[i];
    *c = UW_SINE[i + 64];
}

static int16_t lerp(int i, int frac) {
    /* One axis of it:
     *     bp = t[i]; ax = t[i+1]; ax -= bp; imul cx      (cx = frac)
     *     mov al,ah; mov ah,dl                           (>> 8 of DX:AX)
     *     add ax,bp
     * The shift is taken out of the 32-bit product, so the difference is
     * allowed to use the full 16 bits before scaling. Done in 16-bit C it
     * would overflow for the larger steps. */
    int cur = UW_SINE[i];
    int next = UW_SINE[i + 1];
    return (int16_t)(cur + (((next - cur) * frac) >> 8));
}

void uw_sincos_lerp(uint16_t angle, int16_t *s, int16_t *c) {
    int i = angle >> 8;
    int frac = angle & 0xFF;
    *s = lerp(i, frac);
    *c = lerp(i + 64, frac);
}

uint16_t uw_isqrt32(uint32_t n) {
    /* isqrt32_newton. Five unrolled Newton steps over a seed
     * chosen by which byte of the argument is the highest non-zero one:
     *
     *     >= 2^24  seed 0x4000, and 0xffff when the high word already
     *              reaches 0x4000 -- the clamp is in the original
     *     >= 2^16  seed 0x0400
     *     >= 2^8   seed 0x0040
     *     else     seed 0x0004
     *
     * `x = (x + n/x) / 2`, where the halving is `rcr` -- THROUGH THE CARRY --
     * so the sum may exceed 16 bits and the rotate brings it back. That is
     * why the addition here is 32-bit and the shift is by one: writing this
     * in uint16_t reproduces the original's registers and not its answers.
     *
     * No convergence test and no final correction, so the result can sit one
     * away from the true floor of the root. Reproduce that rather than fix
     * it: callers were tuned against these numbers. */
    uint32_t x;
    if (n == 0) return 0;
    if (n >> 24)      x = (n >> 16) >= 0x4000 ? 0xFFFF : 0x4000;
    else if (n >> 16) x = 0x0400;
    else if (n >> 8)  x = 0x0040;
    else              x = 0x0004;
    for (int k = 0; k < 5; k++) {
        if (x == 0) break;                 /* the original divides; so do we */
        x = (x + n / x) >> 1;
        if (x > 0xFFFF) x = 0xFFFF;
    }
    return (uint16_t)x;
}

/* The arc table, verbatim: 259 words in the graphics module's private segment.
 * Entries 0..128 are the arc sine of a 1.15 fraction's high byte (0x80 is
 * 1.0) in 256ths of a quarter turn scaled to 0x4000, and entries 129..257 the
 * arc cosine the same way (atan_lerp_far reads them there); 258 is the
 * word after, which a fraction of exactly -1.0 on the cosine side reaches. */
static const int16_t UW_ARC[259] = {
         0,     81,    162,    244,    326,    407,    489,    570,
       652,    733,    815,    897,    979,   1061,   1143,   1225,
      1307,   1389,   1471,   1554,   1636,   1719,   1801,   1884,
      1967,   2050,   2133,   2216,   2300,   2383,   2467,   2551,
      2635,   2719,   2804,   2888,   2973,   3058,   3143,   3229,
      3315,   3400,   3487,   3573,   3660,   3747,   3834,   3921,
      4009,   4097,   4185,   4274,   4363,   4452,   4542,   4632,
      4723,   4813,   4905,   4996,   5088,   5181,   5274,   5367,
      5461,   5555,   5650,   5745,   5841,   5938,   6035,   6132,
      6231,   6330,   6429,   6529,   6630,   6732,   6834,   6937,
      7041,   7146,   7252,   7358,   7466,   7574,   7684,   7795,
      7906,   8019,   8133,   8248,   8365,   8483,   8602,   8723,
      8845,   8969,   9095,   9223,   9352,   9484,   9617,   9753,
      9892,  10033,  10177,  10324,  10474,  10628,  10785,  10946,
     11112,  11283,  11460,  11642,  11831,  12028,  12233,  12449,
     12676,  12918,  13177,  13459,  13769,  14121,  14537,  15079,
     16384,  16384,  16302,  16221,  16139,  16057,  15976,  15894,
     15813,  15731,  15650,  15568,  15486,  15404,  15322,  15240,
     15158,  15076,  14994,  14912,  14829,  14747,  14664,  14582,
     14499,  14416,  14333,  14250,  14167,  14083,  14000,  13916,
     13832,  13748,  13664,  13579,  13495,  13410,  13325,  13240,
     13154,  13068,  12983,  12896,  12810,  12723,  12636,  12549,
     12462,  12374,  12286,  12198,  12109,  12020,  11931,  11841,
     11751,  11660,  11570,  11478,  11387,  11295,  11202,  11109,
     11016,  10922,  10828,  10733,  10638,  10542,  10445,  10348,
     10251,  10152,  10053,   9954,   9854,   9753,   9651,   9549,
      9446,   9342,   9237,   9131,   9025,   8917,   8809,   8699,
      8588,   8477,   8364,   8250,   8135,   8018,   7900,   7781,
      7660,   7538,   7414,   7288,   7160,   7031,   6899,   6766,
      6630,   6491,   6350,   6206,   6059,   5909,   5755,   5598,
      5437,   5271,   5100,   4923,   4741,   4552,   4355,   4150,
      3934,   3707,   3465,   3206,   2924,   2614,   2262,   1846,
      1304,      0,      0,
};

/* atan_lerp_near and atan_lerp_far: one table read
 * on the magnitude, interpolated by its low byte as uw_sincos_lerp does, and
 * the sign put back. */
static int16_t arc_lerp(int base, int16_t v) {
    int16_t sign = (int16_t)(v < 0 ? -1 : 0);
    uint16_t a = (uint16_t)((v ^ sign) - sign);
    int i = a >> 8, frac = a & 0xff;
    int16_t cur = UW_ARC[base + i];
    int16_t r = (int16_t)(cur + (int16_t)(((int32_t)(int16_t)(UW_ARC[base + i + 1] - cur) * frac) >> 8));
    return (int16_t)((r ^ sign) - sign);
}

int16_t uw_atan2(int16_t s, int16_t c) {
    /* gfx_atan2, AX = s and BX = c, the answer in CX. */
    int16_t r;
    if (s <= 0x5a82 && s >= -0x5a82) {
        r = arc_lerp(0, s);
        if (c < 0) r = (int16_t)-(int16_t)(r - 0x8000);
        return r;
    }
    r = arc_lerp(129, c);
    if (c < 0) r = (int16_t)(r + 0x8000);
    return s < 0 ? (int16_t)-r : r;
}
