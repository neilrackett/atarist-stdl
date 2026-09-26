/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * SDL_mixer compatibility subset: YM music through STDL_Music,
 * sample chunks on the STDL_Voice mixer, a channel a voice. What that
 * costs and changes for a port is in include/compat/SDL_mixer.h.
 * Before v1.13.0 the chunks were mixed in C over the STDL_OpenAudio
 * ring, from the event pump.
 */

#include <stdlib.h>
#include "stdl_internal.h"
#include <SDL.h>
#include <SDL_mixer.h>

static int mix_open;            /* Mix_OpenAudio succeeded          */
static int device_freq;         /* the voices' rate; 0 = no chunks  */

/* what each channel plays, for Mix_FreeChunk and Mix_VolumeChunk,
 * and its volume (0..128) */
static Mix_Chunk *channel_chunk[MIX_CHANNELS];
static uint8_t channel_volume[MIX_CHANNELS];

/* SDL_mixer's two volumes, 0..128 each, to a voice's 0..64 */
static uint8_t voice_volume(const Mix_Chunk *chunk, int channel)
{
    return (uint8_t)(stdl_mul16(chunk->volume, channel_volume[channel]) >> 8);
}

int Mix_OpenAudio(int frequency, uint16_t format, int channelcount,
                  int chunksize)
{
    int c, rate;

    (void)format; (void)channelcount; (void)chunksize;
    if (mix_open) {
        return 0;
    }
    STDL_Init(STDL_INIT_AUDIO);
    for (c = 0; c < MIX_CHANNELS; c++) {
        channel_chunk[c] = NULL;
        channel_volume[c] = MIX_MAX_VOLUME;
    }

    /* the DMA rate nearest the one asked for, but no faster than
     * 12517 (a PC game's 22050 gets 12517); on a plain ST this fails
     * and only music is available */
    if (frequency <= 0) {
        frequency = MIX_DEFAULT_FREQUENCY;
    }
    rate = stdl_dma_rates[stdl_dma_nearest(frequency < 12517 ? frequency : 12517)];
    device_freq = (STDL_OpenVoices(rate) == 0) ? rate : 0;
    mix_open = 1;
    return 0;
}

void Mix_CloseAudio(void)
{
    if (!mix_open) {
        return;
    }
    STDL_HaltMusic();
    if (device_freq != 0) {
        STDL_CloseVoices();
        device_freq = 0;
    }
    mix_open = 0;
}

/* ---------------------------------------------------------------- */
/* music                                                            */

Mix_Music *Mix_LoadMUS(const char *file)
{
    return STDL_LoadMusic(file);
}

void Mix_FreeMusic(Mix_Music *music)
{
    STDL_FreeMusic(music);
}

int Mix_PlayMusic(Mix_Music *music, int loops)
{
    return STDL_PlayMusic(music, loops);
}

int Mix_HaltMusic(void)
{
    STDL_HaltMusic();
    return 0;
}

void Mix_PauseMusic(void)   { STDL_PauseMusic(); }
void Mix_ResumeMusic(void)  { STDL_ResumeMusic(); }
int  Mix_PausedMusic(void)  { return STDL_PausedMusic(); }
int  Mix_PlayingMusic(void) { return STDL_PlayingMusic(); }

int Mix_VolumeMusic(int volume)
{
    return STDL_VolumeMusic(volume);
}

/* ---------------------------------------------------------------- */
/* chunks                                                           */

/* convert any loadable WAV to signed 8-bit mono at the device rate,
 * so a voice plays it at step 1 */
Mix_Chunk *Mix_LoadWAV(const char *file)
{
    STDL_AudioSpec spec;
    uint8_t *raw;
    uint32_t rawlen;
    Mix_Chunk *chunk;
    uint32_t in_frames, out_frames;
    int frame_bytes;

    if (device_freq == 0) {
        STDL_SetError("no DMA sound hardware for sample chunks");
        return NULL;
    }
    if (STDL_LoadWAV(file, &spec, &raw, &rawlen) == NULL) {
        return NULL;
    }
    frame_bytes = spec.channels * ((spec.format & 0xFF) / 8);
    in_frames = rawlen / (uint32_t)frame_bytes;
    out_frames = (uint32_t)((uint64_t)in_frames * (uint32_t)device_freq
                            / (uint32_t)spec.freq);
    if (out_frames > 0xFFFF) {
        /* a voice's longest (SDL_mixer.h): the chunk is cut short,
         * converting only the source that covers what is kept -
         * converting all of it into 65535 frames would speed it up */
        out_frames = 0xFFFF;
        in_frames = (uint32_t)((uint64_t)out_frames * (uint32_t)spec.freq
                               / (uint32_t)device_freq);
    }
    chunk = calloc(1, sizeof(Mix_Chunk));
    if (chunk == NULL || out_frames == 0) {
        free(chunk);
        STDL_FreeWAV(raw);
        STDL_SetError("out of memory");
        return NULL;
    }
    chunk->abuf = malloc(out_frames);
    if (chunk->abuf == NULL) {
        free(chunk);
        STDL_FreeWAV(raw);
        STDL_SetError("out of memory");
        return NULL;
    }
    stdl_audio_convert((int8_t *)chunk->abuf, out_frames, raw,
                       in_frames, spec.format, spec.channels, 1);
    chunk->alen = out_frames;
    chunk->allocated = 1;
    chunk->volume = MIX_MAX_VOLUME;
    STDL_FreeWAV(raw);
    return chunk;
}

