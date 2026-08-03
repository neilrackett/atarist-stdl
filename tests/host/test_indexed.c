/*
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
/*
 * Byte-per-pixel conversion and the colour remap.
 *
 * Both are checked against a per-pixel reference built from
 * STDL_GetPixel rather than against themselves: the point of
 * STDL_SurfaceFromIndexed8 is that a port stops hand-rolling the
 * group arithmetic, so the test has to state what the pixels are
 * supposed to be, not repeat the same arithmetic and agree with it.
 *
 * The awkward cases are the ones ports hit: widths that are not a
 * multiple of 16 (padding must be transparent, never drawn), a
 * source stride wider than the frame (lifting one sprite out of a
 * sheet), and a colour key that has to become a mask rather than a
 * pixel value.
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

static unsigned rng_state = 7;
static unsigned rnd(void)
{
    rng_state = rng_state * 1103515245u + 12345u;
    return (rng_state >> 16) & 0x7FFF;
}

/* mask bit for pixel (x, y): set = transparent */
static int mask_bit(const STDL_Surface *s, int x, int y)
{
    const uint16_t *mrow;

    if (s->mask == NULL) {
        return 0;
    }
    mrow = (const uint16_t *)(s->mask + (size_t)y * s->maskstride);
    return (mrow[x >> 4] >> (15 - (x & 15))) & 1;
}

/* ---------------------------------------------------------------- */

static void test_convert(int w, int h, int stride, int key)
{
    uint8_t *src;
    STDL_Surface *s;
    int x, y, bad = 0;

    src = malloc((size_t)stride * h);
    for (y = 0; y < h; y++) {
        for (x = 0; x < stride; x++) {
            src[(size_t)y * stride + x] = (uint8_t)(rnd() & 15);
        }
    }

    s = STDL_SurfaceFromIndexed8(src, w, h, stride, key);
    CHECK(s != NULL, "convert %dx%d returned NULL", w, h);
    if (s == NULL) {
        free(src);
        return;
    }
    CHECK(s->w == w && s->h == h, "size %dx%d", s->w, s->h);
    CHECK((key >= 0) == (s->mask != NULL),
          "mask presence wrong for key %d", key);

    for (y = 0; y < h && !bad; y++) {
        for (x = 0; x < w; x++) {
            uint8_t want = src[(size_t)y * stride + x];
            int wantmask = (key >= 0 && want == (uint8_t)key);
            uint8_t got = STDL_GetPixel(s, x, y);

            if (wantmask) {
                if (mask_bit(s, x, y) != 1) {
                    CHECK(0, "keyed pixel %d,%d not transparent", x, y);
                    bad = 1;
                    break;
                }
            } else {
                if (got != want || mask_bit(s, x, y) != 0) {
                    CHECK(0, "pixel %d,%d = %u want %u (mask %d)",
                          x, y, got, want, mask_bit(s, x, y));
                    bad = 1;
                    break;
                }
            }
        }
    }

    /* padding out to the group boundary is colour 0, and transparent
     * whenever the surface carries a mask at all */
    for (y = 0; y < h && !bad; y++) {
        for (x = w; x < ((w + 15) & ~15); x++) {
            if (STDL_GetPixel(s, x, y) != 0) {
                CHECK(0, "padding pixel %d,%d not 0", x, y);
                bad = 1;
                break;
            }
            if (key >= 0 && mask_bit(s, x, y) != 1) {
                CHECK(0, "padding pixel %d,%d not transparent", x, y);
                bad = 1;
                break;
            }
        }
    }
    STDL_FreeSurface(s);
    free(src);
}

/* a source value the surface cannot hold must not corrupt the
 * neighbours: it truncates, exactly as every other writer does */
static void test_convert_truncates(void)
{
    uint8_t src[16];
    STDL_Surface *s;
    int i;

    for (i = 0; i < 16; i++) {
        src[i] = (uint8_t)(0xF0 | i);       /* 240..255 */
    }
    s = STDL_SurfaceFromIndexed8(src, 16, 1, 16, -1);
    CHECK(s != NULL, "truncation surface");
    if (s == NULL) {
        return;
    }
    for (i = 0; i < 16; i++) {
        CHECK(STDL_GetPixel(s, i, 0) == (uint8_t)i,
              "byte %u -> %u, want %d", src[i],
              STDL_GetPixel(s, i, 0), i);
    }
    STDL_FreeSurface(s);
}

