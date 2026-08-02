/*
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
#include <stdio.h>
#include <string.h>
#include <stdl/stdl.h>
#include "stdl_internal.h"

/* minimal stand-ins for the event-module pieces cursor.c uses */
void (*stdl_cursor_hook)(int x, int y);
static int fake_mx = 160, fake_my = 100;
uint8_t STDL_GetMouseState(int *x, int *y)
{
    if (x) *x = fake_mx;
    if (y) *y = fake_my;
    return 0;
}

static int failures;
#define CHECK(c, ...) do { if (!(c)) { failures++; printf("FAIL: "); printf(__VA_ARGS__); printf("\n"); } } while (0)

static uint8_t snap[320 * 200];
static void snapshot(STDL_Surface *s)
{
    int x, y;
    for (y = 0; y < 200; y++)
        for (x = 0; x < 320; x++)
            snap[y * 320 + x] = STDL_GetPixel(s, x, y);
}
static int same(STDL_Surface *s)
{
    int x, y;
    for (y = 0; y < 200; y++)
        for (x = 0; x < 320; x++)
            if (snap[y * 320 + x] != STDL_GetPixel(s, x, y)) {
                printf("  diff at %d,%d\n", x, y);
                return 0;
            }
    return 1;
}

int main(void)
{
    STDL_Surface *s = STDL_CreateSurface(320, 200);
    static const uint8_t data[4] = { 0xF0, 0x00, 0x0F, 0xFF };
    static const uint8_t mask[4] = { 0xFF, 0xF0, 0xFF, 0x0F };
    STDL_Cursor *c;
    int x, y, i;

    /* pretend this surface is the screen */
    stdl_screen = *s;

    for (y = 0; y < 200; y++)
        for (x = 0; x < 320; x++)
            STDL_PutPixel(&stdl_screen, x, y, (uint8_t)((x + y) % 13));
    snapshot(&stdl_screen);

    /* 8x4 cursor: row0 F0/FF -> 4 black + 4 white;
       row1 00/F0 -> 4 white + 4 transparent;
       row2 0F/FF -> 4 white + 4 black? data 0F: right black. left white
       row3 FF/0F -> left "inverted"(black), right black */
    c = STDL_CreateCursor(data, mask, 8, 4, 0, 0);
    CHECK(c != NULL, "create");

    STDL_SetCursor(c);          /* draws at 160,100 */
    CHECK(STDL_GetPixel(&stdl_screen, 160, 100) == 0, "black TL");
    CHECK(STDL_GetPixel(&stdl_screen, 164, 100) == 15, "white TR");
    CHECK(STDL_GetPixel(&stdl_screen, 160, 101) == 15, "white row1");
    CHECK(STDL_GetPixel(&stdl_screen, 164, 101) == snap[101 * 320 + 164],
          "transparent row1");
    CHECK(STDL_GetPixel(&stdl_screen, 160, 103) == 0, "inverted->black");

    /* walk it around, including off-screen edges */
    for (i = 0; i < 500; i++) {
        fake_mx = (i * 37) % 340 - 10;
        fake_my = (i * 23) % 220 - 10;
        stdl_cursor_hook(fake_mx, fake_my);
    }
    /* hide and verify the background is bit-exact again */
    STDL_ShowCursorCtl(0);
    CHECK(same(&stdl_screen), "restore after walk");

    STDL_ShowCursorCtl(1);
    STDL_SetCursor(NULL);
    CHECK(same(&stdl_screen), "restore after unset");

    if (!failures) printf("cursor tests passed\n");
    return failures != 0;
}
