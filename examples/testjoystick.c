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
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "SDL.h"

#define SCREEN_WIDTH	320
#define SCREEN_HEIGHT	200

void WatchJoystick(SDL_Joystick *joystick)
{
	SDL_Surface *screen;
	const char *name;
	int i, done;
	SDL_Event event;
	int x, y, draw;
	SDL_Rect axis_area[2];

	/* Set a video mode to display joystick axis position */
	screen = SDL_SetVideoMode(SCREEN_WIDTH, SCREEN_HEIGHT, 4, 0);
	if ( screen == NULL ) {
		fprintf(stderr, "Couldn't set video mode: %s\n",SDL_GetError());
		return;
	}

	/* Print info about the joystick we are watching */
	name = SDL_JoystickName(SDL_JoystickIndex(joystick));
	printf("Watching joystick %d: (%s)\n", SDL_JoystickIndex(joystick),
	       name ? name : "Unknown Joystick");
	printf("Joystick has %d axes, %d hats, %d balls, and %d buttons\n",
	       SDL_JoystickNumAxes(joystick), SDL_JoystickNumHats(joystick),
	       SDL_JoystickNumBalls(joystick),SDL_JoystickNumButtons(joystick));

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
				break;
			    case SDL_JOYBUTTONUP:
				printf("Joystick %d button %d up\n",
				       event.jbutton.which,
				       event.jbutton.button);
				break;
			    case SDL_KEYDOWN:
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
		for ( i=0; i<SDL_JoystickNumButtons(joystick); ++i ) {
			SDL_Rect area;

			area.x = i*34;
			area.y = SCREEN_HEIGHT-34;
			area.w = 32;
			area.h = 32;
			if (SDL_JoystickGetButton(joystick, i) == SDL_PRESSED) {
				SDL_FillRect(screen, &area, 15);
			} else {
				SDL_FillRect(screen, &area, 0);
			}
			SDL_UpdateRects(screen, 1, &area);
		}

		/* Erase previous axes */
		SDL_FillRect(screen, &axis_area[draw], 0);

		/* Draw the X/Y axis */
		draw = !draw;
		x = (((int)SDL_JoystickGetAxis(joystick, 0))+32768);
		x *= SCREEN_WIDTH;
		x /= 65535;
		if ( x < 0 ) {
			x = 0;
		} else
		if ( x > (SCREEN_WIDTH-16) ) {
			x = SCREEN_WIDTH-16;
		}
		y = (((int)SDL_JoystickGetAxis(joystick, 1))+32768);
		y *= SCREEN_HEIGHT;
		y /= 65535;
		if ( y < 0 ) {
			y = 0;
		} else
		if ( y > (SCREEN_HEIGHT-16) ) {
			y = SCREEN_HEIGHT-16;
		}
		axis_area[draw].x = (Sint16)x;
		axis_area[draw].y = (Sint16)y;
		axis_area[draw].w = 16;
		axis_area[draw].h = 16;
		SDL_FillRect(screen, &axis_area[draw], 15);

		SDL_UpdateRects(screen, 2, axis_area);
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
