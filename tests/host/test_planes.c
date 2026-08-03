/*
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
/*
 * Plane budget: a reduced budget must be a pure optimisation.
 *
 * The same deterministic drawing script - fills, spans, blits
 * (aligned, unaligned, masked, clipped), sprites, tiles, text and
 * the XOR ops - is run once at budget 4 and once at budget 1, 2 and
 * 3, using only colours the budget can express. Every run must
 * produce byte-identical pixel *and* mask blocks: the low planes
 * because the budget must not change what is drawn, the high planes
 * because the budget's whole premise is that they were already
 * zero, and the masks because they are plane-independent bookkeeping
 * that a budget must not disturb.
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

static unsigned rng_state;
static unsigned rnd(void)
{
    rng_state = rng_state * 1103515245u + 12345u;
    return (rng_state >> 16) & 0x7FFF;
}

#define DW 96
#define DH 64

typedef struct {
    uint8_t  px[DW / 16 * 8 * DH];
    uint8_t  mk[DW / 16 * 2 * DH];
    uint32_t pxlen, mklen;
} Snap;

static uint8_t glyph_bits[16 * 8];
static STDL_Font font;

static void font_init(void)
{
    unsigned seed = 99;
    size_t i;

    for (i = 0; i < sizeof(glyph_bits); i++) {
        seed = seed * 1103515245u + 12345u;
        glyph_bits[i] = (uint8_t)(seed >> 20);
    }
    font.cw = 8;
    font.ch = 8;
    font.first = 0;
    font.last = 15;
    font.bytes_per_row = 1;
    font.bits = glyph_bits;
}

static void randomise(STDL_Surface *s, int maxcol)
{
    int x, y;

    for (y = 0; y < s->h; y++) {
        for (x = 0; x < s->w; x++) {
            STDL_PutPixel(s, x, y, (uint8_t)(rnd() % maxcol));
        }
    }
}

/*
 * The script. maxcol is the exclusive colour limit the budget can
 * express (2^budget); everything drawn stays inside it, which is
 * exactly the promise STDL_SetPlaneBudget takes.
 */
