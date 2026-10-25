/*
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
/*
 * STDL_FillPolygonPattern and STDL_MakePattern against references
 * that know nothing of edge stepping.
 *
 *  - The coverage reference: for each row, the polygon's exact
 *    cross-section at the row's sample height in rational arithmetic,
 *    and a pixel drawn when its sample point (16x, 16y) is in
 *    [left, right) of it and the row in [top, bottom). On the host it
 *    is itself checked against a second, independent reference - a
 *    half-plane test per pixel with the top-left tie rule.
 *  - Identity: integer polygons shifted left by four, filled with a
 *    solid pattern, are byte for byte what STDL_FillPolygon SET
 *    draws - pixels and mask - over random polygons, clips, budgets,
 *    masked surfaces and origins. The strongest check of the rule.
 *  - Random sub-pixel polygons with random patterns (opaque,
 *    transparent, and raw struct contents that are not 4-periodic)
 *    against the reference and a per-pixel pattern model: every
 *    pixel of the surface, the mask with it, at every plane budget.
 *  - Tiling: convex polygons cut into fans, and perturbed sub-pixel
 *    grids cut into triangles, every pixel counted - drawn exactly
 *    once inside the outline, never outside.
 *  - Sub-pixel motion: a polygon moved 1/16 pixel at a time matches
 *    the reference at each step, and sixteen steps of a solid fill
 *    are the whole-pixel move.
 *  - Clipping to a clip rect smaller than the surface, coordinates
 *    at the ends of int16_t, edges past the fast path's 255 rows,
 *    zero-area and degenerate input, and writes outside a borrowed
 *    surface's rows caught by sentinels.
 *  - The pattern anchored to the surface: a polygon covering it
 *    reproduces STDL_MakePattern's documented colours at every
 *    pixel.
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

static uint32_t rs = 0x6A09E667u;
static uint32_t rnd32(void)
{
    rs ^= rs << 13;
    rs ^= rs >> 17;
    rs ^= rs << 5;
    return rs;
}

/* 0..n-1 without a divide by n: shifts and a compare, so the
 * checker costs the 68000 little */
static int rnd(int n)
{
    uint32_t m = 1, v;

    while (m < (uint32_t)n) {
        m = (m << 1) | 1;
    }
    do {
        v = rnd32() & m;
    } while (v >= (uint32_t)n);
    return (int)v;
}

#ifndef PP_ITERS
#define PP_ITERS 3000
#endif

enum { W = 112, H = 64 };

/* the whole surface as a pixel a byte, a word at a time: shifts
 * and tests, not a GetPixel each */
static void decode(const STDL_Surface *s, uint8_t *out)
{
    int y, x, b;

    for (y = 0; y < s->h; y++) {
        const uint16_t *g = (const uint16_t *)(s->pixels
            + stdl_row_off(y, s->stride));

        for (x = 0; x < s->w; x += 16, g += 4) {
            uint16_t v0 = g[0], v1 = g[1], v2 = g[2], v3 = g[3];

            for (b = 0; b < 16 && x + b < s->w; b++) {
                *out++ = (uint8_t)((v0 >> 15) | ((v1 >> 15) << 1)
                                   | ((v2 >> 15) << 2) | ((v3 >> 15) << 3));
                v0 = (uint16_t)(v0 << 1);
                v1 = (uint16_t)(v1 << 1);
                v2 = (uint16_t)(v2 << 1);
                v3 = (uint16_t)(v3 << 1);
            }
        }
    }
}

/* random pixels below 1 << budget, a plane word at a time */
static void randomise(STDL_Surface *s, int budget)
{
    int y, g, p;

    for (y = 0; y < s->h; y++) {
        uint16_t *w = (uint16_t *)(s->pixels + stdl_row_off(y, s->stride));

        for (g = 0; g < s->stride / 8; g++) {
            for (p = 0; p < 4; p++) {
                const uint16_t v = (uint16_t)rnd32();

                *w++ = p < budget ? v : 0;
            }
        }
    }
}

static void random_mask(STDL_Surface *s)
{
    int y, g;

    for (y = 0; y < s->h; y++) {
        uint16_t *m = (uint16_t *)(s->mask + stdl_row_off(y, s->maskstride));

        for (g = 0; g < s->maskstride / 2; g++) {
            m[g] = (uint16_t)rnd32();
        }
    }
}

/* ---- the coverage reference ---- */

static long long ceil_div(long long num, long long den)
{
    long long q = num / den;

    if (q * den < num) {
        q++;
    }
    return q;
}

/* ceil(num / den), den > 0, in 32 bits: the target's checking spends
 * a 64-bit divide only where the values need one */
static int32_t ceil_div32(int32_t num, int32_t den)
{
    int32_t q = num / den;

    if (q * den < num) {
        q++;
    }
    return q;
}

/* the first column whose sample point is at or right of the edge a-b
 * at height yy (a above b, a.y <= yy <= b.y): ceil(x / 16) for the
 * exact x = a.x + (yy - a.y) dx / dy */
static int edge_col(const STDL_PointSub *a, const STDL_PointSub *b,
                    int32_t yy)
{
    const int32_t dx = (int32_t)b->x - a->x, dy = (int32_t)b->y - a->y;
    const int32_t t = yy - a->y;

    if (dy < 32768 && t < 32768 && dx > -32768 && dx < 32768) {
        /* each product under 2^30, the sum under 2^31 */
        return (int)ceil_div32((int32_t)a->x * dy + t * dx, dy * 16);
    }
    return (int)ceil_div((long long)a->x * dy + (long long)t * dx,
                         (long long)dy * 16);
}

