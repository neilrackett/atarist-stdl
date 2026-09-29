/*
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
/*
 * Host-side unit tests for the STDL pixel paths: fills, blits
 * (aligned/unaligned/masked), colour keys, sprites, 1bpp expansion.
 * Every operation is checked against a per-pixel reference model.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdl/stdl.h>

static int failures;

#define CHECK(cond, ...) do { \
    if (!(cond)) { \
        failures++; \
        printf("FAIL %s:%d: ", __FILE__, __LINE__); \
        printf(__VA_ARGS__); \
        printf("\n"); \
    } \
} while (0)

/* reference model: plain byte-per-pixel image */
typedef struct {
    int w, h;
    uint8_t *px;
} Ref;

static Ref *ref_new(int w, int h)
{
    Ref *r = malloc(sizeof(Ref));
    r->w = w;
    r->h = h;
    r->px = calloc(1, (size_t)w * h);
    return r;
}

static void ref_free(Ref *r) { free(r->px); free(r); }

static void surf_to_ref(const STDL_Surface *s, Ref *r)
{
    int x, y;
    for (y = 0; y < s->h; y++)
        for (x = 0; x < s->w; x++)
            r->px[y * r->w + x] = STDL_GetPixel(s, x, y);
}

static int ref_cmp(const STDL_Surface *s, const Ref *r, const char *what)
{
    int x, y, bad = 0;
    for (y = 0; y < s->h && bad < 5; y++) {
        for (x = 0; x < s->w && bad < 5; x++) {
            uint8_t got = STDL_GetPixel(s, x, y);
            uint8_t want = r->px[y * r->w + x];
            if (got != want) {
                printf("  %s mismatch at (%d,%d): got %d want %d\n",
                       what, x, y, got, want);
                bad++;
            }
        }
    }
    return bad == 0;
}

static void ref_fill(Ref *r, int x1, int y1, int w, int h, uint8_t c)
{
    int x, y;
    for (y = y1; y < y1 + h; y++) {
        if (y < 0 || y >= r->h) continue;
        for (x = x1; x < x1 + w; x++) {
            if (x < 0 || x >= r->w) continue;
            r->px[y * r->w + x] = c;
        }
    }
}

/* reference blit incl. colour key + clip rect of dst */
static void ref_blit(const Ref *src, int sx, int sy, int w, int h,
                     Ref *dst, int dx, int dy,
                     int usekey, uint8_t key, const STDL_Rect *clip)
{
    int x, y;
    /* source clip */
    if (sx < 0) { w += sx; dx -= sx; sx = 0; }
    if (sy < 0) { h += sy; dy -= sy; sy = 0; }
    if (sx + w > src->w) w = src->w - sx;
    if (sy + h > src->h) h = src->h - sy;
    for (y = 0; y < h; y++) {
        for (x = 0; x < w; x++) {
            int tx = dx + x, ty = dy + y;
            uint8_t v = src->px[(sy + y) * src->w + (sx + x)];
            if (tx < clip->x || tx >= clip->x + clip->w) continue;
            if (ty < clip->y || ty >= clip->y + clip->h) continue;
            if (usekey && v == key) continue;
            dst->px[ty * dst->w + tx] = v;
        }
    }
}

static unsigned rng_state = 12345;
static unsigned rnd(void)
{
    rng_state = rng_state * 1103515245 + 12345;
    return (rng_state >> 16) & 0x7FFF;
}

static void randomise(STDL_Surface *s, int maxcol)
{
    int x, y;
    for (y = 0; y < s->h; y++)
        for (x = 0; x < s->w; x++)
            STDL_PutPixel(s, x, y, (uint8_t)(rnd() % maxcol));
}

/* ---------------------------------------------------------------- */

static void test_putget(void)
{
    STDL_Surface *s = STDL_CreateSurface(50, 40);
    int x, y;
    for (y = 0; y < 40; y++)
        for (x = 0; x < 50; x++)
            STDL_PutPixel(s, x, y, (uint8_t)((x + y) & 15));
    for (y = 0; y < 40; y++)
        for (x = 0; x < 50; x++)
            CHECK(STDL_GetPixel(s, x, y) == ((x + y) & 15),
                  "putget (%d,%d)", x, y);
    STDL_FreeSurface(s);
}

static void test_fills(void)
{
    int i;
    for (i = 0; i < 200; i++) {
        STDL_Surface *s = STDL_CreateSurface(83, 47);
        Ref *r = ref_new(83, 47);
        int j;
        for (j = 0; j < 10; j++) {
            STDL_Rect rect;
            uint8_t c = (uint8_t)(rnd() & 15);
            rect.x = (int16_t)((int)(rnd() % 120) - 20);
            rect.y = (int16_t)((int)(rnd() % 70) - 10);
            rect.w = (uint16_t)(rnd() % 90);
            rect.h = (uint16_t)(rnd() % 60);
            STDL_FillRect(s, &rect, c);
            ref_fill(r, rect.x, rect.y, rect.w, rect.h, c);
        }
        CHECK(ref_cmp(s, r, "fill"), "fill iteration %d", i);
        ref_free(r);
        STDL_FreeSurface(s);
        if (failures) break;
    }
}

/*
 * Fills of whole rows on surfaces whose rows have no padding - one
 * contiguous block, which colours 0 and 15 fill with memset and the
 * others with a single long-pair store run. Every colour, full
 * surface and row bands, plus a masked surface and a clip that
 * narrows the rows, both of which must take the ordinary paths.
 */
static void test_fill_blocks(void)
{
    static const int widths[3] = { 16, 48, 320 };
    int i;

    for (i = 0; i < 120; i++) {
        int w = widths[i % 3], h = 1 + (int)(rnd() % 40);
        STDL_Surface *s = STDL_CreateSurface(w, h);
        Ref *r = ref_new(w, h);
        uint8_t c = (uint8_t)(rnd() & 15);
        STDL_Rect band;
        int kind = (int)(rnd() % 4);

        randomise(s, 16);
        surf_to_ref(s, r);
        if (kind == 3) {
            STDL_CreateMask(s, 1);
        }
        if (kind == 2) {
            STDL_Rect clip = { 3, 0, 0, 0 };
            clip.w = (uint16_t)(w - 3);
            clip.h = (uint16_t)h;
            STDL_SetClipRect(s, &clip);
        }
        band.x = 0;
        band.y = (int16_t)(kind == 1 ? (int)(rnd() % h) : 0);
        band.w = (uint16_t)w;
        band.h = (uint16_t)(kind == 1 ? 1 + (int)(rnd() % h) : h);
        ref_fill(r, kind == 2 ? 3 : 0, band.y,
                 kind == 2 ? w - 3 : w, band.h, c);
        STDL_FillRect(s, kind == 0 ? NULL : &band, c);
        CHECK(ref_cmp(s, r, "fill block"),
              "fill block w=%d h=%d c=%d kind=%d", w, h, c, kind);
        ref_free(r);
        STDL_FreeSurface(s);
        if (failures > 3) return;
    }
}

static void test_hvlines(void)
{
    STDL_Surface *s = STDL_CreateSurface(70, 30);
    Ref *r = ref_new(70, 30);
    int i;
    for (i = 0; i < 300; i++) {
        int a = (int)(rnd() % 90) - 10, b = (int)(rnd() % 90) - 10;
        int y = (int)(rnd() % 40) - 5;
        uint8_t c = (uint8_t)(rnd() & 15);
        if (rnd() & 1) {
            STDL_HLine(s, a, b, y, c);
            {
                int lo = a < b ? a : b, hi = a < b ? b : a;
                if (y >= 0 && y < 30)
                    ref_fill(r, lo, y, hi - lo + 1, 1, c);
            }
        } else {
            STDL_VLine(s, y, a, b, c);
            {
                int lo = a < b ? a : b, hi = a < b ? b : a;
                if (y >= 0 && y < 70)
                    ref_fill(r, y, lo, 1, hi - lo + 1, c);
            }
        }
    }
    CHECK(ref_cmp(s, r, "hvline"), "hvlines");
    ref_free(r);
    STDL_FreeSurface(s);
}

