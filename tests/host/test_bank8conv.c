/*
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
/*
 * stdlconv's 8-plane art, end to end (docs/format.md section 6): the
 * bank bank8conv.py makes with every 8-plane option, loaded on an
 * 8-plane screen that asks for it (STDL_RESPONSIVE_BANKS) and with no
 * such screen, against the pixels the script worked out without
 * stdlconv. Skipped where the host has no Pillow.
 *
 * On the machine (BANKCHK.TOS), built by hand from the root:
 *   STCMD_NO_TTY=1 stcmd m68k-atari-mint-gcc -O2 -std=gnu99 -Iinclude \
 *       -Isrc -Ilib/xpad/src -o dist/BANKCHK.TOS \
 *       tests/host/test_bank8conv.c libstdl.a
 * with test_bank8conv.dir's files beside it: the 8-plane art on a TT's
 * or a Falcon's screen, then the ST's on any machine, printing "B8:ok"
 * or the failures, then "B8:done".
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdl/stdl.h>
#include "responsive.h"

static int failures;

#define CHECK(cond, ...) do { \
    if (!(cond)) { \
        failures++; \
        fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__); \
        fprintf(stderr, __VA_ARGS__); \
        fprintf(stderr, "\n"); \
    } \
} while (0)

#ifdef __m68k__
#define DIR  ""                 /* beside the program */
#else
#define DIR  "test_bank8conv.dir/"
#endif
#define BANK DIR "BANK8.BNK"
#define W 48
#define H 7

static uint8_t b_px[W * H], bst_px[W * H], a_px[W * H];
static uint8_t pal8[256 * 3], pal4[16 * 3];

static int read_file(const char *path, uint8_t *buf, size_t n, size_t skip)
{
    FILE *f = fopen(path, "rb");
    uint8_t head[4];
    size_t got;

    if (f == NULL) {
        return -1;
    }
    if (skip && (fread(head, 1, 4, f) != 4
                 || head[0] != 0 || head[1] != W || head[2] != 0
                 || head[3] != H)) {
        fclose(f);
        return -1;
    }
    got = fread(buf, 1, n, f);
    fclose(f);
    return got == n ? 0 : -1;
}

static int mask_bit(const STDL_Surface *s, int x, int y)
{
    const uint16_t *m;

    if (s->mask == NULL) {
        return 0;
    }
    m = (const uint16_t *)(s->mask + (size_t)y * s->maskstride);
    return (m[x >> 4] >> (15 - (x & 15))) & 1;
}

/* a surface against the art: its pixels `px`, transparent where `clear` */
static void surface(const char *what, int id, int planes, const uint8_t *px,
                    const uint8_t *clear)
{
    STDL_Surface *s = STDL_LoadSurface(BANK, id);
    int x, y;

    CHECK(s != NULL && s->planes == planes && s->w == W && s->h == H,
          "%s: %d planes", what, s ? s->planes : -1);
    for (y = 0; s != NULL && y < H; y++) {
        for (x = 0; x < W; x++) {
            const int i = y * W + x;

            if (STDL_GetPixel(s, x, y) != px[i]
                || mask_bit(s, x, y) != clear[i]) {
                CHECK(0, "%s (%d,%d): %u mask %d, want %u mask %d", what,
                      x, y, STDL_GetPixel(s, x, y), mask_bit(s, x, y),
                      px[i], clear[i]);
                y = H;
                break;
            }
        }
    }
    STDL_FreeSurface(s);
}

/* a sprite's three frames drawn over colour `bg` on `dst` at an odd x,
 * pre-shifted and not */
static void sprite(const char *what, int id, int planes, STDL_Surface *dst,
                   int bg, const uint8_t *px, const uint8_t *clear)
{
    int pass, f, x, y;

    for (pass = 0; pass < 2; pass++) {
        STDL_Sprite *spr = STDL_LoadSprite(BANK, id,
                                           pass ? STDL_PRESHIFT : 0);

        CHECK(spr != NULL && spr->planes == planes, "%s: %d planes", what,
              spr ? spr->planes : -1);
        for (f = 0; spr != NULL && f < W / 16; f++) {
            STDL_FillRect(dst, NULL, (uint8_t)bg);
            STDL_BlitSprite(spr, f, dst, 21, 5);
            for (y = 0; y < H; y++) {
                for (x = 0; x < 16; x++) {
                    const int i = y * W + f * 16 + x;
                    const int want = clear[i] ? bg : px[i];

                    if (STDL_GetPixel(dst, 21 + x, 5 + y) != want) {
                        CHECK(0, "%s frame %d (%d,%d): %u, want %d", what,
                              f, x, y, STDL_GetPixel(dst, 21 + x, 5 + y),
                              want);
                        f = W;
                        y = H;
                        break;
                    }
                }
            }
        }
        STDL_FreeSprite(spr);
    }
}

