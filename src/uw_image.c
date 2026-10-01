/* SPDX-License-Identifier: MIT */
#include "uw_image.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- palettes ----------------------------------------------------------- */

bool uw_palettes_open(uw_palettes *p, const char *path) {
    memset(p, 0, sizeof *p);
    p->file = uw_read_file(path);
    if (!p->file.data) return false;
    p->count = (int)(p->file.size / 768);
    return p->count > 0;
}

void uw_palettes_close(uw_palettes *p) { uw_free(&p->file); p->count = 0; }

uw_rgb uw_palette_colour(const uw_palettes *p, int palette, int index) {
    uw_rgb c = {0, 0, 0};
    if (palette < 0 || palette >= p->count || index < 0 || index > 255)
        return c;
    const uint8_t *q = p->file.data + (size_t)palette * 768 + (size_t)index * 3;
    c.r = (uint8_t)(q[0] * 255 / 63);
    c.g = (uint8_t)(q[1] * 255 / 63);
    c.b = (uint8_t)(q[2] * 255 / 63);
    return c;
}

/* ---- auxiliary palettes -------------------------------------------------- */

bool uw_auxpals_open(uw_auxpals *a, const char *path) {
    memset(a, 0, sizeof *a);
    a->file = uw_read_file(path);
    if (!a->file.data) return false;
    a->count = (int)(a->file.size / UW_AUXPAL_SIZE);
    return a->count > 0;
}

void uw_auxpals_close(uw_auxpals *a) { uw_free(&a->file); a->count = 0; }

const uint8_t *uw_auxpal(const uw_auxpals *a, int i) {
    if (i < 0 || i >= a->count) return NULL;
    return a->file.data + (size_t)i * UW_AUXPAL_SIZE;
}

/* ---- .tr ----------------------------------------------------------------- */

bool uw_tr_open(uw_tr *t, const char *path) {
    memset(t, 0, sizeof *t);
    t->file = uw_read_file(path);
    if (!t->file.data || t->file.size < 4) return false;
    /* A check, not an assertion: the Python raises here for the same reason,
     * that a format test compiled out (or asserted away) is not a test. */
    if (t->file.data[0] != 2) { uw_free(&t->file); return false; }
    t->dim = t->file.data[1];
    t->count = uw_u16(t->file.data + 2);
    if (t->dim <= 0 || t->file.size < 4 + (size_t)t->count * 4) {
        uw_free(&t->file);
        return false;
    }
    return true;
}

void uw_tr_close(uw_tr *t) { uw_free(&t->file); t->count = t->dim = 0; }

uint32_t uw_tr_offset(const uw_tr *t, int i) {
    if (i < 0 || i >= t->count) return 0;
    return uw_u32(t->file.data + 4 + (size_t)i * 4);
}

const uint8_t *uw_tr_texture(const uw_tr *t, int i) {
    uint32_t o = uw_tr_offset(t, i);
    size_t n = (size_t)t->dim * (size_t)t->dim;
    if (i < 0 || i >= t->count || (size_t)o + n > t->file.size) return NULL;
    return t->file.data + o;
}

/* ---- the type-8 4-bit RLE ------------------------------------------------ */

typedef struct {
    const uint8_t *nib;
    size_t n, i, want;
    uint8_t *out;
    size_t cap, produced, limit;
    int  depth;
    bool too_deep, overproduced;
} rle4;

/* Past the end reads as 0 AND STILL ADVANCES, which is what lets the outer
 * loop terminate on a stream whose last record asks for more than it has. */
static uint8_t nib_next(rle4 *s) {
    uint8_t v = (s->i < s->n) ? s->nib[s->i] : 0u;
    s->i++;
    return v;
}

/* first != 0 -> (first << 4) | next; first == 0 -> the next four nibbles as
 * a 16-bit big-endian count. */
static uint32_t rle4_ext(rle4 *s, uint8_t first) {
    if (first != 0)
        return (uint32_t)first << UW_RLE_EXT_SHIFT | nib_next(s);
    uint32_t a = nib_next(s), b = nib_next(s), c = nib_next(s), d = nib_next(s);
    return a << (3 * UW_RLE_EXT_SHIFT) | b << (2 * UW_RLE_EXT_SHIFT)
         | c << UW_RLE_EXT_SHIFT | d;
}