/* reference XOR: invert the planes selected by col */
static void ref_xor(Ref *r, int x1, int y1, int w, int h, uint8_t c)
{
    int x, y;
    for (y = y1; y < y1 + h; y++) {
        if (y < 0 || y >= r->h) continue;
        for (x = x1; x < x1 + w; x++) {
            if (x < 0 || x >= r->w) continue;
            r->px[y * r->w + x] ^= (uint8_t)(c & 15);
        }
    }
}

static void test_xor(void)
{
    int i;

    /* rects and hlines against the reference model */
    for (i = 0; i < 200; i++) {
        STDL_Surface *s = STDL_CreateSurface(83, 47);
        Ref *r = ref_new(83, 47);
        int j;
        randomise(s, 16);
        surf_to_ref(s, r);
        for (j = 0; j < 10; j++) {
            STDL_Rect rect;
            uint8_t c = (uint8_t)(rnd() & 15);
            rect.x = (int16_t)((int)(rnd() % 120) - 20);
            rect.y = (int16_t)((int)(rnd() % 70) - 10);
            rect.w = (uint16_t)(rnd() % 90);
            rect.h = (uint16_t)(rnd() % 60);
            if (rnd() & 1) {
                STDL_XorRect(s, &rect, c);
                ref_xor(r, rect.x, rect.y, rect.w, rect.h, c);
            } else {
                int a = (int)(rnd() % 100) - 10;
                int b = (int)(rnd() % 100) - 10;
                int y = (int)(rnd() % 60) - 5;
                int lo = a < b ? a : b, hi = a < b ? b : a;
                STDL_XorHLine(s, a, b, y, c);
                if (y >= 0 && y < 47)
                    ref_xor(r, lo, y, hi - lo + 1, 1, c);
            }
        }
        CHECK(ref_cmp(s, r, "xor"), "xor iteration %d", i);
        ref_free(r);
        STDL_FreeSurface(s);
        if (failures) break;
    }

    /* vlines and pixels, including the two-plane long-word path */
    {
        STDL_Surface *s = STDL_CreateSurface(70, 30);
        Ref *r = ref_new(70, 30);
        randomise(s, 16);
        surf_to_ref(s, r);
        for (i = 0; i < 400; i++) {
            int a = (int)(rnd() % 50) - 5, b = (int)(rnd() % 50) - 5;
            int x = (int)(rnd() % 80) - 5;
            /* colour 3 exercises the planes 0+1 long path */
            uint8_t c = (uint8_t)((rnd() & 1) ? 3 : (rnd() & 15));
            int lo = a < b ? a : b, hi = a < b ? b : a;
            if (rnd() & 1) {
                STDL_XorVLine(s, x, a, b, c);
                if (x >= 0 && x < 70)
                    ref_xor(r, x, lo, 1, hi - lo + 1, c);
            } else {
                STDL_XorPixel(s, x, a, c);
                ref_xor(r, x, a, 1, 1, c);
            }
        }
        CHECK(ref_cmp(s, r, "xorline"), "xor lines");
        ref_free(r);
        STDL_FreeSurface(s);
    }

    /* XOR is an involution: the same shape twice is a no-op */
    {
        STDL_Surface *s = STDL_CreateSurface(83, 47);
        Ref *r = ref_new(83, 47);
        STDL_Rect rect;
        randomise(s, 16);
        surf_to_ref(s, r);
        rect.x = 5; rect.y = 3; rect.w = 61; rect.h = 29;
        STDL_XorRect(s, &rect, 10);
        rect.x = 5; rect.y = 3; rect.w = 61; rect.h = 29;
        STDL_XorRect(s, &rect, 10);
        STDL_XorVLine(s, 33, 2, 44, 3);
        STDL_XorVLine(s, 33, 2, 44, 3);
        STDL_XorPixel(s, 17, 9, 7);
        STDL_XorPixel(s, 17, 9, 7);
        CHECK(ref_cmp(s, r, "xor involution"), "xor twice restores");
        ref_free(r);
        STDL_FreeSurface(s);
    }

    /* colour 0 touches nothing; a masked surface goes opaque where
     * the XOR landed */
    {
        STDL_Surface *s = STDL_CreateSurface(40, 20);
        Ref *r = ref_new(40, 20);
        STDL_Rect rect;
        randomise(s, 16);
        surf_to_ref(s, r);
        rect.x = 0; rect.y = 0; rect.w = 40; rect.h = 20;
        STDL_XorRect(s, &rect, 0);
        STDL_XorHLine(s, 0, 39, 4, 0);
        STDL_XorVLine(s, 4, 0, 19, 0);
        STDL_XorPixel(s, 4, 4, 0);
        CHECK(ref_cmp(s, r, "xor col 0"), "xor with colour 0 is a no-op");

        STDL_CreateMask(s, 1);
        CHECK(!STDL_SurfaceIsOpaque(s), "mask starts transparent");
        rect.x = 2; rect.y = 2; rect.w = 4; rect.h = 4;
        STDL_XorRect(s, &rect, 5);
        CHECK((*(uint16_t *)(s->mask + 2 * s->maskstride)
               & 0x2000u) == 0,
              "xor cleared the mask bit at (2,2)");
        CHECK((*(uint16_t *)s->mask & 0x8000u) != 0,
              "xor left untouched mask bits alone");
        STDL_FreeSurface(s);
        ref_free(r);
    }
}

/* ---------------------------------------------------------------- */
/* batched span lists                                               */

/* byte-for-byte: pixels and, when present, the mask */
static int same_bytes(const STDL_Surface *a, const STDL_Surface *b,
                      const char *what)
{
    if (memcmp(a->pixels, b->pixels,
               (size_t)a->stride * (size_t)a->h) != 0) {
        printf("  %s: pixel bytes differ\n", what);
        return 0;
    }
    if ((a->mask == NULL) != (b->mask == NULL)) {
        printf("  %s: one surface has a mask and the other does not\n",
               what);
        return 0;
    }
    if (a->mask != NULL
        && memcmp(a->mask, b->mask,
                  (size_t)a->maskstride * (size_t)a->h) != 0) {
        printf("  %s: mask bytes differ\n", what);
        return 0;
    }
    return 1;
}

/* the same list, one span at a time through the scalar primitives */
static void per_span(STDL_Surface *s, const STDL_Span *sp, int n,
                     uint8_t col, int vertical, int xor_op)
{
    int i;
    for (i = 0; i < n; i++) {
        if (sp[i].len <= 0) {
            continue;               /* the batched calls skip these */
        }
        if (vertical) {
            if (xor_op) {
                STDL_XorVLine(s, sp[i].x, sp[i].y,
                              sp[i].y + sp[i].len - 1, col);
            } else {
                STDL_VLine(s, sp[i].x, sp[i].y,
                           sp[i].y + sp[i].len - 1, col);
            }
        } else {
            if (xor_op) {
                STDL_XorHLine(s, sp[i].x, sp[i].x + sp[i].len - 1,
                              sp[i].y, col);
            } else {
                STDL_HLine(s, sp[i].x, sp[i].x + sp[i].len - 1,
                           sp[i].y, col);
            }
        }
    }
}

static void batched(STDL_Surface *s, const STDL_Span *sp, int n,
                    uint8_t col, int vertical, int xor_op)
{
    if (vertical) {
        if (xor_op) STDL_XorVSpans(s, sp, n, col);
        else        STDL_VSpans(s, sp, n, col);
    } else {
        if (xor_op) STDL_XorHSpans(s, sp, n, col);
        else        STDL_HSpans(s, sp, n, col);
    }
}

/*
 * STDL_Points and STDL_PointsC must be indistinguishable from
 * STDL_PutPixel per entry - same pixels, same mask, same clipping -
 * for unsorted, overlapping and off-the-edge lists, masked or not,
 * with and without a clip rect, and for STDL_TRANSPARENT.
 *
 * The unmasked, origin-clipped, long-aligned case has its own inner
 * loop (points_fast / pointsc_fast / points_clear), so the mix below
 * has to reach both it and the general points_run: masked on odd
 * iterations, a clip rect away from the origin on every third, and
 * colour 0 - which takes the clear-only path - as one of the
 * seventeen colours.
 */
