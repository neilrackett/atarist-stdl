/*
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: CC0-1.0
 *
 * STDL port of a public-domain SDL 1.2 test program; the port is
 * dedicated to the public domain like the original.
 */
/*
 * testpalette.c
 *
 * A simple test of runtime palette modification for animation
 * (using the SDL_SetPalette() API).
 *
 * SDL 1.2 test program (public domain), ported to STDL.
 * Port notes - this is the deepest port of the set, because the
 * original divides a 256-entry palette into 64 wave colours plus
 * the boat's own colours. On the ST there are 16 palette entries:
 *   index 0..7   boat colours (SAIL.BMP quantised to 8 by stdlconv)
 *   index 8..14  seven wave colours, cycled for the water effect
 *   index 15     the boat's magenta colour key (never displayed),
 *                re-used as white for the FPS overlay
 * The wavy background is drawn with spans instead of per-pixel
 * random walks, and the FPS overlay goes through FillRect/PutPixel.
 * The palette fade in/out and the wave cycling are exactly the
 * original logic, running on SDL_SetPalette(SDL_PHYSPAL).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#ifndef M_PI
#define M_PI	3.14159265358979323846
#endif

#include "SDL.h"

/* screen size */
#define SCRW 320
#define SCRH 200

#define NBOATS 5
#define SPEED 2
#define NWAVE 7
#define BOATCOLS 8
#define FPS_TEXT_SCALE 1

#ifndef MIN
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#endif
#ifndef MAX
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#endif

static const Uint8 glyph_blank[5] = { 0, 0, 0, 0, 0 };
static const Uint8 glyph_colon[5] = { 0, 2, 0, 2, 0 };
static const Uint8 glyph_dot[5] = { 0, 0, 0, 0, 2 };
static const Uint8 glyph_0[5] = { 7, 5, 5, 5, 7 };
static const Uint8 glyph_1[5] = { 2, 6, 2, 2, 7 };
static const Uint8 glyph_2[5] = { 7, 1, 7, 4, 7 };
static const Uint8 glyph_3[5] = { 7, 1, 7, 1, 7 };
static const Uint8 glyph_4[5] = { 5, 5, 7, 1, 1 };
static const Uint8 glyph_5[5] = { 7, 4, 7, 1, 7 };
static const Uint8 glyph_6[5] = { 7, 4, 7, 5, 7 };
static const Uint8 glyph_7[5] = { 7, 1, 1, 1, 1 };
static const Uint8 glyph_8[5] = { 7, 5, 7, 5, 7 };
static const Uint8 glyph_9[5] = { 7, 5, 7, 1, 7 };
static const Uint8 glyph_F[5] = { 7, 4, 6, 4, 4 };
static const Uint8 glyph_P[5] = { 6, 5, 6, 4, 4 };
static const Uint8 glyph_S[5] = { 3, 4, 2, 1, 6 };

static const Uint8 *GetGlyph3x5(char c)
{
    switch (c) {
    case '0': return glyph_0;
    case '1': return glyph_1;
    case '2': return glyph_2;
    case '3': return glyph_3;
    case '4': return glyph_4;
    case '5': return glyph_5;
    case '6': return glyph_6;
    case '7': return glyph_7;
    case '8': return glyph_8;
    case '9': return glyph_9;
    case 'F': return glyph_F;
    case 'P': return glyph_P;
    case 'S': return glyph_S;
    case ':': return glyph_colon;
    case '.': return glyph_dot;
    case ' ': return glyph_blank;
    default: return glyph_blank;
    }
}

static SDL_bool RectsOverlap(const SDL_Rect *a, const SDL_Rect *b)
{
    int ax2 = a->x + a->w;
    int ay2 = a->y + a->h;
    int bx2 = b->x + b->w;
    int by2 = b->y + b->h;

    if (ax2 <= b->x || bx2 <= a->x || ay2 <= b->y || by2 <= a->y)
	return SDL_FALSE;
    return SDL_TRUE;
}

static void DrawText3x5(SDL_Surface *screen, int x, int y, const char *text, Uint8 color, int scale)
{
    int i, row, col, sr, sc;

    for (i = 0; text[i] != '\0'; ++i) {
	const Uint8 *glyph = GetGlyph3x5(text[i]);
	for (row = 0; row < 5; ++row) {
	    Uint8 bits = glyph[row];
	    for (col = 0; col < 3; ++col) {
		if (bits & (1 << (2 - col))) {
		    int px = x + i * (4 * scale) + col * scale;
		    int py = y + row * scale;
		    for (sr = 0; sr < scale; ++sr)
			for (sc = 0; sc < scale; ++sc)
			    STDL_PutPixel(screen, px + sc, py + sr, color);
		}
	    }
	}
    }
}

