/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * STDL_Overscan: border overscan. Top: 227 seamless visible lines.
 * Bottom: 245 visible lines with one border-coloured seam at
 * picture line 200.
 *
 * The GLUE decides between border and picture per scanline by
 * looking at the sync rate. Top: during scanlines 25-33 a 60Hz
 * screen starts its picture at line 34 where a 50Hz one waits for
 * line 63 - show it 60Hz inside that nine-line window, restore
 * 50Hz two lines later, and 229 rows fetch, the first two (still
 * at 60Hz while the restore is pinned) showing border colour.
 * Bottom: flick to 60Hz across the border test at the end of
 * picture line 200 and the border never turns on - lines continue
 * to 244. The line that runs at 60Hz falls outside the 60Hz
 * display window, so picture line 200 shows as one line of border
 * colour; cycle-counted routines dodge that by hitting a ~60-cycle
 * window, which interrupt-driven code cannot do reliably at every
 * CPU speed. Align a HUD split or dark band with it, or use the
 * seamless top variant.
 *
 * Nothing is cycle-counted: Timer A in delay mode hits the top
 * window from the VBL at any CPU speed because it runs off the
 * MFP's own clock, Timer B counting Display Enable events fires on
 * an exact line for the bottom, and every sub-line wait is pinned
 * to hardware events (bounded polls). Cost is one interrupt plus
 * a two-to-three line poll per frame, ~0.2% of an 8MHz frame.
 *
 * Cooperative by design, where the classic demo versions of these
 * tricks own the machine: the VBL hook is a register-free prefix
 * on the $70 autovector that forces 50Hz (the safety net - a
 * missed window shows one normal border and recovers), arms the
 * timer and falls through to the original handler, so the TOS VBL
 * queue, screenpt handling and STDL's sound tick keep running; the
 * ISRs end with a software EOI and the only MFP resources claimed
 * are the one timer slot (top: Timer A, vector $134, IERA/IMRA bit
 * 5, plus Timer B's counter with its interrupt left masked;
 * bottom: Timer B, vector $120, IERA/IMRA bit 0).
 *
 * The top ISR guards against firing late (a long interrupts-off
 * section, or a BLiTTER hog blit - though blits started while a
 * border is open run in shared mode for exactly this reason, see
 * stdl_no_hog in blitter.c): it reads the Shifter's video counter,
 * and if fetching has started the window is gone, so it skips the
 * sync flip entirely rather than glitch mid-frame, counts the miss
 * and shows one normal-bordered frame. The bottom ISR's bounded
 * line-waits give it the same failure mode.
 *
 * While an ISR runs (including the bounded polls) its in-service
 * bit blocks lower-priority MFP interrupts - the ACIA/IKBD among
 * them - for ~130us once per frame. Harmless, but if a keyboard
 * byte ever drops under stress, look here before suspecting the
 * border code.
 */

#include <stdlib.h>
#include <string.h>
#include "stdl_internal.h"
#include <stdl/stdl_overscan.h>
#include <stdl/stdl_vbl.h>

#ifdef __m68k__

#include <mint/osbind.h>

/* top: 229 rows fetched (frame lines 34-262), the first two show
 * border colour, visible picture is rows 2..228.
 * bottom: 245 rows fetched and visible (picture lines 0-244), row
 * 200 displays as border.
 * both: 274 rows fetched (frame lines 34-307); rows 0-1 hidden,
 * rows 2-228 display, row 229 is the hidden seam line, rows
 * 230-273 display - the surface exposes rows 2..273. */
#define TOP_HIDDEN_ROWS   2
#define BOTH_FETCH_ROWS   274
#define MAX_FETCH_BYTES   (BOTH_FETCH_ROWS * STDL_SCREEN_STRIDE)

#define MODE_TOP 1
#define MODE_BOT 2

uint32_t stdl_ovsc_old70;    /* original $70 vector, read by asm  */
uint8_t  stdl_ovsc_bhi;      /* screen base bytes the top ISR      */
uint8_t  stdl_ovsc_bmid;     /* compares the video counter against */
uint32_t stdl_ovsc_missed;   /* frames whose flip was skipped      */

static void *buf_alloc;
static uint8_t *buf;
static uint32_t old_ta_vec;      /* original $134 and $120 vectors */
static uint32_t old_tb_vec;
static uint8_t  old_sync;        /* sync rate before the first open */
static uint8_t  old_ier;         /* previous IERA/IMRA state of both */
static uint8_t  old_imr;         /* timer bits (0x21 mask)           */
static int      mode;            /* MODE_TOP | MODE_BOT */
static int      pal_vbl_ok;      /* palette VBL callback installed */