static void rle4_emit(rle4 *s, uint8_t v, uint32_t count) {
    for (uint32_t k = 0; k < count; k++) {
        if (s->produced >= s->limit) { s->overproduced = true; return; }
        if (s->produced < s->cap) s->out[s->produced] = v;
        s->produced++;
    }
}

static void rle4_run(rle4 *s) {
    uint32_t c = nib_next(s);
    if (c == 0) c = rle4_ext(s, nib_next(s));
    for (uint32_t k = 0; k < c; k++) rle4_emit(s, nib_next(s), 1);
}

static void rle4_record(rle4 *s, bool suppress_run) {
    if (s->depth > UW_RLE_MAX_DEPTH) { s->too_deep = true; return; }
    uint8_t c1 = nib_next(s);
    if (c1 > 2) {
        uint8_t v = nib_next(s);
        rle4_emit(s, v, c1);
    } else if (c1 == 2) {
        /* meta: decode the next record k times, then one run record */
        uint32_t k = nib_next(s);
        if (k == 0) k = rle4_ext(s, nib_next(s));
        s->depth++;
        for (uint32_t j = 0; j < k && !s->too_deep && !s->overproduced; j++)
            rle4_record(s, true);
        s->depth--;
        if (!suppress_run) rle4_run(s);
        return;
    } else if (c1 == 1) {
        /* no repeat this record; go straight to the run */
    } else {
        uint32_t rep = rle4_ext(s, nib_next(s));
        uint8_t v = nib_next(s);
        rle4_emit(s, v, rep);
    }
    if (!suppress_run) rle4_run(s);
}

size_t uw_rle_decode(const uint8_t *code, size_t n, uint8_t *out, size_t cap,
                     size_t want, bool *too_deep, bool *overproduced) {
    rle4 s;
    memset(&s, 0, sizeof s);
    s.nib = code; s.n = n; s.out = out; s.cap = cap; s.want = want;
    /* A runaway extended count can ask for 65,535 values per record. Valid
     * images stop at width*height plus two, so a limit well above `cap`
     * costs nothing and keeps a malformed file from spinning. */
    s.limit = cap + 4096;
    while (s.i < s.n && s.produced < s.want && !s.overproduced)
        rle4_record(&s, false);
    if (too_deep) *too_deep = s.too_deep;
    if (overproduced) *overproduced = s.overproduced;
    return s.produced;
}

/* ---- .gr ----------------------------------------------------------------- */

bool uw_gr_open(uw_gr *g, const char *path) {
    memset(g, 0, sizeof *g);
    g->file = uw_read_file(path);
    if (!g->file.data || g->file.size < 3) return false;
    if (g->file.data[0] != 1) { uw_free(&g->file); return false; }
    g->count = uw_u16(g->file.data + 1);
    if (g->file.size < 3 + (size_t)g->count * 4) { uw_free(&g->file); return false; }
    return true;
}

void uw_gr_close(uw_gr *g) { uw_free(&g->file); g->count = 0; }

uint32_t uw_gr_offset(const uw_gr *g, int i) {
    if (i < 0 || i >= g->count) return 0;
    return uw_u32(g->file.data + 3 + (size_t)i * 4);
}

size_t uw_gr_entry_size(const uw_gr *g, int i) {
    uint32_t o = uw_gr_offset(g, i);
    if (!o || o > g->file.size) return 0;
    size_t end = i + 1 < g->count ? uw_gr_offset(g, i + 1) : g->file.size;
    return end > o ? end - o : 0;
}

uint32_t uw_gr_sentinel(const uw_gr *g) {
    size_t at = 3 + (size_t)g->count * 4;
    return at + 4 <= g->file.size ? uw_u32(g->file.data + at) : 0;
}

/* Unpacks `bytes` bytes at `p` into one nibble per byte, high nibble first. */
static size_t unpack_nibbles(const uint8_t *p, size_t bytes,
                             uint8_t *out, size_t cap) {
    size_t n = 0;
    for (size_t k = 0; k < bytes; k++) {
        if (n < cap) out[n] = (uint8_t)(p[k] >> 4);
        n++;
        if (n < cap) out[n] = (uint8_t)(p[k] & 0x0f);
        n++;
    }
    return n;
}

