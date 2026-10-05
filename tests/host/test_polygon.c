/*
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
/*
 * STDL_FillPolygon against a reference that knows nothing of edge
 * stepping: for each row, the polygon's exact cross-section in
 * rational arithmetic, and a pixel drawn when its row is in
 * [top, bottom) and its column in [left, right) of that section.
 *
 *  - Random convex polygons (hulls of random points), every
 *    operation, plane budgets 4, 2 and 3, random clip rectangles,
 *    masked surfaces (a drawn pixel turns opaque, or transparent for
 *    a SET in STDL_TRANSPARENT), and surface origins, which a
 *    polygon ignores like the other drawing primitives.
 *  - Tiling: a polygon split along a diagonal and both halves XORed
 *    equals the whole polygon XORed - no seam drawn twice, no gap.
 *  - The four corners of a rectangle fill what STDL_FillRect fills.
 *  - Coordinates at the ends of int16_t: columns far off either side
 *    must not wrap round into the picture, and edges taller than
 *    32767 rows must step right (the 68000's divs.w takes a 16-bit
 *    divisor, so they take the long way).
 *  - The fast path - a polygon inside the clip's columns, no taller
 *    than 256 rows, on a surface with no mask - steps its edges in
 *    16.16 fixed point. Polygons placed to take it, every operation
 *    and colour, and tall ones either side of 256 rows so both paths
 *    meet the same reference; and, natively, the arithmetic it rests
 *    on, for every edge up to 256 rows, with the first edge it gets
 *    wrong at 257 to show the check can fail.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "stdl_internal.h"

static int failures;

#define CHECK(cond, ...) do { \
    if (!(cond)) { \
        if (failures++ < 10) { \
            printf("FAIL %s:%d: ", __FILE__, __LINE__); \
            printf(__VA_ARGS__); \
            printf("\n"); \
        } \
    } \
} while (0)

static uint32_t rs = 0x9E3779B9u;
static int rnd(int n)
{
    rs ^= rs << 13;
    rs ^= rs >> 17;
    rs ^= rs << 5;
    return (int)(rs % (uint32_t)n);
}

enum { W = 112, H = 64 };

/* storage pixel x, y: the planes read directly */
static int getpix(const STDL_Surface *s, int x, int y)
{
    const uint16_t *g = (const uint16_t *)(s->pixels + y * s->stride
                                           + (x >> 4) * 8);
    const int bit = 15 - (x & 15);
    int p, v = 0;

    for (p = 0; p < 4; p++) {
        v |= ((g[p] >> bit) & 1) << p;
    }
    return v;
}

static int getmask(const STDL_Surface *s, int x, int y)
{
    const uint16_t *m = (const uint16_t *)(s->mask + y * s->maskstride);

    return (m[x >> 4] >> (15 - (x & 15))) & 1;
}

/* random pixels below 1 << budget, a plane word at a time: a pixel
 * at a time through STDL_PutPixel was most of PGCHK's time on a plain
 * ST */
static void randomise(STDL_Surface *s, int budget)
{
    int y, g, p;

    for (y = 0; y < s->h; y++) {
        uint16_t *w = (uint16_t *)(s->pixels + y * s->stride);

        for (g = 0; g < s->stride / 8; g++) {
            for (p = 0; p < 4; p++) {
                rs ^= rs << 13;
                rs ^= rs >> 17;
                rs ^= rs << 5;
                *w++ = p < budget ? (uint16_t)rs : 0;
            }
        }
    }
}

static long long ceil_div(long long num, long long den)
{
    long long q = num / den;

    if (q * den < num) {
        q++;
    }
    return q;
}

/* row y's columns [first, end): the exact cross-section [l, r] as
 * fractions, then x >= l and x < r as integers - ceil(l) and ceil(r).
 * Empty (first >= end) outside [top, bottom). */
