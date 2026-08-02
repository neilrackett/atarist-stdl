/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * Timing built on the 200Hz system timer (5ms resolution).
 */

#ifndef STDL_TIME_H
#define STDL_TIME_H

#include <stdint.h>

uint32_t STDL_GetTicks(void);        /* ms since STDL_Init            */
void     STDL_Delay(uint32_t ms);
void     STDL_FrameLimit(int fps);   /* VBL-aligned pacing            */

#endif /* STDL_TIME_H */
