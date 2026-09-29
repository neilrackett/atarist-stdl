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
 * n groups (n >= 1) of a four-plane fill, as long pairs, for a fill
 * whose rows are whole and contiguous - a screen cleared to a colour.
 * Colours 0 and 15 get a memset there; every other colour went row
 * by row through the span loop, whose middle gcc 4.6 compiles to a
 * store at (a0), a store at 4(a0), an addq and a dbf: 46 cycles a
 * group, 36ms for a full screen on a plain ST against the memset's
 * 10. Post-increment stores four groups to a pass were 26.5 (14.1ms
 * a screen); eight registers holding the pattern, stored downward by
 * movem.l twice a pass, are 19.25 - the memset's own rate.
 *
 * Out of line on purpose: inlined into the span loop, the same asm
 * made small fills 5-8% slower through register allocation alone.
 * The caller checks for long alignment, though a 68000 would take
 * any even address. The count is 32-bit, since dbf counts 16.
 */
static __attribute__((noinline)) void fill_longpairs(uint32_t *lp,
                                                     uint32_t n,
                                                     uint32_t l01,
                                                     uint32_t l23)
{
#ifdef __m68k__
    lp += n << 1;                     /* filled from the end down */
    __asm__ volatile(
        "moveq  #7,%%d0\n\t"
        "and.w  %1,%%d0\n\t"          /* groups left over       */
        "lsr.l  #3,%1\n\t"            /* passes of eight groups */
        "subq.w #1,%%d0\n\t"
        "bmi.s  2f\n"
        "1:\n\t"
        "move.l %3,-(%0)\n\t"
        "move.l %2,-(%0)\n\t"
        "dbf    %%d0,1b\n"
        "2:\n\t"
        "subq.l #1,%1\n\t"
        "bmi.s  4f\n\t"
        /* predecrement movem puts d0 lowest and a4 highest */
        "move.l %2,%%d0\n\t"
        "move.l %3,%%d1\n\t"
        "move.l %2,%%d2\n\t"
        "move.l %3,%%d3\n\t"
        "move.l %2,%%a1\n\t"
        "move.l %3,%%a2\n\t"
        "move.l %2,%%a3\n\t"
        "move.l %3,%%a4\n"
        "3:\n\t"
        "movem.l %%d0-%%d3/%%a1-%%a4,-(%0)\n\t"
        "movem.l %%d0-%%d3/%%a1-%%a4,-(%0)\n\t"
        "dbf    %1,3b\n\t"
        "clr.w  %1\n\t"               /* dbf counts 16 bits:    */
        "subq.l #1,%1\n\t"            /* carry into the high    */
        "bcc.s  3b\n"                   /* word, as gcc does      */
        "4:"
        : "+a"(lp), "+d"(n)
        : "d"(l01), "d"(l23)
        : "d0", "d1", "d2", "d3", "a1", "a2", "a3", "a4", "cc",
          "memory");
#else
    /* C twin - what tests/host exercises and the asm must match */
    while (n-- != 0) {
        *lp++ = l01;
        *lp++ = l23;
    }
#endif
}

/*
 * One row of a span: every CPU fill that is not a whole block in 0
 * or 15 (FillRect's rows, STDL_HLine, each span of STDL_HSpans). The
 * edge groups merge a plane pair at a time as XOR-AND-XOR -
 * g ^= (g ^ colour) & edge - which needs the colour as two longs and
 * the edge mask in both halves of a third, and so stays in registers
 * where the per-plane form it replaced spilled to the stack; the
 * groups between are plain long stores. g is the first group, n the
 * number of groups after it. A 68000 long access needs only an even
 * address. Measured on a plain ST against the per-plane form: fills
 * of 16x16 to 64x32 at 0.56-0.85 of the time, a single HLine 0.52.
 */
