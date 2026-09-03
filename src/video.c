/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * STDL_Video: init, machine detection, video mode, page flipping.
 */

#include <mint/osbind.h>
#include <stdlib.h>
#include <string.h>
#include "stdl_internal.h"

stdl_state_t stdl;
STDL_Surface stdl_screen;

void (*stdl_shutdown_audio)(void);
void (*stdl_shutdown_music)(void);
void (*stdl_shutdown_vbl)(void);
void (*stdl_shutdown_overscan)(void);
uint16_t (*stdl_blit_policy)(uint16_t nlines, uint32_t cpl);
void (*stdl_pal_apply_hook)(void);

static STDL_Palette screen_palette;
static STDL_PixelFormat screen_format;

static char error_buf[128] = "no error";

const char *STDL_GetError(void)
{
    return error_buf;
}

void STDL_SetError(const char *msg)
{
    strncpy(error_buf, msg, sizeof(error_buf) - 1);
    error_buf[sizeof(error_buf) - 1] = '\0';
}

const STDL_MachineInfo *STDL_GetMachineInfo(void)
{
    return &stdl.mach;
}

/* ---------------------------------------------------------------- */

static void detect_machine(void)
{
    long *jar = *(long **)0x5A0UL;

    stdl.mach.mch_cookie = 0;
    if (jar != NULL) {
        for (; jar[0] != 0; jar += 2) {
            if (jar[0] == 0x5F4D4348L) {          /* '_MCH' */
                stdl.mach.mch_cookie = (uint32_t)jar[1];
                break;
            }
        }
    }
    /* 0x00010000 STE, 0x00010010 Mega STE, TT/Falcon count as
     * STE-class for the palette (4 bits per channel) */
    stdl.mach.is_ste = (stdl.mach.mch_cookie >= 0x00010000UL);
    stdl.mach.is_megaste = (stdl.mach.mch_cookie == 0x00010010UL);
}

/* Only the Mega STE has the speed register at $FF8E21; touching it
 * elsewhere bus-errors. Returns previous setting or -1. */
static int megaste_speedup(void)
{
    if (stdl.mach.is_megaste) {
        volatile uint8_t *ctl = (volatile uint8_t *)0xFFFF8E21UL;
        uint8_t old = *ctl;
        *ctl = 3;                                 /* 16MHz, cache on */
        return old;
    }
    return -1;
}

/*
 * Leave supervisor mode. Super(old_ssp) via the library is only safe
 * at the exact stack depth of the matching Super(0), because the USP
 * still points at the stack as it was then; resuming user mode deep
 * in a call chain would continue on a stale stack and jump into
 * garbage on return. Point the USP at the live stack before the
 * mode switch so execution continues seamlessly.
 */
static void exit_supervisor(long old_ssp)
{
    __asm__ volatile(
        "move.l %0,-(%%sp)\n\t"
        "move.w #0x20,-(%%sp)\n\t"
        "move.l %%sp,%%a0\n\t"
        "move.l %%a0,%%usp\n\t"
        "trap   #1\n\t"
        "addq.l #6,%%sp"
        :
        : "g"(old_ssp)
        : "d0", "d1", "d2", "a0", "a1", "a2", "memory", "cc");
}

static void megaste_speedrestore(void)
{
    if (stdl.old_cpuspeed >= 0) {
        *(volatile uint8_t *)0xFFFF8E21UL = (uint8_t)stdl.old_cpuspeed;
        stdl.old_cpuspeed = -1;
    }
}

/* ---------------------------------------------------------------- */

/*
 * Everything that outlives the process if it is left installed:
 * interrupt vectors and hardware registers. No GEMDOS calls and no
 * free() - this also runs from the terminate vector below, where the
 * process is already being torn down and the heap is not ours.
 */
