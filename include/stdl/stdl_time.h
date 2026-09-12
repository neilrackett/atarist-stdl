/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * Timing built on the 200Hz system timer (5ms resolution).
 */

#ifndef STDL_TIME_H
#define STDL_TIME_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

uint32_t STDL_GetTicks(void);        /* ms since STDL_Init            */
void     STDL_Delay(uint32_t ms);
void     STDL_FrameLimit(int fps);   /* VBL-aligned pacing            */

/* The raw 200Hz counter, for profiling finer than GetTicks' 5ms
 * grain and for code that wants to pace in ticks without the *5.
 * Counts from system boot, not STDL_Init; take differences.
 *
 * The unit is a 200Hz tick, which is 5ms - NOT a millisecond. A
 * rate worked out as count/elapsed with this counter and then
 * called "per second" is five times too high, and that is a
 * comfortable-looking frame rate for a game running at a fifth of
 * it. STDL_GetTicks is the one that returns milliseconds. */
uint32_t STDL_GetHz200(void);

#ifdef __cplusplus
}
#endif

#endif /* STDL_TIME_H */
