/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * The voice device against an open top border. Timer A opens the
 * border about 2ms into each frame and has a line and a quarter of
 * grace; anything that holds interrupts off across that moment
 * loses the border for the frame. The voice tick runs in the VBL,
 * where a sequencer calls STDL_SetVoice, and SetVoice masks
 * interrupts - so how long it masks them decides whether a module
 * player can share a machine with the border.
 *
 * Three runs of 20 seconds with the border open, misses counted
 * for each: the device idle; four voices looping; and all four
 * retriggered from the tick every frame, after a delay that sweeps
 * the calls across the first 3ms of the frame so some of them land
 * on Timer A's window. The last is the one that failed: 18 misses
 * while SetVoice did its step division with interrupts off, none
 * since.
 *
 * The result goes to OVVOICE.TXT beside the program, and the screen
 * turns green when every run is clean or red when any missed; any
 * key quits. Needs an STE or Mega STE - the voice device is DMA
 * sound.
 *
 * Build: m68k-atari-mint-gcc -O2 -Iinclude -Isrc
 *        tests/hatari/ovvoice.c libstdl.a -o OVVOICE.TOS
 */
#include <stdio.h>
#include <stdl/stdl.h>
#include "stdl_internal.h"

#define SLEN 8000
static int8_t smp[STDL_VOICES][SLEN];

static int tickn;

/* every voice retriggered, as a sequencer does on a row, after a
 * delay that steps across 40 ticks */
static void tick(void *ud)
{
    volatile int spin;
    int v;

    (void)ud;
    if (++tickn >= 40) {
        tickn = 0;
    }
    for (spin = 0; spin < tickn * 30; spin++) {
    }
    for (v = 0; v < STDL_VOICES; v++) {
        STDL_SetVoice(v, smp[v], SLEN, 0, SLEN, 8363 + v * 1500, 48);
    }
}

static uint32_t run(int secs)
{
    uint32_t m0 = STDL_OverscanMisses();
    int i;

    for (i = 0; i < secs * 50; i++) {
        STDL_WaitVBL();
    }
    return STDL_OverscanMisses() - m0;
}

int main(void)
{
    STDL_Surface *screen;
    FILE *f;
    uint32_t idle, busy, trig;
    int v, i;

    if (STDL_Init(STDL_INIT_VIDEO | STDL_INIT_AUDIO) < 0) {
        return 1;
    }
    screen = STDL_SetVideoMode(320, 200, 4, 0);
    f = fopen("OVVOICE.TXT", "w");
    if (STDL_OpenVoices(6258) != 0) {
        if (f) {
            fprintf(f, "no voice device (%s) - needs an STE\n", STDL_GetError());
            fclose(f);
        }
        STDL_Quit();
        return 1;
    }
    if (!STDL_OpenTopBorder()) {
        if (f) {
            fprintf(f, "no top border (%s)\n", STDL_GetError());
            fclose(f);
        }
        STDL_CloseVoices();
        STDL_Quit();
        return 1;
    }
    screen = STDL_GetVideoSurface();
    for (v = 0; v < STDL_VOICES; v++) {
        for (i = 0; i < SLEN; i++) {
            smp[v][i] = (int8_t)(((i * (v + 3)) & 0x7F) - 64);
        }
    }
    run(2);                     /* settle after the open */
    idle = run(20);
    for (v = 0; v < STDL_VOICES; v++) {
        STDL_SetVoice(v, smp[v], SLEN, 0, SLEN, 8363 + v * 1500, 48);
    }
    busy = run(20);
    STDL_SetVoiceTick(tick, NULL);
    trig = run(20);
    STDL_SetVoiceTick(NULL, NULL);
    for (v = 0; v < STDL_VOICES; v++) {
        STDL_StopVoice(v);
    }
    if (f) {
        fprintf(f, "idle 20s: %lu misses\n", (unsigned long)idle);
        fprintf(f, "four voices 20s: %lu misses\n", (unsigned long)busy);
        fprintf(f, "retriggered from the tick 20s: %lu misses\n",
                (unsigned long)trig);
        fprintf(f, "%s\n", (idle | busy | trig) ? "FAIL" : "OK");
        fclose(f);
    }
    if (screen) {
        STDL_Rect r = { 0, 0, 320, 200 };
        STDL_SetColour(1, (idle | busy | trig) ? 0x700 : 0x070);
        STDL_FillRect(screen, &r, 1);
    }
    for (;;) {
        STDL_Event ev;
        if (STDL_PollEvent(&ev) && ev.type == STDL_KEYDOWN) {
            break;
        }
    }
    STDL_CloseTopBorder();
    STDL_CloseVoices();
    STDL_Quit();
    return 0;
}
