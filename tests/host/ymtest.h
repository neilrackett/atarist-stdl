/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * Helpers shared by the sound host tests. The YM register layout,
 * the VBL queue scan and the voice ring were copied into each test
 * that used them; one copy is enough, and unlike the library this
 * costs no m68k text because tests/host is never linked into
 * anything that runs on target.
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

/* The voice device's ring as the fake DMA (stubs.c) sees it: its
 * geometry in voice.c, the buffer the device programmed, and a tick
 * with the play head put where a test wants it - the same call path
 * as the interrupt. */
#define RING  512
#define BLOCK 128

extern uint32_t stdl_host_dma_pos;
extern const void *stdl_host_dma_buf;

static const int8_t *ring_base(void)
{
    return (const int8_t *)stdl_host_dma_buf;
}

/* put the fake play head at frame `f` of the ring and tick */
static void tick_at(uint32_t f)
{
    stdl_host_dma_pos = (uint32_t)(uintptr_t)stdl_host_dma_buf + f;
    vbl_fn();
}

/* a voice's programmed period, from the register file stub */
static uint16_t voice_period(int v)
{
    return (uint16_t)(stdl_host_ym[2 * v]
                      | ((stdl_host_ym[2 * v + 1] & 0x0F) << 8));
}

#endif
