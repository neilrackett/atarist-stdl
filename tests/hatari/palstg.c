/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * Palette staging, seen on screen.
 *
 * The screen is colour 1 throughout, and every frame colour 1 is
 * switched between red and blue from the main loop once the beam is
 * at line 100 of the picture. Written there, a change splits the
 * frame - old colour above the beam, new below - unless it is staged
 * to the vertical blank. Four phases of 100 frames each:
 *
 *   0  STDL_SetColours, unstaged
 *   1  STDL_SetColours, STDL_StagePalette(1)
 *   2  SDL_SetPalette(SDL_PHYSPAL), unstaged
 *   3  SDL_SetPalette(SDL_PHYSPAL), staged
 *
 * A marker in the top-left corner is drawn in colour 2 + phase, so a
 * capture sorts itself. Record it and count:
 *
 *   STCMD_NO_TTY=1 stcmd m68k-atari-mint-gcc -O2 -std=gnu99 \
 *       -Iinclude -Iinclude/compat -Ilib/xpad/src \
 *       -o dist/PALSTG.TOS tests/hatari/palstg.c libstdl.a
 *   MACHINE=st FF=off EXTRA="--avirecord on --avi-vcodec png \
 *       --avi-file $PWD/tmp/palstg.avi" \
 *       tests/hatari/run.sh ps dist/PALSTG.TOS 8 "waitfor PS:done"
 *   python3 tests/hatari/palsplit.py tmp/palstg.avi
 *
 * The pass: every frame of phases 0 and 2 split, none of 1 and 3.
 */
#include <stdio.h>
#include "SDL.h"

/* the beam's byte offset into the displayed page */
static unsigned long vcount(void)
{
    volatile unsigned char *r = (volatile unsigned char *)0xFFFF8205UL;

    return ((unsigned long)r[0] << 16) | ((unsigned long)r[2] << 8) | r[4];
}

int main(int argc, char *argv[])
{
    static const STDL_Colour marks[6] = {
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 255, 255, 255, 0 },
        { 255, 255, 0, 0 }, { 0, 255, 255, 0 }, { 255, 0, 255, 0 }
    };
    SDL_Color c[2] = { { 255, 0, 0, 0 }, { 0, 0, 255, 0 } };
    STDL_Surface *s = STDL_SetVideoMode(320, 200, 4, 0);
    SDL_Surface *sdl;
    STDL_Rect mark = { 0, 0, 16, 16 };
    unsigned long base;
    int phase, f;

    (void)argc;
    (void)argv;
    if (s == NULL) {
        return 1;
    }
    sdl = SDL_GetVideoSurface();
    STDL_SetColours(s, marks, 0, 6);
    STDL_FillRect(s, NULL, 1);
    base = (unsigned long)s->pixels;
    for (phase = 0; phase < 4; phase++) {
        STDL_StagePalette(phase & 1);
        STDL_FillRect(s, &mark, (uint8_t)(2 + phase));
        for (f = 0; f < 100; f++) {
            STDL_WaitVBL();
            while (vcount() < base + 100UL * 160) {
            }
            if (phase < 2) {
                STDL_Colour sc;

                sc.r = c[f & 1].r;
                sc.g = c[f & 1].g;
                sc.b = c[f & 1].b;
                sc.unused = 0;
                STDL_SetColours(s, &sc, 1, 1);
            } else {
                SDL_SetPalette(sdl, SDL_PHYSPAL, &c[f & 1], 1, 1);
            }
        }
    }
    STDL_StagePalette(0);
    STDL_Quit();
    printf("PS:done\n");
    return 0;
}
