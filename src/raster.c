/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * Rasters: colour registers rewritten down the screen by Timer B.
 *
 * Timer B in event-count mode counts the Shifter's Display Enable, a
 * pulse per displayed line, and interrupts every `step` lines; its
 * handler writes the next entry of a table into one or two colour
 * registers. A VBL callback stops the timer, writes the table's first
 * entry and restarts the count before the picture begins, so every
 * frame starts from the same place whatever the last one did.
 *
 * TOS's 200Hz Timer C handler runs for over two scanlines at the
 * CPU's level 6, which holds an MFP interrupt off and would shake a
 * raster by two lines four times a frame. So, as overscan.c does
 * with its borders, a prefix on the Timer C vector lowers the mask
 * to 5 inside that handler, and Timer B (MFP channel 8, above Timer
 * C's 5) is taken through it.
 *
 * A TT shows ST Low on its VGA monitor with each line drawn twice, and
 * its Timer B counts a picture line once: 199-200 events a frame on a
 * TT, as in ST Medium, where TT Low counts each of its 480 lines
 * (TTCOUNT, November 2026). Hatari, which times a TT's video as an
 * STE's, counts ST Low the same. An open on a TT still counts a
 * frame's events and takes the step in them, as the check that the
 * count is a picture's: about 200 is one a line, about 400 two, and
 * the step is doubled. A TT's own modes (a responsive screen's TT Low)
 * and a Falcon are still refused: stock Hatari gives TT Low's 480 lines
 * the 200 events of an ST's frame (the Hatari fork counts 480), and a
 * Falcon's count has not been measured.
 *
 * Programs used to write all of this themselves, and two things
 * went wrong that a library can prevent: Timer B is also what an
 * open border runs on, and nothing stopped both claiming it; and the
 * GEMDOS terminate path did not know about the vectors, so an
 * abnormal exit left Timer B and Timer C pointing into freed memory.
 * Here the two refuse each other (stdl_timerb_owner) and the
 * teardown is on the terminate path with the rest.
 *
 * Its own translation unit: a program without rasters does not carry
 * it.
 */

#include <string.h>
#include "stdl_internal.h"

#define PICTURE_LINES 200
/* step 1: an entry a line, and the one the last line's interrupt
 * writes for the bottom border */
#define MAX_ENTRIES   (PICTURE_LINES + 1)

/*
 * Three tables. The interrupts read the armed one until the next
 * VBL, which then arms the one shown; the program fills the third.
 * With two, the table just shown would be refilled while this
 * frame's lower half still read it.
 */
static uint16_t tab[3][MAX_ENTRIES * 2];
static volatile int8_t showing = -1;    /* the next VBL arms it     */
static volatile int8_t armed = -1;      /* this frame's             */
static int drawing;                     /* the program's            */
static int entries, words;              /* a frame's, an entry's    */
static uint8_t step, r1, r2;
static uint8_t open_;

/* shared with the handlers below */
uint16_t *volatile stdl_rast_ptr;
volatile int16_t stdl_rast_reg1, stdl_rast_reg2;
uint32_t stdl_rast_oldtc;
volatile uint16_t stdl_rast_events;     /* tt_count's */

static uint32_t old_tb;
static uint8_t old_ier, old_imr;
/* our Timer C prefix is in the vector's chain: hooked at open and
 * only unhooked if nobody hooked after us, so a reopen must not hook
 * it again - its saved vector would then be itself, or a chain back
 * to it, and the first tick would loop for ever */
static uint8_t tc_hooked;

#ifdef __m68k__
#define MFP_IERA (*(volatile uint8_t *)0xFFFFFA07UL)
#define MFP_IPRA (*(volatile uint8_t *)0xFFFFFA0BUL)
#define MFP_ISRA (*(volatile uint8_t *)0xFFFFFA0FUL)
#define MFP_IMRA (*(volatile uint8_t *)0xFFFFFA13UL)
#define MFP_TBCR (*(volatile uint8_t *)0xFFFFFA1BUL)
#define MFP_TBDR (*(volatile uint8_t *)0xFFFFFA21UL)
#define VEC_TB   (*(volatile uint32_t *)0x120UL)
#define VEC_TC   (*(volatile uint32_t *)0x114UL)
#else
/* tests/host/stubs.c: the MFP's registers by their offset, and the
 * two vectors, as plain memory */
