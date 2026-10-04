/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * Filled convex polygons, set, XORed, ORed or cleared into the planes
 * of a colour.
 *
 * A filled polygon used to mean building a span list and handing it
 * to STDL_HSpans or STDL_XorHSpans, which clip, find the row and work
 * out the edge masks for every span on its own: one demo measured
 * over a thousand cycles a span for a glenz object, where its own
 * filler stepped the edges in registers for about a quarter of that.
 * Here the edges are stepped down the rows and the row address walks
 * by the stride. The operation is chosen once a polygon, so the row
 * loop has no test for it, and the edge masks come from tables.
 *
 * Coverage follows the rectangle's rule: a pixel is drawn when its
 * row is in [top, bottom) and its column in [left, right), with left
 * and right the polygon's exact edges at that row. So polygons that
 * share an edge tile with no gap and no pixel drawn twice - which is
 * what XOR needs, since a shared edge drawn twice would cancel - and
 * the polygon (x,y) (x+w,y) (x+w,y+h) (x,y+h) fills exactly the
 * rectangle {x, y, w, h}. The edges are stepped exactly, an integer
 * and a remainder, so no rounding drifts down a long edge.
 *
 * Its own translation unit: a program that never fills a polygon
 * does not carry it.
 */

#include "stdl_internal.h"

/* 0xFFFF >> n, then 0xFFFF << (15 - n): a table read where the shift
 * costs 8 + 2n cycles */
static const uint16_t edge_masks[32] = {
    0xFFFF, 0x7FFF, 0x3FFF, 0x1FFF, 0x0FFF, 0x07FF, 0x03FF, 0x01FF,
    0x00FF, 0x007F, 0x003F, 0x001F, 0x000F, 0x0007, 0x0003, 0x0001,
    0x8000, 0xC000, 0xE000, 0xF000, 0xF800, 0xFC00, 0xFE00, 0xFF00,
    0xFF80, 0xFFC0, 0xFFE0, 0xFFF0, 0xFFF8, 0xFFFC, 0xFFFE, 0xFFFF
};
#define lmask edge_masks
#define rmask (edge_masks + 16)

/*
 * One side of the polygon, walked from the top vertex down. At each
 * row the edge's exact x is x - e/dy with 0 <= e < dy, so the first
 * column at or right of it is x itself.
 */
typedef struct {
    int x, e, q, r, dy;     /* column, remainder, step, its fraction */
    int yend;               /* the row the edge ends on (exclusive) */
    int v;                  /* the vertex it ends at */
} Edge;

/* Set e up for the edge from vertex a to vertex b (b below a) at row
 * y, a's row or below. Only a clipped top makes y differ from a's
 * row, and only then is there a 32-bit product and divide - once an
 * edge, not once a row. */
static void edge_start(Edge *e, const STDL_Point *a, const STDL_Point *b,
                       int y)
{
    const int dx = b->x - a->x, dy = b->y - a->y;
    int q, r;

    /* one divs.w where the step fits sixteen bits, as it does for any
     * edge on a screen: a libgcc divide is about 630 cycles, and a
     * 24-face object has 72 edges a frame */
    if (dx >= -32768 && dx <= 32767) {
#ifdef __m68k__
        int32_t qr = dx;

        __asm__("divs.w %1,%0" : "+d"(qr) : "d"((int16_t)dy));
        q = (int16_t)qr;
        r = (int16_t)(qr >> 16);
#else
        q = dx / dy;
        r = dx % dy;
#endif
    } else {
        q = dx / dy;
        r = dx - q * dy;
    }
    if (r < 0) {            /* floor division: 0 <= r < dy */
        r += dy;
        q--;
    }
    e->q = q;
    e->r = r;
    e->dy = dy;
    e->x = a->x;
    e->e = 0;
    e->yend = b->y;
    if (y > a->y) {
        /* the exact x at row y: a->x + t * dx / dy, rounded up */
        const long num = (long)(y - a->y) * dx;
        long fq = num / dy, fr = num - fq * dy;

        if (fr < 0) {
            fr += dy;
            fq--;
        }
        e->x = a->x + (int)fq;
        e->e = 0;
        if (fr > 0) {
            e->x++;
            e->e = dy - (int)fr;
        }
    }
}

