/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * STDL_Draw: span-based primitives. Every shape decomposes to
 * horizontal spans; a span writes whole plane words with edge masks.
 */

#include <stddef.h>
#include <string.h>
#include "stdl_internal.h"

/*
 * Generic CPU span fill, instantiated once per plane budget so the
 * per-group plane writes unroll (gcc 4.6 will not unswitch them).
 * The plane words arrive as scalars, not an array: an array
 * parameter escapes and is then reloaded after every destination
 * store.
 */
STDL_PLANE_INLINE void fill_span_rows(uint8_t *row, uint8_t *mrow,
                                      int stride, int maskstride,
                                      int g0, int g1, int rows,
                                      uint16_t pw0, uint16_t pw1,
                                      uint16_t pw2, uint16_t pw3,
                                      uint16_t lm, uint16_t rm,
                                      int transparent, const int np)
{
    int g, y;

    for (y = 0; y < rows; y++) {
        uint16_t *grp = (uint16_t *)(row + g0 * 8);

        if (lm == 0xFFFFu) {
            stdl_put_planes(grp, pw0, pw1, pw2, pw3, np);
        } else {
            stdl_merge_planes(grp, lm, pw0, pw1, pw2, pw3, np);
        }
        /* walking pointer, whole longs where the budget pairs up:
         * recomputing row + g * 8 per group costs more than the
         * stores do */
        if (g1 > g0 + 1) {
            uint8_t *mid = row + (g0 + 1) * 8;
            int n = g1 - g0 - 1;

            if (np == 4 && ((uintptr_t)mid & 3) == 0) {
                uint32_t l01 = STDL_PACK2(pw0, pw1);
                uint32_t l23 = STDL_PACK2(pw2, pw3);
                uint32_t *lp = (uint32_t *)mid;
                while (n--) {
                    *lp++ = l01;
                    *lp++ = l23;
                }
            } else if (np == 2 && ((uintptr_t)mid & 3) == 0) {
                uint32_t l01 = STDL_PACK2(pw0, pw1);
                uint32_t *lp = (uint32_t *)mid;
                while (n--) {
                    *lp = l01;
                    lp += 2;
                }
            } else {
                uint16_t *wp = (uint16_t *)mid;
                while (n--) {
                    stdl_put_planes(wp, pw0, pw1, pw2, pw3, np);
                    wp += 4;
                }
            }
        }
        if (g1 != g0) {
            grp = (uint16_t *)(row + g1 * 8);
            if (rm == 0xFFFFu) {
                stdl_put_planes(grp, pw0, pw1, pw2, pw3, np);
            } else {
                stdl_merge_planes(grp, rm, pw0, pw1, pw2, pw3, np);
            }
        }

        if (mrow != NULL) {
            uint16_t *mw = (uint16_t *)mrow;
            for (g = g0; g <= g1; g++) {
                uint16_t m = 0xFFFFu;
                if (g == g0) m &= lm;
                if (g == g1) m &= rm;
                if (transparent) {
                    mw[g] |= m;
                } else {
                    mw[g] &= (uint16_t)~m;
                }
            }
            mrow += maskstride;
        }
        row += stride;
    }
}

/*
 * Core rect fill: rows [y1, y2] over storage-space span [x1, x2],
 * all inclusive and pre-clipped. Edge masks, plane words and the
 * row split are computed once, not per row. Handles the surface
 * mask (opaque fills clear it, transparent fills set it).
 */
static void fill_rows(STDL_Surface *s, int x1, int x2, int y1,
                      int y2, uint8_t col, int transparent)
{
    uint8_t *row = s->pixels + (uint32_t)y1 * s->stride;
    uint8_t *mrow = (s->mask != NULL)
        ? s->mask + (uint32_t)y1 * s->maskstride : NULL;
    int g0 = x1 >> 4, g1 = x2 >> 4, p, y;
    int ng = g1 - g0 + 1;
    int rows = y2 - y1 + 1;
    int np = stdl_planes;
    uint16_t lm = (uint16_t)(0xFFFFu >> (x1 & 15));
    uint16_t rm = (uint16_t)(0xFFFFu << (15 - (x2 & 15)));
    uint16_t pw[4];

    if (g0 == g1) {
        lm &= rm;
        rm = lm;
    }
    for (p = 0; p < 4; p++) {
        pw[p] = (col & (1 << p)) ? 0xFFFFu : 0;
    }

    if (ng * rows >= STDL_BLIT_FILL_MIN_CELLS
        && stdl_blitter_active()) {
        uintptr_t base = (uintptr_t)(row + g0 * 8);
        int16_t yinc = (int16_t)(s->stride - (ng - 1) * 8);

        for (p = 0; p < np; p++) {
            /* HOP all-ones: OP_SRC writes ones, OP_ZERO zeros */
            stdl_blitter_go(0, 0, 0, base + (uintptr_t)(p * 2),
                            8, yinc, lm, rm,
                            (uint16_t)ng, (uint16_t)rows,
                            STDL_BLIT_HOP_ONES,
                            pw[p] ? STDL_BLIT_OP_SRC
                                  : STDL_BLIT_OP_ZERO);
        }
        if (mrow != NULL) {
            stdl_blitter_go(0, 0, 0, (uintptr_t)(mrow + g0 * 2),
                            2, (int16_t)(s->maskstride - (ng - 1) * 2),
                            lm, rm, (uint16_t)ng, (uint16_t)rows,
                            STDL_BLIT_HOP_ONES,
                            transparent ? STDL_BLIT_OP_SRC
                                        : STDL_BLIT_OP_ZERO);
            s->opaque_state = 0;
        }
        return;
    }

    /*
     * Whole-group fill of a colour whose plane words are all equal
     * (0 or 15 - the screen clear every frame) is a plain byte fill,
     * and collapses to a single memset when the span covers entire
     * scanlines. This is the difference between ~50ms and ~12ms for
     * a full-screen clear on an 8MHz ST with no BLiTTER.
     *
     * Colour 0 keeps this path at any plane budget: the bytes it
     * writes to the out-of-budget planes are the zeros those planes
     * already hold, and one memset still beats a strided loop over
     * half the data. Colour 15 only has all four plane words equal
     * when all four planes are in budget.
     */
    if (lm == 0xFFFFu && rm == 0xFFFFu
        && (col == 0 || (col == 15 && np == 4))) {
        int fb = (col == 0) ? 0x00 : 0xFF;
        uint32_t span = (uint32_t)ng * 8;

        if (span == s->stride) {
            memset(row, fb, span * (uint32_t)rows);
        } else {
            uint8_t *r = row + g0 * 8;
            for (y = y1; y <= y2; y++) {
                memset(r, fb, span);
                r += s->stride;
            }
        }
        if (mrow != NULL) {
            uint32_t mspan = (uint32_t)ng * 2;
            int mb = transparent ? 0xFF : 0x00;
            if (mspan == s->maskstride) {
                memset(mrow, mb, mspan * (uint32_t)rows);
            } else {
                uint8_t *m = mrow + g0 * 2;
                for (y = y1; y <= y2; y++) {
                    memset(m, mb, mspan);
                    m += s->maskstride;
                }
            }
            s->opaque_state = 0;
        }
        return;
    }

#define FILL_SPAN(np) \
    fill_span_rows(row, mrow, s->stride, s->maskstride, g0, g1, \
                   rows, pw[0], pw[1], pw[2], pw[3], lm, rm, \
                   transparent, (np))
    STDL_PLANE_DISPATCH(np, FILL_SPAN);
#undef FILL_SPAN

    if (s->mask != NULL) {
        s->opaque_state = 0;
    }
}

