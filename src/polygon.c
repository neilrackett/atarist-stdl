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
 * set-ups. PRIMCOST on a plain ST, when its cases still came this
 * way: the 22-row triangle 2.71 -> 2.29ms, the 160-row diamond 17.3
 * -> 15.1. (Since the 16.16 path below, those take that, and this is
 * the path of polygons it declines.)
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

/* XOR into a single plane: g is that plane's word in group 0, and
 * the plane's words are four apart */
static __inline__ __attribute__((always_inline)) void row_xor1(
        stdl_wlong *gl, int x1, int x2, uint32_t l01, uint32_t l23,
        uint32_t k01, uint32_t k23)
{
    uint16_t *g = (uint16_t *)gl + ((x1 >> 4) << 2);
    int n = (x2 >> 4) - (x1 >> 4);
    uint16_t lm = lmask[x1 & 15], rm = rmask[x2 & 15];

    (void)l01;
    (void)l23;
    (void)k01;
    (void)k23;
    if (n == 0) {
        *g ^= (uint16_t)(lm & rm);
        return;
    }
    *g ^= lm;
    g += 4;
    while (--n != 0) {
        *g = (uint16_t)~*g;
        g += 4;
    }
    *g ^= rm;
}

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
C_ROWS(rows_xor1, row_xor1)
C_ROWS(rows_xor2, row_xor2)
C_ROWS(rows_xor4, row_xor4)
#else
/*
 * SET and XOR rows from the column tables, in 68000: two table reads,
 * the edge masks from edge_masks, the middle groups as stores or
 * eor.l to memory. About 30 instructions of a row's set-up against
 * the C's hundred; written once and stamped out per operation and
 * plane count (EDGE merges an edge group under the mask in register
 * M - copied into both words by REP where a pair is written - and MID
 * does the groups between).
 */
#define XOR_EDGE(M, OFF, L) \
    "move.l " L ",%%d7\n\t" "and.l " M ",%%d7\n\t" \
    "eor.l %%d7," OFF "(%%a3)\n\t"
#define SET_EDGE(M, OFF, L) \
    "move.l " OFF "(%%a3),%%d7\n\t" "eor.l " L ",%%d7\n\t" \
    "and.l " M ",%%d7\n\t" "eor.l %%d7," OFF "(%%a3)\n\t"
#define XOR1_EDGE(M, OFF, L) "eor.w " M ",(%%a3)\n\t"
#define REP2(M) "move.w " M ",%%d7\n\t" "swap " M "\n\t" \
                "move.w %%d7," M "\n\t"
#define REP1(M)
#define ROWS_ASM(NAME, EDGE, MID, P23, REP)                               \
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
        "moveq  #-16,%%d2\n\t"                                            \
        "and.w  %%d0,%%d2\n\t"                                            \
        "eor.w  %%d2,%%d0\n\t"          /* the left's column in group */  \
        "moveq  #-16,%%d3\n\t"                                            \
        "and.w  %%d1,%%d3\n\t"                                            \
        "eor.w  %%d3,%%d1\n\t"                                            \
        "lsr.w  #1,%%d2\n\t"            /* its group's byte offset */     \
        "lea    0(%[row],%%d2.w),%%a3\n\t"                                \
        "add.w  %%d0,%%d0\n\t"                                            \
        "move.w 0(%[t],%%d0.w),%%d0\n\t"  /* left mask */                 \
        "add.w  %%d1,%%d1\n\t"                                            \
        "move.w 32(%[t],%%d1.w),%%d1\n\t" /* right mask */                \
        "lsr.w  #1,%%d3\n\t"                                              \
        "sub.w  %%d2,%%d3\n\t"          /* 8 x the groups after it */     \
        "bne.s  2f\n\t"                                                   \
        "and.w  %%d1,%%d0\n\t"          /* one group */                   \
        REP("%%d0")                                                       \
        EDGE("%%d0", "0", "%[l01]")                                       \
        P23(EDGE("%%d0", "4", "%[l23]"))                                  \
        "bra    9f\n"                                                     \
        "2:\n\t"                                                          \
        REP("%%d0")                                                       \
        EDGE("%%d0", "0", "%[l01]")                                       \
        P23(EDGE("%%d0", "4", "%[l23]"))                                  \
        "addq.l #8,%%a3\n\t"                                              \
        "lsr.w  #3,%%d3\n\t"                                              \
        "subq.w #2,%%d3\n\t"                                              \
        "bmi.s  4f\n"                                                     \
        "3:\n\t"                                                          \
        MID                                                               \
        "dbf    %%d3,3b\n"                                                \
        "4:\n\t"                                                          \
        REP("%%d1")                                                       \
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
         "move.l %[l01],(%%a3)\n\t" "addq.l #8,%%a3\n\t", NO23, REP2)
