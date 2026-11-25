/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * FALCWIN: where a Falcon's picture starts and ends on RGB or a TV.
 * STDL makes its 240- and 480-line screens there by opening the
 * VIDEL's window in a 200- or 400-line mode (resp_falcon_window in
 * src/responsive.c), where TOS's overscan modes would also widen the
 * line to 384 pixels. This sets each of TOS's overscan modes, and each
 * of STDL's screens, and prints the VIDEL's vertical registers for
 * every one, in half-lines of a field: STDL's window should be TOS's.
 *
 *   FW:start
 *   FW:tos 320x240x8 vft=.. vbb=.. vbe=.. vdb=.. vde=.. vco=....
 *   ...
 *   FW:stdl 320x240x8 h=240 vft=.. vbb=.. vbe=.. vdb=.. vde=.. vco=....
 *   FW:done
 *
 * On VGA TOS has no overscan, and the lines are STDL's alone. The
 * registers are TOS's own values, so the comparison holds in Hatari;
 * what a monitor shows of them does not.
 *
 * Build by hand (stems of eight characters, for Hatari's 8.3 drive):
 *   STCMD_NO_TTY=1 stcmd m68k-atari-mint-gcc -O2 -std=gnu99 \
 *       -Iinclude -Ilib/xpad/src -o dist/FALCWIN.TOS \
 *       tests/hatari/falcwin.c libstdl.a
 *   MACHINE=falcon MONITOR=rgb TOS=tos404.img tests/hatari/run.sh fw \
 *       dist/FALCWIN.TOS 8 "waitfor FW:done"
 */

#include <stdio.h>
#include <osbind.h>
#include <mint/falcon.h>
#include <stdl/stdl.h>
#include "../../examples/hold.h"

#define VIDEL(a) (*(volatile uint16_t *)(a))

typedef struct {
    const char *what;
    int h;
    uint16_t vft, vbb, vbe, vdb, vde, vco;
} reading;

static reading got[8];
static int ngot;

static long read_videl(void)
{
    reading *r = &got[ngot];

    r->vft = VIDEL(0xFFFF82A2UL) & 0x7FF;
    r->vbb = VIDEL(0xFFFF82A4UL) & 0x7FF;
    r->vbe = VIDEL(0xFFFF82A6UL) & 0x7FF;
    r->vdb = VIDEL(0xFFFF82A8UL) & 0x7FF;
    r->vde = VIDEL(0xFFFF82AAUL) & 0x7FF;
    r->vco = VIDEL(0xFFFF82C2UL);
    return 0;
}

static void take(const char *what, int h, int super)
{
    got[ngot].what = what;
    got[ngot].h = h;
    if (super) {
        read_videl();
    } else {
        (void)Supexec(read_videl);
    }
    ngot++;
}

int main(void)
{
    static const struct {
        const char *what;
        int16_t mode;
    } tos[] = {
        { "tos 320x240x8", BPS8 | COL40 | OVERSCAN },
        { "tos 320x240x4", BPS4 | COL40 | OVERSCAN },
        { "tos 640x480x4", BPS4 | COL80 | VERTFLAG | OVERSCAN },
    };
    const int16_t old = VsetMode(-1);
    void *phys = Physbase(), *logb = Logbase();
    STDL_Surface *s;
    char *buf, *page;
    int i;

    hold_on_exit("FALCWIN");
    printf("FW:start mode=%04x monitor=%d\n", (unsigned)old,
           (int)VgetMonitor());
    if (VgetMonitor() != MON_VGA) {
        /* room for the largest: 768x480 in 4 planes */
        buf = (char *)Mxalloc(768L * 480 / 2 + 256, 0);
        if (buf == NULL) {
            printf("FW:no memory\n");
            return 1;
        }
        page = (char *)(((long)buf + 255) & ~255L);
        for (i = 0; i < 3; i++) {
            (void)VsetScreen(page, page, 3,
                             (int16_t)(tos[i].mode | (old & PAL)));
            Vsync();
            Vsync();
            take(tos[i].what, 0, 0);
        }
        (void)VsetScreen(logb, phys, 3, old);
        Vsync();
        Mfree(buf);
    }
    if (STDL_Init(STDL_INIT_VIDEO) < 0
        || STDL_UseResponsive(STDL_RESPONSIVE_COLOUR | STDL_RESPONSIVE_HEIGHT
                              | STDL_RESPONSIVE_WIDE) < 0) {
        printf("FW:init: %s\n", STDL_GetError());
        return 1;
    }
    s = STDL_SetVideoMode(320, 240, 8, STDL_DOUBLEBUF);
    take("stdl 320x240x8", s ? s->h : -1, 1);
    s = STDL_SetVideoMode(320, 240, 4, STDL_DOUBLEBUF);
    take("stdl 320x240x4", s ? s->h : -1, 1);
    s = STDL_SetVideoMode(320, 200, 8, STDL_DOUBLEBUF);
    take("stdl 320x200x8", s ? s->h : -1, 1);
    s = STDL_SetVideoMode(640, 480, 4, STDL_DOUBLEBUF);
    take("stdl 640x480x4", s ? s->h : -1, 1);
    STDL_Quit();
    for (i = 0; i < ngot; i++) {
        const reading *r = &got[i];

        printf("FW:%s", r->what);
        if (r->h) {
            printf(" h=%d", r->h);
        }
        printf(" vft=%u vbb=%u vbe=%u vdb=%u vde=%u vco=%04x\n",
               r->vft, r->vbb, r->vbe, r->vdb, r->vde, r->vco);
    }
    printf("FW:done\n");
    return 0;
}