bool uw_gr_image(const uw_gr *g, int i, const uint8_t *aux,
                 uint8_t *out, size_t cap, uw_gr_info *info) {
    uw_gr_info z;
    memset(&z, 0, sizeof z);
    z.auxpal = -1;
    z.count_word = -1;
    if (info) *info = z;

    uint32_t o = uw_gr_offset(g, i);
    if (!o) return false;                    /* an empty slot */
    if (!uw_gr_entry_size(g, i)) return false;   /* a zero-length entry */
    if ((size_t)o + 3 > g->file.size) {
        /* An offset with no room for a header. 36 of the retail .GR slots
         * are like this and the Python tool silently skips them; here they
         * are reported, because "the slot is empty" and "the slot points
         * past the end of its file" are different facts. */
        z.truncated = true;
        if (info) *info = z;
        return false;
    }
    const uint8_t *d = g->file.data;
    z.type = d[o];
    z.width = d[o + 1];
    z.height = d[o + 2];
    size_t px = (size_t)z.width * (size_t)z.height;

    if (z.type == UW_GR_UNCOMPRESSED) {
        /* +3 is the pixel count, the pixels start at +5. */
        if ((size_t)o + 5 + px > g->file.size) {
            z.truncated = true;
            if (info) *info = z;
            return false;
        }
        z.count_word = uw_u16(d + o + 3);
        z.produced = px;
        for (size_t k = 0; k < px && k < cap; k++) out[k] = d[o + 5 + k];
        if (info) *info = z;
        return true;
    }

    if (z.type != UW_GR_RLE && z.type != UW_GR_PACKED4) {
        if (info) *info = z;
        return false;               /* a type this does not decode */
    }

    if ((size_t)o + 6 > g->file.size) {
        z.truncated = true;
        if (info) *info = z;
        return false;
    }
    z.auxpal = d[o + 3];
    uint16_t word = uw_u16(d + o + 4);
    z.count_word = word;

    if (z.type == UW_GR_PACKED4) {
        /* `word` is a BYTE count here and a NIBBLE count for type 8. Two
         * formats one field apart, and reading it the other way gives a
         * half-height picture rather than an error. */
        if ((size_t)o + 6 + word > g->file.size) {
            z.truncated = true;
            if (info) *info = z;
            return false;
        }
        size_t n = 0;
        for (size_t k = 0; k < word && n < px; k++) {
            uint8_t hi = (uint8_t)(d[o + 6 + k] >> 4);
            uint8_t lo = (uint8_t)(d[o + 6 + k] & 0x0f);
            if (n < cap) out[n] = aux ? aux[hi] : hi;
            if (++n >= px) break;
            if (n < cap) out[n] = aux ? aux[lo] : lo;
            n++;
        }
        z.produced = (size_t)word * 2;
        if (info) *info = z;
        return n >= px;
    }

    /* type 8: `word` nibbles in ceil(word/2) bytes */
    size_t bytes = ((size_t)word + 1) / 2;
    if ((size_t)o + 6 + bytes > g->file.size) {
        z.truncated = true;
        if (info) *info = z;
        return false;
    }
    /* One nibble per byte. The largest stream in the retail data is well
     * under this; a bigger one is a file this build has never seen. */
    static uint8_t nib[1 << 17];
    if (bytes * 2 > sizeof nib) {
        z.truncated = true;
        if (info) *info = z;
        return false;
    }
    size_t n = unpack_nibbles(d + o + 6, bytes, nib, sizeof nib);
    if (n > word) n = word;         /* an odd nibble count drops the last */

    static uint8_t vals[1 << 17];
    size_t want = px + 8 < sizeof vals ? px + 8 : sizeof vals;
    z.produced = uw_rle_decode(nib, n, vals, want, (size_t)-1, &z.too_deep,
                               &z.overproduced);
    size_t have = z.produced < want ? z.produced : want;
    for (size_t k = 0; k < px && k < have && k < cap; k++)
        out[k] = aux ? aux[vals[k]] : vals[k];
    if (info) *info = z;
    return z.produced >= px && !z.too_deep && !z.overproduced;
}

