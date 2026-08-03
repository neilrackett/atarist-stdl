/*
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
/*
 * STDL_DrawChar against STDL_DrawText.
 *
 * The single-glyph entry point exists purely to remove the per-call
 * setup a one-character string pays, so its whole contract is "the
 * same pixels, faster". That makes STDL_DrawText the reference
 * model: every case here draws the same glyph both ways into
 * identical surfaces and compares the pixel and mask blocks
 * byte-for-byte.
 *
 * The cases are the ones the collapsed clip arithmetic could get
 * wrong - a cell straddling two groups, a cell hanging off the left
 * (where the group index goes negative), off the right, off the top
 * and bottom, a clip rectangle narrower than one cell, cell widths
 * that are not 8, and characters outside the font.
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

static uint8_t glyph_bits[256 * 2 * 16];
static STDL_Font font;

static void font_init(int cw, int ch)
{
    unsigned seed = 12345;
    size_t i;

    for (i = 0; i < sizeof(glyph_bits); i++) {
        seed = seed * 1103515245u + 12345u;
        glyph_bits[i] = (uint8_t)(seed >> 19);
    }
    font.cw = (int16_t)cw;
    font.ch = (int16_t)ch;
    font.first = 32;
    font.last = 90;
    font.bytes_per_row = (uint16_t)((cw + 7) / 8);
    font.bits = glyph_bits;
}

static STDL_Surface *fresh(int masked)
{
    STDL_Surface *s = STDL_CreateSurface(DW, DH);
    unsigned seed = 4321;
    int x, y;

    for (y = 0; y < DH; y++) {
        for (x = 0; x < DW; x++) {
            seed = seed * 1103515245u + 12345u;
            STDL_PutPixel(s, x, y, (uint8_t)((seed >> 20) & 15));
        }
    }
    if (masked) {
        STDL_SetColourKey(s, 1, 5);
    }
    return s;
}

static int differs(const STDL_Surface *a, const STDL_Surface *b)
{
    size_t px = (size_t)a->stride * a->h;

    if (memcmp(a->pixels, b->pixels, px) != 0) {
        return 1;
    }
    if ((a->mask == NULL) != (b->mask == NULL)) {
        return 1;
    }
    if (a->mask != NULL
        && memcmp(a->mask, b->mask, (size_t)a->maskstride * a->h) != 0) {
        return 1;
    }
    return 0;
}

static void one_case(int cw, int ch, int c, int x, int y, int col,
                     int masked, const STDL_Rect *clip)
{
    STDL_Surface *a, *b;
    char buf[2];

    font_init(cw, ch);
    a = fresh(masked);
    b = fresh(masked);
    if (clip != NULL) {
        STDL_SetClipRect(a, clip);
        STDL_SetClipRect(b, clip);
    }

    buf[0] = (char)c;
    buf[1] = '\0';
    STDL_DrawText(a, &font, x, y, buf, (uint8_t)col);
    STDL_DrawChar(b, &font, x, y, c, (uint8_t)col);

    CHECK(!differs(a, b),
          "cw %d ch %d char %d at (%d,%d) col %d masked %d clip %s",
          cw, ch, c, x, y, col, masked, clip ? "yes" : "no");

    STDL_FreeSurface(a);
    STDL_FreeSurface(b);
}

int main(void)
{
    static const int cws[] = { 5, 8, 11, 16 };
    STDL_Rect clip;
    int i, x, y, c;

    for (i = 0; i < (int)(sizeof(cws) / sizeof(cws[0])); i++) {
        int cw = cws[i];

        /* every horizontal position from off the left to off the
         * right, at a couple of vertical ones */
        for (x = -cw - 3; x < DW + 3; x++) {
            one_case(cw, 8, 'A', x, 4, 9, 0, NULL);
            one_case(cw, 8, 'A', x, 4, 9, 1, NULL);
        }
        /* off the top and bottom */
        for (y = -10; y < DH + 3; y++) {
            one_case(cw, 8, 'Q', 17, y, 3, 0, NULL);
            one_case(cw, 13, 'Q', 20, y, 3, 1, NULL);
        }
    }

    /* clip rectangles, including ones narrower than a cell */
    for (i = 0; i < 12; i++) {
        clip.x = (int16_t)(i * 3);
        clip.y = (int16_t)(i);
        clip.w = (uint16_t)(1 + i * 2);
        clip.h = (uint16_t)(3 + i);
        for (x = -4; x < 48; x += 3) {
            one_case(8, 8, 'z' - 40, x, 2, 12, 0, &clip);
            one_case(16, 9, 'z' - 40, x, 2, 12, 1, &clip);
        }
    }

    /* characters outside the font draw nothing, both ways */
    for (c = 0; c < 256; c += 17) {
        one_case(8, 8, c, 12, 6, 7, 0, NULL);
        one_case(8, 8, c, 13, 6, 7, 1, NULL);
    }

    /* every plane budget: both entry points must truncate colours
     * the same way and touch the same planes */
    for (i = 1; i <= 4; i++) {
        STDL_SetPlaneBudget(i);
        for (c = 0; c < 16; c++) {
            one_case(8, 8, 'M', 9, 5, c, 0, NULL);
            one_case(8, 8, 'M', 16, 5, c, 1, NULL);
        }
        STDL_SetPlaneBudget(4);
    }

    /* null arguments are caller errors, not crashes */
    font_init(8, 8);
    STDL_DrawChar(NULL, &font, 0, 0, 'A', 1);
    {
        STDL_Surface *s = fresh(0);
        STDL_DrawChar(s, NULL, 0, 0, 'A', 1);
        STDL_FreeSurface(s);
    }

    if (failures == 0) {
        printf("single-glyph tests passed\n");
    }
    return failures != 0;
}