static void render(int budget, int maxcol, Snap *out)
{
    STDL_Surface *dst, *src, *keyed, *img, *tiles;
    STDL_Sprite *spr, *pspr;
    STDL_Tileset *ts;
    STDL_Rect r, sr, clip;
    int i;

    STDL_SetPlaneBudget(budget);
    rng_state = 20260803u;

    dst = STDL_CreateSurface(DW, DH);
    STDL_CreateMask(dst, 0);
    src = STDL_CreateSurface(48, 24);
    keyed = STDL_CreateSurface(48, 24);
    img = STDL_CreateSurface(32, 16);
    tiles = STDL_CreateSurface(32, 32);

    randomise(src, maxcol);
    randomise(keyed, maxcol);
    randomise(img, maxcol);
    randomise(tiles, maxcol);
    STDL_SetColourKey(keyed, 1, (uint8_t)(maxcol > 1 ? 1 : 0));
    STDL_SetColourKey(img, 1, 0);
    spr = STDL_SpriteFromSurface(img, 32, 0);
    pspr = STDL_SpriteFromSurface(img, 32, STDL_PRESHIFT);
    ts = STDL_TilesetFromSurface(tiles, 16, 16);

    /* whole-surface fill: the memset fast path, colour 0 and top */
    STDL_FillRect(dst, NULL, 0);
    STDL_FillRect(dst, NULL, (uint8_t)(maxcol - 1));
    STDL_FillRect(dst, NULL, 0);

    /* PutGroup / PutGroup8 place whole groups; the words above the
     * budget are left zero, as a legal caller would */
    {
        static const uint16_t pat[4] =
            { 0xF0F0u, 0x3333u, 0x0FF0u, 0x55AAu };
        static const uint8_t bpat[4] = { 0xC3u, 0x5Au, 0x0Fu, 0x3Cu };
        uint16_t planes[4] = { 0, 0, 0, 0 };
        uint8_t  bytes[4]  = { 0, 0, 0, 0 };
        int p;

        for (p = 0; p < 4; p++) {
            if ((maxcol - 1) & (1 << p)) {
                planes[p] = pat[p];
                bytes[p] = bpat[p];
            }
        }
        STDL_PutGroup(dst, 16, 3, planes, 0x00FFu);
        STDL_PutGroup8(dst, 40, 5, bytes, 0x0Fu);
    }

    for (i = 0; i < 120; i++) {
        uint8_t col = (uint8_t)(rnd() % maxcol);
        int op = (int)(rnd() % 15);
        STDL_Span sp[8];

        switch (op) {
        case 0:                                     /* rect fill */
            r.x = (int16_t)((int)(rnd() % 120) - 20);
            r.y = (int16_t)((int)(rnd() % 80) - 10);
            r.w = (uint16_t)(rnd() % 90);
            r.h = (uint16_t)(rnd() % 50);
            STDL_FillRect(dst, &r, col);
            break;
        case 1:                                     /* transparent */
            r.x = (int16_t)(rnd() % 80);
            r.y = (int16_t)(rnd() % 50);
            r.w = (uint16_t)(rnd() % 40);
            r.h = (uint16_t)(rnd() % 20);
            STDL_FillRect(dst, &r, STDL_TRANSPARENT);
            break;
        case 2:                                     /* spans */
            STDL_HLine(dst, (int)(rnd() % 110) - 10,
                       (int)(rnd() % 110) - 10,
                       (int)(rnd() % 70) - 3, col);
            STDL_VLine(dst, (int)(rnd() % 110) - 10,
                       (int)(rnd() % 70) - 3,
                       (int)(rnd() % 70) - 3, col);
            break;
        case 3:                                     /* pixels/lines */
            STDL_PutPixel(dst, (int)(rnd() % 100),
                          (int)(rnd() % 70), col);
            STDL_Line(dst, (int)(rnd() % 100), (int)(rnd() % 70),
                      (int)(rnd() % 100), (int)(rnd() % 70), col);
            break;
        case 4:                                     /* circles */
            STDL_Circle(dst, (int)(rnd() % 100), (int)(rnd() % 70),
                        (int)(rnd() % 20), col);
            STDL_FillCircle(dst, (int)(rnd() % 100),
                            (int)(rnd() % 70),
                            (int)(rnd() % 15), col);
            break;
        case 5:                                     /* plain blit */
        case 6:                                     /* keyed blit */
            sr.x = (int16_t)(rnd() % 40);
            sr.y = (int16_t)(rnd() % 16);
            sr.w = (uint16_t)(rnd() % 48);
            sr.h = (uint16_t)(rnd() % 24);
            r.x = (int16_t)((int)(rnd() % 110) - 10);
            r.y = (int16_t)((int)(rnd() % 70) - 5);
            STDL_BlitSurface(op == 5 ? src : keyed, &sr, dst, &r);
            break;
        case 7:                                     /* aligned blit */
            sr.x = (int16_t)((rnd() % 3) * 16);
            sr.y = (int16_t)(rnd() % 16);
            sr.w = 48;
            sr.h = (uint16_t)(rnd() % 24);
            r.x = (int16_t)((rnd() % 5) * 16);
            r.y = (int16_t)(rnd() % 40);
            STDL_BlitSurface(src, &sr, dst, &r);
            break;
        case 8:                                     /* sprites */
            STDL_BlitSprite(spr, 0, dst,
                            (int)(rnd() % 110) - 10,
                            (int)(rnd() % 70) - 5);
            STDL_BlitSprite(pspr, 0, dst,
                            (int)(rnd() % 110) - 10,
                            (int)(rnd() % 70) - 5);
            break;
        case 9:                                     /* tiles */
            STDL_BlitTile(ts, (int)(rnd() % 4), dst,
                          (int)(rnd() % 100) - 8,
                          (int)(rnd() % 60) - 4);
            break;
        case 10:                                    /* text */
            STDL_DrawText(dst, &font,
                          (int)(rnd() % 110) - 10,
                          (int)(rnd() % 70) - 4,
                          "\1\2\3\4\5\6\7\10", col);
            break;
        case 11:                                    /* span lists */
        case 12:
            {
                int k;
                for (k = 0; k < 8; k++) {
                    sp[k].x = (int16_t)((int)(rnd() % 110) - 10);
                    sp[k].y = (int16_t)((int)(rnd() % 74) - 6);
                    sp[k].len = (int16_t)((int)(rnd() % 24) - 2);
                }
                if (op == 11) {
                    STDL_VSpans(dst, sp, 8, col);
                    STDL_HSpans(dst, sp, 8,
                                (uint8_t)((rnd() & 3) ? col
                                          : STDL_TRANSPARENT));
                } else {
                    STDL_XorVSpans(dst, sp, 8, col);
                    STDL_XorHSpans(dst, sp, 8, col);
                }
            }
            break;
        case 13:                                    /* point lists */
            {
                STDL_Point pt[10];
                uint8_t pc[10];
                int k;
                for (k = 0; k < 10; k++) {
                    pt[k].x = (int16_t)((int)(rnd() % 108) - 6);
                    pt[k].y = (int16_t)((int)(rnd() % 72) - 4);
                    pc[k] = (uint8_t)(rnd() % maxcol);
                }
                STDL_Points(dst, pt, 10, col);
                STDL_PointsC(dst, pt, pc, 10);
                STDL_Points(dst, pt, 10, 0);   /* the clear-only path */
            }
            break;
        default:                                    /* XOR ops */
            r.x = (int16_t)((int)(rnd() % 100) - 10);
            r.y = (int16_t)((int)(rnd() % 60) - 5);
            r.w = (uint16_t)(rnd() % 60);
            r.h = (uint16_t)(rnd() % 30);
            STDL_XorRect(dst, &r, col);
            STDL_XorHLine(dst, (int)(rnd() % 110) - 10,
                          (int)(rnd() % 110) - 10,
                          (int)(rnd() % 70) - 3, col);
            STDL_XorVLine(dst, (int)(rnd() % 110) - 10,
                          (int)(rnd() % 70) - 3,
                          (int)(rnd() % 70) - 3, col);
            STDL_XorPixel(dst, (int)(rnd() % 100),
                          (int)(rnd() % 60), col);
            break;
        }

        /* a clip rect for a quarter of the iterations, so the edge
         * groups and the peeled first/last rows get exercised */
        if ((i & 3) == 0) {
            clip.x = (int16_t)(rnd() % 24);
            clip.y = (int16_t)(rnd() % 16);
            clip.w = (uint16_t)(24 + rnd() % 60);
            clip.h = (uint16_t)(16 + rnd() % 40);
            STDL_SetClipRect(dst, &clip);
        } else if ((i & 3) == 2) {
            STDL_SetClipRect(dst, NULL);
        }
    }
    STDL_SetClipRect(dst, NULL);

    out->pxlen = (uint32_t)dst->stride * dst->h;
    out->mklen = (uint32_t)dst->maskstride * dst->h;
    memcpy(out->px, dst->pixels, out->pxlen);
    memcpy(out->mk, dst->mask, out->mklen);

    STDL_FreeSprite(spr);
    STDL_FreeSprite(pspr);
    STDL_FreeTileset(ts);
    STDL_FreeSurface(dst);
    STDL_FreeSurface(src);
    STDL_FreeSurface(keyed);
    STDL_FreeSurface(img);
    STDL_FreeSurface(tiles);
    STDL_SetPlaneBudget(4);
}

