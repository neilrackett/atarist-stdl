/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * WAV files: uncompressed PCM, 8 or 16 bits, mono or stereo, from any
 * stream, its chunks walked in order (each padded to an even size) to
 * the fmt and the data. SDL2 decodes ADPCM too; stdlconv converts it
 * offline.
 */

#include "internal.h"

static Uint32 le32(const Uint8 *p)
{
    return (Uint32)p[0] | (Uint32)p[1] << 8 | (Uint32)p[2] << 16 |
           (Uint32)p[3] << 24;
}

static Uint16 le16(const Uint8 *p)
{
    return (Uint16)(p[0] | p[1] << 8);
}

SDL_AudioSpec *SDL_LoadWAV_RW(SDL_RWops *src, int freesrc, SDL_AudioSpec *spec,
                              Uint8 **audio_buf, Uint32 *audio_len)
{
    Uint8 h[12], c[8], fmt[16];
    int have_fmt = 0;
    SDL_AudioSpec *ret = NULL;
    if (!src) {
        return NULL;            /* the stream's own error stands */
    }
    if (!spec || !audio_buf || !audio_len) {
        SDL_InvalidParamError("spec");
        goto done;
    }
    *audio_buf = NULL;
    *audio_len = 0;
    if (SDL_RWread(src, h, 12, 1) != 1 || memcmp(h, "RIFF", 4) != 0 ||
        memcmp(h + 8, "WAVE", 4) != 0) {
        SDL_SetError("Unrecognized file type (not WAVE)");
        goto done;
    }
    for (;;) {
        Uint32 n;
        if (SDL_RWread(src, c, 8, 1) != 1) {
            SDL_SetError("No data chunk in the WAVE file");
            goto done;
        }
        n = le32(c + 4);
        if (memcmp(c, "fmt ", 4) == 0) {
            if (n < 16 || SDL_RWread(src, fmt, 16, 1) != 1) {
                SDL_SetError("Bad fmt chunk in the WAVE file");
                goto done;
            }
            n -= 16;
            have_fmt = 1;
        } else if (memcmp(c, "data", 4) == 0) {
            const int channels = have_fmt ? le16(fmt + 2) : 0;
            const int bits = have_fmt ? le16(fmt + 14) : 0;
            Uint32 got, frame;
            if (!have_fmt || le16(fmt) != 1) {
                SDL_SetError("WAVE files here are PCM, uncompressed");
                goto done;
            }
            if ((channels != 1 && channels != 2) || (bits != 8 && bits != 16)) {
                SDL_SetError("WAVE files here are 8 or 16 bits, mono or"
                             " stereo, not %d bits in %d channels",
                             bits, channels);
                goto done;
            }
            *audio_buf = (Uint8 *)malloc(n ? n : 1);
            if (!*audio_buf) {
                SDL_OutOfMemory();
                goto done;
            }
            got = (Uint32)SDL_RWread(src, *audio_buf, 1, n);
            frame = (Uint32)channels * (Uint32)(bits / 8);
            memset(spec, 0, sizeof(*spec));
            spec->freq = (int)le32(fmt + 4);
            spec->format = bits == 8 ? AUDIO_U8 : AUDIO_S16LSB;
            spec->channels = (Uint8)channels;
            spec->samples = 4096;
            *audio_len = got - got % frame;
            ret = spec;
            goto done;
        }
        /* a chunk is padded to an even size */
        if (SDL_RWseek(src, (Sint64)(n + (n & 1)), RW_SEEK_CUR) < 0) {
            goto done;
        }
    }
done:
    if (!ret && audio_buf && *audio_buf) {
        free(*audio_buf);
        *audio_buf = NULL;
    }
    if (freesrc) {
        SDL_RWclose(src);
    }
    return ret;
}

void SDL_FreeWAV(Uint8 *audio_buf)
{
    free(audio_buf);
}
