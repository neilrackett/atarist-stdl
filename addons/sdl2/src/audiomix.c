/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * Mixing, SDL2's arithmetic: the source scaled by volume / 128 (toward
 * zero, as SDL2 divides), added and clamped, in the formats the
 * device takes. SDL_MixAudio uses the open device's.
 */

#include "internal.h"

void SDL_MixAudioFormat(Uint8 *dst, const Uint8 *src, SDL_AudioFormat format,
                        Uint32 len, int volume)
{
    Uint32 i;
    if (volume == 0) {
        return;
    }
    switch (format) {
    case AUDIO_U8:
        for (i = 0; i < len; i++) {
            int v = ((src[i] - 128) * volume) / SDL_MIX_MAXVOLUME + dst[i];
            dst[i] = (Uint8)(v < 0 ? 0 : v > 255 ? 255 : v);
        }
        break;
    case AUDIO_S8:
        for (i = 0; i < len; i++) {
            int v = ((Sint8)src[i] * volume) / SDL_MIX_MAXVOLUME + (Sint8)dst[i];
            dst[i] = (Uint8)(v < -128 ? -128 : v > 127 ? 127 : v);
        }
        break;
    case AUDIO_S16LSB:
    case AUDIO_S16MSB: {
        const int hi = format == AUDIO_S16MSB ? 0 : 1;
        for (i = 0; i + 1 < len; i += 2) {
            const int s = (Sint16)(src[i + hi] << 8 | src[i + 1 - hi]);
            const int d = (Sint16)(dst[i + hi] << 8 | dst[i + 1 - hi]);
            int v = (s * volume) / SDL_MIX_MAXVOLUME + d;
            v = v < -32768 ? -32768 : v > 32767 ? 32767 : v;
            dst[i + hi] = (Uint8)(v >> 8);
            dst[i + 1 - hi] = (Uint8)v;
        }
        break;
    }
    default:
        SDL_SetError("SDL_MixAudioFormat(): format 0x%04X: 8 or 16 bits here",
                     format);
        break;
    }
}

void SDL_MixAudio(Uint8 *dst, const Uint8 *src, Uint32 len, int volume)
{
    const SDL_AudioFormat format = stdl_sdl2_audio_format();
    if (format) {
        SDL_MixAudioFormat(dst, src, format, len, volume);
    }
}