static void section(const STDL_Point *p, int n, int y, int *first,
                    int *end)
{
    long long ln = 0, ld = 1, rn = 0, rd = 1;   /* l = ln/ld, r = rn/rd */
    int i, top = p[0].y, bot = p[0].y, any = 0;

    for (i = 1; i < n; i++) {
        if (p[i].y < top) top = p[i].y;
        if (p[i].y > bot) bot = p[i].y;
    }
    *first = 1;
    *end = 0;
    if (y < top || y >= bot) {
        return;
    }
    for (i = 0; i < n; i++) {
        const STDL_Point *a = &p[i], *b = &p[(i + 1) % n];
        long long num, den;
        int k;

        if (a->y == b->y) {
            if (a->y != y) continue;
            for (k = 0; k < 2; k++) {
                num = k ? b->x : a->x;
                den = 1;
                if (!any || num * ld < ln * den) { ln = num; ld = den; }
                if (!any || num * rd > rn * den) { rn = num; rd = den; }
                any = 1;
            }
            continue;
        }
        if (a->y > b->y) {
            const STDL_Point *t = a; a = b; b = t;
        }
        if (y < a->y || y > b->y) continue;
        den = b->y - a->y;
        num = (long long)a->x * den + (long long)(y - a->y) * (b->x - a->x);
        if (!any || num * ld < ln * den) { ln = num; ld = den; }
        if (!any || num * rd > rn * den) { rn = num; rd = den; }
        any = 1;
    }
    if (any) {
        *first = (int)ceil_div(ln, ld);
        *end = (int)ceil_div(rn, rd);
    }
}

static int apply(int pix, int col, int op)
{
    switch (op) {
    case STDL_DRAW_XOR:   return pix ^ col;
    case STDL_DRAW_OR:    return pix | col;
    case STDL_DRAW_CLEAR: return pix & ~col;
    default:              return col;
    }
}

/* convex hull of k random points around (cx, cy), Andrew's chain,
 * collinear points dropped; returns its size */
static int cross(const STDL_Point *o, const STDL_Point *a,
                 const STDL_Point *b)
{
    return (a->x - o->x) * (b->y - o->y) - (a->y - o->y) * (b->x - o->x);
}

static int cmp_pt(const void *a, const void *b)
{
    const STDL_Point *p = a, *q = b;

    return p->x != q->x ? p->x - q->x : p->y - q->y;
}

static int random_convex(STDL_Point *out)
{
    STDL_Point pts[10];
    int k = 3 + rnd(7), i, n = 0, lo;
    const int cx = rnd(W + 60) - 30, cy = rnd(H + 40) - 20;
    const int rx = 1 + rnd(70), ry = 1 + rnd(50);

    for (i = 0; i < k; i++) {
        pts[i].x = (int16_t)(cx + rnd(2 * rx + 1) - rx);
        pts[i].y = (int16_t)(cy + rnd(2 * ry + 1) - ry);
    }
    qsort(pts, (size_t)k, sizeof pts[0], cmp_pt);
    for (i = 0; i < k; i++) {
        while (n >= 2 && cross(&out[n - 2], &out[n - 1], &pts[i]) <= 0) n--;
        out[n++] = pts[i];
    }
    lo = n + 1;
    for (i = k - 2; i >= 0; i--) {
        while (n >= lo && cross(&out[n - 2], &out[n - 1], &pts[i]) <= 0) n--;
        out[n++] = pts[i];
    }
    return n - 1;
}

#ifndef POLY_ITERS
#define POLY_ITERS 3000
#endif

