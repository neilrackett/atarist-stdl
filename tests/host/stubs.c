/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * Host-test stubs: the ST-specific globals and hardware fallbacks
 * that let the pure pixel-path modules link and run natively.
 */

#include <string.h>
#include <assert.h>
#include "stdl_internal.h"

stdl_state_t stdl;
STDL_Surface stdl_screen;

/* the "hardware" behind the register macros on host builds */
volatile uint32_t stdl_host_clock;
volatile uint16_t stdl_host_hwpal[16];

/*
 * TOS's VBL queue: eight slots at *_vblqueue, with the count at
 * $454. Plain memory here, so vbl.c's slot bookkeeping is testable
 * natively - the tests call the queue entries themselves in place of
 * the interrupt.
 */
static void (*host_vblslots[8])(void);
volatile uint16_t stdl_host_nvbls = 8;
void (**stdl_host_vblqueue)(void) = host_vblslots;

volatile uint8_t stdl_host_vidcnt[3];
volatile uint8_t stdl_host_vidbase[2];
volatile uint8_t stdl_host_mste_ctl;

/* the YM2149 as a register file, and the console-attributes byte */
volatile uint8_t stdl_host_ym_sel;
volatile uint8_t stdl_host_ym[16];
volatile uint8_t stdl_host_conterm;

/* Registers 14 and 15 are the chip's two I/O ports; on an ST they
 * carry floppy drive select, and writing them is the one thing the
 * library must never do (AGENTS.md). Every host write lands here,
 * so the suite catches a computed register number that strays. */
unsigned stdl_host_ym_at(unsigned sel)
{
    assert(sel < 14);
    return sel & 15;
}

void (*stdl_shutdown_audio)(void);
void (*stdl_shutdown_music)(void);
void (*stdl_shutdown_vbl)(void);
void (*stdl_shutdown_hwscroll)(void);

/* video.c is the real STDL_Init and takes supervisor mode; on the
 * host there is nothing to claim, so record the state the modules
 * under test check for and move on. */
int STDL_Init(uint32_t flags)
{
    (void)flags;
    stdl.initialised = 1;
    return 0;
}

static char errbuf[128];

const char *STDL_GetError(void)
{
    return errbuf;
}

void STDL_SetError(const char *msg)
{
    strncpy(errbuf, msg, sizeof(errbuf) - 1);
    errbuf[sizeof(errbuf) - 1] = '\0';
}

void STDL_WaitVBL(void)
{
    stdl_host_clock += 4;
}

STDL_Surface *STDL_GetVideoSurface(void)
{
    return NULL;
}

/*
 * Sound DMA stubs for the voice mixer tests: no hardware, so the
 * "registers" are plain state the tests read and steer. The fake
 * counter walks the programmed buffer so voice_vbl's block
 * arithmetic can be exercised by advancing it from the test.
 */
const int stdl_dma_rates[4] = { 6258, 12517, 25033, 50066 };

uint32_t stdl_host_dma_pos;       /* fake frame address counter */
int      stdl_host_dma_running;
const void *stdl_host_dma_buf;
uint32_t stdl_host_dma_bytes;

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

void stdl_dma_start(const void *data, uint32_t bytes, uint8_t mode,
                    int repeat)
{
    (void)mode;
    (void)repeat;
    stdl_host_dma_buf = data;
    stdl_host_dma_bytes = bytes;
    stdl_host_dma_pos = (uint32_t)(uintptr_t)data;
    stdl_host_dma_running = 1;
}

void stdl_dma_stop(void)
{
    stdl_host_dma_running = 0;
}

uint32_t stdl_dma_counter(void)
{
    return stdl_host_dma_pos;
}

int STDL_SamplePlaying(void)
{
    return 0;
}

/* defined in video.c on target, which the host tests do not link */
void (*stdl_pal_apply_hook)(void);
