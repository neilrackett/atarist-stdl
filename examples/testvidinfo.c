/*
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: CC0-1.0
 *
 * STDL port of a public-domain SDL 1.2 test program; the port is
 * dedicated to the public domain like the original.
 */
/* Simple program -- figure out what kind of video display we have
 *
 * SDL 1.2 test program (public domain), ported to STDL.
 * Port notes:
 *  - the benchmark now runs by default (TOS programs rarely get
 *    command-line arguments) and the mode table is the ST's single
 *    real mode; the two software-surface flag combos are kept to
 *    show the honest "flags didn't match" path
 *  - iteration counts are scaled for a 68000: 64 fill+flip steps
 *    per channel, 50 update rounds of 4 blits
 *  - alpha tests are skipped automatically (4bpp screen)
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "SDL.h"

#define NUM_BLITS	4
#define NUM_UPDATES	50
#define NUM_FILL_STEPS	64

#define FLAG_MASK	(SDL_HWSURFACE | SDL_FULLSCREEN | SDL_DOUBLEBUF | \
                         SDL_SRCCOLORKEY | SDL_SRCALPHA | SDL_RLEACCEL  | \
                         SDL_RLEACCELOK)

void PrintFlags(Uint32 flags)
{
	printf("0x%8.8x", (unsigned)(flags & FLAG_MASK));
	if ( flags & SDL_HWSURFACE ) {
		printf(" SDL_HWSURFACE");
	} else {
		printf(" SDL_SWSURFACE");
	}
	if ( flags & SDL_FULLSCREEN ) {
		printf(" | SDL_FULLSCREEN");
	}
	if ( flags & SDL_DOUBLEBUF ) {
		printf(" | SDL_DOUBLEBUF");
	}
	if ( flags & SDL_SRCCOLORKEY ) {
		printf(" | SDL_SRCCOLORKEY");
	}
	if ( flags & SDL_RLEACCEL ) {
		printf(" | SDL_RLEACCEL");
	}
}

int RunBlitTests(SDL_Surface *screen, SDL_Surface *bmp, int blitcount)
{
	int i, j;
	int maxx;
	int maxy;
	SDL_Rect dst;

	maxx = (int)screen->w - bmp->w + 1;
	maxy = (int)screen->h - bmp->h + 1;
	if ( maxx < 1 ) maxx = 1;
	if ( maxy < 1 ) maxy = 1;
	for ( i = 0; i < NUM_UPDATES; ++i ) {
		for ( j = 0; j < blitcount; ++j ) {
			dst.x = rand() % maxx;
			dst.y = rand() % maxy;
			dst.w = bmp->w;
			dst.h = bmp->h;
			SDL_BlitSurface(bmp, NULL, screen, &dst);
		}
		SDL_Flip(screen);
	}

	return i;
}

int RunModeTests(SDL_Surface *screen)
{
	Uint32 then, now;
	Uint32 frames;
	float seconds;
	int i;
	Uint8 r, g, b;
	SDL_Surface *bmp, *bmpcc, *tmp;
	SDL_Event event;

	while ( SDL_PollEvent(&event) ) {
		if ( event.type == SDL_KEYDOWN )
			return 0;
	}

	/* First test fills and screen update speed */
	printf("Running color fill and fullscreen update test\n");
	then = SDL_GetTicks();
	frames = 0;
	for ( i = 0; i < NUM_FILL_STEPS; ++i ) {
		r = i*4;
		g = 0;
		b = 0;
		SDL_FillRect(screen, NULL, SDL_MapRGB(screen->format, r, g, b));
		SDL_Flip(screen);
		++frames;
	}
	for ( i = 0; i < NUM_FILL_STEPS; ++i ) {
		r = 0;
		g = i*4;
		b = 0;
		SDL_FillRect(screen, NULL, SDL_MapRGB(screen->format, r, g, b));
		SDL_Flip(screen);
		++frames;
	}
	for ( i = 0; i < NUM_FILL_STEPS; ++i ) {
		r = 0;
		g = 0;
		b = i*4;
		SDL_FillRect(screen, NULL, SDL_MapRGB(screen->format, r, g, b));
		SDL_Flip(screen);
		++frames;
	}
	now = SDL_GetTicks();
	seconds = (float)(now - then) / 1000.0f;
	if ( seconds > 0.0f ) {
		printf("%d fills and flips in %2.2f seconds, %2.2f FPS\n",
			(int)frames, seconds, (float)frames / seconds);
	} else {
		printf("%d fills and flips in zero seconds!\n", (int)frames);
	}

	/* clear the screen after fill test */
	SDL_FillRect(screen, NULL, SDL_MapRGB(screen->format, 0, 0, 0));
	SDL_Flip(screen);

	while ( SDL_PollEvent(&event) ) {
		if ( event.type == SDL_KEYDOWN )
			return 0;
	}

	/* run the generic blit test */
	bmp = SDL_LoadBMP("SAMPLE.BMP");
	if ( ! bmp ) {
		printf("Couldn't load SAMPLE.BMP: %s\n", SDL_GetError());
		return 0;
	}
	printf("Running freshly loaded blit test: %dx%d at %d bpp, flags: ",
		bmp->w, bmp->h, bmp->format->BitsPerPixel);
	PrintFlags(bmp->flags);
	printf("\n");
	then = SDL_GetTicks();
	frames = RunBlitTests(screen, bmp, NUM_BLITS);
	now = SDL_GetTicks();
	seconds = (float)(now - then) / 1000.0f;
	if ( seconds > 0.0f ) {
		printf("%d blits / %d updates in %2.2f seconds, %2.2f FPS\n",
			NUM_BLITS*(int)frames, (int)frames, seconds,
			(float)frames / seconds);
	} else {
		printf("%d blits / %d updates in zero seconds!\n",
			NUM_BLITS*(int)frames, (int)frames);
	}

	/* clear the screen after blit test */
	SDL_FillRect(screen, NULL, SDL_MapRGB(screen->format, 0, 0, 0));
	SDL_Flip(screen);

	while ( SDL_PollEvent(&event) ) {
		if ( event.type == SDL_KEYDOWN )
			return 0;
	}

	/* run the colorkeyed blit test */
	bmpcc = SDL_LoadBMP("SAMPLE.BMP");
	if ( ! bmpcc ) {
		printf("Couldn't load SAMPLE.BMP: %s\n", SDL_GetError());
		SDL_FreeSurface(bmp);
		return 0;
	}
	printf("Running freshly loaded cc blit test: %dx%d at %d bpp, flags: ",
		bmpcc->w, bmpcc->h, bmpcc->format->BitsPerPixel);
	SDL_SetColorKey(bmpcc, SDL_SRCCOLORKEY | SDL_RLEACCEL,
			STDL_GetPixel(bmpcc, 0, 0));

	PrintFlags(bmpcc->flags);
	printf("\n");
	then = SDL_GetTicks();
	frames = RunBlitTests(screen, bmpcc, NUM_BLITS);
	now = SDL_GetTicks();
	seconds = (float)(now - then) / 1000.0f;
	if ( seconds > 0.0f ) {
		printf("%d cc blits / %d updates in %2.2f seconds, %2.2f FPS\n",
			NUM_BLITS*(int)frames, (int)frames, seconds,
			(float)frames / seconds);
	} else {
		printf("%d cc blits / %d updates in zero seconds!\n",
			NUM_BLITS*(int)frames, (int)frames);
	}

	/* clear the screen after cc blit test */
	SDL_FillRect(screen, NULL, SDL_MapRGB(screen->format, 0, 0, 0));
	SDL_Flip(screen);

	while ( SDL_PollEvent(&event) ) {
		if ( event.type == SDL_KEYDOWN )
			return 0;
	}

	/* run the display-format blit test */
	tmp = bmp;
	bmp = SDL_DisplayFormat(bmp);
	SDL_FreeSurface(tmp);
	if ( ! bmp ) {
		printf("Couldn't convert SAMPLE.BMP: %s\n", SDL_GetError());
		SDL_FreeSurface(bmpcc);
		return 0;
	}
	printf("Running display format blit test: %dx%d at %d bpp, flags: ",
		bmp->w, bmp->h, bmp->format->BitsPerPixel);
	PrintFlags(bmp->flags);
	printf("\n");
	then = SDL_GetTicks();
	frames = RunBlitTests(screen, bmp, NUM_BLITS);
	now = SDL_GetTicks();
	seconds = (float)(now - then) / 1000.0f;
	if ( seconds > 0.0f ) {
		printf("%d blits / %d updates in %2.2f seconds, %2.2f FPS\n",
			NUM_BLITS*(int)frames, (int)frames, seconds,
			(float)frames / seconds);
	} else {
		printf("%d blits / %d updates in zero seconds!\n",
			NUM_BLITS*(int)frames, (int)frames);
	}

	/* clear the screen after blit test */
	SDL_FillRect(screen, NULL, SDL_MapRGB(screen->format, 0, 0, 0));
	SDL_Flip(screen);

	while ( SDL_PollEvent(&event) ) {
		if ( event.type == SDL_KEYDOWN )
			return 0;
	}

	/* run the display-format colorkeyed blit test */
	tmp = bmpcc;
	bmpcc = SDL_DisplayFormat(bmpcc);
	SDL_FreeSurface(tmp);
	if ( ! bmpcc ) {
		printf("Couldn't convert SAMPLE.BMP: %s\n", SDL_GetError());
		SDL_FreeSurface(bmp);
		return 0;
	}
	printf("Running display format cc blit test: %dx%d at %d bpp, flags: ",
		bmpcc->w, bmpcc->h, bmpcc->format->BitsPerPixel);
	PrintFlags(bmpcc->flags);
	printf("\n");
	then = SDL_GetTicks();
	frames = RunBlitTests(screen, bmpcc, NUM_BLITS);
	now = SDL_GetTicks();
	seconds = (float)(now - then) / 1000.0f;
	if ( seconds > 0.0f ) {
		printf("%d cc blits / %d updates in %2.2f seconds, %2.2f FPS\n",
			NUM_BLITS*(int)frames, (int)frames, seconds,
			(float)frames / seconds);
	} else {
		printf("%d cc blits / %d updates in zero seconds!\n",
			NUM_BLITS*(int)frames, (int)frames);
	}

	/* alpha tests need bpp > 8: skipped on a 4bpp planar screen */

	SDL_FreeSurface(bmpcc);
	SDL_FreeSurface(bmp);

	while ( SDL_PollEvent(&event) ) {
		if ( event.type == SDL_KEYDOWN )
			return 0;
	}
	return 1;
}