static void test_points(void)
{
    const int W = 83, H = 47;
    int iter;

    for (iter = 0; iter < 400; iter++) {
        STDL_Surface *a = STDL_CreateSurface(W, H);
        STDL_Surface *b = STDL_CreateSurface(W, H);
        STDL_Point pts[32];
        uint8_t cols[32];
        int n = 1 + (int)(rnd() % 32);
        int masked = (iter & 1);
        int clipped = (iter % 3) == 0;
        int percol = (iter & 2);
        /* a third of them STDL_TRANSPARENT or above */
        uint8_t col = (uint8_t)(rnd() % 24);
        int i;

        randomise(a, 16);
        STDL_BlitSurface(a, NULL, b, NULL);
        if (masked) {
            STDL_CreateMask(a, 1);
            STDL_CreateMask(b, 1);
            for (i = 0; i < 20; i++) {
                int hx = (int)(rnd() % W), hy = (int)(rnd() % H);
                STDL_PutPixel(a, hx, hy, STDL_TRANSPARENT);
                STDL_PutPixel(b, hx, hy, STDL_TRANSPARENT);
            }
        }
        if (clipped) {
            STDL_Rect c;
            c.x = (int16_t)(iter % 5);      /* 0 reaches the fast loop */
            c.y = (int16_t)(iter % 4);
            c.w = 60; c.h = 30;
            STDL_SetClipRect(a, &c);
            STDL_SetClipRect(b, &c);
        }
        for (i = 0; i < n; i++) {
            pts[i].x = (int16_t)((int)(rnd() % (W + 24)) - 12);
            pts[i].y = (int16_t)((int)(rnd() % (H + 24)) - 12);
            cols[i] = (uint8_t)(rnd() % 17);
        }
        STDL_SurfaceIsOpaque(a);
        STDL_SurfaceIsOpaque(b);
        if (percol) {
            STDL_PointsC(a, pts, cols, n);
            for (i = 0; i < n; i++) {
                STDL_PutPixel(b, pts[i].x, pts[i].y, cols[i]);
            }
        } else {
            STDL_Points(a, pts, n, col);
            for (i = 0; i < n; i++) {
                STDL_PutPixel(b, pts[i].x, pts[i].y, col);
            }
        }
        CHECK(same_bytes(a, b, percol ? "colour list" : "point list"),
              "points iter %d (n=%d col=%d percol=%d masked=%d "
              "clipped=%d)", iter, n, col, percol, masked, clipped);
        CHECK(a->opaque_state == b->opaque_state,
              "points iter %d opaque_state %d vs %d", iter,
              a->opaque_state, b->opaque_state);
        STDL_FreeSurface(a);
        STDL_FreeSurface(b);
        if (failures) return;
    }

    /*
     * The fast loops merge two plane words at a time with a long
     * access, so the caller only takes them when the rows are
     * long-aligned. Nothing the public API can build is misaligned,
     * which is exactly why the fallback needs a test of its own:
     * describe a surface over an odd address and check it still
     * draws what STDL_PutPixel would.
     */
    {
        const int UW = 61, UH = 23;
        uint16_t stride = (uint16_t)(((UW + 15) / 16) * 8);
        uint8_t *raw = malloc((size_t)stride * UH + 2);
        STDL_Surface u, v;
        STDL_Point pts[24];
        uint8_t cols[24];
        int i;

        memset(&u, 0, sizeof(u));
        u.pixels = raw + 2;             /* word- but not long-aligned */
        u.w = (int16_t)UW; u.h = (int16_t)UH;
        u.stride = stride;
        u.planes = 4;
        u.clip.x = 0; u.clip.y = 0;
        u.clip.w = (uint16_t)UW; u.clip.h = (uint16_t)UH;
        memset(u.pixels, 0, (size_t)stride * UH);
        v = u;
        v.pixels = malloc((size_t)stride * UH);
        memset(v.pixels, 0, (size_t)stride * UH);

        for (i = 0; i < 24; i++) {
            pts[i].x = (int16_t)((int)(rnd() % (UW + 8)) - 4);
            pts[i].y = (int16_t)((int)(rnd() % (UH + 8)) - 4);
            cols[i] = (uint8_t)(1 + rnd() % 15);
        }
        STDL_Points(&u, pts, 24, 11);
        for (i = 0; i < 24; i++) {
            STDL_PutPixel(&v, pts[i].x, pts[i].y, 11);
        }
        CHECK(memcmp(u.pixels, v.pixels, (size_t)stride * UH) == 0,
              "unaligned STDL_Points fallback");
        STDL_PointsC(&u, pts, cols, 24);
        for (i = 0; i < 24; i++) {
            STDL_PutPixel(&v, pts[i].x, pts[i].y, cols[i]);
        }
        CHECK(memcmp(u.pixels, v.pixels, (size_t)stride * UH) == 0,
              "unaligned STDL_PointsC fallback");
        STDL_Points(&u, pts, 24, 0);
        for (i = 0; i < 24; i++) {
            STDL_PutPixel(&v, pts[i].x, pts[i].y, 0);
        }
        CHECK(memcmp(u.pixels, v.pixels, (size_t)stride * UH) == 0,
              "unaligned erase fallback");
        free(raw);
        free(v.pixels);
    }

    /* degenerate arguments must be no-ops, not crashes */
    {
        STDL_Surface *s = STDL_CreateSurface(40, 20);
        Ref *r = ref_new(40, 20);
        STDL_Point p[2] = { { 0, 0 }, { 39, 19 } };
        uint8_t c[2] = { 3, 7 };
        randomise(s, 16);
        surf_to_ref(s, r);
        STDL_Points(s, NULL, 3, 5);
        STDL_Points(s, p, 0, 5);
        STDL_Points(s, p, -1, 5);
        STDL_Points(NULL, p, 2, 5);
        STDL_PointsC(s, NULL, c, 2);
        STDL_PointsC(s, p, NULL, 2);
        STDL_PointsC(s, p, c, 0);
        STDL_PointsC(s, p, c, -1);
        STDL_PointsC(NULL, p, c, 2);
        CHECK(ref_cmp(s, r, "points no-ops"), "points no-op cases");
        ref_free(r);
        STDL_FreeSurface(s);
    }
}

/*
 * The batched call has to be indistinguishable from the per-span
 * one: same pixels, same mask, for the same list. Lists are
 * deliberately unsorted, overlapping, degenerate (len <= 0) and
 * off every edge, and both a masked and a maskless destination are
 * used.
 */
