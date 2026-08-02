/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * Host-test stubs: the ST-specific globals and hardware fallbacks
 * that let the pure pixel-path modules link and run natively.
 */

#include <string.h>
#include "stdl_internal.h"

stdl_state_t stdl;
STDL_Surface stdl_screen;

/* the "hardware" behind the register macros on host builds */
volatile uint32_t stdl_host_clock;
volatile uint16_t stdl_host_hwpal[16];

static char errbuf[128];

const char *STDL_GetError(void)
{
    return errbuf;
}

void STDL_SetError(const char *msg)
{
    strncpy(errbuf, msg, sizeof(errbuf) - 1);
    errbuf[sizeof(errbuf) - 1] = '\0';
}

void STDL_WaitVBL(void)
{
    stdl_host_clock += 4;
}

STDL_Surface *STDL_GetVideoSurface(void)
{
    return NULL;
}
