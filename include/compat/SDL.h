/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * SDL 1.2 compatibility shim: maps the common SDL surface of a
 * ported game onto STDL so the source stays recognisable. Anything
 * not mapped here is a compile error by design - the porter should
 * find the gaps at build time, not at runtime.
 */

#ifndef STDL_COMPAT_SDL_H
#define STDL_COMPAT_SDL_H

#ifdef STDL_SDL1_SDL_H
#error "include/compat is STDL's planar SDL 1.2 shim: the SDL1 add-on (addons/sdl1) has an SDL.h of its own, and a program uses one of the two"
#endif

#ifdef __cplusplus
extern "C" {
#endif

#include "SDL_shared.h"

/* a surface is planar: STDL's own */
typedef STDL_Surface     SDL_Surface;

/* ---------------------------------------------------------------- */
/* video                                                            */

#define SDL_SetVideoMode       STDL_SetVideoMode
#define SDL_GetVideoSurface    STDL_GetVideoSurface
#define SDL_UpdateRects(s, n, r) STDL_UpdateRects((n), (r))
/* nothing to do on an ST, where the screen is the picture; a TT's or a
 * Falcon's 8-plane screen (stdl_responsive.h) hears what changed, all
 * of it for a rectangle of 0s, as SDL's */
static __inline__ void SDL_UpdateRect(SDL_Surface *s, Sint32 x, Sint32 y,
                                      Uint32 w, Uint32 h)
{
    STDL_Rect r;

    (void)s;
    if ((w | h) == 0) {
        STDL_UpdateRects(0, (STDL_Rect *)0);
        return;
    }
    r.x = (int16_t)x;
    r.y = (int16_t)y;
    r.w = (uint16_t)w;
    r.h = (uint16_t)h;
    STDL_UpdateRects(1, &r);
}

#define SDL_FreeSurface        STDL_FreeSurface
#define SDL_BlitSurface        STDL_BlitSurface
#define SDL_SetColors(s, c, f, n) STDL_SetColours((s), (c), (f), (n))

#define SDL_PollEvent          STDL_PollEvent
#define SDL_WaitEvent          STDL_WaitEvent
#define SDL_GetMouseState      STDL_GetMouseState
#define SDL_WarpMouse(x, y)    STDL_WarpMouse((uint16_t)(x), (uint16_t)(y))

/* software cursor (see stdl_cursor.h for the save-under caveats) */
typedef STDL_Cursor SDL_Cursor;
#define SDL_CreateCursor(data, mask, w, h, hx, hy) \
        STDL_CreateCursor((data), (mask), (w), (h), (hx), (hy))
#define SDL_SetCursor  STDL_SetCursor
#define SDL_GetCursor  STDL_GetCursor
#define SDL_FreeCursor STDL_FreeCursor
#define SDL_ShowCursor STDL_ShowCursorCtl

/* documented non-goals; fail cleanly (see docs/limits.md) */
int SDL_SetAlpha(SDL_Surface *surface, Uint32 flags, Uint8 alpha);
int SDL_SaveBMP(SDL_Surface *surface, const char *file);

#define SDL_LoadBMP STDL_LoadBMP

int  SDL_Flip(SDL_Surface *screen);
int  SDL_FillRect(SDL_Surface *dst, SDL_Rect *dstrect, Uint32 color);
SDL_Surface *SDL_CreateRGBSurface(Uint32 flags, int width, int height,
                                  int depth, Uint32 Rmask, Uint32 Gmask,
                                  Uint32 Bmask, Uint32 Amask);
SDL_Surface *SDL_DisplayFormat(SDL_Surface *surface);
int  SDL_SetColorKey(SDL_Surface *surface, Uint32 flag, Uint32 key);
int  SDL_SetPalette(SDL_Surface *surface, int flags, SDL_Color *colors,
                    int firstcolor, int ncolors);
const SDL_VideoInfo *SDL_GetVideoInfo(void);
SDL_Rect **SDL_ListModes(SDL_PixelFormat *format, Uint32 flags);
int  SDL_VideoModeOK(int width, int height, int bpp, Uint32 flags);
char *SDL_VideoDriverName(char *namebuf, int maxlen);

#ifdef __cplusplus
}
#endif

#endif /* STDL_COMPAT_SDL_H */
