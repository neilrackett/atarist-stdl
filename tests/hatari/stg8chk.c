/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * On-target check of staged colours on an 8-plane screen
 * (STDL_StagePalette, stdl_responsive.h), on a TT or a Falcon. Colour
 * 1 changes with every frame, its red saying which page the frame
 * drew, staged just before STDL_Flip; four voices mix all the while,
 * their VBL slot ahead of staging's. A VBL callback of the check's
 * own, installed last, logs at every blank the page the display takes
 * and colour 1's register, and each must agree: a colour shown with
 * another page's picture is a colour in the wrong blank. Frames of 1.5
 * VBLs, double-buffered and then triple-buffered, and then frames of
 * no time triple-buffered, where Flip's wait spans the blank that
 * takes the page before; the console's text after each mode set makes
 * a TT's first Flip of page 0 copy the whole page. Also checked: a
 * staged colour is not in the register before the blank.
 *
 * It prints, too, what Hatari may not model: the line the beam is on
 * when the log runs (a Falcon's VFC against VDB, $FFFF82A0 and
 * $FFFF82A8, in half-lines: below VDB is still the blank; a TT's video
 * counter against the page), and on a Falcon the desktop's mode word
 * and the mode the screen got, with the display's rate (PAL or NTSC
 * kept from the desktop).
 *
 *   STCMD_NO_TTY=1 stcmd m68k-atari-mint-gcc -O2 -std=gnu99 \
 *       -Iinclude -Ilib/xpad/src -o dist/STG8CHK.TOS \
 *       tests/hatari/stg8chk.c libstdl.a
 *   TOS="<TOS 3.06>" MACHINE=tt LATE="--mmu off" \
 *       EXTRA="--cpu-exact on --compatible on --memsize 4" \
 *       tests/hatari/run.sh s8 dist/STG8CHK.TOS 8 "waitfor S8:done"
 */
#include <stdio.h>
#include <stdlib.h>
#include <osbind.h>
#include <mint/falcon.h>
#include <stdl/stdl.h>
#include <stdl/stdl_responsive.h>
#include "../../examples/hold.h"

#define FRCLOCK (*(volatile uint32_t *)0x466UL)
#define HZ200   (*(volatile uint32_t *)0x4BAUL)
#define VB_HI   (*(volatile uint8_t *)0xFFFF8201UL)
#define VB_MID  (*(volatile uint8_t *)0xFFFF8203UL)
#define VC_HI   (*(volatile uint8_t *)0xFFFF8205UL)
#define VC_MID  (*(volatile uint8_t *)0xFFFF8207UL)
#define VC_LO   (*(volatile uint8_t *)0xFFFF8209UL)
#define VFC     (*(volatile uint16_t *)0xFFFF82A0UL)
#define VDB     (*(volatile uint16_t *)0xFFFF82A8UL)
#define TTPAL   ((volatile uint16_t *)0xFFFF8400UL)
#define F30PAL  ((volatile uint32_t *)0xFFFF9800UL)
#define FRAMES  60
#define LOG     256

static int falcon;
static uint32_t page_at[3];             /* each page's pixels        */
static int npages;
static volatile int lg_on, lg_n;
static volatile uint32_t lg_base[LOG], lg_col[LOG], lg_beam[LOG];

/* the colour's page, from colour 1's register */
static int col_page(uint32_t w)
{
    return falcon ? (int)((w >> 30) & 3) - 1 : (int)((w >> 10) & 3) - 1;
}

/* the page a display base is, the one whose pixels lie nearest past it */
static int base_page(uint32_t base)
{
    uint32_t best = 0xFFFFFFFFUL;
    int i, p = -1;

    for (i = 0; i < npages; i++) {
        if (page_at[i] >= base && page_at[i] - base < best) {
            best = page_at[i] - base;
            p = i;
        }
    }
    return p;
}

/* last in the VBL queue: what the blank left */
static void logger(void)
{
    uint32_t b;
    int n = lg_n;

    if (!lg_on || n >= LOG) {
        return;
    }
    b = ((uint32_t)VB_HI << 16) | ((uint32_t)VB_MID << 8);
    lg_base[n] = b;
    lg_col[n] = falcon ? F30PAL[1] : TTPAL[1];
    lg_beam[n] = falcon ? VFC
        : (((uint32_t)VC_HI << 16) | ((uint32_t)VC_MID << 8) | VC_LO) - b;
    lg_n = n + 1;
}

static int rate(void)
{
    uint32_t h = HZ200, v;

    while (HZ200 == h) {
    }
    h = HZ200;
    v = FRCLOCK;
    while (HZ200 - h < 200) {
    }
    return (int)(FRCLOCK - v);
}

static uint32_t reg1(void)
{
    return falcon ? F30PAL[1] : TTPAL[1];
}

/* a colour as its register holds it */
static uint32_t word(const STDL_Colour *c)
{
    if (falcon) {
        return ((uint32_t)(c->r & 0xFC) << 24) | ((uint32_t)(c->g & 0xFC) << 16)
             | (uint32_t)(c->b & 0xFC);
    }
    return (uint32_t)(((c->r >> 4) << 8) | (c->g & 0xF0) | (c->b >> 4));
}

