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
    if (dx >= -32768 && dx <= 32767 && dy <= 32767) {
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
        /* the exact x at row y: a->x + t * dx / dy, rounded up. In 32
         * bits while the product fits, as it does for anything near a
         * screen; 64 for an edge from the far ends of int16_t */
        const int t = y - a->y;
        long long fq, fr;

        if (t < 32768 && dx > -32768 && dx < 32768) {
            const int32_t num = (int32_t)t * dx;

            fq = num / dy;
            fr = num - (int32_t)fq * dy;
        } else {
            const long long num = (long long)t * dx;

            fq = num / dy;
            fr = num - fq * dy;
        }
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
 * row whose left is then past its right is empty - and each is held
 * within [lo - 1, hi + 1] too, before it is narrowed to sixteen
 * bits: an edge out at x = -32768 would otherwise wrap to a column
 * the far side of the screen and turn an empty row into one 32000
 * pixels long. A tight loop of its
 * own, so its few values stay in registers - stepped inside the fill,
 * with everything else live, gcc kept them on the stack. 0 when the
 * side ran out (the polygon's bottom).
 */
/*
 * A segment's columns with nothing to clamp, sixteen bits throughout:
 * the polygon lies within the clip's columns and is under 32768 rows
 * tall (see free_ in STDL_FillPolygon). Five instructions a row where
 * the clamped loop takes about ten: on a glenz's triangles, about 30
 * rows each, the side walkers had been costing more than the edge
 * set-ups. PRIMCOST on a plain ST: the 22-row triangle 2.71 ->
 * 2.29ms, the 160-row diamond 17.3 -> 15.1.
 * Returns the column after the last; *ep is the remainder in and out.
 */
static __inline__ int side_steps(int16_t *out, int rows, int x, int *ep,
                                 int q, int r, int dy)
{
#ifdef __m68k__
    int16_t n = (int16_t)(rows - 1), xx = (int16_t)x, ee = (int16_t)*ep;

    __asm__ volatile(
        "1:\n\t"
        "move.w %[x],(%[o])+\n\t"
        "add.w  %[q],%[x]\n\t"
        "sub.w  %[r],%[e]\n\t"
        "bpl.s  2f\n\t"
        "add.w  %[dy],%[e]\n\t"
        "addq.w #1,%[x]\n"
        "2:\n\t"
        "dbf    %[n],1b"
        : [o] "+a"(out), [x] "+d"(xx), [e] "+d"(ee), [n] "+d"(n)
        : [q] "d"((int16_t)q), [r] "d"((int16_t)r), [dy] "d"((int16_t)dy)
        : "cc", "memory");
    *ep = ee;
    return xx;
#else
    /* C twin - what tests/host exercises and the asm must match */
    int er = *ep;

    while (rows-- > 0) {
        *out++ = (int16_t)x;
        x += q;
        er -= r;
        if (er < 0) {
            er += dy;
            x++;
        }
    }
    *ep = er;
    return x;
#endif
}

#define SIDE(NAME, CLAMP)                                                 \
static int NAME(Edge *e, const STDL_Point *pts, int n, int dir,          \
                int bottom, int y, int y1, int16_t *out, int lo, int hi, \
                int off, int free_)                                       \
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
        if (free_) {                                                      \
            x = side_steps(out, stop - y, x, &er, q, r, dy);              \
            out += stop - y;                                              \
            y = stop;                                                     \
        }                                                                 \
        for (; y < stop; y++) {                                           \
            *out++ = (int16_t)(CLAMP(x, lo, hi));                         \
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
/* the left column at least lo, the right at most hi; each also kept
 * one past the other bound, so a row off the clip stays empty */
#define CLAMP_LO(x, lo, hi) \
    ((x) < (lo) ? (lo) : (x) > (hi) + 1 ? (hi) + 1 : (x))
#define CLAMP_HI(x, lo, hi) \
    ((x) > (hi) ? (hi) : (x) < (lo) - 1 ? (lo) - 1 : (x))
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

/* the mask words under a span: drawn pixels become opaque, or - a
 * transparent SET - transparent */
static void mask_row(uint16_t *mw, int x1, int x2, int transparent)
{
    int n = (x2 >> 4) - (x1 >> 4);
    uint16_t lm = lmask[x1 & 15], rm = rmask[x2 & 15];

    mw += x1 >> 4;
    if (n == 0) {
        lm &= rm;
        *mw = transparent ? (uint16_t)(*mw | lm) : (uint16_t)(*mw & ~lm);
        return;
    }
    *mw = transparent ? (uint16_t)(*mw | lm) : (uint16_t)(*mw & ~lm);
    mw++;
    while (--n != 0) {
        *mw++ = transparent ? 0xFFFFu : 0;
    }
    *mw = transparent ? (uint16_t)(*mw | rm) : (uint16_t)(*mw & ~rm);
}

/* rows a chunk: the column tables are static, and a polygon taller
 * than this is drawn a chunk at a time */
#define CHUNK 100
static int16_t tab_l[CHUNK], tab_r[CHUNK];

/* +1 when walking forwards from the top vertex goes down the left
 * side, -1 when it goes down the right, 0 for no area. With y down
 * the screen, the cross product of the top vertex's two edges says
 * which, in two 16-bit multiplies - compared, not subtracted, so
 * nothing overflows; only a top vertex its neighbours run straight
 * through needs the polygon's whole signed area. */
static int orientation(const STDL_Point *pts, int n, int top)
{
    const STDL_Point *t = &pts[top];
    const STDL_Point *nx = &pts[top + 1 < n ? top + 1 : 0];
    const STDL_Point *pv = &pts[top > 0 ? top - 1 : n - 1];
    const int ux = nx->x - t->x, uy = nx->y - t->y;
    const int vx = pv->x - t->x, vy = pv->y - t->y;
    long long area = 0;
    int i;

    if (ux >= -32768 && ux <= 32767 && vx >= -32768 && vx <= 32767
        && uy <= 32767 && vy <= 32767) {
        const int32_t a = STDL_MulS16(ux, vy), b = STDL_MulS16(uy, vx);

        if (a != b) {
            return a < b ? 1 : -1;
        }
    }
    for (i = 0; i < n; i++) {
        const STDL_Point *p = &pts[i], *q = &pts[i + 1 < n ? i + 1 : 0];

        area += (long long)p->x * q->y - (long long)q->x * p->y;
    }
    return area < 0 ? 1 : area > 0 ? -1 : 0;
}

void STDL_FillPolygon(STDL_Surface *dst, const STDL_Point *pts, int n,
                      uint8_t col, int op)
{
    int i, top = 0, bottom, y, y2, lo, hi, dir, transparent, free_;
    int minx, maxx;
    uint32_t l01, l23, k01 = 0, k23 = 0;
    rows_fn fill;
    Edge el, er;

    if (dst == NULL || pts == NULL || n < 3 || dst->pixels == NULL) {
        return;
    }
    /* as STDL_FillRect: STDL_TRANSPARENT punches a hole in a masked
     * surface and is colour 0 on one without; the plane operations
     * take bits 0-3, as the XOR primitives do */
    transparent = op == STDL_DRAW_SET && col >= STDL_TRANSPARENT
                  && dst->mask != NULL;
    col &= STDL_COL_MASK;
    if (transparent) {
        col = 0;
    }
    if (col == 0 && op != STDL_DRAW_SET) {
        return;                 /* no plane to flip, set or clear */
    }
    bottom = pts[0].y;
    minx = maxx = pts[0].x;
    for (i = 1; i < n; i++) {
        if (pts[i].y < pts[top].y) {
            top = i;
        }
        if (pts[i].y > bottom) {
            bottom = pts[i].y;
        }
        if (pts[i].x < minx) {
            minx = pts[i].x;
        }
        if (pts[i].x > maxx) {
            maxx = pts[i].x;
        }
    }
    dir = orientation(pts, n, top);
    if (dir == 0) {
        return;                 /* no area: a line, or a point */
    }

    /* the surface's own coordinates: like the other drawing
     * primitives, and unlike blits and rectangle fills, a polygon
     * does not take the surface origin */
    lo = dst->clip.x;
    hi = dst->clip.x + dst->clip.w - 1;
    y = pts[top].y;
    y2 = bottom;
    if (y < dst->clip.y) {
        y = dst->clip.y;
    }
    if (y2 > dst->clip.y + dst->clip.h) {
        y2 = dst->clip.y + dst->clip.h;
    }
    if (y >= y2 || lo > hi) {
        return;
    }
    /* every edge lies between its vertices, so a polygon within the
     * clip's columns has nothing to clamp, and one under 32768 rows
     * tall keeps every edge's remainder in sixteen bits */
    free_ = minx >= lo && maxx <= hi + 1 && bottom - pts[top].y <= 32767;
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
        uint8_t *row = dst->pixels + stdl_row_off(y, dst->stride);

        if (!side_left(&el, pts, n, dir, bottom, y, y1, tab_l, lo, hi, 0,
                       free_)
            || !side_right(&er, pts, n, -dir, bottom, y, y1, tab_r, lo,
                           hi, 1, free_)) {
            break;
        }
        fill(row, dst->stride, tab_l, tab_r, rows, l01, l23, k01, k23);
        if (dst->mask != NULL) {
            uint8_t *mrow = dst->mask + stdl_row_off(y, dst->maskstride);

            for (i = 0; i < rows; i++, mrow += dst->maskstride) {
                if (tab_l[i] <= tab_r[i]) {
                    mask_row((uint16_t *)mrow, tab_l[i], tab_r[i],
                             transparent);
                }
            }
        }
        y = y1;
    }
}