#define MFP_IERA stdl_host_mfp[0x07]
#define MFP_IPRA stdl_host_mfp[0x0B]
#define MFP_ISRA stdl_host_mfp[0x0F]
#define MFP_IMRA stdl_host_mfp[0x13]
#define MFP_TBCR stdl_host_mfp[0x1B]
#define MFP_TBDR stdl_host_mfp[0x21]
#define VEC_TB   stdl_host_vec_tb
#define VEC_TC   stdl_host_vec_tc
#endif

#ifdef __m68k__
void stdl_rast_isr1(void);
void stdl_rast_isr2(void);
void stdl_rast_isr0(void);
void stdl_rast_isr0_1(void), stdl_rast_isr0_2(void), stdl_rast_isr0_3(void),
     stdl_rast_isr0_4(void), stdl_rast_isr0_5(void), stdl_rast_isr0_6(void),
     stdl_rast_isr0_7(void), stdl_rast_isr0_8(void), stdl_rast_isr0_9(void),
     stdl_rast_isr0_10(void), stdl_rast_isr0_11(void),
     stdl_rast_isr0_12(void), stdl_rast_isr0_13(void),
     stdl_rast_isr0_14(void), stdl_rast_isr0_15(void);
/* colour 0 alone, then with colour k at [k] */
static void (*const isr0k[16])(void) = {
    stdl_rast_isr0, stdl_rast_isr0_1, stdl_rast_isr0_2, stdl_rast_isr0_3,
    stdl_rast_isr0_4, stdl_rast_isr0_5, stdl_rast_isr0_6, stdl_rast_isr0_7,
    stdl_rast_isr0_8, stdl_rast_isr0_9, stdl_rast_isr0_10,
    stdl_rast_isr0_11, stdl_rast_isr0_12, stdl_rast_isr0_13,
    stdl_rast_isr0_14, stdl_rast_isr0_15
};
void stdl_rast_tc(void);
void stdl_rast_count(void);

/*
 * The handlers. The registers' addresses are words, sign-extended by
 * movea.w to $FFFF82xx. Measured (tests/hatari/rastchk.c) at about
 * 220 cycles an interrupt with one register and 250 with two, entry
 * and rte included: a raster every second line is a hundred a frame,
 * 14-16% of a plain ST. Colour 0's own handlers below take that to
 * 11-12%.
 */
