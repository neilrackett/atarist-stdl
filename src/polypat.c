/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * Convex polygons with 12.4 sub-pixel vertices, filled with a 4x4
 * two-colour pattern (PICO-8's fillp): ordered-dither shading, fog,
 * screen-door transparency.
 *
 * The coverage rule is STDL_FillPolygon's, evaluated exactly on the
 * sub-pixel vertices. A pixel (x, y) samples the point (16x, 16y) in
 * vertex units, and is drawn when that point is in the polygon with
 * the top and left edges inside and the bottom and right outside.
 * Rows run from ceil(top / 16) to ceil(bottom / 16) less one; at row
 * y an edge from a to b (a above) is at x = N / D pixels, with
 *
 *   N = a.x * dy + (16y - a.y) * dx,   D = 16 * dy,
 *
 * the left side's first column is ceil(N / D) and the right side's
 * end (exclusive) is ceil(N / D) too. Each edge is stepped down the
 * rows as that column and a remainder: N grows by 16 dx a row,
 * which is D q + 16 r with q and r the floor quotient and remainder
 * of dx / dy - so no rounding drifts down an edge, and integer
 * vertices shifted left by four come out as STDL_FillPolygon's
 * pixels exactly.
 *
 * Setting an edge up costs no divide beyond the slope's. With
 * t = 16y - a.y rows below its vertex, N = dy * B + t * r for
 * B = a.x + t * q, and writing B as 16 bh + bl leaves
 * N = D bh + (bl * dy + t * r): when t < 16, which is every edge but
 * one that a clipped top cuts, that bracket is under 2D, so the
 * column is bh plus 0, 1 or 2 and the remainder falls out of the
 * same compare. A clipped top takes one divu.w more. Everything is
 * built from 16-bit multiplies and divides, so nothing here calls
 * libgcc, whatever the coordinates.
 *
 * Measured on a plain ST (PRIMCOST, Hatari --machine st; 12.4
 * vertices with fractions, a dither between colours 9 and 4, all
 * four planes): the C reference, the first 68000 version, and this
 * one, with its own triangle path:
 *
 *                        C      68000   now    transp.  FillPolygon SET
 *   2-row triangle     1.80ms   0.79   0.645            0.70ms
 *   8-row triangle     2.97     1.15   1.01             1.00
 *   22-row triangle    5.90     2.09   1.91    2.40     1.72
 *   20-row quad        6.84     2.36   2.29             2.00
 *   160-row diamond   53.8     14.9   14.5    21.9     12.7
 *   22 rows, clipped   5.98     2.24   2.07
 *
 * On a Mega STE at 16MHz (--machine megaste) the same five take 0.37,
 * 0.55, 1.03, 1.29 and 8.5ms (FillPolygon SET 0.38, 0.54, 0.91, 1.10,
 * 7.6); the first 68000 version took 0.42, 0.61, 1.10, 1.33 and 8.6.
 * A quad 800 rows tall cut to 200 by the clip takes 25.4ms (16.7 on
 * the Mega STE): 108.4 (58.4) while edges over 255 rows went to the C.
 *
 * In cycles at 8MHz, from a probe timing the call alone: a 2-row
 * triangle about 5050 (6165 in the first 68000 version), the same
 * with its vertices on whole rows 4450 (5890), with a vertical edge
 * 4840 (6200); a 2-row quad about 5440 (5620). Taken apart on the
 * triangle by stopping it at each stage: about 1800 before the second
 * edge (the call, the checks, the register save, the sort, the clip,
 * the side and the first edge), each edge's set-up about 680 - 290
 * of it a divs.w, a muls.w and two mulu.w, the rest some fifty other
 * instructions - and a one-row trapezoid about 500-740. A call that
 * clips out by rows is about 950; a generic polygon's walk spends
 * about 2290 before its first edge (a quad).
 *
 * What did and did not pay, measured or counted:
 *  - The triangle path (sorted vertices, one cross product for the
 *    side, the long edge kept across both trapezoids, no walk): the
 *    2-row triangle 6165 -> 5230 cycles.
 *  - Entering in 68000 rather than through a C wrapper, reading each
 *    vertex once: the first version's 0.88ms -> 0.79 for the 2-row
 *    triangle.
 *  - Edge shortcuts: a vertical edge skips the divide and both
 *    multiplies, a vertex on a sample row (t = 0) both multiplies.
 *  - The start column from k0 = -B & 15 (one small mulu and one
 *    borrow) instead of a compare against D and 2D: about 20 cycles an
 *    edge. Instruction counting promised three times that.
 *  - Long edge masks with the word in both halves (no swap to
 *    replicate them), and edge groups written with (a3)+: about 20
 *    cycles a narrow row.
 *  - Clipping rows first and the clamp's columns only when clamping:
 *    a polygon outside the rows 1418 -> 945 cycles, others 80-100.
 *  - Each edge's remainder kept over 16, against r and dy (pedge_start's
 *    comment): edges to 32767 sub-pixel rows where they were 4095,
 *    for about ten cycles of the set-up (PRIMCOST's other cases within
 *    0.5%, its floor 0.25%). A projected road's nearest face, its ends
 *    hundreds of rows beyond the view, had gone to walk_wide's C rows:
 *    in one port's race that was 0.2-0.7 faces a frame at some 14ms
 *    each on a Mega STE. PRIMCOST's 800-row quad 108.4 -> 25.4ms.
 *  - Not done: a reciprocal table for the slopes. q and r exactly from
 *    a table of 65536 / dy (8KB for the 4095 sub-pixel rows the fast
 *    path then took, 64KB for its 32767 now) is a lookup, two mulu.w
 *    and a correction - about 160 cycles counted, against the divs.w's
 *    140-158 and its floor fix.
 *  - Not done: a quad path like the triangle's (sorting four vertices,
 *    the two chains' cases); the generic walk's own overheads were
 *    trimmed instead, about 100 cycles a quad.
 *  - Built, measured and not kept (2026-10-25): a batch entry point,
 *    STDL_FillPolygonsPattern(dst, list, count). The count above, 500
 *    cycles a polygon, was what a call pays; a list keeps most of it.
 *    It walked the list in 68000 and entered the walker in place of a
 *    call (the walker's exit restoring the list's place from where a
 *    call keeps its registers), so it saved the argument push, the
 *    surface's checks and the register save, and still paid each
 *    polygon's own checks and the exit's restore. PRIMCOST, eight
 *    small faces (2 to 22 rows): 11.41 -> 11.33ms on a plain ST, 6.23
 *    -> 6.21 on a Mega STE - under 1%, not worth an entry point.
 *  - Not done: an edge cache. Faces of a mesh share edges, each set up
 *    twice at about 680 cycles; but in one port's race only 24-30% of
 *    the edges a frame sets up repeat one (counted on the host: a
 *    direct-mapped table of 64-512 entries keyed by both ends, four
 *    hashes), and a lookup that misses costs about what one that hits
 *    saves: some 160 cycles counted against 450 saved.
 *
 * Getting a small face much further needs work shared between faces
 * rather than taken out of each: an edge between two faces of a mesh
 * is set up twice, once by each, at about 680 cycles a time - and as
 * the cache above found, a real scene shares fewer than the mesh
 * suggests.
 *
 * The rows. A probe timing parallelograms of 50 and 100 rows, every
 * row the same width, gives the cost of a row on a plain ST: before
 * the opaque loops were unrolled by the pattern's row, 414 cycles for
 * one group, 523 for two, 675 for six; since, 390, 501 and 643 - the
 * 20 cycles of a pointer step and wrap test gone, at the price of
 * about 40 a trapezoid for the jump in and 1.9KB of code. PRIMCOST:
 * 22 rows 1.91 -> 1.87ms, the 160-row diamond 14.5 -> 14.0, the 2-row
 * triangle 0.645 -> 0.649.
 *
 * What a narrow row is made of, counted for one group (68000 cycles):
 * the plane words 32, the columns and the empty test 26, splitting
 * them into group and bit 24, the group's address 20, the two edge
 * masks 52, the group count 20, the read-modify-write of the group's
 * two longs 104, the two edges' steps about 52, and the row's end 34.
 * A narrow row near 200 cycles is not in this design: two groups are
 * 192 cycles of read-modify-write alone, and the bookkeeping has no
 * register left to move into - a0-a6 and d0-d7 are all taken, which
 * is what kept the fixes, the row count and the clamp's columns in
 * memory. Evaluated and not done, by count:
 *  - a pattern ring (a next pointer per entry) built per polygon:
 *    16 cycles a row saved against about 230 to build, so a loss
 *    below 15 rows;
 *  - a single-group path tested first: about 12 saved on a one-group
 *    row, 22 lost on every other;
 *  - the masks PC-relative, to free a1 for the row count: the table
 *    is 128 bytes and an 8-bit displacement cannot reach both halves
 *    from the loop;
 *  - edges as 16.16 plus a fraction: the exact step is already one
 *    add.l and a carry, with the fix only on a borrow;
 *  - the middle groups unrolled by two: about 10 saved a pair against
 *    20 added to every row for the odd one, so a loss on narrow rows.
 *
 * tests/hatari/ppchk.c runs the host suite on the machine: 27s for
 * the sampled build on a plain ST (15s on a Mega STE), and 148s for
 * -DPPCHK_FULL (13s of host time under fast-forward) - the checking,
 * not the fill: with the fill in C the times were within a few
 * percent.
 *
 * Its own translation unit: a program that never fills a pattern
 * polygon does not carry it.
 */

#include "stdl_internal.h"

