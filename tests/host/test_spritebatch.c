/*
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
/*
 * STDL_BlitSprites against STDL_BlitSprite called once an entry, in
 * the same order, byte for byte: pre-shifted and unshifted sprites of
 * several sizes and frames, positions inside the clip (the direct
 * route) and across or beyond it (handed to STDL_BlitSprite), random
 * clip rectangles, plane budgets 4 and 2.
 */
#include <stdio.h>
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

static uint32_t rs = 0x1234567u;
static int rnd(int n)
{
    rs ^= rs << 13;
    rs ^= rs >> 17;
    rs ^= rs << 5;
    return (int)(rs % (uint32_t)n);
}

enum { W = 160, H = 96 };

static STDL_Sprite *make_sprite(int fw, int fh, int frames, int pre)
{
    STDL_Surface *art = STDL_CreateSurface(fw * frames, fh);
    STDL_Sprite *spr;
    int x, y;

    for (y = 0; y < fh; y++) {
        for (x = 0; x < fw * frames; x++) {
            /* a key colour (0) round a random body */
            STDL_PutPixel(art, x, y, (uint8_t)(rnd(3) ? rnd(16) : 0));
        }
    }
    STDL_SetColourKey(art, 1, 0);
    spr = STDL_SpriteFromSurface(art, fw, pre ? STDL_PRESHIFT : 0);
    STDL_FreeSurface(art);
    return spr;
}

int main(void)
{
    static const int sizes[3][2] = { { 16, 16 }, { 32, 24 }, { 24, 8 } };
    int it;

    for (it = 0; it < 240; it++) {
        const int pre = it & 1, budget = (it & 2) ? 2 : 4;
        const int *sz = sizes[(it >> 2) % 3];
        STDL_Sprite *spr;
        STDL_Surface *a = STDL_CreateSurface(W, H);
        STDL_Surface *b = STDL_CreateSurface(W, H);
        STDL_SpriteAt at[24];
        STDL_Rect clip;
        int n = 1 + rnd(24), k;

        STDL_SetPlaneBudget(budget);
        spr = make_sprite(sz[0], sz[1], 3, pre);
        for (k = 0; k < W * H / 2; k++) {
            a->pixels[k] = b->pixels[k] = (uint8_t)rnd(256);
        }
        if (budget == 2) {
            STDL_FillRect(a, NULL, 0);
            STDL_FillRect(b, NULL, 0);
        }
        clip.x = 0;
        clip.y = 0;
        clip.w = W;
        clip.h = H;
        if (it % 3 == 0) {
            clip.x = (int16_t)rnd(40);
            clip.y = (int16_t)rnd(30);
            clip.w = (uint16_t)(16 + rnd(W - clip.x - 16));
            clip.h = (uint16_t)(8 + rnd(H - clip.y - 8));
        }
        STDL_SetClipRect(a, &clip);
        STDL_SetClipRect(b, &clip);
        for (k = 0; k < n; k++) {
            /* mostly well inside; some across the edges */
            at[k].x = (int16_t)(rnd(5) ? rnd(W - 40) + 4 : rnd(W + 40) - 30);
            at[k].y = (int16_t)(rnd(5) ? rnd(H - 30) + 2 : rnd(H + 30) - 20);
            at[k].frame = (uint16_t)rnd(3);
            if (!pre && rnd(2)) {
                at[k].x &= ~15;         /* unshifted on a group edge */
            }
        }
        STDL_BlitSprites(spr, at, n, a);
        for (k = 0; k < n; k++) {
            STDL_BlitSprite(spr, at[k].frame, b, at[k].x, at[k].y);
        }
        CHECK(memcmp(a->pixels, b->pixels, (size_t)a->stride * H) == 0,
              "iteration %d (pre %d budget %d %dx%d, %d sprites) differs",
              it, pre, budget, sz[0], sz[1], n);
        STDL_FreeSprite(spr);
        STDL_FreeSurface(a);
        STDL_FreeSurface(b);
    }
    STDL_SetPlaneBudget(4);

    if (failures) {
        printf("test_spritebatch: %d failures\n", failures);
        return 1;
    }
    printf("test_spritebatch: ok\n");
    return 0;
}
