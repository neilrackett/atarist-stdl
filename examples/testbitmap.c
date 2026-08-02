/*
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: CC0-1.0
 *
 * STDL port of a public-domain SDL 1.2 test program; the port is
 * dedicated to the public domain like the original.
 */
/* Simple program:  Test bitmap blits
 *
 * SDL 1.2 test program (public domain), ported to STDL.
 * Port notes:
 *  - the 8bpp direct-pixel gradient becomes 16 horizontal band
 *    fills: planar surfaces are drawn with spans, not memset
 *  - LoadXBM expands through STDL_SurfaceFrom1bpp instead of
 *    writing into a 1bpp surface's pixel buffer
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "SDL.h"
#include "picture.xbm"

/* Call this instead of exit(), so we can clean up SDL: atexit() is evil. */
static void quit(int rc)
{
	SDL_Quit();
	exit(rc);
}

SDL_Surface *LoadXBM(SDL_Surface *screen, int w, int h, Uint8 *bits)
{
	SDL_Surface *bitmap;
	Uint8 *reversed;
	int nbytes, i, j;

	/* X11 Bitmap images have the bits reversed */
	nbytes = ((w + 7) / 8) * h;
	reversed = malloc(nbytes);
	if ( reversed == NULL ) {
		fprintf(stderr, "Out of memory\n");
		return(NULL);
	}
	for ( i = 0; i < nbytes; ++i ) {
		Uint8 byte = bits[i];
		reversed[i] = 0;
		for ( j = 7; j >= 0; --j ) {
			reversed[i] |= (byte & 0x01) << j;
			byte >>= 1;
		}
	}

	/* STDL port: expand 1bpp to planar; ink 15 on colour 0 */
	bitmap = STDL_SurfaceFrom1bpp(reversed, w, h, 15, 0);
	free(reversed);
	if ( bitmap == NULL ) {
		fprintf(stderr, "Couldn't allocate bitmap: %s\n",
						SDL_GetError());
	}
	return(bitmap);
}

int main(int argc, char *argv[])
{
	SDL_Surface *screen;
	SDL_Surface *bitmap;
	Uint8  video_bpp;
	Uint32 videoflags;
	int i, done;
	SDL_Event event;
	SDL_Color palette[16];

	/* Initialize SDL */
	if ( SDL_Init(SDL_INIT_VIDEO) < 0 ) {
		fprintf(stderr, "Couldn't initialize SDL: %s\n",SDL_GetError());
		return(1);
	}

	video_bpp = 4;
	videoflags = SDL_SWSURFACE;
	while ( argc > 1 ) {
		--argc;
		if ( strcmp(argv[argc], "-hw") == 0 ) {
			videoflags |= SDL_HWSURFACE;
		} else
		if ( strcmp(argv[argc], "-fullscreen") == 0 ) {
			videoflags |= SDL_FULLSCREEN;
		} else {
			fprintf(stderr, "Usage: %s [-hw] [-fullscreen]\n",
								argv[0]);
			quit(1);
		}
	}

	/* Set video mode (always 320x200x4 on the ST) */
	screen = SDL_SetVideoMode(320, 200, video_bpp, videoflags);
	if ( screen == NULL ) {
		fprintf(stderr, "Couldn't set video mode: %s\n",
							SDL_GetError());
		quit(2);
	}

	/* Set a gray colormap, reverse order from white to black;
	 * index 15 lands on black and is the bitmap ink */
	for ( i=0; i<16; ++i ) {
		palette[i].r = 255 - i*17;
		palette[i].g = 255 - i*17;
		palette[i].b = 255 - i*17;
	}
	SDL_SetColors(screen, palette, 0, 16);

	/* STDL port: paint the gradient with band fills (planar
	 * surfaces have no chunky pixel buffer to memset) */
	for ( i=0; i<15; ++i ) {
		SDL_Rect band;
		band.x = 0;
		band.y = (Sint16)((i * screen->h) / 15);
		band.w = (Uint16)screen->w;
		band.h = (Uint16)(((i+1) * screen->h) / 15 - band.y);
		SDL_FillRect(screen, &band, (Uint32)i);
	}
	SDL_UpdateRect(screen, 0, 0, 0, 0);

	/* Drop any startup events */
	while ( SDL_PollEvent(&event) ) {
	}

	/* Load the bitmap */
	bitmap = LoadXBM(screen, picture_width, picture_height,
					(Uint8 *)picture_bits);
	if ( bitmap == NULL ) {
		quit(1);
	}

	/* Wait for a keystroke */
	done = 0;
	while ( !done ) {
		/* Check for events */
		while ( SDL_PollEvent(&event) ) {
			switch (event.type) {
				case SDL_MOUSEBUTTONDOWN: {
					SDL_Rect dst;

					dst.x = event.button.x - bitmap->w/2;
					dst.y = event.button.y - bitmap->h/2;
					dst.w = bitmap->w;
					dst.h = bitmap->h;
					SDL_BlitSurface(bitmap, NULL,
								screen, &dst);
					SDL_UpdateRects(screen,1,&dst);
					}
					break;
				case SDL_KEYDOWN:
					if (event.key.keysym.sym == SDLK_ESCAPE) {
						done = 1;
					}
					break;
				case SDL_QUIT:
					done = 1;
					break;
				default:
					break;
			}
		}
	}
	SDL_FreeSurface(bitmap);
	SDL_Quit();
	return(0);
}
