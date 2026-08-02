/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * STDL_Music: YM2149 register-stream replay, ticking on the shared
 * YM service (ym.c).
 *
 * STM stream format (big-endian, docs/format.md):
 *   "STM1"  u16 tick_hz  u16 nframes  u16 loop_frame  u16 reserved
 *   frames: u16 change mask (bit r = YM register r, 0..13), then
 *           one byte per set bit, ascending register order.
 * Register 7 is stored with the I/O port bits clear; the service
 * ORs in the direction bits TOS relies on. Register 13 (envelope
 * shape) only appears in a frame when a retrigger is intended.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "stdl_internal.h"
#include <stdl/stdl_music.h>

struct STDL_Music {
    uint16_t tick_hz;
    uint16_t nframes;
    uint16_t loop_frame;
    uint8_t *data;          /* packed frames                        */
    uint32_t datalen;
    uint32_t *frame_off;    /* byte offset of each frame            */
};

/* player state shared with the VBL tick; volatile where the tick
 * and the main program both touch it */
static STDL_Music *volatile cur;
static volatile uint32_t cur_pos;       /* byte offset into data    */
static volatile uint16_t cur_frame;
static volatile int16_t  loops_left;    /* -1 = forever             */
static volatile uint8_t  playing;
static volatile uint8_t  paused;
static uint8_t  music_volume = 128;     /* 0..128                   */
static uint16_t tick_acc;

static uint8_t stream_shadow[14];       /* raw last stream values   */

/* volume-scale a raw stream value for a fixed-volume channel */
static uint8_t scale_vol(uint8_t v)
{
    if (v & 0x10) {
        return v;               /* envelope-driven: leave alone     */
    }
    v = (uint8_t)(((unsigned)v * music_volume) >> 7);
    return v > 15 ? 15 : v;
}

/* the YM service hands a voice back: re-write the stream's state */
static void restore_voice(int voice)
{
    if (playing && cur != NULL) {
        stdl_ym_write(2 * voice, stream_shadow[2 * voice]);
        stdl_ym_write(2 * voice + 1, stream_shadow[2 * voice + 1]);
        stdl_ym_write(8 + voice, scale_vol(stream_shadow[8 + voice]));
        if (!(stdl_ym_owned & 0x08)) {
            stdl_ym_write(6, stream_shadow[6]);
        }
        stdl_ym_mix_update(STDL_YM_VOICE_BITS(voice),
            (uint8_t)(stream_shadow[7] & STDL_YM_VOICE_BITS(voice)));
    } else {
        stdl_ym_write(8 + voice, 0);
        stdl_ym_mix_update(STDL_YM_VOICE_BITS(voice),
                           STDL_YM_VOICE_BITS(voice));
    }
}

/* silence the voices music currently controls */
static void ym_silence(void)
{
    int v;

    for (v = 0; v < 3; v++) {
        if (!(stdl_ym_owned & (1u << v))) {
            stdl_ym_write(8 + v, 0);
            stdl_ym_mix_update(STDL_YM_VOICE_BITS(v),
                               STDL_YM_VOICE_BITS(v));
        }
    }
}

/* does this register belong to a voice an effect owns right now? */
static int reg_owned(int reg)
{
    switch (reg) {
        case 0: case 1: return stdl_ym_owned & 1;
        case 2: case 3: return stdl_ym_owned & 2;
        case 4: case 5: return stdl_ym_owned & 4;
        case 8:  return stdl_ym_owned & 1;
        case 9:  return stdl_ym_owned & 2;
        case 10: return stdl_ym_owned & 4;
        case 6:  return stdl_ym_owned & 8;
        default: return 0;
    }
}

/* apply one frame's register changes; returns updated position */
static uint32_t step_frame(STDL_Music *m, uint32_t pos)
{
    const uint8_t *p = m->data + pos;
    uint16_t mask = stdl_rd16(p);
    int reg;

    p += 2;
    for (reg = 0; reg <= 13; reg++) {
        if (mask & (1u << reg)) {
            uint8_t v = *p++;
            stream_shadow[reg] = v;
            if (reg_owned(reg)) {
                continue;       /* an effect owns this register     */
            }
            if (reg == 7) {
                uint8_t keep = 0;
                int vc;
                for (vc = 0; vc < 3; vc++) {
                    if (stdl_ym_owned & (1u << vc)) {
                        keep |= STDL_YM_VOICE_BITS(vc);
                    }
                }
                stdl_ym_mix_update((uint8_t)(0x3F & ~keep),
                                   (uint8_t)(v & 0x3F & ~keep));
                continue;
            }
            if (reg >= 8 && reg <= 10) {
                v = scale_vol(v);
            }
            stdl_ym_write(reg, v);
        }
    }
    return (uint32_t)(p - m->data);
}

/* per-VBL music tick (registered with the YM service). The
 * accumulator replays non-50Hz streams at the right speed on a
 * 50Hz VBL; 60Hz displays would run 20%% fast - low resolution on
 * a colour monitor is 50Hz, which is v1's world. */