/*
 * SDL 1.2 semantics: the rectangle is translated by the surface
 * origin, clipped, and the final rectangle is written back (in
 * logical coordinates). NULL fills the whole clip rect.
 */
void STDL_FillRect(STDL_Surface *dst, STDL_Rect *r, uint8_t col)
{
    int x1, y1, x2, y2;
    int transparent;

    if (dst == NULL) {
        return;
    }
    if (r == NULL) {
        x1 = dst->clip.x;
        y1 = dst->clip.y;
        x2 = dst->clip.x + dst->clip.w - 1;
        y2 = dst->clip.y + dst->clip.h - 1;
    } else {
        x1 = r->x - dst->org_x;
        y1 = r->y - dst->org_y;
        x2 = x1 + r->w - 1;
        y2 = y1 + r->h - 1;
        if (x1 < dst->clip.x) x1 = dst->clip.x;
        if (y1 < dst->clip.y) y1 = dst->clip.y;
        if (x2 > dst->clip.x + dst->clip.w - 1)
            x2 = dst->clip.x + dst->clip.w - 1;
        if (y2 > dst->clip.y + dst->clip.h - 1)
            y2 = dst->clip.y + dst->clip.h - 1;
        if (x1 > x2 || y1 > y2) {
            r->w = 0;
            r->h = 0;
            return;
        }
        r->x = (int16_t)(x1 + dst->org_x);
        r->y = (int16_t)(y1 + dst->org_y);
        r->w = (uint16_t)(x2 - x1 + 1);
        r->h = (uint16_t)(y2 - y1 + 1);
    }
    if (x1 > x2 || y1 > y2) {
        return;
    }
    /* STDL_TRANSPARENT punches a hole in a masked surface; on a
     * maskless surface it degrades to colour 0 */
    transparent = (col >= STDL_TRANSPARENT && dst->mask != NULL);
    col &= 15;
    if (transparent) {
        col = 0;
    }
    fill_rows(dst, x1, x2, y1, y2, col, transparent);
}

void STDL_HLine(STDL_Surface *dst, int x1, int x2, int y, uint8_t col)
{
    int t;

    if (dst == NULL) {
        return;
    }
    if (x1 > x2) { t = x1; x1 = x2; x2 = t; }
    if (y < dst->clip.y || y >= dst->clip.y + dst->clip.h) {
        return;
    }
    if (x1 < dst->clip.x) x1 = dst->clip.x;
    if (x2 >= dst->clip.x + dst->clip.w)
        x2 = dst->clip.x + dst->clip.w - 1;
    if (x1 > x2) {
        return;
    }
    {
        int transparent = (col >= STDL_TRANSPARENT
                           && dst->mask != NULL);
        fill_rows(dst, x1, x2, y, y, (uint8_t)(transparent ? 0 : col & 15),
                  transparent);
    }
}

/* set/clear one pixel's bit in the low np planes of a group */
STDL_PLANE_INLINE void put_bit_planes(uint16_t *grp, uint8_t col,
                                      uint16_t bit, const int np)
{
    uint16_t nb = (uint16_t)~bit;

    grp[0] = (col & 1) ? (uint16_t)(grp[0] | bit)
                       : (uint16_t)(grp[0] & nb);
    if (np > 1) grp[1] = (col & 2) ? (uint16_t)(grp[1] | bit)
                                   : (uint16_t)(grp[1] & nb);
    if (np > 2) grp[2] = (col & 4) ? (uint16_t)(grp[2] | bit)
                                   : (uint16_t)(grp[2] & nb);
    if (np > 3) grp[3] = (col & 8) ? (uint16_t)(grp[3] | bit)
                                   : (uint16_t)(grp[3] & nb);
}

STDL_PLANE_INLINE void vline_rows(uint8_t *base, uint8_t *mbase,
                                  int stride, int maskstride,
                                  int rows, uint8_t col, uint16_t bit,
                                  int transparent, const int np)
{
    int y;

    for (y = 0; y < rows; y++) {
        put_bit_planes((uint16_t *)base, col, bit, np);
        if (mbase != NULL) {
            if (transparent) {
                *(uint16_t *)mbase |= bit;
            } else {
                *(uint16_t *)mbase &= (uint16_t)~bit;
            }
            mbase += maskstride;
        }
        base += stride;
    }
}

