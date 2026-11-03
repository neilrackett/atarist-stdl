/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * A bank's art for an 8-plane screen (stdl_responsive.h; docs/format.md
 * section 6.1). On one, STDL_LoadSurface, STDL_LoadSprite and
 * STDL_LoadPalette look here first, and take the 4-plane chunk where
 * the bank has no 8-plane one. A surface8 chunk (a sprite8 is one with
 * a frame width in front) holds an id's art in one of two forms:
 *   - its own, 8-plane rows with a palette of their own (palette8);
 *   - planes 4-7 over the ST's art: the 4-plane chunk of the same id
 *     gives planes 0-3 and the transparency, so every pixel's low
 *     nibble is its ST colour and a bank carries the ST's art once.
 * Linked by STDL_RESPONSIVE_BANKS alone: through the 8-plane table
 * every program with the colour linked it, banks or none (1.7K now).
 * The bank reader is asset.c's, handed in, so this links none of its
 * own.
 */

#include <stdlib.h>
#include <string.h>
#include "responsive.h"

/* the form byte of a surface8 payload */
#define A8_OWN   0              /* its own art                        */
#define A8_KEYED 1              /* its own, keyed on the next byte    */
#define A8_OVER  2              /* planes 4-7 over the 4-plane chunk  */

/*
 * The plane words of a payload where they lie, if it has `bytes` of
 * them at `at`: load_chunk's payload is a malloc of its own and every
 * header an even length, so on the machine they are aligned words
 * already; the host swaps them to its byte order in place.
 */
static const uint16_t *a8_words(uint8_t *p, uint32_t len, uint32_t at,
                                uint32_t bytes)
{
    if (len < at || len - at < bytes) {
        STDL_SetError("bad 8-plane chunk");
        return NULL;
    }
#ifndef __m68k__
    stdl_be_words(p + at, p + at, bytes);
#endif
    return (const uint16_t *)(p + at);
}

/* s's mask from 4-plane art's key over planes 0-3, kept as
 * STDL_TRANSPARENT keeps one: pixels of any shade over the ST's key */
static int a8_lokey(STDL_Surface *s, const uint16_t *lo, unsigned key)
{
    uint32_t n = stdl_row_off(s->h, (uint16_t)(s->stride >> 4));
    uint16_t *m;
    int p;

    if (stdl_resp->create_mask(s, 0) < 0) {
        return -1;
    }
    for (m = (uint16_t *)s->mask; n > 0; n--, lo += 4) {
        uint16_t bits = 0xFFFFu;

        for (p = 0; p < 4; p++) {
            bits &= (key & (1u << p)) ? lo[p] : (uint16_t)~lo[p];
        }
        *m++ = bits;
    }
    return stdl_resp->set_key(s, 1, STDL_TRANSPARENT);
}

/* planes 4-7 over a 4-plane surface chunk (its rows, its key) */
static int a8_over_surface(STDL_Surface *s, const uint16_t *hi, uint8_t *lop,
                           uint32_t lolen)
{
    uint32_t n = stdl_row_off(s->h, (uint16_t)(s->stride >> 4));
    const uint16_t *lo, *l;
    uint16_t *d = (uint16_t *)s->pixels;

    if (lolen < 6 || stdl_rd16(lop) != s->w || stdl_rd16(lop + 2) != s->h
        || (lo = a8_words(lop, lolen, 6, n << 3)) == NULL) {
        STDL_SetError("bad 8-plane chunk");
        return -1;
    }
    for (l = lo; n > 0; n--, d += 8, l += 4, hi += 4) {
        d[0] = l[0]; d[1] = l[1]; d[2] = l[2]; d[3] = l[3];
        d[4] = hi[0]; d[5] = hi[1]; d[6] = hi[2]; d[7] = hi[3];
    }
    return lop[4] ? a8_lokey(s, lo, lop[5] & 15u) : 0;
}

/*
 * planes 4-7 over a 4-plane sprite chunk, frames of a mask and four
 * planes a group (docs/format.md section 3), into the strip: one
 * variant of 4 planes, frames a whole number of groups, and both
 * chunks the same strip
 */