#define MFP_IERA (*(volatile uint8_t *)0xFFFFFA07UL)
#define MFP_IPRA (*(volatile uint8_t *)0xFFFFFA0BUL)
#define MFP_ISRA (*(volatile uint8_t *)0xFFFFFA0FUL)
#define MFP_IMRA (*(volatile uint8_t *)0xFFFFFA13UL)
#define MFP_TACR (*(volatile uint8_t *)0xFFFFFA19UL)
#define MFP_TBCR (*(volatile uint8_t *)0xFFFFFA1BUL)
#define SYNC_REG (*(volatile uint8_t *)0xFFFF820AUL)
#define VEC_VBL  (*(volatile uint32_t *)0x70UL)
#define VEC_TA   (*(volatile uint32_t *)0x134UL)
#define VEC_TB   (*(volatile uint32_t *)0x120UL)

__asm__(
"    .text\n"
"    .even\n"

/* -- top border ---------------------------------------------------- */
/* VBL prefix: 50Hz safety, arm Timer A (~1.83ms -> line ~29),
 * continue into the original handler. Touches no registers. */
"_stdl_ovsc_vbl_top:\n"
"    move.b #2,0xffff820a.w\n"
"    clr.b  0xfffffa19.w\n"
"    move.b #90,0xfffffa1f.w\n"
"    move.b #4,0xfffffa19.w\n"
"    move.l _stdl_ovsc_old70,-(%sp)\n"
"    rts\n"
"\n"
/* Timer A ISR. Two lateness guards decide whether the window is
 * still open before anything touches the sync rate:
 *
 * 1. The timer itself is the clock. In delay mode it reloads and
 *    keeps counting after firing, so its data register reads as
 *    90 minus the ticks since the scheduled fire at line ~29 - a
 *    delivery delayed past the end of the window (a blit that
 *    could not be interrupted, an interrupts-off stretch) shows as
 *    a low count. 12 ticks = four lines of grace keeps us inside
 *    the test window.
 * 2. The video counter must still sit at the base - if fetching
 *    has started the frame is displaying, and a sync flip now
 *    would glitch it. This also catches a delivery so late the
 *    timer count has wrapped back into range.
 *
 * Either guard failing skips the flip, counts the miss and shows
 * one normal-bordered frame. On time, 60Hz starts the picture at
 * line 34 and Timer B, silent in event-count mode, pins the 50Hz
 * restore into the blanking gap after line 35. */
"    .even\n"
"_stdl_ovsc_ta:\n"
"    movem.l %d0-%d2/%a0,-(%sp)\n"
"    move.b 0xfffffa1f.w,%d0\n"
"    clr.b  0xfffffa19.w\n"
"    cmp.b  #78,%d0\n"
"    blo.s  ovsc_ta_late\n"
"    move.b 0xffff8205.w,%d0\n"
"    cmp.b  _stdl_ovsc_bhi,%d0\n"
"    bne.s  ovsc_ta_late\n"
"    move.b 0xffff8207.w,%d0\n"
"    cmp.b  _stdl_ovsc_bmid,%d0\n"
"    bne.s  ovsc_ta_late\n"
"    tst.b  0xffff8209.w\n"
"    bne.s  ovsc_ta_late\n"
"    clr.b  0xffff820a.w\n"
"    lea    0xfffffa21.w,%a0\n"
"    clr.b  0xfffffa1b.w\n"
"    move.b #200,(%a0)\n"
"    move.b #8,0xfffffa1b.w\n"
"    bsr    stdl_ovsc_wait_de\n"
"    bsr    stdl_ovsc_wait_de\n"
"    move.b #2,0xffff820a.w\n"
"    clr.b  0xfffffa1b.w\n"
"ovsc_ta_out:\n"
"    bclr   #5,0xfffffa0f.w\n"
"    movem.l (%sp)+,%d0-%d2/%a0\n"
"    rte\n"
"ovsc_ta_late:\n"
"    addq.l #1,_stdl_ovsc_missed\n"
"    bra.s  ovsc_ta_out\n"
"\n"

/* -- both borders -------------------------------------------------- */
/* VBL prefix: 50Hz safety, both timers stopped, Timer A armed for
 * the top window; the top ISR re-arms Timer B for the bottom. */
"    .even\n"
"_stdl_ovsc_vbl_both:\n"
"    move.b #2,0xffff820a.w\n"
"    clr.b  0xfffffa19.w\n"
"    clr.b  0xfffffa1b.w\n"
"    move.b #90,0xfffffa1f.w\n"
"    move.b #4,0xfffffa19.w\n"
"    move.l _stdl_ovsc_old70,-(%sp)\n"
"    rts\n"
"\n"
/* Combined Timer A ISR: the top trick exactly as above, then Timer
 * B is re-armed to interrupt at the end of frame line 260 - 225
 * displayed lines from the pin - chaining into the bottom ISR. A
 * late delivery or a timed-out pin skips the bottom too (Timer B
 * stays stopped), so a missed frame degrades to plain borders as a
 * whole rather than half a trick. */
"    .even\n"
"_stdl_ovsc_ta_both:\n"
"    movem.l %d0-%d2/%a0,-(%sp)\n"
"    move.b 0xfffffa1f.w,%d0\n"
"    clr.b  0xfffffa19.w\n"
"    cmp.b  #78,%d0\n"
"    blo.s  ovsc_tab_late\n"
"    move.b 0xffff8205.w,%d0\n"
"    cmp.b  _stdl_ovsc_bhi,%d0\n"
"    bne.s  ovsc_tab_late\n"
"    move.b 0xffff8207.w,%d0\n"
"    cmp.b  _stdl_ovsc_bmid,%d0\n"
"    bne.s  ovsc_tab_late\n"
"    tst.b  0xffff8209.w\n"
"    bne.s  ovsc_tab_late\n"
"    clr.b  0xffff820a.w\n"
"    lea    0xfffffa21.w,%a0\n"
"    clr.b  0xfffffa1b.w\n"
"    move.b #200,(%a0)\n"
"    move.b #8,0xfffffa1b.w\n"
"    bsr    stdl_ovsc_wait_de\n"
"    bmi.s  ovsc_tab_fail\n"
"    bsr    stdl_ovsc_wait_de\n"
"    move.b #2,0xffff820a.w\n"
"    clr.b  0xfffffa1b.w\n"
"    move.b #225,(%a0)\n"
"    move.b #8,0xfffffa1b.w\n"
"ovsc_tab_out:\n"
"    bclr   #5,0xfffffa0f.w\n"
"    movem.l (%sp)+,%d0-%d2/%a0\n"
"    rte\n"
"ovsc_tab_fail:\n"
"    move.b #2,0xffff820a.w\n"
"ovsc_tab_late:\n"
"    clr.b  0xfffffa1b.w\n"
"    addq.l #1,_stdl_ovsc_missed\n"
"    bra.s  ovsc_tab_out\n"
"\n"

/* -- bottom border ------------------------------------------------- */
/* VBL prefix: 50Hz safety, re-arm Timer B to fire after 198
 * displayed lines - near the end of the picture. */
"    .even\n"
"_stdl_ovsc_vbl_bot:\n"
"    move.b #2,0xffff820a.w\n"
"    clr.b  0xfffffa1b.w\n"
"    move.b #198,0xfffffa21.w\n"
"    move.b #8,0xfffffa1b.w\n"
"    move.l _stdl_ovsc_old70,-(%sp)\n"
"    rts\n"
"\n"
/* Timer B ISR, two lines before the end of the picture. Step to
 * the end of line 200 on the timer's own count, flick to 60Hz
 * across the GLUE's bottom border test, back one line later. Both
 * sync writes land in the blanking gap right after a line ends, so
 * no line runs at two rates. A timed-out wait means a stall pushed
 * us past the picture: skip the flip and count the miss. */
"    .even\n"
"_stdl_ovsc_tb:\n"
"    movem.l %d1/%d2/%a0,-(%sp)\n"
"    lea    0xfffffa21.w,%a0\n"
"    bsr    stdl_ovsc_wait_de\n"
"    bmi.s  ovsc_tb_late\n"
"    bsr    stdl_ovsc_wait_de\n"
"    bmi.s  ovsc_tb_late\n"
"    clr.b  0xffff820a.w\n"
"    bsr    stdl_ovsc_wait_de\n"
"    move.b #2,0xffff820a.w\n"
"ovsc_tb_out:\n"
"    clr.b  0xfffffa1b.w\n"
"    bclr   #0,0xfffffa0f.w\n"
"    movem.l (%sp)+,%d1/%d2/%a0\n"
"    rte\n"
"ovsc_tb_late:\n"
"    addq.l #1,_stdl_ovsc_missed\n"
"    bra.s  ovsc_tb_out\n"
"\n"

/* Wait for Timer B to tick: the end of the next displayed line.
 * Bounded so a missed window can never hang the machine; returns N
 * set on timeout, N clear on success. */
"stdl_ovsc_wait_de:\n"
"    move.w #2000,%d2\n"
"    move.b (%a0),%d1\n"
"1:  cmp.b  (%a0),%d1\n"
"    bne.s  2f\n"
"    dbra   %d2,1b\n"
"2:  tst.w  %d2\n"
"    rts\n"
);