/* pixel row y's columns [*first, *end): the polygon's cross-section
 * at height 16y, [l, r], taken as the columns whose sample points
 * have 16x >= l and 16x < r - ceil(l / 16) and ceil(r / 16), each the
 * least and greatest of the edges' own ceilings, since ceil is
 * monotonic. Empty outside [top, bottom). */
static void section(const STDL_PointSub *p, int n, int y, int *first,
                    int *end)
{
    const int32_t yy = (int32_t)y * 16;
    int i, top = p[0].y, bot = p[0].y, lo = 0, hi = 0, any = 0;

    for (i = 1; i < n; i++) {
        if (p[i].y < top) top = p[i].y;
        if (p[i].y > bot) bot = p[i].y;
    }
    *first = 1;
    *end = 0;
    if (yy < top || yy >= bot) {
        return;
    }
    for (i = 0; i < n; i++) {
        const STDL_PointSub *a = &p[i], *b = &p[i + 1 < n ? i + 1 : 0];
        int c0, c1;

        if (a->y == b->y) {
            if (a->y != yy) continue;
            c0 = (a->x + 15) >> 4;      /* ceil(x / 16) */
            c1 = (b->x + 15) >> 4;
        } else {
            if (a->y > b->y) {
                const STDL_PointSub *t = a; a = b; b = t;
            }
            if (yy < a->y || yy > b->y) continue;
            c0 = c1 = edge_col(a, b, yy);
        }
        if (c1 < c0) {
            const int t = c0; c0 = c1; c1 = t;
        }
        if (!any || c0 < lo) lo = c0;
        if (!any || c1 > hi) hi = c1;
        any = 1;
    }
    if (any) {
        *first = lo;
        *end = hi;
    }
}

/* ---- polygons ---- */

static long long cross(const STDL_PointSub *o, const STDL_PointSub *a,
                       const STDL_PointSub *b)
{
    return (long long)(a->x - o->x) * (b->y - o->y)
         - (long long)(a->y - o->y) * (b->x - o->x);
}

static int cmp_pt(const void *a, const void *b)
{
    const STDL_PointSub *p = a, *q = b;

    return p->x != q->x ? p->x - q->x : p->y - q->y;
}

/* the convex hull of in[0..k-1] into out, either winding; its size */
static int hull(STDL_PointSub *in, int k, STDL_PointSub *out)
{
    int i, n = 0, lo;

    qsort(in, (size_t)k, sizeof in[0], cmp_pt);
    for (i = 0; i < k; i++) {
        while (n >= 2 && cross(&out[n - 2], &out[n - 1], &in[i]) <= 0) n--;
        out[n++] = in[i];
    }
    lo = n + 1;
    for (i = k - 2; i >= 0; i--) {
        while (n >= lo && cross(&out[n - 2], &out[n - 1], &in[i]) <= 0) n--;
        out[n++] = in[i];
    }
    n--;
    if (n >= 3 && rnd(2)) {
        for (i = 0; i < n / 2; i++) {
            STDL_PointSub t = out[i]; out[i] = out[n - 1 - i]; out[n - 1 - i] = t;
        }
    }
    if (n >= 3) {                       /* start anywhere */
        STDL_PointSub tmp[64];
        const int s = rnd(n);

        for (i = 0; i < n; i++) tmp[i] = out[(i + s) % n];
        memcpy(out, tmp, sizeof tmp[0] * (size_t)n);
    }
    return n;
}

/* a random convex polygon in 12.4 about (cx, cy) with radii rx, ry
 * (all in 12.4), 3 to 12 points before the hull */
static int random_sub(STDL_PointSub *out, int cx, int cy, int rx, int ry)
{
    STDL_PointSub pts[12];
    const int k = 3 + rnd(10);
    int i;

    for (i = 0; i < k; i++) {
        long x = cx + rnd(2 * rx + 1) - rx, y = cy + rnd(2 * ry + 1) - ry;

        if (x < -32768) x = -32768;
        if (x > 32767) x = 32767;
        if (y < -32768) y = -32768;
        if (y > 32767) y = 32767;
        pts[i].x = (int16_t)x;
        pts[i].y = (int16_t)y;
    }
    return hull(pts, k, out);
}

/* mostly around the surface, some far outside it, some tiny */
static int random_poly(STDL_PointSub *out)
{
    switch (rnd(8)) {
    case 0:     /* slivers and specks */
        return random_sub(out, rnd(W * 16 + 160) - 80, rnd(H * 16 + 160) - 80,
                          1 + rnd(40), 1 + rnd(40));
    case 1:     /* big, cut by every side */
        return random_sub(out, rnd(W * 16), rnd(H * 16), 400 + rnd(3000),
                          400 + rnd(3000));
    case 2:     /* anywhere in int16_t */
        return random_sub(out, rnd(65536) - 32768, rnd(65536) - 32768,
                          1 + rnd(40000), 1 + rnd(40000));
    default:
        return random_sub(out, rnd(W * 16 + 480) - 240, rnd(H * 16 + 320) - 160,
                          1 + rnd(1100), 1 + rnd(800));
    }
}

static void random_pattern(STDL_Pattern *pat)
{
    int r, p;

    switch (rnd(4)) {
    case 0:     /* raw contents, not even 4-periodic across a group */
        for (r = 0; r < 4; r++) {
            for (p = 0; p < 4; p++) {
                pat->planes[r][p] = (uint16_t)rnd32();
            }
            pat->mask[r] = rnd(3) ? 0 : (uint16_t)rnd32();
        }
        break;
    case 1:
        STDL_MakePattern(pat, (uint8_t)rnd(16), (uint8_t)rnd(16),
                         (uint16_t)rnd32(), 1);
        break;
    default:
        STDL_MakePattern(pat, (uint8_t)rnd(16), (uint8_t)rnd(16),
                         (uint16_t)rnd32(), 0);
        break;
    }
}