STDL_PLANE_INLINE void span_row(uint32_t *g, int n, uint16_t lm,
                                uint16_t rm, uint32_t l01, uint32_t l23,
                                const int np)
{
    uint32_t m, t;

    if (n == 0) {
        lm &= rm;
    }
    m = ((uint32_t)lm << 16) | lm;
    t = (g[0] ^ l01) & m;
    g[0] ^= t;
    if (np > 2) {
        t = (g[1] ^ l23) & m;
        g[1] ^= t;
    }
    if (n == 0) {
        return;
    }
    g += 2;
    if (--n != 0) {
#ifdef __m68k__
        /* post-increment and dbf: gcc's own loop stored at (a0) and
         * 4(a0) and counted with subq/bne, 54 cycles a group to 34 */
        if (np > 2) {
            __asm__ volatile(
                "subq.w #1,%1\n"
                "1:\n\t"
                "move.l %2,(%0)+\n\t"
                "move.l %3,(%0)+\n\t"
                "dbf    %1,1b"
                : "+a"(g), "+d"(n)
                : "d"(l01), "d"(l23)
                : "cc", "memory");
        } else {
            __asm__ volatile(
                "subq.w #1,%1\n"
                "1:\n\t"
                "move.l %2,(%0)\n\t"
                "addq.l #8,%0\n\t"
                "dbf    %1,1b"
                : "+a"(g), "+d"(n)
                : "d"(l01)
                : "cc", "memory");
        }
#else
        /* C twin - what tests/host exercises and the asm must match */
        do {
            g[0] = l01;
            if (np > 2) {
                g[1] = l23;
            }
            g += 2;
        } while (--n != 0);
#endif
    }
    m = ((uint32_t)rm << 16) | rm;
    t = (g[0] ^ l01) & m;
    g[0] ^= t;
    if (np > 2) {
        t = (g[1] ^ l23) & m;
        g[1] ^= t;
    }
}

/* the same row's mask words, from the group span_row started at */
static void span_mask_row(uint16_t *mw, int n, uint16_t lm, uint16_t rm,
                          int transparent)
{
    if (n == 0) {
        lm &= rm;
    }
    if (transparent) {
        *mw++ |= lm;
        if (n == 0) {
            return;
        }
        while (--n != 0) {
            *mw++ = 0xFFFFu;
        }
        *mw |= rm;
    } else {
        *mw++ &= (uint16_t)~lm;
        if (n == 0) {
            return;
        }
        while (--n != 0) {
            *mw++ = 0;
        }
        *mw &= (uint16_t)~rm;
    }
}

/*
 * The CPU fill's rows, one span_row each. Out of line so that the
 * loop's registers are its own: inlined, every branch added to
 * fill_rows moved these fills by several percent.
 */