/* Move to the next edge of a side that reaches below row y; dir +1
 * walks the vertices forwards, -1 backwards. 0 when the side has
 * reached the bottom. */
static int edge_next(Edge *e, const STDL_Point *pts, int n, int dir,
                     int y, int bottom)
{
    while (e->yend <= y) {
        int a = e->v, b = a + dir;

        if (b == n) b = 0;
        if (b < 0) b = n - 1;
        if (pts[a].y >= bottom) {
            return 0;
        }
        if (pts[b].y > y) {
            edge_start(e, &pts[a], &pts[b], y);
        } else {
            e->yend = pts[b].y;     /* flat, or above the row: skip */
        }
        e->v = b;
    }
    return 1;
}

/*
 * One side's columns for rows [y, y1), into out[]: the left side's
 * first column clamped up to lo, the right side's last (its edge less
 * one) clamped down to hi, both in the surface's storage columns. A
 * row whose left is then past its right is empty. A tight loop of its
 * own, so its few values stay in registers - stepped inside the fill,
 * with everything else live, gcc kept them on the stack. 0 when the
 * side ran out (the polygon's bottom).
 */
#define SIDE(NAME, CLAMP)                                                 \
static int NAME(Edge *e, const STDL_Point *pts, int n, int dir,          \
                int bottom, int y, int y1, int16_t *out, int lim, int off)\
{                                                                         \
    while (y < y1) {                                                      \
        int stop, x, er, q, r, dy;                                        \
                                                                          \
        if (e->yend <= y && !edge_next(e, pts, n, dir, y, bottom)) {      \
            return 0;                                                     \
        }                                                                 \
        stop = e->yend < y1 ? e->yend : y1;                               \
        x = e->x - off;                                                   \
        er = e->e;                                                        \
        q = e->q;                                                         \
        r = e->r;                                                         \
        dy = e->dy;                                                       \
        for (; y < stop; y++) {                                           \
            *out++ = (int16_t)(CLAMP(x, lim));                            \
            x += q;                                                       \
            er -= r;                                                      \
            if (er < 0) {                                                 \
                er += dy;                                                 \
                x++;                                                      \
            }                                                             \
        }                                                                 \
        e->x = x + off;                                                   \
        e->e = er;                                                        \
    }                                                                     \
    return 1;                                                             \
}
#define CLAMP_LO(x, lo) ((x) < (lo) ? (lo) : (x))
#define CLAMP_HI(x, hi) ((x) > (hi) ? (hi) : (x))
SIDE(side_left, CLAMP_LO)
SIDE(side_right, CLAMP_HI)

/*
 * The fill of one span [x1, x2] (inclusive, clipped) of row g, as C:
 * the twin of the 68000 rows below on the host, and the only form of
 * OR and CLEAR. The pair arguments are constants in every copy, so
 * the group loop holds no test for the plane pair 2-3 (P23) or for
 * the operation. SET stores the middle groups outright; XOR flips
 * the colour's planes; ANDOR clears the planes in k and sets those in
 * l, which is OR with k = 0 and CLEAR with l = 0.
 */
#define OP_SET(d, l, k, m)   ((d) ^= ((d) ^ (l)) & (m))
#define OP_XOR(d, l, k, m)   ((d) ^= (l) & (m))
#define OP_ANDOR(d, l, k, m) ((d) = ((d) & ~((k) & (m))) | ((l) & (m)))
#define MID_SET(d, l, k)     ((d) = (l))
#define MID_XOR(d, l, k)     ((d) ^= (l))
#define MID_ANDOR(d, l, k)   ((d) = ((d) & ~(k)) | (l))