static void palette(const char *what, int n, const uint8_t *rgb)
{
    static STDL_Colour cols[256];
    STDL_Palette pal = { 256, cols };
    int i;

    CHECK(STDL_LoadPalette(BANK, 0, &pal) == n, "%s: %d colours", what, n);
    for (i = 0; i < n; i++) {
        if (cols[i].r != rgb[i * 3] || cols[i].g != rgb[i * 3 + 1]
            || cols[i].b != rgb[i * 3 + 2]) {
            CHECK(0, "%s: colour %d", what, i);
            break;
        }
    }
}

int main(void)
{
    static uint8_t b_clear[W * H], a_clear[W * H], a_low[W * H];
    STDL_Surface *scr, *dst;
    int i;

#ifdef __m68k__
    if (STDL_Init(STDL_INIT_VIDEO) < 0) {
        fprintf(stderr, "B8:no STDL\nB8:done\n");
        return 1;
    }
#else
    {
        const int r = system("python3 bank8conv.py");

        if (r != 0) {
            /* 2: no Pillow, so no art to load */
            printf(WEXITSTATUS(r) == 2
                   ? "test_bank8conv: skipped (no Pillow)\n"
                   : "test_bank8conv: bank8conv.py failed\n");
            return WEXITSTATUS(r) == 2 ? 0 : 1;
        }
    }
#endif
    if (read_file(DIR "B.IDX", b_px, sizeof(b_px), 1) < 0
        || read_file(DIR "BST.IDX", bst_px, sizeof(bst_px), 1) < 0
        || read_file(DIR "A.IDX", a_px, sizeof(a_px), 1) < 0
        || read_file(DIR "PAL8.RGB", pal8, sizeof(pal8), 0) < 0
        || read_file(DIR "PAL4.RGB", pal4, sizeof(pal4), 0) < 0) {
        printf("test_bank8conv: the script's files are missing\n");
        return 1;
    }
    for (i = 0; i < W * H; i++) {
        b_clear[i] = b_px[i] == 200;
        a_clear[i] = (a_px[i] & 15) == 5;
        a_low[i] = (uint8_t)(a_px[i] & 15);
    }

    /* a TT's 8-plane screen (on the machine, a TT's or a Falcon's),
     * the bank's 8-plane art asked for */
#ifndef __m68k__
    stdl.mach.mch_cookie = 0x00020000UL;
#endif
    CHECK(STDL_UseResponsive(STDL_RESPONSIVE_COLOUR
                             | STDL_RESPONSIVE_BANKS) == 0, "use");
#ifdef __m68k__
    scr = STDL_SetVideoMode(320, 200, 8, 0);
    if (scr != NULL && !STDL_RESP_DEEP()) {
        scr = NULL;             /* an ST: its own 16 colours, below */
    }
#else
    scr = stdl_resp_video->set_mode(320, 200, 8, 0);
    CHECK(scr != NULL && STDL_RESP_DEEP(), "an 8-plane screen");
#endif
    if (scr != NULL) {
        palette("its own 256", 256, pal8);
        surface("its own art", 1, 8, b_px, b_clear);
        surface("art over the ST's", 2, 8, a_px, a_clear);
        sprite("its own sprite", 3, 8, scr, 77, b_px, b_clear);
        sprite("a sprite over the ST's", 4, 8, scr, 77, a_px, a_clear);
#ifndef __m68k__
        stdl_resp_video->shutdown();
        stdl_resp_screen = NULL;
#endif
    }
#ifdef __m68k__
    /* no 8-plane screen from here: an ST Low one, STDL still up (an
     * STE's BLiTTER, which draws the sprites, is supervisor-only) */
    if (scr != NULL && STDL_SetVideoMode(320, 200, 4, 0) == NULL) {
        CHECK(0, "an ST screen after the 8-plane one");
    }
    CHECK(!STDL_RESP_DEEP(), "no 8-plane screen");
#endif

    /* no 8-plane screen: the ST's chunks, made from the same art */
    palette("the ST's 16", 16, pal4);
    surface("the ST's mapping", 1, 4, bst_px, b_clear);
    surface("the ST's low nibbles", 2, 4, a_low, a_clear);
    dst = STDL_CreateSurface(64, 24);
    sprite("the ST's mapped sprite", 3, 4, dst, 7, bst_px, b_clear);
    sprite("the ST's low sprite", 4, 4, dst, 7, a_low, a_clear);
    STDL_FreeSurface(dst);

#ifdef __m68k__
    STDL_Quit();
    if (failures) {
        fprintf(stderr, "B8:%d failure(s)\nB8:done\n", failures);
    } else {
        fprintf(stderr, "B8:ok\nB8:done\n");
    }
    return failures != 0;
#else
    if (failures == 0) {
        printf("test_bank8conv: ok\n");
        return 0;
    }
    printf("test_bank8conv: %d failure(s)\n", failures);
    return 1;
#endif
}
