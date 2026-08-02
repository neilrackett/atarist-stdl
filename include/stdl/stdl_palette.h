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

#include <stdl/stdl_types.h>

/* Program hardware from a logical palette (screen surfaces only;
 * for off-screen surfaces just updates the surface's palette). */
void STDL_SetPalette(STDL_Surface *s, const STDL_Palette *pal);

/* Set logical colours [first, first+n) and, if `s` is the screen,
 * the hardware registers. Returns 1 (all colours always settable). */
int  STDL_SetColours(STDL_Surface *s, const STDL_Colour *cols,
                     int first, int n);
#define STDL_SetColors STDL_SetColours

/* Direct hardware register write, STE-rotated word format. */
void STDL_SetColour(int index, uint16_t stColour);

/* RGB888 -> canonical hardware word (STE rotated nibbles). */
uint16_t STDL_HWColour(uint8_t r, uint8_t g, uint8_t b);

/* Nearest palette index for an RGB888 colour. */
uint8_t STDL_MapRGB(const STDL_PixelFormat *fmt,
                    uint8_t r, uint8_t g, uint8_t b);
void    STDL_GetRGB(uint32_t index, const STDL_PixelFormat *fmt,
                    uint8_t *r, uint8_t *g, uint8_t *b);

/* Blocking VBL-driven fade to a target palette. */
void STDL_FadeTo(const STDL_Palette *target, int frames);

#endif /* STDL_PALETTE_H */
