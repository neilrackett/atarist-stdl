/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * fake_stdl.c's knobs and what it saw.
 */

#ifndef STDL_SDL2_FAKE_STDL_H
#define STDL_SDL2_FAKE_STDL_H

#include <stdl/stdl.h>
#include "SDL.h"

enum { FAKE_TT, FAKE_VGA, FAKE_RGB };

extern int fake_layout, fake_third, fake_npages, fake_shown;
extern int fake_flips, fake_waits, fake_stagings;
extern STDL_Colour fake_regs[256];
extern Uint8 *fake_page[3];
extern long fake_stride, fake_margin;
extern STDL_MachineInfo fake_mach;

#endif /* STDL_SDL2_FAKE_STDL_H */
