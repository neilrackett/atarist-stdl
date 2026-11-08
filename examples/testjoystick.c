/*
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: CC0-1.0
 *
 * STDL port of a public-domain SDL 1.2 test program; the port is
 * dedicated to the public domain like the original.
 */
/* Simple program to test the SDL joystick routines
 *
 * SDL 1.2 test program (public domain), ported to STDL.
 * Port notes:
 *  - the screen is 320x200 and the fill colours are palette
 *    indices (15 white, 0 black)
 *  - the watcher runs unconditionally: TOS programs launched from
 *    the desktop get no argv to select a stick with
 *  - the ST's joystick port 1 is stick 0: 2 digital axes, 1 button.
 *    Note the fire button shares a line with the right mouse button
 *  - with an Xpad pad, the shoulder buttons show STDL_PadRumble
 *  - keys 1 to 6 ask STDL for that many joysticks
 *    (STDL_SetNumJoysticks), for two players or more: 2 makes the
 *    mouse port stick 1 (the mouse goes off until 1 again), and more
 *    add Xpad pads where there are those - merged, pads 2 and 3 as
 *    sticks 2 and 3 (pads 0 and 1 are in sticks 0 and 1); apart, all
 *    four as sticks 2 to 5. M merges or parts them
 *    (STDL_UseJoystickMerge). SDL 1.2 has no hotplug, so the program
 *    closes its sticks and opens what SDL_NumJoysticks says now. Each
 *    stick draws in its own colour; with one, the screen is the
 *    original test's
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "SDL.h"

#define SCREEN_WIDTH	320
#define SCREEN_HEIGHT	200

/* STDL: up to six sticks, each in a colour of the default palette -
 * white, then light green, blue, red, cyan and magenta */
static SDL_Joystick *joysticks[STDL_MAX_JOYSTICKS];
static int num_joysticks;
static const Uint8 colour[STDL_MAX_JOYSTICKS] = { 15, 10, 12, 9, 14, 13 };

static void PrintJoystick(int i, SDL_Joystick *joystick)
{
	const char *name = SDL_JoystickName(i);

	printf("Joystick %d: %s\n", i, name ? name : "Unknown Joystick");
	printf("Joystick has %d axes, %d hats, %d balls, and %d buttons\n",
	       SDL_JoystickNumAxes(joystick), SDL_JoystickNumHats(joystick),
	       SDL_JoystickNumBalls(joystick),SDL_JoystickNumButtons(joystick));
}

/* STDL: ask for n sticks, and open as many as there are now */
static void OpenJoysticks(SDL_Surface *screen, int n)
{
	int i;

	for ( i=0; i<num_joysticks; ++i ) {
		SDL_JoystickClose(joysticks[i]);
	}
	if ( n > 0 ) {
		printf("Asked for %d joysticks, got %d\n", n,
		       STDL_SetNumJoysticks(n));
	}
	num_joysticks = 0;
	for ( i=0; i<SDL_NumJoysticks(); ++i ) {
		joysticks[i] = SDL_JoystickOpen(i);
		if ( joysticks[i] != NULL ) {
			PrintJoystick(i, joysticks[i]);
			num_joysticks = i + 1;
		}
	}
	SDL_FillRect(screen, NULL, 0);
	SDL_UpdateRect(screen, 0, 0, 0, 0);
}

