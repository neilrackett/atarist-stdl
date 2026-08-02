/*
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: CC0-1.0
 *
 * STDL port of a public-domain SDL 1.2 test program; the port is
 * dedicated to the public domain like the original.
 */
/* Simple program:  Fill a colormap with gray and stripe it down the screen
 *
 * SDL 1.2 test program (public domain), ported to STDL.
 * Built with TEST_VGA16 semantics (16 colours). Port notes:
 *  - DrawBackground paints its gradient with band fills instead of
 *    memset into a chunky pixel buffer
 *  - fullscreen toggle / resize / expose events never fire on the
 *    ST but the cases still compile
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "SDL.h"

#define NUM_COLORS	16

/* Draw a randomly sized and colored box centered about (X,Y) */
void DrawBox(SDL_Surface *screen, int X, int Y, int width, int height)
{
	static unsigned int seeded = 0;
	SDL_Rect area;
	Uint32 color;
	Uint32 randc;

	/* Seed the random number generator */
	if ( seeded == 0 ) {
		srand((unsigned int) time(NULL));
		seeded = 1;
	}

	/* Get the bounds of the rectangle */
	area.w = (rand()%width);
	area.h = (rand()%height);
	area.x = X-(area.w/2);
	area.y = Y-(area.h/2);
	randc = (rand()%NUM_COLORS);

	if (screen->format->BytesPerPixel==1) {
		color = randc;
	} else {
		color = SDL_MapRGB(screen->format, randc, randc, randc);
	}

	/* Do it! */
	SDL_FillRect(screen, &area, color);
	if ( screen->flags & SDL_DOUBLEBUF ) {
		SDL_Flip(screen);
	} else {
		SDL_UpdateRects(screen, 1, &area);
	}
}

void DrawBackground(SDL_Surface *screen)
{
	int i, j;

	/* Set the surface pixels and refresh! */
	/* Use two loops in case the surface is double-buffered (both sides) */
	for ( j=0; j<2; ++j ) {
		/* STDL port: band fills instead of memset rows */
		for ( i=0; i<screen->h; ++i ) {
			SDL_Rect band;
			band.x = 0;
			band.y = (Sint16)i;
			band.w = (Uint16)screen->w;
			band.h = 1;
			SDL_FillRect(screen, &band,
				(Uint32)((i*(NUM_COLORS-1))/screen->h));
		}
		if ( screen->flags & SDL_DOUBLEBUF ) {
			SDL_Flip(screen);
		} else {
			SDL_UpdateRect(screen, 0, 0, 0, 0);
			break;
		}
	}
}

SDL_Surface *CreateScreen(Uint16 w, Uint16 h, Uint8 bpp, Uint32 flags)
{
	SDL_Surface *screen;
	int i;
	SDL_Color palette[NUM_COLORS];

	/* Set the video mode */
	screen = SDL_SetVideoMode(w, h, bpp, flags);
	if ( screen == NULL ) {
		fprintf(stderr, "Couldn't set display mode: %s\n",
							SDL_GetError());
		return(NULL);
	}
	fprintf(stderr, "Screen is in %s mode\n",
		(screen->flags & SDL_FULLSCREEN) ? "fullscreen" : "windowed");

	/* Set a gray colormap, reverse order from white to black */
	for ( i=0; i<NUM_COLORS; ++i ) {
		palette[i].r = 255 - i * (256 / NUM_COLORS);
		palette[i].g = 255 - i * (256 / NUM_COLORS);
		palette[i].b = 255 - i * (256 / NUM_COLORS);
	}
	SDL_SetColors(screen, palette, 0, NUM_COLORS);

	return(screen);
}

int main(int argc, char *argv[])
{
	SDL_Surface *screen;
	Uint32 videoflags;
	int    done;
	SDL_Event event;
	int width, height, bpp;

	/* Initialize SDL */
	if ( SDL_Init(SDL_INIT_VIDEO) < 0 ) {
		fprintf(stderr, "Couldn't initialize SDL: %s\n",SDL_GetError());
		exit(1);
	}

	width = 320;
	height = 200;
	bpp = 4;
	videoflags = SDL_SWSURFACE;
	while ( argc > 1 ) {
		--argc;
		if ( argv[argc] && (strcmp(argv[argc], "-hw") == 0) ) {
			videoflags |= SDL_HWSURFACE;
		} else
		if ( argv[argc] && (strcmp(argv[argc], "-flip") == 0) ) {
			videoflags |= SDL_DOUBLEBUF;
		} else
		if ( argv[argc] && (strcmp(argv[argc], "-fullscreen") == 0) ) {
			videoflags |= SDL_FULLSCREEN;
		} else {
			fprintf(stderr,
			"Usage: %s [-hw] [-flip] [-fullscreen]\n", argv[0]);
			exit(1);
		}
	}

	/* Set a video mode */
	screen = CreateScreen(width, height, bpp, videoflags);
	if ( screen == NULL ) {
		exit(2);
	}

	DrawBackground(screen);

	/* Wait for a keystroke */
	done = 0;
	while ( !done && SDL_WaitEvent(&event) ) {
		switch (event.type) {
			case SDL_MOUSEBUTTONDOWN:
				DrawBox(screen, event.button.x, event.button.y, width, height);
				break;
			case SDL_KEYDOWN:
				/* Ignore ALT-TAB for windows */
				if ( (event.key.keysym.sym == SDLK_LALT) ||
				     (event.key.keysym.sym == SDLK_TAB) ) {
					break;
				}
				/* Center the mouse on <SPACE> */
				if ( event.key.keysym.sym == SDLK_SPACE ) {
					SDL_WarpMouse(width/2, height/2);
					break;
				}
				/* Redraw the background on <RETURN> */
				if ( event.key.keysym.sym == SDLK_RETURN ) {
					DrawBackground(screen);
					break;
				}
				/* Any other key quits the application... */
			case SDL_QUIT:
				done = 1;
				break;
			case SDL_VIDEOEXPOSE:
				DrawBackground(screen);
				break;
			default:
				break;
		}
	}
	SDL_Quit();
	return(0);
}
