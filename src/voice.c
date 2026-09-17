/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * STDL_Voice: fixed-function sample music mixer (see stdl_voice.h).
 *
 * The DMA loops over a small mono ring; a VBL callback runs the
 * sequencer tick and then mixes forward in quarter-ring blocks up to
 * (but not into) the quarter the hardware is playing. At the default
 * music rate of 6258Hz the ring holds ~82ms, a quarter ~20ms, so
 * voice programming is audible within a tick or two of the 50Hz
 * sequencer - tight enough for module playback.
 *
 * Volume is a table, not a multiply: one 256-byte signed row per
 * volume level, built at open. The mixer's inner loop is then one
 * byte fetch, one table fetch and an add per voice-sample; a 68000
 * multiply would cost more than the rest of the loop together.
 *
 * Everything the VBL touches is preallocated at open; SetVoice and
 * friends only write voice state (interrupts briefly masked, so a
 * mainline caller cannot race the tick).
 */

#include <stdlib.h>
#include <string.h>
#include "stdl_internal.h"

/* ring of RING_FRAMES mono s8 frames, mixed in quarters */
#define RING_FRAMES 512
#define BLOCK_FRAMES (RING_FRAMES / 4)

typedef struct {
    const int8_t *data;
    uint32_t pos;       /* 16.16 frame position                     */
    uint32_t end;       /* 16.16 end of the current stretch         */
    uint32_t loopstart; /* 16.16 restart point after the first pass */
    uint32_t loopsize;  /* 16.16 loop length, 0 = one-shot          */
    uint32_t step;      /* 16.16 resampling step                    */
    const int8_t *vt;   /* volume table row                         */
    uint8_t active;
} voice_t;

static struct {
    int      open;
    int      dma_freq;
    uint8_t  dma_mode;
    int8_t  *ring;
    void    *ring_alloc;
    int8_t  *voltab;            /* 65 rows of 256 */
    voice_t  v[STDL_VOICES];
    int      fill_block;        /* next quarter to mix */
    void   (*tick)(void *);
    void    *tick_ud;
} vc;

#ifndef __m68k__
/* Host tests read the volume table directly - it is built once at
 * open and its symmetry is the property worth asserting, which no
 * amount of mixing through the ring shows as clearly. Compiled out
 * on target, so it costs a port nothing. */
const int8_t *stdl_host_voltab(void)
{
    return vc.voltab;
}
#endif

static void voice_shutdown(void)
{
    /* terminate-vector context: hardware and vectors only */
    stdl_dma_stop();
    vc.open = 0;
    stdl.dma_owner = STDL_DMA_FREE;
}

/* mix one quarter of the ring */
static void mix_block(int8_t *dst)
{
    int mixed = 0;
    int i;

    for (i = 0; i < STDL_VOICES; i++) {
        voice_t *v = &vc.v[i];
        const int8_t *vt;
        /* the sample, in a local: the store into dst may alias the
         * pointer in the voice struct as far as gcc knows, so
         * without this it is reloaded for every sample - about
         * 6000 cycles a block at 8MHz */
        const int8_t *data;
        uint32_t pos, step, end;
        int n;

        if (!v->active) {
            continue;
        }
        vt = v->vt;
        data = v->data;
        pos = v->pos;
        step = v->step;
        end = v->end;
        if (!mixed) {
            for (n = 0; n < BLOCK_FRAMES; n++) {
                if (pos >= end) {
                    if (v->loopsize == 0) {
                        v->active = 0;
                        memset(dst + n, 0, BLOCK_FRAMES - n);
                        break;
                    }
                    /* Paula-style: after the current stretch ends,
                     * playback confines to the loop region */
                    pos = v->loopstart;
                    end = v->loopstart + v->loopsize;
                    v->end = end;
                }
                dst[n] = vt[(uint8_t)data[pos >> 16]];
                pos += step;
            }
        } else {
            for (n = 0; n < BLOCK_FRAMES; n++) {
                if (pos >= end) {
                    if (v->loopsize == 0) {
                        v->active = 0;
                        break;
                    }
                    pos = v->loopstart;
                    end = v->loopstart + v->loopsize;
                    v->end = end;
                }
                {
                    /* clamped: two voices at full volume sum to
                     * the range exactly, so only a third or fourth
                     * loud voice reaches this, and wrapping would
                     * be far worse than clipping */
                    const int a = dst[n] + vt[(uint8_t)data[pos >> 16]];
                    dst[n] = (int8_t)(a > 127 ? 127
                                    : (a < -128 ? -128 : a));
                }
                pos += step;
            }
        }
        v->pos = pos;
        mixed = 1;
    }
    if (!mixed) {
        memset(dst, 0, BLOCK_FRAMES);
    }
}