#define ROW_OP(NAME, OP, MID, P23)                                        \
static __inline__ __attribute__((always_inline)) void NAME(               \
        stdl_wlong *g, int x1, int x2, uint32_t l01, uint32_t l23,        \
        uint32_t k01, uint32_t k23)                                       \
{                                                                         \
    int n = (x2 >> 4) - (x1 >> 4);                                        \
    uint16_t lm = lmask[x1 & 15], rm = rmask[x2 & 15];                    \
    uint32_t m;                                                           \
                                                                          \
    g += (x1 >> 4) << 1;                                                  \
    if (n == 0) {                                                         \
        lm &= rm;                                                         \
    }                                                                     \
    m = ((uint32_t)lm << 16) | lm;                                        \
    OP(g[0], l01, k01, m);                                                \
    if (P23) { OP(g[1], l23, k23, m); }                                   \
    if (n == 0) {                                                         \
        return;                                                           \
    }                                                                     \
    g += 2;                                                               \
    while (--n != 0) {                                                    \
        MID(g[0], l01, k01);                                              \
        if (P23) { MID(g[1], l23, k23); }                                 \
        g += 2;                                                           \
    }                                                                     \
    m = ((uint32_t)rm << 16) | rm;                                        \
    OP(g[0], l01, k01, m);                                                \
    if (P23) { OP(g[1], l23, k23, m); }                                   \
}

ROW_OP(row_set2, OP_SET, MID_SET, 0)
ROW_OP(row_set4, OP_SET, MID_SET, 1)
ROW_OP(row_xor2, OP_XOR, MID_XOR, 0)
ROW_OP(row_xor4, OP_XOR, MID_XOR, 1)
ROW_OP(row_andor2, OP_ANDOR, MID_ANDOR, 0)
ROW_OP(row_andor4, OP_ANDOR, MID_ANDOR, 1)

/* rows from the column tables, in C */
#define C_ROWS(NAME, ROW)                                                 \
static void NAME(uint8_t *row, int32_t stride, const int16_t *xl,         \
                 const int16_t *xr, int rows, uint32_t l01, uint32_t l23, \
                 uint32_t k01, uint32_t k23)                              \
{                                                                         \
    for (; rows > 0; rows--, row += stride, xl++, xr++) {                 \
        if (*xl <= *xr) {                                                 \
            ROW((stdl_wlong *)row, *xl, *xr, l01, l23, k01, k23);         \
        }                                                                 \
    }                                                                     \
}
C_ROWS(rows_andor2, row_andor2)
C_ROWS(rows_andor4, row_andor4)
#ifndef __m68k__
C_ROWS(rows_set2, row_set2)
C_ROWS(rows_set4, row_set4)
C_ROWS(rows_xor2, row_xor2)
C_ROWS(rows_xor4, row_xor4)
#else
/*
 * SET and XOR rows from the column tables, in 68000: two table reads,
 * the edge masks from edge_masks, the middle groups as stores or
 * eor.l to memory. About 30 instructions of a row's set-up against
 * the C's hundred; written once and stamped out per operation and
 * plane pair (EDGE01/EDGE23 merge the edge groups under the long
 * mask in register M, MID the groups between).
 */
#define XOR_EDGE(M, OFF, L) \
    "move.l " L ",%%d7\n\t" "and.l " M ",%%d7\n\t" \
    "eor.l %%d7," OFF "(%%a3)\n\t"
#define SET_EDGE(M, OFF, L) \
    "move.l " OFF "(%%a3),%%d7\n\t" "eor.l " L ",%%d7\n\t" \
    "and.l " M ",%%d7\n\t" "eor.l %%d7," OFF "(%%a3)\n\t"
