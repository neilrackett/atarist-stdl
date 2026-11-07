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
extern int fake_rumble_low, fake_rumble_high, fake_rumble_ms, fake_rumbles;
/* the joysticks (STDL_SetNumJoysticks): how many, each one's pad and
 * held inputs (a bit an STDL_JOYSTICK_ input), and the merge setting */
extern int fake_joys, fake_pads[6], fake_rumble_which;
extern uint32_t fake_helds[6];
/* STDL_UseJoystickDeviceEvents's, and the core's device events for a
 * new count or a merge change, every slot changing; the readers' */
extern int fake_devices;
void fake_set_joys(int n);
void fake_set_merge(int n);
extern int fake_axes[6][6], fake_hats[6];
/* an STDL event for the next pump; the VBL count the pumps keep time
 * by, how many there were, and the calls between them */
void fake_event(const STDL_Event *e);
extern uint32_t fake_vbl;
extern int fake_pumps, fake_idles;
void fake_key(int down, unsigned sym, unsigned mod, unsigned unicode);

/* the ring device: what was asked for, and a refill of a half ring
 * (2,048 DMA frames) as ring.c makes it, its bytes kept in order */
extern int fake_audio_open, fake_audio_paused, fake_audio_falcon;
extern STDL_AudioSpec fake_audio_spec;
extern Uint8 fake_audio_out[1 << 20];
extern Uint32 fake_audio_outlen;
void fake_audio_refill(void);

#endif /* STDL_SDL2_FAKE_STDL_H */
