/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * What sampled sound costs the CPU: the ring device (SDL_OpenAudio)
 * with a callback that does nothing, and the SDL_mixer compatibility
 * layer's chunks. A loop of fixed slices of work, pumping events
 * between them as a game's main loop would, is counted for four
 * seconds with no audio open at all; then with the ring device open
 * in the hardware's own format and in two it has to convert, and with
 * the mixer open and silent and one, two, three and four chunks
 * looping at a volume short of full, as a game's would be. The count
 * lost is the audio's share of the machine: CPU% = 100 - 100 * count
 * / idle count. Where the work outruns the machine the figure stops
 * meaning anything: over about 90%, read "all of it".
 *
 * The chunk is written as a WAV first, 8-bit unsigned mono at
 * 11025Hz - a rate no DMA mode has, as a PC game's effects would be
 * - and loaded with Mix_LoadWAV, so the load path's conversion is the
 * one a port gets.
 *
 * Results go to MIXBENCH.TXT beside the program. Needs an STE or Mega
 * STE: on a plain ST the ring device fails to open and the mixer
 * opens for music only, so every line reports the failure.
 *
 * The chunks are measured at 6258 and 12517Hz, the two rates the
 * mixer runs at; a PC game's 22050 lands on 12517.
 *
 * Build: m68k-atari-mint-gcc -O2 -Iinclude -Iinclude/compat
 *        tests/hatari/mixbench.c libstdl.a -o MIXBENCH.TOS
 */
#include <stdio.h>
#include <string.h>
#include "SDL.h"
#include "SDL_mixer.h"

#define WAV_RATE   11025
#define WAV_FRAMES 11025        /* one second */

static void put16(FILE *f, unsigned v)
{
    fputc(v & 0xFF, f);
    fputc((v >> 8) & 0xFF, f);
}

static void put32(FILE *f, unsigned long v)
{
    put16(f, (unsigned)(v & 0xFFFF));
    put16(f, (unsigned)(v >> 16));
}

/* a saw with a little movement in it, unsigned 8-bit */
static int write_wav(const char *name)
{
    FILE *f = fopen(name, "wb");
    long i;

    if (f == NULL) {
        return -1;
    }
    fwrite("RIFF", 1, 4, f);
    put32(f, 36 + WAV_FRAMES);
    fwrite("WAVEfmt ", 1, 8, f);
    put32(f, 16);
    put16(f, 1);                /* PCM */
    put16(f, 1);                /* mono */
    put32(f, WAV_RATE);
    put32(f, WAV_RATE);
    put16(f, 1);
    put16(f, 8);
    fwrite("data", 1, 4, f);
    put32(f, WAV_FRAMES);
    for (i = 0; i < WAV_FRAMES; i++) {
        fputc((int)((i * (3 + (i >> 11))) & 0xFF), f);
    }
    fclose(f);
    return 0;
}

/* work counted in slices of about 1.5ms at 8MHz, the events pumped
 * between them - often enough for the ring device's refills, and
 * seldom enough that the pump's own polling is not what is measured */
static long spin(Uint32 ms)
{
    Uint32 end = SDL_GetTicks() + ms;
    long n = 0;
    volatile int k;

    while (SDL_GetTicks() < end) {
        for (k = 0; k < 500; k++) {
        }
        n++;
        SDL_PumpEvents();
    }
    return n;
}

static long idle;
static volatile long calls;

static void empty_callback(void *ud, Uint8 *stream, int len)
{
    (void)ud; (void)stream; (void)len;
    calls++;
}

/* the ring device itself, as SDL_OpenAudio gets it, with a callback
 * that does no work: what a port pays before mixing anything. Signed
 * 8-bit at a DMA rate is written straight into the ring; any other
 * format or rate goes through the converter. */
static void bench_ring(FILE *out, int rate, Uint16 format, int channels,
                       const char *what)
{
    SDL_AudioSpec want;
    long busy;

    memset(&want, 0, sizeof(want));
    want.freq = rate;
    want.format = format;
    want.channels = channels;
    want.samples = 1024;
    want.callback = empty_callback;
    if (SDL_OpenAudio(&want, NULL) < 0) {
        fprintf(out, "ring %5d %s: open failed (%s)\n", rate, what,
                SDL_GetError());
        return;
    }
    SDL_PauseAudio(0);
    spin(500);
    calls = 0;
    busy = spin(4000);
    fprintf(out, "ring %5d Hz %-9s empty callback: %3ld%%, %ld calls/s\n",
            rate, what, 100 - busy * 100 / idle, calls / 4);
    SDL_CloseAudio();
}

static void bench(FILE *out, int rate)
{
    Mix_Chunk *chunk;
    long busy;
    int c;

    if (Mix_OpenAudio(rate, AUDIO_S8, 1, 1024) < 0) {
        fprintf(out, "%5d: open failed (%s)\n", rate, Mix_GetError());
        return;
    }
    chunk = Mix_LoadWAV("MIXBENCH.WAV");
    if (chunk == NULL) {
        fprintf(out, "%5d: no chunks (%s)\n", rate, Mix_GetError());
        Mix_CloseAudio();
        return;
    }
    spin(500);                  /* settle */
    busy = spin(4000);
    fprintf(out, "%5d Hz asked:  open %3ld%%", rate, 100 - busy * 100 / idle);
    for (c = 0; c < MIX_CHANNELS; c++) {
        Mix_Volume(c, 100);
        Mix_PlayChannel(c, chunk, -1);
        busy = spin(4000);
        fprintf(out, "  %d ch %3ld%%", c + 1, 100 - busy * 100 / idle);
        fflush(out);
    }
    fprintf(out, "\n");
    Mix_HaltChannel(-1);
    Mix_FreeChunk(chunk);
    Mix_CloseAudio();
}

int main(int argc, char *argv[])
{
    FILE *out;

    (void)argc; (void)argv;
    if (SDL_Init(SDL_INIT_AUDIO | SDL_INIT_VIDEO) < 0) {
        return 1;
    }
    out = fopen("MIXBENCH.TXT", "w");
    if (out == NULL) {
        return 1;
    }
    if (write_wav("MIXBENCH.WAV") < 0) {
        fprintf(out, "cannot write the WAV\n");
    } else {
        spin(500);
        idle = spin(4000);      /* no audio open at all */
        bench_ring(out, 6258, AUDIO_S8, 1, "S8 mono");
        bench_ring(out, 12517, AUDIO_S8, 1, "S8 mono");
        bench_ring(out, 12517, AUDIO_U8, 1, "U8 mono");
        bench_ring(out, 22050, AUDIO_S16LSB, 2, "S16 st.");
        bench(out, 6258);
        bench(out, 12517);
    }
    fprintf(out, "done\n");
    fclose(out);
    SDL_Quit();
    return 0;
}
