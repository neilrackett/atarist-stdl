/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * Helpers shared by the sound host tests. The YM register layout
 * and the VBL queue scan were copied into each of test_ym.c,
 * test_tone.c and test_opl.c; one copy is enough, and unlike the
 * library this costs no m68k text because tests/host is never
 * linked into anything that runs on target.
 */
#ifndef STDL_YMTEST_H
#define STDL_YMTEST_H

#include "stdl_internal.h"

/* the routine the sound service put in the fake VBL queue, which
 * a test calls where the hardware would. STDL_NVBLS, not a literal
 * 8: two of the three copies hard-coded it and would have stopped
 * scanning the whole queue without anything noticing. */
static void (*vbl_fn)(void);

static void find_vbl(void)
{
    int i;

    vbl_fn = NULL;
    for (i = 0; i < STDL_NVBLS; i++) {
        if (STDL_VBLQUEUE[i] != NULL) {
            vbl_fn = STDL_VBLQUEUE[i];
        }
    }
}

/* how many queue slots are claimed - the sound service takes one */
static int slots_used(void)
{
    int i, n = 0;

    for (i = 0; i < STDL_NVBLS; i++) {
        if (STDL_VBLQUEUE[i] != NULL) {
            n++;
        }
    }
    return n;
}

/* a voice's programmed period, from the register file stub */
static uint16_t voice_period(int v)
{
    return (uint16_t)(stdl_host_ym[2 * v]
                      | ((stdl_host_ym[2 * v + 1] & 0x0F) << 8));
}

#endif
