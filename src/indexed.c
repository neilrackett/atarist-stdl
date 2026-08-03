/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * Source art that is not planar yet: the 1bpp and byte-per-pixel
 * conversions a port would otherwise write by hand, and the colour
 * remap that turns one conversion into as many recoloured variants
 * as the game wants.
 *
 * All load-time paths, and all here rather than in surface.c because
 * the archive links whole objects and surface.o is in every program:
 * a port whose art is already planar must not carry them.
 */

#include <stdlib.h>
#include "stdl_internal.h"

/*
 * 1bpp row-major bits (MSB first) to planar: set bits get colour fg,
 * clear bits colour bg. bytes_per_row is (w + 7) / 8.
 */
STDL_Surface *STDL_SurfaceFrom1bpp(const uint8_t *bits, int w, int h,
                                   uint8_t fg, uint8_t bg)
{
    STDL_Surface *s = STDL_CreateSurface(w, h);
    const uint8_t *src;
    uint8_t *rowp;
    int bpr, y, g, groups, p;

    if (s == NULL) {
        return NULL;
    }
    fg &= 15;
    bg &= 15;
    bpr = (w + 7) / 8;
    groups = s->stride / 8;
    src = bits;
    rowp = s->pixels;
    for (y = 0; y < h; y++) {
        uint16_t *row = (uint16_t *)rowp;
        for (g = 0; g < groups; g++) {
            uint16_t v = 0;
            int b0 = g * 2, b1 = g * 2 + 1;
            if (b0 < bpr) v = (uint16_t)(src[b0] << 8);
            if (b1 < bpr) v |= src[b1];
            for (p = 0; p < stdl_planes; p++) {
                uint16_t word = 0;
                if (fg & (1 << p)) word |= v;
                if (bg & (1 << p)) word |= (uint16_t)~v;
                row[g * 4 + p] = word;
            }
        }
        src += bpr;
        rowp += s->stride;
    }
    return s;
}

/*
 * Chunky (one byte per pixel) to planar. `stride` is the source row
 * pitch in bytes - pass w for tightly packed data, or the width of a
 * larger sheet to lift one frame out of it. `keycolour` is a SOURCE
 * byte value that becomes transparent; pass -1 (or anything outside
 * 0..255) for a fully opaque surface with no mask.
 *
 * Source bytes are ST palette indices. Values are truncated to the
 * plane budget like every other entry point that writes pixels, so
 * the "planes above the budget are zero" invariant holds however odd
 * the source data is. Padding pixels between w and the 16-pixel group
 * boundary are colour 0, and transparent when the surface is keyed.
 */
STDL_Surface *STDL_SurfaceFromIndexed8(const uint8_t *bytes, int w,
                                       int h, int stride,
                                       int keycolour)
{
    STDL_Surface *s;
    const uint8_t *src;
    uint8_t *rowp, *mrowp;
    int keyed = (keycolour >= 0 && keycolour <= 255);
    int groups, y, g, i;

    if (bytes == NULL) {
        STDL_SetError("no indexed pixel data");
        return NULL;
    }
    if (stride <= 0) {
        stride = w;
    }
    s = STDL_CreateSurface(w, h);
    if (s == NULL) {
        return NULL;
    }
    if (keyed && STDL_CreateMask(s, 1) < 0) {
        STDL_FreeSurface(s);
        return NULL;
    }
    groups = s->stride / 8;
    src = bytes;
    rowp = s->pixels;
    mrowp = s->mask;

    for (y = 0; y < h; y++) {
        uint16_t *row = (uint16_t *)rowp;
        uint16_t *mrow = (uint16_t *)mrowp;

        for (g = 0; g < groups; g++) {
            uint16_t p0 = 0, p1 = 0, p2 = 0, p3 = 0, m = 0;
            uint16_t bit = 0x8000u;
            int x = g * 16;

            for (i = 0; i < 16; i++, x++, bit = (uint16_t)(bit >> 1)) {
                uint8_t v;

                if (x >= w) {
                    m |= bit;               /* padding: never drawn */
                    continue;
                }
                v = src[x];
                if (keyed && v == (uint8_t)keycolour) {
                    m |= bit;
                    continue;
                }
                if (v & 1) p0 |= bit;
                if (v & 2) p1 |= bit;
                if (v & 4) p2 |= bit;
                if (v & 8) p3 |= bit;
            }
            stdl_put_planes(row, p0, p1, p2, p3, stdl_planes);
            if (mrow != NULL) {
                mrow[g] = m;
            }
            row += 4;
        }
        rowp += s->stride;
        if (mrowp != NULL) {
            mrowp += s->maskstride;
        }
        src += stride;
    }
    if (keyed) {
        /* the mask is built, not scanned for: the same state
         * STDL_SetColourKey(s, 1, STDL_TRANSPARENT) leaves behind */
        s->colourkey = STDL_TRANSPARENT;
    }
    return s;
}

/*
 * Recolour a surface through a 16-entry palette map: every pixel of
 * index c becomes map[c].
 *
 * This is the answer to "the same artwork in team colours", and it
 * deliberately happens once, into the pixels, rather than at blit
 * time. A blit-time remap on a 68000 cannot be cheap: the source
 * index of sixteen pixels is spread across four plane words, so
 * picking bit q of map[c] per pixel means intersecting the four
 * planes into a mask per source colour (~40-60 register operations
 * per group against the five a plain masked sprite blit costs), and
 * a sprite blit is the hottest loop most ports have. Paying it once
 * per variant at load time costs nothing per frame and lets the
 * result go through the ordinary aligned and pre-shifted paths.
 *
 * The mask is not touched: which pixels are transparent does not
 * change, only what colour the visible ones are. Colours above the
 * plane budget are truncated as everywhere else, and the loop only
 * enumerates the indices the budget can hold - the planes above it
 * are zero, so no other source index exists.
 */
void STDL_RemapSurface(STDL_Surface *s, const uint8_t map[16])
{
    uint8_t *rowp;
    int groups, ncols, y, g, c;

    if (s == NULL || map == NULL) {
        return;
    }
    groups = s->stride / 8;
    ncols = 1 << stdl_planes;
    rowp = s->pixels;

    for (y = 0; y < s->h; y++) {
        uint16_t *grp = (uint16_t *)rowp;

        for (g = 0; g < groups; g++) {
            uint16_t p0 = grp[0], p1 = grp[1];
            uint16_t p2 = grp[2], p3 = grp[3];
            uint16_t o0 = 0, o1 = 0, o2 = 0, o3 = 0;

            for (c = 0; c < ncols; c++) {
                uint8_t to = (uint8_t)(map[c] & STDL_COL_MASK);
                uint16_t t;

                if (to == 0) {
                    continue;       /* contributes to no plane */
                }
                t = (uint16_t)((c & 1) ? p0 : ~p0);
                t &= (uint16_t)((c & 2) ? p1 : ~p1);
                t &= (uint16_t)((c & 4) ? p2 : ~p2);
                t &= (uint16_t)((c & 8) ? p3 : ~p3);
                if (t == 0) {
                    continue;
                }
                if (to & 1) o0 |= t;
                if (to & 2) o1 |= t;
                if (to & 4) o2 |= t;
                if (to & 8) o3 |= t;
            }
            stdl_put_planes(grp, o0, o1, o2, o3, stdl_planes);
            grp += 4;
        }
        rowp += s->stride;
    }
    /* opaque_state stays valid: the mask is untouched */
}
