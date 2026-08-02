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

const char *STDL_GetError(void);
void        STDL_SetError(const char *msg);

#endif /* STDL_VIDEO_H */