extern void stdl_ovsc_vbl_top(void);
extern void stdl_ovsc_vbl_bot(void);
extern void stdl_ovsc_vbl_both(void);
extern void stdl_ovsc_ta(void);
extern void stdl_ovsc_ta_both(void);
extern void stdl_ovsc_tb(void);

/* drains the palette staged by stdl_palette_apply_hw inside the
 * blanking - see stdl_pal_defer in palette.c */
static void ovsc_pal_vbl(void)
{
    stdl_palette_flush();
}

/* Hardware-only teardown, shared with the terminate path: vectors,
 * timers and sync rate back, both timer bits released. No GEMDOS,
 * no heap - release_hardware() repoints the screen afterwards. */
static void ovsc_release(void)
{
    uint16_t sr;

    if (!mode) {
        return;
    }
    sr = stdl_int_off();
    VEC_VBL = stdl_ovsc_old70;
    MFP_TACR = 0;
    MFP_TBCR = 0;
    MFP_IPRA &= (uint8_t)~0x21;
    MFP_ISRA &= (uint8_t)~0x21;
    MFP_IERA = (uint8_t)((MFP_IERA & ~0x21) | old_ier);
    MFP_IMRA = (uint8_t)((MFP_IMRA & ~0x21) | old_imr);
    VEC_TA = old_ta_vec;
    VEC_TB = old_tb_vec;
    SYNC_REG = old_sync;
    stdl_int_restore(sr);
    mode = 0;
    stdl_no_hog = 0;
    stdl_pal_defer = 0;
    stdl_shutdown_overscan = NULL;
}