/* 0xFFFF >> n, then 0xFFFF << (15 - n); the 68000 rows read it too */
const uint16_t stdl_pp_masks[32] = {
    0xFFFF, 0x7FFF, 0x3FFF, 0x1FFF, 0x0FFF, 0x07FF, 0x03FF, 0x01FF,
    0x00FF, 0x007F, 0x003F, 0x001F, 0x000F, 0x0007, 0x0003, 0x0001,
    0x8000, 0xC000, 0xE000, 0xF000, 0xF800, 0xFC00, 0xFE00, 0xFF00,
    0xFF80, 0xFFC0, 0xFFE0, 0xFFF0, 0xFFF8, 0xFFFC, 0xFFFE, 0xFFFF
};
#ifdef __m68k__
/* the same as longs, the word in both halves - a plane pair's mask -
 * for the 68000 rows */
const uint32_t stdl_pp_lmasks[32] = {
    0xFFFFFFFFu, 0x7FFF7FFFu, 0x3FFF3FFFu, 0x1FFF1FFFu,
    0x0FFF0FFFu, 0x07FF07FFu, 0x03FF03FFu, 0x01FF01FFu,
    0x00FF00FFu, 0x007F007Fu, 0x003F003Fu, 0x001F001Fu,
    0x000F000Fu, 0x00070007u, 0x00030003u, 0x00010001u,
    0x80008000u, 0xC000C000u, 0xE000E000u, 0xF000F000u,
    0xF800F800u, 0xFC00FC00u, 0xFE00FE00u, 0xFF00FF00u,
    0xFF80FF80u, 0xFFC0FFC0u, 0xFFE0FFE0u, 0xFFF0FFF0u,
    0xFFF8FFF8u, 0xFFFCFFFCu, 0xFFFEFFFEu, 0xFFFFFFFFu
};
#endif
#define lmask stdl_pp_masks
#define rmask (stdl_pp_masks + 16)

void STDL_MakePattern(STDL_Pattern *p, uint8_t col0, uint8_t col1,
                      uint16_t bits, int transparent)
{
    int r, k;

    if (p == NULL) {
        return;
    }
    for (r = 0; r < 4; r++) {
        /* the row's four pixels, repeated across the group: pixel x
         * is bit 15 - (x & 15), and x & 3 picks the nibble's bit */
        const unsigned nib = (bits >> (12 - 4 * r)) & 0xFu;
        const uint16_t w = (uint16_t)(nib | nib << 4 | nib << 8
                                      | nib << 12);

        for (k = 0; k < 4; k++) {
            uint16_t v = 0;

            if ((col0 >> k) & 1) {
                v |= (uint16_t)~w;
            }
            if (!transparent && ((col1 >> k) & 1)) {
                v |= w;
            }
            p->planes[r][k] = v;
        }
        p->mask[r] = transparent ? w : 0;
    }
}

/* the first pixel row (or column) whose sample point is at or past
 * the 12.4 coordinate v: ceil(v / 16) */
#define CEIL16(v) (((int)(v) + 15) >> 4)

/*
 * An edge from a to b (b below a), set up at pixel row y, a's row or
 * below: its first column and the remainder below it, and the step
 * a row. Exact for any int16_t coordinates, from 16-bit multiplies
 * and divides only (dx, dy and t all fit sixteen bits unsigned).
 */
typedef struct {
    int32_t c;          /* column: ceil(N / D) */
    uint32_t e;         /* D * c - N, in [0, D) */
    int32_t q;          /* whole columns a row */
    uint32_t r16;       /* 16 r: remainder a row, in [0, D) */
    uint32_t d;         /* D = 16 dy */
} EStart;

static void edge_setup(EStart *s, const STDL_PointSub *a,
                       const STDL_PointSub *b, int y)
{
    const int32_t dx = (int32_t)b->x - a->x;
    const unsigned dy = (unsigned)((int32_t)b->y - a->y);   /* 1..65535 */
    const unsigned t = (unsigned)((int32_t)y * 16 - a->y);  /* < dy */
    const unsigned ux = (unsigned)(dx < 0 ? -dx : dx);
    unsigned q0, r0, r, tq, k0;
    int32_t q, bb;
    uint32_t tr, sm;

    /* floor(dx / dy) and dx - q dy in [0, dy), on the magnitude */
    q0 = STDL_DivU16(ux, dy);
    r0 = ux - STDL_MulU16(q0, dy);
    if (dx >= 0) {
        q = (int32_t)q0;
        r = r0;
    } else if (r0 != 0) {
        q = -(int32_t)q0 - 1;
        r = dy - r0;
    } else {
        q = -(int32_t)q0;
        r = 0;
    }
    /* B = a.x + t q: |t q| < |dx| + t, so it fits 32 bits */
    tq = (unsigned)(q < 0 ? -q : q);
    bb = (int32_t)STDL_MulU16(t, tq);
    bb = a->x + (q < 0 ? -bb : bb);
    tr = STDL_MulU16(t, r);
    if (t >= 16) {
        /* a clipped top: fold whole columns of t r / dy into B (the
         * quotient is under t, so it fits) */
        const unsigned k = STDL_DivU16(tr, dy);

        bb += (int32_t)k;
        tr -= STDL_MulU16(k, dy);
    }
    /* the edge is at x = B + t r / dy sub-pixels, with t r < 16 dy.
     * With k0 = -B & 15, B + k0 is the multiple of 16 at or right of
     * B: the column is (B + k0) / 16 if x has not passed it (k0 dy >=
     * t r) and one more if it has, and the remainder D c - N is
     * k0 dy - t r, plus D in the second case. One small multiply and
     * one compare, as the 68000 does it */
    k0 = (unsigned)(-bb) & 15u;
    sm = STDL_MulU16(k0, dy);
    s->d = (uint32_t)dy << 4;
    s->c = (bb + (int32_t)k0) >> 4;
    if (sm >= tr) {
        s->e = sm - tr;
    } else {
        s->c += 1;
        s->e = sm + s->d - tr;
    }
    s->q = q;
    s->r16 = (uint32_t)r << 4;
}

/* a pattern row as the fill writes it: the plane words with the
 * budget's planes cleared, and what the row keeps */
typedef struct {
    uint16_t w[4];
    uint16_t keep;
} PRow;

/*
 * Span [x1, x2] (inclusive, clipped) of a row: the edge groups under
 * their masks, the middle ones stored outright when the row is
 * opaque. np planes, 2 or 4; mw, when the surface has a mask, gets
 * the drawn pixels marked opaque. The C twin of the 68000 rows.
 */
static void span_c(uint8_t *row, int x1, int x2, const PRow *pr, int np,
                   uint16_t *mw)
{
    uint16_t *g = (uint16_t *)row + ((x1 >> 4) << 2);
    const uint16_t keep = pr->keep;
    int n = (x2 >> 4) - (x1 >> 4), p;
    uint16_t lm = lmask[x1 & 15], rm = rmask[x2 & 15], m;

    if (n == 0) {
        lm &= rm;
    }
    m = (uint16_t)(lm & ~keep);
    for (p = 0; p < np; p++) {
        g[p] ^= (uint16_t)((g[p] ^ pr->w[p]) & m);
    }
    if (mw != NULL) {
        mw += x1 >> 4;
        *mw++ &= (uint16_t)~m;
    }
    if (n == 0) {
        return;
    }
    g += 4;
    if (keep == 0) {
        while (--n != 0) {
            for (p = 0; p < np; p++) {
                g[p] = pr->w[p];
            }
            g += 4;
            if (mw != NULL) {
                *mw++ = 0;
            }
        }
    } else {
        m = (uint16_t)~keep;
        while (--n != 0) {
            for (p = 0; p < np; p++) {
                g[p] ^= (uint16_t)((g[p] ^ pr->w[p]) & m);
            }
            g += 4;
            if (mw != NULL) {
                *mw++ &= keep;
            }
        }
    }
    m = (uint16_t)(rm & ~keep);
    for (p = 0; p < np; p++) {
        g[p] ^= (uint16_t)((g[p] ^ pr->w[p]) & m);
    }
    if (mw != NULL) {
        *mw &= (uint16_t)~m;
    }
}

#ifndef __m68k__
/*
 * The fast path's edges, as the 68000 holds them: one long per edge,
 * the remainder in the high word and the column in the low, biased
 * by 32768 so stepping never carries out of it. The remainder e
 * only ever moves by 16r and by D = 16 dy, so e & 15 never changes
 * and e < 16r exactly when e / 16 < r: the high word holds e / 16,
 * against r and dy, and makes the same decisions as e would. A
 * row's step is one add.l of
 *
 *   s = (-r - [q < 0]) << 16 | (q & 0xFFFF)
 *
 * whose carry out says e did not go below zero: the low word's own
 * carry, which a negative q always makes, is cancelled by the -1 in
 * the high word. With no carry, f = dy << 16 | 1 puts dy back and
 * the column on by one. (A slope with r = 0 and q >= 0 would carry
 * never; it is written q - 1 and r = dy instead, which borrows every
 * row and comes to the same.) Valid while dy fits fifteen bits -
 * edges up to 32767 sub-pixel rows, 2047 pixels, the divs.w's
 * divisor - and the columns lie within +-32767, which they do for any
 * polygon whose width fits that.
 */
typedef struct {
    uint32_t x, s, f;
} PEdge;

static void pedge_start(PEdge *pe, const STDL_PointSub *a,
                        const STDL_PointSub *b, int y)
{
    EStart s;
    int32_t q;
    uint32_t r;

    edge_setup(&s, a, b, y);
    q = s.q;
    r = s.r16 >> 4;
    if (r == 0 && q >= 0) {
        q--;
        r = s.d >> 4;
    }
    pe->s = ((uint32_t)(uint16_t)(0u - r - (q < 0 ? 1u : 0u)) << 16)
            | (uint16_t)q;
    pe->f = ((s.d >> 4) << 16) | 1u;
    pe->x = ((s.e >> 4) << 16) | (uint16_t)(s.c + 32768);
}

static __inline__ void pedge_step(PEdge *pe)
{
    const uint32_t x = pe->x + pe->s;

    pe->x = x < pe->x ? x : x + pe->f;  /* carry: e stayed >= 0 */
}
#endif

