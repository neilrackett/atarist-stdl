/*
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: CC0-1.0
 *
 * STDL example program, dedicated to the public domain so it can
 * be used as a starting point without licence concerns.
 */
/*
 * playmus - play YM music (with optional DMA sample effects)
 *
 * STDL example, modelled on SDL_mixer's playmus but written for
 * STDL: loads an STM register stream (stdlconv midi output), plays
 * it through the YM2149 from the VBL, and - on an STE/Mega STE -
 * fires a DMA sample chunk every few seconds on top to show music
 * and effects mixing.
 *
 * Runs on a plain ST too: chunks fail cleanly, music still plays.
 */

#include <stdio.h>
#include <stdlib.h>

#include "SDL.h"
#include "SDL_mixer.h"

#define PLAY_SECONDS 20

int main(int argc, char *argv[])
{
	const char *file;
	Mix_Music *music;
	Mix_Chunk *beep;
	Uint32 start, next_beep;

	if ( SDL_Init(SDL_INIT_AUDIO) < 0 ) {
		fprintf(stderr, "Couldn't initialize SDL: %s\n",
							SDL_GetError());
		return 1;
	}
	if ( Mix_OpenAudio(MIX_DEFAULT_FREQUENCY, AUDIO_S8, 1, 512) < 0 ) {
		fprintf(stderr, "Couldn't open mixer: %s\n", Mix_GetError());
		SDL_Quit();
		return 1;
	}

	file = (argc > 1) ? argv[1] : "DEMO.STM";
	music = Mix_LoadMUS(file);
	if ( music == NULL ) {
		fprintf(stderr, "Couldn't load %s: %s\n", file,
							Mix_GetError());
		Mix_CloseAudio();
		SDL_Quit();
		return 1;
	}

	beep = Mix_LoadWAV("BEEP.WAV");
	if ( beep == NULL ) {
		printf("(no sample chunks: %s)\n", Mix_GetError());
	}

	printf("Playing %s for %d seconds...\n", file, PLAY_SECONDS);
	if ( Mix_PlayMusic(music, -1) < 0 ) {
		fprintf(stderr, "Couldn't play music: %s\n", Mix_GetError());
		Mix_FreeMusic(music);
		Mix_CloseAudio();
		SDL_Quit();
		return 1;
	}

	start = SDL_GetTicks();
	next_beep = start + 4000;
	while ( SDL_GetTicks() - start < PLAY_SECONDS * 1000 ) {
		SDL_Delay(50);
		if ( beep != NULL && SDL_GetTicks() >= next_beep ) {
			printf("beep!\n");
			Mix_PlayChannel(-1, beep, 0);
			next_beep += 4000;
		}
	}

	printf("Done.\n");
	Mix_HaltMusic();
	Mix_FreeMusic(music);
	if ( beep != NULL ) {
		Mix_FreeChunk(beep);
	}
	Mix_CloseAudio();
	SDL_Quit();
	return 0;
}
