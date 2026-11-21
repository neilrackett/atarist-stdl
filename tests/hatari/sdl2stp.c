/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * On-target timing of pixel pairs on an ST's screen, in the SDL2
 * add-on's experimental 68000 build (addons/sdl2/src/stpair.c): the
 * pair loop against st.c's whole one on a 320x168 box of pairs (and
 * the same pages from both, byte for byte), the pair tables' making
 * against the conversion's own, and then the renderer on ST Low:
 * frames of new pixels in pairs copied with SDL_RenderCopy and with
 * STDL_SDL2_RenderCopyPairs, the whole screen and a low-detail view
 * of 320x168 in pairs with 32 rows of whole pixels under it, as a
 * game's view and status bar, from one texture. A frame's time is the
 * copies and the present less the present's flip, which waits for the
 * display. Every frame changes every group (two pictures in turn, of
 * the same 15 colours, so the maps are made once, before the timing).
 * Last, the whole screen again with the program's own 16 colours given
 * (STDL_SDL2_SetSTColours), with which no colour is noted as it comes:
 * what that noting costs a frame.
 *
 *   STCMD_NO_TTY=1 stcmd make sdl2st
 *   STCMD_NO_TTY=1 stcmd m68k-atari-mint-gcc -O2 -std=gnu99 -m68000 \
 *       -DSTDL_SDL2_ST -Iinclude -Iaddons/sdl2/include/SDL2 \
 *       -Iaddons/sdl2/src -c -o obj/sdl2stp.o tests/hatari/sdl2stp.c
 *   STCMD_NO_TTY=1 stcmd m68k-atari-mint-gcc -m68000 \
 *       -o dist/SDL2STP.TOS obj/sdl2stp.o libstdl-sdl2-st.a libstdl.a
 *   TOS="<TOS 2.06>" MACHINE=st EXTRA="--memsize 4" \
 *       tests/hatari/run.sh s2p dist/SDL2STP.TOS 8 \
 *       "waitfor STP:done;waitfor STP:done"
 *
 * MACHINE=st for 8MHz, megaste for 16MHz with the cache. It prints
 * STP: lines and STP:done.
 */
#include <stdio.h>
#include <string.h>
#include "SDL.h"
#include "SDL_stdl.h"
#include "SDL_stdl_bench.h"
#include "st.h"
#include <stdl/stdl.h>
#include "../../examples/hold.h"

#define W 320
#define H 200
#define VIEW 168                /* the view's rows; the bar's are below */
#define FRAMES 8

static Uint8 pic[2][W * H];
static Uint8 page_w[W / 2 * H], page_p[W / 2 * H];
static Uint32 tabs[2 * 1024], ptabs[2 * 1024];

static unsigned long seed = 0x5A1D;

static unsigned rnd(void)
{
    seed = seed * 1103515245UL + 12345UL;
    return (unsigned)(seed >> 16) & 0x7FFF;
}

/* ms, to two places, from 38,400ths of a second over n */
static void ms(const char *what, Uint32 t, int n)
{
    const unsigned long c = (unsigned long)t * 1000UL / 384UL / (unsigned)n;
    printf("STP: %s %lu.%02lu ms\n", what, c / 100, c % 100);
}

/* the loops alone, the view's 320x168 box from pic[0], in pairs */
static void loops(void)
{
    Uint8 a[256], b[256];
    Uint32 t0, t1, t2;
    int i;
    for (i = 0; i < 256; i++) {
        a[i] = (Uint8)(rnd() & 15);
        b[i] = (Uint8)(rnd() & 15);
    }
    t0 = STDL_GetHz38400();
    for (i = 0; i < 4; i++) {
        stdl_sdl2_st_spread(a, b, tabs);
    }
    t1 = STDL_GetHz38400();
    for (i = 0; i < 4; i++) {
        stdl_sdl2_st_pairtabs(tabs, ptabs);
    }
    t2 = STDL_GetHz38400();
    ms("the conversion's tables made", t1 - t0, 4);
    ms("the pair tables made from them", t2 - t1, 4);
    t0 = STDL_GetHz38400();
    for (i = 0; i < 2; i++) {
        stdl_sdl2_st_c2p(pic[0], W, page_w, W / 2, W / 16, VIEW, tabs,
                         tabs + 1024);
    }
    t1 = STDL_GetHz38400();
    for (i = 0; i < 2; i++) {
        stdl_sdl2_st_c2pp(pic[0], W, page_p, W / 2, W / 16, VIEW, ptabs,
                          ptabs + 1024);
    }
    t2 = STDL_GetHz38400();
    ms("320x168 through the whole loop", t1 - t0, 2);
    ms("320x168 through the pair loop", t2 - t1, 2);
    printf("STP: the pages %s\n",
           memcmp(page_w, page_p, sizeof(page_w)) ? "DIFFER" : "the same");
}

/* frames of new pixels: rows [0, split) copied one way and [split, H)
 * the other, `pairs` saying which of the two is pairs; 0 if none */
