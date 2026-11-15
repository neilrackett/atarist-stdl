/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * test_sdl1's SDL2 side: what it needs that only a file including the
 * SDL2 add-on's headers can say, and the two STDL calls the SDL1
 * add-on makes that the SDL2 add-on's stand-in (fake_stdl.c) has not
 * got.
 */

#include "SDL.h"
#include "SDL_stdl.h"
#include <stdl/stdl.h>
#include <string.h>

/* the hints' names as the SDL2 add-on has them, in the order of the
 * SDL1 add-on's STDL_SDL1_HINT_ macros */
const char *const sdl2_hint_names[4] = {
    STDL_SDL2_HINT_PAGES, STDL_SDL2_HINT_CHECK, STDL_SDL2_HINT_ST_TALL,
    STDL_SDL2_HINT_ST_DITHER
};

char test_error[256];

void STDL_SetError(const char *msg)
{
    strncpy(test_error, msg, sizeof(test_error) - 1);
}

int STDL_WaitEvent(STDL_Event *e)
{
    return STDL_PollEvent(e);
}