ROWS_ASM(rows_set4, SET_EDGE,
         "move.l %[l01],(%%a3)+\n\t" "move.l %[l23],(%%a3)+\n\t", WITH23,
         REP2)
ROWS_ASM(rows_xor1, XOR1_EDGE,
         "not.w (%%a3)\n\t" "addq.l #8,%%a3\n\t", NO23, REP1)
ROWS_ASM(rows_xor2, XOR_EDGE,
         "eor.l %[l01],(%%a3)\n\t" "addq.l #8,%%a3\n\t", NO23, REP2)
ROWS_ASM(rows_xor4, XOR_EDGE,
         "eor.l %[l01],(%%a3)+\n\t" "eor.l %[l23],(%%a3)+\n\t", WITH23,
         REP2)
#endif

typedef void (*rows_fn)(uint8_t *, int32_t, const int16_t *,
                        const int16_t *, int, uint32_t, uint32_t,
                        uint32_t, uint32_t);

/*
 * The fast path: no column tables. Both edges are stepped in 16.16
 * fixed point inside the row loop, a trapezoid at a time - the rows
 * between two vertices, where neither edge changes - as a demo's own
 * filler does it. The tables cost a store and a load a row on each
 * side, the clamped stepping besides, and the call into the row loop
 * nine arguments. With the planes below, this took 24 one-plane
 * glenz-like triangles from 85ms to 58 on a plain ST (PRIMCOST); the
 * 68000 walk below, to 49.
 *
 * 16.16 is exact here. The left edge's first column at row a.y + t
 * is ceil(a.x + t * dx / dy). With the step s = floor(65536 * dx /
 * dy), s falls short of the true slope by f / 65536 for some f in
 * [0, 1), so after t rows the stepped x is short by under t / 65536.
 * The true x is a whole column plus k / dy; short of a whole column
 * by less than one, it rounds up to the same column, and with k >= 1
 * it stays above the column below as long as t / 65536 < 1 / dy -
 * which, since t < dy, holds for every dy up to 256. (The right's
 * last column is the same ceiling less one.) So for edges up to 256
 * rows this draws exactly the pixels the tables do, and
 * STDL_FillPolygon takes it only for a polygon no taller than that,
 * wholly inside the clip's columns and with no mask to keep:
 * anything else goes the long way.
 */
typedef struct {
    uint8_t *row;           /* this row's group 0, plane offset added */
    int32_t xl, xr;         /* the left's first column, the right's last */
    int32_t sl, sr;         /* their steps a row */
    uint8_t *end;           /* the row after the trapezoid's last */
    uint32_t l01, l23;      /* the colour's plane words */
    const uint16_t *masks;  /* edge_masks, for the 68000 */
    int32_t stride;
    uint32_t k01, k23;      /* the planes OR and CLEAR clear */
} Trap;
typedef void (*trap_fn)(Trap *);

/* rows from two stepped edges, in C: the 68000's twin below, and the
 * only form of OR and CLEAR */
#define C_TRAP(NAME, ROW)                                                 \
static void NAME(Trap *t)                                                 \
{                                                                         \
    uint8_t *row = t->row;                                                \
    int32_t xl = t->xl, xr = t->xr;                                       \
                                                                          \
    for (; row != t->end; row += t->stride) {                             \
        const int a = (int16_t)(xl >> 16), b = (int16_t)(xr >> 16);       \
                                                                          \
        if (a <= b) {                                                     \
            ROW((stdl_wlong *)row, a, b, t->l01, t->l23, t->k01, t->k23); \
        }                                                                 \
        xl += t->sl;                                                      \
        xr += t->sr;                                                      \
    }                                                                     \
    t->row = row;                                                         \
    t->xl = xl;                                                           \
    t->xr = xr;                                                           \
}
#ifndef __m68k__
C_TRAP(trap_andor2, row_andor2)
C_TRAP(trap_andor4, row_andor4)
C_TRAP(stdl_trap_set2, row_set2)
C_TRAP(stdl_trap_set4, row_set4)
C_TRAP(stdl_trap_xor1, row_xor1)
C_TRAP(stdl_trap_xor2, row_xor2)
C_TRAP(stdl_trap_xor4, row_xor4)
#else
/*
 * The same in 68000, and the walk with it: one call a polygon. The
 * walk is the C's (fill_traps) - each side's next edge, its step from
 * a divs.w and a divu.w of the remainder, its start - with its state
 * in a frame on the stack, and between edges it calls one of the row
 * loops below. In C, with a call into the rows a trapezoid at a
 * time, a 2-row triangle took 0.99ms on a plain ST; with this, 0.83.
 *
 * The row loops take their state in registers and leave the frame
 * alone: a0 stride, a1 edge masks, a2 row, a4/a5 the edges' steps, a6
 * the row to stop at, d4/d5 the edges in 16.16, d6/d7 the plane
 * words; d0-d3 and a3 are theirs. The row body is ROWS_ASM's; only
 * the column fetch (the two positions' whole parts) and the step at
 * the row's end differ.
 */
