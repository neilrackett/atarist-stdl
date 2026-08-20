/*
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
/*
 * STDL_BlitIndexed8 against a per-pixel reference model, and the
 * STDL_CreateSurfaceFrom ownership rules it was built to pair with.
 *
 * The model mirrors the contract: source value 0 transparent,
 * map[c & 15] otherwise, the four source-walk variants, clipping
 * against the destination clip rectangle, and the three mask
 * behaviours (maintain = clear under drawn pixels, MARK = set,
 * UNDER = mask bits protect their pixels).
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

#define DW 64
#define DH 32

/* reference model: pixels and mask bits, byte per pixel */
typedef struct {
    uint8_t px[DW * DH];
    uint8_t mk[DW * DH];        /* 1 = preserved */
} Ref;

static int mask_bit(const STDL_Surface *s, int x, int y)
{
    const uint16_t *m =
        (const uint16_t *)(s->mask + (uint32_t)y * s->maskstride);
    return (m[x >> 4] >> (15 - (x & 15))) & 1;
}

static void surf_to_ref(const STDL_Surface *s, Ref *r)
{
    int x, y;
    for (y = 0; y < s->h; y++) {
        for (x = 0; x < s->w; x++) {
            r->px[y * DW + x] = STDL_GetPixel(s, x, y);
            r->mk[y * DW + x] =
                s->mask ? (uint8_t)mask_bit(s, x, y) : 0;
        }
    }
}

static int ref_cmp(const STDL_Surface *s, const Ref *r, const char *what)
{
    int x, y, bad = 0;
    for (y = 0; y < s->h && bad < 5; y++) {
        for (x = 0; x < s->w && bad < 5; x++) {
            uint8_t got = STDL_GetPixel(s, x, y);
            uint8_t want = r->px[y * DW + x];
            if (got != want) {
                printf("  %s pixel (%d,%d): got %d want %d\n",
                       what, x, y, got, want);
                bad++;
            }
            if (s->mask != NULL) {
                int gm = mask_bit(s, x, y);
                if (gm != r->mk[y * DW + x]) {
                    printf("  %s mask (%d,%d): got %d want %d\n",
                           what, x, y, gm, r->mk[y * DW + x]);
                    bad++;
                }
            }
        }
    }
    return bad == 0;
}

/* the model blit */
static void ref_blit8(Ref *r, const STDL_Rect *clip, const uint8_t *src,
                      int pitch, int x, int y, int w, int h,
                      const uint8_t *map, unsigned flags)
{
    int i, j;

    for (j = 0; j < h; j++) {
        for (i = 0; i < w; i++) {
            int dx = x + i, dy = y + j;
            long so;
            uint8_t c;

            if (dx < clip->x || dx >= clip->x + clip->w
                || dy < clip->y || dy >= clip->y + clip->h) {
                continue;
            }
            if (flags & STDL_I8_COLMAJOR) {
                so = (flags & STDL_I8_XFLIP)
                   ? -(long)i * pitch + j : (long)i * pitch + j;
            } else {
                so = (flags & STDL_I8_XFLIP)
                   ? (long)j * pitch - i : (long)j * pitch + i;
            }
            c = src[so];
            if (c == 0) {
                continue;
            }
            if ((flags & STDL_I8_UNDER) && r->mk[dy * DW + dx]) {
                continue;
            }
            r->px[dy * DW + dx] = map[c & 15];
            r->mk[dy * DW + dx] = (flags & STDL_I8_MARK) ? 1 : 0;
        }
    }
}

/* deterministic chunky pattern with transparent holes */
static uint8_t sprite[32 * 32];

static void sprite_init(void)
{
    unsigned seed = 99;
    size_t i;
    for (i = 0; i < sizeof(sprite); i++) {
        seed = seed * 1103515245u + 12345u;
        sprite[i] = (uint8_t)((seed >> 16) % 5 == 0
                              ? 0 : ((seed >> 20) & 15));
    }
}

static const uint8_t map_id[16] =
    { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 };
static const uint8_t map_swap[16] =
    { 5, 14, 3, 9, 0, 12, 7, 1, 11, 2, 15, 4, 13, 6, 8, 10 };

static void checker_fill(STDL_Surface *s)
{
    int x, y;
    STDL_Rect rc;
    for (y = 0; y < s->h; y++) {
        for (x = 0; x < s->w; x += 8) {
            rc.x = (int16_t)x; rc.y = (int16_t)y;
            rc.w = 8; rc.h = 1;
            STDL_FillRect(s, &rc, ((x >> 3) + y) & 15);
        }
    }
}

/* run one case against the model on the given surface */
static void run_case(STDL_Surface *s, const char *what,
                     const uint8_t *src, int pitch, int x, int y,
                     int w, int h, const uint8_t *map, unsigned flags)
{
    Ref r;

    surf_to_ref(s, &r);
    ref_blit8(&r, &s->clip, src, pitch, x, y, w, h, map, flags);
    STDL_BlitIndexed8(s, src, pitch, x, y, w, h, map, flags);
    CHECK(ref_cmp(s, &r, what), "%s", what);
}