void Mix_FreeChunk(Mix_Chunk *chunk)
{
    int c;

    if (chunk == NULL) {
        return;
    }
    /* the voice mixer reads the samples live: stop it first */
    for (c = 0; c < MIX_CHANNELS; c++) {
        if (channel_chunk[c] == chunk) {
            STDL_StopVoice(c);
            channel_chunk[c] = NULL;
        }
    }
    free(chunk->abuf);
    free(chunk);
}

int Mix_PlayChannel(int channel, Mix_Chunk *chunk, int loops)
{
    int c;

    if (device_freq == 0) {
        STDL_SetError("no DMA sound hardware for sample chunks");
        return -1;
    }
    if (chunk == NULL) {
        return -1;
    }
    if (channel < 0) {
        for (c = 0; c < MIX_CHANNELS; c++) {
            if (!STDL_VoiceActive(c)) {
                channel = c;
                break;
            }
        }
        if (channel < 0) {
            channel = 0;        /* all busy: steal channel 0 */
        }
    }
    if (channel >= MIX_CHANNELS) {
        STDL_SetError("no such mixer channel");
        return -1;
    }
    channel_chunk[channel] = chunk;
    /* the whole chunk is the loop: a pass, then `loops` more (-1:
     * until halted), counted by the voice mixer where it wraps */
    stdl_voice_start(channel, (const int8_t *)chunk->abuf, chunk->alen,
                     0, (loops != 0) ? chunk->alen : 0, (uint32_t)device_freq,
                     voice_volume(chunk, channel), loops);
    return channel;
}

int Mix_HaltChannel(int channel)
{
    int c;

    for (c = 0; c < MIX_CHANNELS; c++) {
        if (channel < 0 || channel == c) {
            STDL_StopVoice(c);
        }
    }
    return 0;
}

int Mix_Playing(int channel)
{
    int c, n = 0;

    if (channel >= 0) {
        return (channel < MIX_CHANNELS && STDL_VoiceActive(channel))
            ? 1 : 0;
    }
    for (c = 0; c < MIX_CHANNELS; c++) {
        n += STDL_VoiceActive(c) ? 1 : 0;
    }
    return n;
}

static uint8_t clamp_vol(int volume)
{
    return (uint8_t)(volume > MIX_MAX_VOLUME ? MIX_MAX_VOLUME
                                             : volume);
}

/* a volume change heard at once on whatever is playing (an idle
 * voice takes it too, and its next start replaces it) */
static void refresh_volume(int channel)
{
    if (channel_chunk[channel] != NULL) {
        STDL_SetVoiceVolume(channel,
                            voice_volume(channel_chunk[channel], channel));
    }
}

int Mix_Volume(int channel, int volume)
{
    int c, old = 0;

    if (channel >= MIX_CHANNELS) {
        return 0;
    }
    for (c = 0; c < MIX_CHANNELS; c++) {
        if (channel < 0 || channel == c) {
            old = channel_volume[c];
            if (volume >= 0) {
                channel_volume[c] = clamp_vol(volume);
                refresh_volume(c);
            }
        }
    }
    return old;
}

int Mix_VolumeChunk(Mix_Chunk *chunk, int volume)
{
    int c, old;

    if (chunk == NULL) {
        return 0;
    }
    old = chunk->volume;
    if (volume >= 0) {
        chunk->volume = clamp_vol(volume);
        for (c = 0; c < MIX_CHANNELS; c++) {
            if (channel_chunk[c] == chunk) {
                refresh_volume(c);
            }
        }
    }
    return old;
}
