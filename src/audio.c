/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * STDL_Audio: the STE/Mega STE sound DMA - the primitives the ring
 * device (ring.c) and the voice mixer (voice.c) program it through,
 * and one-shot playback of a caller's buffer (STDL_PlaySample).
 *
 * The ring device and the WAV code (wav.c) are separate objects, so a
 * program that plays samples or voices does not link either.
 */

#include "stdl_internal.h"

#define DMA_CTRL   (*(volatile uint8_t *)0xFFFF8901UL)
#define DMA_START_H (*(volatile uint8_t *)0xFFFF8903UL)
#define DMA_START_M (*(volatile uint8_t *)0xFFFF8905UL)
#define DMA_START_L (*(volatile uint8_t *)0xFFFF8907UL)
#define DMA_CNT_H  (*(volatile uint8_t *)0xFFFF8909UL)
#define DMA_CNT_M  (*(volatile uint8_t *)0xFFFF890BUL)
#define DMA_CNT_L  (*(volatile uint8_t *)0xFFFF890DUL)
#define DMA_END_H  (*(volatile uint8_t *)0xFFFF890FUL)
#define DMA_END_M  (*(volatile uint8_t *)0xFFFF8911UL)
#define DMA_END_L  (*(volatile uint8_t *)0xFFFF8913UL)
#define DMA_MODE   (*(volatile uint8_t *)0xFFFF8921UL)
#define MW_DATA    (*(volatile uint16_t *)0xFFFF8922UL)
#define MW_MASK    (*(volatile uint16_t *)0xFFFF8924UL)

#define DMA_PLAY_ONCE   0x01
#define DMA_PLAY_REPEAT 0x03    /* enable + loop */

const int stdl_dma_rates[4] = { 6258, 12517, 25033, 50066 };


/* one-shot playback state (STDL_PlaySample); shares the chip with
 * the ring device and the voices, which is why only one may own it */
static int sample_active;

/* emergency stop for restore_all: the DMA must never keep looping
 * over freed RAM after the program exits */
static void sample_shutdown(void)
{
    DMA_CTRL = 0;
    sample_active = 0;
    stdl.dma_owner = STDL_DMA_FREE;
}

/* index of the DMA rate nearest `freq` */
int stdl_dma_nearest(int freq)
{
    int i, best = 0, bestdiff = 0x7FFFFFFF;

    for (i = 0; i < 4; i++) {
        int diff = stdl_dma_rates[i] - freq;
        if (diff < 0) diff = -diff;
        if (diff < bestdiff) {
            bestdiff = diff;
            best = i;
        }
    }
    return best;
}

void stdl_dma_stop(void)
{
    DMA_CTRL = 0;
}

/* ---------------------------------------------------------------- */

static void microwire_write(uint16_t data);

/* master and both channels to 0dB */
static void set_output_levels(void)
{
    microwire_write(0x4E8);     /* %10 011 101000  master 0dB   */
    microwire_write(0x554);     /* %10 101 010100  left 0dB     */
    microwire_write(0x514);     /* %10 100 010100  right 0dB    */
}

static void microwire_write(uint16_t data)
{
    long timeout = 10000;

    MW_MASK = 0x07FF;
    MW_DATA = data;
    /* the mask rotates during the ~16us transfer; wait it out */
    while (MW_MASK != 0x07FF && --timeout > 0)
        ;
}

/*
 * The play position, read without tearing.
 *
 * Three byte reads of a counter that is incrementing underneath
 * them, so a carry between two of the reads gives a value that was
 * never true. Re-reading the high byte catches only the coarse
 * case: if the low byte wraps between reading mid and low, the
 * mid byte has already moved on while the low byte reads as 0, and
 * the result is up to 255 bytes BEHIND the true position with the
 * high byte unchanged throughout.
 *
 * That was not academic. On a 512-byte voice ring it puts the
 * apparent play position up to two blocks early, so the mixer's
 * chase - which stops at the block being played - writes into the
 * block the hardware is reading, and that is a click. It happens
 * when a read straddles a mid-byte carry, so its rate follows the
 * caller's phase against the counter rather than any loop period:
 * a real STE clicked every 5-10 seconds with the voice device
 * refilling, against every 30 seconds for the same buffer at the
 * same rate with nothing writing to it, and the rate drifted
 * within a session. A 50Hz VBL against a 48.9Hz block rate beats
 * about once a second, which is the phase that drifts.
 *
 * So both bytes above the low one are checked. If neither moved
 * across the read of the low byte, the three belong together.
 */