static Uint32 frames(SDL_Renderer *ren, SDL_Texture *tex[2], int split,
                     int pairs)
{
    Uint32 total = 0, conv, flip;
    SDL_Rect top, bot;
    int f;
    top.x = bot.x = 0;
    top.w = bot.w = W;
    top.y = 0;
    top.h = split;
    bot.y = split;
    bot.h = H - split;
    STDL_SDL2_GetPresentTimes(ren, &conv, &flip, 1);
    for (f = 0; f < FRAMES; f++) {
        SDL_Texture *t = tex[f & 1];
        Uint32 t0 = STDL_GetHz38400();
        if (pairs) {
            STDL_SDL2_RenderCopyPairs(ren, t, &top, &top);
        } else {
            SDL_RenderCopy(ren, t, &top, &top);
        }
        if (split < H) {
            SDL_RenderCopy(ren, t, &bot, &bot);
        }
        SDL_RenderPresent(ren);
        total += STDL_GetHz38400() - t0;
    }
    STDL_SDL2_GetPresentTimes(ren, &conv, &flip, 1);
    return total - flip;
}

int main(int argc, char *argv[])
{
    SDL_Window *win;
    SDL_Renderer *ren;
    SDL_Texture *tex[2];
    SDL_Palette *pal;
    SDL_Color c[256];
    int i, x, y;
    (void)argc;
    (void)argv;
    hold_on_exit("SDL2STP");
    /* 15 colours and black, the pictures' pairs of them; the second
     * picture each colour the next, so every group changes */
    for (y = 0; y < H; y++) {
        for (x = 0; x < W; x += 2) {
            const Uint8 v = (Uint8)(1 + rnd() % 15);
            pic[0][y * W + x] = pic[0][y * W + x + 1] = v;
            pic[1][y * W + x] = pic[1][y * W + x + 1] = (Uint8)(1 + v % 15);
        }
    }
    /* the bar's rows not in pairs */
    for (y = VIEW; y < H; y++) {
        for (x = 0; x < W; x++) {
            pic[0][y * W + x] = (Uint8)(1 + rnd() % 15);
            pic[1][y * W + x] = (Uint8)(1 + pic[0][y * W + x] % 15);
        }
    }
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        printf("STP: %s\nSTP:done\n", SDL_GetError());
        return 1;
    }
    loops();
    win = SDL_CreateWindow("SDL2STP", 0, 0, W, H, 0);
    ren = win ? SDL_CreateRenderer(win, -1, 0) : NULL;
    if (!ren) {
        printf("STP: %s\nSTP:done\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }
    pal = SDL_AllocPalette(256);
    for (i = 0; i < 256; i++) {
        c[i].r = (Uint8)(i * 37);
        c[i].g = (Uint8)(i * 91);
        c[i].b = (Uint8)(i * 13);
        c[i].a = 255;
    }
    c[0].r = c[0].g = c[0].b = 0;
    SDL_SetPaletteColors(pal, c, 0, 256);
    STDL_SDL2_SetRenderPalette(ren, pal);
    for (i = 0; i < 2; i++) {
        tex[i] = SDL_CreateTexture(ren, SDL_PIXELFORMAT_INDEX8,
                                   SDL_TEXTUREACCESS_STATIC, W, H);
        SDL_UpdateTexture(tex[i], NULL, pic[i], W);
    }
    /* the maps made, and every page drawn on, before any timing */
    for (i = 0; i < 4; i++) {
        STDL_SDL2_RenderCopyPairs(ren, tex[i & 1], NULL, NULL);
        SDL_RenderPresent(ren);
    }
    ms("a frame of 320x200, whole", frames(ren, tex, H, 0), FRAMES);
    ms("a frame of 320x200, pairs", frames(ren, tex, H, 1), FRAMES);
    ms("a frame of a 320x168 view and a bar, whole",
       frames(ren, tex, VIEW, 0), FRAMES);
    ms("a frame of a 320x168 view as pairs and a bar",
       frames(ren, tex, VIEW, 1), FRAMES);
    {
        /* the program's own 16 (STDL_SDL2_SetSTColours): no colour is
         * noted as copies bring it, which is the rest of a copy's cost
         * beside the compare and the conversion */
        Uint8 map[256];
        for (i = 0; i < 256; i++) {
            map[i] = (Uint8)(i < 16 ? i : 0);
        }
        STDL_SDL2_SetSTColours(ren, map, c);
        for (i = 0; i < 4; i++) {
            STDL_SDL2_RenderCopyPairs(ren, tex[i & 1], NULL, NULL);
            SDL_RenderPresent(ren);
        }
        ms("given 16, a frame of 320x200, whole", frames(ren, tex, H, 0),
           FRAMES);
        ms("given 16, a frame of 320x200, pairs", frames(ren, tex, H, 1),
           FRAMES);
    }
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
    printf("STP:done\n");
    return 0;
}
