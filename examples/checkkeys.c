/*
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: CC0-1.0
 *
 * STDL port of a public-domain SDL 1.2 test program; the port is
 * dedicated to the public domain like the original.
 */
/* Simple program:  Loop, watching keystrokes
   Note that you need to call SDL_PollEvent() or SDL_WaitEvent() to
   pump the event loop and catch keystrokes.

   SDL 1.2 test program (public domain), ported to STDL.
   Port notes: the UTF-8 iconv path is replaced with plain ASCII
   output (the ST keyboard tables are 8-bit), everything else is
   unchanged. Exercises the raw IKBD handler: real key-up events,
   modifiers, unicode translation and key repeat.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "SDL.h"

/* Call this instead of exit(), so we can clean up SDL: atexit() is evil. */
static void quit(int rc)
{
	SDL_Quit();
	exit(rc);
}

static void print_modifiers(void)
{
	int mod;
	printf(" modifiers:");
	mod = SDL_GetModState();
	if(!mod) {
		printf(" (none)");
		return;
	}
	if(mod & KMOD_LSHIFT)
		printf(" LSHIFT");
	if(mod & KMOD_RSHIFT)
		printf(" RSHIFT");
	if(mod & KMOD_LCTRL)
		printf(" LCTRL");
	if(mod & KMOD_RCTRL)
		printf(" RCTRL");
	if(mod & KMOD_LALT)
		printf(" LALT");
	if(mod & KMOD_RALT)
		printf(" RALT");
	if(mod & KMOD_LMETA)
		printf(" LMETA");
	if(mod & KMOD_RMETA)
		printf(" RMETA");
	if(mod & KMOD_NUM)
		printf(" NUM");
	if(mod & KMOD_CAPS)
		printf(" CAPS");
	if(mod & KMOD_MODE)
		printf(" MODE");
}

static void PrintKey(SDL_keysym *sym, int pressed)
{
	/* Print the keycode, name and state */
	if ( sym->sym ) {
		printf("Key %s:  %d-%s (scancode = %d [0x%x])", pressed ?  "pressed" : "released",
					sym->sym, SDL_GetKeyName(sym->sym), sym->scancode, sym->scancode);
	} else {
		printf("Unknown Key (scancode = %d [0x%x]) %s ", sym->scancode, sym->scancode,
					pressed ?  "pressed" : "released");
	}

	/* Print the translated character, if one exists */
	if ( sym->unicode ) {
		/* Is it a control-character? */
		if ( sym->unicode < ' ' ) {
			printf(" (^%c)", sym->unicode+'@');
		} else if ( sym->unicode < 127 ) {
			printf(" '%c' (0x%.4X)", (char)sym->unicode,
						(int)sym->unicode);
		} else {
			printf(" (0x%.4X)", (int)sym->unicode);
		}
	}
	print_modifiers();
	printf("\n");
}

int main(int argc, char *argv[])
{
	SDL_Event event;
	int done;
	Uint32 videoflags;

	/* Initialize SDL */
	if ( SDL_Init(SDL_INIT_VIDEO) < 0 ) {
		fprintf(stderr, "Couldn't initialize SDL: %s\n",SDL_GetError());
		return(1);
	}

	videoflags = SDL_SWSURFACE;
	while( argc > 1 ) {
		--argc;
		if ( argv[argc] && !strcmp(argv[argc], "-fullscreen") ) {
			videoflags |= SDL_FULLSCREEN;
		} else {
			fprintf(stderr, "Usage: %s [-fullscreen]\n", argv[0]);
			quit(1);
		}
	}

	/* Set video mode (becomes 320x200x4 on the ST) */
	if ( SDL_SetVideoMode(640, 480, 0, videoflags) == NULL ) {
		fprintf(stderr, "Couldn't set video mode: %s\n",
							SDL_GetError());
		quit(2);
	}

	/* Enable UNICODE translation for keyboard input */
	SDL_EnableUNICODE(1);

	/* Enable auto repeat for keyboard input */
	SDL_EnableKeyRepeat(SDL_DEFAULT_REPEAT_DELAY,
	                    SDL_DEFAULT_REPEAT_INTERVAL);

	printf("Press keys to see events (mouse button quits):\n");

	/* Watch keystrokes */
	done = 0;
	while ( !done ) {
		/* Check for events */
		SDL_WaitEvent(&event);
		switch (event.type) {
			case SDL_KEYDOWN:
				PrintKey(&event.key.keysym, 1);
				break;
			case SDL_KEYUP:
				PrintKey(&event.key.keysym, 0);
				break;
			case SDL_MOUSEBUTTONDOWN:
				/* Any button press quits the app... */
			case SDL_QUIT:
				done = 1;
				break;
			default:
				break;
		}
	}

	SDL_Quit();
	return(0);
}
