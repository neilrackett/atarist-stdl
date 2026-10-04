/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * Rasters: colour registers changed down the screen, every few lines.
 */

#ifndef STDL_RASTER_H
#define STDL_RASTER_H

#include <stdl/stdl_types.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Change colour register reg1 - and reg2 too, unless it is -1 - every
 * `lines` lines down the picture, from a table of hardware colour
 * words (STDL_HWColour, STDL_HW444). Colour 0 is also the border, so
 * rasters in it run edge to edge: copper bars, skies, a background
 * gradient no 16-colour palette could hold. Another register gives
 * text or an object a gradient of its own. With colour 0 first, every
 * second register has a handler of its own with both addresses
 * written in; another first register takes a general handler, a few
 * points dearer.
 *
 * Entry k of a table holds from picture line k * lines; the last
 * entry, written by the frame's last interrupt (at the end of the
 * picture when `lines` divides 200), holds to the end of the frame
 * and so colours the bottom border too. Returns the number of entries a frame (200 / lines +
 * 1), or -1 with STDL_GetError() set: no video mode, a bad argument,
 * no VBL slot, or a border open - Timer B, which this runs on, is
 * the borders' too, and each refuses while the other holds it.
 *
 * Timer B counts displayed lines and interrupts every `lines` of them
 * to write the next entry. Measured on a plain ST (RASTCHK), with
 * colour 0 as the first register: every fourth line costs 6% of the
 * machine, 6% with a second register; every second line 11% / 12%;
 * every line 22% / 25%. Colour 0 has the quickest handler; with
 * another first register, every second line is 14% / 16%. A VBL
 * callback restarts the count from entry 0 before each picture, and
 * a prefix on TOS's 200Hz Timer C handler lets Timer B in while it
 * runs, which would otherwise shake a raster by two lines four times
 * a frame. An interrupt still waits for the instruction it lands in,
 * so a raster's edge moves by up to a few pixels under divs or movem
 * - the price of a timer, not a cycle-counted loop. Measured in
 * Hatari (RASTCHK's hold, tests/hatari/rstill.py) with a fixed
 * table in colour 0 and the program idle: an edge a line late in 2
 * held frames of 144, and in 16 with TOS's Timer C handler left
 * unopened. Closed on exit, however the program ends.
 *
 * While open, the two registers are the table's: a palette call
 * that sets them is overwritten from the next raster on.
 */
int  STDL_OpenRasters(int lines, int reg1, int reg2);

/*
 * The table to fill for a coming frame: entries in order, one word
 * an entry for one register, two (reg1's, then reg2's) for two. It
 * holds the table last shown when that was shown with `keep`, else
 * whatever it held before - so a program that changes only what moves
 * shows with keep, and one that rewrites every entry shows without
 * and skips the copy. NULL while rasters are closed.
 */
uint16_t *STDL_RasterTable(void);

/*
 * Show the table filled since the last call from the next frame on,
 * as STDL_Flip shows a page; STDL_RasterTable then returns another.
 * keep = 1 copies this table into that one, for a program that
 * changes a few entries a frame; 0 leaves it as it was, for one that
 * rewrites them all (the copy measured about 2.9k cycles a frame for
 * two registers every second line).
 */
void STDL_ShowRasters(int keep);

/* Stop, put Timer B and the vectors back, and restore the two
 * registers from the palette. */
void STDL_CloseRasters(void);

#ifdef __cplusplus
}
#endif

#endif /* STDL_RASTER_H */
