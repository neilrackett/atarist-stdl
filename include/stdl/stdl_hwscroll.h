/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * STE hardware scrolling: show a 320x200 window of a larger planar
 * buffer, positioned to the pixel, without copying anything.
 */

#ifndef STDL_HWSCROLL_H
#define STDL_HWSCROLL_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdl/stdl_types.h>

/*
 * The STE's Shifter has three registers the plain ST lacks: a
 * word-granular video base ($FF820D), a line offset added at the
 * end of every displayed line (LINEWIDTH, $FF820F) and a 0-15 pixel
 * horizontal fine scroll (HSCROLL, $FF8265). Together they let the
 * display window walk over a planar buffer that is wider and taller
 * than the screen, one pixel at a time, at no CPU cost: the smooth
 * scrolling model of tile engines that keep a play field one tile
 * larger than the screen and only redraw the strip that scrolls in.
 *
 * STDL_HasHwScroll says whether the machine has those registers (an
 * STE-class Shifter: STE, Mega STE; the TT and Falcon count too but
 * are not STDL's target). On a plain ST the calls below fail cleanly
 * and touch nothing; a port keeps a copy-based fallback for that.
 */
int STDL_HasHwScroll(void);

/*
 * Show the 320x200 window whose top-left pixel group starts at
 * `base` in a buffer of `stride` bytes per row, offset a further
 * `xfine` (0-15) pixels to the right. The buffer is the caller's -
 * typically an STDL_CreateSurfaceFrom surface, or any planar block
 * in the format contract - and stays the caller's to draw into.
 *
 *   base    address of the group holding the window's top-left
 *           pixel. Any even address: the Shifter reads planes in
 *           fetch order from the base, so the group is whatever
 *           four words start there. The horizontal position is
 *           base for whole groups plus xfine within one
 *   stride  bytes per buffer row: a multiple of 8, from 160 (a
 *           320-pixel buffer, vertical scrolling only) up to 670;
 *           horizontal fine scrolling needs at least 168, because
 *           the Shifter fetches one extra group per line while
 *           HSCROLL is non-zero and LINEWIDTH has to give it back
 *   xfine   0-15 pixels
 *
 * The three registers are programmed together from the vertical
 * blank, never mid-frame, and in the order the hardware wants: the
 * base is latched into the video counter three lines before the
 * VBL, so a base written at one VBL is fetched from the next frame
 * on, while LINEWIDTH and HSCROLL apply to the frame that follows
 * the write. The module therefore writes each request's base one
 * VBL before its LINEWIDTH and HSCROLL, and no frame ever shows a
 * base with the wrong offsets - the seam that STE scrollers get
 * wrong when they write all three at once. A request made during
 * frame N is on screen for frame N+2; STDL_ScrollWindowPending says
 * whether the latest one has been fully programmed yet, for a
 * double-buffered caller that must not draw into a page the Shifter
 * is still fetching.
 *
 * Returns 0, or -1 with STDL_GetError() set: no STE Shifter, no
 * video mode, bad alignment or stride, or no free VBL slot (the
 * module runs from one, claimed on the first call).
 */
int STDL_SetScrollWindow(const void *base, int stride, int xfine);

/*
 * The same, for a window whose top-left pixel is (x, y) of surface
 * s: base is the group holding that pixel and xfine is x & 15. The
 * surface's stride is the buffer stride, so it needs a width of at
 * least 336 pixels for horizontal fine scrolling. x and y are not
 * clipped: the caller keeps the window inside the surface (the
 * Shifter fetches whatever memory the registers point at).
 */
int STDL_SetScrollOrigin(const STDL_Surface *s, int x, int y);

/*
 * Non-zero while the most recent request has not yet reached all
 * three registers (up to two VBLs after the call). A page-flipping
 * caller waits on this before drawing into the page that was on
 * screen: STDL_WaitVBL once or twice, or spin on it.
 */
int STDL_ScrollWindowPending(void);

/*
 * Put the display back on the STDL screen page with the stock
 * registers (LINEWIDTH 0, HSCROLL 0). Done automatically by
 * STDL_Quit and on abnormal termination; a program that wants to
 * go back to plain STDL_Flip page flipping mid-run calls it itself.
 */
void STDL_ResetScrollWindow(void);

#ifdef __cplusplus
}
#endif

#endif /* STDL_HWSCROLL_H */