/* the 50Hz VBL callback: sequencer tick, then mix forward */
static void voice_vbl(void)
{
    uint32_t off;
    int play_block, guard;

    if (!vc.open) {
        return;
    }
    if (vc.tick != NULL) {
        vc.tick(vc.tick_ud);
    }
    off = stdl_dma_counter() - (uint32_t)(uintptr_t)vc.ring;
    if (off >= (uint32_t)RING_FRAMES) {
        return;                 /* counter mid-reload; next tick */
    }
    play_block = (int)(off / BLOCK_FRAMES);
    /* chase the play head: fill every quarter behind it, stopping
     * at the one the hardware is reading */
    for (guard = 0; guard < 3; guard++) {
        if (vc.fill_block == play_block) {
            break;
        }
        mix_block(vc.ring + vc.fill_block * BLOCK_FRAMES);
        vc.fill_block = (vc.fill_block + 1) & 3;
    }
}

int STDL_OpenVoices(int freq)
{
    int level, s;

    if (vc.open) {
        STDL_SetError("voices already open");
        return -1;
    }
    if (!stdl.initialised) {
        STDL_Init(STDL_INIT_AUDIO);
    }
    if (!stdl.mach.is_ste) {
        STDL_SetError("no DMA sound hardware (STE/Mega STE only)");
        return -1;
    }
    STDL_SamplePlaying();       /* expire a finished one-shot */
    if (stdl.dma_owner != STDL_DMA_FREE) {
        STDL_SetError("sound DMA in use");
        return -1;
    }

    memset(&vc, 0, sizeof(vc));
    vc.dma_freq = stdl_dma_rates[stdl_dma_nearest(freq)];
    vc.dma_mode = (uint8_t)(stdl_dma_nearest(freq) | 0x80); /* mono */

    vc.ring_alloc = malloc(RING_FRAMES + 2);
    vc.voltab = malloc(65 * 256);
    if (vc.ring_alloc == NULL || vc.voltab == NULL) {
        free(vc.ring_alloc);
        free(vc.voltab);
        memset(&vc, 0, sizeof(vc));
        STDL_SetError("out of memory for voice mixer");
        return -1;
    }
    vc.ring = (int8_t *)(((uintptr_t)vc.ring_alloc + 1) & ~(uintptr_t)1);
    memset(vc.ring, 0, RING_FRAMES);

    /*
     * Volume rows: vt[vol][byte] = (int8)byte * vol / 128, so a
     * voice at vol 64 is the sample halved and two of them sum to
     * the full s8 range exactly. Three or four loud voices clip,
     * which the mixer clamps.
     *
     * Rounded toward zero, not shifted. An arithmetic >> is a floor,
     * which is not symmetric about zero: at vol 16 every negative
     * input from -1 down mapped to -1 while every small positive
     * mapped to 0, so a quiet sample came out as a DC-biased square
     * wave toggling at its own zero crossings. On a DMA DAC in
     * silence that is an audible tick, heard on real hardware as an
     * intermittent spike under an ambient effect playing at vol 16.
     * Even at vol 64 it was lopsided: -1 and -2 gave -1 where +1 and
     * +2 gave 0. Truncating toward zero costs a branch per entry of
     * a table built once at open, and nothing per sample.
     *
     * The divisor is 128 and not 256, which is the other half of
     * the same report. Scaling an 8-bit sample into an 8-bit table
     * loses resolution at low volumes whatever the rounding: the
     * old rule reserved headroom for four voices at once, so full
     * scale was s/4 and a voice at vol 16 was s/16 - only the
     * loudest sixteenth of the range survived, and an ambient
     * effect at that volume was inaudible even once it had stopped
     * clicking. Halving the reserve doubles what survives at every
     * volume. Two voices still sum exactly; three or four loud ones
     * clip instead of wrapping, because the mixer clamps.
     *
     * That is a deliberate trade and it makes every port louder.
     * Clipping needs three voices near peak at the same moment,
     * where the old rule needed none - against six decibels of
     * resolution on every quiet sound, at every volume, all the
     * time. Paula applies volume after the DAC and keeps its
     * resolution regardless, which no table here can match; this is
     * the closest an 8-bit table gets.
     */
    for (level = 0; level <= 64; level++) {
        int8_t *row = vc.voltab + level * 256;
        for (s = 0; s < 256; s++) {
            const int v = (int)(int8_t)s * level;
            row[s] = (int8_t)(v < 0 ? -((-v) >> 7) : (v >> 7));
        }
    }

    if (STDL_AddVBL(voice_vbl) < 0) {
        free(vc.ring_alloc);
        free(vc.voltab);
        memset(&vc, 0, sizeof(vc));
        return -1;
    }
    vc.fill_block = 1;          /* playback starts in block 0 */
    vc.open = 1;
    stdl.dma_owner = STDL_DMA_VOICES;
    stdl_shutdown_audio = voice_shutdown;
    stdl_dma_start(vc.ring, RING_FRAMES, vc.dma_mode, 1);
    return 0;
}