typedef char surface_layout[offsetof(STDL_Surface, stride) == 8
                           && offsetof(STDL_Surface, clip) == 16 ? 1 : -1];
int stdl_poly_fast(const STDL_Surface *dst, const STDL_Point *pts, int n,
                   void (*rows)(void), int off, uint32_t l01, uint32_t l23,
                   const uint16_t *masks);
void stdl_prows_set2(void);
void stdl_prows_set4(void);
void stdl_prows_xor1(void);
void stdl_prows_xor2(void);
void stdl_prows_xor4(void);

#define T_STR2(x) #x
#define T_STR(x) T_STR2(x)
#define T_SYM(n) T_STR(__USER_LABEL_PREFIX__) #n
#define TX_EDGE(M, A, L) \
    "move.l " L ",%d2\n\t" "and.l " M ",%d2\n\t" "eor.l %d2," A "\n\t"
#define TS_EDGE(M, A, L) \
    "move.l " A ",%d2\n\t" "eor.l " L ",%d2\n\t" "and.l " M ",%d2\n\t" \
    "eor.l %d2," A "\n\t"
#define T1_EDGE(M, A, L) "eor.w " M "," A "\n\t"
#define T_REP2(M) "move.w " M ",%d2\n\t" "swap " M "\n\t" \
                  "move.w %d2," M "\n\t"
#define T_REP1(M)
/* BR is the empty-row branch's size: short where the body allows */
#define PROWS_ASM(NAME, EDGE, MID, P23, REP, BR)                          \
__asm__(                                                                  \
    "\t.text\n\t.even\n\t.globl " T_SYM(NAME) "\n"                        \
    T_SYM(NAME) ":\n"                                                     \
    "1:\n\t"                                                              \
    "move.l %d4,%d0\n\t"                                                  \
    "swap   %d0\n\t"                    /* the left's first column */     \
    "move.l %d5,%d1\n\t"                                                  \
    "swap   %d1\n\t"                    /* the right's last */            \
    "cmp.w  %d0,%d1\n\t"                                                  \
    "blt" BR " 9f\n\t"                  /* an empty row */                \
    "moveq  #-16,%d2\n\t"                                                 \
    "and.w  %d0,%d2\n\t"                                                  \
    "eor.w  %d2,%d0\n\t"                /* the left's column in group */  \
    "moveq  #-16,%d3\n\t"                                                 \
    "and.w  %d1,%d3\n\t"                                                  \
    "eor.w  %d3,%d1\n\t"                                                  \
    "lsr.w  #1,%d2\n\t"                 /* its group's byte offset */     \
    "lea    0(%a2,%d2.w),%a3\n\t"                                         \
    "add.w  %d0,%d0\n\t"                                                  \
    "move.w 0(%a1,%d0.w),%d0\n\t"       /* left mask */                   \
    "add.w  %d1,%d1\n\t"                                                  \
    "move.w 32(%a1,%d1.w),%d1\n\t"      /* right mask */                  \
    "lsr.w  #1,%d3\n\t"                                                   \
    "sub.w  %d2,%d3\n\t"                /* 8 x the groups after it */     \
    "bne.s  2f\n\t"                                                       \
    "and.w  %d1,%d0\n\t"                /* one group */                   \
    REP("%d0")                                                            \
    EDGE("%d0", "(%a3)", "%d6")                                           \
    P23(EDGE("%d0", "4(%a3)", "%d7"))                                     \
    "bra" BR " 9f\n"                                                      \
    "2:\n\t"                                                              \
    REP("%d0")                                                            \
    EDGE("%d0", "(%a3)", "%d6")                                           \
    P23(EDGE("%d0", "4(%a3)", "%d7"))                                     \
    "addq.l #8,%a3\n\t"                                                   \
    "lsr.w  #3,%d3\n\t"                                                   \
    "subq.w #2,%d3\n\t"                                                   \
    "bmi.s  4f\n"                                                         \
    "3:\n\t"                                                              \
    MID                                                                   \
    "dbf    %d3,3b\n"                                                     \
    "4:\n\t"                                                              \
    REP("%d1")                                                            \
    EDGE("%d1", "(%a3)", "%d6")                                           \
    P23(EDGE("%d1", "4(%a3)", "%d7"))                                     \
    "9:\n\t"                                                              \
    "add.l  %a4,%d4\n\t"                                                  \
    "add.l  %a5,%d5\n\t"                                                  \
    "adda.l %a0,%a2\n\t"                                                  \
    "cmpa.l %a6,%a2\n\t"                                                  \
    "bne    1b\n\t"                                                       \
    "rts\n");
