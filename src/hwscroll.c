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
 * So each request is applied in two halves: its base first, its
 * offsets at the VBL that finds the video counter reloaded from that
 * base. The base goes in straight away when the request arrives early
 * enough in the frame (before the reload), otherwise at the VBL; the
 * counter comparison makes the two paths one, and the pair on screen
 * is always a matching pair. A request made early in a frame is on
 * screen from the next; a late one costs a frame more.
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
typedef struct {
    uint32_t base;
    uint8_t  lw, hs, seq;
} hws_req_t;

/* the latest request, written by the program with interrupts off */
static volatile hws_req_t req;
/* the request whose base has been written to the registers and whose
 * offsets are due at the VBL that finds it latched */
static volatile hws_req_t armed;
/* sequence number of the request fully on the hardware */
static volatile uint8_t done_seq;
/* 200Hz stamp of the last VBL, so a request can tell whether it comes
 * early enough in the frame to write its base itself */
static volatile uint32_t vbl_stamp;

static int hws_installed;

static void write_base(uint32_t b)
{
    VID_BASE_HI = (uint8_t)(b >> 16);
    VID_BASE_MID = (uint8_t)(b >> 8);
    VID_BASE_LO = (uint8_t)b;
}

static uint32_t read_counter(void)
{
    return ((uint32_t)STDL_VC_HI << 16) | ((uint32_t)STDL_VC_MID << 8)
         | STDL_VC_LO;
}

/*
 * The VBL runs in the blanking, after the video counter was reloaded
 * from the base three lines earlier: the counter therefore names the
 * base this frame will fetch. If it is the armed request's, that
 * request's offsets go in now and it is complete. Then, if a newer
 * request is waiting, its base is written for the next frame.
 */
static void hws_vbl(void)
{
    vbl_stamp = STDL_HZ200;
    /* done_seq first: once the armed request has landed its offsets
     * are already in the Shifter, and every later frame with the
     * same base would otherwise re-read the counter and rewrite two
     * registers for nothing. */
    if (done_seq != armed.seq && read_counter() == armed.base) {
        VID_LINEWIDTH = armed.lw;
        VID_HSCROLL = armed.hs;
        done_seq = armed.seq;
    }
    if (req.seq != armed.seq) {
        write_base(req.base);
        armed = req;
    }
}

/* Hardware and vectors only: this also runs from the terminate
 * vector, where GEMDOS and the heap are off limits. */
static void hws_release(void)
{
    STDL_RemoveVBL(hws_vbl);
    hws_installed = 0;
    VID_LINEWIDTH = 0;
    VID_HSCROLL = 0;
    write_base((uint32_t)(uintptr_t)stdl.page[0] & ~0xFFUL);
}

int STDL_HasHwScroll(void)
{
    if (!stdl.initialised && STDL_Init(0) < 0) {
        return 0;
    }
    /* any STE-class Shifter, which is what LINEWIDTH and HSCROLL
     * belong to - the same test overscan.c makes, rather than a
     * second copy of the cookie map. TT and Falcon are 0x0002xxxx
     * and above and do not have these registers. */
    return stdl.mach.is_ste && stdl.mach.mch_cookie < 0x00020000UL;
}

int STDL_SetScrollWindow(const void *base, int stride, int xfine)
{
    uint16_t sr;
    int lw, fresh = 0;

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
    lw = ((stride - 160) >> 1) - (xfine != 0 ? 4 : 0);
    if (lw < 0) {
        STDL_SetError("fine scrolling needs a stride of at least 168");
        return -1;
    }

    if (!hws_installed) {
        /* A base the counter can never hold, so a re-install after
         * STDL_ResetScrollWindow cannot match the previous
         * session's armed request and apply its offsets. */
        armed.base = 0xFFFFFFFFUL;
        fresh = 1;
        armed.seq = req.seq;
        done_seq = req.seq;
        vbl_stamp = STDL_HZ200;
        if (STDL_AddVBL(hws_vbl) < 0) {
            return -1;
        }
        hws_installed = 1;
        stdl_shutdown_hwscroll = hws_release;
    }

    sr = stdl_int_off();
    /* A game calls this once a frame whether or not the view moved,
     * and a repeat of the request already on the hardware would cost
     * three register writes and a frame of "pending" for nothing.
     * Not on the frame the module was installed: the registers were
     * reset then, so the same values still have to be written. */
    if (!fresh && req.base == (uint32_t)(uintptr_t)base
        && req.lw == (uint8_t)lw && req.hs == (uint8_t)xfine) {
        stdl_int_restore(sr);
        return 0;
    }
    req.base = (uint32_t)(uintptr_t)base;
    req.lw = (uint8_t)lw;
    req.hs = (uint8_t)xfine;
    req.seq++;
    /*
     * The base register is only read when the counter reloads, three
     * lines before the VBL - about 19.8ms after the previous one. A
     * request that arrives within the first three 200Hz ticks of the
     * frame (at most 15ms in) can therefore write its base now and be
     * on screen from the next frame, its offsets following at that
     * VBL; a later one waits for the VBL to arm it, costing a frame.
     */
    if (STDL_HZ200 - vbl_stamp <= 2) {
        write_base(req.base);
        armed = req;
    }
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
    if (x < 0 || y < 0) {
        STDL_SetError("scroll origin must not be negative");
        return -1;
    }
    /* Both operands 16-bit so gcc 4.6 emits one mulu.w: as a long
     * multiply this is a __mulsi3 call, ~200 cycles on a path a game
     * takes every frame. The group offset is (x / 16) * 8 without
     * the two variable shifts. */
    base = s->pixels + (uint32_t)(uint16_t)y * s->stride
         + ((x >> 1) & ~7);
    return STDL_SetScrollWindow(base, s->stride, x & 15);
}

int STDL_ScrollWindowPending(void)
{
    return hws_installed && req.seq != done_seq;
}

void STDL_ResetScrollWindow(void)
{
    if (hws_installed) {
        hws_release();
        stdl_shutdown_hwscroll = NULL;
    }
}
