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

#endif /* STDL_SDL2_INTERNAL_H */