static void random_clip(STDL_Surface *s, int w, int h)
{
    STDL_Rect c;

    if (rnd(2)) {
        c.x = 0; c.y = 0; c.w = (uint16_t)w; c.h = (uint16_t)h;
    } else {
        c.x = (int16_t)(rnd(w) - 10);
        c.y = (int16_t)(rnd(h) - 10);
        c.w = (uint16_t)rnd(w + 20);
        c.h = (uint16_t)rnd(h + 20);
    }
    STDL_SetClipRect(s, &c);
}

/* pixel (x, y) of a planar block */
static int rawpix(const uint8_t *base, int stride, int x, int y)
{
    const uint16_t *g = (const uint16_t *)(base + stdl_row_off(y, (uint16_t)stride)
                                           + (x >> 4) * 8);
    const int bit = 15 - (x & 15);

    return ((g[0] >> bit) & 1) | (((g[1] >> bit) & 1) << 1)
         | (((g[2] >> bit) & 1) << 2) | (((g[3] >> bit) & 1) << 3);
}

/* the pixels of [a, b) in group g, as a plane word's bits */
static uint16_t cover_word(int a, int b, int g)
{
    const int lo = a > g * 16 ? a - g * 16 : 0;
    const int hi = b < g * 16 + 16 ? b - g * 16 : 16;

    if (lo >= hi) {
        return 0;
    }
    return (uint16_t)((0xFFFFu >> lo) & ~(0xFFFFu >> hi));
}

/*
 * Fill p with pat onto s (random pixels, maybe a random mask, the
 * clip as set) and check every pixel and mask bit against the
 * reference: the expected planes are built from a copy of the
 * surface a row at a time - the reference's columns as a bit mask a
 * group, less the pattern row's kept pixels, take the pattern row's
 * plane words (planes past the budget written as zero) and become
 * opaque in the mask - and compared whole. Word operations, so the
 * check costs the 68000 little more than the fill.
 */
static void fill_and_check(STDL_Surface *s, const STDL_PointSub *p, int n,
                           const STDL_Pattern *pat, int budget,
                           const char *what, int it)
{
    const int h = s->h;
    const size_t pb = stdl_row_off(h, s->stride);
    const size_t mb = s->mask ? stdl_row_off(h, s->maskstride) : 0;
    uint8_t *exp = malloc(pb), *mexp = mb ? malloc(mb) : NULL;
    const STDL_Rect clip = s->clip;
    int x, y, bad = 0;

    STDL_SetPlaneBudget(budget);
    memcpy(exp, s->pixels, pb);
    if (mb) {
        memcpy(mexp, s->mask, mb);
    }
    for (y = clip.y; y < clip.y + clip.h; y++) {
        uint16_t *er = (uint16_t *)(exp + stdl_row_off(y, s->stride));
        uint16_t *mr = mb ? (uint16_t *)(mexp + stdl_row_off(y, s->maskstride))
                          : NULL;
        const int r = y & 3;
        int first, end, g, k;

        section(p, n, y, &first, &end);
        if (first < clip.x) first = clip.x;
        if (end > clip.x + clip.w) end = clip.x + clip.w;
        if (first >= end) {
            continue;
        }
        for (g = first >> 4; g <= (end - 1) >> 4; g++) {
            const uint16_t m = (uint16_t)(cover_word(first, end, g)
                                          & ~pat->mask[r]);

            for (k = 0; k < 4; k++) {
                const uint16_t v = k < budget ? pat->planes[r][k] : 0;

                er[g * 4 + k] = (uint16_t)((er[g * 4 + k] & ~m) | (v & m));
            }
            if (mr) {
                mr[g] &= (uint16_t)~m;
            }
        }
    }
    STDL_FillPolygonPattern(s, p, n, pat);
    for (y = 0; y < h && bad < 3; y++) {
        const size_t o = stdl_row_off(y, s->stride);

        if (memcmp(exp + o, s->pixels + o, s->stride) != 0) {
            for (x = 0; x < s->w; x++) {
                const int want = rawpix(exp, s->stride, x, y);
                const int got = rawpix(s->pixels, s->stride, x, y);

                if (got != want) {
                    CHECK(0, "%s %d budget %d: (%d,%d) got %d want %d",
                          what, it, budget, x, y, got, want);
                    bad++;
                    break;
                }
            }
        }
        if (mb) {
            const size_t mo = stdl_row_off(y, s->maskstride);

            if (memcmp(mexp + mo, s->mask + mo, s->maskstride) != 0) {
                CHECK(0, "%s %d: mask row %d differs", what, it, y);
                bad++;
            }
        }
    }
    if (bad) {
        printf("  polygon:");
        for (x = 0; x < n; x++) printf(" (%d,%d)", p[x].x, p[x].y);
        printf("\n");
    }
    free(exp);
    free(mexp);
    STDL_SetPlaneBudget(4);
}

/* ---- the tests ---- */

#ifndef __m68k__
/* the section reference against a half-plane test per pixel: the
 * point (16x, 16y) inside every edge, or on one whose inward normal
 * points right, or straight down - the top-left rule */