void STDL_VLine(STDL_Surface *dst, int x, int y1, int y2, uint8_t col)
{
    int t;
    uint16_t bit;
    uint8_t *base;

    if (dst == NULL) {
        return;
    }
    if (y1 > y2) { t = y1; y1 = y2; y2 = t; }
    if (x < dst->clip.x || x >= dst->clip.x + dst->clip.w) {
        return;
    }
    if (y1 < dst->clip.y) y1 = dst->clip.y;
    if (y2 >= dst->clip.y + dst->clip.h)
        y2 = dst->clip.y + dst->clip.h - 1;
    if (y1 > y2) {
        return;
    }
    {
        int transparent = (col >= STDL_TRANSPARENT
                           && dst->mask != NULL);
        uint8_t *mbase = (dst->mask != NULL)
            ? dst->mask + (uint32_t)y1 * dst->maskstride
              + ((x >> 4) * 2)
            : NULL;

        int np = stdl_planes;

        col = transparent ? 0 : (uint8_t)(col & 15);
        bit = (uint16_t)(0x8000u >> (x & 15));
        base = dst->pixels + (uint32_t)y1 * dst->stride
             + ((x >> 4) * 8);
#define VLINE_ROWS(np) \
        vline_rows(base, mbase, dst->stride, dst->maskstride, \
                   y2 - y1 + 1, col, bit, transparent, (np))
        STDL_PLANE_DISPATCH(np, VLINE_ROWS);
#undef VLINE_ROWS
        if (dst->mask != NULL) {
            dst->opaque_state = 0;
        }
    }
}

void STDL_PutPixel(STDL_Surface *dst, int x, int y, uint8_t col)
{
    uint16_t *grp;
    uint16_t bit;
    int np, transparent;

    if (dst == NULL
        || x < dst->clip.x || x >= dst->clip.x + dst->clip.w
        || y < dst->clip.y || y >= dst->clip.y + dst->clip.h) {
        return;
    }
    transparent = (col >= STDL_TRANSPARENT && dst->mask != NULL);
    col = transparent ? 0 : (uint8_t)(col & 15);
    bit = (uint16_t)(0x8000u >> (x & 15));
    grp = (uint16_t *)(dst->pixels + (uint32_t)y * dst->stride
                       + ((x >> 4) * 8));
    np = stdl_planes;
#define PUT_BITS(np) put_bit_planes(grp, col, bit, (np))
    STDL_PLANE_DISPATCH(np, PUT_BITS);
#undef PUT_BITS
    if (dst->mask != NULL) {
        uint16_t *m = (uint16_t *)(dst->mask
            + (uint32_t)y * dst->maskstride) + (x >> 4);
        if (transparent) {
            *m |= bit;
        } else {
            *m &= (uint16_t)~bit;
        }
        dst->opaque_state = 0;
    }
}

uint8_t STDL_GetPixel(const STDL_Surface *src, int x, int y)
{
    const uint16_t *grp;
    uint16_t bit;
    uint8_t col = 0;

    if (src == NULL || x < 0 || x >= src->w || y < 0 || y >= src->h) {
        return 0;
    }
    bit = (uint16_t)(0x8000u >> (x & 15));
    grp = (const uint16_t *)(src->pixels + (uint32_t)y * src->stride
                             + ((x >> 4) * 8));
    if (grp[0] & bit) col |= 1;
    if (grp[1] & bit) col |= 2;
    if (grp[2] & bit) col |= 4;
    if (grp[3] & bit) col |= 8;
    return col;
}

/* ---------------------------------------------------------------- */
/* XOR raster op                                                    */

/*
 * XOR one group's plane words with m. The colour's bits arrive as
 * four scalars so the tests are loop-invariant branches, not an
 * indexed loop: planes whose bit is clear are never touched, which
 * is both the XOR identity and the plane budget (col comes in
 * masked with STDL_COL_MASK, so out-of-budget planes never appear).
 */
STDL_PLANE_INLINE void xor_group(uint16_t *grp, uint16_t m,
                                 int c0, int c1, int c2, int c3)
{
    if (c0) grp[0] ^= m;
    if (c1) grp[1] ^= m;
    if (c2) grp[2] ^= m;
    if (c3) grp[3] ^= m;
}

/* one row of a horizontal span: groups g0..g1 with edge masks */
STDL_PLANE_INLINE void xor_row_groups(uint8_t *row, int g0, int g1,
                                      uint16_t lm, uint16_t rm,
                                      int c0, int c1, int c2, int c3)
{
    int g;

    xor_group((uint16_t *)(row + g0 * 8), lm, c0, c1, c2, c3);
    for (g = g0 + 1; g < g1; g++) {
        xor_group((uint16_t *)(row + g * 8), 0xFFFFu, c0, c1, c2, c3);
    }
    if (g1 != g0) {
        xor_group((uint16_t *)(row + g1 * 8), rm, c0, c1, c2, c3);
    }
}

/* the same row's mask words: XOR marks what it touches opaque */
STDL_PLANE_INLINE void xor_mask_row(uint8_t *mrow, int g0, int g1,
                                    uint16_t lm, uint16_t rm)
{
    uint16_t *mw = (uint16_t *)mrow;
    int g;

    mw[g0] &= (uint16_t)~lm;
    for (g = g0 + 1; g < g1; g++) {
        mw[g] = 0;
    }
    if (g1 != g0) {
        mw[g1] &= (uint16_t)~rm;
    }
}

/*
 * Core XOR fill: rows [y1, y2] over storage-space span [x1, x2],
 * all inclusive and pre-clipped. Mirrors fill_rows, except planes
 * whose colour bit is clear are not touched at all (XOR with 0 is
 * the identity) and the edge masks fold into the per-plane words.
 * Always CPU: the shapes worth XORing are small, and keeping the
 * blitter out of it means BLITCHK's fill/blit invariant still
 * describes every accelerated path.
 */
static void xor_rows(STDL_Surface *s, int x1, int x2, int y1,
                     int y2, uint8_t col)
{
    uint16_t stride = s->stride;
    uint8_t *row = s->pixels + stdl_row_off(y1, stride);
    uint8_t *mrow = (s->mask != NULL)
        ? s->mask + stdl_row_off(y1, s->maskstride) : NULL;
    int g0 = x1 >> 4, g1 = x2 >> 4, y;
    int c0 = col & 1, c1 = col & 2, c2 = col & 4, c3 = col & 8;
    uint16_t lm = (uint16_t)(0xFFFFu >> (x1 & 15));
    uint16_t rm = (uint16_t)(0xFFFFu << (15 - (x2 & 15)));

    if (g0 == g1) {
        lm &= rm;
        rm = lm;
    }
    for (y = y1; y <= y2; y++) {
        xor_row_groups(row, g0, g1, lm, rm, c0, c1, c2, c3);
        if (mrow != NULL) {
            xor_mask_row(mrow, g0, g1, lm, rm);
            mrow += s->maskstride;
        }
        row += stride;
    }
    if (s->mask != NULL) {
        s->opaque_state = 0;
    }
}

