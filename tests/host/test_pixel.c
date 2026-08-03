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
        uint8_t col = (uint8_t)(rnd() % 17);   /* 16 = TRANSPARENT */
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

int main(void)
{
    test_putget();
    test_fills();
    test_hvlines();
    test_xor();
    test_spans();
    test_points();
    test_blits();
    test_whole_blit_writeback();
    test_sprites();
    test_1bpp();
    test_tiles();
    if (failures == 0) {
        printf("all pixel-path tests passed\n");
        return 0;
    }
    printf("%d failure(s)\n", failures);
    return 1;
}