__asm__(
"    .text\n"
"    .even\n"
"    .globl _stdl_rast_isr1\n"
"_stdl_rast_isr1:\n"
"    movem.l %a0-%a1,-(%sp)\n"
"    move.l _stdl_rast_ptr,%a0\n"
"    move.w _stdl_rast_reg1,%a1\n"
"    move.w (%a0)+,(%a1)\n"
"    move.l %a0,_stdl_rast_ptr\n"
"    movem.l (%sp)+,%a0-%a1\n"
"    move.b #0xFE,0xfffffa0f.w\n"   /* Timer B's in-service bit */
"    rte\n"
"    .even\n"
"    .globl _stdl_rast_isr2\n"
"_stdl_rast_isr2:\n"
"    movem.l %a0-%a1,-(%sp)\n"
"    move.l _stdl_rast_ptr,%a0\n"
"    move.w _stdl_rast_reg1,%a1\n"
"    move.w (%a0)+,(%a1)\n"
"    move.w _stdl_rast_reg2,%a1\n"
"    move.w (%a0)+,(%a1)\n"
"    move.l %a0,_stdl_rast_ptr\n"
"    movem.l (%sp)+,%a0-%a1\n"
"    move.b #0xFE,0xfffffa0f.w\n"
"    rte\n"
/*
 * Colour 0 first, at a fixed address: one register saved, and the
 * write about 20 cycles sooner after the request. Colour 0 is also
 * the border, so its change shows from the left border of the next
 * line only if it lands before that line starts - which the general
 * handlers above miss now and then by a few dozen cycles.
 */
"    .even\n"
"    .globl _stdl_rast_isr0\n"
"_stdl_rast_isr0:\n"
"    move.l %a0,-(%sp)\n"
"    move.l _stdl_rast_ptr,%a0\n"
"    move.w (%a0)+,0xffff8240.w\n"
"    move.l %a0,_stdl_rast_ptr\n"
"    move.l (%sp)+,%a0\n"
"    move.b #0xFE,0xfffffa0f.w\n"
"    rte\n"
/*
 * Colour 0 and one of the other fifteen: a handler for each, the
 * second register's address an absolute operand like the first's.
 * Fetching the address and saving a second register for it, as the
 * general handler does, made each interrupt a third dearer than one
 * demo's own handler with the address written in.
 */
#define RAST_ISR0K(k, reg) \
"    .even\n" \
"    .globl _stdl_rast_isr0_" #k "\n" \
"_stdl_rast_isr0_" #k ":\n" \
"    move.l %a0,-(%sp)\n" \
"    move.l _stdl_rast_ptr,%a0\n" \
"    move.w (%a0)+,0xffff8240.w\n" \
"    move.w (%a0)+," reg ".w\n" \
"    move.l %a0,_stdl_rast_ptr\n" \
"    move.l (%sp)+,%a0\n" \
"    move.b #0xFE,0xfffffa0f.w\n" \
"    rte\n"
RAST_ISR0K(1, "0xffff8242") RAST_ISR0K(2, "0xffff8244")
RAST_ISR0K(3, "0xffff8246") RAST_ISR0K(4, "0xffff8248")
RAST_ISR0K(5, "0xffff824a") RAST_ISR0K(6, "0xffff824c")
RAST_ISR0K(7, "0xffff824e") RAST_ISR0K(8, "0xffff8250")
RAST_ISR0K(9, "0xffff8252") RAST_ISR0K(10, "0xffff8254")
RAST_ISR0K(11, "0xffff8256") RAST_ISR0K(12, "0xffff8258")
RAST_ISR0K(13, "0xffff825a") RAST_ISR0K(14, "0xffff825c")
RAST_ISR0K(15, "0xffff825e")
/* a TT's frame counted: one an event */
"    .even\n"
"    .globl _stdl_rast_count\n"
"_stdl_rast_count:\n"
"    addq.w #1,_stdl_rast_events\n"
"    move.b #0xFE,0xfffffa0f.w\n"
"    rte\n"
/* Timer C prefix: TOS's handler opened to Timer B, as overscan.c's */
"    .even\n"
"    .globl _stdl_rast_tc\n"
"_stdl_rast_tc:\n"
"    move.w #0x2500,%sr\n"
"    move.l _stdl_rast_oldtc,-(%sp)\n"
"    rts\n"
);
#define ISR1  ((uint32_t)(uintptr_t)stdl_rast_isr1)
#define ISR2  ((uint32_t)(uintptr_t)stdl_rast_isr2)
#define ISR0  ((uint32_t)(uintptr_t)stdl_rast_isr0)
#define ISR0K(k) ((uint32_t)(uintptr_t)isr0k[k])
#define TC    ((uint32_t)(uintptr_t)stdl_rast_tc)
#define COUNT ((uint32_t)(uintptr_t)stdl_rast_count)
#else
/* the host has no MFP: the vectors are tokens the tests read */
#define ISR1  1u
#define ISR2  2u
#define ISR0  4u
#define ISR0K(k) (0x100u + (k))
#define TC    3u
#define COUNT 5u
#endif

/* A TT's Timer B events in a frame of its ST Low: an event a line
 * counted from one VBL to the next, Timer B left as it was found.
 * 1 for about 200, 2 for about 400; 0 if it counts nothing like a
 * picture's lines. Three frames, at each open. */
