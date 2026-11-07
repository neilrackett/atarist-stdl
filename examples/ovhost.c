/*
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: CC0-1.0
 *
 * OVHOST: both borders open, 273 lines, in a program that runs the
 * machine itself and has none of STDL but the overscan host object
 * (stdl_overscan.h, "A program that runs the machine itself"): no
 * STDL_Init, no libstdl.a.
 *
 * It takes the machine the way such a program does: supervisor mode,
 * its own VBL handler on $70 (TOS's is not called while it runs, so
 * the handler counts _frclock itself), its own colours, and three
 * pages of its own, flipped from that VBL. The borders hear of each
 * flip twice: PageNext in the VBL that writes the page to the video
 * base, PageShown in the VBL after, when the page is on screen. With
 * three pages the drawing keeps a page ahead, so the two calls meet
 * in one VBL every frame, which is why PageShown comes first there.
 *
 * Every page has the same bands, white lines on the first and last
 * rows shown (1 and 273), and a white bar a column wide that moves a
 * column a frame. A flip the borders were not told about shows a
 * normal border for that frame and counts as a miss. After 300
 * frames the flips are let finish, the borders closed, and the lines,
 * flips, frames and misses printed (misses=0 when all is well).
 *
 * Nothing calls the system while a border is open: a trap there
 * holds interrupts off inside a frame whose timer interrupts must
 * land on given lines, so this program has no key to stop it early.
 * Built with src/overscan_host.c and nothing else of STDL's (the
 * Makefile's rule); mintlib is here for the start-up, Mxalloc and
 * printf only, as the host object needs no C library.
 */

#include <stdint.h>
#include <stdio.h>
#include <mint/osbind.h>
#include <stdl/stdl_overscan.h>
#define HOLD_NO_STDL
#include "hold.h"

#define PAGES   3
#define ROWS    274                     /* row 0 hidden, 1-273 shown */
#define WORDS   80                      /* words a row               */
#define GROUPS  20                      /* 16-pixel columns a row    */
#define PAGE    ((STDL_OVERSCAN_PAGE_BYTES + 255) & ~255)
#define FRAMES  300

#define VB_HI    (*(volatile uint8_t *)0xFFFF8201UL)
#define VB_MID   (*(volatile uint8_t *)0xFFFF8203UL)
#define PALETTE  ((volatile uint16_t *)0xFFFF8240UL)
#define FRCLOCK  (*(volatile uint32_t *)0x466UL)
#define VEC_VBL  (*(volatile uint32_t *)0x70UL)
#define COOKIES  (*(uint32_t *volatile *)0x5A0UL)

static const uint16_t colours[16] = {
    0x000, 0x700, 0x720, 0x740, 0x760, 0x770, 0x470, 0x070,
    0x074, 0x077, 0x047, 0x007, 0x407, 0x707, 0x704, 0x777
};

static uint8_t *page[PAGES];
static uint8_t band[ROWS];              /* each row's colour         */
static int8_t bar_at[PAGES];            /* each page's bar, or -1    */

/* the flips, shared with the VBL */
static uint8_t *volatile queued;        /* drawn, for the VBL        */
static uint8_t *volatile coming;        /* in the base, not shown    */
static uint8_t *volatile shown;         /* on screen                 */
static volatile uint16_t flips;

/* The VBL: a handler of its own, as a program that owns the machine
 * has, so it counts _frclock where TOS's handler would have. The open
 * puts STDL's prefix in front of it on $70, and the close takes it
 * away again. */
void ovh_vbl(void);
extern char ovh_vbl_entry[];
__asm__(
    "    .text\n"
    "    .even\n"
    "    .globl _ovh_vbl_entry\n"
    "_ovh_vbl_entry:\n"
    "    addq.l  #1,0x466.w\n"
    "    movem.l %d0-%d1/%a0-%a1,-(%sp)\n"
    "    jsr     _ovh_vbl\n"
    "    movem.l (%sp)+,%d0-%d1/%a0-%a1\n"
    "    rte\n");

void ovh_vbl(void)
{
    uint8_t *p;

    if (coming != NULL) {               /* on screen since this VBL */
        STDL_OverscanPageShown();
        shown = coming;
        coming = NULL;
        flips++;
    }
    p = queued;
    if (p != NULL) {                    /* on screen from the next  */
        VB_HI = (uint8_t)((uintptr_t)p >> 16);
        VB_MID = (uint8_t)((uintptr_t)p >> 8);
        STDL_OverscanPageNext(p);
        coming = p;
        queued = NULL;
    }
}