static void test_spans(void)
{
    const int W = 83, H = 47;
    int iter;

    for (iter = 0; iter < 400; iter++) {
        STDL_Surface *a = STDL_CreateSurface(W, H);
        STDL_Surface *b = STDL_CreateSurface(W, H);
        STDL_Span sp[24];
        int n = 1 + (int)(rnd() % 24);
        int vertical = (int)(rnd() & 1);
        int xor_op = (int)(rnd() & 1);
        int masked = (iter & 1);
        int clipped = (iter % 3) == 0;
        /* colour 3 is the two-plane long-word path; 16 is
         * STDL_TRANSPARENT, which only the fills honour */
        uint8_t col = (uint8_t)((rnd() % 3 == 0) ? 3
                       : (xor_op ? (rnd() & 15)
                                 : (rnd() % 17)));
        int i;

        randomise(a, 16);
        STDL_BlitSurface(a, NULL, b, NULL);
        if (masked) {
            STDL_CreateMask(a, 1);
            STDL_CreateMask(b, 1);
            /* punch a few holes so the mask is not uniform */
            for (i = 0; i < 20; i++) {
                int hx = (int)(rnd() % W), hy = (int)(rnd() % H);
                STDL_PutPixel(a, hx, hy, STDL_TRANSPARENT);
                STDL_PutPixel(b, hx, hy, STDL_TRANSPARENT);
            }
        }
        if (clipped) {
            STDL_Rect clip;
            clip.x = (int16_t)(rnd() % 20);
            clip.y = (int16_t)(rnd() % 12);
            clip.w = (uint16_t)(1 + rnd() % (unsigned)(W - clip.x));
            clip.h = (uint16_t)(1 + rnd() % (unsigned)(H - clip.y));
            STDL_SetClipRect(a, &clip);
            STDL_SetClipRect(b, &clip);
        }

        for (i = 0; i < n; i++) {
            switch (rnd() % 8) {
            case 0:             /* off the left / top edge */
                sp[i].x = (int16_t)(-(int)(rnd() % 40));
                sp[i].y = (int16_t)(-(int)(rnd() % 40));
                break;
            case 1:             /* off the right / bottom edge */
                sp[i].x = (int16_t)(W - 1 - (int)(rnd() % 4));
                sp[i].y = (int16_t)(H - 1 - (int)(rnd() % 4));
                break;
            case 2:             /* exactly on the last row/column */
                sp[i].x = (int16_t)(W - 1);
                sp[i].y = (int16_t)(H - 1);
                break;
            case 3:             /* the origin */
                sp[i].x = 0;
                sp[i].y = 0;
                break;
            default:
                sp[i].x = (int16_t)((int)(rnd() % (W + 30)) - 15);
                sp[i].y = (int16_t)((int)(rnd() % (H + 30)) - 15);
                break;
            }
            switch (rnd() % 10) {
            case 0:  sp[i].len = 0; break;
            case 1:  sp[i].len = (int16_t)(-(int)(rnd() % 20)); break;
            case 2:  sp[i].len = (int16_t)(60 + rnd() % 60); break;
            default: sp[i].len = (int16_t)(1 + rnd() % 4); break;
            }
        }

        /* prime the opacity cache so a batched call that forgot to
         * invalidate it would leave a stale value behind */
        STDL_SurfaceIsOpaque(a);
        STDL_SurfaceIsOpaque(b);

        batched(a, sp, n, col, vertical, xor_op);
        per_span(b, sp, n, col, vertical, xor_op);
        CHECK(same_bytes(a, b, "span list"),
              "span iter %d (n=%d vertical=%d xor=%d col=%d masked=%d "
              "clipped=%d)", iter, n, vertical, xor_op, col, masked,
              clipped);

        /* opaque_state must be invalidated exactly as the per-span
         * path invalidates it */
        CHECK(a->opaque_state == b->opaque_state,
              "span iter %d opaque_state %d vs %d", iter,
              a->opaque_state, b->opaque_state);

        STDL_FreeSurface(a);
        STDL_FreeSurface(b);
        if (failures) return;
    }

    /* XOR involution over a whole list, including clipped and
     * degenerate entries */
    {
        STDL_Surface *s = STDL_CreateSurface(W, H);
        Ref *r = ref_new(W, H);
        STDL_Span sp[6] = {
            { 3, -5, 20 }, { 40, 44, 9 }, { -6, 10, 12 },
            { 82, 0, 47 }, { 12, 12, 0 }, { 50, 20, -3 }
        };
        randomise(s, 16);
        surf_to_ref(s, r);
        STDL_XorVSpans(s, sp, 6, 3);
        STDL_XorVSpans(s, sp, 6, 3);
        CHECK(ref_cmp(s, r, "xor vspans involution"),
              "xor vspan list twice restores");
        STDL_XorHSpans(s, sp, 6, 9);
        STDL_XorHSpans(s, sp, 6, 9);
        CHECK(ref_cmp(s, r, "xor hspans involution"),
              "xor hspan list twice restores");
        ref_free(r);
        STDL_FreeSurface(s);
    }

    /* NULL and empty lists are no-ops, colour 0 is a no-op for XOR */
    {
        STDL_Surface *s = STDL_CreateSurface(40, 20);
        Ref *r = ref_new(40, 20);
        STDL_Span sp[2] = { { 0, 0, 20 }, { 39, 0, 20 } };
        randomise(s, 16);
        surf_to_ref(s, r);
        STDL_XorVSpans(s, NULL, 3, 5);
        STDL_XorVSpans(s, sp, 0, 5);
        STDL_XorVSpans(s, sp, -1, 5);
        STDL_XorVSpans(s, sp, 2, 0);
        STDL_XorHSpans(s, sp, 2, 0);
        STDL_XorVSpans(NULL, sp, 2, 5);
        STDL_VSpans(s, NULL, 2, 5);
        STDL_HSpans(s, sp, 0, 5);
        CHECK(ref_cmp(s, r, "span no-ops"), "span no-op cases");
        ref_free(r);
        STDL_FreeSurface(s);
    }
}

static void test_blits(void)
{
    int i;
    for (i = 0; i < 400; i++) {
        int sw = 17 + (int)(rnd() % 80);
        int sh = 5 + (int)(rnd() % 40);
        STDL_Surface *src = STDL_CreateSurface(sw, sh);
        STDL_Surface *dst = STDL_CreateSurface(90, 60);
        Ref *rs = ref_new(sw, sh);
        Ref *rd = ref_new(90, 60);
        STDL_Rect srcrect, dstrect, clip;
        int usekey = (int)(rnd() & 1);
        uint8_t key = 3;

        randomise(src, usekey ? 6 : 16);
        randomise(dst, 16);
        if (usekey) {
            STDL_SetColourKey(src, 1, key);
        }
        /* random clip rect on dst */
        clip.x = (int16_t)(rnd() % 20);
        clip.y = (int16_t)(rnd() % 15);
        clip.w = (uint16_t)(30 + rnd() % 60);
        clip.h = (uint16_t)(20 + rnd() % 40);
        STDL_SetClipRect(dst, &clip);

        surf_to_ref(src, rs);
        surf_to_ref(dst, rd);

        srcrect.x = (int16_t)((int)(rnd() % (sw + 10)) - 5);
        srcrect.y = (int16_t)((int)(rnd() % (sh + 6)) - 3);
        srcrect.w = (uint16_t)(rnd() % (sw + 5));
        srcrect.h = (uint16_t)(rnd() % (sh + 4));
        dstrect.x = (int16_t)((int)(rnd() % 110) - 10);
        dstrect.y = (int16_t)((int)(rnd() % 70) - 6);
        dstrect.w = 0;
        dstrect.h = 0;

        ref_blit(rs, srcrect.x, srcrect.y, srcrect.w, srcrect.h,
                 rd, dstrect.x, dstrect.y, usekey, key, &dst->clip);
        STDL_BlitSurface(src, &srcrect, dst, &dstrect);

        CHECK(ref_cmp(dst, rd, "blit"),
              "blit iter %d (key=%d src=%d,%d %ux%u dst=%d,%d "
              "clip=%d,%d %ux%u)",
              i, usekey, srcrect.x, srcrect.y, srcrect.w, srcrect.h,
              dstrect.x, dstrect.y, clip.x, clip.y, clip.w, clip.h);

        ref_free(rs);
        ref_free(rd);
        STDL_FreeSurface(src);
        STDL_FreeSurface(dst);
        if (failures > 3) return;
    }
}

/*
 * Same-phase unmasked blits whose edges are partial groups, 3-10
 * groups wide - every restore at an unaligned x. The middle of each
 * row is copied inline when short and through memcpy when long, as
 * longs when both rows are long aligned and as words when a borrowed
 * block is only word aligned; every combination against the model,
 * with the destination mask's upkeep checked where there is one.
 */
