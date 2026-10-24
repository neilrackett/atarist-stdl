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
 * Measured on a plain ST (PRIMCOST, Hatari --machine st, 12.4
 * vertices with fractions, a dither between colours 9 and 4), in C:
 *
 *                       pattern   transparent   FillPolygon SET
 *   2-row triangle       1.80ms                     0.70ms
 *   8-row triangle       2.97                       1.00
 *   22-row triangle      5.90      5.94             1.72
 *   20-row quad          6.84                       2.00
 *   160-row diamond     53.8      62.8             12.7
 *   22 rows, clipped     5.98
 *
 * About 1.25ms of set-up and 0.2ms a row: the C is the reference and
 * the host's twin, not the path the machine should take.
 *
 * tests/hatari/ppchk.c runs the host suite on the machine: 36s for
 * the sampled build and 152s for -DPPCHK_FULL on a plain ST (13s of
 * host time under fast-forward), with the fill in C.
 *
 * Its own translation unit: a program that never fills a pattern
 * polygon does not carry it.
 */

#include "stdl_internal.h"

/* 0xFFFF >> n, then 0xFFFF << (15 - n) */
static const uint16_t pp_masks[32] = {
    0xFFFF, 0x7FFF, 0x3FFF, 0x1FFF, 0x0FFF, 0x07FF, 0x03FF, 0x01FF,
    0x00FF, 0x007F, 0x003F, 0x001F, 0x000F, 0x0007, 0x0003, 0x0001,
    0x8000, 0xC000, 0xE000, 0xF000, 0xF800, 0xFC00, 0xFE00, 0xFF00,
    0xFF80, 0xFFC0, 0xFFE0, 0xFFF0, 0xFFF8, 0xFFFC, 0xFFFE, 0xFFFF
};
#define lmask pp_masks
#define rmask (pp_masks + 16)

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

void STDL_FillPolygonPattern(STDL_Surface *dst, const STDL_PointSub *pts,
                             int n, const STDL_Pattern *pat)
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
    if (dst->mask == NULL && bottom - pts[top].y <= 4095
        && maxx - minx <= 32767) {
        walk_fast(&w, minx < 16 * w.lo - 15 || maxx > 16 * w.hi);
        return;
    }
    walk_wide(&w, dst);
}
