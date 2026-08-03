/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * STDL_DrawChar: one glyph, without the string.
 *
 * Ported CGA and VGA text code draws a character at a time - a status
 * bar, a score, a menu - and reaching STDL_DrawText from there means
 * building a two-byte string and paying a whole string call's setup
 * for one glyph. Sopwith does that 350 times a frame, and once the
 * plane budget had halved the memory traffic the setup was what was
 * left: the colour expanded into four plane words, the vertical clip
 * solved, and two 32-bit multiplies that gcc 4.6 turns into __mulsi3
 * calls at ~270 cycles each.
 *
 * This is the same glyph loop with all of that collapsed: the
 * multiplies are 16x16 mulu.w forms, and the horizontal clip is one
 * comparison against the common case (a glyph wholly inside the clip
 * rectangle) instead of the general per-glyph mask.
 *
 * Its own translation unit because the archive links whole objects:
 * a program that only ever draws strings must not carry this.
 */

#include "stdl_internal.h"

/*
 * Draw the glyph rows. Instantiated once per plane budget so the
 * merges unroll with a constant plane count, exactly as
 * STDL_DrawText's loop is.
 */
STDL_PLANE_INLINE void draw_char_rows(uint8_t *g1p, int stride,
                   const uint8_t *glyph, int bpr, int rows,
                   uint16_t clipmask, int shift,
                   uint16_t pw0, uint16_t pw1, uint16_t pw2,
                   uint16_t pw3, const int np)
{
    int wstride = stride >> 1;
    uint16_t *g1w = (uint16_t *)g1p;
    uint16_t *g2w = g1w + 4;
    int row;

    for (row = 0; row < rows;
         row++, glyph += bpr, g1w += wstride, g2w += wstride) {
        uint16_t bits = (uint16_t)(glyph[0] << 8);
        uint16_t hi, lo;

        if (bpr > 1) {
            bits |= glyph[1];
        }
        bits &= clipmask;
        if (bits == 0) {
            continue;
        }
        hi = (uint16_t)(bits >> shift);
        lo = (shift != 0) ? (uint16_t)(bits << (16 - shift)) : 0;

        if (hi != 0) {
            stdl_merge_planes(g1w, hi, pw0, pw1, pw2, pw3, np);
        }
        if (lo != 0) {
            stdl_merge_planes(g2w, lo, pw0, pw1, pw2, pw3, np);
        }
    }
}

void STDL_DrawChar(STDL_Surface *dst, const STDL_Font *font,
                   int x, int y, int ch, uint8_t col)
{
    const uint8_t *glyph;
    uint8_t *g1p;
    uint16_t clipmask, glyphsize;
    uint16_t pw0, pw1, pw2, pw3;
    int cw, ch_rows, bpr, row0, row1, cl, cr, np;

    if (dst == NULL || font == NULL) {
        return;
    }
    cw = font->cw;
    if (cw <= 0 || cw > 16) {
        return;
    }
    if (ch < font->first || ch > font->last) {
        return;
    }

    /* horizontal clip: the cell either fits or is trimmed to a mask */
    cl = dst->clip.x - x;
    cr = (x + cw) - (dst->clip.x + dst->clip.w);
    if (cl >= cw || cr >= cw) {
        return;
    }
    clipmask = (uint16_t)(0xFFFFu << (16 - cw));
    if (cl > 0) {
        clipmask &= (uint16_t)(0xFFFFu >> cl);
    }
    if (cr > 0) {
        clipmask &= (uint16_t)(0xFFFFu << (16 - cw + cr));
    }
    if (clipmask == 0) {
        return;
    }

    /* vertical clip */
    ch_rows = font->ch;
    row0 = 0;
    row1 = ch_rows;
    if (y < dst->clip.y) {
        row0 = dst->clip.y - y;
    }
    if (y + row1 > dst->clip.y + dst->clip.h) {
        row1 = dst->clip.y + dst->clip.h - y;
    }
    if (row0 >= row1) {
        return;
    }

    col &= STDL_COL_MASK;
    pw0 = (col & 1) ? 0xFFFFu : 0;
    pw1 = (col & 2) ? 0xFFFFu : 0;
    pw2 = (col & 4) ? 0xFFFFu : 0;
    pw3 = (col & 8) ? 0xFFFFu : 0;

    /*
     * Glyph and row addresses without a single __mulsi3: glyphsize is
     * at most 16 rows-worth of a 16-pixel cell and the character
     * index at most 255, so both products are 16x16 mulu.w; the row
     * offset goes through the shared stdl_row_off for the same
     * reason.
     */
    bpr = font->bytes_per_row;
    glyphsize = (uint16_t)(bpr * ch_rows);
    glyph = font->bits
          + (uint32_t)((uint16_t)(ch - font->first) * glyphsize)
          + (uint32_t)((uint16_t)row0 * (uint16_t)bpr);
    g1p = dst->pixels + stdl_row_off(y + row0, dst->stride)
        + (x >> 4) * 8;

    np = stdl_planes;
#define CHAR_ROWS(np) \
    draw_char_rows(g1p, dst->stride, glyph, bpr, row1 - row0, \
                   clipmask, x & 15, pw0, pw1, pw2, pw3, (np))
    STDL_PLANE_DISPATCH(np, CHAR_ROWS);
#undef CHAR_ROWS
}