static void test_random(void)
{
    static const int budgets[3] = { 4, 2, 3 };
    int it;

    for (it = 0; it < POLY_ITERS; it++) {
        const int budget = budgets[it % 3];
        const int op = (it / 3) & 3, masked = (it / 12) & 1;
        STDL_Surface *s = STDL_CreateSurface(W, H);
        uint8_t before[W * H], mbefore[W * H];
        STDL_Point p[20];
        STDL_Rect clip;
        int n, x, y, col, ox = 0, oy = 0, bad = 0, trans;

        STDL_SetPlaneBudget(budget);
        if (masked) {
            STDL_CreateMask(s, 1);
        }
        if (it % 5 == 4) {
            ox = rnd(41) - 20;
            oy = rnd(31) - 15;
            STDL_SetSurfaceOrigin(s, ox, oy);
        }
        randomise(s, budget);
        if (masked) {
            for (y = 0; y < H; y++) {
                for (x = 0; x < W; x++) {
                    if (rnd(2)) {
                        ((uint16_t *)(s->mask + y * s->maskstride))[x >> 4]
                            |= (uint16_t)(0x8000u >> (x & 15));
                    }
                }
            }
        }
        clip.x = (int16_t)(rnd(W) - 10);
        clip.y = (int16_t)(rnd(H) - 10);
        clip.w = (uint16_t)rnd(W + 20);
        clip.h = (uint16_t)rnd(H + 20);
        if (it & 1) {
            clip.x = 0; clip.y = 0; clip.w = W; clip.h = H;
        }
        STDL_SetClipRect(s, &clip);
        clip = s->clip;
        for (y = 0; y < H; y++) {
            for (x = 0; x < W; x++) {
                before[y * W + x] = (uint8_t)getpix(s, x, y);
                mbefore[y * W + x] = masked ? (uint8_t)getmask(s, x, y) : 0;
            }
        }
        n = random_convex(p);
        if (n < 3) {
            STDL_FreeSurface(s);
            continue;
        }
        if (rnd(2)) {                   /* the other winding */
            for (x = 0; x < n / 2; x++) {
                STDL_Point t = p[x]; p[x] = p[n - 1 - x]; p[n - 1 - x] = t;
            }
        }
        col = rnd(17);                  /* 16: STDL_TRANSPARENT */
        STDL_FillPolygon(s, p, n, (uint8_t)col, op);
        trans = op == STDL_DRAW_SET && col == STDL_TRANSPARENT && masked;
        col &= (1 << budget) - 1;
        if (trans) {
            col = 0;
        }
        for (y = 0; y < H && bad < 3; y++) {
            int first, end;

            section(p, n, y, &first, &end);
            for (x = 0; x < W && bad < 3; x++) {
                const int in = x >= clip.x && x < clip.x + clip.w
                    && y >= clip.y && y < clip.y + clip.h
                    && x >= first && x < end
                    && (op == STDL_DRAW_SET || col != 0);
                const int want = in ? apply(before[y * W + x], col, op)
                                    : before[y * W + x];
                const int got = getpix(s, x, y);

                if (got != want) {
                    CHECK(0, "it %d op %d budget %d: (%d,%d) got %d want %d",
                          it, op, budget, x, y, got, want);
                    bad++;
                }
                if (masked) {
                    const int wm = in ? trans : mbefore[y * W + x];

                    if (getmask(s, x, y) != wm) {
                        CHECK(0, "it %d: mask (%d,%d) got %d want %d", it,
                              x, y, getmask(s, x, y), wm);
                        bad++;
                    }
                }
            }
        }
        STDL_FreeSurface(s);
    }
    STDL_SetPlaneBudget(4);
}

/* split along a diagonal, both halves XORed: the whole, XORed */
static void test_tiling(void)
{
    int it;

    for (it = 0; it < POLY_ITERS / 6; it++) {
        STDL_Surface *a = STDL_CreateSurface(W, H);
        STDL_Surface *b = STDL_CreateSurface(W, H);
        STDL_Point p[20], h1[20], h2[20];
        int n = random_convex(p), k, i, n1 = 0, n2 = 0;

        if (n < 4) {
            STDL_FreeSurface(a);
            STDL_FreeSurface(b);
            continue;
        }
        k = 2 + rnd(n - 3);             /* diagonal 0 - k */
        for (i = 0; i <= k; i++) h1[n1++] = p[i];
        for (i = k; i <= n; i++) h2[n2++] = p[i % n];
        STDL_FillPolygon(a, p, n, 5, STDL_DRAW_XOR);
        STDL_FillPolygon(b, h1, n1, 5, STDL_DRAW_XOR);
        STDL_FillPolygon(b, h2, n2, 5, STDL_DRAW_XOR);
        CHECK(memcmp(a->pixels, b->pixels, (size_t)a->stride * H) == 0,
              "split polygon %d (%d vertices, diagonal 0-%d) has a seam",
              it, n, k);
        STDL_FreeSurface(a);
        STDL_FreeSurface(b);
    }
}

