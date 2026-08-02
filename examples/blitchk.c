/*
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: CC0-1.0
 *
 * STDL example program, dedicated to the public domain so it can
 * be used as a starting point without licence concerns.
 */
/*
 * blitchk - on-target BLiTTER correctness harness.
 *
 * The host-side unit tests cannot cover the blitter, so this runs
 * on the machine: every randomised fill and blit is performed
 * twice, once with STDL_UseBlitter(0) and once with (1), into two
 * destination surfaces that started identical, and the pixel and
 * mask contents are compared byte-for-byte after each operation.
 * Any divergence between the CPU and blitter paths is a bug in one
 * of them. Finishes with a fill/blit timing comparison.
 *
 * On machines without a blitter it degenerates to comparing the
 * CPU path with itself and reports that nothing was accelerated.
 */

#include <stdio.h>
#include <string.h>
#include <stdl/stdl.h>

#define ITERATIONS 300

static uint32_t rng = 0x12345678;
static uint32_t rnd(void)
{
    rng = rng * 1103515245UL + 12345UL;
    return (rng >> 16) & 0x7FFF;
}

static STDL_Surface *make_src(int keyed)
{
    STDL_Surface *s = STDL_CreateSurface(160, 64);
    int x, y;

    for (y = 0; y < 64; y++) {
        for (x = 0; x < 160; x++) {
            STDL_PutPixel(s, x, y, (uint8_t)(rnd() % (keyed ? 6 : 16)));
        }
    }
    if (keyed) {
        STDL_SetColourKey(s, 1, 3);
    }
    return s;
}

static int compare(const STDL_Surface *a, const STDL_Surface *b)
{
    if (memcmp(a->pixels, b->pixels,
               (uint32_t)a->stride * a->h) != 0) {
        return 1;
    }
    if (a->mask != NULL && b->mask != NULL
        && memcmp(a->mask, b->mask,
                  (uint32_t)a->maskstride * a->h) != 0) {
        return 2;
    }
    return 0;
}

int main(void)
{
    STDL_Surface *src_plain, *src_keyed, *da, *db;
    int i, failures = 0, blit_avail;
    uint32_t t0, t1, cpu_ms, blit_ms;

    if (STDL_Init(STDL_INIT_VIDEO) < 0) {
        fprintf(stderr, "init failed: %s\n", STDL_GetError());
        return 1;
    }
    blit_avail = STDL_GetMachineInfo()->has_blitter;
    printf("blitter %s\n", blit_avail ? "present" : "ABSENT");

    src_plain = make_src(0);
    src_keyed = make_src(1);
    da = STDL_CreateSurface(320, 160);
    db = STDL_CreateSurface(320, 160);
    STDL_CreateMask(da, 1);
    STDL_CreateMask(db, 1);

    for (i = 0; i < ITERATIONS; i++) {
        int op = (int)(rnd() % 3);
        int phase = (int)(rnd() & 15);
        STDL_Rect r, r2;

        if (op == 0) {
            /* fill: random rect, random colour or transparent */
            uint8_t col = (uint8_t)(rnd() % 17);
            r.x = (int16_t)((int)(rnd() % 360) - 20);
            r.y = (int16_t)((int)(rnd() % 180) - 10);
            r.w = (uint16_t)(rnd() % 200);
            r.h = (uint16_t)(rnd() % 100);
            r2 = r;
            STDL_UseBlitter(0);
            STDL_FillRect(da, &r, col);
            STDL_UseBlitter(1);
            STDL_FillRect(db, &r2, col);
        } else {
            /* blit: same phase so the blitter path is exercised */
            STDL_Surface *src = (op == 1) ? src_plain : src_keyed;
            STDL_Rect sr;
            sr.x = (int16_t)(phase + 16 * (int)(rnd() % 3));
            sr.y = (int16_t)(rnd() % 20);
            sr.w = (uint16_t)(rnd() % 160);
            sr.h = (uint16_t)(rnd() % 64);
            r.x = (int16_t)(phase + 16 * ((int)(rnd() % 14) - 1));
            r.y = (int16_t)((int)(rnd() % 170) - 10);
            r2 = r;
            STDL_UseBlitter(0);
            STDL_BlitSurface(src, &sr, da, &r);
            STDL_UseBlitter(1);
            STDL_BlitSurface(src, &sr, db, &r2);
        }

        {
            int diff = compare(da, db);
            if (diff != 0) {
                failures++;
                printf("MISMATCH iter %d op %d (%s)\n", i, op,
                       diff == 1 ? "pixels" : "mask");
                if (failures > 5) {
                    break;
                }
                /* resync so later iterations stay meaningful */
                memcpy(db->pixels, da->pixels,
                       (uint32_t)da->stride * da->h);
                memcpy(db->mask, da->mask,
                       (uint32_t)da->maskstride * da->h);
            }
        }
    }
    printf(failures == 0 ? "PASS: %d operations identical\n"
                         : "FAIL: %d mismatches in %d ops\n",
           failures == 0 ? ITERATIONS : failures, ITERATIONS);

    /* timing: full-screen-ish fills and big aligned blits */
    {
        STDL_Rect big = { 0, 0, 320, 160 };
        STDL_Rect d = { 0, 0, 0, 0 };
        int n;

        STDL_UseBlitter(0);
        t0 = STDL_GetTicks();
        for (n = 0; n < 50; n++) {
            STDL_Rect f = big;
            STDL_FillRect(da, &f, (uint8_t)(n & 15));
            d.x = 0; d.y = 0;
            STDL_BlitSurface(src_keyed, NULL, da, &d);
        }
        t1 = STDL_GetTicks();
        cpu_ms = t1 - t0;

        STDL_UseBlitter(1);
        t0 = STDL_GetTicks();
        for (n = 0; n < 50; n++) {
            STDL_Rect f = big;
            STDL_FillRect(da, &f, (uint8_t)(n & 15));
            d.x = 0; d.y = 0;
            STDL_BlitSurface(src_keyed, NULL, da, &d);
        }
        t1 = STDL_GetTicks();
        blit_ms = t1 - t0;

        printf("50x (fill 320x160 + keyed blit 160x64): "
               "cpu %lums, blitter %lums\n",
               (unsigned long)cpu_ms, (unsigned long)blit_ms);
    }

    STDL_Quit();
    return failures != 0;
}
