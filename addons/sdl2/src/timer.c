/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * The clock and the cursor: STDL's clock, and no cursor on a responsive
 * screen.
 */

#include "internal.h"

Uint32 SDL_GetTicks(void)
{
    return STDL_GetTicks();
}

void SDL_Delay(Uint32 ms)
{
    STDL_Delay(ms);
}

int SDL_ShowCursor(int toggle)
{
    (void)toggle;
    return SDL_DISABLE;
}