size_t uw_bit_codes(const uint8_t *data, size_t bytes, size_t count, int bits,
                    uint8_t *out, size_t cap) {
    uint32_t acc = 0;
    int nb = 0;
    size_t i = 0, n = 0;
    for (size_t k = 0; k < count; k++) {
        while (nb < bits) {
            acc = (acc << 8) | (i < bytes ? data[i] : 0u);
            i++;
            nb += 8;
        }
        nb -= bits;
        uint8_t v = (uint8_t)((acc >> nb) & ((1u << bits) - 1u));
        acc &= (1u << nb) - 1u;
        if (n < cap) out[n] = v;
        n++;
    }
    return n;
}

/* ---- the interface's art ------------------------------------------------ */

typedef struct { uint8_t *px; int w, h; } art_image;
static art_image art_obj[0x200], art_1000[0x100], art_2000[0x100];
static int art_loaded;
static char art_data_dir[400] = "DATA";   /* until uw_art_load names the game's */

static int art_file(const char *data_dir, const char *name, const uw_blob *aux, art_image *out, int max) {
    char path[512];
    uw_gr g;
    int i, n = 0;
    snprintf(path, sizeof path, "%s/%s.GR", data_dir, name);
    if (!uw_gr_open(&g, path)) return 0;
    for (i = 0; i < g.count && i < max; i++, n++) {
        uw_gr_info info;
        const uint8_t *pal = NULL;
        size_t sz;
        uw_gr_image(&g, i, NULL, NULL, 0, &info);
        if (info.type != 4 && info.auxpal >= 0 && aux->data
            && (size_t)(info.auxpal + 1) * UW_AUXPAL_SIZE <= aux->size)
            pal = aux->data + info.auxpal * UW_AUXPAL_SIZE;
        if (info.width <= 0 || info.height <= 0 || (info.type != 4 && !pal)) continue;
        sz = (size_t)info.width * (size_t)info.height;
        out[i].px = malloc(sz + 4);
        if (!out[i].px || !uw_gr_image(&g, i, pal, out[i].px, sz, &info)) {
            free(out[i].px);
            out[i].px = NULL;
            continue;
        }
        out[i].w = info.width;
        out[i].h = info.height;
    }
    uw_gr_close(&g);
    return n;
}

int uw_art_load(const char *data_dir) {
    static const char *files_2000[] = { "LFTI", "FLASKS", "COMPASS", "DRAGONS", "INV", "POWER", "EYES",
                                        "CHAINS", "SPELLS", "SCRLEDGE", "OPTB" };
    static const char *files_1000[] = { "BUTTONS", "CURSORS", "3DWIN" };
    char path[512];
    uw_blob aux;
    size_t f;
    int at;
    if (art_loaded) return art_loaded;
    snprintf(art_data_dir, sizeof art_data_dir, "%s", data_dir);
    snprintf(path, sizeof path, "%s/ALLPALS.DAT", data_dir);
    aux = uw_read_file(path);
    art_loaded += art_file(data_dir, "OBJECTS", &aux, art_obj, 0x200);
    art_loaded += art_file(data_dir, "TMFLAT", &aux, art_obj + 0x170, 0x10);
    for (f = 0, at = 0; f < sizeof files_1000 / sizeof *files_1000; f++) {
        int n = art_file(data_dir, files_1000[f], &aux, art_1000 + at, 0x100 - at);
        at += n;
        art_loaded += n;
    }
    for (f = 0, at = 0; f < sizeof files_2000 / sizeof *files_2000; f++) {
        int n = art_file(data_dir, files_2000[f], &aux, art_2000 + at, 0x100 - at);
        at += n;
        art_loaded += n;
    }
    uw_free(&aux);
    return art_loaded;
}

const uint8_t *uw_art(void *user, uint16_t id, int *w, int *h) {
    art_image *a = NULL;
    (void)user;
    if (id >= 0x2000 && id < 0x2100) a = &art_2000[id - 0x2000];
    else if (id >= 0x1000 && id < 0x1100) a = &art_1000[id - 0x1000];
    else if (id < 0x200) a = &art_obj[id];
    if (!a || !a->px) return NULL;
    *w = a->w;
    *h = a->h;
    return a->px;
}

typedef struct { char name[16]; int index; art_image img; } gr_cached;
static gr_cached gr_cache[64];
static int gr_cache_n;