static void release_hardware(void)
{
    int i;

    if (stdl_shutdown_music != NULL) {
        stdl_shutdown_music();
        stdl_shutdown_music = NULL;
    }
    if (stdl_shutdown_audio != NULL) {
        stdl_shutdown_audio();
        stdl_shutdown_audio = NULL;
    }
    if (stdl_shutdown_vbl != NULL) {
        stdl_shutdown_vbl();
        stdl_shutdown_vbl = NULL;
    }
    if (stdl_shutdown_overscan != NULL) {
        /* border, timers and vectors back before the Setscreen
         * below repoints the display at page[0] */
        stdl_shutdown_overscan();
        stdl_shutdown_overscan = NULL;
    }
    stdl_events_remove();
    if (stdl.video_set) {
        for (i = 0; i < 16; i++) {
            (void)Setcolor(i, stdl.old_palette[i]);
        }
        (void)Setscreen(stdl.page[0], stdl.page[0], stdl.old_rez);
        stdl.video_set = 0;
    }
    megaste_speedrestore();
}

/*
 * GEMDOS terminate vector - etv_term at $0408, reached through
 * Setexc vector number 0x102. atexit is not enough: abort(), a
 * failed assert(), a signal handler and a TOS exception all reach
 * Pterm without running atexit handlers, and libcmini runs the ones
 * it does have in registration order rather than LIFO. Whatever the
 * route, GEMDOS is about to give this program's memory to someone
 * else while a VBL queue slot still points into it - which turns an
 * out-of-memory into a machine crash one frame later. (Measured:
 * Sopwith at 512K asserts on a failed calloc and EmuTOS then panics
 * with an illegal instruction taken from the level-4 autovector.)
 */
static void (*old_term)(void);

static void term_handler(void)
{
    (void)Setexc(0x102, (void *)old_term);
    if (stdl.initialised) {
        release_hardware();
        stdl.initialised = 0;
    }
    if (old_term != NULL) {
        old_term();
    }
}

static void restore_all(void)
{
    if (!stdl.initialised) {
        return;
    }
    (void)Setexc(0x102, (void *)old_term);
    release_hardware();
    free(stdl.page1_alloc);
    stdl.page1_alloc = NULL;
    (void)Cconws("\33e");                               /* cursor back on */
    if (stdl.old_ssp != 0) {
        exit_supervisor(stdl.old_ssp);
        stdl.old_ssp = 0;
    }
    stdl.initialised = 0;
}

int STDL_Init(uint32_t flags)
{
    (void)flags;
    if (stdl.initialised) {
        return 0;
    }
    memset(&stdl, 0, sizeof(stdl));

    /*
     * Supervisor mode from here on - low memory (the cookie jar
     * pointer at $5A0, the 200Hz counter at $4BA) bus-errors in user
     * mode. Super(0) is a *toggle*, not an idempotent "enter": called
     * when the caller is already supervisor it drops to USER mode,
     * and detect_machine() below then bus-errors on $5A0. Ports that
     * took supervisor themselves before reaching STDL are entitled to
     * a library that notices, so ask first and only claim the mode -
     * and the responsibility for giving it back - when it is ours.
     */
    if (Super(1L) == 0L) {
        stdl.old_ssp = (long)Super(0L);
    }
    detect_machine();
    stdl.old_cpuspeed = megaste_speedup();
    stdl_time_init();

    {
        long bm = Blitmode(-1);
        stdl.mach.has_blitter = (bm >= 0 && (bm & 2)) ? 1 : 0;
    }

    stdl.initialised = 1;
    old_term = (void (*)(void))Setexc(0x102, (void *)term_handler);
    atexit(restore_all);
    return 0;
}

void STDL_Quit(void)
{
    restore_all();
}

/* ---------------------------------------------------------------- */

