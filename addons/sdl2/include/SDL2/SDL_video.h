/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * The SDL2 add-on's window, which is the screen: STDL's 256-colour
 * responsive screen (stdl_responsive.h), 320x240 in eight interleaved
 * planes on a TT or a Falcon. One window at a time, of 320x240 or
 * less, shown centred; position, title and the window flags are
 * accepted and have nothing to act on. The experimental 68000 build
 * (docs/sdl2.md) gives a first window wider than 320 STDL's 640x480 in
 * 16 colours on a TT or a Falcon, and SDL_GetDesktopDisplayMode says
 * which a program can have.
 */

#ifndef STDL_SDL2_VIDEO_H
#define STDL_SDL2_VIDEO_H

#include "SDL_stdinc.h"
#include "SDL_rect.h"
#include "SDL_pixels.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct SDL_Window SDL_Window;

/* SDL2's values */
#define SDL_WINDOW_FULLSCREEN         0x00000001u
#define SDL_WINDOW_SHOWN              0x00000004u
#define SDL_WINDOW_HIDDEN             0x00000008u
#define SDL_WINDOW_BORDERLESS         0x00000010u
#define SDL_WINDOW_RESIZABLE          0x00000020u
#define SDL_WINDOW_MINIMIZED          0x00000040u
#define SDL_WINDOW_MAXIMIZED          0x00000080u
#define SDL_WINDOW_INPUT_GRABBED      0x00000100u
#define SDL_WINDOW_INPUT_FOCUS        0x00000200u
#define SDL_WINDOW_MOUSE_FOCUS        0x00000400u
#define SDL_WINDOW_FULLSCREEN_DESKTOP (SDL_WINDOW_FULLSCREEN | 0x00001000u)
#define SDL_WINDOW_ALLOW_HIGHDPI      0x00002000u

#define SDL_WINDOWPOS_UNDEFINED_MASK    0x1FFF0000u
#define SDL_WINDOWPOS_UNDEFINED_DISPLAY(X) (SDL_WINDOWPOS_UNDEFINED_MASK | (X))
#define SDL_WINDOWPOS_UNDEFINED         SDL_WINDOWPOS_UNDEFINED_DISPLAY(0)
#define SDL_WINDOWPOS_CENTERED_MASK     0x2FFF0000u
#define SDL_WINDOWPOS_CENTERED_DISPLAY(X) (SDL_WINDOWPOS_CENTERED_MASK | (X))
#define SDL_WINDOWPOS_CENTERED          SDL_WINDOWPOS_CENTERED_DISPLAY(0)

/* SDL2's */
typedef struct {
    Uint32 format;
    int w;
    int h;
    int refresh_rate;
    void *driverdata;
} SDL_DisplayMode;

#define SDL_GetDesktopDisplayMode stdl_sdl2_GetDesktopDisplayMode
#define SDL_CreateWindow   stdl_sdl2_CreateWindow
#define SDL_DestroyWindow  stdl_sdl2_DestroyWindow
#define SDL_GetWindowSize  stdl_sdl2_GetWindowSize
#define SDL_SetWindowTitle stdl_sdl2_SetWindowTitle

/*
 * The largest window this build gives on this machine, as a program
 * asks the desktop's mode to size a window or a fullscreen picture by:
 * 640x480 on a TT or a Falcon in the experimental 68000 build (the
 * wide screen's 16 colours), else 320x240 (256 colours on a TT or a
 * Falcon, an ST's 16 in the experimental build); format
 * SDL_PIXELFORMAT_INDEX8, the textures', and refresh_rate 0, as the
 * rate is the screen's to measure once it is set. Display 0 only,
 * after SDL_Init(SDL_INIT_VIDEO).
 */
extern int SDL_GetDesktopDisplayMode(int displayIndex, SDL_DisplayMode *mode);

/*
 * w and h at most 320 and 240, or what SDL_GetDesktopDisplayMode says.
 * The first window sets the screen up;
 * the screen stays up after SDL_DestroyWindow, until SDL_Quit or the
 * program's exit, and a later window takes it again (the page count
 * then is the first window's).
 */
extern SDL_Window *SDL_CreateWindow(const char *title, int x, int y, int w,
                                    int h, Uint32 flags);
extern void SDL_DestroyWindow(SDL_Window *window);
extern void SDL_GetWindowSize(SDL_Window *window, int *w, int *h);
extern void SDL_SetWindowTitle(SDL_Window *window, const char *title);

#ifdef __cplusplus
}
#endif

#endif /* STDL_SDL2_VIDEO_H */
