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
#else
/* host tests: the bank a TT has selected, as the mode register's low
 * bits */
int stdl_host_resp_bank;
#define TT_SHIFT stdl_host_resp_bank
#endif

static void w4_display(int vga)
{
    if (stdl_rs.falcon) {
#ifdef __m68k__
        /* on RGB or a TV, the desktop's PAL or NTSC: a set or a monitor
         * made for one may not sync to the other */
        (void)VsetScreen(stdl_rs.page[0], stdl_rs.page[0], 3,
                         (int16_t)(BPS4 | COL80
                                   | (vga ? VGA : ((stdl.old_vmode & PAL)
                                                   | VERTFLAG))));
#endif
        /* 640x400 interlaced made 480 lines tall, as the 320-pixel
         * screens' 200 are made 240 */
        if (!vga && !resp_falcon_window()) {
            resp_rows(400);
        }
    } else {
#ifdef __m68k__
        (void)Setscreen(stdl_rs.page[0], stdl_rs.page[0], 4);
#endif
        /* TT Medium's sixteen are the selected bank's registers */
        stdl_rs.bank = (uint8_t)((TT_SHIFT & 0x0F) << 4);
    }
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
    .w = 640, .h = 480, .display = w4_display
};