static void test_rect(void)
{
    int it;

    for (it = 0; it < POLY_ITERS / 10; it++) {
        STDL_Surface *a = STDL_CreateSurface(W, H);
        STDL_Surface *b = STDL_CreateSurface(W, H);
        STDL_Rect r;
        STDL_Point p[4];

        r.x = (int16_t)(rnd(W + 20) - 10);
        r.y = (int16_t)(rnd(H + 20) - 10);
        r.w = (uint16_t)(1 + rnd(W));
        r.h = (uint16_t)(1 + rnd(H));
        p[0].x = r.x;                       p[0].y = r.y;
        p[1].x = (int16_t)(r.x + r.w);      p[1].y = r.y;
        p[2].x = (int16_t)(r.x + r.w);      p[2].y = (int16_t)(r.y + r.h);
        p[3].x = r.x;                       p[3].y = (int16_t)(r.y + r.h);
        STDL_FillRect(a, &r, 11);
        STDL_FillPolygon(b, p, 4, 11, STDL_DRAW_SET);
        CHECK(memcmp(a->pixels, b->pixels, (size_t)a->stride * H) == 0,
              "rectangle %d,%d %dx%d differs from FillRect", r.x, r.y,
              r.w, r.h);
        STDL_FreeSurface(a);
        STDL_FreeSurface(b);
    }
}

/* a convex polygon with every vertex in [x0, x1) x [y0, y1) */
static int convex_in(STDL_Point *out, int x0, int y0, int x1, int y1)
{
    STDL_Point pts[10];
    int k = 3 + rnd(7), i, n = 0, lo;

    for (i = 0; i < k; i++) {
        pts[i].x = (int16_t)(x0 + rnd(x1 - x0));
        pts[i].y = (int16_t)(y0 + rnd(y1 - y0));
    }
    qsort(pts, (size_t)k, sizeof pts[0], cmp_pt);
    for (i = 0; i < k; i++) {
        while (n >= 2 && cross(&out[n - 2], &out[n - 1], &pts[i]) <= 0) n--;
        out[n++] = pts[i];
    }
    lo = n + 1;
    for (i = k - 2; i >= 0; i--) {
        while (n >= lo && cross(&out[n - 2], &out[n - 1], &pts[i]) <= 0) n--;
        out[n++] = pts[i];
    }
    if (rnd(2)) {                       /* either winding */
        for (i = 0; i < (n - 1) / 2; i++) {
            STDL_Point t = out[i]; out[i] = out[n - 2 - i]; out[n - 2 - i] = t;
        }
    }
    return n - 1;
}

/* fill p on a w x h surface of random pixels, clipped to clip, and
 * check every pixel against the reference */
