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
 * four planes), the C reference first and then the 68000 path below:
 *
 *                        C      68000   transparent   FillPolygon SET
 *   2-row triangle     1.80ms   0.79ms                   0.70ms
 *   8-row triangle     2.97     1.15                     1.00
 *   22-row triangle    5.90     2.09      2.46           1.72
 *   20-row quad        6.84     2.36                     2.00
 *   160-row diamond   53.8     14.9      23.4           12.8
 *   22 rows, clipped   5.98     2.24
 *
 * On a Mega STE at 16MHz (--machine megaste) the 68000 path takes
 * 0.42, 0.61, 1.10, 1.33 and 8.6ms for the first five (FillPolygon
 * SET 0.38, 0.53, 0.91, 1.10, 7.6). In cycles at 8MHz, from a probe
 * timing the call alone: one that clips out entirely is about 1300
 * (the entry's checks, the register save, the vertex scan and the
 * clip), a 2-row triangle about 6200, and each further narrow row
 * about 460-530, plus 34 for each opaque middle group (two move.l
 * and the dbf) or 114 transparent. Of a triangle's set-up, each edge
 * costs about 860 - 620 for its arithmetic (a divs.w, a muls.w, two
 * mulu.w), the rest walking to it - and the winding 350. The first
 * 68000 version came in through a C wrapper and built its call
 * afresh: 0.88ms for the 2-row triangle; entering in 68000 and
 * reading each vertex once took 800 cycles off.
 *
 * A batch entry point was considered and not added: what it could
 * save is the per-call part - the argument push, the entry's checks,
 * the register save and restore and the frame - about 700 cycles a
 * polygon counted from the code (the whole of a call that clips out
 * measures 1300), against 6200 for the smallest and more for any
 * other.
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
    unsigned q0, r0, r, tq;
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
    /* N = D (B >> 4) + sm, and sm < 2D */
    sm = STDL_MulU16((unsigned)(bb & 15), dy) + tr;
    s->d = (uint32_t)dy << 4;
    s->c = bb >> 4;
    s->e = 0;
    if (sm > s->d) {
        s->c += 2;
        s->e = 2 * s->d - sm;
    } else if (sm != 0) {
        s->c += 1;
        s->e = s->d - sm;
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
 * the remainder e in the high word and the column in the low, biased
 * by 32768 so stepping never carries out of it. A row's step is one
 * add.l of
 *
 *   s = (-16r - [q < 0]) << 16 | (q & 0xFFFF)
 *
 * whose carry out says e did not go below zero: the low word's own
 * carry, which a negative q always makes, is cancelled by the -1 in
 * the high word. With no carry, f = D << 16 | 1 puts D back and the
 * column on by one. (A slope with r = 0 and q >= 0 would carry
 * never; it is written q - 1 and r = dy instead, which borrows every
 * row and comes to the same.) Valid while D = 16 dy fits sixteen
 * bits - edges up to 4095 sub-pixel rows, 255 pixels - and the
 * columns lie within +-32767, which they do for any polygon whose
 * width fits that.
 */
typedef struct {
    uint32_t x, s, f;
} PEdge;

static void pedge_start(PEdge *pe, const STDL_PointSub *a,
                        const STDL_PointSub *b, int y)
{
    EStart s;
    int32_t q;
    uint32_t r16;

    edge_setup(&s, a, b, y);
    q = s.q;
    r16 = s.r16;
    if (r16 == 0 && q >= 0) {
        q--;
        r16 = s.d;
    }
    pe->s = ((uint32_t)(uint16_t)(0u - r16 - (q < 0 ? 1u : 0u)) << 16)
            | (uint16_t)q;
    pe->f = (s.d << 16) | 1u;
    pe->x = (s.e << 16) | (uint16_t)(s.c + 32768);
}

static __inline__ void pedge_step(PEdge *pe)
{
    const uint32_t x = pe->x + pe->s;

    pe->x = x < pe->x ? x : x + pe->f;  /* carry: e stayed >= 0 */
}
#endif

/* the edges at the polygon's full width: column and remainder in
 * 32 bits each, for edges taller than 255 pixels or wider than the
 * fast path takes, and on masked surfaces */
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
/*
 * The fast path, in C: the 68000's twin. Both sides walked down from
 * the top vertex - skipping edges that are flat or end above the
 * row, stopping at the bottom vertex - a trapezoid at a time.
 */
static void walk_fast(Walk *w, int clamp)
{
    const STDL_PointSub *pts = w->pts;
    const unsigned lo = (unsigned)(w->lo + 32768);
    const unsigned hi = (unsigned)(w->hi + 32768);
    int il = w->top, ir = w->top, lend = CEIL16(pts[w->top].y);
    int rend = lend, y = w->y;
    uint8_t *row = w->row;
    PEdge l = { 0, 0, 0 }, r = { 0, 0, 0 };

    for (;;) {
        int stop;

        while (lend <= y) {
            const int b = next_vertex(il, w->dir, w->n);

            if (pts[il].y >= w->bottom) {
                return;
            }
            lend = CEIL16(pts[b].y);
            if (lend > y) {
                pedge_start(&l, &pts[il], &pts[b], y);
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
                pedge_start(&r, &pts[ir], &pts[b], y);
            }
            ir = b;
        }
        stop = lend < rend ? lend : rend;
        if (stop > w->y2) {
            stop = w->y2;
        }
        for (; y < stop; y++) {
            unsigned a = (uint16_t)l.x, e = (uint16_t)r.x;

            if (clamp) {
                if (a < lo) {
                    a = lo;
                }
                if (e > hi) {
                    e = hi;
                }
            }
            if (a < e) {
                span_c(row, (int)a - 32768, (int)e - 32768 - 1,
                       &w->pr[y & 3], w->np, NULL);
            }
            pedge_step(&l);
            pedge_step(&r);
            row += w->stride;
        }
        if (y >= w->y2) {
            return;
        }
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
 * each row, and d0-d3/a3 are theirs. The fixes D:1, the rows left,
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
#define P_REP(M) "move.w " M ",%d2\n\t" "swap " M "\n\t" \
                 "move.w %d2," M "\n\t"
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
#define PPROWS(NAME, ESIZE, P23, TR, CL, MID)                             \
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
    "move.w 0(%a1,%d0.w),%d0\n\t"       /* left mask */                   \
    "add.w  %d1,%d1\n\t"                                                  \
    "move.w 32(%a1,%d1.w),%d1\n\t"      /* right mask */                  \
    "lsr.w  #1,%d3\n\t"                                                   \
    "sub.w  %d2,%d3\n\t"                /* 8 x the groups after it */     \
    "bne.s  2f\n\t"                                                       \
    "and.w  %d1,%d0\n\t"                /* one group */                   \
    P_REP("%d0")                                                          \
    TR("and.l  -4(%a6),%d0\n\t")                                          \
    P_EDGE("%d0", "(%a3)", "%d6")                                         \
    P23(P_EDGE("%d0", "4(%a3)", "%d7"))                                   \
    "bra    9f\n"                                                         \
    "2:\n\t"                                                              \
    P_REP("%d0")                                                          \
    TR("and.l  -4(%a6),%d0\n\t")                                          \
    P_EDGE("%d0", "(%a3)", "%d6")                                         \
    P23(P_EDGE("%d0", "4(%a3)", "%d7"))                                   \
    "addq.l #8,%a3\n\t"                                                   \
    "lsr.w  #3,%d3\n\t"                                                   \
    "subq.w #2,%d3\n\t"                                                   \
    "bmi.s  4f\n\t"                                                       \
    TR("move.l -4(%a6),%d0\n\t")                                          \
    "3:\n\t"                                                              \
    MID                                                                   \
    "dbf    %d3,3b\n"                                                     \
    "4:\n\t"                                                              \
    P_REP("%d1")                                                          \
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
#define MID_T2 P_EDGE("%d0", "(%a3)", "%d6") "addq.l #8,%a3\n\t"
#define MID_T4 P_EDGE("%d0", "(%a3)", "%d6") P_EDGE("%d0", "4(%a3)", "%d7") \
               "addq.l #8,%a3\n\t"
PPROWS(stdl_pprows_o2, 8, P_OFF, P_OFF, P_OFF, MID_O2)
PPROWS(stdl_pprows_o4, 8, P_ON, P_OFF, P_OFF, MID_O4)
PPROWS(stdl_pprows_t2, 12, P_OFF, P_ON, P_OFF, MID_T2)
PPROWS(stdl_pprows_t4, 12, P_ON, P_ON, P_OFF, MID_T4)
PPROWS(stdl_pprows_o2c, 8, P_OFF, P_OFF, P_ON, MID_O2)
PPROWS(stdl_pprows_o4c, 8, P_ON, P_OFF, P_ON, MID_O4)
PPROWS(stdl_pprows_t2c, 12, P_OFF, P_ON, P_ON, MID_T2)
PPROWS(stdl_pprows_t4c, 12, P_ON, P_ON, P_ON, MID_T4)

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
    "sub.w  %d6,%d7\n\t"                                                  \
    "move.w %d7,%d6\n\t"                                                  \
    "ext.l  %d6\n\t"                    /* dx */                          \
    "divs.w %d1,%d6\n\t"                /* r:q */                         \
    "move.l %d6,%d7\n\t"                                                  \
    "swap   %d7\n\t"                                                      \
    "tst.w  %d7\n\t"                                                      \
    "bpl.s  21f\n\t"                                                      \
    "add.w  %d1,%d7\n\t"                /* floor: 0 <= r < dy */          \
    "subq.w #1,%d6\n"                                                     \
    "21:\n\t"                                                             \
    "move.w %d2," X "\n\t"                                                \
    "muls.w %d6," X "\n\t"                                                \
    "add.l  " X ",%d0\n\t"              /* B = a.x + t q */               \
    "move.w %d2," X "\n\t"                                                \
    "mulu.w %d7," X "\n\t"              /* t r */                         \
    "cmp.w  #16,%d2\n\t"                                                  \
    "bcs.s  22f\n\t"                                                      \
    "divu.w %d1," X "\n\t"              /* a clipped top: whole columns */\
    "moveq  #0,%d2\n\t"                                                   \
    "move.w " X ",%d2\n\t"                                                \
    "add.l  %d2,%d0\n\t"                                                  \
    "clr.w  " X "\n\t"                                                    \
    "swap   " X "\n"                    /* t r mod dy */                  \
    "22:\n\t"                                                             \
    "moveq  #15,%d2\n\t"                                                  \
    "and.w  %d0,%d2\n\t"                                                  \
    "mulu.w %d1,%d2\n\t"                                                  \
    "add.l  " X ",%d2\n\t"              /* sm = (B & 15) dy + t r */      \
    "asr.l  #4,%d0\n\t"                 /* the column, less 0-2 */        \
    "lsl.w  #4,%d1\n\t"                 /* D */                           \
    "moveq  #0," X "\n\t"                                                 \
    "move.w %d1," X "\n\t"                                                \
    "cmp.l  " X ",%d2\n\t"                                                \
    "bhi.s  23f\n\t"                                                      \
    "tst.l  %d2\n\t"                                                      \
    "beq.s  24f\n\t"                                                      \
    "addq.w #1,%d0\n\t"                 /* 0 < sm <= D */                 \
    "sub.w  %d2," X "\n\t"                                                \
    "bra.s  25f\n"                                                        \
    "23:\n\t"                                                             \
    "addq.w #2,%d0\n\t"                 /* D < sm < 2D */                 \
    "add.l  " X "," X "\n\t"                                              \
    "sub.l  %d2," X "\n\t"                                                \
    "bra.s  25f\n"                                                        \
    "24:\n\t"                                                             \
    "moveq  #0," X "\n"                                                   \
    "25:\n\t"                                                             \
    "swap   " X "\n\t"                                                    \
    "move.w %d0," X "\n\t"                                                \
    "eor.w  #0x8000," X "\n\t"          /* e:column + 32768 */            \
    "lsl.w  #4,%d7\n\t"                 /* 16 r */                        \
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
    "move.l %d1," F "\n\t"              /* the fix, D:1 */
/*
 * One side: while its edge ends at or above row d3, move to the next
 * vertex (ADV adds or subtracts the step, wrapping round), stopping
 * at the bottom vertex; an edge reaching below the row is set up.
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
    "cmp.w  24(%sp),%d6\n\t"                                              \
    "bge    99f\n\t"                    /* at the bottom vertex */        \
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

/*
 * The frame: 0 vertices, 4 row loop, 8 n x 4, 10 step x 4, 12/14 the
 * left's and right's vertex, 16/18 the rows their edges end on, 20
 * the row, 22 the last row (exclusive), 24 the bottom vertex's y, 26
 * where a declined polygon goes (0: back to the C, else on to
 * stdl_ppoly_wide), 28/32 the edges' fixes, 36 the trapezoid's rows,
 * 40 the pattern's end, 44/46 the clip's columns, biased, 48/52 the
 * row loops, unclamped and clamped, 56 the pattern, 60 its entry's
 * size. The arguments are 112 bytes up: dst, pts, n, then the
 * pattern for the public entry or the rest of stdl_ppoly_walk's.
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
    "move.l %a1,56(%sp)\n\t"
    "move.w #8,60(%sp)\n\t"
    "cmp.w  #2,%d1\n\t"
    "beq.s  1f\n\t"
    "move.l #" P_SYM(stdl_pprows_o4) ",48(%sp)\n\t"
    "move.l #" P_SYM(stdl_pprows_o4c) ",52(%sp)\n\t"
    "bra.s  2f\n"
    "1:\n\t"
    "move.l #" P_SYM(stdl_pprows_o2) ",48(%sp)\n\t"
    "move.l #" P_SYM(stdl_pprows_o2c) ",52(%sp)\n"
    "2:\n\t"
    "bra.s  50f\n\t"

    /* stdl_ppoly_walk(dst, pts, n, rows, rows_clamp, pat, esize): the
     * same from the C, which has made the pattern's copy; 0 when it
     * has drawn or found nothing to draw, 1 to decline */
    "\t.globl " P_SYM(stdl_ppoly_walk) "\n"
    P_SYM(stdl_ppoly_walk) ":\n\t"
    "movem.l %d2-%d7/%a2-%a6,-(%sp)\n\t"
    "lea    -64(%sp),%sp\n\t"
    "clr.w  26(%sp)\n\t"
    "move.l 124(%sp),48(%sp)\n\t"
    "move.l 128(%sp),52(%sp)\n\t"
    "move.l 132(%sp),56(%sp)\n\t"
    "move.w 138(%sp),60(%sp)\n"

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
    "move.w %d3,%d0\n\t"
    "sub.w  %d2,%d0\n\t"
    "cmp.w  #4095,%d0\n\t"
    "bhi    98f\n\t"                    /* taller than 4095 */
    "move.w %d5,%d0\n\t"
    "sub.w  %d4,%d0\n\t"
    "cmp.w  #32767,%d0\n\t"
    "bhi    98f\n\t"                    /* wider than 32767 */
    "move.w %d3,24(%sp)\n\t"            /* bottom */
    /* the pixel rows: ceil(top / 16) to ceil(bottom / 16), clipped */
    "move.l 112(%sp),%a1\n\t"           /* the surface */
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
    "move.w 18(%a1),%d7\n\t"            /* clip y */
    "cmp.w  %d7,%d0\n\t"
    "bge.s  6f\n\t"
    "move.w %d7,%d0\n"
    "6:\n\t"
    "add.w  22(%a1),%d7\n\t"
    "cmp.w  %d7,%d1\n\t"
    "ble.s  7f\n\t"
    "move.w %d7,%d1\n"
    "7:\n\t"
    "cmp.w  %d1,%d0\n\t"
    "bge    99f\n\t"                    /* no row inside the clip */
    "move.w %d0,20(%sp)\n\t"
    "move.w %d1,22(%sp)\n\t"
    /* the columns [lo, hi), biased; the clamp only for a polygon that
     * reaches past them: x < 16 lo - 15 or x > 16 hi */
    "move.w 16(%a1),%d0\n\t"            /* clip x */
    "move.w %d0,%d1\n\t"
    "add.w  20(%a1),%d1\n\t"
    "cmp.w  %d1,%d0\n\t"
    "bge    99f\n\t"                    /* no column */
    "move.w %d0,%d7\n\t"
    "eor.w  #0x8000,%d7\n\t"
    "move.w %d7,44(%sp)\n\t"
    "move.w %d1,%d7\n\t"
    "eor.w  #0x8000,%d7\n\t"
    "move.w %d7,46(%sp)\n\t"
    "move.l 48(%sp),4(%sp)\n\t"         /* the rows, unclamped */
    "ext.l  %d0\n\t"
    "lsl.l  #4,%d0\n\t"
    "moveq  #15,%d7\n\t"
    "sub.l  %d7,%d0\n\t"
    "move.w %d4,%d7\n\t"
    "ext.l  %d7\n\t"
    "cmp.l  %d0,%d7\n\t"
    "blt.s  8f\n\t"
    "ext.l  %d1\n\t"
    "lsl.l  #4,%d1\n\t"
    "move.w %d5,%d7\n\t"
    "ext.l  %d7\n\t"
    "cmp.l  %d1,%d7\n\t"
    "ble.s  9f\n"
    "8:\n\t"
    "move.l 52(%sp),4(%sp)\n"           /* the rows, clamped */
    "9:\n\t"
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
    /* the first row, less the bias; the stride; the pattern's row
     * for it and its end */
    "move.w 20(%sp),%d0\n\t"
    "mulu.w 8(%a1),%d0\n\t"
    "move.l (%a1),%a2\n\t"
    "add.l  %d0,%a2\n\t"
    "suba.w #16384,%a2\n\t"
    "moveq  #0,%d0\n\t"
    "move.w 8(%a1),%d0\n\t"
    "move.l %d0,%a0\n\t"
    "move.l 56(%sp),%a6\n\t"            /* the pattern */
    "move.w 60(%sp),%d1\n\t"            /* its entry's size */
    "move.w %d1,%d2\n\t"
    "lsl.w  #2,%d2\n\t"
    "ext.l  %d2\n\t"
    "add.l  %a6,%d2\n\t"
    "move.l %d2,40(%sp)\n\t"            /* its end */
    "moveq  #3,%d2\n\t"
    "and.w  20(%sp),%d2\n\t"
    "mulu.w %d1,%d2\n\t"
    "adda.l %d2,%a6\n\t"                /* the first row's entry */
    "lea    " P_SYM(stdl_pp_masks) ",%a1\n"
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
    /* sixteen-bit edges: no mask to keep, edges up to 4095 sub-pixel
     * rows, and a width whose deltas fit */
#ifndef __m68k__
    if (dst->mask == NULL && bottom - pts[top].y <= 4095
        && maxx - minx <= 32767) {
        walk_fast(&w, minx < 16 * w.lo - 15 || maxx > 16 * w.hi);
        return;
    }
#endif
    walk_wide(&w, dst);
}