/* ---------------------------------------------------------------- */

static void test_remap(const uint8_t *map, int budget)
{
    enum { W = 40, H = 9 };
    uint8_t src[W * H];
    uint8_t want[W * H];
    STDL_Surface *s;
    int x, y, i, ncols = 1 << budget;

    for (i = 0; i < W * H; i++) {
        src[i] = (uint8_t)(rnd() % ncols);
    }
    STDL_SetPlaneBudget(budget);
    s = STDL_SurfaceFromIndexed8(src, W, H, W, 0);
    CHECK(s != NULL, "remap source surface");
    if (s == NULL) {
        STDL_SetPlaneBudget(4);
        return;
    }
    for (i = 0; i < W * H; i++) {
        want[i] = (uint8_t)(map[src[i]] & (ncols - 1));
    }

    STDL_RemapSurface(s, map);

    for (y = 0; y < H; y++) {
        for (x = 0; x < W; x++) {
            uint8_t got = STDL_GetPixel(s, x, y);
            uint8_t exp = want[y * W + x];

            if (got != exp) {
                CHECK(0, "budget %d: pixel %d,%d = %u want %u "
                      "(src %u)", budget, x, y, got, exp,
                      src[y * W + x]);
                y = H;
                break;
            }
            /* the mask is bookkeeping about transparency, not
             * colour: remapping must leave it exactly alone */
            if (mask_bit(s, x, y) != (src[y * W + x] == 0)) {
                CHECK(0, "budget %d: mask moved at %d,%d",
                      budget, x, y);
                y = H;
                break;
            }
        }
    }
    STDL_FreeSurface(s);
    STDL_SetPlaneBudget(4);
}

int main(void)
{
    static const uint8_t identity[16] = {
        0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15
    };
    static const uint8_t swap01[16] = {   /* a plane swap */
        0, 2, 1, 3, 4, 6, 5, 7, 8, 10, 9, 11, 12, 14, 13, 15
    };
    static const uint8_t collapse[16] = { /* not a permutation */
        0, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3
    };
    static const uint8_t shuffle[16] = {
        5, 12, 0, 9, 1, 15, 3, 3, 8, 8, 7, 2, 14, 6, 11, 10
    };
    int i;

    /* exact multiple of 16, an awkward width, one pixel wide, and a
     * frame lifted out of a wider sheet */
    test_convert(32, 5, 32, -1);
    test_convert(32, 5, 32, 0);
    test_convert(37, 11, 37, 7);
    test_convert(1, 1, 1, -1);
    test_convert(17, 3, 64, 15);
    test_convert(64, 2, 200, 0);
    test_convert_truncates();

    /* NULL data is a caller error, not a crash */
    CHECK(STDL_SurfaceFromIndexed8(NULL, 8, 8, 8, -1) == NULL,
          "NULL data accepted");

    for (i = 1; i <= 4; i++) {
        test_remap(identity, i);
        test_remap(swap01, i);
        test_remap(collapse, i);
        test_remap(shuffle, i);
    }

    /* a NULL map is a no-op, not a crash */
    {
        uint8_t src[16] = { 1, 2, 3, 4, 5, 6, 7, 8,
                            9, 10, 11, 12, 13, 14, 15, 0 };
        STDL_Surface *s = STDL_SurfaceFromIndexed8(src, 16, 1, 16, -1);
        STDL_RemapSurface(s, NULL);
        STDL_RemapSurface(NULL, identity);
        for (i = 0; i < 16; i++) {
            CHECK(STDL_GetPixel(s, i, 0) == src[i],
                  "NULL map changed pixel %d", i);
        }
        STDL_FreeSurface(s);
    }

    if (failures == 0) {
        printf("indexed-source tests passed\n");
    }
    return failures != 0;
}
