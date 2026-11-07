/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * The SDL2 add-on's own state, shared between its objects. The
 * add-on reaches the core through its public headers only.
 */

#ifndef STDL_SDL2_INTERNAL_H
#define STDL_SDL2_INTERNAL_H

#include "SDL.h"
#include <stdl/stdl.h>

/* SDL_INIT_ bits granted, for SDL_WasInit */
extern Uint32 stdl_sdl2_inited;

/* keymap.c: STDL's key symbols as SDL2's */
SDL_Scancode stdl_sdl2_scancode(unsigned sym);
SDL_Keycode stdl_sdl2_keycode(unsigned sym);

/* video.c: the screen position of the logical size's top left, which
 * the mouse is reported from */
void stdl_sdl2_screen_origin(int *x, int *y);

/* events.c: an event for the queue; 1, 0 ignored, -1 full */
int stdl_sdl2_push(SDL_Event *e);

/* joystick.c: an STDL joystick event, the state it carries kept and
 * passed on to an open joystick and controller; the controller's
 * buttons, read and their changes sent, at a pump that brought its
 * joystick an event */
void stdl_sdl2_joy_event(const STDL_Event *se);
void stdl_sdl2_controller_poll(void);

/* audio.c: the open device's format, or 0 */
SDL_AudioFormat stdl_sdl2_audio_format(void);

/* little-endian fields of the files the add-on reads (BMP, WAV) */
static __inline__ Uint32 stdl_sdl2_le32(const Uint8 *p)
{
    return (Uint32)p[0] | (Uint32)p[1] << 8 | (Uint32)p[2] << 16 |
           (Uint32)p[3] << 24;
}

static __inline__ unsigned stdl_sdl2_le16(const Uint8 *p)
{
    return (unsigned)(p[0] | p[1] << 8);
}

#endif /* STDL_SDL2_INTERNAL_H */