PROWS_ASM(stdl_prows_set2, TS_EDGE,
          "move.l %d6,(%a3)\n\t" "addq.l #8,%a3\n\t", NO23, T_REP2, ".s")
PROWS_ASM(stdl_prows_set4, TS_EDGE,
          "move.l %d6,(%a3)+\n\t" "move.l %d7,(%a3)+\n\t", WITH23, T_REP2,
          "")
PROWS_ASM(stdl_prows_xor1, T1_EDGE,
          "not.w  (%a3)\n\t" "addq.l #8,%a3\n\t", NO23, T_REP1, ".s")
PROWS_ASM(stdl_prows_xor2, TX_EDGE,
          "eor.l %d6,(%a3)\n\t" "addq.l #8,%a3\n\t", NO23, T_REP2, ".s")
PROWS_ASM(stdl_prows_xor4, TX_EDGE,
          "eor.l %d6,(%a3)+\n\t" "eor.l %d7,(%a3)+\n\t", WITH23, T_REP2,
          ".s")

/*
 * An edge from the vertex at a3 to the one at a6 (d1 its row), for
 * row d3: the step into SR, the start into X with the rounding added
 * by BIAS. d0-d2 scratch. The step is floor(65536 * dx / dy): divs.w
 * gives the quotient and remainder, a negative remainder is moved up
 * by dy and the quotient down by one, and divu.w of the remainder in
 * the high word gives the fraction. Below a clipped top, t rows on,
 * the start moves by t times the step in a muls.w and a mulu.w.
 */
#define W_EDGE(X, SR, BIAS)                                               \
    "sub.w  2(%a3),%d1\n\t"             /* dy */                          \
    "move.w (%a6),%d0\n\t"                                                \
    "sub.w  (%a3),%d0\n\t"              /* dx */                          \
    "ext.l  %d0\n\t"                                                      \
    "divs.w %d1,%d0\n\t"                /* remainder:quotient */          \
    "move.l %d0,%d2\n\t"                                                  \
    "clr.w  %d2\n\t"                    /* remainder:0 */                 \
    "tst.l  %d2\n\t"                                                      \
    "bpl.s  5f\n\t"                                                       \
    "subq.w #1,%d0\n\t"                                                   \
    "swap   %d2\n\t"                                                      \
    "add.w  %d1,%d2\n\t"                                                  \
    "swap   %d2\n"                                                        \
    "5:\n\t"                                                              \
    "divu.w %d1,%d2\n\t"                /* the fraction */                \
    "swap   %d0\n\t"                                                      \
    "move.w %d2,%d0\n\t"                /* the step */                    \
    "move.l %d0," SR "\n\t"                                               \
    "move.w (%a3)," X "\n\t"                                              \
    "swap   " X "\n\t"                                                    \
    "clr.w  " X "\n\t"                                                    \
    BIAS                                                                  \
    "move.w %d3,%d1\n\t"                                                  \
    "sub.w  2(%a3),%d1\n\t"             /* rows below the vertex */       \
    "beq.s  6f\n\t"                                                       \
    "move.l %d0,%d2\n\t"                                                  \
    "swap   %d2\n\t"                                                      \
    "muls.w %d1,%d2\n\t"                                                  \
    "swap   %d2\n\t"                                                      \
    "clr.w  %d2\n\t"                                                      \
    "add.l  %d2," X "\n\t"                                                \
    "mulu.w %d1,%d0\n\t"                                                  \
    "add.l  %d0," X "\n"                                                  \
    "6:\n\t"
/*
 * One side: while its edge ends at or above row d3, move to the next
 * vertex (ADV adds or subtracts the step, wrapping round), stopping
 * at the bottom vertex; an edge that reaches below the row is set up.
 * IDX and END are the side's frame slots.
 */
#define W_SIDE(IDX, END, ADV, X, SR, BIAS)                                \
    "1:\n\t"                                                              \
    "cmp.w  " END "(%sp),%d3\n\t"                                         \
    "blt    8f\n\t"                     /* its edge reaches below */      \
    "move.l (%sp),%a3\n\t"              /* the vertices */                \
    "move.w " IDX "(%sp),%d0\n\t"                                         \
    "move.w 2(%a3,%d0.w),%d1\n\t"                                         \
    "cmp.w  24(%sp),%d1\n\t"                                              \
    "bge    99f\n\t"                    /* at the bottom vertex */        \
    "move.w %d0,%d2\n\t"                                                  \
    ADV " 10(%sp),%d2\n\t"                                                \
    "bpl.s  2f\n\t"                                                       \
    "add.w  8(%sp),%d2\n\t"             /* below the first: the last */   \
    "bra.s  3f\n"                                                         \
    "2:\n\t"                                                              \
    "cmp.w  8(%sp),%d2\n\t"                                               \
    "bne.s  3f\n\t"                                                       \
    "moveq  #0,%d2\n"                   /* past the last: the first */    \
    "3:\n\t"                                                              \
    "move.w %d2," IDX "(%sp)\n\t"                                         \
    "lea    0(%a3,%d2.w),%a6\n\t"                                         \
    "lea    0(%a3,%d0.w),%a3\n\t"                                         \
    "move.w 2(%a6),%d1\n\t"                                               \
    "move.w %d1," END "(%sp)\n\t"                                         \
    "cmp.w  %d3,%d1\n\t"                                                  \
    "ble.s  1b\n\t"                     /* flat, or above the row */      \
    W_EDGE(X, SR, BIAS)                                                   \
    "8:\n\t"
