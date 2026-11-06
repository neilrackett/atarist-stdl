/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * Border overscan for a program that runs the machine itself: one
 * object, compiled from this file alone and linked instead of
 * libstdl.a (never with it - it is the whole of STDL such a program
 * has). "A program that runs the machine itself" in stdl_overscan.h
 * is the contract.
 *
 * overscan.c reaches into the rest of the library for a few things:
 * its state, its ST RAM allocator, palette staging, the error string
 * and a wait for the blank. Here they are what such a program needs:
 * the page is the program's and nothing is allocated, the colours
 * are the program's and nothing is staged, and the wait watches
 * _frclock, which the program's VBL counts. overscan.c is included
 * rather than linked so that the page calls below can keep its own
 * idea of the page on screen, a static, in step with the program's
 * flips.
 *
 * One port did this for itself first, compiling overscan.c beside
 * its own copy of these definitions and writing the ISRs' page bytes
 * from its VBL, and a release that gave overscan.c a new hook broke
 * it with nothing in STDL's build to say so. The Makefile now builds
 * this object and fails if it needs anything but libgcc's
 * arithmetic, so a new hook breaks STDL's build instead.
 */

#ifndef __m68k__
#error "overscan_host.c is the ST's: build it with m68k-atari-mint-gcc"
#endif

#include <stdint.h>

/* no C library: a page is cleared here, a long at a time */
static void ovsc_host_clear(void *p);
#define OVSC_CLEAR(p)     ovsc_host_clear(p)

/* the program sets a Mega STE's speed in the register itself, so the
 * calibration is keyed on what the register says now */
static int ovsc_host_speed(void);
#define OVSC_SPEED_KEY()  ovsc_host_speed()

#include "overscan.c"

/* the struct's layout is a contract with callers in assembly
 * (stdl_overscan.h): this does not compile if it moves */
typedef char ovsc_host_layout[(offsetof(STDL_OverscanHost, page) == 0
    && offsetof(STDL_OverscanHost, screen) == 4
    && offsetof(STDL_OverscanHost, mch) == 8
    && offsetof(STDL_OverscanHost, blitter) == 12
    && sizeof(STDL_OverscanHost) == 14) ? 1 : -1];

/* what overscan.c expects of the rest of the library */
stdl_state_t stdl;
STDL_Surface stdl_screen;
void (*stdl_shutdown_overscan)(void);
int (*stdl_ovsc_flip)(void);
uint16_t (*stdl_blit_policy)(uint16_t nlines, uint32_t cpl);
uint8_t stdl_vbl_hz = 50;
int stdl_sync_want = -1;
uint8_t stdl_timerb_owner;

static const char *host_error = "no error";
static uint8_t *host_page;      /* the page the first border opens on */
static uint8_t *host_next;      /* the page the last PageNext named   */

/* every message overscan.c sets is a string literal */
void STDL_SetError(const char *msg)
{
    host_error = msg;
}

const char *STDL_GetError(void)
{
    return host_error;
}

void STDL_WaitVBL(void)
{
    const uint32_t fc = STDL_FRCLOCK;

    while (STDL_FRCLOCK == fc) {
    }
}

/* the program's page, every time: STDL_OverscanHostInit leaves
 * stdl.doublebuf 0, so overscan.c never asks for a second one */
void *stdl_stram_alloc(uint32_t bytes)
{
    (void)bytes;
    return host_page;
}

void stdl_stram_free(void *p)
{
    (void)p;
}

void stdl_palette_apply_hw(void)
{
}

int stdl_pal_stage_on(int who)
{
    (void)who;
    return 0;
}

void stdl_pal_stage_off(int who)
{
    (void)who;
}

void stdl_page0_shown(void)
{
}

static void ovsc_host_clear(void *p)
{
    uint32_t *l = (uint32_t *)p;
    int n;

    for (n = MAX_FETCH_BYTES / 16; n > 0; n--) {
        l[0] = 0;
        l[1] = 0;
        l[2] = 0;
        l[3] = 0;
        l += 4;
    }
}

static int ovsc_host_speed(void)
{
    return stdl.mach.mch_cookie == 0x00010010UL ? (STDL_MSTE_CTL & 3) : 0;
}

/* About two seconds on a plain ST, as ovsc_release's own bound: a VBL
 * that does not count _frclock would hang the open's first wait. */
#define HOST_VBL_SPIN 400000L

int STDL_OverscanHostInit(const STDL_OverscanHost *host)
{
    uint32_t fc;
    long i;

    if (mode != 0) {
        STDL_SetError("a border is open");
        return -1;
    }
    if (host == NULL || host->page == NULL
        || ((uintptr_t)host->page & 255) != 0) {
        STDL_SetError("the overscan page must be 256-byte aligned");
        return -1;
    }
    fc = STDL_FRCLOCK;
    for (i = 0; i < HOST_VBL_SPIN && STDL_FRCLOCK == fc; i++) {
    }
    if (STDL_FRCLOCK == fc) {
        STDL_SetError("_frclock is not counting: the VBL must add 1 "
                      "to $466 every frame");
        return -1;
    }
    host_page = (uint8_t *)host->page;
    host_next = NULL;
    stdl.initialised = 1;
    stdl.video_set = 1;
    stdl.mach.mch_cookie = host->mch;
    stdl.mach.has_blitter = host->blitter != 0;
    stdl.page[0] = (uint8_t *)host->screen;
    stdl.doublebuf = 0;
    stdl.old_sync = -1;
    return 0;
}

/*
 * The top ISR accepts either of two page bases, a pair of bytes each,
 * because the counter reads one page on the frame a flip takes effect
 * while the program's state already says the other. With the borders
 * open the pair the counter is on is never the one written - the
 * second while the old page is still shown, the first once the new
 * one is - so an ISR that lands between two stores still matches.
 */
void STDL_OverscanPageNext(const void *page)
{
    const uintptr_t p = (uintptr_t)page;

    if (mode == 0) {
        return;
    }
    host_next = (uint8_t *)p;
    stdl_ovsc_bhi2 = (uint8_t)(p >> 16);
    stdl_ovsc_bmid2 = (uint8_t)(p >> 8);
}

void STDL_OverscanPageShown(void)
{
    uint8_t *p = host_next;

    if (mode == 0 || p == NULL) {
        return;
    }
    stdl_ovsc_bhi = stdl_ovsc_bhi2;
    stdl_ovsc_bmid = stdl_ovsc_bmid2;
    /* the bottom ISR polls for the counter at frame line 262, the
     * page's row 228 with the top border open and 199 without */
    stdl_ovsc_l262mid = (uint8_t)(((uintptr_t)p
        + ((mode & MODE_TOP) ? 228u * STDL_SCREEN_STRIDE
                             : 199u * STDL_SCREEN_STRIDE)) >> 8);
    buf = p;
    buf_base = p;
    draw = p;
    host_next = NULL;
}

void STDL_OverscanRelease(void)
{
    if (mode == 0) {
        return;
    }
    ovsc_release();
    stdl_ovsc_flip = NULL;
    buf_alloc = NULL;
    buf = NULL;
    buf_base = NULL;
    draw = NULL;
    host_next = NULL;
}
