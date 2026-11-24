/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * Between the SDL1 add-on's two halves. SDL 1.2's headers and SDL2's
 * give the same names to different things (SDL_Surface, SDL_Rect,
 * SDL_Color), so no file can include both: video.c is written against
 * SDL 1.2's (the add-on's own SDL.h), bridge.c against the SDL2
 * add-on's, and they meet here in ints, bytes and pointers.
 */

#ifndef STDL_SDL1_BRIDGE_H
#define STDL_SDL1_BRIDGE_H

/* The screen: a window and renderer the first time, then a logical
 * size of w by h and a texture over the caller's pixels (no copy).
 * *ox, *oy: where the picture's top left is on STDL's screen, for the
 * mouse. -1 with STDL's error set. */
int  stdl_sdl1_br_open(void *pixels, int w, int h, int pitch, int *ox,
                       int *oy);
/* a box of the screen's pixels to the display, then the present */
void stdl_sdl1_br_push(int x, int y, int w, int h);
void stdl_sdl1_br_present(void);
/* n of the display's colours from `first`, 4 bytes each (r, g, b and
 * one unused, STDL_Colour's layout and SDL2's SDL_Color's), shown at
 * once */
void stdl_sdl1_br_colours(const void *colours, int first, int n);
int  stdl_sdl1_br_hint(const char *name, const char *value);
/* the largest screen there can be: 640x480 on a TT or a Falcon over the
 * SDL2 add-on's experimental build (SDL_GetDesktopDisplayMode) until a
 * screen of 320 pixels is made, else 320x240 */
void stdl_sdl1_br_limit(int *w, int *h);
/* the SDL2 add-on's border index, for when the screen is made; -1
 * with STDL's error set once it has been */
int  stdl_sdl1_br_border(unsigned x);
int  stdl_sdl1_br_check(unsigned long *flips, unsigned long *bad);

/* A BMP's pixels into a block the caller frees, pitch bytes a row,
 * and its palette, 4 bytes a colour (256 at most). The SDL2 add-on's
 * loader; the file's name is tried in upper case too. NULL with STDL's error
 * set. */
void *stdl_sdl1_br_bmp(const char *file, int *w, int *h, int *pitch,
                       unsigned char *colours, int *ncolours);

#endif /* STDL_SDL1_BRIDGE_H */