static void test_partial_middle(void)
{
    enum { W = 192, H = 12, STRIDE = W / 2, MSTRIDE = W / 8 };
    static uint8_t bufs[2][STRIDE * H + 8];
    static uint16_t mbuf[MSTRIDE * H / 2];
    int view, withmask, phase, ng;

    for (view = 0; view <= 1; view++) {
        for (withmask = 0; withmask <= 1; withmask++) {
            for (phase = 0; phase < 16; phase++) {
                for (ng = 3; ng <= 10; ng++) {
                    STDL_Surface *src, *dst;
                    Ref *rs, *rd;
                    STDL_Rect sr, dr;
                    int w = ng * 16 - phase - (int)(rnd() % 15) - 1;
                    int x, y, bad = 0;

                    if (view) {
                        /* word aligned, never long aligned; a
                         * borrowed view brings its own mask */
                        uint8_t *a = bufs[0], *b = bufs[1];
                        while (((uintptr_t)a & 3) != 2) { a++; }
                        while (((uintptr_t)b & 3) != 2) { b++; }
                        src = STDL_CreateSurfaceFrom(a, W, H, STRIDE,
                                                     NULL, 0);
                        dst = STDL_CreateSurfaceFrom(b, W, H, STRIDE,
                                                     withmask
                                                     ? (uint8_t *)mbuf
                                                     : NULL,
                                                     withmask ? MSTRIDE
                                                     : 0);
                    } else {
                        src = STDL_CreateSurface(W, H);
                        dst = STDL_CreateSurface(W, H);
                    }
                    if (src == NULL || dst == NULL) {
                        CHECK(0, "partial-middle setup");
                        return;
                    }
                    randomise(src, 16);
                    randomise(dst, 16);
                    /* after randomise: PutPixel maintains the mask,
                     * so drawing the background made it all opaque */
                    if (withmask && view) {
                        memset(mbuf, 0xFF, sizeof(mbuf));
                    } else if (withmask && STDL_CreateMask(dst, 1) != 0) {
                        CHECK(0, "partial-middle mask");
                        return;
                    }
                    rs = ref_new(W, H);
                    rd = ref_new(W, H);
                    surf_to_ref(src, rs);
                    surf_to_ref(dst, rd);

                    sr.x = (int16_t)(16 + phase);
                    sr.y = 1;
                    sr.w = (uint16_t)w;
                    sr.h = 9;
                    dr.x = (int16_t)(32 + phase);
                    dr.y = 2;
                    ref_blit(rs, sr.x, sr.y, sr.w, sr.h, rd, dr.x, dr.y,
                             0, 0, &dst->clip);
                    STDL_BlitSurface(src, &sr, dst, &dr);
                    CHECK(ref_cmp(dst, rd, "partial middle"),
                          "partial middle view=%d mask=%d phase=%d w=%d",
                          view, withmask, phase, w);
                    if (withmask) {
                        /* opaque exactly where the blit landed */
                        for (y = 0; y < H; y++) {
                            const uint16_t *mr = (const uint16_t *)
                                (dst->mask + y * dst->maskstride);
                            for (x = 0; x < W; x++) {
                                int in = x >= dr.x && x < dr.x + w
                                      && y >= dr.y && y < dr.y + 9;
                                int set = (mr[x >> 4]
                                           >> (15 - (x & 15))) & 1;
                                if (set == in) {
                                    bad++;
                                }
                            }
                        }
                        CHECK(bad == 0, "partial middle mask phase=%d "
                              "w=%d view=%d: %d bits", phase, w, view,
                              bad);
                    }
                    ref_free(rs);
                    ref_free(rd);
                    STDL_FreeSurface(src);
                    STDL_FreeSurface(dst);
                    if (failures > 3) return;
                }
            }
        }
    }
}

/*
 * Unaligned blits with no source mask and no flags take their own
 * loop, whose edges are peeled and whose middle groups are stored
 * without reading the destination. Every width shape (one group,
 * two, many), both shift directions, plane budgets 4, 2 and 3, with
 * and without a destination mask, against the model - and the mask
 * must end up opaque exactly under the blit.
 */
static void test_shift_plain(void)
{
    static const int budgets[3] = { 4, 2, 3 };
    enum { W = 200, H = 14 };
    int iter;

    for (iter = 0; iter < 600; iter++) {
        const int budget = budgets[iter % 3];
        const int withmask = (iter / 3) & 1;
        STDL_Surface *src = STDL_CreateSurface(W, H);
        STDL_Surface *dst = STDL_CreateSurface(W, H);
        Ref *rs = ref_new(W, H), *rd = ref_new(W, H);
        STDL_Rect sr, dr;
        int w = 1 + (int)(rnd() % 150);
        int sx = 8 + (int)(rnd() % 40), dx = 8 + (int)(rnd() % 40);
        int x, y, bad = 0;

        if ((sx & 15) == (dx & 15)) {
            dx ^= 5;                    /* keep the phases apart */
        }
        STDL_SetPlaneBudget(budget);
        randomise(src, 1 << budget);
        randomise(dst, 1 << budget);
        if (withmask) {
            STDL_CreateMask(dst, 1);
        }
        surf_to_ref(src, rs);
        surf_to_ref(dst, rd);
        sr.x = (int16_t)sx;
        sr.y = 2;
        sr.w = (uint16_t)w;
        sr.h = 9;
        dr.x = (int16_t)dx;
        dr.y = 3;
        ref_blit(rs, sr.x, sr.y, sr.w, sr.h, rd, dr.x, dr.y, 0, 0,
                 &dst->clip);
        STDL_BlitSurface(src, &sr, dst, &dr);
        CHECK(ref_cmp(dst, rd, "shift plain"),
              "shift plain sx=%d dx=%d w=%d np=%d mask=%d", sx, dx, w,
              budget, withmask);
        if (withmask) {
            for (y = 0; y < H; y++) {
                const uint16_t *mr = (const uint16_t *)
                    (dst->mask + y * dst->maskstride);
                for (x = 0; x < W; x++) {
                    int in = x >= dr.x && x < dr.x + dr.w
                          && y >= dr.y && y < dr.y + dr.h;
                    int set = (mr[x >> 4] >> (15 - (x & 15))) & 1;
                    if (set == in) {
                        bad++;
                    }
                }
            }
            CHECK(bad == 0, "shift plain mask sx=%d dx=%d w=%d: %d bits",
                  sx, dx, w, bad);
        }
        ref_free(rs);
        ref_free(rd);
        STDL_FreeSurface(src);
        STDL_FreeSurface(dst);
        STDL_SetPlaneBudget(4);
        if (failures > 3) return;
    }
}

static void test_whole_blit_writeback(void)
{
    /* NULL srcrect + dstrect writeback semantics */
    STDL_Surface *src = STDL_CreateSurface(32, 8);
    STDL_Surface *dst = STDL_CreateSurface(64, 16);
    STDL_Rect d = { -4, -2, 0, 0 };
    randomise(src, 16);
    STDL_BlitSurface(src, NULL, dst, &d);
    CHECK(d.x == 0 && d.y == 0 && d.w == 28 && d.h == 6,
          "writeback got %d,%d %ux%u", d.x, d.y, d.w, d.h);
    STDL_FreeSurface(src);
    STDL_FreeSurface(dst);
}

static void test_whole_copy(void)
{
    /* a whole surface onto one the same size: one memcpy when the
     * rows have no padding, the general path for everything else */
    enum { W = 320, H = 40, VW = 128, VH = 8, VSTRIDE = 96 };
    STDL_Surface *src = STDL_CreateSurface(W, H);
    STDL_Surface *dst = STDL_CreateSurface(W, H);
    STDL_Rect d = { 0, 0, 0, 0 };
    Ref *rs = ref_new(W, H);
    Ref *rd = ref_new(W, H);
    uint8_t *a = malloc((size_t)VSTRIDE * VH);
    uint8_t *b = malloc((size_t)VSTRIDE * VH);
    STDL_Surface *va, *vb;
    int i, x, y, bad = 0;

    randomise(src, 16);
    randomise(dst, 16);
    STDL_BlitSurface(src, NULL, dst, &d);
    CHECK(memcmp(dst->pixels, src->pixels, (size_t)src->stride * H) == 0,
          "whole copy");
    CHECK(d.x == 0 && d.y == 0 && d.w == W && d.h == H,
          "whole copy writeback got %d,%d %ux%u", d.x, d.y, d.w, d.h);

    /* a keyed source keeps its key */
    randomise(src, 6);
    randomise(dst, 16);
    STDL_SetColourKey(src, 1, 3);
    surf_to_ref(src, rs);
    surf_to_ref(dst, rd);
    ref_blit(rs, 0, 0, W, H, rd, 0, 0, 1, 3, &dst->clip);
    STDL_BlitSurface(src, NULL, dst, NULL);
    CHECK(ref_cmp(dst, rd, "whole keyed"), "whole keyed copy");

    /* views into wider rows: the bytes beside them are not theirs */
    for (i = 0; i < VSTRIDE * VH; i++) {
        a[i] = (uint8_t)rnd();
    }
    memset(b, 0xA5, (size_t)VSTRIDE * VH);
    va = STDL_CreateSurfaceFrom(a, VW, VH, VSTRIDE, NULL, 0);
    vb = STDL_CreateSurfaceFrom(b, VW, VH, VSTRIDE, NULL, 0);
    STDL_BlitSurface(va, NULL, vb, NULL);
    for (y = 0; y < VH; y++) {
        if (memcmp(b + y * VSTRIDE, a + y * VSTRIDE, VW / 2) != 0) {
            bad++;
        }
        for (x = VW / 2; x < VSTRIDE; x++) {
            if (b[y * VSTRIDE + x] != 0xA5) {
                bad++;
            }
        }
    }
    CHECK(bad == 0, "whole copy between views: %d bad rows or bytes", bad);

    STDL_FreeSurface(va);
    STDL_FreeSurface(vb);
    free(a);
    free(b);
    ref_free(rs);
    ref_free(rd);
    STDL_FreeSurface(src);
    STDL_FreeSurface(dst);
}