static SDL_Rect GetFPSRect(SDL_Surface *screen)
{
    SDL_Rect r;
    int text_w = (9 * (4 * FPS_TEXT_SCALE)) - FPS_TEXT_SCALE; /* "FPS:000.0" */
    int text_h = 5 * FPS_TEXT_SCALE;
    int margin = 4;

    r.x = margin;
    r.y = margin;
    r.w = text_w + 4;
    r.h = text_h + 4;
    return r;
}

static void DrawFPSOverlay(SDL_Surface *screen, int fps_tenths, Uint8 fg, Uint8 bg, SDL_Rect *out_rect)
{
    char text[16];
    SDL_Rect r = GetFPSRect(screen);
    SDL_Rect fill = r;

    if (fps_tenths < 0) fps_tenths = 0;
    if (fps_tenths > 9999) fps_tenths = 9999;

    SDL_FillRect(screen, &fill, bg);
    SDL_snprintf(text, sizeof(text), "FPS:%3d.%1d", fps_tenths / 10, fps_tenths % 10);
    DrawText3x5(screen, r.x + 2, r.y + 2, text, fg, FPS_TEXT_SCALE);

    if (out_rect) {
	*out_rect = r;
    }
}

/*
 * wave colours: a seven-colour cross-section of the original
 * 64-entry wave map (every ninth entry).
 */
static SDL_Color wavemap[NWAVE] = {
    {0, 2, 103, 0}, {0, 55, 174, 0}, {33, 135, 230, 0},
    {89, 195, 244, 0}, {167, 233, 252, 0}, {102, 251, 252, 0},
    {2, 158, 252, 0}
};

/* Call this instead of exit(), so we can clean up SDL: atexit() is evil. */
static void quit(int rc)
{
	SDL_Quit();
	exit(rc);
}

static void sdlerr(const char *when)
{
    fprintf(stderr, "SDL error: %s: %s\n", when, SDL_GetError());
    quit(1);
}

/* create a background surface: wavy stripes in colours 8..14 */
static SDL_Surface *make_bg(SDL_Surface *screen, int startcol)
{
    int y;
    SDL_Surface *bg = SDL_CreateRGBSurface(SDL_SWSURFACE, screen->w, screen->h,
					   4, 0, 0, 0, 0);
    if(!bg)
	sdlerr("creating background surface");

    /* horizontal spans with a drifting phase make the palette
     * cycling read as rolling waves */
    for(y = 0; y < bg->h; y++) {
	int x = 0;
	int phase = (y / 2) + (rand() % 2);
	while (x < bg->w) {
	    int len = 8 + (rand() % 12);
	    Uint8 c = (Uint8)(startcol + ((phase + x / 16) % NWAVE));
	    STDL_HLine(bg, x, MIN(x + len - 1, bg->w - 1), y, c);
	    x += len;
	}
    }
    return(bg);
}

/*
 * Return a surface flipped horisontally. Load-time only, so the
 * pixel-at-a-time copy is fine.
 */
static SDL_Surface *hflip(SDL_Surface *s)
{
    int i, j;
    SDL_Surface *z = SDL_CreateRGBSurface(SDL_SWSURFACE, s->w, s->h, 4,
					  0, 0, 0, 0);
    if(!z)
	sdlerr("creating flip surface");
    /* copy palette */
    SDL_SetColors(z, s->format->palette->colors,
		  0, s->format->palette->ncolors);

    for(i = 0; i < s->h; i++)
	for(j = 0; j < s->w; j++)
	    STDL_PutPixel(z, s->w - 1 - j, i, STDL_GetPixel(s, j, i));

    return z;
}