/*
 * stdl_poly_fast(dst, pts, n, rows, off, l01, l23, masks): the whole
 * of a SET or XOR onto a surface with no mask, when the polygon is
 * inside the clip's columns and no taller than 256 rows - which is
 * every polygon of a demo's object or a game's sprite. The vertex
 * scan, the clip, the test for this path and the winding are the
 * C's (STDL_FillPolygon), in 68000 because in C they were most of a
 * small polygon's cost: a 2-row triangle 0.83ms -> 0.69, a 22-row
 * one in one plane 1.47 -> 1.33. Returns 0 when it has drawn (or found
 * nothing to draw), 1 to send the polygon the long way: outside the
 * columns, too tall, or a top vertex its neighbours run straight
 * through, where only the whole area tells the winding.
 *
 * The frame: 0 vertices, 4 row loop, 8 n x 4, 10 step x 4, 12/14 the
 * left's and right's vertex, 16/18 the rows their edges end on, 20
 * the row, 22 the last row (exclusive), 24 the bottom vertex's row.
 * The arguments are 74 bytes up.
 */
__asm__(
    "\t.text\n\t.even\n\t.globl " T_SYM(stdl_poly_fast) "\n"
    T_SYM(stdl_poly_fast) ":\n\t"
    "movem.l %d2-%d7/%a2-%a6,-(%sp)\n\t"
    "lea    -26(%sp),%sp\n\t"
    "move.l 78(%sp),%a3\n\t"            /* vertices */
    "move.l 82(%sp),%d0\n\t"            /* n */
    "move.l (%a3),%d1\n\t"              /* x:y */
    "move.w %d1,%d2\n\t"                /* top row */
    "move.w %d1,%d3\n\t"                /* bottom row */
    "swap   %d1\n\t"
    "move.w %d1,%d4\n\t"                /* least x */
    "move.w %d1,%d5\n\t"                /* greatest x */
    "moveq  #0,%d6\n\t"                 /* top vertex x 4 */
    "moveq  #4,%d7\n\t"
    "lea    4(%a3),%a0\n\t"
    "subq.w #2,%d0\n"
    "1:\n\t"
    "move.l (%a0)+,%d1\n\t"
    "cmp.w  %d2,%d1\n\t"
    "bge.s  2f\n\t"
    "move.w %d1,%d2\n\t"
    "move.w %d7,%d6\n"
    "2:\n\t"
    "cmp.w  %d3,%d1\n\t"
    "ble.s  3f\n\t"
    "move.w %d1,%d3\n"
    "3:\n\t"
    "swap   %d1\n\t"
    "cmp.w  %d4,%d1\n\t"
    "bge.s  4f\n\t"
    "move.w %d1,%d4\n"
    "4:\n\t"
    "cmp.w  %d5,%d1\n\t"
    "ble.s  5f\n\t"
    "move.w %d1,%d5\n"
    "5:\n\t"
    "addq.w #4,%d7\n\t"
    "dbf    %d0,1b\n\t"
    "move.l 74(%sp),%a1\n\t"            /* the surface */
    "move.w 16(%a1),%d0\n\t"            /* clip x */
    "cmp.w  %d0,%d4\n\t"
    "blt    98f\n\t"                    /* left of the clip */
    "add.w  20(%a1),%d0\n\t"
    "cmp.w  %d0,%d5\n\t"
    "bgt    98f\n\t"                    /* right of it */
    "move.w %d3,%d0\n\t"
    "sub.w  %d2,%d0\n\t"
    "cmp.w  #256,%d0\n\t"
    "bhi    98f\n\t"                    /* taller than 256 rows */
    "move.w %d3,24(%sp)\n\t"            /* bottom */
    "move.w 18(%a1),%d0\n\t"            /* clip y */
    "move.w %d2,%d1\n\t"
    "cmp.w  %d0,%d1\n\t"
    "bge.s  6f\n\t"
    "move.w %d0,%d1\n"                  /* the first row */
    "6:\n\t"
    "add.w  22(%a1),%d0\n\t"
    "cmp.w  %d3,%d0\n\t"
    "ble.s  7f\n\t"
    "move.w %d3,%d0\n"                  /* the last row, exclusive */
    "7:\n\t"
    "cmp.w  %d0,%d1\n\t"
    "bge    99f\n\t"                    /* no row inside the clip */
    "move.w %d1,20(%sp)\n\t"
    "move.w %d0,22(%sp)\n\t"
    /* the winding, from the top vertex's two edges: ux * vy against
     * uy * vx, as orientation() */
    "move.w %d7,8(%sp)\n\t"             /* n x 4 */
    "move.w %d6,%d0\n\t"
    "addq.w #4,%d0\n\t"
    "cmp.w  %d7,%d0\n\t"
    "bne.s  1f\n\t"
    "moveq  #0,%d0\n"
    "1:\n\t"                            /* the next vertex */
    "move.w %d6,%d1\n\t"
    "bne.s  2f\n\t"
    "move.w %d7,%d1\n"
    "2:\n\t"
    "subq.w #4,%d1\n\t"                 /* the one before */
    "move.l 0(%a3,%d6.w),%d2\n\t"       /* top x:y */
    "move.l 0(%a3,%d0.w),%d3\n\t"
    "move.l 0(%a3,%d1.w),%d4\n\t"
    "sub.w  %d2,%d3\n\t"                /* uy */
    "sub.w  %d2,%d4\n\t"                /* vy */
    "swap   %d2\n\t"
    "swap   %d3\n\t"
    "swap   %d4\n\t"
    "sub.w  %d2,%d3\n\t"                /* ux */
    "sub.w  %d2,%d4\n\t"                /* vx */
    "move.w %d3,%d0\n\t"
    "swap   %d4\n\t"
    "muls.w %d4,%d0\n\t"                /* ux * vy */
    "swap   %d3\n\t"
    "swap   %d4\n\t"
    "muls.w %d4,%d3\n\t"                /* uy * vx */
    "moveq  #4,%d1\n\t"
    "cmp.l  %d3,%d0\n\t"
    "blt.s  3f\n\t"
    "beq    98f\n\t"                    /* straight through: the area */
    "moveq  #-4,%d1\n"
    "3:\n\t"
    "move.w %d1,10(%sp)\n\t"            /* step x 4 */
    "move.w %d6,12(%sp)\n\t"
    "move.w %d6,14(%sp)\n\t"
    "move.w 2(%a3,%d6.w),%d1\n\t"
    "move.w %d1,16(%sp)\n\t"            /* both edges end at the top */
    "move.w %d1,18(%sp)\n\t"
    "move.l %a3,(%sp)\n\t"
    "move.l 86(%sp),4(%sp)\n\t"         /* row loop */
    "move.w 20(%sp),%d0\n\t"
    "mulu.w 8(%a1),%d0\n\t"             /* the first row's offset */
    "move.l (%a1),%a2\n\t"
    "add.l  %d0,%a2\n\t"
    "add.l  90(%sp),%a2\n\t"            /* and the plane's */
    "move.l 94(%sp),%d6\n\t"
    "move.l 98(%sp),%d7\n\t"
    "moveq  #0,%d0\n\t"
    "move.w 8(%a1),%d0\n\t"
    "move.l %d0,%a0\n\t"                /* stride */
    "move.l 102(%sp),%a1\n"             /* edge masks */
    "0:\n\t"
    "move.w 20(%sp),%d3\n\t"
    W_SIDE("12", "16", "add.w", "%d4", "%a4", "add.l #0xFFFF,%d4\n\t")
    W_SIDE("14", "18", "sub.w", "%d5", "%a5", "subq.l #1,%d5\n\t")
    "move.w 16(%sp),%d0\n\t"            /* the first edge to end */
    "cmp.w  18(%sp),%d0\n\t"
    "ble.s  1f\n\t"
    "move.w 18(%sp),%d0\n"
    "1:\n\t"
    "cmp.w  22(%sp),%d0\n\t"
    "ble.s  2f\n\t"
    "move.w 22(%sp),%d0\n"              /* or the clip */
    "2:\n\t"
    "move.w %d0,20(%sp)\n\t"
    "sub.w  %d3,%d0\n\t"                /* rows */
    "move.w %a0,%d1\n\t"
    "mulu.w %d1,%d0\n\t"
    "lea    0(%a2,%d0.l),%a6\n\t"       /* the row to stop at */
    "move.l 4(%sp),%a3\n\t"
    "jsr    (%a3)\n\t"
    "move.w 20(%sp),%d0\n\t"
    "cmp.w  22(%sp),%d0\n\t"
    "blt    0b\n"
    "99:\n\t"
    "moveq  #0,%d0\n\t"
    "lea    26(%sp),%sp\n\t"
    "movem.l (%sp)+,%d2-%d7/%a2-%a6\n\t"
    "rts\n"
    "98:\n\t"
    "moveq  #1,%d0\n\t"
    "lea    26(%sp),%sp\n\t"
    "movem.l (%sp)+,%d2-%d7/%a2-%a6\n\t"
    "rts\n");