static void test_sprites(void)
{
    /* sprite from surface must draw identically to a keyed blit */
    int iter;
    for (iter = 0; iter < 60; iter++) {
        STDL_Surface *img = STDL_CreateSurface(32, 20);
        STDL_Surface *a = STDL_CreateSurface(90, 40);
        STDL_Surface *b = STDL_CreateSurface(90, 40);
        STDL_Sprite *spr;
        int x = (int)(rnd() % 100) - 20;
        int y = (int)(rnd() % 50) - 10;
        int pre = (int)(rnd() & 1);
        STDL_Rect d;

        randomise(img, 5);
        STDL_SetColourKey(img, 1, 2);
        randomise(a, 16);
        STDL_BlitSurface(a, NULL, b, NULL);

        spr = STDL_SpriteFromSurface(img, 32,
                                     pre ? STDL_PRESHIFT : 0);
        CHECK(spr != NULL, "sprite build");
        if (!spr) return;

        d.x = (int16_t)x;
        d.y = (int16_t)y;
        d.w = 32;
        d.h = 20;
        STDL_BlitSurface(img, NULL, a, &d);
        STDL_BlitSprite(spr, 0, b, x, y);

        {
            int xx, yy, bad = 0;
            for (yy = 0; yy < 40 && bad < 4; yy++)
                for (xx = 0; xx < 90 && bad < 4; xx++) {
                    uint8_t va = STDL_GetPixel(a, xx, yy);
                    uint8_t vb = STDL_GetPixel(b, xx, yy);
                    if (va != vb) {
                        printf("  sprite mismatch (%d,%d): surf=%d "
                               "sprite=%d [x=%d y=%d pre=%d]\n",
                               xx, yy, va, vb, x, y, pre);
                        bad++;
                        failures++;
                    }
                }
        }
        STDL_FreeSprite(spr);
        STDL_FreeSurface(img);
        STDL_FreeSurface(a);
        STDL_FreeSurface(b);
        if (failures > 3) return;
    }
}

/*
 * Sprites against the keyed blit of the same frame, across the shapes
 * the row loops treat differently: widths 1-80 (a partial last group,
 * one group, several), multi-frame strips, both loops (unshifted and
 * pre-shifted, every phase), random clip rectangles cutting either
 * edge or both, and plane budgets 4, 2 and 3 - the loops are
 * instantiated per budget.
 */
static void test_sprites_wide(void)
{
    static const int budgets[3] = { 4, 2, 3 };
    int iter;

    for (iter = 0; iter < 360; iter++) {
        const int budget = budgets[iter % 3];
        const int maxcol = 1 << budget;
        const uint8_t key = (uint8_t)(maxcol - 2);
        int w = 1 + (int)(rnd() % 80);
        int h = 1 + (int)(rnd() % 24);
        int nframes = 1, frame = 0, pre = (int)(rnd() & 1);
        STDL_Surface *img, *a, *b;
        STDL_Sprite *spr;
        STDL_Rect clip, sr, d;
        int x, y, xx, yy, bad = 0;

        if ((w & 15) == 0 && (rnd() & 1)) {
            nframes = 1 + (int)(rnd() % 3);
            frame = (int)(rnd() % nframes);
        }
        STDL_SetPlaneBudget(budget);
        img = STDL_CreateSurface(w * nframes, h);
        a = STDL_CreateSurface(120, 60);
        b = STDL_CreateSurface(120, 60);
        randomise(img, maxcol);
        STDL_SetColourKey(img, 1, key);
        randomise(a, maxcol);
        STDL_BlitSurface(a, NULL, b, NULL);

        clip.x = (int16_t)(rnd() % 40);
        clip.y = (int16_t)(rnd() % 20);
        clip.w = (uint16_t)(10 + rnd() % 90);
        clip.h = (uint16_t)(5 + rnd() % 45);
        STDL_SetClipRect(a, &clip);
        STDL_SetClipRect(b, &clip);

        spr = STDL_SpriteFromSurface(img, w, pre ? STDL_PRESHIFT : 0);
        CHECK(spr != NULL, "wide sprite build w=%d", w);
        if (spr == NULL) {
            return;
        }
        x = (int)(rnd() % (120 + w + 16)) - w - 8;
        y = (int)(rnd() % (60 + h + 8)) - h - 4;

        sr.x = (int16_t)(frame * w);
        sr.y = 0;
        sr.w = (uint16_t)w;
        sr.h = (uint16_t)h;
        d.x = (int16_t)x;
        d.y = (int16_t)y;
        STDL_BlitSurface(img, &sr, a, &d);
        STDL_BlitSprite(spr, frame, b, x, y);

        for (yy = 0; yy < 60 && bad < 4; yy++) {
            for (xx = 0; xx < 120 && bad < 4; xx++) {
                uint8_t va = STDL_GetPixel(a, xx, yy);
                uint8_t vb = STDL_GetPixel(b, xx, yy);
                if (va != vb) {
                    printf("  wide sprite (%d,%d): blit=%d sprite=%d "
                           "[w=%d h=%d f=%d/%d x=%d y=%d pre=%d np=%d "
                           "clip=%d,%d %ux%u]\n", xx, yy, va, vb, w, h,
                           frame, nframes, x, y, pre, budget, clip.x,
                           clip.y, clip.w, clip.h);
                    bad++;
                    failures++;
                }
            }
        }
        STDL_FreeSprite(spr);
        STDL_FreeSurface(img);
        STDL_FreeSurface(a);
        STDL_FreeSurface(b);
        STDL_SetPlaneBudget(4);
        if (failures > 3) return;
    }
}

static void test_1bpp(void)
{
    static const uint8_t bits[] = {
        0xF0, 0x0F,     /* row 0: 11110000 00001111 */
        0xAA, 0x55,     /* row 1: 10101010 01010101 */
    };
    STDL_Surface *s = STDL_SurfaceFrom1bpp(bits, 16, 2, 7, 2);
    int x;
    CHECK(s != NULL, "1bpp create");
    for (x = 0; x < 16; x++) {
        int bit0 = (bits[x >> 3] >> (7 - (x & 7))) & 1;
        int bit1 = (bits[2 + (x >> 3)] >> (7 - (x & 7))) & 1;
        CHECK(STDL_GetPixel(s, x, 0) == (bit0 ? 7 : 2),
              "1bpp row0 x=%d", x);
        CHECK(STDL_GetPixel(s, x, 1) == (bit1 ? 7 : 2),
              "1bpp row1 x=%d", x);
    }
    STDL_FreeSurface(s);
}

