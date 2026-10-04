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

/*
 * Seeking replays the stream's deltas from the nearest key: every
 * KEY_FRAMES frames the loader records where the frame starts and
 * the registers as they stand before it. A key every 256 frames is
 * 18 bytes per five seconds of a 50Hz stream, and a seek replays at
 * most 255 frames into the shadow. (Each frame's offset used to be
 * kept instead, four bytes a frame - 21KB for a song under two
 * minutes - for the one offset the player needed, the loop's.)
 */
#define KEY_SHIFT  8
#define KEY_FRAMES (1u << KEY_SHIFT)

typedef struct {
    uint32_t off;           /* byte offset of the key's frame       */
    uint8_t  regs[14];      /* the stream's registers before it     */
} MusicKey;

struct STDL_Music {
    uint16_t tick_hz;
    uint16_t nframes;
    uint16_t loop_frame;
    const uint8_t *data;    /* packed frames                        */
    uint32_t datalen;
    uint32_t loop_off;      /* byte offset of the loop frame        */
    MusicKey *keys;         /* one per KEY_FRAMES frames            */
    void    *block;         /* the loaded file; NULL when borrowed  */
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
    /* in the music tick: gcc 4.6 multiplies two bytes with __mulsi3 */
    v = (uint8_t)(STDL_MulU16(v, music_volume) >> 7);
    return v > 15 ? 15 : v;
}

/* write a voice's registers from the stream's state */
static void write_voice(int voice)
{
    stdl_ym_write(2 * voice, stream_shadow[2 * voice]);
    stdl_ym_write(2 * voice + 1, stream_shadow[2 * voice + 1]);
    stdl_ym_write(8 + voice, scale_vol(stream_shadow[8 + voice]));
    if (!(stdl_ym_owned & 0x08)) {
        stdl_ym_write(6, stream_shadow[6]);
    }
    stdl_ym_mix_update(STDL_YM_VOICE_BITS(voice),
        (uint8_t)(stream_shadow[7] & STDL_YM_VOICE_BITS(voice)));
}

/* the whole stream state onto the chip, around any effect: after a
 * pause, which silenced it, and after a seek. The stream writes only
 * what changes, so without this a note held across either stayed
 * silent until its next change. The envelope's shape goes last and
 * only if a voice follows it, since writing it restarts it. */
static void write_shadow(void)
{
    int v, env = 0;

    for (v = 0; v < 3; v++) {
        if (!(stdl_ym_owned & (1u << v))) {
            write_voice(v);
            env |= stream_shadow[8 + v] & 0x10;
        }
    }
    stdl_ym_write(11, stream_shadow[11]);
    stdl_ym_write(12, stream_shadow[12]);
    if (env) {
        stdl_ym_write(13, stream_shadow[13]);
    }
}

/* the YM service hands a voice back: re-write the stream's state,
 * unless the music is paused - then the voice stays silent until
 * resume writes the whole state back */