#endif

/* The C walk, the 68000's twin. On the machine OR and CLEAR, which the
 * 68000 walk leaves out, keep the column tables: rare enough not to
 * be worth this code in every program that fills a polygon. */
#ifndef __m68k__
/* an edge's step a row in 16.16, dx across and dy (1 to 256) down:
 * floor(65536 * dx / dy), from a divs.w and a divu.w of its
 * remainder */
static int32_t slope16(int dx, int dy)
{
    int q = dx / dy, r = dx % dy;

    if (r < 0) {            /* floor division: 0 <= r < dy */
        r += dy;
        q--;
    }
    return (int32_t)(((uint32_t)q << 16)
                     | STDL_DivU16((uint32_t)r << 16, (unsigned)dy));
}

/* where an edge from a with step s is at row y (a->y <= y, less than
 * 256 rows on), in 16.16 with the rounding added: 0xFFFF makes the
 * left's whole part its first column, -1 the right's its last. Only a
 * clipped top starts below a, and then the product is two 16-bit
 * multiplies. */
static int32_t edge_x(const STDL_Point *a, int32_t s, int y, int32_t bias)
{
    int32_t x = (int32_t)((uint32_t)(int32_t)a->x << 16) + bias;
    const int t = y - a->y;

    if (t > 0) {
        x += (int32_t)((uint32_t)STDL_MulS16(t, (int16_t)(s >> 16)) << 16)
             + (int32_t)STDL_MulU16((unsigned)t, (uint16_t)s);
    }
    return x;
}

