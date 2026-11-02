/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * Responsive programs: an ST program that also uses what a better
 * machine has, where it has it, asking for each thing by name. Ask
 * once, before the first STDL_SetVideoMode:
 *
 *     STDL_UseResponsive(STDL_RESPONSIVE_COLOUR | STDL_RESPONSIVE_TALL);
 *     screen = STDL_SetVideoMode(320, 240, 8, STDL_DOUBLEBUF);
 *
 * and branch on what the screen says: format->BitsPerPixel (8, or 4)
 * and h (240, or 200). Each option is a promise that the program
 * handles what it brings; nothing changes for a program that does not
 * ask, and a later release adds options rather than changing these.
 *
 * STDL_RESPONSIVE_COLOUR: 256 colours, asked for with a bpp of 8.
 *   - A TT: TT Low, each row on two of its 480 lines; 200 rows centred
 *     in 240, unless the screen is tall.
 *   - A Falcon: an 8-bit mode, 240 lines on VGA (the VIDEL doubles
 *     them), on RGB or a TV 200 or, tall, 240 in overscan, 384 pixels
 *     wide with the picture centred.
 *   - An ST, an STE or a Mega STE: its 16 colours, as without it.
 *   Every surface created while the screen is up has 8 interleaved
 *   planes, the display's own layout, so the screen is the display
 *   page itself; Flip copies what changed to the doubled lines on a TT
 *   and has nothing to do on a Falcon. Colours run 0-255 in every call
 *   that takes one; 4-plane data drawn onto one (a surface or a sprite
 *   made before the screen, an image a loader returns, STDL_PutGroup)
 *   keeps its colours 0-15.
 *
 * STDL_RESPONSIVE_TALL: a 320x240 screen, asked for with a height over
 *   200, on every machine.
 *   - An ST, an STE or a Mega STE: its own screen with the bottom border
 *     open (stdl_overscan.h), 240 of the 245 lines; 200 if the border
 *     cannot open (rasters already on Timer B), with the error set.
 *   - A TT: TT Low; without the colour, a 4-plane surface that Flip
 *     compares with the display and copies across where it changed.
 *   - A Falcon: a VIDEL mode of 240 lines, 4 bits without the colour,
 *     in the ST's own plane layout.
 *   Without the colour the screen is an ordinary 4-plane surface
 *   everywhere: every call works on it, and so do a program's own
 *   writes to its pixels.
 *
 * A responsive screen on a TT or a Falcon is always double-buffered.
 * Its colours 0-15 start as an ST screen's do, and every palette call
 * reaches the TT's and the Falcon's own registers, staged ones and
 * STDL_SetColour too; a word for the ST's registers sets its entry at
 * the ST's 4 bits a channel.
 *
 * What an 8-plane surface does not take: a blit from it onto a 4-plane
 * surface, or one of its sprites onto one; the blit flags (UNDER,
 * MARK); patterned polygons; tilesets, from one or onto one; the
 * cursor on a responsive screen. Each sets the error and draws nothing.
 * Plane budgets apply to 4-plane surfaces alone. A TT or a Falcon has
 * no borders, rasters or hardware scrolling for any screen
 * (stdl_overscan.h, stdl_raster.h, stdl_hwscroll.h).
 *
 * The mode set moves the console's cursor below the rows a centred
 * screen leaves above it, so text printed after it lands in the
 * picture, as on an ST; text a program puts there with its own cursor
 * escapes lands where it says.
 *
 * STDL_UpdateRects tells an 8-plane screen what a program wrote to its
 * pixels itself, as SDL_UpdateRect would; the library's own calls need
 * nothing.
 */

#ifndef STDL_RESPONSIVE_H
#define STDL_RESPONSIVE_H

#include <stdl/stdl_types.h>

#ifdef __cplusplus
extern "C" {
#endif

/* the things a program can ask for; one call may ask for several */
#define STDL_RESPONSIVE_COLOUR 0x0001u
#define STDL_RESPONSIVE_TALL   0x0002u

/*
 * 0, or -1 for an option this library does not know, or while a
 * responsive screen is up (ask before the mode set). Inline so that a
 * constant `what` links the code of the options it names and no other:
 * the colour's 8-plane routines and the tall screen's border code are
 * each several kilobytes.
 */
struct stdl_resp_model;
extern const struct stdl_resp_model stdl_resp_colour, stdl_resp_tall;
int stdl_use_responsive(unsigned what, const struct stdl_resp_model *colour,
                        const struct stdl_resp_model *tall);

static __inline__ int STDL_UseResponsive(unsigned what)
{
    return stdl_use_responsive(what,
        (what & STDL_RESPONSIVE_COLOUR) ? &stdl_resp_colour : 0,
        (what & STDL_RESPONSIVE_TALL) ? &stdl_resp_tall : 0);
}

/*
 * For measuring the prototype; they go when it is done. STDL_RespClock
 * counts 38,400 a second (the 200Hz clock and the count inside its
 * tick); the stats are in its units. With STDL_RESPB_NOWAIT, Flip asks
 * for the page and does not wait for the VBL, so a frame's time is its
 * own.
 */
#define STDL_RESPB_NOWAIT 1u

typedef struct {
    uint32_t flips;
    uint32_t present;       /* clock units spent presenting          */
    uint32_t cells;         /* 16-pixel groups copied to the display,
                             * a row's each                         */
    const uint8_t *page;    /* the screen's rows on the page the last
                             * Flip showed (not the margins, where
                             * the console may have written)       */
    uint32_t page_bytes;
} STDL_RespStats;

void STDL_RespBench(unsigned flags);
STDL_RespStats *STDL_RespGetStats(void);
uint32_t STDL_RespClock(void);

#ifdef __cplusplus
}
#endif

#endif /* STDL_RESPONSIVE_H */
