/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * Top-border overscan: 227 visible lines instead of 200.
 */

#ifndef STDL_OVERSCAN_H
#define STDL_OVERSCAN_H

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Open the top border. The picture grows upward from 200 to 227
 * visible lines; the bottom and side borders are untouched. Works on
 * any 50Hz colour screen - STF, STE, Mega STE - with no cycle-exact
 * code: one Timer A interrupt per frame flips the sync rate inside
 * the GLUE's nine-line top-border test window, and Timer B (silently
 * counting Display Enable events) pins the flip back two lines
 * later. Costs about 0.2% of an 8MHz frame.
 *
 * On success the screen surface is updated in place: pixels points
 * at the first visible line, h and clip.h become 227, the stride
 * stays 160. All 227 lines are seamless; the display fetches from a
 * dedicated buffer whose two hidden lead rows and the tail beyond
 * line 226 read as colour 0, i.e. border.
 *
 * Returns the new visible height (227), or 0 with STDL_GetError()
 * set when unavailable: no video mode yet, or STDL_DOUBLEBUF, which
 * this version does not combine with. The tricks run on a 50Hz
 * frame, so a 60Hz base screen is switched to 50Hz while a border
 * is open and restored on the final close - opening a border is an
 * active choice, and it always takes effect on ST-class hardware.
 *
 * Uses MFP Timer A (vector, IERA/IMRA bit 5) and Timer B's counter
 * with its interrupt masked; both are returned by Close. A frame
 * that misses its timing window (a long interrupts-off section)
 * shows one frame with a normal top border and recovers by itself;
 * while a border is open STDL starts BLiTTER operations in non-hog
 * mode (restart idiom, near-hog speed) so its own blits cannot
 * cause this. See STDL_OverscanMisses() below.
 */
int  STDL_OpenTopBorder(void);

/* Put the border, the timers and the screen surface back. Safe to
 * call when the border is not open. */
void STDL_CloseTopBorder(void);

/*
 * Open the bottom border instead: 245 visible lines, added BELOW
 * the normal picture (Timer B fires near the end of the picture
 * and flicks 60Hz across the GLUE's border test). One caveat the
 * top variant does not have: picture line 200 - the line that runs
 * at 60Hz while the test is fooled - displays as a single line of
 * border colour. Interrupt-driven code cannot dodge that at every
 * CPU speed (the cycle-counted escape is a ~60-cycle window), so
 * align a HUD split or a dark band with it, or prefer the seamless
 * top variant. Content below the seam goes at surface rows
 * 201..244.
 *
 * Same requirements and behaviour as the top border otherwise
 * (no STDL_DOUBLEBUF, ST-class machine; the screen surface is
 * updated in place). Returns the resulting
 * surface height, or 0 with STDL_GetError() set.
 *
 * The two variants combine automatically: opening the second while
 * the first is open switches to the combined mode (272 surface
 * rows - 227 seamless lines, the hidden seam at row 227, then 44
 * more), and closing one of the pair drops back to the other
 * alone. Each Open returns the height the screen ended up with,
 * and closing reshapes the surface the same way, so redraw after
 * any transition. The game-window sweet spot is unchanged: content
 * at rows 0..226 of a top or combined surface never meets a seam.
 */
int  STDL_OpenBottomBorder(void);
void STDL_CloseBottomBorder(void);

/* Frames whose border flip was skipped since the border was
 * opened - a long interrupts-off stretch or bus stall pushed the
 * timer past its window, and that frame showed a normal border
 * instead of glitching mid-frame. Steady increments mean something
 * in the program stalls the CPU every frame; while a border is
 * open STDL already starts BLiTTER operations in shared mode, the
 * usual culprit. */
uint32_t STDL_OverscanMisses(void);

/* Surface heights while a border is open (27, 45 or 72 lines over
 * the normal 200). */
#define STDL_OVERSCAN_TOP_H       227
#define STDL_OVERSCAN_BOTTOM_H    245
#define STDL_OVERSCAN_BOTH_H      272

#ifdef __cplusplus
}
#endif

#endif /* STDL_OVERSCAN_H */