/* Rows [y, y2) a trapezoid at a time: walk both sides down from the
 * top vertex as edge_next does - skipping edges that are flat or end
 * above the row, and stopping at the bottom vertex - and hand the
 * rows between to fill. */
static __attribute__((noinline)) void fill_traps(
        Trap *t, trap_fn fill, const STDL_Point *pts, int n, int top,
        int dir, int y, int y2, int bottom)
{
    int il = top, ir = top, lend = pts[top].y, rend = lend;

    for (;;) {
        int stop;

        while (lend <= y) {
            int b = il + dir;

            if (b == n) b = 0;
            if (b < 0) b = n - 1;
            if (pts[il].y >= bottom) {
                return;
            }
            if (pts[b].y > y) {
                t->sl = slope16(pts[b].x - pts[il].x, pts[b].y - pts[il].y);
                t->xl = edge_x(&pts[il], t->sl, y, 0xFFFF);
            }
            lend = pts[b].y;
            il = b;
        }
        while (rend <= y) {
            int b = ir - dir;

            if (b == n) b = 0;
            if (b < 0) b = n - 1;
            if (pts[ir].y >= bottom) {
                return;
            }
            if (pts[b].y > y) {
                t->sr = slope16(pts[b].x - pts[ir].x, pts[b].y - pts[ir].y);
                t->xr = edge_x(&pts[ir], t->sr, y, -1);
            }
            rend = pts[b].y;
            ir = b;
        }
        stop = lend < rend ? lend : rend;
        if (stop > y2) {
            stop = y2;
        }
        t->end = t->row + stdl_row_off(stop - y, (uint16_t)t->stride);
        fill(t);
        y = stop;
        if (y >= y2) {
            return;
        }
    }
}
#endif

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
static __attribute__((noinline)) int area_sign(const STDL_Point *pts,
                                               int n)
{
    long long area = 0;
    int i;

    for (i = 0; i < n; i++) {
        const STDL_Point *p = &pts[i], *q = &pts[i + 1 < n ? i + 1 : 0];

        area += (long long)p->x * q->y - (long long)q->x * p->y;
    }
    return area < 0 ? 1 : area > 0 ? -1 : 0;
}

