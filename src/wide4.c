/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * The wide screen (STDL_RESPONSIVE_WIDE): 640x480 in 16 colours on a
 * TT (TT Medium) and a Falcon (its own mode on VGA; on RGB or a TV
 * 640x400 interlaced with the window opened to 480 lines). Both are
 * the ST's interleaved 4-plane layout, 320 bytes a row, so the screen
 * is an ordinary 4-plane surface over the display page itself: every
 * call and a program's own writes work on it as on an ST Low screen,
 * and Flip has nothing to copy, only the page to swap. Its colours
 * 0-15 go to the TT's (the selected bank's) or the Falcon's own
 * registers (responsive.c). Its own object, so that a program asking
 * for the colour or the tall screen carries none of it.
 */

#include "responsive.h"

#ifdef __m68k__
#include <osbind.h>
#include <mint/falcon.h>

#define TT_SHIFT  (*(volatile uint16_t *)0xFFFF8262UL)
/* the VIDEL's vertical blank's start and end, and the picture's first
 * line and the line after its last: in lines while it interlaces */
#define VIDEL_VBB (*(volatile uint16_t *)0xFFFF82A4UL)
#define VIDEL_VBE (*(volatile uint16_t *)0xFFFF82A6UL)
#define VIDEL_VDB (*(volatile uint16_t *)0xFFFF82A8UL)
#define VIDEL_VDE (*(volatile uint16_t *)0xFFFF82AAUL)
#else
/* host tests: a Falcon's RGB with no room for 480 lines, and the bank
 * a TT has selected; and the registers (responsive.c) */
int stdl_host_resp_400, stdl_host_resp_bank;
extern uint32_t stdl_host_resp_regs[256];
#endif

/* 640x480, 320 bytes a line on every machine and monitor */
static int w4_geometry(int vga)
{
    (void)vga;
    stdl_rs.w = 640;
    stdl_rs.h = 480;
    stdl_rs.line = 320;
    return 480;
}

/* the screen's rows, fewer than it was set up with: the display shows
 * no more */
static void w4_rows(int rows)
{
    stdl_rs.h = (int16_t)rows;
    stdl_rs.screen.s.h = (int16_t)rows;
    stdl_rs.screen.s.clip.h = (uint16_t)rows;
}

#ifdef __m68k__
/*
 * A Falcon's 640x400 interlaced on RGB or a TV made 480 lines tall: the
 * window opened 40 lines at each end, the window the Falcon's own
 * overscan modes have, so the line stays 640 pixels and the page the
 * TT's and VGA's. NTSC has too few lines below the picture for 40 more,
 * so the window moves up into those above by what it lacks. 0, left at
 * 400 lines, where a mode set some other way leaves no room above
 * either. Read after the VBL that takes the mode set.
 */
static int w4_falcon_480(void)
{
    int vdb, vde, vbb, vbe;

    STDL_WaitVBL();
    vdb = (VIDEL_VDB & 0x7FF) - 40;
    vde = (VIDEL_VDE & 0x7FF) + 40;
    vbb = VIDEL_VBB & 0x7FF;
    vbe = VIDEL_VBE & 0x7FF;
    if (vde > vbb) {
        vdb -= vde - vbb;
        vde = vbb;
    }
    if (vdb < vbe) {
        return 0;
    }
    VIDEL_VDB = (uint16_t)vdb;
    VIDEL_VDE = (uint16_t)vde;
    return 1;
}
#endif

/* the TT's selected bank: TT Medium's sixteen are its registers */
static uint16_t w4_bank;

/* a word for the ST's registers (stdl_pal_write) onto the bank's, the
 * TT's 4 bits a channel */
static void w4_tt_word(int index, uint16_t word)
{
    STDL_Colour c;

    stdl_hw_to_rgb(word, &c);
#ifdef __m68k__
    STDL_TTPAL[w4_bank + index]
#else
    stdl_host_resp_regs[w4_bank + index]
#endif
        = (uint16_t)(((c.r >> 4) << 8) | (c.g & 0xF0) | (c.b >> 4));
}

static void w4_display(int vga)
{
#ifdef __m68k__
    if (stdl_rs.falcon) {
        /* on RGB or a TV, the desktop's PAL or NTSC: a set or a monitor
         * made for one may not sync to the other */
        (void)VsetScreen(stdl_rs.page[0], stdl_rs.page[0], 3,
                         (int16_t)(BPS4 | COL80
                                   | (vga ? VGA : ((stdl.old_vmode & PAL)
                                                   | VERTFLAG))));
        if (!vga && !w4_falcon_480()) {
            w4_rows(400);
        }
    } else {
        (void)Setscreen(stdl_rs.page[0], stdl_rs.page[0], 4);
        w4_bank = (uint16_t)((TT_SHIFT & 0x0F) << 4);
        stdl_pal_write = w4_tt_word;
    }
#else
    if (stdl_rs.falcon && !vga && stdl_host_resp_400) {
        w4_rows(400);
    }
    if (!stdl_rs.falcon) {
        w4_bank = (uint16_t)(stdl_host_resp_bank << 4);
        stdl_pal_write = w4_tt_word;
    }
#endif
}

/* nothing of its own: the pages are the display's */
static int w4_screen_init(void)
{
    stdl_rs.track = NULL;       /* the present reads no box */
    return 0;
}

static void w4_screen_page(int p)
{
    STDL_Surface *s = &stdl_rs.screen.s;

    s->pixels = stdl_rs.page[p];
    s->stride = (uint16_t)stdl_rs.line;
}

static void w4_screen_free(void)
{
}

/* no ops: its surfaces have 4 planes, made and drawn as an ST's are;
 * no present: the page drawn is the page shown */
const resp_model stdl_resp_wide = {
    .planes = 4,
    .screen_init = w4_screen_init, .screen_page = w4_screen_page,
    .screen_free = w4_screen_free,
    .geometry = w4_geometry, .display = w4_display
};
