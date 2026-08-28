/*
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
/* A fully-transparent masked source whose plane words hold garbage
 * must draw nothing, whatever the flags and phase. Reproduces a
 * port's confetti sprites: a baked frame with no pixels (mask all
 * 0xFF) but uninitialised plane data drawn ever after, because
 * SetColourKey with a numeric key rebuilt the mask from the
 * garbage plane words. STDL_TRANSPARENT is the borrowed-mask key:
 * it must leave the given mask untouched, and the blit must then
 * draw nothing at any phase or flag combination. */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdl/stdl.h>

static int failures;

int main(void)
{
    /* one-group 8x8 "bake": planes full of garbage, mask says
     * everything is transparent; guard slack on both ends is left
     * uninitialised-looking garbage too, as a real bake's is */
    enum { GROUPS = 1, H = 8, GUARD = 4 };
    static uint16_t block[GUARD + GROUPS * 4 * H + GROUPS * H + GUARD];
    uint16_t *planes = block + GUARD;
    uint16_t *mask = planes + GROUPS * 4 * H;
    unsigned seed = 12345;

    for (unsigned i = 0; i < sizeof(block) / sizeof(block[0]); ++i) {
        seed = seed * 1103515245u + 12345u;
        block[i] = (uint16_t)(seed >> 16);
    }
    for (int i = 0; i < GROUPS * H; ++i) {
        mask[i] = 0xFFFF;                    /* all transparent */
    }

    STDL_Surface *view = STDL_CreateSurfaceFrom((uint8_t *)planes,
        GROUPS * 16, H, GROUPS * 8, (uint8_t *)mask, GROUPS * 2);
    STDL_Surface *dst = STDL_CreateSurface(64, 32);
    if (view == NULL || dst == NULL) {
        printf("FAIL: setup\n");
        return 1;
    }
    STDL_CreateMask(dst, 0);
    STDL_SetColourKey(view, 1, STDL_TRANSPARENT);

    for (int flags = 0; flags <= 3; ++flags) {
        for (int phase = 0; phase < 16; ++phase) {
            STDL_Rect r;
            r.x = 0; r.y = 0; r.w = 64; r.h = 32;
            STDL_FillRect(dst, &r, 5);
            /* a marked stripe so UNDER has foreground to respect */
            memset(dst->mask, 0, (size_t)dst->maskstride * 10);
            memset(dst->mask + dst->maskstride * 10, 0xFF,
                   (size_t)dst->maskstride * 4);
            memset(dst->mask + dst->maskstride * 14, 0,
                   (size_t)dst->maskstride * 18);
            r.x = 0; r.y = 10; r.w = 64; r.h = 4;
            STDL_FillRect(dst, &r, 7);

            STDL_Rect sr, dr;
            sr.x = 0; sr.y = 0; sr.w = 8; sr.h = 8;
            dr.x = (int16_t)(8 + phase); dr.y = 8;
            dr.w = 8; dr.h = 8;
            STDL_BlitSurfaceEx(view, &sr, dst, &dr, (unsigned)flags);

            for (int y = 0; y < 32; ++y) {
                for (int x = 0; x < 64; ++x) {
                    const int want = (y >= 10 && y < 14) ? 7 : 5;
                    const int got = STDL_GetPixel(dst, x, y);
                    if (got != want) {
                        ++failures;
                        printf("FAIL: flags %d phase %d: leak at "
                               "%d,%d (got %d want %d)\n",
                               flags, phase, x, y, got, want);
                        y = 32;
                        break;
                    }
                }
            }
        }
    }

    if (failures == 0) {
        printf("underleak: OK\n");
        return 0;
    }
    printf("underleak: %d failures\n", failures);
    return 1;
}
