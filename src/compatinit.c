/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * SDL 1.2's start and stop, for SDL_WasInit. STDL has no subsystems:
 * STDL_Init brings up everything whatever it is asked for, and
 * STDL_Quit puts it all back. So these only keep what a program asked
 * for, which is what SDL_WasInit answers, and a program that starts
 * its sound only where SDL_WasInit(SDL_INIT_AUDIO) says it has not
 * (one port's did) takes the same path as on SDL. An object of its
 * own: every program on the shim or the SDL1 add-on calls SDL_Init,
 * and compat.o would bring the planar drawing with it.
 */

#include <SDL.h>

static Uint32 asked;

int SDL_Init(Uint32 flags)
{
    asked |= flags;
    return STDL_Init(flags);
}

void SDL_QuitSubSystem(Uint32 flags)
{
    asked &= ~flags;
}

Uint32 SDL_WasInit(Uint32 flags)
{
    return flags ? (asked & flags) : asked;
}

void SDL_Quit(void)
{
    asked = 0;
    STDL_Quit();
}