int main(int argc, char **argv)
{
    SDL_Color cmap[16];
    SDL_Surface *screen;
    SDL_Surface *bg;
    SDL_Surface *boat[2];
    unsigned vidflags = 0;
    unsigned start;
    Uint32 fps_then;
    Uint32 fps_frames;
    Uint32 fps_now;
    int fade_max = 100;
    int fade_level, fade_dir;
    int boatcols, frames, i, red;
    int boatx[NBOATS], boaty[NBOATS], boatdir[NBOATS];
    int boats = NBOATS;
    int palette_step = 1;
    int show_fps = 1;
    int log_fps = 0;
    int fps_tenths = 0;
    Uint8 fps_fg = 15;          /* white (palette entry 15)          */
    Uint8 fps_bg = BOATCOLS;    /* darkest wave colour, not entry 0:
                                   the boat palette's 0 is white     */

    if(SDL_Init(SDL_INIT_VIDEO) < 0)
	sdlerr("initialising SDL");

    while(--argc) {
	++argv;
	if(strcmp(*argv, "-nofade") == 0)
	    fade_max = 1;
	else if((strcmp(*argv, "-fademax") == 0) && argc > 0) {
	    fade_max = atoi(*++argv), --argc;
	    if(fade_max < 1)
		fade_max = 1;
	}
	else if(strcmp(*argv, "-boats") == 0 && argc > 0) {
	    boats = atoi(*++argv), --argc;
	    if(boats < 1) boats = 1;
	    if(boats > NBOATS) boats = NBOATS;
	}
	else if(strcmp(*argv, "-palstep") == 0 && argc > 0) {
	    palette_step = atoi(*++argv), --argc;
	    if(palette_step < 1) palette_step = 1;
	}
	else if(strcmp(*argv, "-nofps") == 0)
	    show_fps = 0;
	else if(strcmp(*argv, "-fpslog") == 0)
	    log_fps = 1;
	else {
	    fprintf(stderr,
		    "usage: testpalette "
		    " [-nofade] [-fademax N] [-boats N]"
		    " [-palstep N] [-nofps] [-fpslog]\n");
	    quit(1);
	}
    }

    /* Ask explicitly for a hardware palette */
    if((screen = SDL_SetVideoMode(SCRW, SCRH, 4, vidflags | SDL_HWPALETTE)) == NULL)
	sdlerr("setting video mode");

    if((boat[0] = SDL_LoadBMP("SAIL.BMP")) == NULL)
	sdlerr("loading SAIL.BMP");
    /* We've chosen magenta (#ff00ff) as colour key for the boat */
    SDL_SetColorKey(boat[0], SDL_SRCCOLORKEY | SDL_RLEACCEL,
		    SDL_MapRGB(boat[0]->format, 0xff, 0x00, 0xff));
    boatcols = BOATCOLS;
    boat[1] = hflip(boat[0]);
    SDL_SetColorKey(boat[1], SDL_SRCCOLORKEY | SDL_RLEACCEL,
		    SDL_MapRGB(boat[1]->format, 0xff, 0x00, 0xff));

    /*
     * First set the physical screen palette to black, so the user won't
     * see our initial drawing on the screen.
     */
    memset(cmap, 0, sizeof(cmap));
    SDL_SetPalette(screen, SDL_PHYSPAL, cmap, 0, 16);

    /*
     * Proper palette management is important when playing games with the
     * colormap. We have divided the 16 entries as follows:
     *
     * index 0..(boatcols-1):        used for the boat
     * index boatcols..boatcols+6:   used for the waves
     * index 15:                     colour key / FPS text
     */
    SDL_SetPalette(screen, SDL_LOGPAL,
		   boat[0]->format->palette->colors, 0, boatcols);
    SDL_SetPalette(screen, SDL_LOGPAL, wavemap, boatcols, NWAVE);
    {
	SDL_Color white = { 255, 255, 255, 0 };
	SDL_SetPalette(screen, SDL_LOGPAL, &white, 15, 1);
    }

    /*
     * Now the logical screen palette is set, and will remain unchanged.
     * The boats already have the same palette so fast blits can be used.
     */
    memcpy(cmap, screen->format->palette->colors, 16 * sizeof(SDL_Color));

    /* save the index of the red colour for later */
    red = SDL_MapRGB(screen->format, 0xff, 0x00, 0x00);

    bg = make_bg(screen, boatcols); /* make a nice wavy background surface */

    /* initial screen contents */
    if(SDL_BlitSurface(bg, NULL, screen, NULL) < 0)
	sdlerr("blitting background to screen");
    SDL_Flip(screen);		/* actually put the background on screen */

    /* determine initial boat placements */
    for(i = 0; i < boats; i++) {
	boatx[i] = (rand() % (screen->w + boat[0]->w)) - boat[0]->w;
	if (boats > 1) {
	    boaty[i] = i * (screen->h - boat[0]->h) / (boats - 1);
	} else {
	    boaty[i] = (screen->h - boat[0]->h) / 2;
	}
	boatdir[i] = ((rand() >> 5) & 1) * 2 - 1;
    }

    start = SDL_GetTicks();
    fps_then = start;
    fps_frames = 0;
    frames = 0;
    fade_dir = 1;
    fade_level = 0;
    do {
	SDL_Event e;
	SDL_Rect updates[NBOATS + 1];
	SDL_Rect r;
	SDL_Rect fps_rect;
	SDL_Rect fps_target;
	int redphase;
	int nupdates;
	int palette_changed = 0;
	int fps_needs_redraw = 0;

	/* A small event loop: just exit on any key or mouse button event */
	while(SDL_PollEvent(&e)) {
	    if(e.type == SDL_KEYDOWN || e.type == SDL_QUIT
	       || e.type == SDL_MOUSEBUTTONDOWN) {
		if(fade_dir < 0)
		    fade_level = 0;
		fade_dir = -1;
	    }
	}

	/* move boats */
	for(i = 0; i < boats; i++) {
	    int old_x = boatx[i];
	    /* update boat position */
	    boatx[i] += boatdir[i] * SPEED;
	    if(boatx[i] <= -boat[0]->w || boatx[i] >= screen->w)
		boatdir[i] = -boatdir[i];

	    /* paint over the old boat position */
	    r.x = old_x;
	    r.y = boaty[i];
	    r.w = boat[0]->w;
	    r.h = boat[0]->h;
	    if(SDL_BlitSurface(bg, &r, screen, &r) < 0)
		sdlerr("blitting background");

	    /* construct update rectangle (bounding box of old and new pos) */
	    updates[i].x = MIN(old_x, boatx[i]);
	    updates[i].y = boaty[i];
	    updates[i].w = boat[0]->w + SPEED;
	    updates[i].h = boat[0]->h;
	    /* clip update rectangle to screen */
	    if(updates[i].x < 0) {
		updates[i].w += updates[i].x;
		updates[i].x = 0;
	    }
	    if(updates[i].x + updates[i].w > screen->w)
		updates[i].w = screen->w - updates[i].x;
	}
	nupdates = boats;

	for(i = 0; i < boats; i++) {
	    /* paint boat on new position */
	    r.x = boatx[i];
	    r.y = boaty[i];
	    if(SDL_BlitSurface(boat[(boatdir[i] + 1) / 2], NULL,
			       screen, &r) < 0)
		sdlerr("blitting boat");
	}

	/* cycle wave palette */
	for(i = 0; i < NWAVE; i++)
	    cmap[boatcols + ((i + frames) % NWAVE)] = wavemap[i];

	if(fade_dir) {
	    /* Fade the entire palette in/out */
	    fade_level += fade_dir;

	    /* Fade using direct palette manipulation */
	    memcpy(cmap, screen->format->palette->colors,
		   boatcols * sizeof(SDL_Color));
	    for(i = 0; i < 16; i++) {
		cmap[i].r = cmap[i].r * fade_level / fade_max;
		cmap[i].g = cmap[i].g * fade_level / fade_max;
		cmap[i].b = cmap[i].b * fade_level / fade_max;
	    }
	    if(fade_level == fade_max)
		fade_dir = 0;
	}

	/* pulse the red colour (done after the fade, for a night effect) */
	redphase = frames % 64;
	cmap[red].r = (int)(255 * sin(redphase * M_PI / 63));

	if ((frames % palette_step) == 0) {
	    SDL_SetPalette(screen, SDL_PHYSPAL, cmap, 0, 16);
	    palette_changed = 1;
	}

	fps_now = SDL_GetTicks();
	fps_frames++;
	if (fps_now > fps_then && (fps_now - fps_then) >= 1000) {
	    fps_tenths = (int)((fps_frames * 10000) / (fps_now - fps_then));
	    fps_then = fps_now;
	    fps_frames = 0;
	    fps_needs_redraw = 1;
		if (log_fps) {
		    printf("fps %d.%d\n", fps_tenths / 10, fps_tenths % 10);
		}
	}

	if (palette_changed) {
	    updates[0].x = 0;
	    updates[0].y = 0;
	    updates[0].w = screen->w;
	    updates[0].h = screen->h;
	    nupdates = 1;
	}

	if (show_fps) {
	    fps_target = GetFPSRect(screen);
	    if (frames == 0) {
		fps_needs_redraw = 1;
	    } else {
		for (i = 0; i < nupdates; i++) {
		    if (RectsOverlap(&updates[i], &fps_target)) {
			fps_needs_redraw = 1;
			break;
		    }
		}
	    }

	    if (fps_needs_redraw) {
		DrawFPSOverlay(screen, fps_tenths, fps_fg, fps_bg, &fps_rect);
		updates[nupdates++] = fps_rect;
	    }
	}

	/* update changed areas of the screen */
	SDL_UpdateRects(screen, nupdates, updates);
	frames++;
    } while(fade_level > 0);

    printf("%d frames, %.2f fps\n",
	   frames, 1000.0 * frames / (SDL_GetTicks() - start));

    SDL_Quit();
    return 0;
}