static void test_tiles(void)
{
    STDL_Surface *img = STDL_CreateSurface(32, 32);
    STDL_Surface *dst = STDL_CreateSurface(64, 40);
    STDL_Tileset *ts;
    int x, y;

    randomise(img, 16);
    ts = STDL_TilesetFromSurface(img, 16, 16);
    CHECK(ts != NULL && ts->ntiles == 4, "tileset build");
    if (!ts) return;
    STDL_BlitTile(ts, 3, dst, 16, 4);
    for (y = 0; y < 16; y++)
        for (x = 0; x < 16; x++)
            CHECK(STDL_GetPixel(dst, 16 + x, 4 + y)
                  == STDL_GetPixel(img, 16 + x, 16 + y),
                  "tile 3 pixel (%d,%d)", x, y);
    STDL_FreeTileset(ts);
    STDL_FreeSurface(img);
    STDL_FreeSurface(dst);
}

/*
 * Tiles against a per-pixel model of their contract: x rounds down to
 * a group, a group the clip rectangle cuts is dropped whole, masked
 * tiles keep the destination under the key. Four tile shapes, masked
 * and plain, random positions and clips, plane budgets 4, 2 and 3.
 */
static void test_tiles_wide(void)
{
    static const int shapes[4][2] = { { 16, 8 }, { 16, 16 }, { 32, 16 },
                                      { 48, 12 } };
    static const int budgets[3] = { 4, 2, 3 };
    int iter;

    for (iter = 0; iter < 300; iter++) {
        const int budget = budgets[iter % 3];
        const int maxcol = 1 << budget;
        const int tw = shapes[iter & 3][0], th = shapes[iter & 3][1];
        const int masked = (iter >> 2) & 1;
        const uint8_t key = (uint8_t)(maxcol - 2);
        STDL_Surface *img, *dst;
        STDL_Tileset *ts;
        STDL_Rect clip;
        Ref *want;
        int index, x, y, px, py, gx, bad = 0;

        STDL_SetPlaneBudget(budget);
        img = STDL_CreateSurface(tw * 2, th * 2);
        dst = STDL_CreateSurface(120, 60);
        randomise(img, maxcol);
        randomise(dst, maxcol);
        if (masked) {
            STDL_SetColourKey(img, 1, key);
        }
        ts = STDL_TilesetFromSurface(img, tw, th);
        if (ts == NULL) {
            CHECK(0, "tileset build");
            return;
        }
        index = (int)(rnd() % 4);
        x = (int)(rnd() % (120 + tw + 16)) - tw - 8;
        y = (int)(rnd() % (60 + th + 8)) - th - 4;
        clip.x = (int16_t)(rnd() % 40);
        clip.y = (int16_t)(rnd() % 20);
        clip.w = (uint16_t)(10 + rnd() % 90);
        clip.h = (uint16_t)(5 + rnd() % 45);
        STDL_SetClipRect(dst, &clip);
        want = ref_new(120, 60);
        surf_to_ref(dst, want);

        gx = (x >> 4) * 16;
        for (py = y; py < y + th; py++) {
            for (px = gx; px < gx + tw; px++) {
                int g0x = gx + ((px - gx) & ~15);
                int sx = (index % 2) * tw + (px - gx);
                int sy = (index / 2) * th + (py - y);
                uint8_t c = STDL_GetPixel(img, sx, sy);
                if (py < clip.y || py >= clip.y + clip.h
                    || g0x < clip.x || g0x + 15 >= clip.x + clip.w
                    || px < 0 || px >= 120 || py < 0 || py >= 60) {
                    continue;
                }
                if (masked && c == key) {
                    continue;
                }
                want->px[py * 120 + px] = c;
            }
        }
        STDL_BlitTile(ts, index, dst, x, y);
        for (py = 0; py < 60 && bad < 4; py++) {
            for (px = 0; px < 120 && bad < 4; px++) {
                if (STDL_GetPixel(dst, px, py) != want->px[py * 120 + px]) {
                    printf("  tile (%d,%d): got %d want %d [tw=%d th=%d "
                           "masked=%d np=%d x=%d y=%d clip=%d,%d %ux%u]\n",
                           px, py, STDL_GetPixel(dst, px, py),
                           want->px[py * 120 + px], tw, th, masked,
                           budget, x, y, clip.x, clip.y, clip.w, clip.h);
                    bad++;
                    failures++;
                }
            }
        }
        ref_free(want);
        STDL_FreeTileset(ts);
        STDL_FreeSurface(img);
        STDL_FreeSurface(dst);
        STDL_SetPlaneBudget(4);
        if (failures > 3) return;
    }
}

/*
 * Text against a per-pixel model: fonts 5, 8, 11 and 16 pixels wide
 * (one and two bytes a glyph row) and 6 to 13 high, strings at every
 * phase and partly off the surface, random clip rectangles, colours
 * above the budget (truncated), budgets 4, 2 and 3 - through
 * STDL_DrawText and STDL_DrawChar both, which share their row loop.
 */
static void test_text_model(void)
{
    static const int cws[4] = { 5, 8, 11, 16 };
    static const int chs[3] = { 6, 8, 13 };
    static const int budgets[3] = { 4, 2, 3 };
    int iter;

    for (iter = 0; iter < 240; iter++) {
        const int budget = budgets[iter % 3];
        const int cw = cws[(iter / 3) & 3], ch = chs[iter % 3];
        const int bpr = (cw + 7) >> 3;
        const int viachar = (iter >> 4) & 1;
        STDL_Font font;
        STDL_Surface *dst = STDL_CreateSurface(112, 48);
        Ref *want = ref_new(112, 48);
        STDL_Rect clip;
        char text[6];
        int k, x, y, col, px, py, bad = 0;

        font.cw = (int16_t)cw;
        font.ch = (int16_t)ch;
        font.first = 'A';
        font.last = 'Z';
        font.bytes_per_row = (uint16_t)bpr;
        font.bits = malloc((size_t)26 * ch * bpr);
        for (k = 0; k < 26 * ch * bpr; k++) {
            font.bits[k] = (uint8_t)rnd();
        }
        for (k = 0; k < 5; k++) {
            text[k] = (char)('A' + rnd() % 26);
        }
        text[5] = '\0';
        STDL_SetPlaneBudget(budget);
        randomise(dst, 1 << budget);
        clip.x = (int16_t)(rnd() % 30);
        clip.y = (int16_t)(rnd() % 12);
        clip.w = (uint16_t)(10 + rnd() % 90);
        clip.h = (uint16_t)(4 + rnd() % 36);
        STDL_SetClipRect(dst, &clip);
        clip = dst->clip;           /* as stored: within the surface */
        surf_to_ref(dst, want);
        x = (int)(rnd() % 130) - 20;
        y = (int)(rnd() % 60) - 10;
        col = (int)(rnd() % 16);

        for (k = 0; k < 5; k++) {
            const uint8_t *g = font.bits
                + (size_t)(text[k] - 'A') * ch * bpr;
            int r, c;
            for (r = 0; r < ch; r++) {
                for (c = 0; c < cw; c++) {
                    px = x + k * cw + c;
                    py = y + r;
                    if (!((g[r * bpr + (c >> 3)] >> (7 - (c & 7))) & 1)
                        || px < clip.x || px >= clip.x + clip.w
                        || py < clip.y || py >= clip.y + clip.h) {
                        continue;
                    }
                    want->px[py * 112 + px] =
                        (uint8_t)(col & ((1 << budget) - 1));
                }
            }
        }
        if (viachar) {
            for (k = 0; k < 5; k++) {
                STDL_DrawChar(dst, &font, x + k * cw, y, text[k],
                              (uint8_t)col);
            }
        } else {
            STDL_DrawText(dst, &font, x, y, text, (uint8_t)col);
        }
        for (py = 0; py < 48 && bad < 4; py++) {
            for (px = 0; px < 112 && bad < 4; px++) {
                if (STDL_GetPixel(dst, px, py) != want->px[py * 112 + px]) {
                    printf("  text (%d,%d): got %d want %d [cw=%d ch=%d "
                           "x=%d y=%d col=%d np=%d char=%d]\n", px, py,
                           STDL_GetPixel(dst, px, py),
                           want->px[py * 112 + px], cw, ch, x, y, col,
                           budget, viachar);
                    bad++;
                    failures++;
                }
            }
        }
        free(font.bits);
        ref_free(want);
        STDL_FreeSurface(dst);
        STDL_SetPlaneBudget(4);
        if (failures > 3) return;
    }
}