void STDL_XorRect(STDL_Surface *dst, STDL_Rect *r, uint8_t col)
{
    int x1, y1, x2, y2;

    if (dst == NULL) {
        return;
    }
    if (r == NULL) {
        x1 = dst->clip.x;
        y1 = dst->clip.y;
        x2 = dst->clip.x + dst->clip.w - 1;
        y2 = dst->clip.y + dst->clip.h - 1;
    } else {
        x1 = r->x - dst->org_x;
        y1 = r->y - dst->org_y;
        x2 = x1 + r->w - 1;
        y2 = y1 + r->h - 1;
        if (x1 < dst->clip.x) x1 = dst->clip.x;
        if (y1 < dst->clip.y) y1 = dst->clip.y;
        if (x2 > dst->clip.x + dst->clip.w - 1)
            x2 = dst->clip.x + dst->clip.w - 1;
        if (y2 > dst->clip.y + dst->clip.h - 1)
            y2 = dst->clip.y + dst->clip.h - 1;
        if (x1 > x2 || y1 > y2) {
            r->w = 0;
            r->h = 0;
            return;
        }
        r->x = (int16_t)(x1 + dst->org_x);
        r->y = (int16_t)(y1 + dst->org_y);
        r->w = (uint16_t)(x2 - x1 + 1);
        r->h = (uint16_t)(y2 - y1 + 1);
    }
    col &= STDL_COL_MASK;
    if (col == 0 || x1 > x2 || y1 > y2) {
        return;
    }
    xor_rows(dst, x1, x2, y1, y2, col);
}

void STDL_XorHLine(STDL_Surface *dst, int x1, int x2, int y,
                   uint8_t col)
{
    int t;

    if (dst == NULL) {
        return;
    }
    if (x1 > x2) { t = x1; x1 = x2; x2 = t; }
    if (y < dst->clip.y || y >= dst->clip.y + dst->clip.h) {
        return;
    }
    if (x1 < dst->clip.x) x1 = dst->clip.x;
    if (x2 >= dst->clip.x + dst->clip.w)
        x2 = dst->clip.x + dst->clip.w - 1;
    col &= STDL_COL_MASK;
    if (x1 > x2 || col == 0) {
        return;
    }
    xor_rows(dst, x1, x2, y, y, col);
}

void STDL_XorVLine(STDL_Surface *dst, int x, int y1, int y2,
                   uint8_t col)
{
    int t, y, p;
    uint16_t bit;
    uint8_t *base;

    if (dst == NULL) {
        return;
    }
    if (y1 > y2) { t = y1; y1 = y2; y2 = t; }
    if (x < dst->clip.x || x >= dst->clip.x + dst->clip.w) {
        return;
    }
    if (y1 < dst->clip.y) y1 = dst->clip.y;
    if (y2 >= dst->clip.y + dst->clip.h)
        y2 = dst->clip.y + dst->clip.h - 1;
    col &= STDL_COL_MASK;
    if (y1 > y2 || col == 0) {
        return;
    }
    bit = (uint16_t)(0x8000u >> (x & 15));
    base = dst->pixels + stdl_row_off(y1, dst->stride) + ((x >> 4) * 8);

    /* Planes 0 and 1 are adjacent words, so the common two-plane
     * case is one long XOR per row instead of two word ones. */
    if ((col & 12) == 0 && (col & 3) == 3
        && ((uintptr_t)base & 3) == 0) {
        uint32_t lm = ((uint32_t)bit << 16) | bit;
        for (y = y1; y <= y2; y++) {
            *(uint32_t *)base ^= lm;
            base += dst->stride;
        }
    } else {
        for (y = y1; y <= y2; y++) {
            uint16_t *grp = (uint16_t *)base;
            for (p = 0; p < 4; p++) {
                if (col & (1 << p)) grp[p] ^= bit;
            }
            base += dst->stride;
        }
    }

    if (dst->mask != NULL) {
        uint8_t *mbase = dst->mask
                       + stdl_row_off(y1, dst->maskstride)
                       + ((x >> 4) * 2);
        for (y = y1; y <= y2; y++) {
            *(uint16_t *)mbase &= (uint16_t)~bit;
            mbase += dst->maskstride;
        }
        dst->opaque_state = 0;
    }
}

void STDL_XorPixel(STDL_Surface *dst, int x, int y, uint8_t col)
{
    uint16_t *grp;
    uint16_t bit;
    int p;

    if (dst == NULL
        || x < dst->clip.x || x >= dst->clip.x + dst->clip.w
        || y < dst->clip.y || y >= dst->clip.y + dst->clip.h) {
        return;
    }
    col &= STDL_COL_MASK;
    if (col == 0) {
        return;
    }
    bit = (uint16_t)(0x8000u >> (x & 15));
    grp = (uint16_t *)(dst->pixels + stdl_row_off(y, dst->stride)
                       + ((x >> 4) * 8));
    for (p = 0; p < 4; p++) {
        if (col & (1 << p)) grp[p] ^= bit;
    }
    if (dst->mask != NULL) {
        uint16_t *m = (uint16_t *)(dst->mask
            + stdl_row_off(y, dst->maskstride)) + (x >> 4);
        *m &= (uint16_t)~bit;
        dst->opaque_state = 0;
    }
}

/* ---------------------------------------------------------------- */
/* batched spans                                                    */

/*
 * A short span costs almost nothing to draw and a lot to call: the
 * register save, the clip rectangle, the colour dispatch and the
 * row-address multiply are all per call, not per pixel. The span
 * lists hand the whole field to the library at once so that work
 * happens once. Measured on an 8MHz ST replaying Sopwith's terrain
 * outline (320 columns, ~1.8 rows each, plane budget 2): one
 * STDL_XorVLine per column 47.5ms a frame, the same decomposition
 * batched 29.8ms, and with flat stretches folded into horizontal
 * spans 23.9ms - against 18.6ms for the same shape written
 * straight into the planes by hand.
 *
 * The list is walked with a pointer over a packed 6-byte struct;
 * gcc 4.6 spills anything carried in an array, so nothing is.
 */

/*
 * Vertical fill spans. The plane budget is dispatched once for the
 * whole list, so the per-group plane writes unroll exactly as they
 * do inside a single STDL_VLine.
 */
