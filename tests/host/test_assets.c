/*
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
/* Asset loaders end-to-end: the tracked example assets load, the
 * colour key survives at index 15, and keyed blits respect it. */
#include <stdio.h>
#include <stdl/stdl.h>

static int failures;
#define CHECK(c, ...) do { if (!(c)) { failures++; \
    printf("FAIL %d: ", __LINE__); printf(__VA_ARGS__); \
    printf("\n"); } } while (0)

/*
 * STDL_DrawText against a per-glyph-bit reference: set bits take the
 * colour, clear bits leave the destination alone, and everything is
 * clipped to dst->clip. Exercised at every 16-pixel phase and with
 * the text running off all four edges.
 */
static uint8_t glyph_bits[16 * 8];      /* 16 glyphs, 8 rows, 8 wide */

static void draw_text_ref(STDL_Surface *dst, int x, int y,
                          const char *text, uint8_t col,
                          const STDL_Rect *clip)
{
    int i, row, bit;

    for (i = 0; text[i] != '\0'; i++, x += 8) {
        uint8_t c = (uint8_t)text[i];
        if (c > 15) {
            continue;
        }
        for (row = 0; row < 8; row++) {
            uint8_t g = glyph_bits[c * 8 + row];
            for (bit = 0; bit < 8; bit++) {
                int px = x + bit, py = y + row;
                if ((g & (0x80 >> bit)) == 0) continue;
                if (px < clip->x || px >= clip->x + clip->w) continue;
                if (py < clip->y || py >= clip->y + clip->h) continue;
                STDL_PutPixel(dst, px, py, col);
            }
        }
    }
}

static void test_drawtext(void)
{
    STDL_Font font;
    STDL_Surface *got, *want;
    STDL_Rect clip;
    unsigned seed = 7;
    int t, x, y;

    for (t = 0; t < (int)sizeof(glyph_bits); t++) {
        seed = seed * 1103515245u + 12345u;
        glyph_bits[t] = (uint8_t)(seed >> 20);
    }
    font.cw = 8;
    font.ch = 8;
    font.first = 0;
    font.last = 15;
    font.bytes_per_row = 1;
    font.bits = glyph_bits;

    got = STDL_CreateSurface(83, 47);
    want = STDL_CreateSurface(83, 47);

    for (t = 0; t < 400; t++) {
        static const char msg[] = "\1\2\3\4\5\6\7\10\11\12";
        uint8_t col;
        int usecl;

        seed = seed * 1103515245u + 12345u;
        x = (int)((seed >> 8) % 120) - 20;
        seed = seed * 1103515245u + 12345u;
        y = (int)((seed >> 8) % 70) - 10;
        seed = seed * 1103515245u + 12345u;
        col = (uint8_t)((seed >> 8) & 15);
        usecl = (t & 3) == 0;

        STDL_FillRect(got, NULL, 5);
        STDL_FillRect(want, NULL, 5);
        clip.x = 0; clip.y = 0; clip.w = 83; clip.h = 47;
        if (usecl) {
            clip.x = 9; clip.y = 5; clip.w = 40; clip.h = 20;
        }
        STDL_SetClipRect(got, usecl ? &clip : NULL);
        STDL_SetClipRect(want, NULL);

        STDL_DrawText(got, &font, x, y, msg, col);
        draw_text_ref(want, x, y, msg, col, &clip);

        {
            int bad = 0, px, py;
            for (py = 0; py < 47 && bad < 3; py++) {
                for (px = 0; px < 83 && bad < 3; px++) {
                    uint8_t a = STDL_GetPixel(got, px, py);
                    uint8_t b = STDL_GetPixel(want, px, py);
                    if (a != b) {
                        bad++;
                        printf("  text (%d,%d) x=%d y=%d col=%d "
                               "clip=%d: got %d want %d\n",
                               px, py, x, y, col, usecl, a, b);
                    }
                }
            }
            CHECK(bad == 0, "DrawText iteration %d", t);
            if (bad) break;
        }
    }

    STDL_FreeSurface(got);
    STDL_FreeSurface(want);
}

/*
 * Sprites narrower than a group. A frame only has to start on a
 * 16-pixel boundary, so a single frame can be any width: the pixels
 * past frame_w must come out transparent, at every destination phase
 * and pre-shifted or not. Koules' cast is 6, 8, 12, 14 and 24 wide.
 */