/* the algorithms STDL_Line/Circle/FillCircle replaced, as the
 * reference: a pixel or a row at a time, through calls whose own
 * semantics are tested elsewhere */
static void old_line(STDL_Surface *d, int x1, int y1, int x2, int y2,
                     uint8_t col)
{
    int dx, dy, sx, sy, err, e2;
    if (y1 == y2 || x1 == x2) {
        STDL_Rect r;
        r.x = (int16_t)(x1 < x2 ? x1 : x2);
        r.y = (int16_t)(y1 < y2 ? y1 : y2);
        r.w = (uint16_t)((x1 < x2 ? x2 - x1 : x1 - x2) + 1);
        r.h = (uint16_t)((y1 < y2 ? y2 - y1 : y1 - y2) + 1);
        STDL_FillRect(d, &r, col);
        return;
    }
    dx = x2 > x1 ? x2 - x1 : x1 - x2;
    dy = y2 > y1 ? y1 - y2 : y2 - y1;
    sx = x1 < x2 ? 1 : -1;
    sy = y1 < y2 ? 1 : -1;
    err = dx + dy;
    for (;;) {
        STDL_PutPixel(d, x1, y1, col);
        if (x1 == x2 && y1 == y2) break;
        e2 = err * 2;
        if (e2 >= dy) { err += dy; x1 += sx; }
        if (e2 <= dx) { err += dx; y1 += sy; }
    }
}

static void old_row(STDL_Surface *d, int x1, int x2, int y, uint8_t col)
{
    STDL_Rect r;
    r.x = (int16_t)x1;
    r.y = (int16_t)y;
    r.w = (uint16_t)(x2 - x1 + 1);
    r.h = 1;
    STDL_FillRect(d, &r, col);
}

static void old_circle(STDL_Surface *d, int cx, int cy, int r, int fill,
                       uint8_t col)
{
    int x = r, y = 0, err = 1 - r;
    while (x >= y) {
        if (fill) {
            old_row(d, cx - x, cx + x, cy + y, col);
            old_row(d, cx - x, cx + x, cy - y, col);
            old_row(d, cx - y, cx + y, cy + x, col);
            old_row(d, cx - y, cx + y, cy - x, col);
        } else {
            STDL_PutPixel(d, cx + x, cy + y, col);
            STDL_PutPixel(d, cx - x, cy + y, col);
            STDL_PutPixel(d, cx + x, cy - y, col);
            STDL_PutPixel(d, cx - x, cy - y, col);
            STDL_PutPixel(d, cx + y, cy + x, col);
            STDL_PutPixel(d, cx - y, cy + x, col);
            STDL_PutPixel(d, cx + y, cy - x, col);
            STDL_PutPixel(d, cx - y, cy - x, col);
        }
        y++;
        if (err < 0) {
            err += 2 * y + 1;
        } else {
            x--;
            err += 2 * (y - x) + 1;
        }
    }
}

/*
 * Lines and circles against the pixel-at-a-time algorithms they
 * replaced, byte for byte, pixels and masks: endpoints and centres
 * off the surface, random clips, colours including STDL_TRANSPARENT
 * on a masked surface, budgets 4, 2 and 3, radii up to past the
 * filled circle's half-width table.
 */
/* random pixels in the budget's planes, a byte at a time: PutPixel
 * per pixel takes seconds a surface on the target */
static void rand_planes(STDL_Surface *s, int budget)
{
    int y, k;

    for (y = 0; y < s->h; y++) {
        uint8_t *row = s->pixels + (size_t)y * s->stride;
        for (k = 0; k < s->stride; k++) {
            row[k] = ((k & 7) >> 1) < budget ? (uint8_t)rnd() : 0;
        }
    }
}

static void test_shapes_ref(void)
{
    static const int budgets[3] = { 4, 2, 3 };
#ifdef __m68k__
    const int iters = 150;      /* PIXCHK: minutes, not an hour */
#else
    const int iters = 600;
#endif
    int iter;

    for (iter = 0; iter < iters; iter++) {
        const int budget = budgets[iter % 3];
        const int shape = (iter / 3) % 5;
        /* rows wide enough for HLine's BLiTTER cut on the HLine
         * passes */
        const int sw = (shape == 3) ? 640 : 100;
        STDL_Surface *a = STDL_CreateSurface(sw, 70);
        STDL_Surface *b = STDL_CreateSurface(sw, 70);
        STDL_Rect clip;
        /* a third of them STDL_TRANSPARENT or above */
        uint8_t col = (uint8_t)(rnd() % 24);
        int k1 = (int)(rnd() % 160) - 30, k2 = (int)(rnd() % 120) - 25;
        int k3 = (int)(rnd() % 160) - 30, k4 = (int)(rnd() % 120) - 25;
        int r = (iter % 50 == 7) ? 300 + (int)(rnd() % 40)
                                 : (int)(rnd() % 60);

        STDL_SetPlaneBudget(budget);
        rand_planes(a, budget);
        if (iter & 1) {
            size_t k;
            STDL_CreateMask(a, 0);
            for (k = 0; k < (size_t)a->maskstride * a->h; k++) {
                a->mask[k] = (uint8_t)rnd();
            }
        }
        /* a copy of a, mask included (DuplicateSurface rebuilds a
         * mask from the colour key instead) */
        memcpy(b->pixels, a->pixels, (size_t)a->stride * a->h);
        if (a->mask != NULL) {
            STDL_CreateMask(b, 0);
            memcpy(b->mask, a->mask, (size_t)a->maskstride * a->h);
        }
        clip.x = (int16_t)(rnd() % 30);
        clip.y = (int16_t)(rnd() % 20);
        clip.w = (uint16_t)(5 + rnd() % (sw - 10));
        clip.h = (uint16_t)(5 + rnd() % 60);
        STDL_SetClipRect(a, &clip);
        STDL_SetClipRect(b, &clip);
        if (shape == 0) {
            STDL_Line(a, k1, k2, k3, k4, col);
            old_line(b, k1, k2, k3, k4, col);
        } else if (shape == 3) {
            /* ends either way round, widths past the BLiTTER cut */
            int h1 = (int)(rnd() % 700) - 40, h2 = (int)(rnd() % 700) - 40;
            STDL_HLine(a, h1, h2, k2, col);
            old_row(b, h1 < h2 ? h1 : h2, h1 < h2 ? h2 : h1, k2, col);
        } else if (shape == 4) {
            STDL_VLine(a, k1, k2, k4, col);
            old_line(b, k1, k2, k1, k4, col);
        } else {
            STDL_Circle(a, k1, k2, r, col);   /* replaced below */
            if (shape == 2) {
                memcpy(a->pixels, b->pixels, (size_t)b->stride * b->h);
                if (a->mask != NULL) {
                    memcpy(a->mask, b->mask, (size_t)b->maskstride * b->h);
                }
                STDL_FillCircle(a, k1, k2, r, col);
            }
            old_circle(b, k1, k2, r, shape == 2, col);
        }
        CHECK(memcmp(a->pixels, b->pixels, (size_t)a->stride * a->h) == 0
              && (a->mask == NULL
                  || memcmp(a->mask, b->mask,
                            (size_t)a->maskstride * a->h) == 0),
              "shape %d (%d,%d)-(%d,%d) r=%d col=%d np=%d mask=%d differs",
              shape, k1, k2, k3, k4, r, col, budget, a->mask != NULL);
        STDL_FreeSurface(a);
        STDL_FreeSurface(b);
        STDL_SetPlaneBudget(4);
        if (failures > 3) return;
    }
}

int main(void)
{
    test_putget();
    test_fills();
    test_fill_blocks();
    test_hvlines();
    test_xor();
    test_spans();
    test_points();
    test_blits();
    test_partial_middle();
    test_shift_plain();
    test_whole_blit_writeback();
    test_whole_copy();
    test_sprites();
    test_sprites_wide();
    test_1bpp();
    test_tiles();
    test_tiles_wide();
    test_text_model();
    test_shapes_ref();
    if (failures == 0) {
        printf("all pixel-path tests passed\n");
        return 0;
    }
    printf("%d failure(s)\n", failures);
    return 1;
}