void WatchJoystick(SDL_Joystick *joystick)
{
	SDL_Surface *screen;
	int i, j, done;
	SDL_Event event;
	int x, y, draw;
	SDL_Rect axis_area[STDL_MAX_JOYSTICKS][2];

	/* Set a video mode to display joystick axis position */
	screen = SDL_SetVideoMode(SCREEN_WIDTH, SCREEN_HEIGHT, 4, 0);
	if ( screen == NULL ) {
		fprintf(stderr, "Couldn't set video mode: %s\n",SDL_GetError());
		return;
	}

	/* Print info about the joystick we are watching */
	printf("Watching joystick %d\n", SDL_JoystickIndex(joystick));
	PrintJoystick(SDL_JoystickIndex(joystick), joystick);
	joysticks[0] = joystick;
	num_joysticks = 1;

	/* Initialize drawing rectangles */
	memset(axis_area, 0, (sizeof axis_area));
	draw = 0;

	/* Loop, getting joystick events! */
	done = 0;
	while ( ! done ) {
		while ( SDL_PollEvent(&event) ) {
			switch (event.type) {
			    case SDL_JOYAXISMOTION:
				printf("Joystick %d axis %d value: %d\n",
				       event.jaxis.which,
				       event.jaxis.axis,
				       event.jaxis.value);
				break;
			    case SDL_JOYBUTTONDOWN:
				printf("Joystick %d button %d down\n",
				       event.jbutton.which,
				       event.jbutton.button);
				/* STDL: a pad's shoulders, buttons 4 and 5,
				 * pulse its heavy and light motors in turn -
				 * each stick's own pad. */
				if (event.jbutton.button == 4 ||
				    event.jbutton.button == 5) {
					int heavy = event.jbutton.button == 4;

					printf("Rumble %s: %s\n",
					       heavy ? "heavy" : "light",
					       STDL_PadRumble(event.jbutton.which,
					                       heavy ? 200 : 0,
					                       heavy ? 0 : 200,
					                       300) == 0 ?
					       "on" : "not supported");
				}
				break;
			    case SDL_JOYBUTTONUP:
				printf("Joystick %d button %d up\n",
				       event.jbutton.which,
				       event.jbutton.button);
				break;
			    case SDL_KEYDOWN:
				/* STDL: 1 to 6 sticks */
				if ( event.key.keysym.sym >= SDLK_1 &&
				     event.key.keysym.sym <= SDLK_6 ) {
					OpenJoysticks(screen,
					    event.key.keysym.sym - SDLK_0);
					memset(axis_area, 0, (sizeof axis_area));
					break;
				}
				/* STDL: the pads merged into the ports' sticks,
				 * or sticks of their own */
				if ( event.key.keysym.sym == SDLK_m ) {
					printf("Pads %s\n",
					       STDL_UseJoystickMerge(
					         !STDL_UseJoystickMerge(-1)) ?
					       "apart" : "merged");
					OpenJoysticks(screen, 0);
					memset(axis_area, 0, (sizeof axis_area));
					break;
				}
				if ( event.key.keysym.sym != SDLK_ESCAPE ) {
					break;
				}
				/* Fall through to signal quit */
			    case SDL_QUIT:
				done = 1;
				break;
			    default:
				break;
			}
		}
		/* Update visual joystick state */
		for ( j=0; j<num_joysticks; ++j ) {
			SDL_Joystick *stick = joysticks[j];

			for ( i=0; i<SDL_JoystickNumButtons(stick); ++i ) {
				SDL_Rect area;

				/* one stick: the original's row of 32x32
				 * boxes; more: a row of small ones each,
				 * the first stick's at the bottom */
				if ( num_joysticks == 1 ) {
					area.x = i*34;
					area.y = SCREEN_HEIGHT-34;
					area.w = 32;
					area.h = 32;
				} else {
					area.x = i*18;
					area.y = SCREEN_HEIGHT-10*(j+1);
					area.w = 16;
					area.h = 8;
				}
				if (SDL_JoystickGetButton(stick, i) == SDL_PRESSED) {
					SDL_FillRect(screen, &area, colour[j]);
				} else {
					SDL_FillRect(screen, &area, 0);
				}
				SDL_UpdateRects(screen, 1, &area);
			}

			/* Erase previous axes */
			SDL_FillRect(screen, &axis_area[j][draw], 0);
		}
		draw = !draw;
		for ( j=0; j<num_joysticks; ++j ) {
			SDL_Joystick *stick = joysticks[j];

			/* Draw the X/Y axis */
			x = (((int)SDL_JoystickGetAxis(stick, 0))+32768);
			x *= SCREEN_WIDTH;
			x /= 65535;
			if ( x < 0 ) {
				x = 0;
			} else
			if ( x > (SCREEN_WIDTH-16) ) {
				x = SCREEN_WIDTH-16;
			}
			y = (((int)SDL_JoystickGetAxis(stick, 1))+32768);
			y *= SCREEN_HEIGHT;
			y /= 65535;
			if ( y < 0 ) {
				y = 0;
			} else
			if ( y > (SCREEN_HEIGHT-16) ) {
				y = SCREEN_HEIGHT-16;
			}
			axis_area[j][draw].x = (Sint16)x;
			axis_area[j][draw].y = (Sint16)y;
			axis_area[j][draw].w = 16;
			axis_area[j][draw].h = 16;
			SDL_FillRect(screen, &axis_area[j][draw], colour[j]);

			SDL_UpdateRects(screen, 2, axis_area[j]);
		}
	}
	for ( j=1; j<num_joysticks; ++j ) {
		SDL_JoystickClose(joysticks[j]);
	}
}

int main(int argc, char *argv[])
{
	const char *name;
	int i;
	SDL_Joystick *joystick;

	/* Initialize SDL (Note: video is required to start event loop) */
	if ( SDL_Init(SDL_INIT_VIDEO|SDL_INIT_JOYSTICK) < 0 ) {
		fprintf(stderr, "Couldn't initialize SDL: %s\n",SDL_GetError());
		exit(1);
	}

	/* Print information about the joysticks */
	printf("There are %d joysticks attached\n", SDL_NumJoysticks());
	for ( i=0; i<SDL_NumJoysticks(); ++i ) {
		name = SDL_JoystickName(i);
		printf("Joystick %d: %s\n",i,name ? name : "Unknown Joystick");
		joystick = SDL_JoystickOpen(i);
		if (joystick == NULL) {
			fprintf(stderr, "SDL_JoystickOpen(%d) failed: %s\n", i, SDL_GetError());
		} else {
			printf("       axes: %d\n", SDL_JoystickNumAxes(joystick));
			printf("      balls: %d\n", SDL_JoystickNumBalls(joystick));
			printf("       hats: %d\n", SDL_JoystickNumHats(joystick));
			printf("    buttons: %d\n", SDL_JoystickNumButtons(joystick));
			SDL_JoystickClose(joystick);
		}
	}

	/* STDL port: no argv from the desktop - watch stick 0 */
	joystick = SDL_JoystickOpen(0);
	if ( joystick == NULL ) {
		printf("Couldn't open joystick 0: %s\n", SDL_GetError());
	} else {
		WatchJoystick(joystick);
		SDL_JoystickClose(joystick);
	}
	SDL_QuitSubSystem(SDL_INIT_VIDEO|SDL_INIT_JOYSTICK);
	SDL_Quit();

	return(0);
}