static void compare(const Snap *ref, const Snap *got, int budget)
{
    uint32_t i;
    int bad = 0;

    CHECK(ref->pxlen == got->pxlen && ref->mklen == got->mklen,
          "budget %d: snapshot sizes differ", budget);
    for (i = 0; i < ref->pxlen && bad < 5; i++) {
        if (ref->px[i] != got->px[i]) {
            bad++;
            failures++;
            printf("  budget %d pixel byte %lu: got %02X want %02X "
                   "(row %lu, group %lu, plane %lu)\n",
                   budget, (unsigned long)i, got->px[i], ref->px[i],
                   (unsigned long)(i / (DW / 16 * 8)),
                   (unsigned long)(i % (DW / 16 * 8) / 8),
                   (unsigned long)(i % 8 / 2));
        }
    }
    bad = 0;
    for (i = 0; i < ref->mklen && bad < 5; i++) {
        if (ref->mk[i] != got->mk[i]) {
            bad++;
            failures++;
            printf("  budget %d mask byte %lu: got %02X want %02X\n",
                   budget, (unsigned long)i, got->mk[i], ref->mk[i]);
        }
    }
}

/* the budget must never leave a bit set above its own planes */
static void check_high_planes_zero(const Snap *s, int budget)
{
    uint32_t i;
    int bad = 0;

    for (i = 0; i < s->pxlen && bad < 3; i++) {
        int plane = (int)(i % 8) / 2;
        if (plane >= budget && s->px[i] != 0) {
            bad++;
            failures++;
            printf("  budget %d left plane %d non-zero at byte %lu\n",
                   budget, plane, (unsigned long)i);
        }
    }
}