const uint8_t *uw_gr_file_image(const char *data_dir, const char *name, int index, int *w, int *h) {
    int k;
    char path[512];
    uw_blob aux;
    uw_gr g;
    uw_gr_info info;
    const uint8_t *pal = NULL;
    size_t sz;
    for (k = 0; k < gr_cache_n; k++)
        if (gr_cache[k].index == index && !strcmp(gr_cache[k].name, name)) {
            if (!gr_cache[k].img.px) return NULL;
            *w = gr_cache[k].img.w;
            *h = gr_cache[k].img.h;
            return gr_cache[k].img.px;
        }
    if (gr_cache_n == (int)(sizeof gr_cache / sizeof *gr_cache) || strlen(name) >= sizeof gr_cache[0].name) return NULL;
    k = gr_cache_n++;
    snprintf(gr_cache[k].name, sizeof gr_cache[k].name, "%s", name);
    gr_cache[k].index = index;
    gr_cache[k].img.px = NULL;
    {
        /* "NAME#raw": a 4-bit image's nibbles, no auxiliary palette applied */
        const char *raw = strchr(name, '#');
        snprintf(path, sizeof path, "%s/%.*s.GR", data_dir, raw ? (int)(raw - name) : (int)strlen(name), name);
        if (!uw_gr_open(&g, path)) return NULL;
        snprintf(path, sizeof path, "%s/ALLPALS.DAT", data_dir);
        aux = uw_read_file(path);
        uw_gr_image(&g, index, NULL, NULL, 0, &info);
        if (raw && info.type != 4) {
            static const uint8_t nibbles[UW_AUXPAL_SIZE] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 };
            pal = nibbles;
        } else if (info.type != 4 && info.auxpal >= 0 && aux.data && (size_t)(info.auxpal + 1) * UW_AUXPAL_SIZE <= aux.size) {
            pal = aux.data + info.auxpal * UW_AUXPAL_SIZE;
        }
    }
    if (info.width > 0 && info.height > 0 && (info.type == 4 || pal)) {
        sz = (size_t)info.width * (size_t)info.height;
        gr_cache[k].img.px = malloc(sz + 4);
        if (gr_cache[k].img.px && uw_gr_image(&g, index, pal, gr_cache[k].img.px, sz, &info)) {
            gr_cache[k].img.w = info.width;
            gr_cache[k].img.h = info.height;
        } else {
            free(gr_cache[k].img.px);
            gr_cache[k].img.px = NULL;
        }
    }
    uw_free(&aux);
    uw_gr_close(&g);
    if (!gr_cache[k].img.px) return NULL;
    *w = gr_cache[k].img.w;
    *h = gr_cache[k].img.h;
    return gr_cache[k].img.px;
}

const uint8_t *uw_gr_file(void *user, const char *name, int index, int *w, int *h) {
    (void)user;
    return uw_gr_file_image(art_data_dir, name, index, w, h);
}

/* gr_load_to_buffer: the entry's bytes, undecoded. */
const uint8_t *uw_gr_file_entry(const char *data_dir, const char *name, int index, size_t len) {
    static struct { char name[16]; int index; uint8_t *px; size_t len; } cache[8];
    static int cache_n;
    char path[512];
    uw_gr g;
    uint32_t off;
    size_t have;
    int k;
    for (k = 0; k < cache_n; k++)
        if (cache[k].index == index && cache[k].len >= len && !strcmp(cache[k].name, name))
            return cache[k].px;
    if (cache_n == (int)(sizeof cache / sizeof *cache) || strlen(name) >= sizeof cache[0].name) return NULL;
    snprintf(path, sizeof path, "%s/%s.GR", data_dir, name);
    if (!uw_gr_open(&g, path)) return NULL;
    off = uw_gr_offset(&g, index);
    have = uw_gr_entry_size(&g, index);
    if (!off || have < len || (size_t)off + len > g.file.size) { uw_gr_close(&g); return NULL; }
    k = cache_n++;
    snprintf(cache[k].name, sizeof cache[k].name, "%s", name);
    cache[k].index = index;
    cache[k].len = len;
    cache[k].px = malloc(len);
    if (cache[k].px) memcpy(cache[k].px, g.file.data + off, len);
    uw_gr_close(&g);
    return cache[k].px;
}

const uint8_t *uw_gr_entry(void *user, const char *name, int index, size_t len) {
    (void)user;
    return uw_gr_file_entry(art_data_dir, name, index, len);
}