STDL_Surface *STDL_SetVideoMode(int w, int h, int bpp, uint32_t flags)
{
    int i;
    (void)w; (void)h; (void)bpp;

    if (!stdl.initialised && STDL_Init(STDL_INIT_VIDEO) < 0) {
        return NULL;
    }

    if (!stdl.video_set) {
        stdl.old_rez = Getrez();
        if (stdl.old_rez == 2) {
            STDL_SetError("monochrome monitor: low resolution "
                          "unavailable (mono support is post-v1)");
            return NULL;
        }
        for (i = 0; i < 16; i++) {
            stdl.old_palette[i] = Setcolor(i, -1);
        }
        stdl.page[0] = (uint8_t *)Physbase();
        (void)Cconws("\33f\33E");                       /* cursor off, clear */
        if (stdl.old_rez != 0) {
            (void)Setscreen((void *)-1L, (void *)-1L, 0);
        }
        stdl.video_set = 1;
        stdl_events_install();
    }

    stdl.doublebuf = (flags & STDL_DOUBLEBUF) ? 1 : 0;
    if (stdl.doublebuf && stdl.page[1] == NULL) {
        /* 256-byte alignment satisfies the plain ST's screen base
         * granularity (STE only needs word alignment) */
        stdl.page1_alloc = malloc(STDL_SCREEN_BYTES + 256);
        if (stdl.page1_alloc == NULL) {
            STDL_SetError("out of memory for second screen page");
            stdl.doublebuf = 0;
        } else {
            stdl.page[1] = (uint8_t *)
                (((uintptr_t)stdl.page1_alloc + 255) & ~(uintptr_t)255);
            memset(stdl.page[1], 0, STDL_SCREEN_BYTES);
        }
    }

    /* default palette: colour 0 black, 15 white, the rest the ST
     * desktop-ish spread; ports normally set their own right away */
    for (i = 0; i < 16; i++) {
        stdl.colours[i].r = (uint8_t)((i & 1) ? 0xAA : 0) + ((i & 8) ? 0x55 : 0);
        stdl.colours[i].g = (uint8_t)((i & 2) ? 0xAA : 0) + ((i & 8) ? 0x55 : 0);
        stdl.colours[i].b = (uint8_t)((i & 4) ? 0xAA : 0) + ((i & 8) ? 0x55 : 0);
    }
    stdl.colours[0].r = stdl.colours[0].g = stdl.colours[0].b = 0;
    stdl.colours[15].r = stdl.colours[15].g = stdl.colours[15].b = 255;
    stdl_palette_apply_hw();

    screen_palette.ncolors = 16;
    screen_palette.colors = stdl.colours;
    screen_format.palette = &screen_palette;
    screen_format.BitsPerPixel = 4;
    screen_format.BytesPerPixel = 1;

    memset(&stdl_screen, 0, sizeof(stdl_screen));
    stdl_screen.w = STDL_SCREEN_W;
    stdl_screen.h = STDL_SCREEN_H;
    stdl_screen.stride = STDL_SCREEN_STRIDE;
    stdl_screen.planes = STDL_SCREEN_PLANES;
    stdl_screen.flags = STDL_SCREEN | STDL_HWSURFACE | STDL_FULLSCREEN
                      | STDL_HWPALETTE
                      | (stdl.doublebuf ? STDL_DOUBLEBUF : 0);
    stdl_screen.clip.x = 0;
    stdl_screen.clip.y = 0;
    stdl_screen.clip.w = STDL_SCREEN_W;
    stdl_screen.clip.h = STDL_SCREEN_H;
    stdl_screen.format = &screen_format;

    if (stdl.doublebuf) {
        stdl.backpage = 1;
        Setscreen(stdl.page[0], stdl.page[0], -1);
    } else {
        stdl.backpage = 0;
    }
    stdl_screen.pixels = stdl.page[stdl.backpage];

    /* a budget set before the mode (or before the back page was
     * allocated) still has to leave the high planes zeroed */
    stdl_planes_clear_screens();

    return &stdl_screen;
}

STDL_Surface *STDL_GetVideoSurface(void)
{
    return stdl.video_set ? &stdl_screen : NULL;
}

void STDL_WaitVBL(void)
{
    uint32_t fc = STDL_FRCLOCK;
    while (STDL_FRCLOCK == fc)
        ;
}

void STDL_Flip(void)
{
    if (stdl.doublebuf) {
        Setscreen((void *)-1L, stdl.page[stdl.backpage], -1);
        STDL_WaitVBL();
        stdl.backpage ^= 1;
        stdl_screen.pixels = stdl.page[stdl.backpage];
    } else {
        STDL_WaitVBL();
    }
}

void STDL_UpdateRects(int n, STDL_Rect *rects)
{
    /* single-buffered rendering goes straight to screen RAM */
    (void)n; (void)rects;
}