static void fill_and_check(int w, int h, const STDL_Point *p, int n,
                           int col, int op, int budget, STDL_Rect clip,
                           const char *what, int it)
{
    STDL_Surface *s = STDL_CreateSurface(w, h);
    uint8_t *before = malloc((size_t)w * h);
    int x, y, bad = 0;

    STDL_SetPlaneBudget(budget);
    randomise(s, budget);
    STDL_SetClipRect(s, &clip);
    clip = s->clip;
    for (y = 0; y < h; y++) {
        for (x = 0; x < w; x++) {
            before[y * w + x] = (uint8_t)getpix(s, x, y);
        }
    }
    STDL_FillPolygon(s, p, n, (uint8_t)col, op);
    col &= (1 << budget) - 1;
    for (y = 0; y < h && bad < 3; y++) {
        int first, end;

        section(p, n, y, &first, &end);
        for (x = 0; x < w && bad < 3; x++) {
            const int in = x >= clip.x && x < clip.x + clip.w
                && y >= clip.y && y < clip.y + clip.h
                && x >= first && x < end
                && (op == STDL_DRAW_SET || col != 0);
            const int want = in ? apply(before[y * w + x], col, op)
                                : before[y * w + x];

            if (getpix(s, x, y) != want) {
                CHECK(0, "%s %d op %d col %d budget %d: (%d,%d) got %d "
                      "want %d", what, it, op, col, budget, x, y,
                      getpix(s, x, y), want);
                bad++;
            }
        }
    }
    free(before);
    STDL_FreeSurface(s);
    STDL_SetPlaneBudget(4);
}

/* polygons inside the clip's columns, for the 16.16 path: every
 * operation, every colour, every budget, the clip around them */
static void test_inside(void)
{
    static const int budgets[3] = { 4, 2, 3 };
    int it;

    for (it = 0; it < POLY_ITERS; it++) {
        STDL_Point p[20];
        STDL_Rect clip;
        const int bx = rnd(W - 8), by = rnd(H - 8);
        const int bw = 1 + rnd(W - bx), bh = 1 + rnd(H - by);
        int n = convex_in(p, bx, by, bx + bw, by + bh), i, minx = W;
        int maxx = 0;

        if (n < 3) {
            continue;
        }
        for (i = 0; i < n; i++) {
            if (p[i].x < minx) minx = p[i].x;
            if (p[i].x > maxx) maxx = p[i].x;
        }
        /* columns that hold it (its right edge at the clip's end
         * included); rows that sometimes cut it */
        clip.x = (int16_t)rnd(minx + 1);
        clip.w = (uint16_t)(maxx - clip.x + rnd(W - maxx + 1));
        clip.y = (int16_t)(it & 1 ? 0 : rnd(H));
        clip.h = (uint16_t)(it & 1 ? H : rnd(H + 1));
        fill_and_check(W, H, p, n, rnd(17), (it / 3) & 3, budgets[it % 3],
                       clip, "inside", it);
    }
}

/* polygons 240 to 280 rows tall, wholly inside: those up to 256 take
 * the 16.16 path, the rest the tables, and both meet the reference */
static void test_tall(void)
{
    enum { TW = 48, TH = 300 };
    int it;

    for (it = 0; it < POLY_ITERS / 30; it++) {
        STDL_Point p[20];
        /* every other one with its top cut 120 to 220 rows down, so
         * an edge starts far below its vertex */
        STDL_Rect clip = { 0, (int16_t)(it & 1 ? 120 + rnd(101) : 0), TW,
                           TH };
        const int top = rnd(10), tall = 240 + rnd(41);
        int n = convex_in(p, 0, top, TW, top + tall + 1);

        if (n < 3) {
            continue;
        }
        fill_and_check(TW, TH, p, n, 1 + rnd(15), it & 3 ? STDL_DRAW_XOR
                       : STDL_DRAW_SET, 4, clip, "tall", it);
    }
    /* edges just past the limit that 16.16 would step a column wrong,
     * as the left side and as the right: these fail if the fast path
     * is ever let past 256 rows */
    {
        static const int16_t past[4][2] = {
            { -1, 257 }, { 18, 271 }, { -11, 274 }, { -7, 286 }
        };
        int k;

        for (k = 0; k < 8; k++) {
            const int dx = past[k >> 1][0], dy = past[k >> 1][1];
            STDL_Point p[3];
            STDL_Rect clip = { 0, 0, TW, TH };

            p[0].x = 24;                    p[0].y = 5;
            p[1].x = (int16_t)(24 + dx);    p[1].y = (int16_t)(5 + dy);
            p[2].x = (int16_t)(k & 1 ? 2 : 46);
            p[2].y = (int16_t)(5 + dy);
            fill_and_check(TW, TH, p, 3, 6, STDL_DRAW_SET, 4, clip,
                           "past 256", k);
        }
    }
}