STDL_PLANE_INLINE int vspans_run(STDL_Surface *dst,
        const STDL_Span *spans, int count, uint8_t col,
        int transparent, const int np)
{
    int drew = 0;
    uint8_t *pixels = dst->pixels;
    uint8_t *maskbase = dst->mask;
    uint16_t stride = dst->stride;
    uint16_t maskstride = dst->maskstride;
    int cx0 = dst->clip.x, cy0 = dst->clip.y;
    int cx1 = cx0 + dst->clip.w - 1;
    int cy1 = cy0 + dst->clip.h - 1;
    const STDL_Span *s = spans;
    int i;

    for (i = 0; i < count; i++, s++) {
        int x = s->x, y = s->y, rows = s->len, n;
        uint16_t bit;
        uint8_t *p;

        if (rows <= 0 || x < cx0 || x > cx1) {
            continue;
        }
        if (y < cy0) {
            rows -= cy0 - y;
            y = cy0;
        }
        if (y + rows > cy1 + 1) {
            rows = cy1 + 1 - y;
        }
        if (rows <= 0) {
            continue;
        }

        drew = 1;
        bit = (uint16_t)(0x8000u >> (x & 15));
        p = pixels + stdl_row_off(y, stride) + ((x >> 1) & ~7);
        n = rows;
        do {
            put_bit_planes((uint16_t *)p, col, bit, np);
            p += stride;
        } while (--n);

        if (maskbase != NULL) {
            uint8_t *m = maskbase + stdl_row_off(y, maskstride)
                       + ((x >> 4) * 2);
            n = rows;
            if (transparent) {
                do {
                    *(uint16_t *)m |= bit;
                    m += maskstride;
                } while (--n);
            } else {
                uint16_t nb = (uint16_t)~bit;
                do {
                    *(uint16_t *)m &= nb;
                    m += maskstride;
                } while (--n);
            }
        }
    }
    return drew;
}

void STDL_VSpans(STDL_Surface *dst, const STDL_Span *spans,
                 int count, uint8_t col)
{
    int transparent, np, drew = 0;

    if (dst == NULL || spans == NULL || count <= 0
        || dst->clip.w == 0 || dst->clip.h == 0) {
        return;
    }
    transparent = (col >= STDL_TRANSPARENT && dst->mask != NULL);
    col = transparent ? 0 : (uint8_t)(col & 15);
    np = stdl_planes;
#define VSPANS_RUN(np) \
    drew = vspans_run(dst, spans, count, col, transparent, (np))
    STDL_PLANE_DISPATCH(np, VSPANS_RUN);
#undef VSPANS_RUN
    if (drew && dst->mask != NULL) {
        dst->opaque_state = 0;
    }
}

/*
 * Horizontal fill spans: one row of fill_span_rows per span, with
 * the plane words and the budget dispatch hoisted out of the list.
 */
STDL_PLANE_INLINE int hspans_run(STDL_Surface *dst,
        const STDL_Span *spans, int count,
        uint16_t pw0, uint16_t pw1, uint16_t pw2, uint16_t pw3,
        int transparent, const int np)
{
    int drew = 0;
    uint8_t *pixels = dst->pixels;
    uint8_t *maskbase = dst->mask;
    uint16_t stride = dst->stride;
    uint16_t maskstride = dst->maskstride;
    int cx0 = dst->clip.x, cy0 = dst->clip.y;
    int cx1 = cx0 + dst->clip.w - 1;
    int cy1 = cy0 + dst->clip.h - 1;
    const STDL_Span *s = spans;
    int i;

    for (i = 0; i < count; i++, s++) {
        int x1 = s->x, y = s->y, x2, g0, g1;
        uint16_t lm, rm;

        if (s->len <= 0 || y < cy0 || y > cy1) {
            continue;
        }
        x2 = x1 + s->len - 1;
        if (x1 < cx0) x1 = cx0;
        if (x2 > cx1) x2 = cx1;
        if (x1 > x2) {
            continue;
        }
        g0 = x1 >> 4;
        g1 = x2 >> 4;
        lm = (uint16_t)(0xFFFFu >> (x1 & 15));
        rm = (uint16_t)(0xFFFFu << (15 - (x2 & 15)));
        if (g0 == g1) {
            lm &= rm;
            rm = lm;
        }
        drew = 1;
        fill_span_rows(pixels + stdl_row_off(y, stride),
                       maskbase != NULL
                           ? maskbase + stdl_row_off(y, maskstride)
                           : NULL,
                       stride, maskstride, g0, g1, 1,
                       pw0, pw1, pw2, pw3, lm, rm, transparent, np);
    }
    return drew;
}

/*
 * One plane bit, duplicated into both halves of a long and indexed
 * by x & 15. Merging a point through this covers two planes per
 * long operation and replaces a variable shift with a load; both
 * halves are equal, so it needs no byte-order form.
 */
static const uint32_t stdl_bit32[16] = {
    0x80008000UL, 0x40004000UL, 0x20002000UL, 0x10001000UL,
    0x08000800UL, 0x04000400UL, 0x02000200UL, 0x01000100UL,
    0x00800080UL, 0x00400040UL, 0x00200020UL, 0x00100010UL,
    0x00080008UL, 0x00040004UL, 0x00020002UL, 0x00010001UL
};

/* the half of a plane pair that a long merge must leave alone when
 * the budget stops inside the pair (np 1 and 3) */
#define STDL_PAIR_HI STDL_PACK2(0xFFFFu, 0u)

/* a colour index as the two long plane pairs a group merge wants */
#define STDL_PLANEPAIR(c) \
    { STDL_PACK2((c) & 1 ? 0xFFFFu : 0u, (c) & 2 ? 0xFFFFu : 0u), \
      STDL_PACK2((c) & 4 ? 0xFFFFu : 0u, (c) & 8 ? 0xFFFFu : 0u) }