static uint8_t tt_count(void)
{
    const uint32_t vec = VEC_TB;
    const uint8_t ier = (uint8_t)(MFP_IERA & 0x01);
    const uint8_t imr = (uint8_t)(MFP_IMRA & 0x01);
    uint16_t sr, n;

    STDL_WaitVBL();
    sr = stdl_int_off();
    MFP_TBCR = 0;
    VEC_TB = COUNT;
    MFP_TBDR = 1;
    MFP_TBCR = 8;
    MFP_IERA |= 0x01;
    MFP_IMRA |= 0x01;
    stdl_int_restore(sr);
    STDL_WaitVBL();
    stdl_rast_events = 0;
    STDL_WaitVBL();
#ifdef __m68k__
    n = stdl_rast_events;
#else
    n = stdl_host_tt_events;            /* the tests' frame */
#endif
    sr = stdl_int_off();
    MFP_TBCR = 0;
    MFP_IPRA &= (uint8_t)~0x01;
    MFP_ISRA &= (uint8_t)~0x01;
    MFP_IERA = (uint8_t)((MFP_IERA & ~0x01) | ier);
    MFP_IMRA = (uint8_t)((MFP_IMRA & ~0x01) | imr);
    VEC_TB = vec;
    stdl_int_restore(sr);
    if (n < 150 || n > 500) {
        return 0;
    }
    return n > 300 ? 2 : 1;
}

/* Before the picture: the first entry by hand, the count restarted.
 * Every frame, so a frame that ran long cannot carry its position
 * into the next. */
static void rast_vbl(void)
{
    const uint16_t *t;

    if (showing < 0) {
        return;
    }
    armed = showing;
    t = tab[(int)armed];
    MFP_TBCR = 0;
    STDL_HWPAL[r1] = t[0];
    if (words == 2) {
        STDL_HWPAL[r2] = t[1];
    }
    stdl_rast_ptr = (uint16_t *)t + words;
    MFP_TBDR = step;
    MFP_TBCR = 8;               /* event count: Display Enable */
}

/* the teardown, here and on the terminate path: vectors and MFP
 * registers only */
static void rast_release(void)
{
    uint16_t sr;

    if (!open_) {
        return;
    }
    sr = stdl_int_off();
    MFP_TBCR = 0;
    MFP_IPRA &= (uint8_t)~0x01;
    MFP_ISRA &= (uint8_t)~0x01;
    MFP_IERA = (uint8_t)((MFP_IERA & ~0x01) | old_ier);
    MFP_IMRA = (uint8_t)((MFP_IMRA & ~0x01) | old_imr);
    VEC_TB = old_tb;
    if (VEC_TC == TC) {         /* unless someone hooked it after us */
        VEC_TC = stdl_rast_oldtc;
        tc_hooked = 0;
    }
    stdl_int_restore(sr);
    open_ = 0;
    showing = armed = -1;
    stdl_timerb_owner = 0;
    stdl_shutdown_rasters = NULL;
}

