/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * SDL_mixer compatibility subset.
 *
 * Music maps to STDL_Music: YM2149 register streams produced by
 * `stdlconv midi` (works on every ST). Sample chunks map to the
 * STDL_Voice mixer, one channel a voice, mixed from the VBL (STE/Mega
 * STE; on a plain ST chunk calls fail cleanly and music still
 * plays). Anything not declared here is a compile error by design.
 *
 * Chunks cost, on an 8MHz STE: 5% for one at 6258Hz, 9% for two and
 * 20% for all four; 8%, 16% and 40% at 12517; about half on a Mega STE
 * (measured, tests/hatari/mixbench.c). Mix_OpenAudio mixes at 6258 or
 * 12517, whichever is nearer the rate asked for - a PC game's 22050
 * gets 12517 - since four voices at 25033 would be the whole machine.
 * Chunks are converted to that rate as they load, at most 65535
 * frames each (about five seconds at 12517). The voice ring holds
 * 82ms at 6258 and 41ms at 12517: a VBL held off for most of that
 * while a chunk plays - a synchronous disk read can be - repeats a
 * stretch of it, so a game that loads while effects play may want
 * 6258. A chunk at full volume plays at half scale, the voice mixer's
 * room for two loud sounds at once - 6dB quieter than before v1.13.0,
 * so re-check effects balanced against the music. Mix_PlayChannel's
 * loops count is exact, counted by the voice mixer to the frame.
 * Mix_Playing reads the voice, which ends when its last frame is
 * mixed - up to three blocks (about 60ms at 6258, 30ms at 12517)
 * before that frame is heard.
 */

#ifndef STDL_COMPAT_SDL_MIXER_H
#define STDL_COMPAT_SDL_MIXER_H

#ifdef __cplusplus
extern "C" {
#endif

#include "SDL.h"

#define MIX_MAX_VOLUME     128
#define MIX_CHANNELS       4
#define MIX_DEFAULT_FREQUENCY 12517

typedef STDL_Music Mix_Music;

typedef struct Mix_Chunk {
    int      allocated;
    uint8_t *abuf;          /* signed 8-bit mono at the device rate */
    uint32_t alen;
    uint8_t  volume;        /* 0..128 */
} Mix_Chunk;

/* frequency/format/channels describe the *chunk* device; music is
 * the YM and ignores them. chunksize is accepted and ignored. */
int  Mix_OpenAudio(int frequency, uint16_t format, int channels,
                   int chunksize);
void Mix_CloseAudio(void);

/* music */
Mix_Music *Mix_LoadMUS(const char *file);
void Mix_FreeMusic(Mix_Music *music);
int  Mix_PlayMusic(Mix_Music *music, int loops);
int  Mix_HaltMusic(void);
void Mix_PauseMusic(void);
void Mix_ResumeMusic(void);
int  Mix_PausedMusic(void);
int  Mix_PlayingMusic(void);
int  Mix_VolumeMusic(int volume);

/* sample chunks */
Mix_Chunk *Mix_LoadWAV(const char *file);
void Mix_FreeChunk(Mix_Chunk *chunk);
int  Mix_PlayChannel(int channel, Mix_Chunk *chunk, int loops);
int  Mix_HaltChannel(int channel);
int  Mix_Playing(int channel);
int  Mix_Volume(int channel, int volume);
int  Mix_VolumeChunk(Mix_Chunk *chunk, int volume);

#define Mix_GetError SDL_GetError

#ifdef __cplusplus
}
#endif

#endif /* STDL_COMPAT_SDL_MIXER_H */
