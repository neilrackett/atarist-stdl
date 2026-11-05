/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * The clock, the cursor and the platform: STDL's clock, no cursor on a
 * responsive screen, and "Atari".
 */

#include "internal.h"

Uint32 SDL_GetTicks(void)
{
    return STDL_GetTicks();
}

Uint64 SDL_GetTicks64(void)
{
    static Uint32 last, wraps;
    const Uint32 now = STDL_GetTicks();
    if (now < last) {
        wraps++;
    }
    last = now;
    return (Uint64)wraps << 32 | now;
}

void SDL_Delay(Uint32 ms)
{
    STDL_Delay(ms);
}

/* Timer C's count from boot, which wraps in 31 hours, carried on in 64
 * bits; a program reading it less often than that sees every wrap */
Uint64 SDL_GetPerformanceCounter(void)
{
    static Uint32 last, wraps;
    const Uint32 now = STDL_GetHz38400();
    if (now < last) {
        wraps++;
    }
    last = now;
    return (Uint64)wraps << 32 | now;
}

Uint64 SDL_GetPerformanceFrequency(void)
{
    return 38400;
}

const char *SDL_GetPlatform(void)
{
    return "Atari";
}

int SDL_ShowCursor(int toggle)
{
    (void)toggle;
    return SDL_DISABLE;
}
