/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * Border overscan: 228, 245 or 273 visible lines instead of 200.
 */

#ifndef STDL_OVERSCAN_H
#define STDL_OVERSCAN_H

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Open the top border. The picture grows upward from 200 to 228
 * visible lines; the bottom and side borders are untouched. Works on
 * any 50Hz colour screen - STF, STE, Mega STE - with no cycle-exact
 * code: one Timer A interrupt per frame flips the sync rate inside
 * the GLUE's nine-line top-border test window, and Timer B (silently
 * counting Display Enable events) pins the flip back once the first
 * line has been fetched. Costs under 1% of an 8MHz frame, two to
 * three lines of polling.
 *
 * On success the screen surface is updated in place: pixels points
 * at the first visible line, h and clip.h become 228, the stride
 * stays 160. All 228 lines are seamless; the display fetches from a
 * dedicated buffer whose one hidden lead row (the line fetched at
 * 60Hz timing while the rate is being put back) and the tail beyond
 * line 228 read as colour 0, i.e. border.
 *
 * Returns the new visible height (228), or 0 with STDL_GetError()
 * set when unavailable: no video mode yet, or STDL_DOUBLEBUF, which
 * this version does not combine with. The tricks run on a 50Hz
 * frame, so a 60Hz base screen is switched to 50Hz while a border
 * is open and restored on the final close - opening a border is an
 * active choice, and it always takes effect on ST-class hardware.
 *
 * Uses MFP Timer A (vector, IERA/IMRA bit 5) and Timer B's counter
 * with its interrupt masked, and puts a prefix on TOS's Timer C
 * vector that lowers the CPU mask inside the 200Hz handler so the
 * border timers can interrupt it (it runs for over two scanlines at
 * a time, and without that the ISRs would have to fire lines early
 * and spin); all of it is returned by Close. A frame
 * that misses its timing window (a long interrupts-off section)
 * shows one frame with a normal top border and recovers by itself;
 * while a border is open STDL places its own BLiTTER operations
 * around the timing windows (see STDL_OverscanMisses() below) so
 * they cannot cause this.
 */
int  STDL_OpenTopBorder(void);

/* Put the border, the timers and the screen surface back. Safe to
 * call when the border is not open. */
void STDL_CloseTopBorder(void);

/*
 * Open the bottom border instead: 245 visible lines, added BELOW
 * the normal picture, all of them seamless - content below the
 * old picture goes at surface rows 200..244. Timer B (counting
 * Display Enable events) gets the ISR near the end of the picture,
 * and the Shifter's video counter, read while line 262 is being
 * fetched, places the sync flick to within a few cycles of where
 * the GLUE tests for the border and where the next line would
 * start: 60Hz on for ~80 cycles across the test, off again before
 * line 263 can start early. The dbra loops that run out that
 * distance are calibrated the first time the bottom border opens,
 * by timing the same loop against displayed scanlines - about one
 * frame with interrupts masked (the 200Hz system tick loses a few
 * counts, once) - so the placement holds on any CPU speed. On a
 * machine whose video counter cannot be read mid-line (no ST, but
 * an emulator's 16MHz mode) the ISR times from Timer B instead;
 * the check is part of the calibration.
 *
 * Costs an interrupt and two to three lines of polling per frame,
 * under 1% of an 8MHz frame, most of it waiting on the beam. Same
 * requirements and behaviour as the top border otherwise (no
 * STDL_DOUBLEBUF, ST-class machine; the screen surface is updated
 * in place). Returns the resulting surface height, or 0 with
 * STDL_GetError() set.
 *
 * The two variants combine automatically: opening the second while
 * the first is open switches to the combined mode (273 seamless
 * surface rows), and closing one of the pair drops back to the
 * other alone. Each Open returns the height the screen ended up
 * with, and closing reshapes the surface the same way, so redraw
 * after any transition.
 */
int  STDL_OpenBottomBorder(void);
void STDL_CloseBottomBorder(void);

/* Frames whose border flip was skipped since the border was
 * opened - a long interrupts-off stretch or bus stall pushed the
 * timer past its window, and that frame showed a normal border
 * instead of glitching mid-frame. Steady increments mean something
 * in the program stalls the CPU every frame. STDL's own BLiTTER
 * operations are not that something: while a border is open each
 * one is placed from the beam's position (the video counter in the
 * picture, a Timer B stopwatch in the blanking) so that it ends a
 * few lines before the next timing window, split around the window
 * when it would not, and run in hog mode - the ISR is never held
 * off by a blit. Measured in Hatari (STE, cycle-exact, back-to-back
 * full-screen fills): throughput 14% below the no-border figure
 * with the bottom border open, 18% with the top, 28% with both,
 * against 2.1x slower in shared mode; small blits (64x32) pay
 * 35-47% for the per-operation decision; the border is lost in
 * about one frame in 1600 beyond the frame in which it opens (none
 * on a plain ST, which has no BLiTTER). Programs that drive the
 * BLiTTER themselves must use non-hog mode, or keep hog blits
 * short and away from lines 30-36, 259-265 and 310-1. */
uint32_t STDL_OverscanMisses(void);

/* Surface heights while a border is open (28, 45 or 73 lines over
 * the normal 200). */
#define STDL_OVERSCAN_TOP_H       228
#define STDL_OVERSCAN_BOTTOM_H    245
#define STDL_OVERSCAN_BOTH_H      273

#ifdef __cplusplus
}
#endif

#endif /* STDL_OVERSCAN_H */
