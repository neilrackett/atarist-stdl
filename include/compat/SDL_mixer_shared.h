/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * The SDL_mixer subset's declarations (SDL_mixer.h beside this file
 * says what they are), for both of STDL's SDL 1.2 front ends: each
 * has an SDL_mixer.h that includes its own SDL.h and then this.
 */

#ifndef STDL_COMPAT_SDL_MIXER_SHARED_H
#define STDL_COMPAT_SDL_MIXER_SHARED_H

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

#endif /* STDL_COMPAT_SDL_MIXER_SHARED_H */
