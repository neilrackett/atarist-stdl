/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * STDL_Dirty keeps one restore list per page.
 *
 *  - One page: byte-identical to the single-list algorithm it
 *    replaced (a copy of which lives here as the reference), over
 *    random push / restore / reset sequences, overflow included.
 *  - Two pages drawn alternately, as a double-buffered game draws:
 *    everything drawn is pushed, so after each restore the page must
 *    equal the background exactly - the invariant the single list
 *    broke by replaying one page's rectangles onto the other.
 *  - A list overflowing on one page restores that page whole and
 *    leaves the other's list alone.
 *  - A third page evicts the older list, and the evicted page is
 *    restored whole when it comes back.
 */
#include <stdio.h>
#include <string.h>
#include "stdl_internal.h"

static int failures;

#define CHECK(cond, ...) do { \
    if (!(cond)) { \
        failures++; \
        printf("FAIL %s:%d: ", __FILE__, __LINE__); \
        printf(__VA_ARGS__); \
        printf("\n"); \
    } \
} while (0)

static uint32_t rng = 0xD1B54A32u;
static unsigned rnd(void)
{
    rng = rng * 1103515245u + 12345u;
    return (rng >> 16) & 0x7FFF;
}

enum { W = 96, H = 48, MAXR = 12 };

static void randomise(STDL_Surface *s)
{
    int x, y;
    for (y = 0; y < s->h; y++)
        for (x = 0; x < s->w; x++)
            STDL_PutPixel(s, x, y, (uint8_t)(rnd() & 15));
}

static int same(const STDL_Surface *a, const STDL_Surface *b)
{
    return memcmp(a->pixels, b->pixels, (size_t)a->stride * a->h) == 0;
}

static STDL_Rect random_rect(void)
{
    STDL_Rect r;
    r.x = (int16_t)((int)(rnd() % (W + 20)) - 10);
    r.y = (int16_t)((int)(rnd() % (H + 10)) - 5);
    r.w = (uint16_t)(1 + rnd() % 40);
    r.h = (uint16_t)(1 + rnd() % 20);
    return r;
}

/* the single-list algorithm, as it was, for the one-page reference */
static STDL_Surface *old_bg;
static STDL_Rect old_rects[MAXR];
static int old_n, old_over;

static void old_push(const STDL_Rect *r)
{
    if (old_n >= MAXR) {
        old_over = 1;
        return;
    }
    old_rects[old_n++] = *r;
}

static void old_restore(STDL_Surface *dst)
{
    int i;
    if (old_over) {
        STDL_BlitSurface(old_bg, NULL, dst, NULL);
    } else {
        for (i = 0; i < old_n; i++) {
            STDL_Rect d = old_rects[i];
            STDL_BlitSurface(old_bg, &old_rects[i], dst, &d);
        }
    }
    old_n = 0;
    old_over = 0;
}

static void test_one_page(void)
{
    STDL_Surface *bg = STDL_CreateSurface(W, H);
    STDL_Surface *a = STDL_CreateSurface(W, H);
    STDL_Surface *b = STDL_CreateSurface(W, H);
    int i;

    randomise(bg);
    randomise(a);
    STDL_BlitSurface(a, NULL, b, NULL);
    old_bg = bg;
    old_n = old_over = 0;
    CHECK(STDL_DirtyInit(bg, MAXR) == 0, "init");

    for (i = 0; i < 3000; i++) {
        int op = (int)(rnd() % 10);
        if (op < 6) {
            /* draw something and push it, on both */
            STDL_Rect r = random_rect(), r2 = r;
            uint8_t c = (uint8_t)(rnd() & 15);
            STDL_FillRect(a, &r, c);
            STDL_FillRect(b, &r2, c);
            r = random_rect();
            STDL_DirtyPush(&r);
            old_push(&r);
        } else if (op < 9) {
            STDL_DirtyRestore(a);
            old_restore(b);
        } else {
            STDL_DirtyReset();
            old_n = old_over = 0;
        }
        if (!same(a, b)) {
            CHECK(0, "one page differs from the old list at op %d", i);
            break;
        }
    }
    STDL_DirtyQuit();
    STDL_FreeSurface(bg);
    STDL_FreeSurface(a);
    STDL_FreeSurface(b);
}

/* draw n random pushed rectangles into page p */
static void draw_pushed(STDL_Surface *p, int n)
{
    while (n-- > 0) {
        STDL_Rect r = random_rect(), pushed = r;
        STDL_FillRect(p, &r, (uint8_t)(rnd() & 15));
        STDL_DirtyPush(&pushed);
    }
}

static void test_two_pages(void)
{
    STDL_Surface *bg = STDL_CreateSurface(W, H);
    STDL_Surface *pg[3];
    int frame, k;

    randomise(bg);
    for (k = 0; k < 3; k++) {
        pg[k] = STDL_CreateSurface(W, H);
        STDL_BlitSurface(bg, NULL, pg[k], NULL);
    }
    CHECK(STDL_DirtyInit(bg, MAXR) == 0, "init");

    for (frame = 0; frame < 400; frame++) {
        STDL_Surface *p = pg[frame & 1];
        int n = (frame % 37 == 5) ? MAXR + 3 : (int)(rnd() % 6);

        /* the game's frame: restore the back page, draw, flip */
        STDL_DirtyRestore(p);
        CHECK(same(p, bg), "page %d not clean at frame %d",
              frame & 1, frame);
        if (!same(p, bg)) {
            break;
        }
        draw_pushed(p, n);

        /* now and then a third page: an off-screen surface */
        if (frame % 50 == 17) {
            STDL_DirtyRestore(pg[2]);
            CHECK(same(pg[2], bg), "third page not clean at %d", frame);
            draw_pushed(pg[2], 3);
            STDL_DirtyRestore(pg[2]);
            CHECK(same(pg[2], bg), "third page twice at %d", frame);
        }
    }
    STDL_DirtyQuit();
    STDL_FreeSurface(bg);
    for (k = 0; k < 3; k++) {
        STDL_FreeSurface(pg[k]);
    }
}

int main(void)
{
    test_one_page();
    test_two_pages();
    if (failures == 0) {
        printf("dirty-list tests passed\n");
        return 0;
    }
    printf("%d failure(s)\n", failures);
    return 1;
}