static void cases_on(STDL_Surface *s, int masked)
{
    /* interior draw, all four source walks */
    run_case(s, "plain", sprite, 32, 5, 3, 16, 12, map_swap, 0);
    run_case(s, "xflip", sprite + 31, 32, 9, 7, 16, 12, map_swap,
             STDL_I8_XFLIP);
    run_case(s, "colmajor", sprite, 32, 21, 2, 12, 16, map_id,
             STDL_I8_COLMAJOR);
    run_case(s, "cm+xflip", sprite + 20 * 32, 32, 3, 11, 12, 16,
             map_swap, STDL_I8_COLMAJOR | STDL_I8_XFLIP);

    /* clipping: off every edge (source biased like a caller that
     * clips nothing and trusts the blit) */
    run_case(s, "clip left", sprite + 8, 32, -6, 4, 20, 10, map_id, 0);
    run_case(s, "clip right", sprite, 32, DW - 7, 2, 20, 10, map_id, 0);
    run_case(s, "clip top", sprite, 32, 4, -5, 16, 16, map_id, 0);
    run_case(s, "clip bottom", sprite, 32, 4, DH - 6, 16, 16, map_id, 0);
    run_case(s, "clip xflip", sprite + 31, 32, -4, 1, 20, 10, map_id,
             STDL_I8_XFLIP);
    run_case(s, "clip cm", sprite, 32, -3, -2, 14, 14, map_id,
             STDL_I8_COLMAJOR);

    /* a clip rectangle narrower than the blit */
    {
        STDL_Rect rc = { 10, 6, 20, 12 };
        STDL_SetClipRect(s, &rc);
        run_case(s, "clip rect", sprite, 32, 6, 2, 24, 20, map_swap, 0);
        STDL_SetClipRect(s, NULL);
    }

    if (masked) {
        run_case(s, "mark", sprite, 32, 8, 8, 16, 12, map_swap,
                 STDL_I8_MARK);
        /* the marked pixels now protect against an UNDER draw */
        run_case(s, "under", sprite, 32, 12, 10, 16, 12, map_id,
                 STDL_I8_UNDER);
        /* default maintenance clears mask under drawn pixels */
        run_case(s, "maintain", sprite, 32, 10, 9, 16, 12, map_id, 0);
        run_case(s, "under+mark", sprite, 32, 11, 11, 16, 12, map_swap,
                 STDL_I8_UNDER | STDL_I8_MARK);
    }
}

int main(void)
{
    STDL_Surface *s;
    uint8_t *block;
    uint8_t *mask;
    int groups = DW / 16;

    STDL_Init(0);
    sprite_init();

    /* library-owned surface, unmasked then masked */
    s = STDL_CreateSurface(DW, DH);
    checker_fill(s);
    cases_on(s, 0);
    STDL_CreateMask(s, 0);      /* all-opaque mask */
    cases_on(s, 1);
    STDL_FreeSurface(s);

    /* borrowed surface: caller-owned pixels and mask */
    block = calloc(1, (size_t)groups * 8 * DH + 16);
    mask = calloc(1, (size_t)groups * 2 * DH + 16);
    s = STDL_CreateSurfaceFrom(block + 8, DW, DH, groups * 8,
                               mask + 8, groups * 2);
    CHECK(s != NULL, "CreateSurfaceFrom failed: %s", STDL_GetError());
    if (s != NULL) {
        CHECK((s->flags & STDL_PREALLOC) != 0, "PREALLOC flag missing");
        CHECK((s->flags & STDL_SRCKEY) != 0, "mask should enable SRCKEY");
        checker_fill(s);
        cases_on(s, 1);

        /* disabling the key must not free the borrowed mask (ASan
         * would fault on the invalid free) */
        STDL_SetColourKey(s, 0, 0);
        CHECK(s->mask == NULL, "key disable should detach the mask");
        /* the library must refuse to allocate a mask it would leak */
        CHECK(STDL_CreateMask(s, 0) < 0,
              "CreateMask on a borrowed surface should fail");
        STDL_FreeSurface(s);    /* frees the header only */
    }

    /* invalid parameter rejection */
    CHECK(STDL_CreateSurfaceFrom(NULL, DW, DH, 32, NULL, 0) == NULL,
          "NULL pixels accepted");
    CHECK(STDL_CreateSurfaceFrom(block + 8, DW, DH, 30, NULL, 0) == NULL,
          "unaligned stride accepted");
    CHECK(STDL_CreateSurfaceFrom(block + 9, DW, DH, 32, NULL, 0) == NULL,
          "odd base address accepted");

    free(block);
    free(mask);

    if (failures) {
        printf("%d FAILURES\n", failures);
        return 1;
    }
    printf("test_blit8: OK\n");
    return 0;
}
