/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * Bitmap-font text: STDL_DrawText and freeing a font.
 *
 * Its own translation unit, out of sprite.c, because the linker's
 * granularity on this toolchain is the object file: a port that
 * draws sprites but renders no text (or only through STDL_DrawChar,
 * which lives in drawchar.c) no longer carries this code.
 */

#include <stdlib.h>
#include "stdl_internal.h"

void STDL_FreeFont(STDL_Font *font)
{
    if (font != NULL) {
        free(font->bits);
        free(font);
    }
}

/*
 * Draw text with a 1bpp cell font (cw <= 16). Set bits get colour
 * col; clear bits leave the destination untouched.
 *
 * Everything that does not vary per glyph row - the clip masks, the
 * group shift, the plane fill words and the row pointers - is
 * hoisted out of the row loop by hand: gcc 4.6 will not unswitch it,
 * and games that render a status bar a character at a time run this
 * inner loop thousands of times a frame. The plane budget is
 * hoisted the same way - the glyph loop is instantiated once per
 * budget, so a 4-colour game moves half the memory per glyph.
 */
STDL_PLANE_INLINE void draw_text_glyphs(uint8_t *pixels, int stride,
                   const STDL_Font *font, int x, int y,
                   const char *text,
                   uint16_t pw0, uint16_t pw1, uint16_t pw2,
                   uint16_t pw3, int row0, int row1,
                   int cx1, int cx2, const int np)
{
    int i, cw, ch, bpr;
    uint16_t widthmask, glyphsize;
    const uint8_t *bits0;
    uint8_t *rowbase;

    cw = font->cw;
    ch = font->ch;
    bpr = font->bytes_per_row;
    widthmask = (uint16_t)(0xFFFFu << (16 - cw));

    /*
     * Everything that does not depend on which glyph this is comes
     * out of the loop. That matters more than it looks: gcc 4.6 turns
     * a 32-bit multiply into a __mulsi3 call (~270 cycles measured on
     * an 8MHz 68000), and the obvious form of this loop makes four of
     * them per character - `(c - first) * bpr * ch` is two on its own.
     * What is left is one mulu.w: glyphsize is at most 16*32/8 and
     * c - first at most 255, so both operands fit 16 bits.
     */
    glyphsize = (uint16_t)(bpr * ch);
    bits0 = font->bits + stdl_row_off(row0, (uint16_t)bpr);
    rowbase = pixels + (uint32_t)(y + row0) * stride;

    for (i = 0; text[i] != '\0'; i++, x += cw) {
        uint8_t c = (uint8_t)text[i];
        const uint8_t *glyph;
        uint16_t clipmask;
        int shift, gx, cl, cr;
        uint16_t *g1w;

        if (c < font->first || c > font->last) {
            continue;
        }
        cl = cx1 - x;
        cr = (x + cw) - cx2;
        if (cl >= cw || cr >= cw) {
            continue;                       /* fully clipped away */
        }
        clipmask = widthmask;
        if (cl > 0) clipmask &= (uint16_t)(0xFFFFu >> cl);
        if (cr > 0) clipmask &= (uint16_t)(0xFFFFu << (16 - cw + cr));
        if (clipmask == 0) {
            continue;
        }

        shift = x & 15;
        gx = x >> 4;
        glyph = bits0 + stdl_row_off(c - font->first, glyphsize);
        g1w = (uint16_t *)(rowbase + gx * 8);

        stdl_glyph_rows((uint8_t *)g1w, stride, glyph, bpr, row1 - row0,
                        clipmask, shift, pw0, pw1, pw2, pw3, np);
    }
}

void STDL_DrawText(STDL_Surface *dst, const STDL_Font *font,
                   int x, int y, const char *text, uint8_t col)
{
    int row0, row1, cx1, cx2, np;
    uint16_t pw0, pw1, pw2, pw3;

    if (dst == NULL || font == NULL || text == NULL
        || font->cw > 16 || font->cw <= 0) {
        return;
    }
    col &= STDL_COL_MASK;
    pw0 = (col & 1) ? 0xFFFFu : 0;
    pw1 = (col & 2) ? 0xFFFFu : 0;
    pw2 = (col & 4) ? 0xFFFFu : 0;
    pw3 = (col & 8) ? 0xFFFFu : 0;

    /* vertical clip is the same for every glyph on the line */
    row0 = 0;
    row1 = font->ch;
    if (y < dst->clip.y) row0 = dst->clip.y - y;
    if (y + row1 > dst->clip.y + dst->clip.h)
        row1 = dst->clip.y + dst->clip.h - y;
    if (row0 >= row1) {
        return;
    }
    cx1 = dst->clip.x;
    cx2 = dst->clip.x + dst->clip.w;

    np = stdl_planes;
#define TEXT_GLYPHS(np) \
    draw_text_glyphs(dst->pixels, dst->stride, font, x, y, text, \
                     pw0, pw1, pw2, pw3, row0, row1, cx1, cx2, (np))
    STDL_PLANE_DISPATCH(np, TEXT_GLYPHS);
#undef TEXT_GLYPHS
}
