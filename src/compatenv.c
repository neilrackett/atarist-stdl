/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * SDL 1.2's SDL_putenv, "NAME=value", on setenv: mintlib and libcmini
 * both have that one, and libcmini has no putenv. The variable is the
 * C library's and nothing more: STDL reads none, so one a program sets
 * to choose SDL's driver or to centre its window changes nothing. An
 * object of its own, for the programs that call it.
 */

#include <SDL.h>

int SDL_putenv(const char *variable)
{
    char name[64];
    const char *eq = strchr(variable, '=');
    size_t n = eq ? (size_t)(eq - variable) : 0;

    if (n == 0 || n >= sizeof(name)) {
        return -1;
    }
    memcpy(name, variable, n);
    name[n] = '\0';
    return setenv(name, eq + 1, 1);
}
