/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * A bank's art for an 8-plane screen (stdl_responsive.h; docs/format.md
 * section 6.1). On one, STDL_LoadSurface, STDL_LoadSprite,
 * STDL_LoadTileset and STDL_LoadPalette look here first, and take the
 * 4-plane chunk where the bank has no 8-plane one. A surface8 chunk (a
 * sprite8 is one with a frame width in front, a tileset8 one with a
 * tile size) holds an id's art in one of two forms:
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
 * planes 4-7 over a 4-plane chunk's cells, a sprite's frames or a
 * tileset's tiles (docs/format.md section 3): each cell its rows of
 * groups, a mask (where `masked`) and four planes a group, the cells
 * across the art and then down, as they were cut from it. The mask is
 * all the cut reads (create_mask has set the key's flag): the surface
 * goes once the frames or the tiles are made
 */
static void a8_cells(STDL_Surface *s, int gc, int ch, int masked,
                     const uint16_t *hi, const uint16_t *lo)
{
    const int gs = s->stride >> 4, gw = s->w >> 4;
    int cy, g0, y, g;

    for (cy = 0; cy < s->h; cy += ch) {
        for (g0 = 0; g0 < gw; g0 += gc) {
            for (y = cy; y < cy + ch; y++) {
                uint16_t *d = (uint16_t *)(s->pixels
                                           + stdl_row_off(y, s->stride))
                            + (g0 << 3);
                uint16_t *m = masked
                    ? (uint16_t *)(s->mask + stdl_row_off(y, s->maskstride))
                      + g0
                    : NULL;
                const uint16_t *h4 = hi + (stdl_row_off(y, (uint16_t)gs) << 2)
                                   + (g0 << 2);

                for (g = 0; g < gc; g++, d += 8, h4 += 4) {
                    if (masked) {
                        m[g] = *lo++;
                    }
                    d[0] = lo[0]; d[1] = lo[1]; d[2] = lo[2]; d[3] = lo[3];
                    d[4] = h4[0]; d[5] = h4[1]; d[6] = h4[2]; d[7] = h4[3];
                    lo += 4;
                }
            }
        }
    }
}

/* over a 4-plane sprite chunk: one variant of 4 planes, frames a whole
 * number of groups, and both chunks the same strip */
static int a8_over_sprite(STDL_Surface *s, int fw, const uint16_t *hi,
                          uint8_t *lop, uint32_t lolen)
{
    const int gf = fw >> 4, gs = s->stride >> 4;
    const uint16_t *lo;
    int nf;

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
    a8_cells(s, gf, s->h, 1, hi, lo);
    return 0;
}

/* over a 4-plane tileset chunk: 4 planes, tiles of the same size, and
 * as many as the art holds, which they fill */
static int a8_over_tiles(STDL_Surface *s, int tw, int th, const uint16_t *hi,
                         uint8_t *lop, uint32_t lolen)
{
    const int gt = tw >> 4, masked = lolen >= 14 && lop[8] != 0;
    const unsigned sw = masked ? 5 : 4;
    const unsigned cols = STDL_DivU16((uint16_t)s->w, (unsigned)tw);
    const unsigned rows = STDL_DivU16((uint16_t)s->h, (unsigned)th);
    const uint16_t *lo;

    if (lolen < 14 || lop[9] != 4 || stdl_rd16(lop) != tw
        || stdl_rd16(lop + 2) != th || stdl_rd16(lop + 6) != gt
        || STDL_MulU16(cols, (unsigned)tw) != (uint32_t)s->w
        || STDL_MulU16(rows, (unsigned)th) != (uint32_t)s->h
        || stdl_rd16(lop + 4) != STDL_MulU16(cols, rows)
        || stdl_rd32(lop + 10)
           != stdl_row_off(th, (uint16_t)STDL_MulU16((unsigned)gt, sw))
        || (lo = a8_words(lop, lolen, 14,
                          stdl_row_off(s->h, (uint16_t)STDL_MulU16(
                              (unsigned)(s->w >> 4), sw << 1)))) == NULL
        || (masked && stdl_resp->create_mask(s, 0) < 0)) {
        STDL_SetError("bad 8-plane tileset chunk");
        return -1;
    }
    a8_cells(s, gt, th, masked, hi, lo);
    return 0;
}

/*
 * The 8-plane surface a surface8 payload holds (a sprite8's or a
 * tileset8's after its cell size, cw by ch, its frames or tiles cut from
 * it): its own rows, written straight in as STDL_LoadSurface writes 4
 * planes, or planes 4-7 over the 4-plane chunk of the same id, of
 * `type`
 */
static STDL_Surface *a8_art(const char *bank, int id, uint8_t *p,
                            uint32_t len, int type, int cw, int ch,
                            stdl_chunk_fn read)
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
               || (lop = read(bank, type, id, &lolen)) == NULL) {
        ok = -1;
    } else {
        ok = type == CHUNK_SURFACE ? a8_over_surface(s, hi, lop, lolen)
           : type == CHUNK_SPRITE ? a8_over_sprite(s, cw, hi, lop, lolen)
           : a8_over_tiles(s, cw, ch, hi, lop, lolen);
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
            r = a8_art(bank, id, p, *len, type, 0, 0, read);
        }
    } else {
        /* a sprite8's frame width, or a tileset8's tile size, then the
         * art the frames or the tiles are cut from */
        const int tiles = type == CHUNK_TILESET;
        const uint32_t at = tiles ? 4 : 2;
        int cw, ch;

        p = read(bank, tiles ? CHUNK_TILESET8 : CHUNK_SPRITE8, id, len);
        if (p != NULL && *len >= at) {
            cw = stdl_rd16(p);
            ch = tiles ? stdl_rd16(p + 2) : 0;
            if (tiles && (cw == 0 || (cw & 15) != 0 || ch == 0)) {
                STDL_SetError("bad 8-plane tileset chunk");
            } else if ((s = a8_art(bank, id, p + at, *len - at, type, cw, ch,
                                   read)) != NULL) {
                r = tiles ? (void *)stdl_resp->tileset_from(s, cw, ch)
                          : (void *)stdl_resp->sprite_from(s, cw, flags);
                stdl_resp->free_surface(s);
            }
        }
    }
    free(p);
    return r;
}

const resp_banks stdl_resp_banks = { a8_load };
