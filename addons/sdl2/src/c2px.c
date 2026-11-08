/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * The conversion's loops for one fixed X, built once for each of the 15
 * (sdl2.mk: -DSTDL_SDL2_XS=10 ... f0) into an object of its own, so a
 * program names its X as a constant (STDL_SDL2_SetBorderIndex) and
 * links only that pair. X's bits 4 and 5 are planes 4 and 5, the high
 * and low words of d2 once the merges are done, and bits 6 and 7
 * planes 6 and 7, d0's: each long takes a not.w (the low word), a not.l
 * (both) or both (the high word alone).
 */

#include "c2p.h"

#ifndef STDL_SDL2_XS
#error "c2px.c is built with -DSTDL_SDL2_XS=<X in hex, without 0x>"
#endif

#define C2PX_CAT3_(a, b, c) a##b##c
#define C2PX_CAT3(a, b, c)  C2PX_CAT3_(a, b, c)
#define C2PX_HEX_(s)        0x##s
#define C2PX_HEX(s)         C2PX_HEX_(s)
#define C2PX_STR_(s)        #s
#define C2PX_STR(s)         C2PX_STR_(s)

#define C2PX_X     C2PX_HEX(STDL_SDL2_XS)
#define C2PX_NAME  C2PX_CAT3(stdl_sdl2_c2p_x, STDL_SDL2_XS, )

#if (C2PX_X & 0x0F) || C2PX_X > 0xF0 || C2PX_X == 0
#error "X is a multiple of 16 from 0x10 to 0xF0"
#endif

#ifdef __m68k__
#include "c2p_asm.h"

#if (C2PX_X & 0x30) == 0x30
#define C2PX_T2 "	not.l	%d2\n"
#elif (C2PX_X & 0x30) == 0x20
#define C2PX_T2 "	not.w	%d2\n"
#elif (C2PX_X & 0x30) == 0x10
#define C2PX_T2 "	not.l	%d2\n	not.w	%d2\n"
#else
#define C2PX_T2 ""
#endif
#if (C2PX_X & 0xC0) == 0xC0
#define C2PX_T0 "	not.l	%d0\n"
#elif (C2PX_X & 0xC0) == 0x80
#define C2PX_T0 "	not.w	%d0\n"
#elif (C2PX_X & 0xC0) == 0x40
#define C2PX_T0 "	not.l	%d0\n	not.w	%d0\n"
#else
#define C2PX_T0 ""
#endif

__asm__(C2P_PAIR(C2PX_STR(C2PX_NAME), C2PX_T2 C2PX_T0));

extern stdl_sdl2_c2p_fn C2PX_CAT3(stdl_sdl2_c2p_x, STDL_SDL2_XS, 1),
                        C2PX_CAT3(stdl_sdl2_c2p_x, STDL_SDL2_XS, 2);
const struct stdl_sdl2_c2pv C2PX_NAME = {
    C2PX_CAT3(stdl_sdl2_c2p_x, STDL_SDL2_XS, 1),
    C2PX_CAT3(stdl_sdl2_c2p_x, STDL_SDL2_XS, 2)
};
#else
const struct stdl_sdl2_c2pv C2PX_NAME = { 0, 0 };
#endif
