/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * STDL_Time: 200Hz system timer. Requires supervisor mode, which
 * STDL_Init establishes for the whole program.
 *
 * The waits service the cooperative hooks (DMA audio refill, compat
 * timers) once per 5ms tick, so a program sitting in STDL_Delay or
 * STDL_FrameLimit keeps sound and timers running - the same
 * guarantee STDL_PumpEvents gives a polling main loop.
 */

#include "stdl_internal.h"

static uint32_t ticks_base;

void stdl_time_init(void)
{
    ticks_base = STDL_HZ200;
}

uint32_t STDL_GetTicks(void)
{
    return (STDL_HZ200 - ticks_base) * 5;
}

/* run the cooperative services, at most once per 200Hz tick */
static void idle_services(void)
{
    static uint32_t last;
    uint32_t now = STDL_HZ200;

    if (now == last) {
        return;
    }
    last = now;
    if (stdl_timer_hook != NULL) {
        stdl_timer_hook();
    }
    if (stdl_audio_hook != NULL) {
        stdl_audio_hook();
    }
}

void STDL_Delay(uint32_t ms)
{
    /* round up so short delays don't degenerate to busy spinning */
    uint32_t end = STDL_HZ200 + (ms + 4) / 5;
    while ((int32_t)(STDL_HZ200 - end) < 0) {
        idle_services();
    }
}

void STDL_FrameLimit(int fps)
{
    static uint32_t next;
    uint32_t step, now;

    if (fps <= 0) {
        next = 0;
        return;
    }
    step = 200 / (uint32_t)fps;
    if (step == 0) {
        step = 1;
    }
    now = STDL_HZ200;
    if (next == 0 || (int32_t)(now - next) > (int32_t)step * 4) {
        next = now;   /* first call, or we fell hopelessly behind */
    }
    while ((int32_t)(STDL_HZ200 - next) < 0) {
        idle_services();
    }
    next += step;
}
