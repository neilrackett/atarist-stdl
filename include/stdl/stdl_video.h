/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * Init, video mode, page flipping.
 */

#ifndef STDL_VIDEO_H
#define STDL_VIDEO_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdl/stdl_types.h>

/* values match SDL 1.2, like the surface flags and keysyms */
#define STDL_INIT_TIMER    0x00000001u
#define STDL_INIT_AUDIO    0x00000010u
#define STDL_INIT_VIDEO    0x00000020u
#define STDL_INIT_JOYSTICK 0x00000200u
#define STDL_INIT_EVERYTHING 0x0000FFFFu

int  STDL_Init(uint32_t flags);
void STDL_Quit(void);

/*
 * v1 always selects 320x200, 4 planes, 16 colours; w/h/bpp are
 * accepted for source compatibility and ignored.  Honoured flags:
 * STDL_DOUBLEBUF and STDL_TRIPLEBUF.  Returns the screen surface
 * (drawing target: the back buffer when double- or triple-buffered,
 * the live screen otherwise).
 *
 * STDL_TRIPLEBUF adds a third page (32K of ST RAM) and implies
 * STDL_DOUBLEBUF. Without the memory the mode is double-buffered,
 * STDL_GetError says why, and the surface's flags carry
 * STDL_DOUBLEBUF alone - test them for STDL_TRIPLEBUF to know. The
 * mode can be set again with or without it; the default palette
 * comes back each time.
 */
STDL_Surface *STDL_SetVideoMode(int w, int h, int bpp, uint32_t flags);
STDL_Surface *STDL_GetVideoSurface(void);

/*
 * Show the page drawn from the next VBL. Double-buffered, it then
 * waits for that VBL, after which the other page is off the screen
 * and free: a frame is rounded up to whole VBLs plus the wait, so one
 * of 34-40ms takes 40 at 50Hz, and the average frame pays half a VBL
 * idle. Triple-buffered, the next frame goes into the third page at
 * once, and the wait is only for the previous request to have been
 * taken: a frame longer than a VBL never waits (a VBL and a half
 * takes a VBL and a half: TRIPCHK, 40 such frames 60 VBLs against
 * 80), and one faster than the display is held to it, every frame
 * shown. Single-buffered, it waits for the VBL.
 */
void STDL_Flip(void);
void STDL_UpdateRects(int n, STDL_Rect *rects);
void STDL_WaitVBL(void);

/*
 * Machine information (valid after STDL_Init). A TT and a Falcon are
 * STE-class (is_ste: 4096 colours, DMA sound): they run an ST program
 * in ST Low, with the TT's and Falcon's own differences handled by the
 * library - the desktop's modes and colours put back at exit, the
 * display rate measured rather than assumed, a Falcon's sound routed
 * and its missing 6258Hz played at 12517. is_tt and is_falcon are for
 * a program that wants to do more there; has_dma_sound is what decides
 * the sound devices, from the _SND cookie where TOS has one (a TT's
 * DMA sound can be switched off), the machine type where it has not.
 */
typedef struct {
    uint8_t  is_ste;       /* STE-class shifter: 4096 colours          */
    uint8_t  is_megaste;
    uint8_t  has_blitter;  /* and the library uses it (not a Falcon's)  */
    uint8_t  is_tt;        /* _MCH 0x00020000                          */
    uint32_t mch_cookie;   /* raw _MCH value, 0 if absent              */
    uint8_t  is_falcon;    /* _MCH 0x00030000                          */
    uint8_t  has_dma_sound;
    uint16_t cpu;          /* _CPU: 0, 10, 20, 30, 40 or 60; 0 absent */
} STDL_MachineInfo;

const STDL_MachineInfo *STDL_GetMachineInfo(void);

/*
 * Large same-phase fills and blits go through the BLiTTER when the
 * hardware has one (detected at STDL_Init); this toggles the
 * acceleration at runtime - for benchmarking the CPU paths or
 * sidestepping a suspected blitter issue. 1 = allow (default),
 * 0 = force CPU, -1 = query. Returns the previous setting.
 */
int STDL_UseBlitter(int enable);