void RunVideoTests(void)
{
	static const struct {
		int w, h, bpp;
	} mode_list[] = {
		{ 320, 200, 4 }
	};
	static const Uint32 flags[] = {
		(SDL_SWSURFACE),
		(SDL_HWSURFACE | SDL_FULLSCREEN),
		(SDL_HWSURFACE | SDL_FULLSCREEN | SDL_DOUBLEBUF)
	};
	int i, j;
	SDL_Surface *screen;

	/* Test out several different video mode combinations */
	SDL_WM_SetCaption("SDL Video Benchmark", "vidtest");
	SDL_ShowCursor(0);
	for ( i = 0; i < (int)SDL_TABLESIZE(mode_list); ++i ) {
		for ( j = 0; j < (int)SDL_TABLESIZE(flags); ++j ) {
			printf("===================================\n");
			printf("Setting video mode: %dx%d at %d bpp, flags: ",
			                          mode_list[i].w,
			                          mode_list[i].h,
			                          mode_list[i].bpp);
			PrintFlags(flags[j]);
			printf("\n");
			screen = SDL_SetVideoMode(mode_list[i].w,
			                          mode_list[i].h,
			                          mode_list[i].bpp,
			                          flags[j]);
			if ( ! screen ) {
				printf("Setting video mode failed: %s\n", SDL_GetError());
				continue;
			}
			if ( (screen->flags & FLAG_MASK) != flags[j] ) {
				printf("Flags didn't match: ");
				PrintFlags(screen->flags);
				printf("\n");
				continue;
			}
			if ( ! RunModeTests(screen) ) {
				return;
			}
		}
	}
}