static const uint32_t stdl_planepair[16][2] = {
    STDL_PLANEPAIR(0),  STDL_PLANEPAIR(1),  STDL_PLANEPAIR(2),
    STDL_PLANEPAIR(3),  STDL_PLANEPAIR(4),  STDL_PLANEPAIR(5),
    STDL_PLANEPAIR(6),  STDL_PLANEPAIR(7),  STDL_PLANEPAIR(8),
    STDL_PLANEPAIR(9),  STDL_PLANEPAIR(10), STDL_PLANEPAIR(11),
    STDL_PLANEPAIR(12), STDL_PLANEPAIR(13), STDL_PLANEPAIR(14),
    STDL_PLANEPAIR(15)
};
#undef STDL_PLANEPAIR

/*
 * Batched single pixels, unmasked destination clipped at its own
 * origin: the particle-field inner loop, and the one place in the
 * library where the register file decides the speed. Written per
 * plane -
 *
 *     grp[p] = (grp[p] & ~bit) | (pw[p] & bit)
 *
 * - it needs the four plane words, the bit and its complement live
 * at once; gcc 4.6 runs out of registers and spills, and the merge
 * costs nine instructions and five stack accesses per plane, per
 * point (measured: 728 cycles a point on an 8MHz 68000, of which
 * the merge alone is ~340). Carrying the plane words as two longs
 * and merging with XOR-AND-XOR needs one temporary and no
 * complement, so the whole point stays in registers.
 *
 * Two conditions get the last two registers back, and the caller
 * sends anything else down points_run: the group has to be
 * long-aligned for the long merge, and the clip origin has to be
 * (0,0) so each axis costs one unsigned compare (a negative
 * coordinate wraps above the width) instead of a pair.
 */
STDL_PLANE_INLINE void points_fast(STDL_Surface *dst,
        const STDL_Point *pts, int count,
        uint32_t pl01, uint32_t pl23, const int np)
{
    uint8_t *pixels = dst->pixels;
    uint16_t stride = dst->stride;
    unsigned cw = dst->clip.w, ch = dst->clip.h;
    const STDL_Point *p = pts;
    const STDL_Point *end = pts + count;

    for (; p != end; p++) {
        unsigned x = (unsigned)(int)p->x, y = (unsigned)(int)p->y;
        uint32_t *g, m, t;

        if (x >= cw || y >= ch) {
            continue;
        }
        /* (x >> 4) * 8 without the round trip through the shifter */
        g = (uint32_t *)(pixels + stdl_row_off((int)y, stride)
                         + ((x >> 1) & ~7u));
        m = stdl_bit32[x & 15];
        if (np == 1) {
            m &= STDL_PAIR_HI;
        }
        t = (g[0] ^ pl01) & m;
        g[0] ^= t;
        if (np > 2) {
            if (np == 3) {
                m &= STDL_PAIR_HI;
            }
            t = (g[1] ^ pl23) & m;
            g[1] ^= t;
        }
    }
}

/*
 * Erasing a particle field is the same loop with every plane word
 * zero, which collapses the merge to one AND per long. Planes above
 * the budget are already zero, so clearing a bit there is a no-op
 * and this needs no plane dispatch.
 */
static void points_clear(STDL_Surface *dst, const STDL_Point *pts,
                         int count)
{
    uint8_t *pixels = dst->pixels;
    uint16_t stride = dst->stride;
    unsigned cw = dst->clip.w, ch = dst->clip.h;
    const STDL_Point *p = pts;
    const STDL_Point *end = pts + count;

    for (; p != end; p++) {
        unsigned x = (unsigned)(int)p->x, y = (unsigned)(int)p->y;
        uint32_t *g, m;

        if (x >= cw || y >= ch) {
            continue;
        }
        g = (uint32_t *)(pixels + stdl_row_off((int)y, stride)
                         + ((x >> 1) & ~7u));
        m = ~stdl_bit32[x & 15];
        g[0] &= m;
        g[1] &= m;
    }
}

/*
 * The same loop with a colour per point. The only extra work is one
 * byte load and the plane-pair table lookup it indexes, which is
 * far less than a caller pays to sort its list into colour runs and
 * make a batched call per run.
 */
STDL_PLANE_INLINE void pointsc_fast(STDL_Surface *dst,
        const STDL_Point *pts, const uint8_t *cols, int count,
        const int np)
{
    uint8_t *pixels = dst->pixels;
    uint16_t stride = dst->stride;
    unsigned cw = dst->clip.w, ch = dst->clip.h;
    const STDL_Point *p = pts;
    const STDL_Point *end = pts + count;
    const uint8_t *c = cols;

    for (; p != end; p++, c++) {
        unsigned x = (unsigned)(int)p->x, y = (unsigned)(int)p->y;
        const uint32_t *pl;
        uint32_t *g, m, t;

        if (x >= cw || y >= ch) {
            continue;
        }
        g = (uint32_t *)(pixels + stdl_row_off((int)y, stride)
                         + ((x >> 1) & ~7u));
        m = stdl_bit32[x & 15];
        pl = stdl_planepair[*c & 15];
        if (np == 1) {
            m &= STDL_PAIR_HI;
        }
        t = (g[0] ^ pl[0]) & m;
        g[0] ^= t;
        if (np > 2) {
            if (np == 3) {
                m &= STDL_PAIR_HI;
            }
            t = (g[1] ^ pl[1]) & m;
            g[1] ^= t;
        }
    }
}

/*
 * Batched single pixels. Everything a span needs and a point does
 * not - the length clamp, both edge masks, the "does it straddle two
 * groups" tail - is gone, and what is left per point is a clip test,
 * one mulu.w for the row and one read-modify-write per plane. This
 * is the general form: masked destinations, and surfaces whose rows
 * are not long-aligned.
 */
STDL_PLANE_INLINE int points_run(STDL_Surface *dst,
        const STDL_Point *pts, int count,
        uint16_t pw0, uint16_t pw1, uint16_t pw2, uint16_t pw3,
        int transparent, const int np)
{
    int drew = 0;
    uint8_t *pixels = dst->pixels;
    uint8_t *maskbase = dst->mask;
    uint16_t stride = dst->stride;
    uint16_t maskstride = dst->maskstride;
    int cx0 = dst->clip.x, cy0 = dst->clip.y;
    int cx1 = cx0 + dst->clip.w - 1;
    int cy1 = cy0 + dst->clip.h - 1;
    const STDL_Point *p = pts;
    int i;

    for (i = 0; i < count; i++, p++) {
        int x = p->x, y = p->y;
        uint16_t bit, *grp;

        if (x < cx0 || x > cx1 || y < cy0 || y > cy1) {
            continue;
        }
        grp = (uint16_t *)(pixels + stdl_row_off(y, stride)
                           + (x >> 4) * 8);
        bit = (uint16_t)(0x8000u >> (x & 15));
        stdl_merge_planes(grp, bit, pw0, pw1, pw2, pw3, np);
        if (maskbase != NULL) {
            uint16_t *mw = (uint16_t *)(maskbase
                                        + stdl_row_off(y, maskstride));
            if (transparent) {
                mw[x >> 4] |= bit;
            } else {
                mw[x >> 4] &= (uint16_t)~bit;
            }
        }
        drew = 1;
    }
    return drew;
}