static void test_budgets(void)
{
    static Snap ref, got;
    int budget;

    for (budget = 1; budget <= 3; budget++) {
        int maxcol = 1 << budget;

        render(4, maxcol, &ref);
        render(budget, maxcol, &got);
        compare(&ref, &got, budget);
        check_high_planes_zero(&got, budget);
    }
}

/* colour indices above the budget are truncated, not corrupting */
static void test_colour_truncation(void)
{
    STDL_Surface *s = STDL_CreateSurface(64, 8);
    STDL_Rect r;
    int x;

    STDL_SetPlaneBudget(2);
    r.x = 0; r.y = 0; r.w = 64; r.h = 8;
    STDL_FillRect(s, &r, 9);            /* 9 & 3 == 1 */
    for (x = 0; x < 64; x += 7) {
        CHECK(STDL_GetPixel(s, x, 0) == 1,
              "truncated fill at x=%d gave %d", x,
              STDL_GetPixel(s, x, 0));
    }
    STDL_PutPixel(s, 5, 2, 14);         /* 14 & 3 == 2 */
    CHECK(STDL_GetPixel(s, 5, 2) == 2, "truncated pixel");
    STDL_HLine(s, 0, 63, 4, 15);        /* 15 & 3 == 3 */
    CHECK(STDL_GetPixel(s, 33, 4) == 3, "truncated span");
    STDL_XorRect(s, &r, 12);            /* 12 & 3 == 0: no-op */
    CHECK(STDL_GetPixel(s, 33, 4) == 3, "XOR above budget is a no-op");

    /* raw group writers must not be a back door into the high
     * planes either - the invariant has to hold through every
     * public entry point, not just the colour-taking ones */
    {
        uint16_t planes[4] = { 0xFFFFu, 0, 0xFFFFu, 0xFFFFu };
        uint8_t  bytes[4]  = { 0xFFu, 0, 0xFFu, 0xFFu };
        STDL_PutGroup(s, 0, 6, planes, 0);
        CHECK(STDL_GetPixel(s, 3, 6) == 1, "PutGroup truncated to 1");
        STDL_PutGroup8(s, 16, 7, bytes, 0);
        CHECK(STDL_GetPixel(s, 19, 7) == 1, "PutGroup8 truncated to 1");
    }
    STDL_SetPlaneBudget(4);
    STDL_FreeSurface(s);
}

