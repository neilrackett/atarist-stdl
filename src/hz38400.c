/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * STDL_GetHz38400: the 200Hz clock with the count inside its tick.
 * An object of its own, as time.c is in every program and this is
 * for the programs that profile.
 */

#include "stdl_internal.h"

#define MFP_TCDR    (*(volatile uint8_t *)0xFFFFFA23UL)

uint32_t STDL_GetHz38400(void)
{
#ifdef __m68k__
    uint32_t t;
    uint8_t c;

    /* Timer C counts 192 down to 1 a 200Hz tick; read again if the
     * tick moved on between the two reads */
    do {
        t = STDL_HZ200;
        c = MFP_TCDR;
    } while (t != STDL_HZ200);
    return (t << 7) + (t << 6) + (uint32_t)(192 - c);
#else
    return (STDL_HZ200 << 7) + (STDL_HZ200 << 6);
#endif
}