#define ROWS_ASM(NAME, EDGE, MID, P23)                                    \
static void NAME(uint8_t *row, int32_t stride, const int16_t *xl,         \
                 const int16_t *xr, int rows, uint32_t l01, uint32_t l23, \
                 uint32_t k01, uint32_t k23)                              \
{                                                                         \
    int16_t h = (int16_t)rows;                                            \
                                                                          \
    (void)k01;                                                            \
    (void)k23;                                                            \
    __asm__ volatile(                                                     \
        "1:\n\t"                                                          \
        "move.w (%[L])+,%%d0\n\t"                                         \
        "move.w (%[R])+,%%d1\n\t"                                         \
        "cmp.w  %%d0,%%d1\n\t"                                            \
        "blt    9f\n\t"                 /* empty row */                   \
        "move.w %%d0,%%d2\n\t"                                            \
        "lsr.w  #4,%%d2\n\t"            /* first group */                 \
        "move.w %%d1,%%d3\n\t"                                            \
        "lsr.w  #4,%%d3\n\t"                                              \
        "sub.w  %%d2,%%d3\n\t"          /* groups after it */             \
        "lsl.w  #3,%%d2\n\t"                                              \
        "lea    0(%[row],%%d2.w),%%a3\n\t"                                \
        "moveq  #15,%%d7\n\t"                                             \
        "and.w  %%d7,%%d0\n\t"                                            \
        "add.w  %%d0,%%d0\n\t"                                            \
        "move.w 0(%[t],%%d0.w),%%d0\n\t"  /* left mask */                 \
        "and.w  %%d7,%%d1\n\t"                                            \
        "add.w  %%d1,%%d1\n\t"                                            \
        "move.w 32(%[t],%%d1.w),%%d1\n\t" /* right mask */                \
        "tst.w  %%d3\n\t"                                                 \
        "bne.s  2f\n\t"                                                   \
        "and.w  %%d1,%%d0\n\t"          /* one group */                   \
        "move.w %%d0,%%d7\n\t" "swap %%d0\n\t" "move.w %%d7,%%d0\n\t"     \
        EDGE("%%d0", "0", "%[l01]")                                       \
        P23(EDGE("%%d0", "4", "%[l23]"))                                  \
        "bra    9f\n"                                                     \
        "2:\n\t"                                                          \
        "move.w %%d0,%%d7\n\t" "swap %%d0\n\t" "move.w %%d7,%%d0\n\t"     \
        EDGE("%%d0", "0", "%[l01]")                                       \
        P23(EDGE("%%d0", "4", "%[l23]"))                                  \
        "addq.l #8,%%a3\n\t"                                              \
        "subq.w #2,%%d3\n\t"                                              \
        "bmi.s  4f\n"                                                     \
        "3:\n\t"                                                          \
        MID                                                               \
        "dbf    %%d3,3b\n"                                                \
        "4:\n\t"                                                          \
        "move.w %%d1,%%d7\n\t" "swap %%d1\n\t" "move.w %%d7,%%d1\n\t"     \
        EDGE("%%d1", "0", "%[l01]")                                       \
        P23(EDGE("%%d1", "4", "%[l23]"))                                  \
        "9:\n\t"                                                          \
        "adda.l %[st],%[row]\n\t"                                         \
        "subq.w #1,%[h]\n\t"                                              \
        "bne    1b"                                                       \
        : [row] "+a"(row), [L] "+a"(xl), [R] "+a"(xr), [h] "+d"(h)        \
        : [t] "a"(edge_masks), [st] "a"(stride), [l01] "d"(l01),          \
          [l23] "d"(l23)                                                  \
        : "d0", "d1", "d2", "d3", "d7", "a3", "cc", "memory");            \
}
#define WITH23(x) x
#define NO23(x)
ROWS_ASM(rows_set2, SET_EDGE,
         "move.l %[l01],(%%a3)\n\t" "addq.l #8,%%a3\n\t", NO23)
ROWS_ASM(rows_set4, SET_EDGE,
         "move.l %[l01],(%%a3)+\n\t" "move.l %[l23],(%%a3)+\n\t", WITH23)
ROWS_ASM(rows_xor2, XOR_EDGE,
         "eor.l %[l01],(%%a3)\n\t" "addq.l #8,%%a3\n\t", NO23)
ROWS_ASM(rows_xor4, XOR_EDGE,
         "eor.l %[l01],(%%a3)+\n\t" "eor.l %[l23],(%%a3)+\n\t", WITH23)
#endif

typedef void (*rows_fn)(uint8_t *, int32_t, const int16_t *,
                        const int16_t *, int, uint32_t, uint32_t,
                        uint32_t, uint32_t);

/* the mask words under a span: drawn pixels become opaque */
static void mask_row(uint16_t *mw, int x1, int x2)
{
    int n = (x2 >> 4) - (x1 >> 4);
    uint16_t lm = lmask[x1 & 15], rm = rmask[x2 & 15];

    mw += x1 >> 4;
    if (n == 0) {
        *mw &= (uint16_t)~(lm & rm);
        return;
    }
    *mw++ &= (uint16_t)~lm;
    while (--n != 0) {
        *mw++ = 0;
    }
    *mw &= (uint16_t)~rm;
}

