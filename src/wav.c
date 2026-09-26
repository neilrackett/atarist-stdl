/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * Sample formats: the WAV loader, and conversion of any supported
 * format to the signed 8-bit frames the DMA and the voice mixer play.
 * No hardware here, so tests/host covers it; the ring device and the
 * SDL_mixer layer link it, a program that only plays its own buffers
 * or voices does not.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "stdl_internal.h"

/*
 * Conversion to signed 8-bit at a new rate, nearest neighbour: the
 * ring device's refill (ring.c) and the SDL_mixer chunk loader. Output
 * frame j reads source frame floor(j * src_frames / dst_frames), or
 * now and then the one before, the step's fraction being truncated
 * to 16 bits. It is stepped as a byte pointer and that fraction,
 * whose carry adds a frame - no multiply or divide in the loop, and
 * one loop per layout so no frame tests what it is. An earlier `while (acc >= dst_frames) src +=
 * frame_bytes` compiled, under gcc 4.6, to a __mulsi3 call on every
 * output frame; in the ring device that alone took 37% of an 8MHz STE.
 *
 * dst_frames is at most 65535 and src_frames under 65536 times it, so
 * the setup is two divu.w: the ring refills 2048 frames at a time and
 * the chunk loader caps a chunk at 65535.
 */
void stdl_audio_convert(int8_t *dst, uint32_t dst_frames,
                        const uint8_t *src, uint32_t src_frames,
                        uint16_t format, int channels, int mono_mix)
{
    uint8_t xr = 0, csize = 1, fb;
    uint16_t step_i, step_f, frac = 0;
    uint32_t n, step_b;

    switch (format) {
        case STDL_AUDIO_U8:
            xr = 0x80; break;
        case STDL_AUDIO_S8:
            break;
        case STDL_AUDIO_S16LSB:
            src += 1;               /* the high byte */
            csize = 2; break;
        default:                    /* S16MSB */
            csize = 2; break;
    }
    if (dst_frames == 0) {
        return;
    }
    fb = (uint8_t)(csize << (channels == 2));
    step_i = stdl_divu(src_frames, (uint16_t)dst_frames);
    step_f = stdl_divu((src_frames - stdl_row_off(step_i, (uint16_t)dst_frames)) << 16,
                       (uint16_t)dst_frames);
    step_b = (uint32_t)step_i << (fb >> 1);     /* fb is 1, 2 or 4 */

#define STEP() do { \
        const uint16_t nf = (uint16_t)(frac + step_f); \
        src += step_b; \
        if (nf < frac) { \
            src += fb; \
        } \
        frac = nf; \
    } while (0)

    if (channels == 2 && mono_mix) {
        for (n = dst_frames; n != 0; n--) {
            const int a = (int8_t)(src[0] ^ xr);
            const int b = (int8_t)(src[csize] ^ xr);
            *dst++ = (int8_t)((a + b) >> 1);
            STEP();
        }
    } else if (channels == 2) {
        for (n = dst_frames; n != 0; n--) {
            *dst++ = (int8_t)(src[0] ^ xr);
            *dst++ = (int8_t)(src[csize] ^ xr);
            STEP();
        }
    } else {
        for (n = dst_frames; n != 0; n--) {
            *dst++ = (int8_t)(src[0] ^ xr);
            STEP();
        }
    }
#undef STEP
}

/* WAV loading (uncompressed PCM only) */

STDL_AudioSpec *STDL_LoadWAV(const char *file, STDL_AudioSpec *spec,
                             uint8_t **audio_buf, uint32_t *audio_len)
{
    FILE *f;
    uint8_t head[12], chunk[8], fmt[16];
    int have_fmt = 0;
    uint8_t *data = NULL;
    uint32_t datalen = 0;

    if (spec == NULL || audio_buf == NULL || audio_len == NULL) {
        return NULL;
    }
    f = stdl_fopen_ci(file, "rb");
    if (f == NULL) {
        STDL_SetError("cannot open WAV file");
        return NULL;
    }
    if (fread(head, 1, 12, f) != 12 || memcmp(head, "RIFF", 4) != 0
        || memcmp(head + 8, "WAVE", 4) != 0) {
        STDL_SetError("not a WAV file");
        fclose(f);
        return NULL;
    }
    while (fread(chunk, 1, 8, f) == 8) {
        uint32_t sz = stdl_le32(chunk + 4);
        if (memcmp(chunk, "fmt ", 4) == 0 && sz >= 16) {
            if (fread(fmt, 1, 16, f) != 16) {
                break;
            }
            have_fmt = 1;
            if (fseek(f, (long)(sz - 16 + (sz & 1)), SEEK_CUR) != 0) {
                break;
            }
        } else if (memcmp(chunk, "data", 4) == 0) {
            data = malloc(sz);
            if (data == NULL || fread(data, 1, sz, f) != sz) {
                free(data);
                data = NULL;
                break;
            }
            datalen = sz;
            break;
        } else {
            if (fseek(f, (long)(sz + (sz & 1)), SEEK_CUR) != 0) {
                break;
            }
        }
    }
    fclose(f);

    if (!have_fmt || data == NULL) {
        free(data);
        STDL_SetError("truncated WAV file");
        return NULL;
    }
    if (stdl_le16(fmt) != 1) {
        free(data);
        STDL_SetError("compressed WAV; convert with stdlconv wav");
        return NULL;
    }
    memset(spec, 0, sizeof(*spec));
    spec->channels = (uint8_t)stdl_le16(fmt + 2);
    spec->freq = (int)stdl_le32(fmt + 4);
    switch (stdl_le16(fmt + 14)) {
        case 8:
            spec->format = STDL_AUDIO_U8;
            spec->silence = 0x80;
            break;
        case 16:
            spec->format = STDL_AUDIO_S16LSB;
            break;
        default:
            free(data);
            STDL_SetError("unsupported WAV bit depth");
            return NULL;
    }
    if (spec->channels < 1 || spec->channels > 2) {
        free(data);
        STDL_SetError("unsupported WAV channel count");
        return NULL;
    }
    spec->samples = 1024;
    *audio_buf = data;
    *audio_len = datalen;
    return spec;
}

void STDL_FreeWAV(uint8_t *audio_buf)
{
    free(audio_buf);
}