static int8_t noise[4096];

int main(void)
{
    hold_on_exit("STG8CHK");
    /* double- and triple-buffered at 1.5 VBLs a frame, then triple at
     * none, where Flip waits for the page before to be taken */
    static const uint32_t modes[3] = { STDL_DOUBLEBUF, STDL_TRIPLEBUF,
                                       STDL_TRIPLEBUF };
    const STDL_MachineInfo *mi;
    STDL_Surface *s;
    int m, f, i, k, ticks, bad = 0, early, hz, desk = 0, fail = 0, first = 0;
    uint32_t bmin, bmax;

    for (i = 0; i < (int)sizeof(noise); i++) {
        noise[i] = (int8_t)((i * 75 + (i >> 3) * 13) & 0x7F) - 64;
    }
    if (STDL_Init(STDL_INIT_VIDEO) < 0) {
        printf("S8: init: %s\nS8:done FAIL\n", STDL_GetError());
        return 1;
    }
    if (STDL_GetMachineInfo()->is_falcon) {
        desk = VsetMode(-1);
    }
    STDL_UseResponsive(STDL_RESPONSIVE_COLOUR);
    for (m = 0; m < 3; m++) {
        s = STDL_SetVideoMode(320, 200, 8, modes[m]);
        if (s == NULL || s->format->BitsPerPixel != 8) {
            printf("S8: no 8-plane screen: %s\nS8:done FAIL\n",
                   s == NULL ? STDL_GetError() : "4 planes");
            return 1;
        }
        mi = STDL_GetMachineInfo();
        falcon = mi->is_falcon;
        if (m == 0) {
            if (STDL_OpenVoices(12517) == 0) {
                for (k = 0; k < 4; k++) {
                    STDL_SetVoice(k, noise, sizeof(noise), 0, sizeof(noise),
                                  6258 + k * 1000, 32);
                }
            }
            if (STDL_StagePalette(1) != 0 || STDL_AddVBL(logger) != 0) {
                printf("S8: staging or the log: %s\nS8:done FAIL\n",
                       STDL_GetError());
                return 1;
            }
        }
        hz = rate();
        ticks = m == 2 ? 0 : (300 + hz / 2) / hz;
        npages = (s->flags & STDL_TRIPLEBUF) ? 3 : 2;
        for (i = 0; i < npages; i++) {
            page_at[i] = (uint32_t)s->pixels;
            STDL_Flip();
        }
        printf("S8: %s, %d pages, %d VBLs a second, frames of %d ticks\n",
               falcon ? "Falcon" : "TT", npages, hz, ticks);
        if (falcon) {
            printf("S8: mode word %04x, the desktop's %04x\n",
                   (unsigned)VsetMode(-1), (unsigned)desk);
        }

        lg_n = 0;
        early = 0;
        lg_on = 1;
        for (f = 0; f < FRAMES; f++) {
            STDL_Colour c;
            uint32_t h;

            for (k = 0; k < ticks; k++) {
                h = HZ200;
                while (HZ200 == h) {
                }
            }
            for (k = 0; k < npages && page_at[k] != (uint32_t)s->pixels; k++) {
            }
            c.r = (uint8_t)((k + 1) << 6);
            c.g = (uint8_t)(f << 2);
            c.b = 0x80;
            c.unused = 0;
            if (f == 0) {
                first = k;
            }
            STDL_SetColours(s, &c, 1, 1);
            early += reg1() == word(&c);
            STDL_Flip();
        }
        STDL_WaitVBL();
        STDL_WaitVBL();
        lg_on = 0;

        bmin = 0xFFFFFFFFUL;
        bmax = 0;
        /* from the first blank showing the first frame's page */
        for (k = 0; k < lg_n && base_page(lg_base[k]) != first; k++) {
        }
        for (bad = 0; k < lg_n; k++) {
            const int p = base_page(lg_base[k]), c = col_page(lg_col[k]);

            if (p != c) {
                if (bad < 4) {
                    printf("S8: blank %d shows page %d in page %d's colour\n",
                           k, p, c);
                }
                bad++;
            }
            if (lg_beam[k] < bmin) bmin = lg_beam[k];
            if (lg_beam[k] > bmax) bmax = lg_beam[k];
        }
        printf("S8: %d blanks logged, %d in another page's colour, "
               "%d staged colours written at once\n", lg_n, bad, early);
        if (falcon) {
            printf("S8: VFC at the log %lu-%lu, VDB %u (half-lines)\n",
                   (unsigned long)bmin, (unsigned long)bmax, (unsigned)VDB);
        } else {
            printf("S8: video counter at the log %lu-%lu bytes into the "
                   "page\n", (unsigned long)bmin, (unsigned long)bmax);
        }
        fail |= bad != 0 || early != 0 || lg_n < FRAMES;
    }
    STDL_RemoveVBL(logger);
    STDL_StagePalette(0);
    STDL_CloseVoices();
    STDL_Quit();
    printf("S8:done %s\n", fail ? "FAIL" : "PASS");
    fflush(stdout);
    return fail;
}