void STDL_CloseVoices(void)
{
    if (!vc.open) {
        return;
    }
    STDL_RemoveVBL(voice_vbl);
    stdl_dma_stop();
    if (stdl.dma_owner == STDL_DMA_VOICES) {
        stdl.dma_owner = STDL_DMA_FREE;
    }
    free(vc.ring_alloc);
    free(vc.voltab);
    memset(&vc, 0, sizeof(vc));
}

int STDL_VoicesOpen(void)
{
    return vc.open;
}

void STDL_SetVoice(int v, const int8_t *data, uint32_t len,
                   uint32_t loop_off, uint32_t loop_len,
                   uint32_t freq, uint8_t vol)
{
    voice_t *p;
    uint16_t sr;

    if (!vc.open || v < 0 || v >= STDL_VOICES) {
        return;
    }
    p = &vc.v[v];
    if (data == NULL || len == 0 || freq == 0) {
        p->active = 0;
        return;
    }
    if (len > 0xFFFF) {
        len = 0xFFFF;           /* 16.16 positions; see the header */
    }
    if (loop_off > len) {
        loop_off = len;
    }
    if (loop_len > len - loop_off) {
        loop_len = len - loop_off;
    }
    if (vol > 64) {
        vol = 64;
    }
    sr = stdl_int_off();
    p->active = 0;              /* keep the mixer off a half-set voice */
    p->data = data;
    p->pos = 0;
    p->end = len << 16;
    p->loopstart = loop_off << 16;
    p->loopsize = loop_len << 16;
    p->step = (freq << 16) / (uint32_t)vc.dma_freq;
    p->vt = vc.voltab + (uint32_t)vol * 256;
    p->active = 1;
    stdl_int_restore(sr);
}

void STDL_StopVoice(int v)
{
    if (vc.open && v >= 0 && v < STDL_VOICES) {
        vc.v[v].active = 0;
    }
}

int STDL_VoiceActive(int v)
{
    if (!vc.open || v < 0 || v >= STDL_VOICES) {
        return 0;
    }
    return vc.v[v].active;
}

void STDL_SetVoiceFreq(int v, uint32_t freq)
{
    if (vc.open && v >= 0 && v < STDL_VOICES && freq != 0) {
        vc.v[v].step = (freq << 16) / (uint32_t)vc.dma_freq;
    }
}

void STDL_SetVoiceVolume(int v, uint8_t vol)
{
    if (vc.open && v >= 0 && v < STDL_VOICES) {
        if (vol > 64) {
            vol = 64;
        }
        vc.v[v].vt = vc.voltab + (uint32_t)vol * 256;
    }
}

void STDL_SetVoiceTick(void (*fn)(void *), void *userdata)
{
    uint16_t sr = stdl_int_off();

    vc.tick = fn;
    vc.tick_ud = userdata;
    stdl_int_restore(sr);
}
