/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * The SDL1 add-on, experimental (docs/sdl1.md): SDL 1.2's picture as
 * SDL 1.2 programs draw it, a byte a pixel through a palette of 256,
 * for getting an SDL 1.2 program running on every machine before it is
 * ported to planes. A screen surface's pixels are the program's to
 * write; SDL_UpdateRects and SDL_Flip hand them to STDL's SDL2 add-on
 * (its experimental 68000 build), which converts what changed: 256
 * colours on a TT or a Falcon, 16 on an ST. It is for prototyping, not
 * for a port to ship on an ST: the planar shim (include/compat) is.
 *
 * Everything that is no picture - events and keys, the joystick,
 * timers, audio, the mixer - is the planar shim's own
 * (compat/SDL_shared.h), so that half of a program moves to it
 * unchanged. A program asks for the add-on by name: this directory on
 * its include path in place of include/compat, and libstdl-sdl1.a and
 * libstdl-sdl2-st.a ahead of libstdl.a on its link.
 *
 * Every video name is a macro onto a stdl_sdl1_ link name, as
 * libstdl.a's own SDL_Flip, SDL_FillRect and the rest are the planar
 * shim's. A call not declared here is a compile error by design.
 */

#ifndef STDL_SDL1_SDL_H
#define STDL_SDL1_SDL_H

#if defined(STDL_COMPAT_SDL_H) || defined(STDL_SDL2_SDL_H)
#error "the SDL1 add-on's SDL.h has met another SDL.h of STDL's (include/compat, or the SDL2 add-on's): a program uses one"
#endif