int STDL_OpenRasters(int lines, int reg1, int reg2)
{
    uint16_t sr;
    int i;

    if (open_) {
        STDL_CloseRasters();
    }
    if (!stdl.video_set) {
        STDL_SetError("no video mode set");
        return -1;
    }
    if (stdl.mach.mch_cookie >= 0x00020000UL && !stdl_is_tt()) {
        /* a Falcon's VIDEL lines: see the top of the file */
        STDL_SetError("rasters need an ST, STE, Mega STE or TT (not a "
                      "Falcon)");
        return -1;
    }
    if (stdl_is_tt() && stdl_resp_screen != NULL) {
        STDL_SetError("rasters on a TT need its ST Low screen, not a "
                      "responsive one");
        return -1;
    }
    if (stdl_timerb_owner == STDL_TB_BORDER) {
        STDL_SetError("a border is using Timer B");
        return -1;
    }
    if (lines < 1 || lines > 255 || reg1 < 0 || reg1 > 15 || reg2 > 15
        || reg2 == reg1) {
        STDL_SetError("bad raster step or colour register");
        return -1;
    }
    if (reg2 < 0) {
        reg2 = -1;
    }
    step = (uint8_t)lines;
    if (stdl_is_tt()) {
        const uint8_t tt_x = tt_count();

        if (tt_x == 0) {
            STDL_SetError("Timer B does not count this TT's lines");
            return -1;
        }
        if (lines * tt_x > 255) {
            STDL_SetError("raster step over 127 on a TT counting both "
                          "of a line's two");
            return -1;
        }
        step = (uint8_t)(lines * tt_x);
    }
    /* an entry for the picture's top, one for each interrupt - the
     * last, at the end of the picture, colours the bottom border */
    entries = STDL_DivU16(PICTURE_LINES, (unsigned)lines) + 1;
    words = reg2 >= 0 ? 2 : 1;
    r1 = (uint8_t)reg1;
    r2 = (uint8_t)(reg2 < 0 ? 0 : reg2);
    stdl_rast_reg1 = (int16_t)(0x8240 + reg1 * 2);
    stdl_rast_reg2 = (int16_t)(0x8240 + (reg2 < 0 ? 0 : reg2) * 2);
    /* both tables start as the palette, so nothing changes on screen
     * until the program fills one in */
    {
        const uint16_t c1 = STDL_HWColour(stdl.colours[r1].r,
                                          stdl.colours[r1].g,
                                          stdl.colours[r1].b);
        const uint16_t c2 = STDL_HWColour(stdl.colours[r2].r,
                                          stdl.colours[r2].g,
                                          stdl.colours[r2].b);
        uint16_t *t = tab[0];

        for (i = 0; i < entries; i++) {
            *t++ = c1;
            if (words == 2) {
                *t++ = c2;
            }
        }
    }
    memcpy(tab[1], tab[0], sizeof tab[0]);
    memcpy(tab[2], tab[0], sizeof tab[0]);
    drawing = 1;
    showing = 0;
    armed = 0;
    if (STDL_AddVBL(rast_vbl) < 0) {
        showing = -1;
        return -1;
    }

    sr = stdl_int_off();
    old_tb = VEC_TB;
    old_ier = (uint8_t)(MFP_IERA & 0x01);
    old_imr = (uint8_t)(MFP_IMRA & 0x01);
    if (!tc_hooked) {
        stdl_rast_oldtc = VEC_TC;
        VEC_TC = TC;
        tc_hooked = 1;
    }
    MFP_TBCR = 0;
    stdl_rast_ptr = tab[0] + words;
    VEC_TB = r1 == 0 ? ISR0K(words == 2 ? r2 : 0)
                     : (words == 2 ? ISR2 : ISR1);
    MFP_IERA |= 0x01;
    MFP_IMRA |= 0x01;
    open_ = 1;
    stdl_timerb_owner = STDL_TB_RASTERS;
    stdl_shutdown_rasters = rast_release;
    stdl_int_restore(sr);
    return entries;
}

uint16_t *STDL_RasterTable(void)
{
    return open_ ? tab[drawing] : NULL;
}

void STDL_ShowRasters(int keep)
{
    int next;

    if (!open_) {
        return;
    }
    /* a byte store: the VBL sees one table or the other. Then the
     * table to fill next is the one neither the VBL nor this frame's
     * interrupts will read - armed is read after the store, so a VBL
     * between the two has already moved on to the table shown */
    showing = (int8_t)drawing;
    for (next = 0; next == showing || next == armed; next++) {
    }
    if (keep) {
        memcpy(tab[next], tab[drawing], (size_t)(entries << words));
    }
    drawing = next;
}

void STDL_CloseRasters(void)
{
    if (!open_) {
        return;
    }
    STDL_RemoveVBL(rast_vbl);
    rast_release();
    stdl_palette_apply_hw();    /* the palette's own colours back */
}