static int a8_over_sprite(STDL_Surface *s, int fw, const uint16_t *hi,
                          uint8_t *lop, uint32_t lolen)
{
    const int gf = fw >> 4, gs = s->stride >> 4;
    const uint16_t *lo;
    int nf, f, y, g, g0;

    if (lolen < 14 || lop[6] != 1 || lop[7] != 4 || stdl_rd16(lop) != fw
        || (fw & 15) || stdl_rd16(lop + 8) != gf
        || stdl_rd16(lop + 2) != s->h
        || (nf = stdl_rd16(lop + 4), s->w != (int)STDL_MulU16((unsigned)fw,
                                                              (unsigned)nf))
        || stdl_rd32(lop + 10) != stdl_row_off(s->h, (uint16_t)(gf * 5))
        || (lo = a8_words(lop, lolen, 14,
                          stdl_row_off(s->h, (uint16_t)(gs * 10)))) == NULL
        || stdl_resp->create_mask(s, 0) < 0) {
        STDL_SetError("bad 8-plane sprite chunk");
        return -1;
    }
    for (f = 0, g0 = 0; f < nf; f++, g0 += gf) {
        for (y = 0; y < s->h; y++) {
            uint16_t *d = (uint16_t *)(s->pixels + stdl_row_off(y, s->stride))
                        + (g0 << 3);
            uint16_t *m = (uint16_t *)(s->mask
                                       + stdl_row_off(y, s->maskstride)) + g0;
            const uint16_t *h4 = hi + (stdl_row_off(y, (uint16_t)gs) << 2)
                               + (g0 << 2);

            for (g = 0; g < gf; g++, d += 8, lo += 5, h4 += 4) {
                m[g] = lo[0];
                d[0] = lo[1]; d[1] = lo[2]; d[2] = lo[3]; d[3] = lo[4];
                d[4] = h4[0]; d[5] = h4[1]; d[6] = h4[2]; d[7] = h4[3];
            }
        }
    }
    return stdl_resp->set_key(s, 1, STDL_TRANSPARENT);
}

/*
 * The 8-plane surface a surface8 payload holds (a sprite8's, after its
 * frame width `fw`, its frames side by side): its own rows, written
 * straight in as STDL_LoadSurface writes 4 planes, or planes 4-7 over
 * the 4-plane chunk of the same id
 */
static STDL_Surface *a8_art(const char *bank, int id, uint8_t *p,
                            uint32_t len, int fw, stdl_chunk_fn read)
{
    STDL_Surface *s;
    const uint16_t *hi;
    uint8_t *lop;
    uint32_t lolen, groups;
    int ok;

    if (len < 6 || (s = stdl_resp->create(stdl_rd16(p),
                                          stdl_rd16(p + 2))) == NULL) {
        return NULL;
    }
    groups = stdl_row_off(s->h, (uint16_t)(s->stride >> 4));
    if (p[4] != A8_OVER) {
        if (len - 6 < groups << 4) {
            STDL_SetError("bad 8-plane chunk");
            ok = -1;
        } else {
            stdl_be_words(s->pixels, p + 6, groups << 4);
            ok = p[4] == A8_KEYED ? stdl_resp->set_key(s, 1, p[5]) : 0;
        }
    } else if ((hi = a8_words(p, len, 6, groups << 3)) == NULL
               || (lop = read(bank, fw ? CHUNK_SPRITE : CHUNK_SURFACE, id,
                              &lolen)) == NULL) {
        ok = -1;
    } else {
        ok = fw ? a8_over_sprite(s, fw, hi, lop, lolen)
                : a8_over_surface(s, hi, lop, lolen);
        free(lop);
    }
    if (ok < 0) {
        stdl_resp->free_surface(s);
        return NULL;
    }
    return s;
}

static void *a8_load(const char *bank, int type, int id, uint32_t flags,
                     uint32_t *len, stdl_chunk_fn read)
{
    STDL_Surface *s;
    void *r = NULL;
    uint8_t *p;

    /* here, not in asset.c, which every program with a bank links */
    if (!STDL_RESP_DEEP()) {
        return NULL;
    }
    if (type == CHUNK_PALETTE) {
        return read(bank, CHUNK_PALETTE8, id, len);
    }
    if (type == CHUNK_SURFACE) {
        p = read(bank, CHUNK_SURFACE8, id, len);
        if (p != NULL) {
            r = a8_art(bank, id, p, *len, 0, read);
        }
    } else {
        p = read(bank, CHUNK_SPRITE8, id, len);
        if (p != NULL && *len >= 2
            && (s = a8_art(bank, id, p + 2, *len - 2, stdl_rd16(p),
                           read)) != NULL) {
            r = stdl_resp->sprite_from(s, stdl_rd16(p), flags);
            stdl_resp->free_surface(s);
        }
    }
    free(p);
    return r;
}

const resp_banks stdl_resp_banks = { a8_load };
