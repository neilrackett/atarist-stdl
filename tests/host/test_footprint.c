/*
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
/*
 * Per-surface heap footprint.
 *
 * The pixel tests all pass on a machine with gigabytes of RAM, so
 * nothing in this suite noticed that STDL_CreateSurface took three
 * malloc blocks for the surface, its format and its palette. On a
 * 1M ST that is the budget that matters: a tile-per-surface game
 * (FreeNukum keeps 1259 surfaces alive, most of them 16x16) pays
 * more in allocator headers than a tile's 128 bytes of pixels, and
 * when the heap runs out STDL_CreateSurface returns NULL and the
 * port dereferences it.
 *
 * So this asserts the shape the allocator sees, not the pixels:
 * a surface's metadata is ONE block, its pixels are a second, and
 * a mask - if asked for - is the third and last. Freeing must give
 * all of it back, which ASan checks by leaking loudly.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdl/stdl.h>

static int failures;
#define CHECK(c, ...) do { if (!(c)) { failures++; \
    printf("FAIL %d: ", __LINE__); printf(__VA_ARGS__); \
    printf("\n"); } } while (0)

/* metadata block: surface header, pixel format, palette, colours */
#define META_BYTES (sizeof(STDL_Surface) + sizeof(STDL_PixelFormat) \
                    + sizeof(STDL_Palette) + 16 * sizeof(STDL_Colour))

static void check_one_block(STDL_Surface *s, const char *what)
{
    const uint8_t *base = (const uint8_t *)s;
    const uint8_t *end = base + META_BYTES;
    const uint8_t *fmt = (const uint8_t *)s->format;
    const uint8_t *pal = (const uint8_t *)s->format->palette;
    const uint8_t *col = (const uint8_t *)s->format->palette->colors;

    /* every piece of metadata lives inside the surface's own block:
     * one allocation, not three */
    CHECK(fmt >= base && fmt + sizeof(STDL_PixelFormat) <= end,
          "%s: format outside the surface block", what);
    CHECK(pal >= base && pal + sizeof(STDL_Palette) <= end,
          "%s: palette outside the surface block", what);
    CHECK(col >= base && col + 16 * sizeof(STDL_Colour) <= end,
          "%s: colours outside the surface block", what);

    /* and the pixels are their own block, not part of it */
    CHECK(s->pixels < base || s->pixels >= end,
          "%s: pixels inside the metadata block", what);
}

int main(void)
{
    STDL_Surface *s, *d;
    int i, n;

    /* the case that matters: many small surfaces, as a tile cache */
    s = STDL_CreateSurface(16, 16);
    CHECK(s != NULL, "16x16 create: %s", STDL_GetError());
    if (s == NULL) {
        return 1;
    }
    check_one_block(s, "16x16");

    /* the metadata block must not grow with the surface */
    d = STDL_CreateSurface(320, 200);
    CHECK(d != NULL, "320x200 create: %s", STDL_GetError());
    if (d != NULL) {
        check_one_block(d, "320x200");
        STDL_FreeSurface(d);
    }

    /* the palette is per surface and writable in place, so sharing
     * one block must not have made it shared state */
    d = STDL_CreateSurface(16, 16);
    CHECK(d != NULL, "second 16x16 create");
    if (d != NULL) {
        CHECK(d->format != s->format, "formats are per surface");
        CHECK(d->format->palette != s->format->palette,
              "palettes are per surface");
        s->format->palette->colors[3].r = 0x11;
        d->format->palette->colors[3].r = 0x22;
        CHECK(s->format->palette->colors[3].r == 0x11,
              "palette write bled between surfaces");
        CHECK(d->format->palette->ncolors == 16, "ncolors set");
        STDL_FreeSurface(d);
    }

    /* a mask is the third block and no more; adding and dropping it
     * repeatedly must not leak (ASan reports it if it does) */
    for (i = 0; i < 8; i++) {
        CHECK(STDL_SetColourKey(s, 1, 0) == 0, "key on");
        CHECK(s->mask != NULL, "mask allocated");
        check_one_block(s, "keyed 16x16");
        CHECK(STDL_SetColourKey(s, 0, 0) == 0, "key off");
        CHECK(s->mask == NULL, "mask dropped");
    }
    STDL_FreeSurface(s);

    /* duplicates and 1bpp expansions go through the same allocator
     * path and must keep the same shape */
    s = STDL_CreateSurface(48, 24);
    STDL_SetColourKey(s, 1, 2);
    d = STDL_DuplicateSurface(s);
    CHECK(d != NULL, "duplicate");
    if (d != NULL) {
        check_one_block(d, "duplicate");
        STDL_FreeSurface(d);
    }
    STDL_FreeSurface(s);

    {
        static const uint8_t bits[8] = {
            0xF0, 0x0F, 0xCC, 0x33, 0xAA, 0x55, 0xFF, 0x00
        };
        d = STDL_SurfaceFrom1bpp(bits, 8, 8, 1, 0);
        CHECK(d != NULL, "1bpp surface");
        if (d != NULL) {
            check_one_block(d, "1bpp");
            STDL_FreeSurface(d);
        }
    }

    /* churn: a tile cache's worth of create/free must round-trip */
    n = 256;
    {
        STDL_Surface **tiles = malloc((size_t)n * sizeof(*tiles));
        CHECK(tiles != NULL, "test allocation");
        if (tiles != NULL) {
            for (i = 0; i < n; i++) {
                tiles[i] = STDL_CreateSurface(16, 16);
                CHECK(tiles[i] != NULL, "tile %d", i);
                if (tiles[i] == NULL) {
                    break;
                }
                STDL_SetColourKey(tiles[i], 1, 0);
            }
            for (i = 0; i < n; i++) {
                STDL_FreeSurface(tiles[i]);
            }
            free(tiles);
        }
    }

    if (failures == 0) {
        printf("surface footprint tests passed\n");
        return 0;
    }
    printf("%d failure(s)\n", failures);
    return 1;
}