void STDL_Points(STDL_Surface *dst, const STDL_Point *pts,
                 int count, uint8_t col)
{
    uint16_t pw[4];
    int transparent, np, p, drew = 0;

    if (dst == NULL || pts == NULL || count <= 0
        || dst->clip.w == 0 || dst->clip.h == 0) {
        return;
    }
    transparent = (col >= STDL_TRANSPARENT && dst->mask != NULL);
    col = transparent ? 0 : (uint8_t)(col & 15);
    np = stdl_planes;
    if (dst->mask == NULL && dst->clip.x == 0 && dst->clip.y == 0
        && (((uintptr_t)dst->pixels | (uintptr_t)dst->stride) & 3u) == 0) {
        if (col == 0) {
            points_clear(dst, pts, count);
        } else {
            uint32_t pl01 = STDL_PACK2((col & 1) ? 0xFFFFu : 0u,
                                       (col & 2) ? 0xFFFFu : 0u);
            uint32_t pl23 = STDL_PACK2((col & 4) ? 0xFFFFu : 0u,
                                       (col & 8) ? 0xFFFFu : 0u);
#define POINTS_FAST(np) points_fast(dst, pts, count, pl01, pl23, (np))
            STDL_PLANE_DISPATCH(np, POINTS_FAST);
#undef POINTS_FAST
        }
        return;                 /* no mask, so nothing to invalidate */
    }
    for (p = 0; p < 4; p++) {
        pw[p] = (col & (1 << p)) ? 0xFFFFu : 0;
    }
#define POINTS_RUN(np) \
    drew = points_run(dst, pts, count, pw[0], pw[1], pw[2], pw[3], \
                      transparent, (np))
    STDL_PLANE_DISPATCH(np, POINTS_RUN);
#undef POINTS_RUN
    if (drew && dst->mask != NULL) {
        dst->opaque_state = 0;
    }
}

void STDL_PointsC(STDL_Surface *dst, const STDL_Point *pts,
                  const uint8_t *cols, int count)
{
    int np, i;

    if (dst == NULL || pts == NULL || cols == NULL || count <= 0
        || dst->clip.w == 0 || dst->clip.h == 0) {
        return;
    }
    np = stdl_planes;
    if (dst->mask == NULL && dst->clip.x == 0 && dst->clip.y == 0
        && (((uintptr_t)dst->pixels | (uintptr_t)dst->stride) & 3u) == 0) {
#define POINTSC_FAST(np) pointsc_fast(dst, pts, cols, count, (np))
        STDL_PLANE_DISPATCH(np, POINTSC_FAST);
#undef POINTSC_FAST
        return;                 /* no mask, so nothing to invalidate */
    }
    /* masked or oddly aligned: rare enough that the definition is
     * also the implementation */
    for (i = 0; i < count; i++) {
        STDL_PutPixel(dst, pts[i].x, pts[i].y, cols[i]);
    }
}

void STDL_HSpans(STDL_Surface *dst, const STDL_Span *spans,
                 int count, uint8_t col)
{
    uint16_t pw[4];
    int transparent, np, p, drew = 0;

    if (dst == NULL || spans == NULL || count <= 0
        || dst->clip.w == 0 || dst->clip.h == 0) {
        return;
    }
    transparent = (col >= STDL_TRANSPARENT && dst->mask != NULL);
    col = transparent ? 0 : (uint8_t)(col & 15);
    for (p = 0; p < 4; p++) {
        pw[p] = (col & (1 << p)) ? 0xFFFFu : 0;
    }
    np = stdl_planes;
#define HSPANS_RUN(np) \
    drew = hspans_run(dst, spans, count, pw[0], pw[1], pw[2], pw[3], \
                      transparent, (np))
    STDL_PLANE_DISPATCH(np, HSPANS_RUN);
#undef HSPANS_RUN
    if (drew && dst->mask != NULL) {
        dst->opaque_state = 0;
    }
}

/*
 * Vertical XOR spans. `pair` is a compile-time flag for the common
 * two-plane colour: planes 0 and 1 are adjacent words, so one long
 * XOR does both. It is decided once for the whole list - the group
 * address is a multiple of 8 from the surface base, so if the base
 * is long-aligned every span is.
 */
STDL_PLANE_INLINE int xor_vspans_run(STDL_Surface *dst,
        const STDL_Span *spans, int count, uint8_t col,
        const int pair)
{
    int drew = 0;
    uint8_t *pixels = dst->pixels;
    uint8_t *maskbase = dst->mask;
    uint16_t stride = dst->stride;
    uint16_t maskstride = dst->maskstride;
    int cx0 = dst->clip.x, cy0 = dst->clip.y;
    int cx1 = cx0 + dst->clip.w - 1;
    int cy1 = cy0 + dst->clip.h - 1;
    int c0 = col & 1, c1 = col & 2, c2 = col & 4, c3 = col & 8;
    const STDL_Span *s = spans;
    int i;

    for (i = 0; i < count; i++, s++) {
        int x = s->x, y = s->y, rows = s->len, n;
        uint16_t bit;
        uint8_t *p;

        if (rows <= 0 || x < cx0 || x > cx1) {
            continue;
        }
        if (y < cy0) {
            rows -= cy0 - y;
            y = cy0;
        }
        if (y + rows > cy1 + 1) {
            rows = cy1 + 1 - y;
        }
        if (rows <= 0) {
            continue;
        }

        drew = 1;
        bit = (uint16_t)(0x8000u >> (x & 15));
        /* (x >> 4) * 8 without the shift pair; x is clipped, so
         * never negative */
        p = pixels + stdl_row_off(y, stride) + ((x >> 1) & ~7);
        n = rows;
        if (pair) {
            uint32_t lw = ((uint32_t)bit << 16) | bit;
            do {
                *(uint32_t *)p ^= lw;
                p += stride;
            } while (--n);
        } else {
            do {
                xor_group((uint16_t *)p, bit, c0, c1, c2, c3);
                p += stride;
            } while (--n);
        }

        if (maskbase != NULL) {
            uint8_t *m = maskbase + stdl_row_off(y, maskstride)
                       + ((x >> 4) * 2);
            uint16_t nb = (uint16_t)~bit;

            n = rows;
            do {
                *(uint16_t *)m &= nb;
                m += maskstride;
            } while (--n);
        }
    }
    return drew;
}