static int orientation(const STDL_Point *pts, int n, int top)
{
    const STDL_Point *t = &pts[top];
    const STDL_Point *nx = &pts[top + 1 < n ? top + 1 : 0];
    const STDL_Point *pv = &pts[top > 0 ? top - 1 : n - 1];
    const int ux = nx->x - t->x, uy = nx->y - t->y;
    const int vx = pv->x - t->x, vy = pv->y - t->y;

    if (ux >= -32768 && ux <= 32767 && vx >= -32768 && vx <= 32767
        && uy <= 32767 && vy <= 32767) {
        const int32_t a = STDL_MulS16(ux, vy), b = STDL_MulS16(uy, vx);

        if (a != b) {
            return a < b ? 1 : -1;
        }
    }
    return area_sign(pts, n);
}
/* the row loops by operation and planes written, for each path */
enum { K_SET2, K_SET4, K_XOR1, K_XOR2, K_XOR4, K_ANDOR2, K_ANDOR4 };
static const rows_fn rows_by_kind[7] = {
    rows_set2, rows_set4, rows_xor1, rows_xor2, rows_xor4, rows_andor2,
    rows_andor4
};
#ifdef __m68k__
static void (*const prows_by_kind[5])(void) = {
    stdl_prows_set2, stdl_prows_set4, stdl_prows_xor1, stdl_prows_xor2,
    stdl_prows_xor4
};
#else
static const trap_fn trap_by_kind[7] = {
    stdl_trap_set2, stdl_trap_set4, stdl_trap_xor1, stdl_trap_xor2,
    stdl_trap_xor4, trap_andor2, trap_andor4
};
#endif

/* a plane pair's words for the colour's two bits */
static const uint32_t pair_words[4] = {
    0, STDL_PACK2(0xFFFFu, 0), STDL_PACK2(0, 0xFFFFu),
    STDL_PACK2(0xFFFFu, 0xFFFFu)
};

static void fill_tables(STDL_Surface *dst, const Trap *t, rows_fn fill,
                        int off, const STDL_Point *pts, int n, int top,
                        int dir, int y, int y2, int bottom, int lo, int hi,
                        int free_, int transparent);

void STDL_FillPolygon(STDL_Surface *dst, const STDL_Point *pts, int n,
                      uint8_t col, int op)
{
    int i, top = 0, bottom, y, y2, lo, hi, dir, transparent, free_;
    int minx, maxx, off = 0, kind;
    uint32_t l01, l23, k01 = 0, k23 = 0;
    Trap t;

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

    l01 = pair_words[col & 3];
    l23 = pair_words[(col >> 2) & 3];
    /* SET writes pair 2-3 whenever the budget has it, to clear it.
     * The others write only the planes the colour has: a colour with
     * nothing in planes 0-1 is a pair from the third plane's word, and
     * an XOR into one plane - each face of a glenz object - touches
     * that plane's word alone */
    if (op == STDL_DRAW_SET) {
        kind = stdl_planes > 2 ? K_SET4 : K_SET2;
    } else {
        if ((col & 3) == 0) {
            off = 4;
            l01 = l23;
            l23 = 0;
        }
        if (op == STDL_DRAW_XOR) {
            if ((col & (col - 1)) == 0) {
                off += (col & 0xA) ? 2 : 0;
                kind = K_XOR1;
            } else {
                kind = l23 != 0 ? K_XOR4 : K_XOR2;
            }
        } else {
            if (op == STDL_DRAW_CLEAR) {
                k01 = l01;
                k23 = l23;
                l01 = l23 = 0;
            }
            kind = (k23 | l23) != 0 ? K_ANDOR4 : K_ANDOR2;
        }
    }
#ifdef __m68k__
    /* the 16.16 path, start to finish in 68000; it hands back what it
     * does not take */
    if (kind < K_ANDOR2 && dst->mask == NULL && n <= 8191
        && stdl_poly_fast(dst, pts, n, prows_by_kind[kind], off, l01, l23,
                          edge_masks) == 0) {
        return;
    }
#endif

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

    t.stride = dst->stride;
    t.l01 = l01;
    t.l23 = l23;
    t.k01 = k01;
    t.k23 = k23;
#ifndef __m68k__
    if (dst->mask == NULL && free_ && bottom - pts[top].y <= 256) {
        t.row = dst->pixels + stdl_row_off(y, dst->stride) + off;
        t.masks = edge_masks;
        fill_traps(&t, trap_by_kind[kind], pts, n, top, dir, y, y2,
                   bottom);
        return;
    }
#endif
    fill_tables(dst, &t, rows_by_kind[kind], off, pts, n, top, dir, y, y2,
                bottom, lo, hi, free_, transparent);
}

/* the long way, a chunk of rows at a time through the column tables:
 * a polygon cut by the clip's columns, taller than 256 rows, or on a
 * surface with a mask to keep. Out of line, so the fast path's
 * registers are not spent on it. */
static __attribute__((noinline)) void fill_tables(
        STDL_Surface *dst, const Trap *t, rows_fn fill, int off,
        const STDL_Point *pts, int n, int top, int dir, int y, int y2,
        int bottom, int lo, int hi, int free_, int transparent)
{
    Edge el, er;
    int i;

    el.v = er.v = top;
    el.yend = er.yend = pts[top].y;
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
        fill(row + off, dst->stride, tab_l, tab_r, rows, t->l01, t->l23,
             t->k01, t->k23);
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
