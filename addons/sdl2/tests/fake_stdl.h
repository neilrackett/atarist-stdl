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

/* input: STDL's events as the program's pump would take them */
extern uint16_t fake_mod;
extern int fake_mouse_x, fake_mouse_y;
extern uint8_t fake_mouse_buttons;
extern int fake_repeat_delay, fake_repeat_interval, fake_unicode;
extern int fake_pad;                    /* an Xpad pad is there */
extern uint32_t fake_held;              /* a bit an STDL_JOYKEY_ */
extern int fake_rumble_low, fake_rumble_high, fake_rumble_ms, fake_rumbles;
void fake_event(const STDL_Event *e);
void fake_key(int down, unsigned sym, unsigned mod, unsigned unicode);

#endif /* STDL_SDL2_FAKE_STDL_H */