#ifndef __m68k__
/* The claim the 16.16 path rests on: floor(65536 * dx / dy) stepped t
 * rows (t < dy) from 0xFFFF lands on ceil(t * dx / dy) for every dy up
 * to 256. Returns the first t that does not, or -1. */
static int stepped_wrong(int dx, int dy)
{
    const long long s = (long long)dx * 65536 >= 0
        ? (long long)dx * 65536 / dy
        : -((-(long long)dx * 65536 + dy - 1) / dy);
    int t;

    for (t = 0; t < dy; t++) {
        const long long v = 0xFFFF + t * s;
        const long long got = v >= 0 ? v / 65536 : -((-v + 65535) / 65536);

        if (got != ceil_div((long long)t * dx, dy)) {
            return t;
        }
    }
    return -1;
}

static void test_fixed_point(void)
{
    int dy, dx, ok = 1;

    for (dy = 1; dy <= 256 && ok; dy++) {
        for (dx = -400; dx <= 400; dx++) {
            if (stepped_wrong(dx, dy) >= 0) {
                CHECK(0, "16.16 off at dx %d dy %d row %d", dx, dy,
                      stepped_wrong(dx, dy));
                ok = 0;
                break;
            }
        }
    }
    CHECK(stepped_wrong(-258, 257) == 256,
          "the 257-row edge that 16.16 gets wrong no longer does");
}
#endif

/* every pixel of the surface against the reference */
static void check_against(const STDL_Point *p, int n, const char *what)
{
    STDL_Surface *s = STDL_CreateSurface(W, H);
    int x, y, bad = 0;

    STDL_FillPolygon(s, p, n, 7, STDL_DRAW_SET);
    for (y = 0; y < H && bad < 3; y++) {
        int first, end;

        section(p, n, y, &first, &end);
        for (x = 0; x < W && bad < 3; x++) {
            const int want = x >= first && x < end ? 7 : 0;

            if (getpix(s, x, y) != want) {
                CHECK(0, "%s: (%d,%d) got %d want %d", what, x, y,
                      getpix(s, x, y), want);
                bad++;
            }
        }
    }
    STDL_FreeSurface(s);
}

static void test_extremes(void)
{
    static const STDL_Point wrap_r[3] = {
        { -32768, 0 }, { -32000, 8 }, { -32768, 12 }
    };
    static const STDL_Point wrap_l[3] = {
        { 32767, 0 }, { 32767, 12 }, { 32000, 8 }
    };
    static const STDL_Point tall[3] = {
        { -15000, -20000 }, { 15000, 20000 }, { -15000, 20000 }
    };
    static const STDL_Point taller[4] = {
        { 20, -32767 }, { 60, -32767 }, { 90, 32767 }, { 10, 32767 }
    };
    static const STDL_Point wide[3] = {
        { -32768, 30 }, { 32767, 10 }, { 32767, 50 }
    };
    /* clipped 30000 rows down an edge 60000 wide: the product of the
     * two passes 32 bits */
    static const STDL_Point steep[3] = {
        { -30000, -30000 }, { 30000, 30000 }, { -30000, 30000 }
    };

    check_against(wrap_r, 3, "far left");
    check_against(wrap_l, 3, "far right");
    check_against(tall, 3, "40000 rows");
    check_against(taller, 4, "65534 rows");
    check_against(wide, 3, "65535 columns");
    check_against(steep, 3, "clipped 30000 rows down");
}

int main(void)
{
#ifndef __m68k__
    test_fixed_point();
#endif
    test_extremes();
    test_random();
    test_inside();
    test_tall();
    test_tiling();
    test_rect();
    if (failures) {
        printf("test_polygon: %d failures\n", failures);
        return 1;
    }
    printf("test_polygon: ok\n");
    return 0;
}
