/*
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: CC0-1.0
 *
 * STDL port of a public-domain SDL 1.2 test program; the port is
 * dedicated to the public domain like the original.
 */
/* Program to load a wave file and loop playing it using SDL sound
 *
 * loopwaves.c is much more robust in handling WAVE files --
 * This is only for simple WAVEs
 *
 * SDL 1.2 test program (public domain), ported to STDL.
 * Port notes:
 *  - SAMPLE.WAV is the original MS-ADPCM sample.wav decoded and
 *    resampled offline to unsigned 8-bit mono at 12517 Hz (an exact
 *    STE DMA rate) with `stdlconv wav`
 *  - there are no signals on TOS, so the loop stops after 15
 *    seconds instead of waiting for SIGINT
 *  - audio needs the STE/Mega STE DMA hardware; on a plain ST
 *    SDL_OpenAudio fails cleanly and the program reports it
 */

#include <stdio.h>
#include <stdlib.h>

#include "SDL.h"
#include "SDL_audio.h"

struct {
	SDL_AudioSpec spec;
	Uint8   *sound;			/* Pointer to wave data */
	Uint32   soundlen;		/* Length of wave data */
	int      soundpos;		/* Current play position */
} wave;


/* Call this instead of exit(), so we can clean up SDL: atexit() is evil. */
static void quit(int rc)
{
	SDL_Quit();
	exit(rc);
}


void SDLCALL fillerup(void *unused, Uint8 *stream, int len)
{
	Uint8 *waveptr;
	int    waveleft;

	(void)unused;

	/* Set up the pointers */
	waveptr = wave.sound + wave.soundpos;
	waveleft = wave.soundlen - wave.soundpos;

	/* Go! */
	while ( waveleft <= len ) {
		SDL_memcpy(stream, waveptr, waveleft);
		stream += waveleft;
		len -= waveleft;
		waveptr = wave.sound;
		waveleft = wave.soundlen;
		wave.soundpos = 0;
	}
	SDL_memcpy(stream, waveptr, len);
	wave.soundpos += len;
}

static int done = 0;

int main(int argc, char *argv[])
{
	char name[32];
	const char *file;
	Uint32 start;

	/* Load the SDL library */
	if ( SDL_Init(SDL_INIT_AUDIO) < 0 ) {
		fprintf(stderr, "Couldn't initialize SDL: %s\n",SDL_GetError());
		return(1);
	}
	file = (argc < 2) ? "SAMPLE.WAV" : argv[1];
	/* Load the wave file into memory */
	if ( SDL_LoadWAV(file, &wave.spec, &wave.sound, &wave.soundlen) == NULL ) {
		fprintf(stderr, "Couldn't load %s: %s\n", file, SDL_GetError());
		quit(1);
	}
	printf("Loaded %s: %lu bytes, %d Hz, %d channel(s)\n",
		file, (unsigned long)wave.soundlen, wave.spec.freq,
		wave.spec.channels);

	wave.spec.callback = fillerup;

	/* Initialize fillerup() variables */
	if ( SDL_OpenAudio(&wave.spec, NULL) < 0 ) {
		fprintf(stderr, "Couldn't open audio: %s\n", SDL_GetError());
		SDL_FreeWAV(wave.sound);
		quit(2);
	}
	SDL_PauseAudio(0);

	/* Let the audio run */
	printf("Using audio driver: %s\n", SDL_AudioDriverName(name, 32));
	start = SDL_GetTicks();
	while ( ! done && (SDL_GetAudioStatus() == SDL_AUDIO_PLAYING) ) {
		SDL_Delay(1000);
		/* STDL port: no signals on TOS - stop after 15 seconds */
		if ( SDL_GetTicks() - start > 15000 ) {
			done = 1;
		}
	}
	printf("Done playing\n");

	/* Clean up on signal */
	SDL_CloseAudio();
	SDL_FreeWAV(wave.sound);
	SDL_Quit();
	return(0);
}