static void test_odd_width_sprite(uint32_t flags, const char *what)
{
    static const int widths[] = { 1, 6, 8, 12, 14, 15, 17, 24, 31 };
    int wi;

    for (wi = 0; wi < (int)(sizeof(widths) / sizeof(widths[0])); wi++) {
        int w = widths[wi];
        int h = 5, x, px, py;
        STDL_Surface *src = STDL_CreateSurface(w, h);
        STDL_Sprite *spr;

        /* solid colour 3 over the whole frame; 0 is the key, so
         * anything outside it must leave the destination alone */
        STDL_FillRect(src, NULL, 3);
        STDL_SetColourKey(src, 1, 0);
        spr = STDL_SpriteFromSurface(src, w, flags);
        CHECK(spr != NULL, "%s w=%d: %s", what, w, STDL_GetError());
        if (spr == NULL) {
            STDL_FreeSurface(src);
            continue;
        }
        CHECK(spr->w == w, "%s w=%d: sprite w %d", what, w, spr->w);
        CHECK(spr->nframes == 1, "%s w=%d: %d frames", what, w,
              spr->nframes);

        for (x = 0; x < 17; x++) {
            STDL_Surface *dst = STDL_CreateSurface(64, 8);
            STDL_FillRect(dst, NULL, 6);
            STDL_BlitSprite(spr, 0, dst, x, 1);
            for (py = 0; py < 8; py++) {
                for (px = 0; px < 64; px++) {
                    int inside = (px >= x && px < x + w
                                  && py >= 1 && py < 1 + h);
                    uint8_t want = inside ? 3 : 6;
                    uint8_t got = STDL_GetPixel(dst, px, py);
                    if (got != want) {
                        CHECK(0, "%s w=%d x=%d (%d,%d) got %d want %d",
                              what, w, x, px, py, got, want);
                        px = 64; py = 8;
                    }
                }
            }
            STDL_FreeSurface(dst);
        }
        STDL_FreeSprite(spr);
        STDL_FreeSurface(src);
    }
}

/* A strip of frames still has to be group-aligned, and the library
 * has to say so rather than quietly drawing the wrong pixels. */
static void test_sprite_frame_rules(void)
{
    STDL_Surface *s = STDL_CreateSurface(48, 4);
    STDL_Sprite *spr;

    STDL_FillRect(s, NULL, 2);
    spr = STDL_SpriteFromSurface(s, 16, 0);
    CHECK(spr != NULL && spr->nframes == 3, "48/16 gives 3 frames");
    STDL_FreeSprite(spr);

    CHECK(STDL_SpriteFromSurface(s, 12, 0) == NULL,
          "12-wide frames in a 48-wide strip must be rejected");
    CHECK(STDL_SpriteFromSurface(s, 64, 0) == NULL,
          "frame wider than the surface must be rejected");
    CHECK(STDL_SpriteFromSurface(s, 0, 0) == NULL,
          "zero-wide frame must be rejected");

    spr = STDL_SpriteFromSurface(s, 48, 0);
    CHECK(spr != NULL && spr->nframes == 1, "whole surface, 1 frame");
    STDL_FreeSprite(spr);
    STDL_FreeSurface(s);
}

int main(void)
{
    STDL_Surface *sail, *icon, *dst;
    uint8_t key;
    STDL_Rect d;

    test_drawtext();
    test_odd_width_sprite(0, "plain");
    test_odd_width_sprite(STDL_PRESHIFT, "preshift");
    test_sprite_frame_rules();

    sail = STDL_LoadBMP("../../examples/assets/SAIL.BMP");
    CHECK(sail != NULL, "SAIL.BMP load: %s", STDL_GetError());
    if (sail == NULL) {
        return 1;
    }
    CHECK(sail->w == 147 && sail->h == 105, "SAIL dimensions");

    /* the magenta colour key must sit at index 15 exactly
     * (stdlconv bmp16 --keycolor pins it there) */
    key = STDL_MapRGB(sail->format, 0xFF, 0x00, 0xFF);
    CHECK(key == 15, "key index %d, want 15", key);
    CHECK(STDL_GetPixel(sail, 0, 0) == 15, "corner is key colour");
    STDL_SetColourKey(sail, 1, key);

    /* keyed blit: background shows through where the key was */
    dst = STDL_CreateSurface(160, 120);
    STDL_FillRect(dst, NULL, 6);
    d.x = 0; d.y = 0;
    STDL_BlitSurface(sail, NULL, dst, &d);
    CHECK(STDL_GetPixel(dst, 0, 0) == 6, "key pixel preserved dst");
    CHECK(STDL_SurfaceIsOpaque(sail) == 0, "sail has transparency");

    icon = STDL_LoadBMP("../../examples/assets/ICON.BMP");
    CHECK(icon != NULL, "ICON.BMP load: %s", STDL_GetError());
    if (icon != NULL) {
        CHECK(icon->w == 32 && icon->h == 32, "ICON dimensions");
        STDL_FreeSurface(icon);
    }

    STDL_FreeSurface(sail);
    STDL_FreeSurface(dst);
    if (failures == 0) {
        printf("all asset tests passed\n");
        return 0;
    }
    printf("%d failure(s)\n", failures);
    return 1;
}
