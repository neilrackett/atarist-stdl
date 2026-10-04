/*
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
/*
 * STDL_FadeTo, a step at a time: every step written to the palette
 * is from + (target - from) * f / frames rounded toward zero, as the
 * 32-bit C it replaced computed it, the last step is the target, and
 * entries past the target's count are left alone. The steps are
 * caught through stdl_pal_apply_hook, which is where an open border
 * stages them.
 */
#include <stdio.h>
#include <string.h>
#include <stdl/stdl.h>
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

static STDL_Colour seen[400][16];
static int nseen;

static void catch_step(void)
{
    if (nseen < 400) {
        memcpy(seen[nseen], stdl.colours, sizeof(seen[0]));
    }
    nseen++;
}

static uint8_t want(int from, int to, int f, int frames)
{
    return (uint8_t)(from + ((to - from) * f) / frames);
}

static void fade(const STDL_Colour *from, const STDL_Colour *to, int n,
                 int frames)
{
    STDL_Colour tc[16];
    STDL_Palette target;
    int f, i;

    memcpy(stdl.colours, from, sizeof(stdl.colours));
    memcpy(tc, to, sizeof(tc));
    target.ncolors = n;
    target.colors = tc;
    nseen = 0;
    STDL_FadeTo(&target, frames);
    CHECK(nseen == frames, "%d frames: %d steps", frames, nseen);
    for (f = 1; f <= nseen && f <= 400; f++) {
        for (i = 0; i < 16; i++) {
            const STDL_Colour *s = &seen[f - 1][i];

            if (i >= n) {
                CHECK(s->r == from[i].r && s->g == from[i].g
                      && s->b == from[i].b,
                      "entry %d past the target's %d moved", i, n);
                continue;
            }
            CHECK(s->r == want(from[i].r, to[i].r, f, frames)
                  && s->g == want(from[i].g, to[i].g, f, frames)
                  && s->b == want(from[i].b, to[i].b, f, frames),
                  "%d frames, step %d, entry %d: %u,%u,%u", frames, f, i,
                  s->r, s->g, s->b);
        }
    }
}

int main(void)
{
    static const int lengths[] = { 1, 2, 7, 25, 128, 300 };
    STDL_Colour a[16], b[16];
    uint32_t r = 12345;
    int i, k;

    stdl_pal_apply_hook = catch_step;
    for (k = 0; k < (int)(sizeof lengths / sizeof lengths[0]); k++) {
        for (i = 0; i < 16; i++) {
            r = r * 1103515245u + 12345u;
            a[i].r = (uint8_t)(r >> 8);
            a[i].g = (uint8_t)(r >> 16);
            a[i].b = (uint8_t)(r >> 24);
            b[i].r = (uint8_t)~a[i].g;
            b[i].g = (uint8_t)(a[i].b ^ 0x5A);
            b[i].b = (uint8_t)(i * 17);
        }
        /* black to white and back: the largest steps either way */
        a[0].r = a[0].g = a[0].b = 0;
        b[0].r = b[0].g = b[0].b = 255;
        a[1].r = a[1].g = a[1].b = 255;
        b[1].r = b[1].g = b[1].b = 0;
        fade(a, b, 16, lengths[k]);
        fade(b, a, 9, lengths[k]);
    }
    stdl_pal_apply_hook = NULL;

    if (failures) {
        printf("test_palette: %d failures\n", failures);
        return 1;
    }
    printf("test_palette: ok\n");
    return 0;
}