/*
 * The batched point primitives take their colours straight from the
 * caller, so they are the easiest place to leak a bit above the
 * budget - and the odd budgets are the interesting ones, because
 * their fast loops merge a plane *pair* as one long and have to mask
 * the half the budget does not reach. The whole-list script uses only
 * in-budget colours, where the leak is invisible (the plane it would
 * touch is zero anyway), so out-of-budget colours are checked here.
 */
static void test_point_truncation(void)
{
    int budget;

    for (budget = 1; budget <= 4; budget++) {
        STDL_Surface *s = STDL_CreateSurface(64, 8);
        STDL_Point p[3] = { { 1, 1 }, { 17, 2 }, { 33, 3 } };
        uint8_t pc[3] = { 15, 9, 6 };
        int mask = (1 << budget) - 1;

        STDL_SetPlaneBudget(budget);
        STDL_Points(s, p, 3, 15);
        CHECK(STDL_GetPixel(s, 1, 1) == (15 & mask),
              "budget %d: STDL_Points 15 gave %d, want %d", budget,
              STDL_GetPixel(s, 1, 1), 15 & mask);
        CHECK(STDL_GetPixel(s, 33, 3) == (15 & mask),
              "budget %d: STDL_Points 15 gave %d at x=33", budget,
              STDL_GetPixel(s, 33, 3));
        STDL_PointsC(s, p, pc, 3);
        CHECK(STDL_GetPixel(s, 1, 1) == (15 & mask),
              "budget %d: STDL_PointsC 15 gave %d", budget,
              STDL_GetPixel(s, 1, 1));
        CHECK(STDL_GetPixel(s, 17, 2) == (9 & mask),
              "budget %d: STDL_PointsC 9 gave %d", budget,
              STDL_GetPixel(s, 17, 2));
        CHECK(STDL_GetPixel(s, 33, 3) == (6 & mask),
              "budget %d: STDL_PointsC 6 gave %d", budget,
              STDL_GetPixel(s, 33, 3));
        STDL_SetPlaneBudget(4);
        STDL_FreeSurface(s);
    }
}

static void test_api(void)
{
    CHECK(STDL_SetPlaneBudget(-1) == 4, "default budget is 4");
    CHECK(STDL_SetPlaneBudget(2) == 4, "returns previous budget");
    CHECK(STDL_SetPlaneBudget(-1) == 2, "query does not change it");
    CHECK(STDL_SetPlaneBudget(0) == 2, "returns previous budget");
    CHECK(STDL_SetPlaneBudget(-1) == 1, "0 clamps up to 1");
    CHECK(STDL_SetPlaneBudget(9) == 1, "returns previous budget");
    CHECK(STDL_SetPlaneBudget(-1) == 4, "9 clamps down to 4");
}

/* the whole point: a surface built under a budget still reads back
 * the colours that were drawn into it */
static void test_readback(void)
{
    STDL_Surface *s;
    int x, y;

    STDL_SetPlaneBudget(3);
    s = STDL_CreateSurface(40, 12);
    for (y = 0; y < 12; y++)
        for (x = 0; x < 40; x++)
            STDL_PutPixel(s, x, y, (uint8_t)((x + y) & 7));
    for (y = 0; y < 12; y++)
        for (x = 0; x < 40; x++)
            CHECK(STDL_GetPixel(s, x, y) == ((x + y) & 7),
                  "readback (%d,%d)", x, y);
    STDL_FreeSurface(s);
    STDL_SetPlaneBudget(4);
}

int main(void)
{
    font_init();
    test_api();
    test_budgets();
    test_colour_truncation();
    test_point_truncation();
    test_readback();
    if (failures == 0) {
        printf("all plane-budget tests passed\n");
        return 0;
    }
    printf("%d failure(s)\n", failures);
    return 1;
}
