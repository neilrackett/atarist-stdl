/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * Border overscan: 228, 245 or 273 visible lines instead of 200.
 */

#ifndef STDL_OVERSCAN_H
#define STDL_OVERSCAN_H

#include <stdint.h>

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
 * set when unavailable: no video mode yet, a TT/Falcon, or rasters
 * open (STDL_OpenRasters) - both run on Timer B.
 *
 * STDL_DOUBLEBUF works here: opening a border with it set allocates
 * a second tall page, and STDL_Flip swaps them. The flip goes
 * through this module rather than Setscreen, because while a border
 * is open this module owns the video base, and its interrupts
 * measure the video counter against the page being displayed. The
 * cost is one more tall buffer, about 37K; a game that draws a
 * whole frame and shows it is the case this is for, since it stops
 * the drawing racing the beam. The tricks run on a 50Hz
 * frame, so a 60Hz base screen is switched to 50Hz while a border
 * is open and restored on the final close - opening a border is an
 * active choice, and it always takes effect on ST-class hardware.
 *
 * On a 16MHz Mega STE with its cache enabled, versions before
 * 1.15.1 lost the bottom border in frames that drew, worst through
 * the BLiTTER, and the advice here was STDL_UseBlitter(0). The
 * cause was the cache - every BLiTTER operation empties it - leaving
 * the border interrupt's timing-critical path partly cold; the
 * interrupt now warms all of it. No workaround is needed. See
 * docs/overscan.md for the things measured to disturb an open
 * border.
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
 * call when the border is not open. The last border to close also
 * reseeds the Shifter's plane phase with a moment of hi-res in the
 * blanking (so does the terminate path), since a sync trick gone
 * wrong can leave the phase rotated - every colour wrong - and on
 * real hardware that survives the program's exit; the desktop
 * would come back rotated. */
void STDL_CloseTopBorder(void);

/*
 * Open the bottom border instead: 245 visible lines, added BELOW
 * the normal picture, all of them seamless - content below the
 * old picture goes at surface rows 200..244. Timer B (counting
 * Display Enable events) gets the ISR near the end of the picture,
 * and the Shifter's video counter, read while line 262 is being
 * fetched, places the sync flick to the bus cycle: a 60Hz pulse of
 * 16 to 28 cycles across the GLUE's border test at cycle ~500,
 * restored to 50Hz before line 262 ends. It must end inside the
 * line: real hardware fixes the next line's length at the boundary,
 * and a restore that arrives in line 263 cuts that line four cycles
 * short and leaves the Shifter's plane phase a word out for the rest
 * of the frame - every colour wrong, every shape in place - which
 * no emulator shows. The loops that run out the distance are
 * calibrated the first time the bottom border opens, against
 * displayed scanlines and then against the machine's own execution
 * of the flick's instructions, read back from the counter - one to
 * two frames with interrupts masked (the 200Hz system tick loses a
 * few counts, once) - so the placement holds on any CPU speed. Where
 * the GLUE's test sits against the counter is then found by trying:
 * it differs between machines, between the hardware and the
 * emulator, and from one boot to the next on the same machine, so
 * the first open tries the pulse from an early position upward and
 * settles two cycles past the first that opens - about a third of a
 * second during which the bottom border flickers, once per process,
 * and STDL_OverscanMisses() counts from after it. On a
 * machine whose video counter cannot be read mid-line (an
 * emulator's 16MHz mode) the ISR times from Timer B instead, less
 * precisely; the check is part of the calibration.
 *
 * Costs an interrupt and two to three lines of polling per frame,
 * under 1% of an 8MHz frame, most of it waiting on the beam. Same
 * requirements and behaviour as the top border otherwise
 * (ST-class machine; STDL_DOUBLEBUF supported as above, and the
 * screen surface is repointed by each flip). Returns the resulting surface height, or 0 with
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

/* Frames whose border flip was skipped since the borders were last
 * programmed - a long interrupts-off stretch or bus stall pushed the
 * timer past its window, and that frame showed a normal border
 * instead of glitching mid-frame. Every open starts the count again
 * from 0, and so does a close that leaves the other border open,
 * which re-programs it; the last close leaves it as it was. So read
 * it before closing: one port read it after closing its two borders
 * one at a time and reported the partial close's fresh 0, which on
 * hardware proved nothing. Steady increments mean something
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

/*
 * A program that runs the machine itself.
 *
 * The calls above, for a program that does not use the rest of the
 * library: one that takes over the interrupts and the screen itself,
 * flips its own pages, sets its own colours, and links no C library
 * either. It compiles one file, src/overscan_host.c, with
 * -Iinclude, and links that object in place of libstdl.a, never
 * beside it. The object needs nothing but libgcc's arithmetic
 * (__mulsi3, __divsi3, __udivsi3), and STDL's build checks that it
 * stays so.
 *
 * The program gives the borders one page and keeps its other pages
 * itself. Before the first open it calls STDL_OverscanHostInit,
 * then opens and closes the borders with the calls above, which
 * behave as they are documented there with these differences:
 *   - opening the first border clears host->page and shows it
 *     (XBIOS Setscreen). Opening the second, and closing one of two,
 *     clear the page on screen and re-program the borders on it: the
 *     page STDL_OverscanPageShown last named, or host->page when it
 *     has named none since the first border opened;
 *   - the last close shows host->screen (Setscreen) and leaves the
 *     colours alone: nothing is staged, the program's VBL sets them;
 *   - nothing is allocated, and STDL_Flip does not exist: the
 *     program flips its pages itself, telling the borders as below;
 *   - a Mega STE's speed is read from $FFFF8E21 at each open of the
 *     bottom border, which is calibrated again when it has changed,
 *     so set it before opening and leave it while a border is open.
 *
 * What the program must provide: supervisor mode; its interrupts
 * running at IPL 3 or below across an open or a close; a VBL that
 * adds 1 to _frclock ($466) every frame, as TOS's handler does (the
 * open and close wait for the blank by watching it); and $70 already
 * holding its own VBL handler, which the open chains a prefix in
 * front of and the last close puts back. While a border is open
 * Timer A and Timer B are STDL's, and Timer C's vector carries a
 * prefix (see STDL_OpenTopBorder); 50Hz is forced and the sync rate
 * read at the first open is put back at the last close. The MFP's
 * enable and mask bits for Timers A and B (IERA/IMRA 0x21) are taken
 * at the open and put back as they were at the close; nothing else
 * of the MFP's is touched.
 *
 * vlink warns that the object makes .text writable: the border ISRs
 * keep the variables they read in their timing windows beside their
 * code, as they do in the library build.
 */