static int inside_hp(const STDL_PointSub *p, int n, int x, int y,
                     int sgn)
{
    int i;

    for (i = 0; i < n; i++) {
        const STDL_PointSub *a = &p[i], *b = &p[i + 1 < n ? i + 1 : 0];
        const long long dx = b->x - a->x, dy = b->y - a->y;
        const long long c = sgn * (dx * (16LL * y - a->y)
                                   - dy * (16LL * x - a->x));

        if (c < 0) {
            return 0;
        }
        if (c == 0) {
            const long long nx = -sgn * dy, ny = sgn * dx;

            if (!(nx > 0 || (nx == 0 && ny > 0))) {
                return 0;
            }
        }
    }
    return 1;
}

static void test_reference(void)
{
    int it;

    for (it = 0; it < PP_ITERS / 3; it++) {
        STDL_PointSub p[24];
        long long area = 0;
        int n = random_poly(p), i, x, y, sgn, bad = 0;

        if (n < 3) {
            continue;
        }
        for (i = 0; i < n; i++) {
            area += cross(&p[0], &p[i], &p[i + 1 < n ? i + 1 : 0]);
        }
        sgn = area > 0 ? 1 : -1;
        for (y = -4; y < H + 4 && !bad; y++) {
            int first, end;

            section(p, n, y, &first, &end);
            for (x = -4; x < W + 4; x++) {
                const int a = x >= first && x < end;
                const int b = area != 0 && inside_hp(p, n, x, y, sgn);

                if (a != b) {
                    CHECK(0, "references disagree at (%d,%d): section %d "
                          "half-plane %d", x, y, a, b);
                    bad = 1;
                    break;
                }
            }
        }
    }
}
#endif

/* integer polygons shifted up to 12.4, solid: STDL_FillPolygon's
 * output byte for byte */
static int cmp_int(const void *a, const void *b)
{
    const STDL_Point *p = a, *q = b;

    return p->x != q->x ? p->x - q->x : p->y - q->y;
}

static int random_int(STDL_Point *out)
{
    STDL_Point pts[10];
    int k = 3 + rnd(7), i, n = 0, lo;
    const int cx = rnd(W + 60) - 30, cy = rnd(H + 40) - 20;
    const int rx = 1 + rnd(70), ry = 1 + rnd(50);

    for (i = 0; i < k; i++) {
        pts[i].x = (int16_t)(cx + rnd(2 * rx + 1) - rx);
        pts[i].y = (int16_t)(cy + rnd(2 * ry + 1) - ry);
    }
    qsort(pts, (size_t)k, sizeof pts[0], cmp_int);
    for (i = 0; i < k; i++) {
        while (n >= 2
               && (out[n - 1].x - out[n - 2].x) * (pts[i].y - out[n - 2].y)
                  - (out[n - 1].y - out[n - 2].y) * (pts[i].x - out[n - 2].x)
                  <= 0) n--;
        out[n++] = pts[i];
    }
    lo = n + 1;
    for (i = k - 2; i >= 0; i--) {
        while (n >= lo
               && (out[n - 1].x - out[n - 2].x) * (pts[i].y - out[n - 2].y)
                  - (out[n - 1].y - out[n - 2].y) * (pts[i].x - out[n - 2].x)
                  <= 0) n--;
        out[n++] = pts[i];
    }
    n--;
    if (n >= 3 && rnd(2)) {
        for (i = 0; i < n / 2; i++) {
            STDL_Point t = out[i]; out[i] = out[n - 1 - i]; out[n - 1 - i] = t;
        }
    }
    return n;
}

static void test_identity(void)
{
    static const int budgets[4] = { 4, 2, 3, 1 };
    int it;

    for (it = 0; it < PP_ITERS; it++) {
        const int budget = budgets[it & 3], masked = (it >> 2) & 1;
        STDL_Surface *a = STDL_CreateSurface(W, H);
        STDL_Surface *b = STDL_CreateSurface(W, H);
        STDL_Point ip[20];
        STDL_PointSub sp[20];
        STDL_Pattern pat;
        const int col = rnd(16);
        int n = random_int(ip), i;
        size_t pbytes = (size_t)stdl_row_off(H, a->stride);

        STDL_SetPlaneBudget(budget);
        if (masked) {
            STDL_CreateMask(a, 1);
            STDL_CreateMask(b, 1);
            random_mask(a);
            memcpy(b->mask, a->mask, (size_t)stdl_row_off(H, a->maskstride));
        }
        if (it % 5 == 4) {
            const int ox = rnd(41) - 20, oy = rnd(31) - 15;

            STDL_SetSurfaceOrigin(a, ox, oy);
            STDL_SetSurfaceOrigin(b, ox, oy);
        }
        randomise(a, budget);
        memcpy(b->pixels, a->pixels, pbytes);
        random_clip(a, W, H);
        STDL_SetClipRect(b, &a->clip);
        for (i = 0; i < n; i++) {
            sp[i].x = (int16_t)(ip[i].x * 16);
            sp[i].y = (int16_t)(ip[i].y * 16);
        }
        STDL_MakePattern(&pat, (uint8_t)col, (uint8_t)col, (uint16_t)rnd32(),
                         0);
        if (n >= 3) {
            STDL_FillPolygon(a, ip, n, (uint8_t)col, STDL_DRAW_SET);
            STDL_FillPolygonPattern(b, sp, n, &pat);
        }
        CHECK(memcmp(a->pixels, b->pixels, pbytes) == 0,
              "identity %d (budget %d, masked %d, %d vertices) differs from "
              "STDL_FillPolygon", it, budget, masked, n);
        if (masked) {
            CHECK(memcmp(a->mask, b->mask,
                         (size_t)stdl_row_off(H, a->maskstride)) == 0,
                  "identity %d: mask differs from STDL_FillPolygon", it);
        }
        STDL_FreeSurface(a);
        STDL_FreeSurface(b);
    }
    STDL_SetPlaneBudget(4);
}