/* rows a chunk: the column tables are static, and a polygon taller
 * than this is drawn a chunk at a time */
#define CHUNK 100
static int16_t tab_l[CHUNK], tab_r[CHUNK];

void STDL_FillPolygon(STDL_Surface *dst, const STDL_Point *pts, int n,
                      uint8_t col, int op)
{
    int i, top = 0, bottom, y, y2, ox, oy, lo, hi, fwd_left;
    int32_t area = 0;
    uint32_t l01, l23, k01 = 0, k23 = 0;
    rows_fn fill;
    Edge el, er;

    if (dst == NULL || pts == NULL || n < 3 || dst->pixels == NULL) {
        return;
    }
    col &= STDL_COL_MASK;
    if (col == 0 && op != STDL_DRAW_SET) {
        return;                 /* no plane to flip, set or clear */
    }
    bottom = pts[0].y;
    for (i = 0; i < n; i++) {
        const STDL_Point *a = &pts[i], *b = &pts[i + 1 < n ? i + 1 : 0];

        if (a->y < pts[top].y) {
            top = i;
        }
        if (a->y > bottom) {
            bottom = a->y;
        }
        area += STDL_MulS16(a->x, b->y) - STDL_MulS16(b->x, a->y);
    }
    if (area == 0) {
        return;                 /* no area: a line, or a point */
    }
    /* with y down the screen, positive area is clockwise to the eye,
     * and walking forwards from the top then goes down the right */
    fwd_left = area < 0;

    /* edges in the program's coordinates, columns stored in the
     * surface's: the clip comes into the one, the origin off the
     * other */
    ox = dst->org_x;
    oy = dst->org_y;
    lo = dst->clip.x;
    hi = dst->clip.x + dst->clip.w - 1;
    y = pts[top].y;
    y2 = bottom;
    if (y < dst->clip.y + oy) {
        y = dst->clip.y + oy;
    }
    if (y2 > dst->clip.y + oy + dst->clip.h) {
        y2 = dst->clip.y + oy + dst->clip.h;
    }
    if (y >= y2 || lo > hi) {
        return;
    }
    el.v = er.v = top;
    el.yend = er.yend = pts[top].y;

    l01 = STDL_PACK2((col & 1) ? 0xFFFFu : 0, (col & 2) ? 0xFFFFu : 0);
    l23 = STDL_PACK2((col & 4) ? 0xFFFFu : 0, (col & 8) ? 0xFFFFu : 0);
    /* SET writes pair 2-3 whenever the budget has it, to clear it;
     * the others only when the colour has something there */
    if (op == STDL_DRAW_SET) {
        fill = stdl_planes > 2 ? rows_set4 : rows_set2;
    } else if (op == STDL_DRAW_XOR) {
        fill = l23 != 0 ? rows_xor4 : rows_xor2;
    } else {
        if (op == STDL_DRAW_CLEAR) {
            k01 = l01;
            k23 = l23;
            l01 = l23 = 0;
        }
        fill = (k23 | l23) != 0 ? rows_andor4 : rows_andor2;
    }
    if (dst->mask != NULL) {
        dst->opaque_state = 0;
    }

    while (y < y2) {
        const int y1 = y + CHUNK < y2 ? y + CHUNK : y2;
        const int rows = y1 - y;
        uint8_t *row = dst->pixels + stdl_row_off(y - oy, dst->stride);

        if (!side_left(&el, pts, n, fwd_left ? 1 : -1, bottom, y, y1,
                       tab_l, lo, ox)
            || !side_right(&er, pts, n, fwd_left ? -1 : 1, bottom, y, y1,
                           tab_r, hi, ox + 1)) {
            break;
        }
        fill(row, dst->stride, tab_l, tab_r, rows, l01, l23, k01, k23);
        if (dst->mask != NULL) {
            uint8_t *mrow = dst->mask + stdl_row_off(y - oy, dst->maskstride);

            for (i = 0; i < rows; i++, mrow += dst->maskstride) {
                if (tab_l[i] <= tab_r[i]) {
                    mask_row((uint16_t *)mrow, tab_l[i], tab_r[i]);
                }
            }
        }
        y = y1;
    }
}
