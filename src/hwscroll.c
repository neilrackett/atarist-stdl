/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * STE hardware scrolling: LINEWIDTH, HSCROLL and the word-granular
 * video base, programmed from the vertical blank.
 *
 * The one thing this module knows that a port would otherwise learn
 * the hard way is the order the Shifter reads its registers in. The
 * video base ($FF8201/03/0D) is latched into the video counter three
 * lines before the end of the frame (line 310 at 50Hz, 260 at 60Hz),
 * which is *before* the VBL interrupt at the top of the next frame;
 * a base written from a VBL callback is therefore fetched from the
 * frame after the one just starting. LINEWIDTH ($FF820F) and HSCROLL
 * ($FF8265) are not latched: written in the same callback they shape
 * the frame that is just starting - the one still fetched from the
 * previous base. Write all three at once and every frame in which
 * HSCROLL crosses zero shows a base with the wrong line offset: the
 * picture jumps a group sideways for one frame.
 *
 * So each request is applied in two halves: its base at the VBL
 * where it is picked up, its offsets at the VBL after. The offsets
 * written at any VBL are those of the request whose base went in at
 * the previous one, and the pair on screen is always a matching pair.
 * The cost is a frame of latency, which a scrolling game has anyway.
 *
 * Its own translation unit, so a program that never scrolls does
 * not link it (there is no section garbage collection on this
 * toolchain: the linker's granularity is the object).
 */

#include "stdl_internal.h"

#ifdef __m68k__
#define VID_BASE_HI   (*(volatile uint8_t *)0xFFFF8201UL)
#define VID_BASE_MID  (*(volatile uint8_t *)0xFFFF8203UL)
#define VID_BASE_LO   (*(volatile uint8_t *)0xFFFF820DUL)
#define VID_LINEWIDTH (*(volatile uint8_t *)0xFFFF820FUL)
#define VID_HSCROLL   (*(volatile uint8_t *)0xFFFF8265UL)
#else
/* host builds: the registers are plain bytes, so the sequencing
 * below can be exercised natively if a test ever wants to */
static volatile uint8_t host_vid[5];
#define VID_BASE_HI   host_vid[0]
#define VID_BASE_MID  host_vid[1]
#define VID_BASE_LO   host_vid[2]
#define VID_LINEWIDTH host_vid[3]
#define VID_HSCROLL   host_vid[4]
#endif

/* the plain STE and the Mega STE: the TT and Falcon read as STE-class
 * for the palette but have their own video chips */
#define MCH_STE     0x00010000UL
#define MCH_MEGASTE 0x00010010UL

/* the latest request, written by the program with interrupts off */
static volatile uint32_t req_base;
static volatile uint8_t  req_lw;
static volatile uint8_t  req_hs;
static volatile uint8_t  req_seq;

/* owned by the VBL callback: the offsets belonging to the base that
 * was written last, and the sequence numbers of what has reached
 * the hardware */
static uint8_t pend_lw, pend_hs, pend_seq;
static volatile uint8_t done_seq;

static int hws_installed;

static void hws_vbl(void)
{
    uint32_t b;

    /* the offsets of the base the Shifter will fetch this frame */
    VID_LINEWIDTH = pend_lw;
    VID_HSCROLL = pend_hs;
    done_seq = pend_seq;

    /* the newest base, latched three lines before the next VBL; its
     * offsets wait for that VBL. The program updates the three
     * fields with interrupts masked, so this snapshot is consistent. */
    b = req_base;
    VID_BASE_HI = (uint8_t)(b >> 16);
    VID_BASE_MID = (uint8_t)(b >> 8);
    VID_BASE_LO = (uint8_t)b;
    pend_lw = req_lw;
    pend_hs = req_hs;
    pend_seq = req_seq;
}

/* Hardware and vectors only: this also runs from the terminate
 * vector, where GEMDOS and the heap are off limits. */
static void hws_release(void)
{
    uint32_t b;

    STDL_RemoveVBL(hws_vbl);
    hws_installed = 0;
    VID_LINEWIDTH = 0;
    VID_HSCROLL = 0;
    b = (uint32_t)(uintptr_t)stdl.page[0];
    VID_BASE_HI = (uint8_t)(b >> 16);
    VID_BASE_MID = (uint8_t)(b >> 8);
    VID_BASE_LO = 0;
    done_seq = req_seq;
}

int STDL_HasHwScroll(void)
{
    if (!stdl.initialised && STDL_Init(0) < 0) {
        return 0;
    }
    return stdl.mach.mch_cookie == MCH_STE
        || stdl.mach.mch_cookie == MCH_MEGASTE;
}

int STDL_SetScrollWindow(const void *base, int stride, int xfine)
{
    uint16_t sr;
    int lw;

    if (!STDL_HasHwScroll()) {
        STDL_SetError("hardware scrolling needs an STE Shifter");
        return -1;
    }
    if (!stdl.video_set) {
        STDL_SetError("no video mode for hardware scrolling");
        return -1;
    }
    /* the Shifter assigns planes by fetch order from the base, so any
     * word address is a valid group start; only the low bit is fixed */
    if (base == NULL || ((uintptr_t)base & 1) != 0) {
        STDL_SetError("scroll window base must be word aligned");
        return -1;
    }
    if (stride < 160 || stride > 670 || (stride & 7) != 0) {
        STDL_SetError("scroll window stride must be a multiple of 8 "
                      "from 160 to 670");
        return -1;
    }
    xfine &= 15;
    /* while HSCROLL is non-zero the Shifter fetches one extra group
     * (8 bytes in four planes) per line, which LINEWIDTH gives back */
    lw = (stride - 160) / 2 - (xfine != 0 ? 4 : 0);
    if (lw < 0) {
        STDL_SetError("fine scrolling needs a stride of at least 168");
        return -1;
    }

    if (!hws_installed) {
        /* seed the pending offsets with the stock values, so the
         * first VBL writes nothing the Shifter did not already have */
        pend_lw = 0;
        pend_hs = 0;
        pend_seq = req_seq;
        done_seq = req_seq;
        if (STDL_AddVBL(hws_vbl) < 0) {
            return -1;
        }
        hws_installed = 1;
        stdl_shutdown_hwscroll = hws_release;
    }

    sr = stdl_int_off();
    req_base = (uint32_t)(uintptr_t)base;
    req_lw = (uint8_t)lw;
    req_hs = (uint8_t)xfine;
    req_seq++;
    stdl_int_restore(sr);
    return 0;
}

int STDL_SetScrollOrigin(const STDL_Surface *s, int x, int y)
{
    const uint8_t *base;

    if (s == NULL || s->pixels == NULL) {
        STDL_SetError("null surface for scroll origin");
        return -1;
    }
    /* y * stride is a rare multiply, once per frame at most */
    base = s->pixels + (int32_t)y * s->stride + (x >> 4) * 8;
    return STDL_SetScrollWindow(base, s->stride, x & 15);
}

int STDL_ScrollWindowPending(void)
{
    return hws_installed && req_seq != done_seq;
}

void STDL_ResetScrollWindow(void)
{
    if (hws_installed) {
        hws_release();
        stdl_shutdown_hwscroll = NULL;
    }
}