/* random sub-pixel polygons and patterns against the reference */
static void test_random(void)
{
    static const int budgets[4] = { 4, 2, 3, 1 };
    int it;

    for (it = 0; it < PP_ITERS; it++) {
        const int budget = budgets[it & 3];
        STDL_Surface *s = STDL_CreateSurface(W, H);
        STDL_PointSub p[24];
        STDL_Pattern pat;
        int n = random_poly(p);

        STDL_SetPlaneBudget(budget);
        if ((it >> 2) % 3 == 1) {
            STDL_CreateMask(s, 1);
            random_mask(s);
        }
        if (it % 7 == 3) {
            STDL_SetSurfaceOrigin(s, rnd(41) - 20, rnd(31) - 15);
        }
        randomise(s, budget);
        random_clip(s, W, H);
        random_pattern(&pat);
        if (n >= 3) {
            fill_and_check(s, p, n, &pat, budget, "random", it);
        }
        STDL_FreeSurface(s);
    }
}

/* polygons inside the clip's columns, often: the fast path's
 * no-clamp loop; and tall ones, either side of 4095 sub-pixel rows
 * (the fast path's limit until its remainders were kept over 16) and
 * of 32767, its limit now */
static void test_inside_tall(void)
{
    int it;

    for (it = 0; it < PP_ITERS / 3; it++) {
        STDL_Surface *s = STDL_CreateSurface(W, H);
        STDL_PointSub p[24];
        STDL_Pattern pat;
        STDL_Rect c;
        int n, i, minx = 32767, maxx = -32768;

        n = random_sub(p, rnd(W * 16), rnd(H * 16), 1 + rnd(W * 8),
                       1 + rnd(H * 8));
        if (n < 3) {
            STDL_FreeSurface(s);
            continue;
        }
        for (i = 0; i < n; i++) {
            if (p[i].x < minx) minx = p[i].x;
            if (p[i].x > maxx) maxx = p[i].x;
        }
        randomise(s, 4);
        c.x = 0; c.y = 0; c.w = W; c.h = H;
        if (minx >= 0 && maxx <= W * 16) {
            c.x = (int16_t)rnd((minx + 15) / 16 + 1);
            c.w = (uint16_t)((maxx + 15) / 16 - c.x
                             + rnd(W - (maxx + 15) / 16 + 1));
        }
        if (it & 1) {
            c.y = (int16_t)rnd(H);
            c.h = (uint16_t)rnd(H + 1);
        }
        STDL_SetClipRect(s, &c);
        random_pattern(&pat);
        fill_and_check(s, p, n, &pat, 4, "inside", it);
        STDL_FreeSurface(s);
    }
    /* 240 to 290 pixel rows on a tall surface, every other one with
     * its top clipped 100 to 200 rows down: under and over 4095; and,
     * every third one, 1000 to 2100 rows about the surface's middle:
     * under and over 32767 */
    for (it = 0; it < (PP_ITERS >= 60 ? PP_ITERS / 20 : 6); it++) {
        enum { TW = 48, TH = 320 };
        STDL_Surface *s = STDL_CreateSurface(TW, TH);
        STDL_PointSub p[24];
        STDL_Pattern pat;
        STDL_Rect c = { 0, 0, TW, TH };
        const int huge = it % 3 == 2;
        const int tall = huge ? 1000 * 16 + rnd(1100 * 16)
                              : 240 * 16 + rnd(50 * 16);
        int n = random_sub(p, rnd(TW * 16),
                           huge ? TH * 8 : 20 * 16 + tall / 2, TW * 8,
                           tall / 2);

        if (it & 1) {
            c.y = (int16_t)(100 + rnd(101));
        }
        if (n < 3) {
            STDL_FreeSurface(s);
            continue;
        }
        randomise(s, 4);
        STDL_SetClipRect(s, &c);
        random_pattern(&pat);
        fill_and_check(s, p, n, &pat, 4, "tall", it);
        STDL_FreeSurface(s);
    }
    /* exactly at the fast path's limit, and one past it: now and as
     * it was; the tallest begin 1000 rows above the surface */
    {
        static const int32_t hgt[8] = { 4094, 4095, 4096, 4097,
                                        32766, 32767, 32768, 32769 };
        int k;

        for (k = 0; k < 16; k++) {
            enum { TW = 48, TH = 300 };
            STDL_Surface *s = STDL_CreateSurface(TW, TH);
            STDL_PointSub p[3];
            STDL_Pattern pat;
            const int32_t y0 = k >= 8 ? -16000 : 37;
            const int32_t h = hgt[k >> 1];

            p[0].x = 24 * 16 + 3;            p[0].y = (int16_t)y0;
            p[1].x = (int16_t)(k & 1 ? 2 * 16 + 5 : 45 * 16 + 11);
            p[1].y = (int16_t)(y0 + h);
            p[2].x = (int16_t)(k & 1 ? 40 * 16 : 7 * 16);
            p[2].y = (int16_t)(y0 + h - 1 - 33 * ((k >> 1) & 3));
            randomise(s, 4);
            STDL_MakePattern(&pat, 3, 12, 0x5A5A, k & 2);
            fill_and_check(s, p, 3, &pat, 4, "limit", k);
            STDL_FreeSurface(s);
        }
    }
}

