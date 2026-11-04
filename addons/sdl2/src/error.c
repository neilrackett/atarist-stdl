/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * The error string. Built for the 68000 with init.c: SDL_Init's
 * refusal on an ST comes through here.
 */

#include "internal.h"

/* SDL2 keeps 1K; every message the add-on writes fits in a line */
static char err[128];

int SDL_SetError(const char *fmt, ...)
{
    if (fmt) {
        va_list ap;
        va_start(ap, fmt);
        vsnprintf(err, sizeof(err), fmt, ap);
        va_end(ap);
    }
    return -1;
}

const char *SDL_GetError(void)
{
    return err;
}

void SDL_ClearError(void)
{
    err[0] = '\0';
}
