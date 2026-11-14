/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * SDL_Init and friends. Built for the 68000 (sdl2.mk), where the
 * add-on's drawing is built for the 68020: an SDL2 program started on
 * an ST arrives here, and must get an error it can print rather than
 * an illegal instruction on the way. Nothing here calls into code
 * built for the 68020 before the machine is known.
 */

#include "internal.h"

Uint32 stdl_sdl2_inited;

int SDL_InitSubSystem(Uint32 flags)
{
    if (flags & SDL_INIT_GAMECONTROLLER) {
        flags |= SDL_INIT_JOYSTICK;
    }
    if (flags & (SDL_INIT_VIDEO | SDL_INIT_JOYSTICK)) {
        flags |= SDL_INIT_EVENTS;
    }
    /* STDL_Init ignores its flags and returns 0 at once when the
     * program called it first, so asking for everything is asking
     * for what there is */
    if (STDL_Init(STDL_INIT_EVERYTHING) < 0) {
        return SDL_SetError("%s", STDL_GetError());
    }
#ifndef STDL_SDL2_ST
    /* _MCH 2 is a TT and 3 a Falcon; an ST, an STE or a Mega STE has
     * no 256-colour screen, and an ST with a 68030 card runs this code
     * but has neither machine's modes */
    if (STDL_SDL2_NO_256()) {
        return SDL_SetError("This program needs a TT or a Falcon");
    }
#endif
    stdl_sdl2_inited |= flags & SDL_INIT_EVERYTHING;
    return 0;
}

int SDL_Init(Uint32 flags)
{
    return SDL_InitSubSystem(flags);
}

void SDL_QuitSubSystem(Uint32 flags)
{
    stdl_sdl2_inited &= ~flags;
}

Uint32 SDL_WasInit(Uint32 flags)
{
    return flags ? (stdl_sdl2_inited & flags) : stdl_sdl2_inited;
}

void SDL_Quit(void)
{
    stdl_sdl2_inited = 0;
    STDL_Quit();
}

void SDL_GetVersion(SDL_version *ver)
{
    if (ver) {
        SDL_VERSION(ver);
    }
}
