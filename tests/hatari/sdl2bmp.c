/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * On-target check of the SDL2 add-on's SDL_LoadBMP
 * (addons/sdl2/src/bmp.c) on a TT or a Falcon: art converted by
 * `stdlconv bmp8` as a port would convert its own, loaded from the
 * GEMDOS drive and shown. sdl2bmp.py draws a picture and a sheet of
 * balls on a transparent background and converts them together, so
 * they share one palette, index 0 black and the key at 255. Before
 * the screen opens this loads them and the core's 4-bit SAIL.BMP and
 * prints each one's size, colour count and a hash of its pixels and
 * palette, which must be what sdl2bmp.py prints for the same files,
 * read by Pillow. Then it shows the picture with the balls blitted
 * over it through their key, in the files' own palette, until Esc
 * (or 60 seconds), and prints S2B:done.
 *
 *   STCMD_NO_TTY=1 stcmd make && STCMD_NO_TTY=1 stcmd make sdl2
 *   STCMD_NO_TTY=1 stcmd m68k-atari-mint-gcc -O2 -std=gnu99 \
 *       -m68020-60 -msoft-float -Iinclude -Iaddons/sdl2/include/SDL2 \
 *       -c -o obj/sdl2bmp.o tests/hatari/sdl2bmp.c
 *   STCMD_NO_TTY=1 stcmd m68k-atari-mint-gcc -m68000 \
 *       -o dist/SDL2BMP.TOS obj/sdl2bmp.o libstdl-sdl2.a libstdl.a
 *   python3 tests/hatari/sdl2bmp.py      # the BMPs into dist/
 *   SDL_VIDEODRIVER=dummy TOS=<etos512uk.img> MACHINE=tt \
 *       EXTRA="--cpu-exact on --compatible on --memsize 4" \
 *       tests/hatari/run.sh s2b dist/SDL2BMP.TOS 8 \
 *       "waitfor S2B:shown;sleep 3;shot;key 0x01;waitfor S2B:done"
 *
 * and MACHINE=falcon with MONITOR=vga and MONITOR=rgb.
 */

#include "SDL.h"
#include "SDL_stdl.h"

#include <stdio.h>

static Uint32 fnv(const Uint8 *p, long n, Uint32 h)
{
    while (n-- > 0) {
        h = (h ^ *p++) * 0x01000193UL;
    }
    return h;
}

/* the file's size, colours and hash, as sdl2bmp.py prints them */
static SDL_Surface *load(const char *name)
{
    SDL_Surface *s = SDL_LoadBMP(name);
    const SDL_Palette *pal;
    Uint32 h = 0x811C9DC5UL;
    int y, i;
    if (!s) {
        printf("S2B:%s FAIL %s\n", name, SDL_GetError());
        return NULL;
    }
    pal = s->format->palette;
    for (y = 0; y < s->h; y++) {
        h = fnv((const Uint8 *)s->pixels + y * s->pitch, s->w, h);
    }
    for (i = 0; i < pal->ncolors; i++) {
        h = fnv(&pal->colors[i].r, 3, h);
    }
    printf("S2B:%s %dx%d n=%d hash=%08lx\n", name, s->w, s->h,
           pal->ncolors, (unsigned long)h);
    return s;
}

int main(int argc, char *argv[])
{
    SDL_Window *win;
    SDL_Renderer *ren;
    SDL_Texture *tex;
    SDL_Surface *pic, *spr, *sail;
    SDL_Palette *pal;
    SDL_Rect from, to;
    SDL_Event e;
    Uint32 start;
    int i, quit = 0;
    (void)argc;
    (void)argv;

    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        printf("S2B:%s\nS2B:done\n", SDL_GetError());
        return 1;
    }
    pic = load("S2BPIC.BMP");
    spr = load("S2BSPR.BMP");
    sail = load("SAIL.BMP");
    /* what is refused, and why */
    if (SDL_LoadBMP("NOFILE.BMP") == NULL) {
        printf("S2B:NOFILE.BMP refused: %s\n", SDL_GetError());
    }
    SDL_FreeSurface(sail);
    if (!pic || !spr) {
        printf("S2B:done\n");
        SDL_Quit();
        return 1;
    }

    /* the balls over the picture through their key, 255, which
     * stdlconv gave the transparent pixels and no colour else */
    SDL_SetColorKey(spr, SDL_TRUE, 255);
    for (i = 0; i < 9; i++) {
        from.x = (i % 3) * 32;
        from.y = 0;
        from.w = from.h = 32;
        to.x = 24 + i * 32;
        to.y = 120 + (i & 1) * 40 + (i % 3) * 8;
        SDL_BlitSurface(spr, &from, pic, &to);
    }

    /* printed before the screen opens: on a TT a console write would
     * stay in the picture */
    printf("S2B:shown\n");
    win = SDL_CreateWindow("SDL2BMP", SDL_WINDOWPOS_CENTERED,
                           SDL_WINDOWPOS_CENTERED, 320, 240, 0);
    ren = win ? SDL_CreateRenderer(win, -1, SDL_RENDERER_PRESENTVSYNC)
              : NULL;
    if (!ren) {
        SDL_Quit();
        printf("S2B:%s\nS2B:done\n", SDL_GetError());
        return 1;
    }
    /* the files' one palette is the screen's; its index 0 is black,
     * so the border needs no border index */
    pal = SDL_AllocPalette(256);
    SDL_SetPaletteColors(pal, pic->format->palette->colors, 0,
                         pic->format->palette->ncolors);
    STDL_SDL2_SetRenderPalette(ren, pal);
    tex = SDL_CreateTextureFromSurface(ren, pic);
    SDL_RenderCopy(ren, tex, NULL, NULL);
    SDL_RenderPresent(ren);

    start = SDL_GetTicks();
    while (!quit && !SDL_TICKS_PASSED(SDL_GetTicks(), start + 60000)) {
        while (SDL_PollEvent(&e)) {
            quit |= e.type == SDL_QUIT || (e.type == SDL_KEYDOWN &&
                                           e.key.keysym.sym == SDLK_ESCAPE);
        }
        SDL_Delay(20);
    }
    SDL_DestroyTexture(tex);
    SDL_DestroyRenderer(ren);
    SDL_FreePalette(pal);
    SDL_DestroyWindow(win);
    SDL_FreeSurface(spr);
    SDL_FreeSurface(pic);
    SDL_Quit();
    printf("S2B:done\n");
    return 0;
}