static __attribute__((noinline)) void fill_block(uint32_t *g, int n,
        int rows, uint16_t stride, uint16_t lm, uint16_t rm,
        uint32_t l01, uint32_t l23)
{
    const int np = stdl_planes;

#define FILL_BLOCK(np) \
    do { \
        span_row(g, n, lm, rm, l01, l23, (np)); \
        g = (uint32_t *)((uint8_t *)g + stride); \
    } while (--rows != 0)
    STDL_PLANE_DISPATCH(np, FILL_BLOCK);
#undef FILL_BLOCK
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
    uint8_t *row = s->pixels + stdl_row_off(y1, s->stride);
    uint8_t *mrow = (s->mask != NULL)
        ? s->mask + stdl_row_off(y1, s->maskstride) : NULL;
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

    /* asked first: on a plain ST the size test's multiply is for
     * nothing */
    if (stdl_blitter_active()
        && (STDL_BLIT_FORCED()
            || stdl_row_off(ng, (uint16_t)rows)
               >= STDL_BLIT_FILL_MIN_CELLS)
        && STDL_BLIT_REACHES(s, s)) {
        uintptr_t base = (uintptr_t)(row + g0 * 8);
        int16_t yinc = (int16_t)(s->stride - (ng - 1) * 8);

        if ((lm & rm) == 0xFFFFu
            && (col == 0 || (col == 15 && np == 4))) {
            /*
             * Whole groups in 0 or 15: every plane word the same, so
             * one operation walks all four planes of each group (x
             * increment 2) - the BLiTTER's version of the memset
             * below. Out-of-budget planes get zeros they already hold.
             */
            const uint16_t nw = (uint16_t)(ng * 4);
            stdl_blitter_go(0, 0, 0, base, 2,
                            (int16_t)(s->stride - (nw - 1) * 2),
                            0xFFFFu, 0xFFFFu, nw, (uint16_t)rows,
                            STDL_BLIT_HOP_ONES,
                            col ? STDL_BLIT_OP_ONES : STDL_BLIT_OP_ZERO,
                            0);
        } else {
            /* one setup; each plane changes only the operation -
             * HOP all-ones: OP_ONES writes ones, OP_ZERO zeros */
            stdl_blitter_setup(0, 0, 8, yinc, lm, rm, (uint16_t)ng,
                               STDL_BLIT_HOP_ONES, STDL_BLIT_OP_ZERO, 0);
            for (p = 0; p < np; p++) {
                STDL_BLIT_SET_OP(pw[p] ? STDL_BLIT_OP_ONES
                                       : STDL_BLIT_OP_ZERO);
                stdl_blitter_run(0, base + (uintptr_t)(p * 2),
                                 (uint16_t)ng, (uint16_t)rows,
                                 STDL_BLIT_HOP_ONES);
            }
        }
        if (mrow != NULL) {
            stdl_blitter_go(0, 0, 0, (uintptr_t)(mrow + g0 * 2),
                            2, (int16_t)(s->maskstride - (ng - 1) * 2),
                            lm, rm, (uint16_t)ng, (uint16_t)rows,
                            STDL_BLIT_HOP_ONES,
                            transparent ? STDL_BLIT_OP_SRC
                                        : STDL_BLIT_OP_ZERO, 0);
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
     *
     * A memset per row is another matter: mintlib's costs about 70us
     * a call before it stores anything, and span_row beats it at
     * every width a screen has (measured on a plain ST, 32 rows: 16
     * pixels 1347us against 745, 320 pixels 4687 against 4375). The
     * slopes cross near 33 groups, so only rows that wide - a world
     * surface - still take it.
     */
    if ((lm & rm) == 0xFFFFu && !(col == 0 || (col == 15 && np == 4))) {
        /* any other colour over a whole block of whole rows: see
         * fill_longpairs */
        if ((uint16_t)(ng << 3) == s->stride && np == 4 && mrow == NULL
            && ((uintptr_t)row & 3) == 0) {
            fill_longpairs((uint32_t *)row,
                           stdl_row_off(rows, (uint16_t)ng),
                           STDL_PACK2(pw[0], pw[1]),
                           STDL_PACK2(pw[2], pw[3]));
            return;
        }
    } else if ((lm & rm) == 0xFFFFu
               && (ng >= 32 || (uint16_t)(ng << 3) == s->stride)) {
        int fb = (col == 0) ? 0x00 : 0xFF;
        uint32_t span = (uint32_t)ng * 8;

        if (span == s->stride) {
            memset(row, fb, stdl_row_off(rows, (uint16_t)span));
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
                memset(mrow, mb, stdl_row_off(rows, (uint16_t)mspan));
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

    if (rows == 1) {
        /* one row: the call and its argument traffic are most of it */
#define FILL_ONE(np) \
        span_row((uint32_t *)(row + g0 * 8), g1 - g0, lm, rm, \
                 STDL_PACK2(pw[0], pw[1]), STDL_PACK2(pw[2], pw[3]), (np))
        STDL_PLANE_DISPATCH(np, FILL_ONE);
#undef FILL_ONE
    } else {
        fill_block((uint32_t *)(row + g0 * 8), g1 - g0, rows, s->stride,
                   lm, rm, STDL_PACK2(pw[0], pw[1]),
                   STDL_PACK2(pw[2], pw[3]));
    }
    if (mrow != NULL) {
        uint16_t *mw = (uint16_t *)mrow + g0;
        const uint16_t maskstride = s->maskstride;

        y = rows;
        do {
            span_mask_row(mw, g1 - g0, lm, rm, transparent);
            mw = (uint16_t *)((uint8_t *)mw + maskstride);
        } while (--y != 0);
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
    col &= STDL_COL_MASK;
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
    /* a row too short for the BLiTTER skips fill_rows' set-up */
    if ((x2 >> 4) - (x1 >> 4) < STDL_BLIT_FILL_MIN_CELLS - 1) {
        const int transparent = (col >= STDL_TRANSPARENT
                                 && dst->mask != NULL);
        const int g0 = x1 >> 4, n = (x2 >> 4) - g0;
        const uint16_t lm = (uint16_t)(0xFFFFu >> (x1 & 15));
        const uint16_t rm = (uint16_t)(0xFFFFu << (15 - (x2 & 15)));
        uint32_t *g = (uint32_t *)(dst->pixels
            + stdl_row_off(y, dst->stride) + g0 * 8);
        uint32_t l01, l23;
        int np = stdl_planes;

        col = transparent ? 0 : (uint8_t)(col & STDL_COL_MASK);
        l01 = STDL_PACK2((col & 1) ? 0xFFFFu : 0u,
                         (col & 2) ? 0xFFFFu : 0u);
        l23 = STDL_PACK2((col & 4) ? 0xFFFFu : 0u,
                         (col & 8) ? 0xFFFFu : 0u);
#define SPAN_ROW(np) span_row(g, n, lm, rm, l01, l23, (np))
        STDL_PLANE_DISPATCH(np, SPAN_ROW);
#undef SPAN_ROW
        if (dst->mask != NULL) {
            span_mask_row((uint16_t *)(dst->mask
                              + stdl_row_off(y, dst->maskstride)) + g0,
                          n, lm, rm, transparent);
            dst->opaque_state = 0;
        }
        return;
    }
    {
        int transparent = (col >= STDL_TRANSPARENT
                           && dst->mask != NULL);
        fill_rows(dst, x1, x2, y, y,
                  (uint8_t)(transparent ? 0 : col & STDL_COL_MASK),
                  transparent);
    }
}

/*
 * One column of pixels, the plane words merged a pair at a time the
 * way points_fast does it: the colour as two longs and the bit in
 * both halves of a third, so a row is two XOR-AND-XORs with nothing
 * decided inside the loop. A 68000 long access needs only an even
 * address, which every group is.
 */
STDL_PLANE_INLINE void vline_rows(uint8_t *base, uint16_t stride,
                                  int rows, uint32_t l01, uint32_t l23,
                                  uint32_t m, const int np)
{
    do {
        uint32_t *g = (uint32_t *)base;
        uint32_t t = (g[0] ^ l01) & m;

        g[0] ^= t;
        if (np > 2) {
            t = (g[1] ^ l23) & m;
            g[1] ^= t;
        }
        base += stride;
    } while (--rows != 0);
}

void STDL_VLine(STDL_Surface *dst, int x, int y1, int y2, uint8_t col)
{
    int t, transparent, np, rows;
    uint16_t bit;
    uint32_t l01, l23;
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
    transparent = (col >= STDL_TRANSPARENT && dst->mask != NULL);
    col = transparent ? 0 : (uint8_t)(col & STDL_COL_MASK);
    rows = y2 - y1 + 1;
    bit = (uint16_t)(0x8000u >> (x & 15));
    base = dst->pixels + stdl_row_off(y1, dst->stride) + ((x >> 4) * 8);
    l01 = STDL_PACK2((col & 1) ? 0xFFFFu : 0u, (col & 2) ? 0xFFFFu : 0u);
    l23 = STDL_PACK2((col & 4) ? 0xFFFFu : 0u, (col & 8) ? 0xFFFFu : 0u);
    np = stdl_planes;
#define VLINE_ROWS(np) \
    vline_rows(base, dst->stride, rows, l01, l23, \
               ((uint32_t)bit << 16) | bit, (np))
    STDL_PLANE_DISPATCH(np, VLINE_ROWS);
#undef VLINE_ROWS
    if (dst->mask != NULL) {
        uint16_t maskstride = dst->maskstride;
        uint8_t *m = dst->mask + stdl_row_off(y1, maskstride)
                   + ((x >> 4) * 2);

        if (transparent) {
            do {
                *(uint16_t *)m |= bit;
                m += maskstride;
            } while (--rows != 0);
        } else {
            const uint16_t nb = (uint16_t)~bit;
            do {
                *(uint16_t *)m &= nb;
                m += maskstride;
            } while (--rows != 0);
        }
        dst->opaque_state = 0;
    }
}

static const uint32_t stdl_bit32[16] = {
    0x80008000UL, 0x40004000UL, 0x20002000UL, 0x10001000UL,
    0x08000800UL, 0x04000400UL, 0x02000200UL, 0x01000100UL,
    0x00800080UL, 0x00400040UL, 0x00200020UL, 0x00100010UL,
    0x00080008UL, 0x00040004UL, 0x00020002UL, 0x00010001UL
};

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
 * One pixel as two plane-pair merges, g ^= (g ^ colour) & bit, the
 * colour and the bit from the tables above: no plane-budget dispatch
 * and no per-plane test - a plane beyond the budget is zero in both
 * the colour and the destination. The per-plane form it replaced cost
 * 146us a call on a plain ST.
 */
void STDL_PutPixel(STDL_Surface *dst, int x, int y, uint8_t col)
{
    uint8_t *g;
    uint32_t m, t;
    const uint32_t *pl;
    int transparent;

    if (dst == NULL
        || x < dst->clip.x || x >= dst->clip.x + dst->clip.w
        || y < dst->clip.y || y >= dst->clip.y + dst->clip.h) {
        return;
    }
    transparent = (col >= STDL_TRANSPARENT && dst->mask != NULL);
    pl = stdl_planepair[transparent ? 0 : (col & STDL_COL_MASK)];
    m = stdl_bit32[x & 15];
    g = dst->pixels + stdl_row_off(y, dst->stride) + ((x >> 1) & ~7);
    t = stdl_ld32(g);
    stdl_st32(g, t ^ ((t ^ pl[0]) & m));
    t = stdl_ld32(g + 4);
    stdl_st32(g + 4, t ^ ((t ^ pl[1]) & m));
    if (dst->mask != NULL) {
        uint16_t *mw = (uint16_t *)(dst->mask
            + stdl_row_off(y, dst->maskstride)) + (x >> 4);
        if (transparent) {
            *mw |= (uint16_t)m;
        } else {
            *mw &= (uint16_t)~m;
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
    grp = (const uint16_t *)(src->pixels + stdl_row_off(y, src->stride)
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
 * XOR a plane pair at a time: the colour as two longs, each half
 * 0xFFFF where its plane's bit is set, ANDed with the pixels' mask in
 * both halves - g ^= colour & mask. A plane whose bit is clear XORs
 * with zero, the identity, so out-of-budget planes (col arrives
 * masked with STDL_COL_MASK) are never changed. The word-at-a-time
 * form this replaced tested the colour's four bits for every group:
 * 2.2ms for a 32x32 XorRect on a plain ST.
 *
 * The column forms keep a second instantiation for colour 3, the
 * terrain outline a port draws a column at a time: there the colour
 * is a constant, the loop's one long is the pixel's bit in both
 * halves and nothing else stays live. Given the colour as a variable
 * the same loop spilled the bit and the stride around it, 6% slower
 * than the special case it replaced. `both` is false only there.
 */
#define XOR_COL3(col, BODY) \
    do {                        \
        if ((col) == 3) {       \
            BODY(0xFFFFFFFFu, 0u, 0); \
        } else {                \
            BODY(XOR_L01(col), XOR_L23(col), 1); \
        }                       \
    } while (0)

/* one row: g the first group, n the groups after it */
STDL_PLANE_INLINE void xor_row(uint32_t *g, int n, uint16_t lm,
                               uint16_t rm, uint32_t l01, uint32_t l23,
                               const int both)
{
    uint32_t m;

    if (n == 0) {
        lm &= rm;
    }
    m = ((uint32_t)lm << 16) | lm;
    g[0] ^= l01 & m;
    if (both) {
        g[1] ^= l23 & m;
    }
    if (n == 0) {
        return;
    }
    g += 2;
    while (--n != 0) {
        g[0] ^= l01;
        if (both) {
            g[1] ^= l23;
        }
        g += 2;
    }
    m = ((uint32_t)rm << 16) | rm;
    g[0] ^= l01 & m;
    if (both) {
        g[1] ^= l23 & m;
    }
}

/* one column, rows >= 1, the colour's pairs masked to the pixel's bit
 * here, beside the loop, where it costs no register across the
 * caller's address arithmetic */
STDL_PLANE_INLINE void xor_column(uint8_t *p, uint16_t stride, int rows,
                                  uint16_t bit, uint32_t l01, uint32_t l23,
                                  const int both)
{
    const uint32_t m = ((uint32_t)bit << 16) | bit;
    const uint32_t a = l01 & m, b = l23 & m;

    do {
        ((uint32_t *)p)[0] ^= a;
        if (both) {
            ((uint32_t *)p)[1] ^= b;
        }
        p += stride;
    } while (--rows != 0);
}

/* the colour's plane pairs */
#define XOR_L01(col) STDL_PACK2(((col) & 1) ? 0xFFFFu : 0u, \
                                ((col) & 2) ? 0xFFFFu : 0u)
#define XOR_L23(col) STDL_PACK2(((col) & 4) ? 0xFFFFu : 0u, \
                                ((col) & 8) ? 0xFFFFu : 0u)

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
    const uint16_t stride = s->stride;
    const int g0 = x1 >> 4, g1 = x2 >> 4, n = g1 - g0;
    const uint16_t lm = (uint16_t)(0xFFFFu >> (x1 & 15));
    const uint16_t rm = (uint16_t)(0xFFFFu << (15 - (x2 & 15)));
    const uint32_t l01 = XOR_L01(col), l23 = XOR_L23(col);
    uint8_t *row = s->pixels + stdl_row_off(y1, stride) + g0 * 8;
    int y;

    /* both pairs always: a row's second XOR with zero costs less
     * than a second copy of the loop costs the size budget */
    for (y = y1; y <= y2; y++) {
        xor_row((uint32_t *)row, n, lm, rm, l01, l23, 1);
        row += stride;
    }
    if (s->mask != NULL) {
        uint8_t *mrow = s->mask + stdl_row_off(y1, s->maskstride);
        const uint16_t elm = (g0 == g1) ? (uint16_t)(lm & rm) : lm;

        for (y = y1; y <= y2; y++) {
            xor_mask_row(mrow, g0, g1, elm, rm);
            mrow += s->maskstride;
        }
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
    STDL_Span sp;
    int t;

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
    /* clipped, so it fits a span's 16 bits: one span through the
     * batched form, whose column loops (colour 3's among them) it
     * would otherwise carry a second copy of */
    sp.x = (int16_t)x;
    sp.y = (int16_t)y1;
    sp.len = (int16_t)(y2 - y1 + 1);
    STDL_XorVSpans(dst, &sp, 1, col);
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
        const STDL_Span *spans, int count, uint32_t l01, uint32_t l23,
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
        vline_rows(p, stride, rows, l01, l23,
                   ((uint32_t)bit << 16) | bit, np);

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
    uint32_t l01, l23;

    if (dst == NULL || spans == NULL || count <= 0
        || dst->clip.w == 0 || dst->clip.h == 0) {
        return;
    }
    transparent = (col >= STDL_TRANSPARENT && dst->mask != NULL);
    col = transparent ? 0 : (uint8_t)(col & STDL_COL_MASK);
    l01 = STDL_PACK2((col & 1) ? 0xFFFFu : 0u, (col & 2) ? 0xFFFFu : 0u);
    l23 = STDL_PACK2((col & 4) ? 0xFFFFu : 0u, (col & 8) ? 0xFFFFu : 0u);
    np = stdl_planes;
#define VSPANS_RUN(np) \
    drew = vspans_run(dst, spans, count, l01, l23, transparent, (np))
    STDL_PLANE_DISPATCH(np, VSPANS_RUN);
#undef VSPANS_RUN
    if (drew && dst->mask != NULL) {
        dst->opaque_state = 0;
    }
}

/*
 * Horizontal fill spans: one span_row per span, with the plane words
 * and the budget dispatch hoisted out of the list.
 */
STDL_PLANE_INLINE int hspans_run(STDL_Surface *dst,
        const STDL_Span *spans, int count, uint32_t l01, uint32_t l23,
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
        int x1 = s->x, y = s->y, x2, g0, n;
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
        n = (x2 >> 4) - g0;
        lm = (uint16_t)(0xFFFFu >> (x1 & 15));
        rm = (uint16_t)(0xFFFFu << (15 - (x2 & 15)));
        drew = 1;
        span_row((uint32_t *)(pixels + stdl_row_off(y, stride) + g0 * 8),
                 n, lm, rm, l01, l23, np);
        if (maskbase != NULL) {
            span_mask_row((uint16_t *)(maskbase
                              + stdl_row_off(y, maskstride)) + g0,
                          n, lm, rm, transparent);
        }
    }
    return drew;
}

/*
 * One plane bit, duplicated into both halves of a long and indexed
 * by x & 15. Merging a point through this covers two planes per
 * long operation and replaces a variable shift with a load; both
 * halves are equal, so it needs no byte-order form.
 */

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
        t = (g[0] ^ pl01) & m;
        g[0] ^= t;
        if (np > 2) {
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
    const uint8_t cmask = STDL_COL_MASK;   /* loop-invariant */

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
        pl = stdl_planepair[*c & cmask];
        t = (g[0] ^ pl[0]) & m;
        g[0] ^= t;
        if (np > 2) {
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
    col = transparent ? 0 : (uint8_t)(col & STDL_COL_MASK);
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
    int transparent, np, drew = 0;
    uint32_t l01, l23;

    if (dst == NULL || spans == NULL || count <= 0
        || dst->clip.w == 0 || dst->clip.h == 0) {
        return;
    }
    transparent = (col >= STDL_TRANSPARENT && dst->mask != NULL);
    col = transparent ? 0 : (uint8_t)(col & STDL_COL_MASK);
    l01 = STDL_PACK2((col & 1) ? 0xFFFFu : 0u, (col & 2) ? 0xFFFFu : 0u);
    l23 = STDL_PACK2((col & 4) ? 0xFFFFu : 0u, (col & 8) ? 0xFFFFu : 0u);
    np = stdl_planes;
#define HSPANS_RUN(np) \
    drew = hspans_run(dst, spans, count, l01, l23, transparent, (np))
    STDL_PLANE_DISPATCH(np, HSPANS_RUN);
#undef HSPANS_RUN
    if (drew && dst->mask != NULL) {
        dst->opaque_state = 0;
    }
}

/*
 * Vertical XOR spans: a column each, the colour's plane pairs masked
 * to the span's bit once per span (see xor_row).
 */
STDL_PLANE_INLINE int xor_vspans_run(STDL_Surface *dst,
        const STDL_Span *spans, int count, uint32_t l01, uint32_t l23,
        const int both)
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
        xor_column(pixels + stdl_row_off(y, stride) + ((x >> 1) & ~7),
                   stride, rows, bit, l01, l23, both);

        if (maskbase != NULL) {
            uint8_t *mp = maskbase + stdl_row_off(y, maskstride)
                        + ((x >> 4) * 2);
            uint16_t nb = (uint16_t)~bit;

            n = rows;
            do {
                *(uint16_t *)mp &= nb;
                mp += maskstride;
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
#define XOR_VSPANS(l01, l23, both) \
    drew = xor_vspans_run(dst, spans, count, (l01), (l23), (both))
    XOR_COL3(col, XOR_VSPANS);
#undef XOR_VSPANS
    if (drew && dst->mask != NULL) {
        dst->opaque_state = 0;
    }
}

void STDL_XorHSpans(STDL_Surface *dst, const STDL_Span *spans,
                    int count, uint8_t col)
{
    uint8_t *pixels, *maskbase;
    uint16_t stride, maskstride;
    int cx0, cx1, cy0, cy1, i, drew = 0;
    uint32_t l01, l23;
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
    l01 = XOR_L01(col);
    l23 = XOR_L23(col);

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
        xor_row((uint32_t *)(pixels + stdl_row_off(y, stride) + g0 * 8),
                g1 - g0, lm, rm, l01, l23, 1);
        if (maskbase != NULL) {
            xor_mask_row(maskbase + stdl_row_off(y, maskstride),
                         g0, g1, lm, rm);
        }
    }
    if (drew && dst->mask != NULL) {
        dst->opaque_state = 0;
    }
}
