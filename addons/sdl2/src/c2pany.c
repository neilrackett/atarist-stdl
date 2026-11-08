/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * The conversion's loops for an X known only at run time: its two
 * longs read from the arguments once a group and XORed in. An object
 * of its own, linked only by a program that sets its border index from
 * a variable (STDL_SDL2_SetBorderIndex); a constant takes c2px.c's
 * loops, which cost nothing over X = 0's (c2p_asm.h has the numbers).
 */

#include "c2p.h"

#ifdef __m68k__
#include "c2p_asm.h"

#define C2P_ANY \
    "	move.l	104(%sp),%d7\n" \
    "	eor.l	%d7,%d2\n" \
    "	move.l	108(%sp),%d7\n" \
    "	eor.l	%d7,%d0\n"

__asm__(C2P_PAIR("stdl_sdl2_c2p_any", C2P_ANY));

extern stdl_sdl2_c2p_fn stdl_sdl2_c2p_any1, stdl_sdl2_c2p_any2;
const struct stdl_sdl2_c2pv stdl_sdl2_c2p_any = {
    stdl_sdl2_c2p_any1, stdl_sdl2_c2p_any2
};
#else
const struct stdl_sdl2_c2pv stdl_sdl2_c2p_any = { 0, 0 };
#endif