uint32_t stdl_dma_counter(void)
{
    uint8_t h, m, l, m2, h2;

    do {
        h = DMA_CNT_H;
        m = DMA_CNT_M;
        l = DMA_CNT_L;
        m2 = DMA_CNT_M;
        h2 = DMA_CNT_H;
    } while (h != h2 || m != m2);
    return ((uint32_t)h << 16) | ((uint32_t)m << 8) | l;
}

/*
 * Program and start playback. Programming order matters: stop
 * first, because the address registers latch into the counter when
 * playback starts, and writing them under a running DMA can be
 * picked up mid-frame.
 */
void stdl_dma_start(const void *data, uint32_t bytes, uint8_t mode,
                    int repeat)
{
    uint32_t start = (uint32_t)data;
    uint32_t end = start + bytes;

    DMA_CTRL = 0;
    DMA_MODE = mode;
    DMA_START_H = (uint8_t)(start >> 16);
    DMA_START_M = (uint8_t)(start >> 8);
    DMA_START_L = (uint8_t)start;
    DMA_END_H = (uint8_t)(end >> 16);
    DMA_END_M = (uint8_t)(end >> 8);
    DMA_END_L = (uint8_t)end;
    set_output_levels();
    DMA_CTRL = repeat ? DMA_PLAY_REPEAT : DMA_PLAY_ONCE;
}

/* ---------------------------------------------------------------- */
/* one-shot playback: the DMA reads the caller's buffer once        */

static int sample_start(const void *data, uint32_t bytes, int freq,
                        int repeat)
{
    if (!stdl.initialised) {
        STDL_Init(STDL_INIT_AUDIO);
    }
    if (!stdl.mach.is_ste) {
        STDL_SetError("no DMA sound hardware (STE/Mega STE only)");
        return -1;
    }
    STDL_SamplePlaying();       /* expire a finished one-shot */
    if (stdl.dma_owner != STDL_DMA_FREE
        && stdl.dma_owner != STDL_DMA_SAMPLE) {
        STDL_SetError("sound DMA in use");
        return -1;
    }
    bytes &= ~1UL;              /* the counter works in words */
    if (data == NULL || bytes < 2 || ((uint32_t)data & 1) != 0) {
        STDL_SetError("bad sample buffer");
        return -1;
    }
    if (!stdl_is_stram(data, bytes)) {
        /*
         * The DMA reads this buffer directly and cannot see
         * alt-RAM, so a caller that allocated it with plain malloc
         * on a machine with any - which a program linked ALTALLOC
         * will do, and that is the toolchain default - would get
         * silence or noise and no indication why. Refusing says it
         * in one line instead. STDL_LoadWAV's buffer is ordinary
         * memory for this reason: it is usually mixed by the CPU,
         * where alt-RAM is fine.
         */
        STDL_SetError("sample buffer is not in ST RAM (the sound DMA "
                      "cannot reach alt-RAM; allocate it with "
                      "Mxalloc mode 0)");
        return -1;
    }
    stdl_dma_start(data, bytes,
                   (uint8_t)(stdl_dma_nearest(freq) | 0x80), /* mono */
                   repeat);
    sample_active = 1;
    stdl.dma_owner = STDL_DMA_SAMPLE;
    stdl_shutdown_audio = sample_shutdown;
    return 0;
}

int STDL_PlaySample(const void *data, uint32_t bytes, int freq)
{
    return sample_start(data, bytes, freq, 0);
}

/*
 * Looping variant: the DMA replays the buffer until STDL_StopSample
 * (or another Play*) - ambient loops and simple music beds at zero
 * per-frame CPU cost. As with STDL_PlaySample the hardware reads the
 * buffer live: stop playback before freeing or rewriting it.
 */
int STDL_PlaySampleLoop(const void *data, uint32_t bytes, int freq)
{
    return sample_start(data, bytes, freq, 1);
}

void STDL_StopSample(void)
{
    if (sample_active) {
        DMA_CTRL = 0;
        sample_active = 0;
        if (stdl.dma_owner == STDL_DMA_SAMPLE) {
            stdl.dma_owner = STDL_DMA_FREE;
        }
    }
}

int STDL_SamplePlaying(void)
{
    if (!sample_active) {
        return 0;
    }
    /* in play-once mode the hardware clears the enable bit when it
     * reaches the end address - no polling of our own required (a
     * looping sample keeps the bit set until STDL_StopSample) */
    if ((DMA_CTRL & 1) == 0) {
        sample_active = 0;
        if (stdl.dma_owner == STDL_DMA_SAMPLE) {
            stdl.dma_owner = STDL_DMA_FREE;
        }
    }
    return sample_active;
}