/* the edges at the polygon's full width: column and remainder in
 * 32 bits each, for polygons taller than 2047 pixels or wider than
 * the fast path takes, and on masked surfaces */
typedef struct {
    int32_t c, q;
    uint32_t e, r16, d;
} WEdge;

static void wedge_start(WEdge *we, const STDL_PointSub *a,
                        const STDL_PointSub *b, int y)
{
    EStart s;

    edge_setup(&s, a, b, y);
    we->c = s.c;
    we->e = s.e;
    we->q = s.q;
    we->r16 = s.r16;
    we->d = s.d;
}

static __inline__ void wedge_step(WEdge *we)
{
    we->c += we->q;
    if (we->e < we->r16) {
        we->e += we->d - we->r16;
        we->c++;
    } else {
        we->e -= we->r16;
    }
}

/* +1 when walking forwards from the top vertex goes down the left
 * side, -1 when it goes down the right, 0 for no area - as
 * STDL_FillPolygon's orientation(). The whole area only for a top
 * vertex its neighbours run straight through, or deltas past sixteen
 * bits; summed in 64 bits from 16-bit products, so no libgcc
 * multiply. */
static __attribute__((noinline)) int area_sign(const STDL_PointSub *pts,
                                               int n)
{
    long long area = 0;
    int i;

    for (i = 0; i < n; i++) {
        const STDL_PointSub *p = &pts[i], *q = &pts[i + 1 < n ? i + 1 : 0];

        area += (long long)STDL_MulS16(p->x, q->y);
        area -= (long long)STDL_MulS16(q->x, p->y);
    }
    return area < 0 ? 1 : area > 0 ? -1 : 0;
}

