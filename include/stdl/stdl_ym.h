/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * YM2149 definitions shared by the sound APIs (STDL_Sfx, STDL_Tone).
 */

#ifndef STDL_YM_H
#define STDL_YM_H

#include <stdl/stdl_types.h>

/* YM tone period for a frequency in Hz (2MHz master / 16). Periods
 * are 12 bits: 31Hz is the lowest tone, the chip's top is inaudible. */
#define STDL_YM_PERIOD(hz) ((uint16_t)(125000L / (hz)))

#endif /* STDL_YM_H */