#ifdef __cplusplus
extern "C" {
#endif

#include <compat/SDL_shared.h>

/* ---------------------------------------------------------------- */
/* a surface: 8 bits a pixel, SDL 1.2's fields                      */

/*
 * -DSTDL_SDL1_STRICT renames the pixels, so that every line of a
 * program that reads or writes them itself stops the build: those are
 * the lines a planar port has to rewrite (docs/porting.md, "Direct
 * pixel access"), as a planar surface's pixels are plane words.
 */
#ifdef STDL_SDL1_STRICT
#define STDL_SDL1_PIXELS stdl_sdl1_chunky_pixels
#else
#define STDL_SDL1_PIXELS pixels
#endif

typedef struct SDL_Surface {
    Uint32 flags;
    SDL_PixelFormat *format;    /* BitsPerPixel 8, its palette 256 */
    int w, h;
    Uint16 pitch;
    void *STDL_SDL1_PIXELS;
    int offset;
    void *hwdata;
    SDL_Rect clip_rect;
    Uint32 unused1;
    Uint32 locked;
    void *map;
    unsigned int format_version;
    int refcount;
} SDL_Surface;

/* ---------------------------------------------------------------- */
/* video                                                            */

#define SDL_SetVideoMode        stdl_sdl1_SetVideoMode
#define SDL_GetVideoSurface     stdl_sdl1_GetVideoSurface
#define SDL_UpdateRects         stdl_sdl1_UpdateRects
#define SDL_UpdateRect          stdl_sdl1_UpdateRect
#define SDL_Flip                stdl_sdl1_Flip
#define SDL_CreateRGBSurface    stdl_sdl1_CreateRGBSurface
#define SDL_CreateRGBSurfaceFrom stdl_sdl1_CreateRGBSurfaceFrom
#define SDL_FreeSurface         stdl_sdl1_FreeSurface
#define SDL_DisplayFormat       stdl_sdl1_DisplayFormat
#define SDL_LoadBMP             stdl_sdl1_LoadBMP
#define SDL_UpperBlit           stdl_sdl1_UpperBlit
#define SDL_BlitSurface         stdl_sdl1_UpperBlit
#define SDL_FillRect            stdl_sdl1_FillRect
#define SDL_SetColorKey         stdl_sdl1_SetColorKey
#define SDL_SetClipRect         stdl_sdl1_SetClipRect
#define SDL_GetClipRect         stdl_sdl1_GetClipRect
#define SDL_SetColors           stdl_sdl1_SetColors
#define SDL_SetPalette          stdl_sdl1_SetPalette
#define SDL_GetVideoInfo        stdl_sdl1_GetVideoInfo
#define SDL_ListModes           stdl_sdl1_ListModes
#define SDL_VideoModeOK         stdl_sdl1_VideoModeOK
#define SDL_VideoDriverName     stdl_sdl1_VideoDriverName
#define SDL_PollEvent           stdl_sdl1_PollEvent
#define SDL_WaitEvent           stdl_sdl1_WaitEvent
#define SDL_GetMouseState       stdl_sdl1_GetMouseState
#define SDL_WarpMouse           stdl_sdl1_WarpMouse

/*
 * The screen: at most 320x240, 8 bits a pixel, centred; on a TT or a
 * Falcon over the SDL2 add-on's experimental build (libstdl-sdl2-st.a)
 * up to 640x480, the first call's width over 320 choosing STDL's
 * 640x480 in 16 colours (docs/sdl1.md). SDL_VideoModeOK and
 * SDL_ListModes say which. bpp 0 or 8 is 8; another depth is 8 with
 * SDL_ANYFORMAT and refused without it, as there is no other. Set
 * again, it is a new surface of the new size on the same screen (on an
 * ST the first call's height chose ST Low or the tall screen:
 * docs/sdl2.md). NULL with the error set.
 */
SDL_Surface *SDL_SetVideoMode(int width, int height, int bpp, Uint32 flags);
SDL_Surface *SDL_GetVideoSurface(void);

/*
 * What the screen surface's rectangles now hold goes to the display:
 * each widened to the 16-pixel groups it touches, which the add-on
 * compares or converts whole, so a pixel written beside a rectangle
 * and not yet updated can show with it. A rectangle of 0s is the
 * whole screen, as SDL_Flip is; neither waits for the display.
 */
void SDL_UpdateRects(SDL_Surface *screen, int numrects, SDL_Rect *rects);
void SDL_UpdateRect(SDL_Surface *screen, Sint32 x, Sint32 y, Uint32 w,
                    Uint32 h);
int  SDL_Flip(SDL_Surface *screen);

/* depth 8 only (0 is 8); the masks are not read. A new surface's
 * palette is black, as SDL 1.2's. */
SDL_Surface *SDL_CreateRGBSurface(Uint32 flags, int width, int height,
                                  int depth, Uint32 Rmask, Uint32 Gmask,
                                  Uint32 Bmask, Uint32 Amask);
SDL_Surface *SDL_CreateRGBSurfaceFrom(void *pixels, int width, int height,
                                      int depth, int pitch, Uint32 Rmask,
                                      Uint32 Gmask, Uint32 Bmask,
                                      Uint32 Amask);
void SDL_FreeSurface(SDL_Surface *surface);
/* a copy in the screen's colours: each pixel, and the colour key, on
 * the nearest of the screen's palette as it is now */
SDL_Surface *SDL_DisplayFormat(SDL_Surface *surface);
/* uncompressed, 1, 4 or 8 bits a pixel, as an 8-bit surface with the
 * file's palette; the name is tried in upper case too */
SDL_Surface *SDL_LoadBMP(const char *file);

/*
 * A blit, keyed or not, clipped as SDL 1.2's: dstrect comes back as
 * what was drawn. Where the two surfaces' palettes differ each source
 * colour goes to the destination palette's nearest, as SDL 1.2's 8-bit
 * blits do; the map is made again when either palette changes (up to
 * 256 colours each against 256), so convert a picture once with
 * SDL_DisplayFormat, or give it the screen's palette, where the
 * screen's colours change often.
 */
int SDL_UpperBlit(SDL_Surface *src, SDL_Rect *srcrect, SDL_Surface *dst,
                  SDL_Rect *dstrect);
int SDL_FillRect(SDL_Surface *dst, SDL_Rect *dstrect, Uint32 color);
int SDL_SetColorKey(SDL_Surface *surface, Uint32 flag, Uint32 key);
SDL_bool SDL_SetClipRect(SDL_Surface *surface, const SDL_Rect *rect);
void SDL_GetClipRect(SDL_Surface *surface, SDL_Rect *rect);

/* The screen surface's colours are the display's, at once (a fade's
 * steps need no SDL_Flip); another surface's are its own record.
 * SDL_LOGPAL is the surface's palette, which SDL_MapRGB reads, and
 * SDL_PHYSPAL the display's. */
int SDL_SetColors(SDL_Surface *surface, SDL_Color *colors, int firstcolor,
                  int ncolors);
int SDL_SetPalette(SDL_Surface *surface, int flags, SDL_Color *colors,
                   int firstcolor, int ncolors);

const SDL_VideoInfo *SDL_GetVideoInfo(void);
SDL_Rect **SDL_ListModes(SDL_PixelFormat *format, Uint32 flags);
int  SDL_VideoModeOK(int width, int height, int bpp, Uint32 flags);
char *SDL_VideoDriverName(char *namebuf, int maxlen);

/* STDL's events, the mouse measured from the picture's top left */
int  SDL_PollEvent(SDL_Event *event);
int  SDL_WaitEvent(SDL_Event *event);
Uint8 SDL_GetMouseState(int *x, int *y);
void SDL_WarpMouse(Uint16 x, Uint16 y);

/* no cursor is drawn, and none can be made */
#define SDL_ShowCursor(toggle) ((void)(toggle), 0)

/* documented non-goals; fail cleanly (see docs/limits.md) */
#define SDL_SetAlpha(surface, flags, alpha) \
        ((void)(surface), (void)(flags), (void)(alpha), \
         STDL_SetError("alpha blending is a documented non-goal"), -1)
#define SDL_SaveBMP(surface, file) \
        ((void)(surface), (void)(file), \
         STDL_SetError("SDL_SaveBMP not implemented"), -1)

/* ---------------------------------------------------------------- */
/* on the way to planes                                             */

/*
 * STDL's two single-pixel calls (stdl_draw.h), answered here for this
 * add-on's surfaces: a line that read or wrote a surface's pixels,
 * rewritten onto them, then builds and runs on this add-on and on the
 * planar shim alike. So a program can be moved a line at a time under
 * STDL_SDL1_STRICT, running here at every step, and change its include
 * path and archives when the build is clean. They are slow on planes,
 * where a loop of them wants spans, fills or sprites instead
 * (docs/porting.md); a pixel outside the surface is not drawn, and
 * reads 0.
 */
#define STDL_PutPixel stdl_sdl1_PutPixel
#define STDL_GetPixel stdl_sdl1_GetPixel
void  STDL_PutPixel(SDL_Surface *dst, int x, int y, Uint8 col);
Uint8 STDL_GetPixel(const SDL_Surface *src, int x, int y);

/* ---------------------------------------------------------------- */
/* the add-on's own                                                 */

/*
 * The SDL2 add-on's hints (its SDL_stdl.h says what each does), set
 * before the first SDL_SetVideoMode: STDL_SDL1_SetHint(
 * STDL_SDL1_HINT_ST_TALL, "0") keeps an ST on ST Low.
 */
#define STDL_SDL1_HINT_PAGES     "STDL_SDL2_PAGES"
#define STDL_SDL1_HINT_CHECK     "STDL_SDL2_CHECK"
#define STDL_SDL1_HINT_COMPARE   "STDL_SDL2_COMPARE"
#define STDL_SDL1_HINT_ST_TALL   "STDL_SDL2_ST_TALL"
#define STDL_SDL1_HINT_ST_DITHER "STDL_SDL2_ST_DITHER"
int STDL_SDL1_SetHint(const char *name, const char *value);

/*
 * A TT's and a Falcon's border, and the bands round a picture smaller
 * than their screen, are register 0, which is the picture's colour 0.
 * Where that is not black, name a colour the picture never shows, a
 * multiple of 16: the SDL2 add-on's border index (docs/sdl2.md), which
 * puts that one on register 0, held black. Before the first
 * SDL_SetVideoMode: -1 with the error set after it, or for an index
 * the add-on refuses. Nothing on an ST, whose slot 0 is black.
 */
int STDL_SDL1_SetBorderIndex(unsigned x);

/*
 * How many of the 256 colours a surface's pixels use, and which
 * (used[i] 1 or 0; NULL for the count alone): a planar port's budget
 * is 16 at a time, so a prototype that prints its screen's count
 * says how far its art is from one. A pass over the surface, so call
 * it where that costs nothing.
 */
int STDL_SDL1_CountColours(const SDL_Surface *surface, Uint8 *used);

/* STDL_SDL1_HINT_CHECK's result: flips checked and flips whose page
 * was not what the updates made it, or -1 with the check off */
int STDL_SDL1_GetCheck(Uint32 *flips, Uint32 *bad);

#ifdef __cplusplus
}
#endif

#endif /* STDL_SDL1_SDL_H */