static int orientation(const STDL_PointSub *pts, int n, int top)
{
    const STDL_PointSub *t = &pts[top];
    const STDL_PointSub *nx = &pts[top + 1 < n ? top + 1 : 0];
    const STDL_PointSub *pv = &pts[top > 0 ? top - 1 : n - 1];
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

/* what one polygon's walk needs */
typedef struct {
    const STDL_PointSub *pts;
    int n, top, dir, bottom;    /* bottom: the lowest vertex's y */
    int y, y2;                  /* pixel rows [y, y2) */
    int lo, hi;                 /* columns [lo, hi) */
    uint8_t *row;               /* row y */
    int stride;
    PRow pr[4];                 /* by row & 3 */
    int np;
} Walk;

/* the next vertex down a side: dir +1 forwards, -1 backwards */
static __inline__ int next_vertex(int i, int dir, int n)
{
    i += dir;
    if (i == n) {
        i = 0;
    }
    if (i < 0) {
        i = n - 1;
    }
    return i;
}

#ifndef __m68k__
/* rows [y, stop) between edges l and r, as the 68000's row loops do
 * them; returns the row after */
static uint8_t *rows_fast(const Walk *w, PEdge *l, PEdge *r, uint8_t *row,
                          int y, int stop, int clamp)
{
    const unsigned lo = (unsigned)(w->lo + 32768);
    const unsigned hi = (unsigned)(w->hi + 32768);

    for (; y < stop; y++) {
        unsigned a = (uint16_t)l->x, e = (uint16_t)r->x;

        if (clamp) {
            if (a < lo) {
                a = lo;
            }
            if (e > hi) {
                e = hi;
            }
        }
        if (a < e) {
            span_c(row, (int)a - 32768, (int)e - 32768 - 1, &w->pr[y & 3],
                   w->np, NULL);
        }
        pedge_step(l);
        pedge_step(r);
        row += w->stride;
    }
    return row;
}

/*
 * The fast path, in C: the 68000's twin. Both sides walked down from
 * the top vertex - skipping edges that are flat or end above the
 * row, stopping at the bottom vertex - a trapezoid at a time.
 */
static void walk_fast(Walk *w, int clamp)
{
    const STDL_PointSub *pts = w->pts;
    int il = w->top, ir = w->top, lend = CEIL16(pts[w->top].y);
    int rend = lend, y = w->y;
    uint8_t *row = w->row;
    PEdge l = { 0, 0, 0 }, r = { 0, 0, 0 };

    for (;;) {
        int stop;

        /* no test for the bottom vertex here, as in the 68000: a side
         * stops at the edge reaching it, whose row ceil(bottom / 16)
         * is at least y2, and the walk ends at y2 - it would never
         * fire */
        while (lend <= y) {
            const int b = next_vertex(il, w->dir, w->n);

            lend = CEIL16(pts[b].y);
            if (lend > y) {
                pedge_start(&l, &pts[il], &pts[b], y);
            }
            il = b;
        }
        while (rend <= y) {
            const int b = next_vertex(ir, -w->dir, w->n);

            rend = CEIL16(pts[b].y);
            if (rend > y) {
                pedge_start(&r, &pts[ir], &pts[b], y);
            }
            ir = b;
        }
        stop = lend < rend ? lend : rend;
        if (stop > w->y2) {
            stop = w->y2;
        }
        row = rows_fast(w, &l, &r, row, y, stop, clamp);
        y = stop;
        if (y >= w->y2) {
            return;
        }
    }
}

/*
 * A triangle, as the 68000 takes one: its vertices sorted by y into
 * top, middle and bottom, the long edge top-bottom on one side for
 * every row and the short ones top-middle and middle-bottom on the
 * other, the side from one cross product. Three edges set up and no
 * walk: the same pixels as walk_fast.
 */
static void walk_tri(Walk *w, int clamp)
{
    const STDL_PointSub *t = &w->pts[0], *m = &w->pts[1];
    const STDL_PointSub *b = &w->pts[2], *u;
    PEdge lng, sht;
    int ym, y = w->y;
    int32_t c1, c2;
    uint8_t *row = w->row;

    /* the 68000's three compare-and-exchanges */
    if (m->y < t->y) { u = t; t = m; m = u; }
    if (b->y < m->y) {
        u = m; m = b; b = u;
        if (m->y < t->y) { u = t; t = m; m = u; }
    }
    c1 = STDL_MulS16(b->x - t->x, m->y - t->y);
    c2 = STDL_MulS16(b->y - t->y, m->x - t->x);
    if (c1 == c2) {
        return;                 /* no area */
    }
    ym = CEIL16(m->y);
    pedge_start(&lng, t, b, y);
    if (y < ym) {
        const int stop = ym < w->y2 ? ym : w->y2;

        pedge_start(&sht, t, m, y);
        /* c1 < c2: the middle is right of the long edge, which is
         * then the left side */
        row = c1 < c2 ? rows_fast(w, &lng, &sht, row, y, stop, clamp)
                      : rows_fast(w, &sht, &lng, row, y, stop, clamp);
        y = stop;
        if (y >= w->y2) {
            return;
        }
    }
    pedge_start(&sht, m, b, y);
    if (c1 < c2) {
        rows_fast(w, &lng, &sht, row, y, w->y2, clamp);
    } else {
        rows_fast(w, &sht, &lng, row, y, w->y2, clamp);
    }
}

#endif

/* everything else, with 32-bit edges: the same walk, the same
 * pixels, and the surface mask kept */
static __attribute__((noinline)) void walk_wide(Walk *w, STDL_Surface *dst)
{
    const STDL_PointSub *pts = w->pts;
    int il = w->top, ir = w->top, lend = CEIL16(pts[w->top].y);
    int rend = lend, y = w->y;
    uint8_t *row = w->row, *mrow = NULL;
    WEdge l = { 0, 0, 0, 0, 0 }, r = { 0, 0, 0, 0, 0 };

    if (dst->mask != NULL) {
        mrow = dst->mask + stdl_row_off(y, dst->maskstride);
        dst->opaque_state = 0;
    }
    for (;;) {
        int stop;

        while (lend <= y) {
            const int b = next_vertex(il, w->dir, w->n);

            if (pts[il].y >= w->bottom) {
                return;
            }
            lend = CEIL16(pts[b].y);
            if (lend > y) {
                wedge_start(&l, &pts[il], &pts[b], y);
            }
            il = b;
        }
        while (rend <= y) {
            const int b = next_vertex(ir, -w->dir, w->n);

            if (pts[ir].y >= w->bottom) {
                return;
            }
            rend = CEIL16(pts[b].y);
            if (rend > y) {
                wedge_start(&r, &pts[ir], &pts[b], y);
            }
            ir = b;
        }
        stop = lend < rend ? lend : rend;
        if (stop > w->y2) {
            stop = w->y2;
        }
        for (; y < stop; y++) {
            const int32_t a = l.c < w->lo ? w->lo : l.c;
            const int32_t e = r.c > w->hi ? w->hi : r.c;

            if (a < e) {
                span_c(row, (int)a, (int)e - 1, &w->pr[y & 3], w->np,
                       (uint16_t *)mrow);
            }
            wedge_step(&l);
            wedge_step(&r);
            row += w->stride;
            if (mrow != NULL) {
                mrow += dst->maskstride;
            }
        }
        if (y >= w->y2) {
            return;
        }
    }
}

#ifdef __m68k__
/*
 * The fast path in 68000: the C's walk_fast with pedge_start and the
 * rows, start to finish - the vertex scan, the clip, the winding, each
 * edge's set-up and the row loops. STDL_FillPolygonPattern itself is
 * the entry for an opaque pattern at a budget of 2 or 4 on a surface
 * with no mask, which is what a game draws its faces with; it reads
 * the STDL_Pattern in place. The C (stdl_ppoly_c) makes a copy for a
 * transparent pattern or an odd budget and comes in through
 * stdl_ppoly_walk.
 *
 * The row loops take their state in registers: a0 stride, a1 the edge
 * masks, a2 the row (less 16384, the column bias halved), a4/a5 the
 * edges' steps, a6 the pattern row, d4/d5 the edges (remainder:column
 * + 32768); d6/d7 are the row's plane words, loaded from the pattern
 * each row, and d0-d3/a3 are theirs. The fixes dy:1, the rows left,
 * the pattern's end and the clamp's columns are in the walker's
 * frame. A row is one pattern entry: two longs of plane words, and
 * for a transparent pattern a third, the row's mask inverted into
 * both words. Opaque rows store the middle groups outright,
 * transparent ones merge them under it.
 */
/* the offsets the 68000 reads; __builtin_offsetof because libcmini's
 * offsetof is not a constant expression here */
typedef char pp_surface_layout[
    __builtin_offsetof(STDL_Surface, stride) == 8
    && __builtin_offsetof(STDL_Surface, clip) == 16
    && __builtin_offsetof(STDL_Surface, mask) == 24
    && __builtin_offsetof(STDL_Pattern, mask) == 32
    && sizeof(int) == 4 ? 1 : -1];
int stdl_ppoly_walk(const STDL_Surface *dst, const STDL_PointSub *pts,
                    int n, void (*rows)(void), void (*rows_clamp)(void),
                    const void *pat, int esize);
void stdl_pprows_o2(void);
void stdl_pprows_o4(void);
void stdl_pprows_t2(void);
void stdl_pprows_t4(void);
void stdl_pprows_o2c(void);
void stdl_pprows_o4c(void);
void stdl_pprows_t2c(void);
void stdl_pprows_t4c(void);

#define P_STR2(x) #x
#define P_STR(x) P_STR2(x)
#define P_SYM(n) P_STR(__USER_LABEL_PREFIX__) #n
/* an edge group under mask M: A = A ^ ((A ^ L) & M) */
#define P_EDGE(M, A, L) \
    "move.l " A ",%d2\n\t" "eor.l " L ",%d2\n\t" "and.l " M ",%d2\n\t" \
    "eor.l %d2," A "\n\t"
/* the same, stepping A on: (a3)+ */
#define P_EDGEI(M, L) \
    "move.l (%a3),%d2\n\t" "eor.l " L ",%d2\n\t" "and.l " M ",%d2\n\t" \
    "eor.l %d2,(%a3)+\n\t"
#define P_ON(x) x
#define P_OFF(x)
/* the frame, as the row loops see it (the walker's, 4 further up) */
#define P_FL "32(%sp)"
#define P_FR "36(%sp)"
#define P_CNT "40(%sp)"
#define P_PEND "44(%sp)"
#define P_LO "48(%sp)"
#define P_HI "50(%sp)"
/*
 * NAME: the row loop for one kind. ESIZE the pattern entry, P23 the
 * planes 2-3 pair, TR the transparent pattern's mask (third long of
 * the entry, at -4(a6) once the row's words are loaded), CL the
 * clamp to the clip's columns, MID one middle group.
 */
/*
 * The opaque rows, unrolled four ways by the pattern's row: copy k
 * loads row k's plane words from a fixed offset of the pattern (a6,
 * which then never moves), so a row has no pointer to step and no
 * wrap to test - 20 cycles of each row's 52 - and a two-plane row
 * loads one long, not two. The entry jumps to the copy for the first
 * row's y & 3 (d3, the row the walker is on); each copy counts the
 * row off and the last goes round to the first.
 */
#define P_OBODY(P23, P23N, CL, MID)                                       \
    "move.w %d4,%d0\n\t"                /* the left's first column */     \
    "move.w %d5,%d1\n\t"                /* the right's end */             \
    CL("cmp.w  " P_LO ",%d0\n\t"                                          \
       "bcc.s  11f\n\t"                                                   \
       "move.w " P_LO ",%d0\n"                                            \
       "11:\n\t"                                                          \
       "cmp.w  " P_HI ",%d1\n\t"                                          \
       "bls.s  12f\n\t"                                                   \
       "move.w " P_HI ",%d1\n"                                            \
       "12:\n\t")                                                         \
    "cmp.w  %d0,%d1\n\t"                                                  \
    "bls    9f\n\t"                     /* an empty row */                \
    "subq.w #1,%d1\n\t"                 /* the right's last column */     \
    "moveq  #-16,%d2\n\t"                                                 \
    "and.w  %d0,%d2\n\t"                                                  \
    "eor.w  %d2,%d0\n\t"                /* the left's column in group */  \
    "moveq  #-16,%d3\n\t"                                                 \
    "and.w  %d1,%d3\n\t"                                                  \
    "eor.w  %d3,%d1\n\t"                                                  \
    "lsr.w  #1,%d2\n\t"                 /* its group's byte offset */     \
    "lea    0(%a2,%d2.w),%a3\n\t"                                         \
    "add.w  %d0,%d0\n\t"                                                  \
    "add.w  %d0,%d0\n\t"                                                  \
    "move.l 0(%a1,%d0.w),%d0\n\t"       /* left mask, in both words */    \
    "add.w  %d1,%d1\n\t"                                                  \
    "add.w  %d1,%d1\n\t"                                                  \
    "move.l 64(%a1,%d1.w),%d1\n\t"      /* right mask */                  \
    "lsr.w  #1,%d3\n\t"                                                   \
    "sub.w  %d2,%d3\n\t"                /* 8 x the groups after it */     \
    "bne.s  2f\n\t"                                                       \
    "and.l  %d1,%d0\n\t"                /* one group */                   \
    P_EDGE("%d0", "(%a3)", "%d6")                                         \
    P23(P_EDGE("%d0", "4(%a3)", "%d7"))                                   \
    "bra    9f\n"                                                         \
    "2:\n\t"                                                              \
    P_EDGEI("%d0", "%d6")                                                 \
    P23(P_EDGEI("%d0", "%d7"))                                            \
    P23N("addq.l #4,%a3\n\t")                                             \
    "lsr.w  #3,%d3\n\t"                                                   \
    "subq.w #2,%d3\n\t"                                                   \
    "bmi.s  4f\n\t"                                                       \
    "3:\n\t"                                                              \
    MID                                                                   \
    "dbf    %d3,3b\n"                                                     \
    "4:\n\t"                                                              \
    P_EDGE("%d1", "(%a3)", "%d6")                                         \
    P23(P_EDGE("%d1", "4(%a3)", "%d7"))                                   \
    "9:\n\t"                                                              \
    "add.l  %a4,%d4\n\t"                /* carry: no borrow */            \
    "bcs.s  5f\n\t"                                                       \
    "add.l  " P_FL ",%d4\n"                                               \
    "5:\n\t"                                                              \
    "add.l  %a5,%d5\n\t"                                                  \
    "bcs.s  6f\n\t"                                                       \
    "add.l  " P_FR ",%d5\n"                                               \
    "6:\n\t"                                                              \

#define P_OLOAD(K, P23)                                                   \
    "move.l " #K "(%a6),%d6\n\t"                                          \
    P23("move.l " #K "+4(%a6),%d7\n\t")
#define PPROWS4(NAME, P23, P23N, CL, MID)                                 \
__asm__(                                                                  \
    "\t.text\n\t.even\n\t.globl " P_SYM(NAME) "\n"                        \
    P_SYM(NAME) ":\n\t"                                                    \
    "moveq  #3,%d0\n\t"                                                   \
    "and.w  %d3,%d0\n\t"                                                  \
    "add.w  %d0,%d0\n\t"                                                  \
    "move.w 90f(%pc,%d0.w),%d0\n\t"                                       \
    "jmp    90f(%pc,%d0.w)\n"                                              \
    "90:\n\t"                                                             \
    ".word  80f-90b,81f-90b,82f-90b,83f-90b\n"                            \
    "80:\n\t"                                                             \
    P_OLOAD(0, P23) P_OBODY(P23, P23N, CL, MID)                           \
    "adda.l %a0,%a2\n\t"                                                  \
    "subq.w #1," P_CNT "\n\t"                                             \
    "beq    99f\n"                                                        \
    "81:\n\t"                                                             \
    P_OLOAD(8, P23) P_OBODY(P23, P23N, CL, MID)                           \
    "adda.l %a0,%a2\n\t"                                                  \
    "subq.w #1," P_CNT "\n\t"                                             \
    "beq    99f\n"                                                        \
    "82:\n\t"                                                             \
    P_OLOAD(16, P23) P_OBODY(P23, P23N, CL, MID)                          \
    "adda.l %a0,%a2\n\t"                                                  \
    "subq.w #1," P_CNT "\n\t"                                             \
    "beq    99f\n"                                                        \
    "83:\n\t"                                                             \
    P_OLOAD(24, P23) P_OBODY(P23, P23N, CL, MID)                          \
    "adda.l %a0,%a2\n\t"                                                  \
    "subq.w #1," P_CNT "\n\t"                                             \
    "bne    80b\n"                                                        \
    "99:\n\t"                                                             \
    "rts\n");
#define PPROWS(NAME, ESIZE, P23, P23N, TR, CL, MID)                       \
__asm__(                                                                  \
    "\t.text\n\t.even\n\t.globl " P_SYM(NAME) "\n"                        \
    P_SYM(NAME) ":\n"                                                     \
    "1:\n\t"                                                              \
    "move.l (%a6)+,%d6\n\t"                                               \
    "move.l (%a6)+,%d7\n\t"                                               \
    TR("addq.l #4,%a6\n\t")                                               \
    "move.w %d4,%d0\n\t"                /* the left's first column */     \
    "move.w %d5,%d1\n\t"                /* the right's end */             \
    CL("cmp.w  " P_LO ",%d0\n\t"                                          \
       "bcc.s  11f\n\t"                                                   \
       "move.w " P_LO ",%d0\n"                                            \
       "11:\n\t"                                                          \
       "cmp.w  " P_HI ",%d1\n\t"                                          \
       "bls.s  12f\n\t"                                                   \
       "move.w " P_HI ",%d1\n"                                            \
       "12:\n\t")                                                         \
    "cmp.w  %d0,%d1\n\t"                                                  \
    "bls    9f\n\t"                     /* an empty row */                \
    "subq.w #1,%d1\n\t"                 /* the right's last column */     \
    "moveq  #-16,%d2\n\t"                                                 \
    "and.w  %d0,%d2\n\t"                                                  \
    "eor.w  %d2,%d0\n\t"                /* the left's column in group */  \
    "moveq  #-16,%d3\n\t"                                                 \
    "and.w  %d1,%d3\n\t"                                                  \
    "eor.w  %d3,%d1\n\t"                                                  \
    "lsr.w  #1,%d2\n\t"                 /* its group's byte offset */     \
    "lea    0(%a2,%d2.w),%a3\n\t"                                         \
    "add.w  %d0,%d0\n\t"                                                  \
    "add.w  %d0,%d0\n\t"                                                  \
    "move.l 0(%a1,%d0.w),%d0\n\t"       /* left mask, in both words */    \
    "add.w  %d1,%d1\n\t"                                                  \
    "add.w  %d1,%d1\n\t"                                                  \
    "move.l 64(%a1,%d1.w),%d1\n\t"      /* right mask */                  \
    "lsr.w  #1,%d3\n\t"                                                   \
    "sub.w  %d2,%d3\n\t"                /* 8 x the groups after it */     \
    "bne.s  2f\n\t"                                                       \
    "and.l  %d1,%d0\n\t"                /* one group */                   \
    TR("and.l  -4(%a6),%d0\n\t")                                          \
    P_EDGE("%d0", "(%a3)", "%d6")                                         \
    P23(P_EDGE("%d0", "4(%a3)", "%d7"))                                   \
    "bra    9f\n"                                                         \
    "2:\n\t"                                                              \
    TR("and.l  -4(%a6),%d0\n\t")                                          \
    P_EDGEI("%d0", "%d6")                                                 \
    P23(P_EDGEI("%d0", "%d7"))                                            \
    P23N("addq.l #4,%a3\n\t")                                             \
    "lsr.w  #3,%d3\n\t"                                                   \
    "subq.w #2,%d3\n\t"                                                   \
    "bmi.s  4f\n\t"                                                       \
    TR("move.l -4(%a6),%d0\n\t")                                          \
    "3:\n\t"                                                              \
    MID                                                                   \
    "dbf    %d3,3b\n"                                                     \
    "4:\n\t"                                                              \
    TR("and.l  -4(%a6),%d1\n\t")                                          \
    P_EDGE("%d1", "(%a3)", "%d6")                                         \
    P23(P_EDGE("%d1", "4(%a3)", "%d7"))                                   \
    "9:\n\t"                                                              \
    "add.l  %a4,%d4\n\t"                /* carry: no borrow */            \
    "bcs.s  5f\n\t"                                                       \
    "add.l  " P_FL ",%d4\n"                                               \
    "5:\n\t"                                                              \
    "add.l  %a5,%d5\n\t"                                                  \
    "bcs.s  6f\n\t"                                                       \
    "add.l  " P_FR ",%d5\n"                                               \
    "6:\n\t"                                                              \
    "cmpa.l " P_PEND ",%a6\n\t"                                           \
    "bne.s  7f\n\t"                                                       \
    "lea    -" #ESIZE "*4(%a6),%a6\n"   /* the pattern's first row */     \
    "7:\n\t"                                                              \
    "adda.l %a0,%a2\n\t"                                                  \
    "subq.w #1," P_CNT "\n\t"                                             \
    "bne    1b\n\t"                                                       \
    "rts\n");
#define MID_O2 "move.l %d6,(%a3)\n\t" "addq.l #8,%a3\n\t"
#define MID_O4 "move.l %d6,(%a3)+\n\t" "move.l %d7,(%a3)+\n\t"
#define MID_T2 P_EDGEI("%d0", "%d6") "addq.l #4,%a3\n\t"
#define MID_T4 P_EDGEI("%d0", "%d6") P_EDGEI("%d0", "%d7")
PPROWS4(stdl_pprows_o2, P_OFF, P_ON, P_OFF, MID_O2)
PPROWS4(stdl_pprows_o4, P_ON, P_OFF, P_OFF, MID_O4)
PPROWS(stdl_pprows_t2, 12, P_OFF, P_ON, P_ON, P_OFF, MID_T2)
PPROWS(stdl_pprows_t4, 12, P_ON, P_OFF, P_ON, P_OFF, MID_T4)
PPROWS4(stdl_pprows_o2c, P_OFF, P_ON, P_ON, MID_O2)
PPROWS4(stdl_pprows_o4c, P_ON, P_OFF, P_ON, MID_O4)
PPROWS(stdl_pprows_t2c, 12, P_OFF, P_ON, P_ON, P_ON, MID_T2)
PPROWS(stdl_pprows_t4c, 12, P_ON, P_OFF, P_ON, P_ON, MID_T4)

/*
 * An edge from vertex a (x:y in d6) to vertex b (x:y in d7), set up
 * for pixel row d3: pedge_start. The slope from one divs.w, floored;
 * B = a.x + t q and t r from a muls.w and a mulu.w, t the sub-pixel
 * rows from a to the row; under a clipped top (t >= 16) a divu.w
 * folds t r's whole columns into B; then (B & 15) dy + t r against D
 * says whether the column is B >> 4 plus 0, 1 or 2. X is the edge's
 * register, free while it is set up, SR its step's, F its fix's
 * frame slot. d0-d2 and d6-d7 scratch; d3 kept.
 */
#define P_SETUP(X, SR, F)                                                 \
    "move.w %d7,%d1\n\t"                                                  \
    "sub.w  %d6,%d1\n\t"                /* dy */                          \
    "move.w %d3,%d2\n\t"                                                  \
    "lsl.w  #4,%d2\n\t"                                                   \
    "sub.w  %d6,%d2\n\t"                /* t, rows below a */             \
    "swap   %d6\n\t"                                                      \
    "swap   %d7\n\t"                                                      \
    "move.w %d6,%d0\n\t"                                                  \
    "ext.l  %d0\n\t"                    /* a.x */                         \
    "sub.w  %d6,%d7\n\t"                /* dx */                          \
    "bne.s  20f\n\t"                                                      \
    "moveq  #0,%d6\n\t"                 /* vertical: q = r = 0, B = a.x */ \
    "moveq  #0,%d7\n\t"                                                   \
    "moveq  #0," X "\n\t"                                                 \
    "bra.s  22f\n"                                                        \
    "20:\n\t"                                                             \
    "move.w %d7,%d6\n\t"                                                  \
    "ext.l  %d6\n\t"                                                      \
    "divs.w %d1,%d6\n\t"                /* r:q */                         \
    "move.l %d6,%d7\n\t"                                                  \
    "swap   %d7\n\t"                    /* r */                           \
    "tst.w  %d7\n\t"                                                      \
    "bpl.s  21f\n\t"                                                      \
    "add.w  %d1,%d7\n\t"                /* floor: 0 <= r < dy */          \
    "subq.w #1,%d6\n"                                                     \
    "21:\n\t"                                                             \
    "moveq  #0," X "\n\t"                                                 \
    "tst.w  %d2\n\t"                                                      \
    "beq.s  22f\n\t"                    /* a on the row: B = a.x, t r = 0 */\
    "move.w %d6," X "\n\t"                                                \
    "muls.w %d2," X "\n\t"              /* t the source: under 16, cheap */\
    "add.l  " X ",%d0\n\t"              /* B = a.x + t q */               \
    "move.w %d7," X "\n\t"                                                \
    "mulu.w %d2," X "\n\t"              /* t r */                         \
    "cmp.w  #16,%d2\n\t"                                                  \
    "bcs.s  22f\n\t"                                                      \
    "divu.w %d1," X "\n\t"              /* a clipped top: whole columns */\
    "moveq  #0,%d2\n\t"                                                   \
    "move.w " X ",%d2\n\t"                                                \
    "add.l  %d2,%d0\n\t"                                                  \
    "clr.w  " X "\n\t"                                                    \
    "swap   " X "\n"                    /* t r mod dy */                  \
    "22:\n\t"                                                             \
    /* x = B + t r / dy sub-pixels, t r < 16 dy. With k0 = -B & 15 the  \
     * column is (B + k0) / 16 when k0 dy >= t r, else one more; the     \
     * remainder e is k0 dy - t r, plus D in the second case, and the    \
     * edge keeps e / 16 against dy (pedge_start's comment) */           \
    "moveq  #0,%d2\n\t"                                                   \
    "sub.w  %d0,%d2\n\t"                                                  \
    "and.w  #15,%d2\n\t"                /* k0 */                          \
    "add.l  %d2,%d0\n\t"                                                  \
    "asr.l  #4,%d0\n\t"                 /* (B + k0) / 16 */               \
    "mulu.w %d1,%d2\n\t"                /* k0 dy */                       \
    "sub.l  " X ",%d2\n\t"                                                \
    "bcc.s  23f\n\t"                                                      \
    "addq.w #1,%d0\n\t"                                                   \
    "asr.l  #4,%d2\n\t"                                                   \
    "add.w  %d1,%d2\n\t"                /* (e + D) / 16 = e / 16 + dy */  \
    "bra.s  24f\n"                                                        \
    "23:\n\t"                                                             \
    "lsr.l  #4,%d2\n"                                                     \
    "24:\n\t"                                                             \
    "swap   %d2\n\t"                                                      \
    "move.w %d0,%d2\n\t"                                                  \
    "eor.w  #0x8000,%d2\n\t"            /* e / 16:column + 32768 */       \
    "move.l %d2," X "\n\t"                                                \
    "tst.w  %d7\n\t"                    /* r */                           \
    "bne.s  26f\n\t"                                                      \
    "tst.w  %d6\n\t"                                                      \
    "bmi.s  26f\n\t"                                                      \
    "subq.w #1,%d6\n\t"                 /* r = 0, q >= 0: q - 1, r = dy */\
    "move.w %d1,%d7\n"                                                    \
    "26:\n\t"                                                             \
    "neg.w  %d7\n\t"                                                      \
    "tst.w  %d6\n\t"                                                      \
    "bpl.s  27f\n\t"                                                      \
    "subq.w #1,%d7\n"                   /* the low word's carry */        \
    "27:\n\t"                                                             \
    "swap   %d7\n\t"                                                      \
    "move.w %d6,%d7\n\t"                                                  \
    "move.l %d7," SR "\n\t"             /* the step */                    \
    "swap   %d1\n\t"                                                      \
    "move.w #1,%d1\n\t"                                                   \
    "move.l %d1," F "\n\t"              /* the fix, dy:1 */
/*
 * One side: while its edge ends at or above row d3, move to the next
 * vertex (ADV adds or subtracts the step, wrapping round); an edge
 * reaching below the row is set up. No test for the bottom vertex: a
 * side stops at the edge that reaches it, whose row is at least the
 * last, and the walk ends at the last row (walk_fast's comment).
 * IDX and END are the side's frame slots, END the pixel row its edge
 * ends on (exclusive).
 */
#define P_SIDE(IDX, END, ADV, X, SR, F)                                   \
    "1:\n\t"                                                              \
    "cmp.w  " END "(%sp),%d3\n\t"                                         \
    "blt    8f\n\t"                     /* its edge reaches below */      \
    "move.l (%sp),%a3\n\t"              /* the vertices */                \
    "move.w " IDX "(%sp),%d0\n\t"                                         \
    "move.l 0(%a3,%d0.w),%d6\n\t"       /* a, x:y */                      \
    ADV " 10(%sp),%d0\n\t"                                                \
    "bpl.s  2f\n\t"                                                       \
    "add.w  8(%sp),%d0\n\t"             /* below the first: the last */   \
    "bra.s  3f\n"                                                         \
    "2:\n\t"                                                              \
    "cmp.w  8(%sp),%d0\n\t"                                               \
    "bne.s  3f\n\t"                                                       \
    "moveq  #0,%d0\n"                   /* past the last: the first */    \
    "3:\n\t"                                                              \
    "move.w %d0," IDX "(%sp)\n\t"                                         \
    "move.l 0(%a3,%d0.w),%d7\n\t"       /* b */                           \
    "move.w %d7,%d1\n\t"                                                  \
    "ext.l  %d1\n\t"                                                      \
    "moveq  #15,%d0\n\t"                                                  \
    "add.l  %d0,%d1\n\t"                                                  \
    "asr.l  #4,%d1\n\t"                 /* its first row past b */        \
    "move.w %d1," END "(%sp)\n\t"                                         \
    "cmp.w  %d3,%d1\n\t"                                                  \
    "ble    1b\n\t"                     /* flat, or above the row */      \
    P_SETUP(X, SR, F)                                                     \
    "8:\n\t"

/* the triangle's first trapezoid: rows to the middle's row or the
 * last, whichever is first, leaving d3 the row reached; done if that
 * is the last */
#define P_TRAP1                                                           \
    "move.w 16(%sp),%d0\n\t"            /* the middle's row */            \
    "cmp.w  18(%sp),%d0\n\t"                                              \
    "ble.s  9f\n\t"                                                       \
    "move.w 18(%sp),%d0\n"              /* or the last */                 \
    "9:\n\t"                                                              \
    "move.w %d0,20(%sp)\n\t"                                              \
    "sub.w  %d3,%d0\n\t"                                                  \
    "move.w %d0,36(%sp)\n\t"            /* the trapezoid's rows */        \
    "move.l 4(%sp),%a3\n\t"                                               \
    "jsr    (%a3)\n\t"                                                    \
    "move.w 20(%sp),%d3\n\t"                                              \
    "cmp.w  18(%sp),%d3\n\t"                                              \
    "bge    99f\n"

/*
 * The frame: 0 vertices, 4 row loop, 8 n x 4, 10 step x 4, 12/14 the
 * left's and right's vertex, 16/18 the rows their edges end on, 20
 * the row, 22 the last row (exclusive), 26 where a declined polygon
 * goes (0: back to the C, else on to stdl_ppoly_wide), 28/32 the
 * edges' fixes, 36 the trapezoid's rows, 40 the pattern's end, 44/46
 * the clip's columns, biased (only when clamping). The triangle uses
 * 0, 8 and 12 for its vertices, 16 the middle's row and 22 its side
 * instead. The arguments are 112 bytes up: dst, pts, n, then the
 * pattern for the public entry or the rest of stdl_ppoly_walk's. Into
 * the walk (label 50): a1 the pattern, a2 its entry's size, a4/a5 the
 * row loops, unclamped and clamped.
 */
__asm__(
    "\t.text\n\t.even\n\t.globl " P_SYM(STDL_FillPolygonPattern) "\n"
    P_SYM(STDL_FillPolygonPattern) ":\n\t"
    /* what the C must see to: no surface, no pixels, a mask, no
     * pattern, a transparent one, an odd budget, no vertices, too few
     * or too many; scratch registers only */
    "move.l 4(%sp),%d0\n\t"
    "beq.s  97f\n\t"
    "move.l %d0,%a0\n\t"
    "tst.l  (%a0)\n\t"
    "beq.s  97f\n\t"
    "tst.l  24(%a0)\n\t"
    "bne.s  97f\n\t"
    "move.l 16(%sp),%d0\n\t"
    "beq.s  97f\n\t"
    "move.l %d0,%a1\n\t"
    "move.l 32(%a1),%d1\n\t"
    "or.l   36(%a1),%d1\n\t"
    "bne.s  97f\n\t"
    "move.l " P_SYM(stdl_planes) ",%d1\n\t"
    "btst   #0,%d1\n\t"
    "bne.s  97f\n\t"
    "tst.l  8(%sp)\n\t"
    "beq.s  97f\n\t"
    "move.l 12(%sp),%d0\n\t"
    "subq.l #3,%d0\n\t"
    "cmp.l  #8191-3,%d0\n\t"
    "bls.s  96f\n"
    "97:\n\t"
    "jmp    " P_SYM(stdl_ppoly_c) "\n"
    "96:\n\t"
    "movem.l %d2-%d7/%a2-%a6,-(%sp)\n\t"
    "lea    -64(%sp),%sp\n\t"
    "move.w #1,26(%sp)\n\t"             /* declined: on to the C's */
    "cmp.w  #2,%d1\n\t"
    "beq.s  1f\n\t"
    "lea    " P_SYM(stdl_pprows_o4) ",%a4\n\t"
    "lea    " P_SYM(stdl_pprows_o4c) ",%a5\n\t"
    "bra.s  2f\n"
    "1:\n\t"
    "lea    " P_SYM(stdl_pprows_o2) ",%a4\n\t"
    "lea    " P_SYM(stdl_pprows_o2c) ",%a5\n"
    "2:\n\t"
    "cmp.w  #3,122(%sp)\n\t"
    "beq    60f\n\t"                     /* a triangle */
    "move.w #8,%a2\n\t"                 /* the pattern's entry: 8 bytes */
    "bra    50f\n\t"

    /* stdl_ppoly_walk(dst, pts, n, rows, rows_clamp, pat, esize): the
     * same from the C, which has made the pattern's copy; 0 when it
     * has drawn or found nothing to draw, 1 to decline */
    "\t.globl " P_SYM(stdl_ppoly_walk) "\n"
    P_SYM(stdl_ppoly_walk) ":\n\t"
    "movem.l %d2-%d7/%a2-%a6,-(%sp)\n\t"
    "lea    -64(%sp),%sp\n\t"
    "clr.w  26(%sp)\n\t"
    "move.l 124(%sp),%a4\n\t"           /* the rows, unclamped */
    "move.l 128(%sp),%a5\n\t"           /* and clamped */
    "move.l 132(%sp),%a1\n\t"           /* the pattern */
    "move.l 136(%sp),%a2\n\t"           /* its entry's size */
    "bra    50f\n"

    /* A triangle (the public entry only - an opaque pattern read in
     * place, so the entry is 8 bytes): walk_tri. The vertices sorted
     * by y into top, middle and bottom, the long edge top-bottom on
     * one side and the two short ones on the other, the side from
     * one cross product; everything in registers but the vertices,
     * kept in the frame across the edges' set-ups. Frame: 0/8/12 top,
     * middle, bottom (x:y), 4 row loop, 16 the middle's row, 18 the
     * last row (exclusive), 20 the row, 22 the long edge's side
     * (0xFF: left). */
    "60:\n\t"
    "move.l 116(%sp),%a0\n\t"
    "move.l (%a0)+,%d0\n\t"
    "move.l (%a0)+,%d1\n\t"
    "move.l (%a0),%d2\n\t"
    "cmp.w  %d0,%d1\n\t"
    "bge.s  1f\n\t"
    "exg    %d0,%d1\n"
    "1:\n\t"
    "cmp.w  %d1,%d2\n\t"
    "bge.s  2f\n\t"
    "exg    %d1,%d2\n\t"
    "cmp.w  %d0,%d1\n\t"
    "bge.s  2f\n\t"
    "exg    %d0,%d1\n"
    "2:\n\t"
    /* the rows: ceil(y / 16) of the top and bottom, clipped; out
     * before any other work if none is left */
    "moveq  #15,%d7\n\t"
    "move.w %d0,%d3\n\t"
    "ext.l  %d3\n\t"
    "add.l  %d7,%d3\n\t"
    "asr.l  #4,%d3\n\t"                 /* the first row */
    "move.w %d2,%d4\n\t"
    "ext.l  %d4\n\t"
    "add.l  %d7,%d4\n\t"
    "asr.l  #4,%d4\n\t"                 /* the last (exclusive) */
    "move.l 112(%sp),%a0\n\t"           /* the surface */
    "move.w 18(%a0),%d5\n\t"
    "cmp.w  %d5,%d3\n\t"
    "bge.s  1f\n\t"
    "move.w %d5,%d3\n"
    "1:\n\t"
    "add.w  22(%a0),%d5\n\t"
    "cmp.w  %d5,%d4\n\t"
    "ble.s  2f\n\t"
    "move.w %d5,%d4\n"
    "2:\n\t"
    "cmp.w  %d4,%d3\n\t"
    "bge    99f\n\t"                    /* no row inside the clip */
    "move.w %d3,20(%sp)\n\t"
    "move.w %d4,18(%sp)\n\t"
    "move.w %d1,%d5\n\t"
    "ext.l  %d5\n\t"
    "add.l  %d7,%d5\n\t"
    "asr.l  #4,%d5\n\t"
    "move.w %d5,16(%sp)\n\t"            /* the middle's row */
    "move.w %d2,%d5\n\t"
    "sub.w  %d0,%d5\n\t"                /* uy, the long edge's dy */
    "cmp.w  #32767,%d5\n\t"
    "bhi    98f\n\t"                    /* taller than 32767 */
    "move.l %d0,(%sp)\n\t"
    "move.l %d1,8(%sp)\n\t"
    "move.l %d2,12(%sp)\n\t"
    "move.w %d1,%d6\n\t"
    "sub.w  %d0,%d6\n\t"                /* vy */
    "swap   %d0\n\t"
    "swap   %d1\n\t"
    "swap   %d2\n\t"
    "move.w %d0,%d4\n\t"                /* least x */
    "move.w %d0,%d7\n\t"                /* greatest x */
    "cmp.w  %d1,%d4\n\t"
    "ble.s  3f\n\t"
    "move.w %d1,%d4\n"
    "3:\n\t"
    "cmp.w  %d1,%d7\n\t"
    "bge.s  4f\n\t"
    "move.w %d1,%d7\n"
    "4:\n\t"
    "cmp.w  %d2,%d4\n\t"
    "ble.s  5f\n\t"
    "move.w %d2,%d4\n"
    "5:\n\t"
    "cmp.w  %d2,%d7\n\t"
    "bge.s  6f\n\t"
    "move.w %d2,%d7\n"
    "6:\n\t"
    "sub.w  %d4,%d7\n\t"
    "cmp.w  #32767,%d7\n\t"
    "bhi    98f\n\t"                    /* wider than 32767 */
    "add.w  %d4,%d7\n\t"
    /* the columns: out if wholly left (x <= 16 lo) or right
     * (x >= 16 hi - 15) of the clip; the clamp, and its columns
     * biased, only if it reaches past either side */
    "move.w 16(%a0),%d0\n\t"            /* lo */
    "move.w %d0,%d3\n\t"
    "add.w  20(%a0),%d3\n\t"            /* hi */
    "cmp.w  %d3,%d0\n\t"
    "bge    99f\n\t"                    /* no column */
    "ext.l  %d4\n\t"
    "ext.l  %d7\n\t"
    "move.w %d0,%d1\n\t"
    "ext.l  %d1\n\t"
    "lsl.l  #4,%d1\n\t"                 /* 16 lo */
    "cmp.l  %d1,%d7\n\t"
    "ble    99f\n\t"                    /* left of the clip */
    "move.w %d3,%d2\n\t"
    "ext.l  %d2\n\t"
    "lsl.l  #4,%d2\n\t"                 /* 16 hi */
    "move.l %a4,4(%sp)\n\t"             /* the rows, unclamped */
    "cmp.l  %d2,%d7\n\t"
    "bgt.s  3f\n\t"
    "moveq  #15,%d7\n\t"
    "sub.l  %d7,%d1\n\t"
    "cmp.l  %d1,%d4\n\t"
    "bge.s  4f\n"
    "3:\n\t"
    "move.l %a5,4(%sp)\n\t"             /* the rows, clamped */
    "eor.w  #0x8000,%d0\n\t"
    "move.w %d0,44(%sp)\n\t"
    "eor.w  #0x8000,%d3\n\t"
    "move.w %d3,46(%sp)\n"
    "4:\n\t"
    "moveq  #15,%d7\n\t"
    "sub.l  %d7,%d2\n\t"
    "cmp.l  %d2,%d4\n\t"
    "bge    99f\n\t"                    /* right of the clip */
    /* which side the middle is: ux vy against uy vx */
    "move.w 12(%sp),%d2\n\t"
    "move.w 8(%sp),%d1\n\t"
    "move.w (%sp),%d0\n\t"
    "sub.w  %d0,%d2\n\t"                /* ux */
    "sub.w  %d0,%d1\n\t"                /* vx */
    "muls.w %d6,%d2\n\t"
    "muls.w %d5,%d1\n\t"
    "cmp.l  %d1,%d2\n\t"
    "beq    99f\n\t"                    /* no area */
    "slt    22(%sp)\n\t"                /* the middle right: long left */
    /* the first row, less the bias; the stride; the pattern's row
     * for it and its end */
    "move.w 20(%sp),%d3\n\t"
    "move.w %d3,%d0\n\t"
    "mulu.w 8(%a0),%d0\n\t"
    "move.l (%a0),%a2\n\t"
    "add.l  %d0,%a2\n\t"
    "suba.w #16384,%a2\n\t"
    "moveq  #0,%d0\n\t"
    "move.w 8(%a0),%d0\n\t"
    "move.l %d0,%a0\n\t"
    "lea    32(%a1),%a6\n\t"
    "move.l %a6,40(%sp)\n\t"            /* the pattern's end */
    "moveq  #3,%d0\n\t"
    "and.w  %d3,%d0\n\t"
    "lsl.w  #3,%d0\n\t"
    "move.l %a1,%a6\n\t"                /* opaque rows find their own row */
    "lea    " P_SYM(stdl_pp_lmasks) ",%a1\n\t"
    /* the long edge, the short one from the top if the middle's row
     * is below this one, its rows, then the short one from the
     * middle; d3 the row throughout */
    "move.l (%sp),%d6\n\t"
    "move.l 12(%sp),%d7\n\t"
    "tst.b  22(%sp)\n\t"
    "bne    70f\n\t"
    P_SETUP("%d5", "%a5", "32(%sp)")    /* long edge right */
    "cmp.w  16(%sp),%d3\n\t"
    "bge    1f\n\t"
    "move.l (%sp),%d6\n\t"
    "move.l 8(%sp),%d7\n\t"
    P_SETUP("%d4", "%a4", "28(%sp)")
    P_TRAP1
    "1:\n\t"
    "move.l 8(%sp),%d6\n\t"
    "move.l 12(%sp),%d7\n\t"
    P_SETUP("%d4", "%a4", "28(%sp)")
    "bra    75f\n"
    "70:\n\t"
    P_SETUP("%d4", "%a4", "28(%sp)")    /* long edge left */
    "cmp.w  16(%sp),%d3\n\t"
    "bge    1f\n\t"
    "move.l (%sp),%d6\n\t"
    "move.l 8(%sp),%d7\n\t"
    P_SETUP("%d5", "%a5", "32(%sp)")
    P_TRAP1
    "1:\n\t"
    "move.l 8(%sp),%d6\n\t"
    "move.l 12(%sp),%d7\n\t"
    P_SETUP("%d5", "%a5", "32(%sp)")
    "75:\n\t"
    "move.w 18(%sp),%d0\n\t"            /* to the last row */
    "sub.w  %d3,%d0\n\t"
    "move.w %d0,36(%sp)\n\t"
    "move.l 4(%sp),%a3\n\t"
    "jsr    (%a3)\n\t"
    "bra    99f\n"
    "50:\n\t"
    "move.l 116(%sp),%a3\n\t"           /* vertices */
    "move.l 120(%sp),%d0\n\t"           /* n */
    "move.l (%a3),%d1\n\t"              /* x:y */
    "move.w %d1,%d2\n\t"                /* top y */
    "move.w %d1,%d3\n\t"                /* bottom y */
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
    "move.w %d7,8(%sp)\n\t"             /* n x 4 */
    /* the pixel rows: ceil(top / 16) to ceil(bottom / 16), clipped;
     * out before any other work if none is left */
    "move.l 112(%sp),%a0\n\t"           /* the surface */
    "moveq  #15,%d7\n\t"
    "move.w %d2,%d0\n\t"
    "ext.l  %d0\n\t"
    "add.l  %d7,%d0\n\t"
    "asr.l  #4,%d0\n\t"
    "move.w %d0,16(%sp)\n\t"            /* both edges end at the top */
    "move.w %d0,18(%sp)\n\t"
    "move.w %d3,%d1\n\t"
    "ext.l  %d1\n\t"
    "add.l  %d7,%d1\n\t"
    "asr.l  #4,%d1\n\t"
    "move.w 18(%a0),%d7\n\t"            /* clip y */
    "cmp.w  %d7,%d0\n\t"
    "bge.s  6f\n\t"
    "move.w %d7,%d0\n"
    "6:\n\t"
    "add.w  22(%a0),%d7\n\t"
    "cmp.w  %d7,%d1\n\t"
    "ble.s  7f\n\t"
    "move.w %d7,%d1\n"
    "7:\n\t"
    "cmp.w  %d1,%d0\n\t"
    "bge    99f\n\t"                    /* no row inside the clip */
    "move.w %d0,20(%sp)\n\t"
    "move.w %d1,22(%sp)\n\t"
    "move.w %d3,%d0\n\t"
    "sub.w  %d2,%d0\n\t"
    "cmp.w  #32767,%d0\n\t"
    "bhi    98f\n\t"                    /* taller than 32767 */
    "move.w %d5,%d0\n\t"
    "sub.w  %d4,%d0\n\t"
    "cmp.w  #32767,%d0\n\t"
    "bhi    98f\n\t"                    /* wider than 32767 */
    /* the columns: out if wholly left (x <= 16 lo) or right
     * (x >= 16 hi - 15) of the clip; the clamp, and its columns
     * biased, only if it reaches past either side */
    "move.w 16(%a0),%d0\n\t"            /* lo */
    "move.w %d0,%d1\n\t"
    "add.w  20(%a0),%d1\n\t"            /* hi */
    "cmp.w  %d1,%d0\n\t"
    "bge    99f\n\t"                    /* no column */
    "ext.l  %d4\n\t"
    "ext.l  %d5\n\t"
    "move.w %d0,%d2\n\t"
    "ext.l  %d2\n\t"
    "lsl.l  #4,%d2\n\t"                 /* 16 lo */
    "cmp.l  %d2,%d5\n\t"
    "ble    99f\n\t"                    /* left of the clip */
    "move.w %d1,%d3\n\t"
    "ext.l  %d3\n\t"
    "lsl.l  #4,%d3\n\t"                 /* 16 hi */
    "move.l %a4,4(%sp)\n\t"             /* the rows, unclamped */
    "cmp.l  %d3,%d5\n\t"
    "bgt.s  8f\n\t"
    "moveq  #15,%d7\n\t"
    "sub.l  %d7,%d2\n\t"
    "cmp.l  %d2,%d4\n\t"
    "bge.s  9f\n"
    "8:\n\t"
    "move.l %a5,4(%sp)\n\t"             /* the rows, clamped */
    "eor.w  #0x8000,%d0\n\t"
    "move.w %d0,44(%sp)\n\t"
    "eor.w  #0x8000,%d1\n\t"
    "move.w %d1,46(%sp)\n"
    "9:\n\t"
    "moveq  #15,%d7\n\t"
    "sub.l  %d7,%d3\n\t"
    "cmp.l  %d3,%d4\n\t"
    "bge    99f\n\t"                    /* right of the clip */
    /* the winding, from the top vertex's two edges: ux * vy against
     * uy * vx, as orientation() */
    "move.w 8(%sp),%d7\n\t"
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
    "move.l %a3,(%sp)\n\t"
    /* the pattern's row for the first row (an entry of 8 or 12 bytes)
     * and its end; then the first row, less the bias, and the stride */
    "move.w 20(%sp),%d3\n\t"
    "moveq  #3,%d0\n\t"
    "and.w  %d3,%d0\n\t"
    "lsl.w  #2,%d0\n\t"                 /* row x 4 */
    "move.w %d0,%d1\n\t"
    "add.w  %d0,%d0\n\t"                /* x 8 */
    "move.w %a2,%d2\n\t"
    "lsl.w  #2,%d2\n\t"                 /* four entries */
    "cmp.w  #32,%d2\n\t"
    "bne.s  5f\n\t"
    "moveq  #0,%d0\n\t"                 /* opaque rows find their own row */
    "bra.s  4f\n"
    "5:\n\t"
    "add.w  %d1,%d0\n"                  /* x 12 */
    "4:\n\t"
    "lea    0(%a1,%d2.w),%a6\n\t"
    "move.l %a6,40(%sp)\n\t"            /* the pattern's end */
    "lea    0(%a1,%d0.w),%a6\n\t"       /* the first row's entry */
    "move.w %d3,%d0\n\t"
    "mulu.w 8(%a0),%d0\n\t"
    "move.l (%a0),%a2\n\t"
    "add.l  %d0,%a2\n\t"
    "suba.w #16384,%a2\n\t"
    "moveq  #0,%d0\n\t"
    "move.w 8(%a0),%d0\n\t"
    "move.l %d0,%a0\n\t"
    "lea    " P_SYM(stdl_pp_lmasks) ",%a1\n"
    "0:\n\t"
    "move.w 20(%sp),%d3\n\t"
    P_SIDE("12", "16", "add.w", "%d4", "%a4", "28(%sp)")
    P_SIDE("14", "18", "sub.w", "%d5", "%a5", "32(%sp)")
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
    "sub.w  %d3,%d0\n\t"
    "move.w %d0,36(%sp)\n\t"            /* the trapezoid's rows */
    "move.l 4(%sp),%a3\n\t"
    "jsr    (%a3)\n\t"
    "move.w 20(%sp),%d0\n\t"
    "cmp.w  22(%sp),%d0\n\t"
    "blt    0b\n"
    "99:\n\t"
    "moveq  #0,%d0\n\t"
    "lea    64(%sp),%sp\n\t"
    "movem.l (%sp)+,%d2-%d7/%a2-%a6\n\t"
    "rts\n"
    "98:\n\t"
    "move.w 26(%sp),%d0\n\t"
    "lea    64(%sp),%sp\n\t"
    "movem.l (%sp)+,%d2-%d7/%a2-%a6\n\t"
    "tst.w  %d0\n\t"
    "bne.s  1f\n\t"
    "moveq  #1,%d0\n\t"
    "rts\n"
    "1:\n\t"
    "jmp    " P_SYM(stdl_ppoly_wide) "\n");

static void (*const pp_rows[8])(void) = {
    stdl_pprows_o2, stdl_pprows_o4, stdl_pprows_t2, stdl_pprows_t4,
    stdl_pprows_o2c, stdl_pprows_o4c, stdl_pprows_t2c, stdl_pprows_t4c
};

/* a transparent pattern or an odd budget through the 68000 walk: a
 * copy of the pattern with the planes past the budget cleared and,
 * for a transparent one, each row's mask inverted into a long after
 * its words */
static int fast_68k(STDL_Surface *dst, const STDL_PointSub *pts, int n,
                    const STDL_Pattern *pat)
{
    uint32_t copy[12];
    const int tr = (pat->mask[0] | pat->mask[1] | pat->mask[2]
                    | pat->mask[3]) != 0;
    const int k = (tr ? 2 : 0) + (stdl_planes > 2 ? 1 : 0);
    const uint32_t keep01 = stdl_planes >= 2 ? 0xFFFFFFFFu : 0xFFFF0000u;
    const uint32_t keep23 = stdl_planes >= 4 ? 0xFFFFFFFFu
                          : stdl_planes == 3 ? 0xFFFF0000u : 0;
    uint32_t *o = copy;
    int r;

    for (r = 0; r < 4; r++) {
        const stdl_wlong *w = (const stdl_wlong *)pat->planes[r];
        const uint32_t m = (uint16_t)~pat->mask[r];

        *o++ = w[0] & keep01;
        *o++ = w[1] & keep23;
        if (tr) {
            *o++ = (m << 16) | m;
        }
    }
    return stdl_ppoly_walk(dst, pts, n, pp_rows[k], pp_rows[k + 4], copy,
                           tr ? 12 : 8);
}
#endif

/*
 * The C entry. On the machine the 68000's STDL_FillPolygonPattern
 * comes here for what it does not take itself - a transparent
 * pattern or an odd budget, which want a copy of the pattern, a
 * masked surface, anything odd - as stdl_ppoly_c, and for a polygon
 * its walk declines (too tall, too wide, a straight-through top) as
 * stdl_ppoly_wide. On the host it is the whole of it.
 */
#ifdef __m68k__
void stdl_ppoly_wide(STDL_Surface *dst, const STDL_PointSub *pts, int n,
                     const STDL_Pattern *pat);
void stdl_ppoly_c(STDL_Surface *dst, const STDL_PointSub *pts, int n,
                  const STDL_Pattern *pat);

void stdl_ppoly_c(STDL_Surface *dst, const STDL_PointSub *pts, int n,
                  const STDL_Pattern *pat)
{
    if (dst == NULL || pts == NULL || pat == NULL || n < 3
        || dst->pixels == NULL) {
        return;
    }
    if (dst->mask == NULL && n <= 8191 && fast_68k(dst, pts, n, pat) == 0) {
        return;
    }
    stdl_ppoly_wide(dst, pts, n, pat);
}

void stdl_ppoly_wide(STDL_Surface *dst, const STDL_PointSub *pts, int n,
                     const STDL_Pattern *pat)
#else
void STDL_FillPolygonPattern(STDL_Surface *dst, const STDL_PointSub *pts,
                             int n, const STDL_Pattern *pat)
#endif
{
    Walk w;
    int i, k, top = 0, bottom, minx, maxx;

    if (dst == NULL || pts == NULL || pat == NULL || n < 3
        || dst->pixels == NULL) {
        return;
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
    /* the rows whose sample points it covers, within the clip */
    w.y = CEIL16(pts[top].y);
    w.y2 = CEIL16(bottom);
    if (w.y < dst->clip.y) {
        w.y = dst->clip.y;
    }
    if (w.y2 > dst->clip.y + dst->clip.h) {
        w.y2 = dst->clip.y + dst->clip.h;
    }
    w.lo = dst->clip.x;
    w.hi = dst->clip.x + dst->clip.w;
    if (w.y >= w.y2 || w.lo >= w.hi) {
        return;
    }
    w.dir = orientation(pts, n, top);
    if (w.dir == 0) {
        return;                 /* no area: a line, or a point */
    }
    w.pts = pts;
    w.n = n;
    w.top = top;
    w.bottom = bottom;
    w.stride = dst->stride;
    w.row = dst->pixels + stdl_row_off(w.y, dst->stride);
    /* SET writes planes 2-3 whenever the budget has them, as
     * STDL_FillPolygon does; the planes past the budget are written
     * as zero */
    w.np = stdl_planes > 2 ? 4 : 2;
    for (i = 0; i < 4; i++) {
        for (k = 0; k < 4; k++) {
            w.pr[i].w[k] = k < stdl_planes ? pat->planes[i][k] : 0;
        }
        w.pr[i].keep = pat->mask[i];
    }
    /* sixteen-bit edges: no mask to keep, edges up to 32767 sub-pixel
     * rows, and a width whose deltas fit */
#ifndef __m68k__
    if (dst->mask == NULL && bottom - pts[top].y <= 32767
        && maxx - minx <= 32767) {
        const int clamp = minx < 16 * w.lo - 15 || maxx > 16 * w.hi;

        if (n == 3) {
            walk_tri(&w, clamp);
        } else {
            walk_fast(&w, clamp);
        }
        return;
    }
#endif
    walk_wide(&w, dst);
}