/* (Re)program prefixes, vectors and enable bits for a mode. The
 * screen must already point at the tall buffer. */
static void ovsc_program(int m)
{
    const uint8_t bits = (uint8_t)(((m & MODE_TOP) ? 0x20 : 0)
                                 | ((m & MODE_BOT) ? 0x01 : 0));
    uint16_t sr = stdl_int_off();

    if (!mode) {
        /* first open: save both timers' state and hook the VBL */
        old_ta_vec = VEC_TA;
        old_tb_vec = VEC_TB;
        old_ier = (uint8_t)(MFP_IERA & 0x21);
        old_imr = (uint8_t)(MFP_IMRA & 0x21);
        stdl_ovsc_old70 = VEC_VBL;
    } else {
        VEC_VBL = stdl_ovsc_old70;      /* prefix swap below */
    }
    MFP_TACR = 0;
    MFP_TBCR = 0;
    MFP_IPRA &= (uint8_t)~0x21;
    MFP_ISRA &= (uint8_t)~0x21;
    VEC_TA = (uint32_t)(uintptr_t)
        ((m == (MODE_TOP | MODE_BOT)) ? stdl_ovsc_ta_both
                                      : stdl_ovsc_ta);
    VEC_TB = (uint32_t)(uintptr_t)stdl_ovsc_tb;
    MFP_IERA = (uint8_t)((MFP_IERA & ~0x21) | bits);
    MFP_IMRA = (uint8_t)((MFP_IMRA & ~0x21) | bits);
    stdl_ovsc_bhi = (uint8_t)((uintptr_t)buf >> 16);
    stdl_ovsc_bmid = (uint8_t)((uintptr_t)buf >> 8);
    stdl_ovsc_missed = 0;
    VEC_VBL = (uint32_t)(uintptr_t)
        ((m == (MODE_TOP | MODE_BOT)) ? stdl_ovsc_vbl_both
         : (m & MODE_TOP)             ? stdl_ovsc_vbl_top
                                      : stdl_ovsc_vbl_bot);
    stdl_int_restore(sr);

    mode = m;
    stdl_no_hog = 1;
    stdl_shutdown_overscan = ovsc_release;
}

static void ovsc_surface(void)
{
    switch (mode) {
    case MODE_TOP:
        stdl_screen.pixels = buf + TOP_HIDDEN_ROWS * STDL_SCREEN_STRIDE;
        stdl_screen.h = STDL_OVERSCAN_TOP_H;
        break;
    case MODE_BOT:
        stdl_screen.pixels = buf;
        stdl_screen.h = STDL_OVERSCAN_BOTTOM_H;
        break;
    case MODE_TOP | MODE_BOT:
        stdl_screen.pixels = buf + TOP_HIDDEN_ROWS * STDL_SCREEN_STRIDE;
        stdl_screen.h = STDL_OVERSCAN_BOTH_H;
        break;
    default:
        stdl_screen.pixels = stdl.page[stdl.backpage];
        stdl_screen.h = STDL_SCREEN_H;
        break;
    }
    stdl_screen.clip.x = 0;
    stdl_screen.clip.y = 0;
    stdl_screen.clip.w = STDL_SCREEN_W;
    stdl_screen.clip.h = (uint16_t)stdl_screen.h;
}

