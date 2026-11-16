/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * The line STDL_Init's first call writes to the console: the library,
 * the release it was built from and where it lives, as a web library
 * says its name in a browser's console. On unless STDL_UseBanner(0)
 * came first.
 *
 * An object of its own, so that a new release changes this one and no
 * other: a port that compares its binary before and after a move to a
 * newer STDL finds the version's bytes here and the rest as it was.
 */

#include <stdl/stdl.h>
#include "stdl_internal.h"
#include <mint/osbind.h>

/* " v1.30.0": the release tag the tree was built at, from the
 * Makefile's obj/version.h, with a + after it for a tree past its tag;
 * nothing where git could not say, or in a build that is not the
 * Makefile's */
#ifndef STDL_BANNER_VERSION
#define STDL_BANNER_VERSION ""
#endif

static uint8_t off, shown;

int STDL_UseBanner(int on)
{
    const int was = !off;
    if (on >= 0) {
        off = (uint8_t)(on == 0);
    }
    return was;
}

/* once a program, however often it inits */
void stdl_banner(void)
{
    if (!off && !shown) {
        shown = 1;
        (void)Cconws("STDL" STDL_BANNER_VERSION
                     " | neilrackett.com/atarist\r\n");
    }
}