/*
 * The Mega STE's CPU speed and cache, which STDL_Init turns on by
 * default. A port that wants the machine left as the user set it,
 * or that wants to compare the two speeds inside one binary, says
 * so here:
 *
 *   0  leave the control alone - and, after STDL_Init, put back
 *      what the machine had when the library claimed it
 *   1  the library's default, currently 16MHz with the cache
 *   2  16MHz, cache off - worth far less than 3 for CPU pixel
 *      work, since the bus stays at 8MHz either way
 *   3  16MHz with the cache, the same as 1 today
 *  -1  query without changing anything
 *
 * Returns the previous setting. Values 2 and 3 are the two bits of
 * the hardware register (bit 0 cache, bit 1 clock); the other bits
 * of that byte are preserved. Call it before STDL_Init to stop the
 * switch happening at all, or after to change speed on the fly. On
 * anything that is not a Mega STE it records the setting and does
 * nothing else.
 *
 * Change speed with the borders closed. Opening one measures the
 * timing of its own code on the machine, and that measurement is
 * redone on the next open after a speed change - but a border
 * already open keeps the table it was given, which is wrong at the
 * other clock. See examples/overscan.c.
 *
 * Measured by BLITCHK.TOS on an emulated Mega STE, 50 full-screen
 * fills plus keyed blits: 2705ms of CPU work at the speed the user
 * had set, 2610ms at 16MHz without the cache, 1690ms with it. The
 * cache is what buys the CPU time; the clock alone buys 3%. The
 * blitter path moved more than expected there (1080ms to 615ms for
 * the clock alone), which wants confirming on real hardware before
 * anyone relies on it. Note also that Mega STE frame times shift
 * about 10% with unrelated code layout, so compare modes inside one
 * binary rather than between builds - which is what this call is
 * for.
 */
int STDL_UseMegaSteSpeedup(int mode);

/*
 * The display's sync rate: 50 or 60, or -1 to ask without
 * changing. Returns the rate now in effect.
 *
 * What TOS set at boot is the machine's region - PAL starts at 50,
 * NTSC at 60 - and this is how a port asks for the other. 60Hz
 * gives ten more frames a second and the same 320x200 picture; it
 * is worth having for a game already inside its frame budget, and
 * worth nothing for one that is not.
 *
 * Three things to know before using it.
 *
 * The VBL rate changes with it, so anything timed by counting
 * frames runs 20%% fast at 60Hz. STDL_Music and the STDL_Voice
 * sequencer tick and STDL_Sfx steps are rate-aware and compensate;
 * STDL_AddVBL callbacks are per-tick by definition and will not. Time from STDL_GetTicks
 * where it matters.
 *
 * Overscan is PAL-timed. While a border is open the display is
 * forced to 50Hz whatever was asked for, and the request takes
 * effect on the final close - so this returns 50 while a border is
 * open, and a port that wants both cannot have both.
 *
 * And 60Hz on a PAL machine is outside what some period TVs and
 * monitors will sync, though ST colour monitors take both. It is
 * opt-in for that reason, and the return value is what actually
 * happened, so a port can ask and then believe the answer rather
 * than the request.
 *
 * On a TT or a Falcon the rate is not the sync register's, and this
 * changes nothing: it measures the display's rate - frames against
 * the 200Hz timer, about a third of a second - and returns that,
 * which is what the rate-aware devices above then divide by (-1
 * returns the last measurement without taking another). The
 * library measures at STDL_Init and after each STDL_SetVideoMode; a
 * program that sets a TT's or a Falcon's mode itself calls this
 * with 0 afterwards. A Falcon on a VGA monitor runs at 60, where the 50 the
 * library once assumed played every sequencer 20% fast.
 */
int STDL_SetRefresh(int hz);

/*
 * Plane budget: how many of the four bitplanes STDL maintains.
 *
 * The screen is always four planes, but a game that only uses
 * colours 0-3 leaves planes 2 and 3 zero forever and every write
 * STDL makes to them is wasted bus time. Setting the budget to N is
 * a promise that no colour index >= 2^N will be drawn again; fills,
 * spans, blits, sprites, tiles, text, the XOR ops and the BLiTTER
 * paths then touch only planes 0..N-1 and move N/4 of the memory.
 *
 * N is 1..4 and defaults to 4, which is bit-for-bit the behaviour
 * of a build with no budget set. A negative argument only queries.
 * Returns the previous budget.
 *
 * Lowering the budget zeroes planes N..3 of the screen page(s) STDL
 * owns, which is what makes skipping those writes sound; it can be
 * called before or after STDL_SetVideoMode. Surfaces the program
 * allocates start zeroed, so they stay compliant as long as the
 * promise holds. Anything already holding colours >= 2^N when the
 * budget drops (a 16-colour picture loaded earlier, planar data
 * poked in by hand) keeps those high plane bits and will render
 * wrong: re-create or re-fill it.
 *
 * Colours themselves degrade predictably rather than corrupting:
 * every primitive behaves as if the colour index were masked to the
 * low N bits, so a stray colour 9 at budget 2 draws colour 1.
 * Palette entries are unaffected - all 16 registers stay settable
 * and are programmed as before, but only the first 2^N can appear
 * on screen (STDL_MapRGB will not return an index above that).
 */
int STDL_SetPlaneBudget(int planes);

const char *STDL_GetError(void);
void        STDL_SetError(const char *msg);

#ifdef __cplusplus
}
#endif

#endif /* STDL_VIDEO_H */