/* Open one border, combining with the other if it is already open.
 * Returns the resulting surface height or 0 with the error set. */
static int ovsc_open(int which)
{
    int m = mode | which;

    if (m == mode) {
        return stdl_screen.h;
    }
    if (!stdl.video_set) {
        STDL_SetError("no video mode set");
        return 0;
    }
    if (stdl.doublebuf) {
        STDL_SetError("border overscan does not combine with "
                      "STDL_DOUBLEBUF");
        return 0;
    }
    if (stdl.mach.mch_cookie >= 0x00020000UL) {
        STDL_SetError("TT/Falcon video has no GLUE borders");
        return 0;
    }
    /* The tricks are built on a 50Hz frame: the top border only
     * exists because a 50Hz picture waits for line 63, and the
     * bottom trick's line counts are PAL's. Opening a border is an
     * active choice, so a 60Hz base screen is not refused - it is
     * switched to 50Hz for as long as a border stays open (the VBL
     * prefix asserts it every frame anyway) and put back on the
     * final close. ST colour monitors sync both rates; a TV fed
     * through a modulator follows its region as it always did. */
    if (!mode) {
        old_sync = (uint8_t)(SYNC_REG & 3);
        SYNC_REG = 2;
    }
    if (buf_alloc == NULL) {
        /* 256-byte alignment satisfies the plain ST's screen base
         * granularity; unused rows stay zero = border colour. One
         * buffer sized for the largest mode serves them all, so a
         * mode change never reallocates */
        buf_alloc = malloc(MAX_FETCH_BYTES + 256);
        if (buf_alloc == NULL) {
            STDL_SetError("out of memory for overscan screen");
            return 0;
        }
        buf = (uint8_t *)(((uintptr_t)buf_alloc + 255)
                          & ~(uintptr_t)255);
    }
    memset(buf, 0, MAX_FETCH_BYTES);

    if (!mode) {
        /* point the display at the tall buffer and let the switch
         * land before the border trick starts, so no frame ever
         * fetches past the end of the old 200-row screen */
        (void)Setscreen(buf, buf, -1);
        STDL_WaitVBL();
        /* palette writes land in the blanking from here on; if no
         * VBL slot is free they stay immediate, as before */
        pal_vbl_ok = (STDL_AddVBL(ovsc_pal_vbl) == 0);
        stdl_pal_defer = (uint8_t)pal_vbl_ok;
    }
    ovsc_program(m);
    ovsc_surface();
    return stdl_screen.h;
}

static int ovsc_close(int which)
{
    int m = mode & ~which;

    if (!(mode & which)) {
        return stdl_screen.h;
    }
    if (m) {
        memset(buf, 0, MAX_FETCH_BYTES);
        ovsc_program(m);
        ovsc_surface();
        return stdl_screen.h;
    }
    ovsc_release();
    if (pal_vbl_ok) {
        STDL_RemoveVBL(ovsc_pal_vbl);
        pal_vbl_ok = 0;
    }
    stdl_palette_flush();
    (void)Setscreen(stdl.page[0], stdl.page[0], -1);
    STDL_WaitVBL();
    ovsc_surface();
    free(buf_alloc);
    buf_alloc = NULL;
    buf = NULL;
    return 0;
}

int STDL_OpenTopBorder(void)
{
    return ovsc_open(MODE_TOP);
}

int STDL_OpenBottomBorder(void)
{
    return ovsc_open(MODE_BOT);
}

void STDL_CloseTopBorder(void)
{
    (void)ovsc_close(MODE_TOP);
}

void STDL_CloseBottomBorder(void)
{
    (void)ovsc_close(MODE_BOT);
}

uint32_t STDL_OverscanMisses(void)
{
    return stdl_ovsc_missed;
}

#else /* !__m68k__ */

int STDL_OpenTopBorder(void)
{
    STDL_SetError("border overscan needs ST hardware");
    return 0;
}

int STDL_OpenBottomBorder(void)
{
    STDL_SetError("border overscan needs ST hardware");
    return 0;
}

void STDL_CloseTopBorder(void)
{
}

void STDL_CloseBottomBorder(void)
{
}

uint32_t STDL_OverscanMisses(void)
{
    return 0;
}

#endif