static void restore_voice(int voice)
{
    if (playing && !paused && cur != NULL) {
        write_voice(voice);
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

/* one frame's changes into `regs` only; returns the next frame's
 * offset */
static uint32_t skip_frame(const STDL_Music *m, uint32_t pos,
                           uint8_t *regs)
{
    const uint8_t *p = m->data + pos;
    uint16_t mask = stdl_rd16(p);
    int reg;

    p += 2;
    for (reg = 0; reg <= 13; reg++) {
        if (mask & (1u << reg)) {
            regs[reg] = *p++;
        }
    }
    return (uint32_t)(p - m->data);
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
 * accumulator replays a stream of any tick rate at the right speed
 * on the VBL, dividing by the rate actually in effect rather than
 * by 50: STDL_SetRefresh can put the display at 60Hz, and a tune
 * that assumed the VBL was 50 would then run 20%% fast. */
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
    while (tick_acc >= stdl_vbl_hz && playing) {
        tick_acc = (uint16_t)(tick_acc - stdl_vbl_hz);
        pos = step_frame(m, pos);
        frame++;
        if (frame >= m->nframes) {
            if (loops_left < 0 || --loops_left > 0) {
                frame = m->loop_frame;
                pos = m->loop_off;
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

/* the bytes a frame's mask says follow it */
static unsigned mask_bytes(uint16_t mask)
{
    unsigned n = 0;

    for (; mask != 0; mask &= (uint16_t)(mask - 1)) {
        n++;
    }
    return n;
}

/* Check and index a stream in memory: the header, every frame's
 * mask, and the keys for seeking. `block` is what STDL_FreeMusic
 * frees, or NULL for a stream the program owns. */
static STDL_Music *music_parse(const uint8_t *stm, uint32_t bytes,
                               void *block)
{
    STDL_Music *m;
    uint8_t regs[14];
    uint32_t off;
    unsigned i;

    if (stm == NULL || bytes < 12 || memcmp(stm, "STM1", 4) != 0) {
        STDL_SetError("not an STM music stream");
        return NULL;
    }
    m = calloc(1, sizeof(STDL_Music));
    if (m == NULL) {
        STDL_SetError("out of memory");
        return NULL;
    }
    m->tick_hz = stdl_rd16(stm + 4);
    m->nframes = stdl_rd16(stm + 6);
    m->loop_frame = stdl_rd16(stm + 8);
    m->data = stm + 12;
    m->datalen = bytes - 12;
    if (m->tick_hz == 0 || m->tick_hz > 200 || m->nframes == 0
        || m->loop_frame >= m->nframes || m->datalen == 0) {
        STDL_SetError("bad STM header");
        free(m);
        return NULL;
    }
    m->keys = malloc(sizeof(MusicKey)
                     * (((uint32_t)m->nframes + KEY_FRAMES - 1) >> KEY_SHIFT));
    if (m->keys == NULL) {
        STDL_SetError("out of memory");
        free(m);
        return NULL;
    }

    /* index the variable-length frames and validate the stream. A
     * seek from frame 0 starts from silence: every register zero and
     * the mixer's six enables off. stdlconv's first frame sets every
     * register anyway. */
    memset(regs, 0, sizeof regs);
    regs[7] = 0x3F;
    off = 0;
    for (i = 0; i < m->nframes; i++) {
        uint16_t mask;

        if (off + 2 > m->datalen) {
            STDL_SetError("corrupt STM stream");
            goto bad;
        }
        if ((i & (KEY_FRAMES - 1)) == 0) {
            MusicKey *k = &m->keys[i >> KEY_SHIFT];

            k->off = off;
            memcpy(k->regs, regs, sizeof regs);
        }
        if (i == m->loop_frame) {
            m->loop_off = off;
        }
        mask = stdl_rd16(m->data + off);
        if (mask & 0xC000) {
            STDL_SetError("STM touches YM I/O registers");
            goto bad;
        }
        if (off + 2 + mask_bytes(mask) > m->datalen) {
            STDL_SetError("corrupt STM stream");
            goto bad;
        }
        off = skip_frame(m, off, regs);
    }
    m->block = block;
    return m;

bad:
    free(m->keys);
    free(m);
    return NULL;
}

STDL_Music *STDL_LoadMusic(const char *file)
{
    FILE *f;
    STDL_Music *m;
    uint8_t *block;
    long fsize;

    f = stdl_fopen_ci(file, "rb");
    if (f == NULL) {
        STDL_SetError("cannot open music file");
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    fsize = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (fsize < 12) {
        STDL_SetError("not an STM music stream");
        fclose(f);
        return NULL;
    }
    block = malloc((size_t)fsize);
    if (block == NULL) {
        STDL_SetError("out of memory");
        fclose(f);
        return NULL;
    }
    if (fread(block, 1, (size_t)fsize, f) != (size_t)fsize) {
        STDL_SetError("truncated STM stream");
        free(block);
        fclose(f);
        return NULL;
    }
    fclose(f);
    m = music_parse(block, (uint32_t)fsize, block);
    if (m == NULL) {
        free(block);
    }
    return m;
}

STDL_Music *STDL_MusicFromMemory(const void *stm, uint32_t bytes)
{
    return music_parse((const uint8_t *)stm, bytes, NULL);
}

void STDL_FreeMusic(STDL_Music *music)
{
    if (music == NULL) {
        return;
    }
    if (music == cur) {
        STDL_HaltMusic();
    }
    free(music->block);
    free(music->keys);
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
    memset(stream_shadow, 0, sizeof stream_shadow);
    stream_shadow[7] = 0x3F;
    cur = music;
    cur_frame = 0;
    cur_pos = 0;
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
    if (paused && playing && cur != NULL) {
        write_shadow();
    }
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

int STDL_MusicLength(const STDL_Music *music)
{
    return music != NULL ? (int)music->nframes : -1;
}

int STDL_MusicFrame(void)
{
    return cur != NULL ? (int)cur_frame : -1;
}

int STDL_SeekMusic(int frame)
{
    STDL_Music *m = cur;
    const MusicKey *k;
    uint8_t regs[14];
    uint32_t pos;
    uint8_t was;
    int i;

    if (m == NULL || frame < 0 || frame >= (int)m->nframes) {
        STDL_SetError(m == NULL ? "no music to seek" : "frame out of range");
        return -1;
    }
    /* the replay is main-loop work: the tick waits it out rather than
     * the interrupts, and writes nothing until the state is whole */
    was = playing;
    playing = 0;
    k = &m->keys[(unsigned)frame >> KEY_SHIFT];
    memcpy(regs, k->regs, sizeof regs);
    pos = k->off;
    for (i = frame & ~(int)(KEY_FRAMES - 1); i < frame; i++) {
        pos = skip_frame(m, pos, regs);
    }
    memcpy(stream_shadow, regs, sizeof regs);
    cur_pos = pos;
    cur_frame = (uint16_t)frame;
    tick_acc = 0;
    /* playing again before the state goes out: an effect handing a
     * voice back meanwhile then restores it rather than silencing
     * it, and a tick landing in the middle writes from the same,
     * already whole, state */
    playing = was;
    if (was && !paused) {
        write_shadow();
    }
    return 0;
}

int STDL_GetMusicRegisters(uint8_t regs[14])
{
    uint16_t sr;

    if (cur == NULL) {
        memset(regs, 0, 14);
        return -1;
    }
    sr = stdl_int_off();
    memcpy(regs, stream_shadow, 14);
    stdl_int_restore(sr);
    return 0;
}