/* One 16-pixel column of a page, rows 1-273: the bands, or the white
 * bar. Row 0 stays as the open cleared it: it is fetched too, a word
 * out of step, on the line above the picture, so anything drawn there
 * shows as a smear of the wrong colours. */
static void put_column(uint8_t *p, int group, int white)
{
    uint16_t *w = (uint16_t *)(void *)(p + WORDS * 2 + group * 8);
    int r;

    for (r = 1; r < ROWS; r++, w += WORDS) {
        const unsigned c = white ? 15u : band[r];

        w[0] = (c & 1) ? 0xFFFF : 0;
        w[1] = (c & 2) ? 0xFFFF : 0;
        w[2] = (c & 4) ? 0xFFFF : 0;
        w[3] = (c & 8) ? 0xFFFF : 0;
    }
}

static uint32_t mch_cookie(void)        /* supervisor only */
{
    uint32_t *c = COOKIES;

    if (c == NULL) {
        return 0;
    }
    for (; c[0] != 0; c += 2) {
        if (c[0] == 0x5F4D4348UL) {     /* "_MCH" */
            return c[1];
        }
    }
    return 0;
}

int main(void)
{
    STDL_OverscanHost host;
    uint16_t old_palette[16];
    uint32_t old_vbl, start = 0, frames = 0, misses = 0;
    const char *error = NULL;
    long mem, ssp;
    int lines = 0, i, n, at = 0, next = 1;

    hold_on_exit(NULL);

    /* the pages in ST RAM, which is all the Shifter can see */
    mem = Sversion() >= 0x1900 ? Mxalloc(PAGES * PAGE + 256L, 0)
                               : Malloc(PAGES * PAGE + 256L);
    if (mem <= 0) {
        printf("OVHOST: no ST RAM for %d pages\n", PAGES);
        return 1;
    }
    page[0] = (uint8_t *)(((uintptr_t)mem + 255) & ~(uintptr_t)255);
    for (i = 1; i < PAGES; i++) {
        page[i] = page[i - 1] + PAGE;
    }
    for (i = 1; i < ROWS; i++) {
        band[i] = (uint8_t)((i == 1 || i == ROWS - 1) ? 15
                            : 1 + (i - 2) / 20 % 14);
    }

    ssp = Super(0L);
    host.page = page[0];
    host.screen = Logbase();
    host.mch = mch_cookie();
    host.blitter = 0;
    for (i = 0; i < 16; i++) {
        old_palette[i] = PALETTE[i];
        PALETTE[i] = colours[i];
    }
    old_vbl = VEC_VBL;
    VEC_VBL = (uint32_t)(uintptr_t)ovh_vbl_entry;

    if (STDL_OverscanHostInit(&host) != 0) {
        error = STDL_GetError();
    } else {
        /* both: the second open combines them, 273 lines. The open
         * clears the page it shows, so the pages are drawn after */
        lines = STDL_OpenTopBorder() ? STDL_OpenBottomBorder() : 0;
        if (lines != STDL_OVERSCAN_BOTH_H) {
            error = STDL_GetError();
            STDL_CloseBottomBorder();
            STDL_CloseTopBorder();
        }
    }
    if (error == NULL) {
        shown = page[0];
        for (i = 0; i < PAGES; i++) {
            for (n = 0; n < GROUPS; n++) {
                put_column(page[i], n, 0);
            }
            bar_at[i] = -1;
        }
        start = FRCLOCK;
        for (n = 0; n < FRAMES; n++) {
            uint8_t *p = page[next];

            /* a page is free once it is neither on screen nor on its
             * way there: with three, the one shown two flips ago */
            while (queued != NULL || coming == p || shown == p) {
            }
            if (bar_at[next] >= 0) {
                put_column(p, bar_at[next], 0);
            }
            put_column(p, at, 1);
            bar_at[next] = (int8_t)at;
            queued = p;
            if (++at == GROUPS) {
                at = 0;
            }
            if (++next == PAGES) {
                next = 0;
            }
        }
        /* no flip may be in progress across a close */
        while (queued != NULL || coming != NULL) {
        }
        frames = FRCLOCK - start;
        misses = STDL_OverscanMisses();
        STDL_CloseBottomBorder();
        STDL_CloseTopBorder();
    }

    VEC_VBL = old_vbl;
    for (i = 0; i < 16; i++) {
        PALETTE[i] = old_palette[i];
    }
    Super(ssp);
    if (error != NULL) {
        printf("OVHOST: %s\n", error);
        return 1;
    }
    printf("OVHOST lines=%d flips=%u frames=%lu misses=%lu\n", lines,
           (unsigned)flips, (unsigned long)frames,
           (unsigned long)misses);
    return 0;
}