/* every pixel counted: drawn exactly once inside the outline */
typedef struct {
    uint8_t cnt[W * H];
} Counts;

/* colour 1 onto a cleared surface: plane 0 alone says what was
 * drawn, read a word at a time to keep the 68000's checking short */
static void count_into(Counts *c, const STDL_PointSub *p, int n)
{
    STDL_Surface *s = STDL_CreateSurface(W, H);
    STDL_Pattern pat;
    int x, y;

    STDL_MakePattern(&pat, 1, 1, 0, 0);
    STDL_FillPolygonPattern(s, p, n, &pat);
    for (y = 0; y < H; y++) {
        const uint16_t *g = (const uint16_t *)(s->pixels
            + stdl_row_off(y, s->stride));
        uint8_t *cr = c->cnt + y * W;

        for (x = 0; x < W; x += 16, g += 4) {
            uint16_t v = g[0];
            int b;

            for (b = 0; v != 0; b++, v = (uint16_t)(v << 1)) {
                if (v & 0x8000u) {
                    cr[x + b]++;
                }
            }
        }
    }
    STDL_FreeSurface(s);
}

static void test_tiling(void)
{
    static Counts c;
    int it;

    /* convex polygons as fans from one vertex */
    for (it = 0; it < PP_ITERS / 10; it++) {
        STDL_PointSub p[24], tri[3];
        int n = random_sub(p, rnd(W * 16), rnd(H * 16), 16 + rnd(W * 10),
                           16 + rnd(H * 10)), k, x, y, bad = 0;

        if (n < 4) {
            continue;
        }
        memset(&c, 0, sizeof c);
        for (k = 1; k + 1 < n; k++) {
            tri[0] = p[0];
            tri[1] = p[k];
            tri[2] = p[k + 1];
            count_into(&c, tri, 3);
        }
        for (y = 0; y < H && !bad; y++) {
            int first, end;

            section(p, n, y, &first, &end);
            for (x = 0; x < W; x++) {
                const int want = x >= first && x < end;

                if (c.cnt[y * W + x] != want) {
                    CHECK(0, "fan %d (%d vertices): (%d,%d) drawn %d times, "
                          "want %d", it, n, x, y, c.cnt[y * W + x], want);
                    bad = 1;
                    break;
                }
            }
        }
    }
    /* perturbed sub-pixel grids over a rectangle, each cell cut into
     * two triangles along a random diagonal, either winding */
    for (it = 0; it < PP_ITERS / 30 + 2; it++) {
        enum { GX = 6, GY = 4 };
        STDL_PointSub g[GY + 1][GX + 1];
        const int x0 = rnd(400) - 200, y0 = rnd(300) - 150;
        const int cw = (W * 16 + 400 - x0) / GX, ch = (H * 16 + 300 - y0) / GY;
        int i, j, x, y, bad = 0;

        memset(&c, 0, sizeof c);
        for (j = 0; j <= GY; j++) {
            for (i = 0; i <= GX; i++) {
                g[j][i].x = (int16_t)(x0 + i * cw
                    + (i > 0 && i < GX ? rnd(cw / 2) - cw / 4 : 0));
                g[j][i].y = (int16_t)(y0 + j * ch
                    + (j > 0 && j < GY ? rnd(ch / 2) - ch / 4 : 0));
            }
        }
        for (j = 0; j < GY; j++) {
            for (i = 0; i < GX; i++) {
                STDL_PointSub t[3];
                const int diag = rnd(2), rev = rnd(2);

                t[0] = g[j][i];
                t[1] = diag ? g[j][i + 1] : g[j + 1][i + 1];
                t[2] = g[j + 1][i];
                if (rev) { STDL_PointSub u = t[1]; t[1] = t[2]; t[2] = u; }
                count_into(&c, t, 3);
                t[0] = g[j][i + 1];
                t[1] = g[j + 1][i + 1];
                t[2] = diag ? g[j + 1][i] : g[j][i];
                if (rev) { STDL_PointSub u = t[1]; t[1] = t[2]; t[2] = u; }
                count_into(&c, t, 3);
            }
        }
        for (y = 0; y < H && !bad; y++) {
            for (x = 0; x < W; x++) {
                const int want = 16 * x >= x0 && 16 * x < x0 + GX * cw
                    && 16 * y >= y0 && 16 * y < y0 + GY * ch;

                if (c.cnt[y * W + x] != want) {
                    CHECK(0, "grid %d: (%d,%d) drawn %d times, want %d", it,
                          x, y, c.cnt[y * W + x], want);
                    bad = 1;
                    break;
                }
            }
        }
    }
}

