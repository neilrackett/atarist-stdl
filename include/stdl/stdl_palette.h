/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * Palette control. Handles ST 3-bit vs STE 4-bit register layout
 * transparently: hardware words are always written in STE rotated
 * format, which plain ST hardware reads correctly (bit 3 ignored).
 */

#ifndef STDL_PALETTE_H
#define STDL_PALETTE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdl/stdl_types.h>

/* Program hardware from a logical palette (screen surfaces only;
 * for off-screen surfaces just updates the surface's palette). */
void STDL_SetPalette(STDL_Surface *s, const STDL_Palette *pal);

/* Set logical colours [first, first+n) and, if `s` is the screen,
 * the hardware registers. Returns 1 (all colours always settable). */
int  STDL_SetColours(STDL_Surface *s, const STDL_Colour *cols,
                     int first, int n);
#define STDL_SetColors STDL_SetColours

/* Direct hardware register write, STE-rotated word format. Always
 * immediate, staged or not: it is what a raster effect or a VBL
 * callback wants. */
void STDL_SetColour(int index, uint16_t stColour);

/* RGB888 -> canonical hardware word (STE rotated nibbles). */
uint16_t STDL_HWColour(uint8_t r, uint8_t g, uint8_t b);

/*
 * A constant 0xRGB with four bits a channel, 0-15 each as the STE's
 * 4096 colours count them, as a hardware word - for tables passed
 * to STDL_SetColour, at compile time. The hardware does not take
 * 0xRGB as written: each nibble keeps the channel's top three bits
 * in bits 2-0 and its lowest in bit 3, so a plain ST, which reads
 * three bits, sees the nearest of its 512. 0xC72 written straight
 * to a register shows green; STDL_HW444(0xC72) is the orange it
 * meant. An ST-style 0x777 (three bits a channel, 0-7) is already
 * a valid word and needs no macro.
 */
#define STDL_HW444_NIB(v) ((((v) >> 1) & 7) | (((v) & 1) << 3))
#define STDL_HW444(rgb) ((uint16_t)( \
    (STDL_HW444_NIB(((rgb) >> 8) & 15) << 8) \
    | (STDL_HW444_NIB(((rgb) >> 4) & 15) << 4) \
    | STDL_HW444_NIB((rgb) & 15)))

/*
 * Stage palette changes to the vertical blank (on = 1), or write them
 * where they are made (0, the default). Staged, everything that sets
 * the screen's palette - STDL_SetPalette, STDL_SetColours,
 * STDL_FadeTo, and the SDL shim's SDL_SetColors and
 * SDL_SetPalette(SDL_PHYSPAL) - is converted when it is made and
 * written to the registers in the next blanking, so no frame shows
 * the old colours above the beam and the new below it. Measured in
 * Hatari with a colour switched at mid-screen every frame
 * (tests/hatari/palstg.c): 99 and 100 frames of 100 split unstaged,
 * through STDL_SetColours and the SDL shim; 1 and 2 staged, both at
 * the switch from the phase before. The
 * cost is up to a frame's delay and one VBL slot. STDL_SetColour is
 * never staged. Returns 0, or -1 with STDL_GetError() set when no
 * VBL slot is free, and changes then stay immediate. An open border
 * stages on its own account whatever this says, and closing it
 * leaves this setting in place.
 */
int STDL_StagePalette(int on);

/* Nearest palette index for an RGB888 colour. */
uint8_t STDL_MapRGB(const STDL_PixelFormat *fmt,
                    uint8_t r, uint8_t g, uint8_t b);
void    STDL_GetRGB(uint32_t index, const STDL_PixelFormat *fmt,
                    uint8_t *r, uint8_t *g, uint8_t *b);

/* Blocking VBL-driven fade to a target palette. */
void STDL_FadeTo(const STDL_Palette *target, int frames);

#ifdef __cplusplus
}
#endif

#endif /* STDL_PALETTE_H */
