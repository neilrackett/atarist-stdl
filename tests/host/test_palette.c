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
 *
 * Then STDL_StagePalette: staged changes reach the registers only
 * when the VBL queue runs, the last change before a blank wins, the
 * program's staging and a border's share one slot and outlive each
 * other, turning it off writes what was waiting, no free slot leaves
 * writes immediate, and staging comes back after release_hardware
 * has cleared the queue and the hook. And STDL_HW444 against the
 * run-time conversion.
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

/* run the queue as the VBL interrupt would */
static void fire(void)
{
    int i;

    for (i = 0; i < STDL_NVBLS; i++) {
        if (STDL_VBLQUEUE[i] != NULL) {
            STDL_VBLQUEUE[i]();
        }
    }
}

static int slots_used(void)
{
    int i, n = 0;

    for (i = 0; i < STDL_NVBLS; i++) {
        n += STDL_VBLQUEUE[i] != NULL;
    }
    return n;
}

/* colour 3 to a grey, through the path every palette call takes */
static void set3(uint8_t v)
{
    stdl.colours[3].r = stdl.colours[3].g = stdl.colours[3].b = v;
    stdl_palette_apply_hw();
}

static uint16_t grey(uint8_t v)
{
    return STDL_HWColour(v, v, v);
}

static void test_staging(void)
{
    int i;

    for (i = 0; i < STDL_NVBLS; i++) {
        STDL_VBLQUEUE[i] = NULL;
    }
    stdl_pal_apply_hook = NULL;

    set3(0x20);
    CHECK(STDL_HWPAL[3] == grey(0x20), "unstaged write not immediate");

    CHECK(STDL_StagePalette(1) == 0, "staging refused");
    CHECK(slots_used() == 1, "staging took %d slots", slots_used());
    set3(0x40);
    CHECK(STDL_HWPAL[3] == grey(0x20), "staged write reached hardware");
    set3(0x60);
    fire();
    CHECK(STDL_HWPAL[3] == grey(0x60), "blank did not write the last");
    STDL_HWPAL[3] = 0;
    fire();
    CHECK(STDL_HWPAL[3] == 0, "a blank rewrote an unchanged palette");

    /* a border joins and the program leaves: still staged */
    CHECK(stdl_pal_stage_on(STDL_PAL_STAGE_BORDER) == 0, "border refused");
    CHECK(slots_used() == 1, "border took a second slot");
    CHECK(STDL_StagePalette(0) == 0, "unstaging failed");
    set3(0x80);
    CHECK(STDL_HWPAL[3] == 0, "border's staging ended with the program's");
    fire();
    CHECK(STDL_HWPAL[3] == grey(0x80), "border staging did not write");

    /* the program rejoins; the border closing leaves it staged */
    CHECK(STDL_StagePalette(1) == 0, "restaging refused");
    stdl_pal_stage_off(STDL_PAL_STAGE_BORDER);
    set3(0xA0);
    CHECK(STDL_HWPAL[3] == grey(0x80), "border close ended program staging");

    /* off: what was waiting goes now, then immediate, slot freed */
    STDL_StagePalette(0);
    CHECK(STDL_HWPAL[3] == grey(0xA0), "waiting change dropped at off");
    CHECK(slots_used() == 0, "slot kept after off");
    set3(0xC0);
    CHECK(STDL_HWPAL[3] == grey(0xC0), "write not immediate after off");

    /* a full queue: refused, and writes stay immediate */
    for (i = 0; i < STDL_NVBLS; i++) {
        STDL_VBLQUEUE[i] = fire;
    }
    CHECK(STDL_StagePalette(1) == -1, "staging accepted with no slot");
    set3(0xE0);
    CHECK(STDL_HWPAL[3] == grey(0xE0), "refused staging still staged");
    for (i = 0; i < STDL_NVBLS; i++) {
        STDL_VBLQUEUE[i] = NULL;
    }

    /* release_hardware empties the queue and the hook together */
    CHECK(STDL_StagePalette(1) == 0, "staging refused after a full queue");
    for (i = 0; i < STDL_NVBLS; i++) {
        STDL_VBLQUEUE[i] = NULL;
    }
    stdl_pal_apply_hook = NULL;
    CHECK(STDL_StagePalette(1) == 0 && slots_used() == 1,
          "staging not reinstalled after a release");
    set3(0x10);
    CHECK(STDL_HWPAL[3] == grey(0xE0), "reinstalled staging not staged");
    fire();
    CHECK(STDL_HWPAL[3] == grey(0x10), "reinstalled staging not written");
    STDL_StagePalette(0);
}

/* STDL_HW444: the rotated nibble, against the run-time conversion of
 * the same 4-bit level spread to 8 bits */
static void test_hw444(void)
{
    static const uint16_t table[] = { STDL_HW444(0xC72), STDL_HW444(0xFFF),
                                      STDL_HW444(0x000) };
    int v;

    for (v = 0; v < 16; v++) {
        uint8_t c = (uint8_t)(v << 4);

        CHECK(STDL_HW444(v << 8 | v << 4 | v) == STDL_HWColour(c, c, c),
              "HW444 level %d", v);
    }
    CHECK(table[0] == STDL_HWColour(0xC0, 0x70, 0x20), "HW444(0xC72) %04x",
          table[0]);
    CHECK(table[1] == 0x0FFF && table[2] == 0, "HW444 extremes");
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
    test_staging();
    test_hw444();

    if (failures) {
        printf("test_palette: %d failures\n", failures);
        return 1;
    }
    printf("test_palette: ok\n");
    return 0;
}