/* 1/16 pixel at a time, across and down */
static void test_subpixel(void)
{
    int it;

    for (it = 0; it < PP_ITERS / 100 + 2; it++) {
        STDL_PointSub p[24], q[24];
        STDL_Pattern pat, solid;
        int n = random_sub(p, W * 8, H * 8, 40 + rnd(500), 40 + rnd(300));
        int k, i, axis = it & 1;
        STDL_Surface *s0 = STDL_CreateSurface(W, H);
        STDL_Surface *s1 = STDL_CreateSurface(W, H);

        if (n < 3) {
            STDL_FreeSurface(s0);
            STDL_FreeSurface(s1);
            continue;
        }
        random_pattern(&pat);
        for (k = 0; k <= 16; k++) {
            STDL_Surface *s = STDL_CreateSurface(W, H);

            for (i = 0; i < n; i++) {
                q[i].x = (int16_t)(p[i].x + (axis ? 0 : k));
                q[i].y = (int16_t)(p[i].y + (axis ? k : 0));
            }
            randomise(s, 4);
            fill_and_check(s, q, n, &pat, 4, "sub-pixel", it * 17 + k);
            STDL_FreeSurface(s);
        }
        /* sixteen steps of a solid fill are one whole pixel: the same
         * pixels moved, rounded vertices included */
        STDL_MakePattern(&solid, 9, 9, 0, 0);
        for (i = 0; i < n; i++) {
            q[i].x = (int16_t)(p[i].x + (axis ? 0 : 16));
            q[i].y = (int16_t)(p[i].y + (axis ? 16 : 0));
        }
        STDL_FillPolygonPattern(s0, p, n, &solid);
        STDL_FillPolygonPattern(s1, q, n, &solid);
        {
            static uint8_t d0[W * H], d1[W * H];
            int x, y, bad = 0;

            decode(s0, d0);
            decode(s1, d1);
            for (y = 0; y < H && !bad; y++) {
                for (x = 0; x < W; x++) {
                    const int sx = axis ? x : x - 1, sy = axis ? y - 1 : y;
                    const int want = sx >= 0 && sy >= 0 ? d0[sy * W + sx]
                                                        : d1[y * W + x];

                    if (d1[y * W + x] != want) {
                        CHECK(0, "whole-pixel move %d: (%d,%d)", it, x, y);
                        bad = 1;
                        break;
                    }
                }
            }
        }
        STDL_FreeSurface(s0);
        STDL_FreeSurface(s1);
    }
}

/* clip rects inside the surface, polygons crossing them on every
 * side and far beyond */
static void test_clip(void)
{
    int it;

    for (it = 0; it < PP_ITERS / 10; it++) {
        STDL_Surface *s = STDL_CreateSurface(W, H);
        STDL_PointSub p[24];
        STDL_Pattern pat;
        STDL_Rect c;
        int n = random_sub(p, rnd(W * 16), rnd(H * 16), 200 + rnd(2000),
                           200 + rnd(1500));

        c.x = (int16_t)(1 + rnd(W / 2));
        c.y = (int16_t)(1 + rnd(H / 2));
        c.w = (uint16_t)(1 + rnd(W - c.x - 1));
        c.h = (uint16_t)(1 + rnd(H - c.y - 1));
        randomise(s, 4);
        STDL_SetClipRect(s, &c);
        random_pattern(&pat);
        if (n >= 3) {
            fill_and_check(s, p, n, &pat, 4, "clip", it);
        }
        STDL_FreeSurface(s);
    }
}

static void check_one(const STDL_PointSub *p, int n, const char *what)
{
    STDL_Surface *s = STDL_CreateSurface(W, H);
    STDL_Pattern pat;

    randomise(s, 4);
    STDL_MakePattern(&pat, 5, 10, 0xA5C3, 0);
    fill_and_check(s, p, n, &pat, 4, what, 0);
    STDL_FreeSurface(s);
    s = STDL_CreateSurface(W, H);
    STDL_CreateMask(s, 1);
    random_mask(s);
    randomise(s, 4);
    STDL_MakePattern(&pat, 6, 0, 0x1248, 1);
    fill_and_check(s, p, n, &pat, 4, what, 1);
    STDL_FreeSurface(s);
}

static void test_extremes(void)
{
    static const STDL_PointSub wrap_r[3] = {
        { -32768, 0 }, { -32000, 128 }, { -32768, 192 }
    };
    static const STDL_PointSub wrap_l[3] = {
        { 32767, 0 }, { 32767, 192 }, { 32000, 128 }
    };
    static const STDL_PointSub huge[3] = {
        { -32768, -32768 }, { 32767, 32767 }, { -32768, 32767 }
    };
    static const STDL_PointSub huge2[4] = {
        { -32768, -32768 }, { 32767, -32768 }, { 32767, 32767 },
        { -32768, 32767 }
    };
    static const STDL_PointSub taller[4] = {
        { 320, -32767 }, { 960, -32767 }, { 1440, 32767 }, { 160, 32767 }
    };
    static const STDL_PointSub wide[3] = {
        { -32768, 480 }, { 32767, 160 }, { 32767, 800 }
    };
    static const STDL_PointSub steep[3] = {
        { -30000, -30000 }, { 30000, 30000 }, { -30000, 30000 }
    };
    static const STDL_PointSub sliver[3] = {
        { 100, 100 }, { 1700, 117 }, { 101, 118 }
    };
    static const STDL_PointSub flat[4] = {     /* no row samples it */
        { 0, 33 }, { 1700, 33 }, { 1700, 47 }, { 0, 47 }
    };
    static const STDL_PointSub line[4] = {     /* zero area */
        { 10, 10 }, { 500, 400 }, { 990, 790 }, { 500, 400 }
    };
    static const STDL_PointSub point[3] = { { 77, 77 }, { 77, 77 }, { 77, 77 } };
    static const STDL_PointSub dup[5] = {      /* repeated vertices */
        { 100, 100 }, { 100, 100 }, { 900, 300 }, { 900, 300 }, { 50, 700 }
    };
    STDL_PointSub many[64];
    int i;

    check_one(wrap_r, 3, "far left");
    check_one(wrap_l, 3, "far right");
    check_one(huge, 3, "int16_t corners");
    check_one(huge2, 4, "the whole plane");
    check_one(taller, 4, "65534 rows");
    check_one(wide, 3, "65535 columns");
    check_one(steep, 3, "clipped 30000 rows down");
    check_one(sliver, 3, "sliver");
    check_one(flat, 4, "between rows");
    check_one(line, 4, "zero area");
    check_one(point, 3, "a point");
    check_one(dup, 5, "repeated vertices");
    {
        /* the hull of a ring of points, built from integer steps so
         * the vertices are not on a lattice line */
        STDL_PointSub ring[40];

        for (i = 0; i < 40; i++) {
            const int q = i / 10, r = i % 10;
            const int u = r * 50, v = 500 - (r * r * 5);

            ring[i].x = (int16_t)(W * 8 + (q == 0 ? u : q == 1 ? v : q == 2
                                            ? -u : -v));
            ring[i].y = (int16_t)(H * 8 + (q == 0 ? v : q == 1 ? -u : q == 2
                                            ? -v : u) / 2);
        }
        i = hull(ring, 40, many);
        if (i >= 3) {
            check_one(many, i, "many-sided");
        }
    }
}