void STDL_XorVSpans(STDL_Surface *dst, const STDL_Span *spans,
                    int count, uint8_t col)
{
    int drew;

    if (dst == NULL || spans == NULL || count <= 0) {
        return;
    }
    col &= STDL_COL_MASK;
    if (col == 0 || dst->clip.w == 0 || dst->clip.h == 0) {
        return;
    }
    if (col == 3 && ((uintptr_t)dst->pixels & 3) == 0
        && (dst->stride & 3) == 0) {
        drew = xor_vspans_run(dst, spans, count, col, 1);
    } else {
        drew = xor_vspans_run(dst, spans, count, col, 0);
    }
    if (drew && dst->mask != NULL) {
        dst->opaque_state = 0;
    }
}

void STDL_XorHSpans(STDL_Surface *dst, const STDL_Span *spans,
                    int count, uint8_t col)
{
    uint8_t *pixels, *maskbase;
    uint16_t stride, maskstride;
    int cx0, cx1, cy0, cy1, c0, c1, c2, c3, i, drew = 0;
    const STDL_Span *s = spans;

    if (dst == NULL || spans == NULL || count <= 0) {
        return;
    }
    col &= STDL_COL_MASK;
    if (col == 0 || dst->clip.w == 0 || dst->clip.h == 0) {
        return;
    }
    pixels = dst->pixels;
    maskbase = dst->mask;
    stride = dst->stride;
    maskstride = dst->maskstride;
    cx0 = dst->clip.x;
    cy0 = dst->clip.y;
    cx1 = cx0 + dst->clip.w - 1;
    cy1 = cy0 + dst->clip.h - 1;
    c0 = col & 1; c1 = col & 2; c2 = col & 4; c3 = col & 8;

    for (i = 0; i < count; i++, s++) {
        int x1 = s->x, y = s->y, x2, g0, g1;
        uint16_t lm, rm;

        if (s->len <= 0 || y < cy0 || y > cy1) {
            continue;
        }
        x2 = x1 + s->len - 1;
        if (x1 < cx0) x1 = cx0;
        if (x2 > cx1) x2 = cx1;
        if (x1 > x2) {
            continue;
        }
        g0 = x1 >> 4;
        g1 = x2 >> 4;
        lm = (uint16_t)(0xFFFFu >> (x1 & 15));
        rm = (uint16_t)(0xFFFFu << (15 - (x2 & 15)));
        if (g0 == g1) {
            lm &= rm;
            rm = lm;
        }
        drew = 1;
        xor_row_groups(pixels + stdl_row_off(y, stride), g0, g1,
                       lm, rm, c0, c1, c2, c3);
        if (maskbase != NULL) {
            xor_mask_row(maskbase + stdl_row_off(y, maskstride),
                         g0, g1, lm, rm);
        }
    }
    if (drew && dst->mask != NULL) {
        dst->opaque_state = 0;
    }
}

/* ---------------------------------------------------------------- */

void STDL_Line(STDL_Surface *dst, int x1, int y1, int x2, int y2,
               uint8_t col)
{
    int dx, dy, sx, sy, err, e2;

    if (dst == NULL) {
        return;
    }
    if (y1 == y2) {
        STDL_HLine(dst, x1, x2, y1, col);
        return;
    }
    if (x1 == x2) {
        STDL_VLine(dst, x1, y1, y2, col);
        return;
    }
    dx = x2 > x1 ? x2 - x1 : x1 - x2;
    dy = y2 > y1 ? y1 - y2 : y2 - y1;   /* negative magnitude */
    sx = x1 < x2 ? 1 : -1;
    sy = y1 < y2 ? 1 : -1;
    err = dx + dy;
    for (;;) {
        STDL_PutPixel(dst, x1, y1, col);
        if (x1 == x2 && y1 == y2) {
            break;
        }
        e2 = err * 2;
        if (e2 >= dy) { err += dy; x1 += sx; }
        if (e2 <= dx) { err += dx; y1 += sy; }
    }
}

void STDL_Circle(STDL_Surface *dst, int cx, int cy, int r, uint8_t col)
{
    int x = r, y = 0, err = 1 - r;

    if (dst == NULL || r < 0) {
        return;
    }
    while (x >= y) {
        STDL_PutPixel(dst, cx + x, cy + y, col);
        STDL_PutPixel(dst, cx - x, cy + y, col);
        STDL_PutPixel(dst, cx + x, cy - y, col);
        STDL_PutPixel(dst, cx - x, cy - y, col);
        STDL_PutPixel(dst, cx + y, cy + x, col);
        STDL_PutPixel(dst, cx - y, cy + x, col);
        STDL_PutPixel(dst, cx + y, cy - x, col);
        STDL_PutPixel(dst, cx - y, cy - x, col);
        y++;
        if (err < 0) {
            err += 2 * y + 1;
        } else {
            x--;
            err += 2 * (y - x) + 1;
        }
    }
}

void STDL_FillCircle(STDL_Surface *dst, int cx, int cy, int r,
                     uint8_t col)
{
    int x = r, y = 0, err = 1 - r;

    if (dst == NULL || r < 0) {
        return;
    }
    while (x >= y) {
        STDL_HLine(dst, cx - x, cx + x, cy + y, col);
        STDL_HLine(dst, cx - x, cx + x, cy - y, col);
        STDL_HLine(dst, cx - y, cx + y, cy + x, col);
        STDL_HLine(dst, cx - y, cx + y, cy - x, col);
        y++;
        if (err < 0) {
            err += 2 * y + 1;
        } else {
            x--;
            err += 2 * (y - x) + 1;
        }
    }
}