int main(int argc, char *argv[])
{
	const SDL_VideoInfo *info;
	int i;
	SDL_Rect **modes;
	char driver[128];

	if ( SDL_Init(SDL_INIT_VIDEO) < 0 ) {
		fprintf(stderr,
			"Couldn't initialize SDL: %s\n", SDL_GetError());
		exit(1);
	}
	if ( SDL_VideoDriverName(driver, sizeof(driver)) ) {
		printf("Video driver: %s\n", driver);
	}
	(void)SDL_SetVideoMode(320, 200, 4, 0);
	info = SDL_GetVideoInfo();
	printf(
"Current display: %dx%d, %d bits-per-pixel\n",
		info->current_w, info->current_h, info->vfmt->BitsPerPixel);
	/* Print available fullscreen video modes */
	modes = SDL_ListModes(NULL, SDL_FULLSCREEN);
	if ( modes == (SDL_Rect **)0 ) {
		printf("No available fullscreen video modes\n");
	} else
	if ( modes == (SDL_Rect **)-1 ) {
		printf("No special fullscreen video modes\n");
	} else {
		printf("Fullscreen video modes:\n");
		for ( i=0; modes[i]; ++i ) {
			printf("\t%dx%dx%d\n", modes[i]->w, modes[i]->h, info->vfmt->BitsPerPixel);
		}
	}
	if ( info->hw_available ) {
		printf("Hardware surfaces are available (%dK video memory)\n",
			(int)info->video_mem);
	}
	if ( info->blit_hw ) {
		printf(
"Copy blits between hardware surfaces are accelerated\n");
	}
	if ( info->blit_hw_CC ) {
		printf(
"Colorkey blits between hardware surfaces are accelerated\n");
	}
	if ( info->blit_fill ) {
		printf(
"Color fills on hardware surfaces are accelerated\n");
	}

	/* STDL port: the benchmark is the point on real hardware, so
	 * it runs by default; pass -nobenchmark to skip */
	if ( !(argv[1] && (strcmp(argv[1], "-nobenchmark") == 0)) ) {
		RunVideoTests();
	}

	SDL_Quit();
	return(0);
}