static void music_tick(void)
{
    STDL_Music *m = cur;
    uint32_t pos;
    uint16_t frame;

    if (!playing || paused || m == NULL) {
        return;
    }
    pos = cur_pos;
    frame = cur_frame;
    tick_acc = (uint16_t)(tick_acc + m->tick_hz);
    while (tick_acc >= 50 && playing) {
        tick_acc -= 50;
        pos = step_frame(m, pos);
        frame++;
        if (frame >= m->nframes) {
            if (loops_left < 0 || --loops_left > 0) {
                frame = m->loop_frame;
                pos = m->frame_off[frame];
            } else {
                playing = 0;
                ym_silence();
            }
        }
    }
    cur_pos = pos;
    cur_frame = frame;
}

/* ---------------------------------------------------------------- */

STDL_Music *STDL_LoadMusic(const char *file)
{
    FILE *f;
    uint8_t head[12];
    STDL_Music *m = NULL;
    long fsize;
    uint32_t off;
    int i;

    f = stdl_fopen_ci(file, "rb");
    if (f == NULL) {
        STDL_SetError("cannot open music file");
        return NULL;
    }
    if (fread(head, 1, 12, f) != 12
        || memcmp(head, "STM1", 4) != 0) {
        STDL_SetError("not an STM music stream");
        fclose(f);
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    fsize = ftell(f) - 12;
    fseek(f, 12, SEEK_SET);

    m = calloc(1, sizeof(STDL_Music));
    if (m == NULL) {
        fclose(f);
        STDL_SetError("out of memory");
        return NULL;
    }
    m->tick_hz = stdl_rd16(head + 4);
    m->nframes = stdl_rd16(head + 6);
    m->loop_frame = stdl_rd16(head + 8);
    if (m->tick_hz == 0 || m->tick_hz > 200 || m->nframes == 0
        || m->loop_frame >= m->nframes || fsize <= 0) {
        STDL_SetError("bad STM header");
        free(m);
        fclose(f);
        return NULL;
    }
    m->datalen = (uint32_t)fsize;
    m->data = malloc(m->datalen);
    m->frame_off = malloc(sizeof(uint32_t) * m->nframes);
    if (m->data == NULL || m->frame_off == NULL
        || fread(m->data, 1, m->datalen, f) != m->datalen) {
        STDL_SetError("truncated STM stream");
        STDL_FreeMusic(m);
        fclose(f);
        return NULL;
    }
    fclose(f);

    /* index the variable-length frames and validate the stream */
    off = 0;
    for (i = 0; i < m->nframes; i++) {
        uint16_t mask;
        int reg, n = 0;
        if (off + 2 > m->datalen) {
            STDL_SetError("corrupt STM stream");
            STDL_FreeMusic(m);
            return NULL;
        }
        m->frame_off[i] = off;
        mask = stdl_rd16(m->data + off);
        if (mask & 0xC000) {
            STDL_SetError("STM touches YM I/O registers");
            STDL_FreeMusic(m);
            return NULL;
        }
        for (reg = 0; reg <= 13; reg++) {
            if (mask & (1u << reg)) {
                n++;
            }
        }
        off += 2 + (uint32_t)n;
    }
    if (off > m->datalen) {
        STDL_SetError("corrupt STM stream");
        STDL_FreeMusic(m);
        return NULL;
    }
    return m;
}

void STDL_FreeMusic(STDL_Music *music)
{
    if (music == NULL) {
        return;
    }
    if (music == cur) {
        STDL_HaltMusic();
    }
    free(music->data);
    free(music->frame_off);
    free(music);
}

int STDL_PlayMusic(STDL_Music *music, int loops)
{
    if (music == NULL) {
        STDL_SetError("no music to play");
        return -1;
    }
    if (stdl_ym_install() < 0) {
        return -1;
    }
    playing = 0;                /* stop the tick mid-change */
    cur = music;
    cur_frame = 0;
    cur_pos = music->frame_off[0];
    loops_left = (int16_t)(loops < 0 ? -1 : (loops == 0 ? 1 : loops));
    tick_acc = 0;
    paused = 0;
    stdl_ym_restore_voice = restore_voice;
    stdl_music_tick = music_tick;
    playing = 1;
    return 0;
}

void STDL_HaltMusic(void)
{
    if (playing || cur != NULL) {
        playing = 0;
        cur = NULL;
        ym_silence();
    }
}

void STDL_PauseMusic(void)
{
    if (playing && !paused) {
        paused = 1;
        ym_silence();
    }
}

void STDL_ResumeMusic(void)
{
    paused = 0;
}

int STDL_PausedMusic(void)
{
    return paused;
}

int STDL_PlayingMusic(void)
{
    return playing;
}

int STDL_VolumeMusic(int volume)
{
    int old = music_volume;

    if (volume >= 0) {
        music_volume = (uint8_t)(volume > 128 ? 128 : volume);
    }
    return old;
}