/* writes outside the rows: a borrowed block with sentinels round it */
static void test_guard(void)
{
    enum { GW = 64, GH = 24, STRIDE = GW / 2, PAD = 64 };
    static uint8_t block[PAD + STRIDE * GH + PAD];
    int it, k;

    for (it = 0; it < PP_ITERS / 30 + 2; it++) {
        STDL_Surface *s;
        STDL_PointSub p[24];
        STDL_Pattern pat;
        int n = random_sub(p, rnd(GW * 16), rnd(GH * 16), 50 + rnd(3000),
                           50 + rnd(2000));

        memset(block, 0xA5, sizeof block);
        s = STDL_CreateSurfaceFrom(block + PAD, GW, GH, STRIDE, NULL, 0);
        if (s == NULL) {
            CHECK(0, "STDL_CreateSurfaceFrom failed");
            return;
        }
        random_pattern(&pat);
        if (n >= 3) {
            STDL_FillPolygonPattern(s, p, n, &pat);
        }
        for (k = 0; k < PAD; k++) {
            CHECK(block[k] == 0xA5 && block[PAD + STRIDE * GH + k] == 0xA5,
                  "guard %d: byte %d outside the surface written", it, k);
        }
        STDL_FreeSurface(s);
    }
}

/* the pattern is the surface's: every pixel of a covering polygon,
 * from STDL_MakePattern's documented meaning of its arguments */
static void test_anchor(void)
{
    int it;

    for (it = 0; it < PP_ITERS / 100 + 4; it++) {
        STDL_Surface *s = STDL_CreateSurface(W, H);
        STDL_PointSub p[4];
        STDL_Pattern pat;
        const uint16_t bits = (uint16_t)rnd32();
        const int c0 = rnd(16), c1 = rnd(16), tr = it & 1;
        int x, y, bad = 0;

        randomise(s, 4);
        p[0].x = (int16_t)(-rnd(200)); p[0].y = (int16_t)(-rnd(200));
        p[1].x = (int16_t)(W * 16 + rnd(200)); p[1].y = p[0].y;
        p[2].x = p[1].x; p[2].y = (int16_t)(H * 16 + rnd(200));
        p[3].x = p[0].x; p[3].y = p[2].y;
        {
            static uint8_t before[W * H], after[W * H];

            decode(s, before);
            STDL_MakePattern(&pat, (uint8_t)c0, (uint8_t)c1, bits, tr);
            STDL_FillPolygonPattern(s, p, 4, &pat);
            decode(s, after);
            for (y = 0; y < H && !bad; y++) {
                for (x = 0; x < W; x++) {
                    const int set = (bits >> (15 - ((y & 3) * 4 + (x & 3))))
                                    & 1;
                    const int want = set ? (tr ? before[y * W + x] : c1) : c0;

                    if (after[y * W + x] != want) {
                        CHECK(0, "anchor %d: (%d,%d) got %d want %d (bits "
                              "%04x %d/%d %d)", it, x, y, after[y * W + x],
                              want, bits, c0, c1, tr);
                        bad = 1;
                        break;
                    }
                }
            }
        }
        STDL_FreeSurface(s);
    }
    /* and the struct's own layout, as the header documents it */
    {
        STDL_Pattern pat;

        STDL_MakePattern(&pat, 0, 15, 0x8000, 0);
        CHECK(pat.planes[0][0] == 0x8888 && pat.planes[1][0] == 0
              && pat.mask[0] == 0, "MakePattern layout: %04x %04x",
              pat.planes[0][0], pat.planes[1][0]);
        STDL_MakePattern(&pat, 3, 0, 0x000F, 1);
        CHECK(pat.mask[3] == 0xFFFF && pat.mask[0] == 0
              && pat.planes[0][1] == 0xFFFF && pat.planes[3][0] == 0,
              "MakePattern transparent layout");
    }
}

/* the target runner (tests/hatari/ppchk.c) times each test with its
 * clock; natively nothing is timed */
#ifdef PP_CLOCK
#define RUN(t) do { \
        const unsigned long t0_ = PP_CLOCK(); \
        t(); \
        printf("  %-18s %6lu ms\n", #t, PP_CLOCK() - t0_); \
    } while (0)
#else
#define RUN(t) t()
#endif

int main(void)
{
#ifndef __m68k__
    RUN(test_reference);
#endif
    RUN(test_anchor);
    RUN(test_extremes);
    RUN(test_identity);
    RUN(test_random);
    RUN(test_inside_tall);
    RUN(test_clip);
    RUN(test_subpixel);
    RUN(test_tiling);
    RUN(test_guard);
    if (failures) {
        printf("test_polypat: %d failures\n", failures);
        return 1;
    }
    printf("test_polypat: ok\n");
    return 0;
}
