/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * Init, video mode, page flipping.
 */

#ifndef STDL_VIDEO_H
#define STDL_VIDEO_H

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
 * STDL_DOUBLEBUF.  Returns the screen surface (drawing target: the
 * back buffer when double-buffered, the live screen otherwise).
 */
STDL_Surface *STDL_SetVideoMode(int w, int h, int bpp, uint32_t flags);
STDL_Surface *STDL_GetVideoSurface(void);

void STDL_Flip(void);                          /* VBL-synced page flip */
void STDL_UpdateRects(int n, STDL_Rect *rects);
void STDL_WaitVBL(void);

/* Machine information (valid after STDL_Init) */
typedef struct {
    uint8_t  is_ste;       /* STE-class shifter: 4096 colours          */
    uint8_t  is_megaste;
    uint8_t  has_blitter;
    uint8_t  unused;
    uint32_t mch_cookie;   /* raw _MCH value, 0 if absent              */
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

#endif /* STDL_VIDEO_H */