/* One overscan page: 274 rows of 160 bytes, rows 1-273 shown with
 * both borders open (rows 1-228 with the top alone, 0-244 with the
 * bottom alone). With the top border open, row 0 is fetched too, a
 * word out of step, on the line above the picture: keep it colour 0,
 * as the open leaves it, or what is drawn there shows as a smear
 * (seen in Hatari, as colours 7 and 8 where a white bar crossed it). */
#define STDL_OVERSCAN_PAGE_BYTES  (274 * 160)

/* For callers in assembly: page at 0, screen at 4, mch at 8 and
 * blitter, a byte, at 12, 14 bytes in all at gcc's m68k alignment of
 * 2. overscan_host.c does not compile if that changes. */
typedef struct STDL_OverscanHost {
    void     *page;     /* the page the first open shows: ST RAM,
                         * 256-byte aligned, STDL_OVERSCAN_PAGE_BYTES;
                         * every page the program flips to is the
                         * same size, aligned the same way */
    void     *screen;   /* shown again when the last border closes:
                         * the screen TOS had (Logbase)            */
    uint32_t  mch;      /* the _MCH cookie, 0 without one; a TT or a
                         * Falcon ($20000 and up) is refused by the
                         * open, which sets the error              */
    uint8_t   blitter;  /* nonzero when the program runs the BLiTTER
                         * with a border open: the ISRs pause it
                         * across each flick. Its hog-mode blits must
                         * still keep away from the lines given at
                         * STDL_OverscanMisses                      */
} STDL_OverscanHost;

/* Take the machine description; call it with no border open. Waits
 * for _frclock to move, at most about two seconds. Returns 0, or -1
 * with STDL_GetError() set: a border is open, the page is missing or
 * not 256-byte aligned, or _frclock is not counting. */
int  STDL_OverscanHostInit(const STDL_OverscanHost *host);

/*
 * The program's page flips, two calls a flip, both from its VBL:
 *   STDL_OverscanPageNext(page) in the VBL that writes the page to
 *     the video base ($FF8201/$FF8203), which the Shifter takes up
 *     at the next frame;
 *   STDL_OverscanPageShown() in the VBL after, the first frame that
 *     page is on screen.
 * The top ISR checks the video counter against the page on screen
 * and the one coming, and the bottom ISR polls for the counter at
 * the page's frame line 262, so a flip they were not told about
 * shows a normal border and counts in STDL_OverscanMisses().
 *
 * Both only store a few bytes (no multiply or divide), in an order
 * that is safe against the border ISRs, and follow the C calling
 * convention (d0-d1/a0-a1 not preserved). In a VBL that does both,
 * PageShown goes first. Both do nothing with no border open; call
 * them only while a border stays open, never from a VBL that can
 * interrupt an open or a close: let a flip in progress finish before
 * either, and start the next after.
 */
void STDL_OverscanPageNext(const void *page);
void STDL_OverscanPageShown(void);

/* Puts back what an open border borrowed - the $70 and $114 vectors
 * as they were at the open, Timers A and B, their MFP bits, the sync
 * rate - waits a bounded time for the blank and reseeds the Shifter,
 * and nothing more: no XBIOS, no GEMDOS, no colours, no screen. For
 * an exception or terminate handler, which shows its own screen
 * afterwards. Does nothing with no border open. */
void STDL_OverscanRelease(void);

/* the error the last failed call set; the host object defines it */
const char *STDL_GetError(void);

#ifdef __cplusplus
}
#endif

#endif /* STDL_OVERSCAN_H */
