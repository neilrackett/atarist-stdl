/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * The conversion alone, as a program that draws its own chunky screen
 * uses it (SDL_stdl_c2p.h): only that header, which must compile as
 * gnu99 and C++98 with warnings as errors and with no SDL.h on the
 * path, built for the 68000, and linked by `make sdl2` (sdl2check)
 * against each of the add-on's archives with nothing else of STDL, no
 * libstdl.a. A link that brought the renderer (video.o) would fail
 * there on its calls into STDL. Never run.
 */

#include <SDL2/SDL_stdl_c2p.h>

static uint8_t chunky[2 * 16 * 2];
static uint8_t planes[4 * 16 * 2];

int main(int argc, char *argv[])
{
    const unsigned x = (unsigned)argc << 4;     /* X at run time */
    (void)argv;
    STDL_SDL2_ConvertBox(chunky, 32, planes, 32, 2, 2, 0, 0);
    STDL_SDL2_ConvertBox(chunky, 32, planes, 32, 2, 2, 1, 0x70);
    STDL_SDL2_ConvertBox(chunky, 32, planes, 32, 2, 2, 0, x);
    STDL_SDL2_ConvertBoxPairs(chunky, 32, planes, 32, 2, 2, 1, 0);
    STDL_SDL2_ConvertBoxPairs(chunky, 32, planes, 32, 2, 2, 0, x);
    return planes[0];
}
