/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * The voice mixer's inner loop is 68000 assembly on target and C on
 * the host (src/voice.c). tests/host checks the C against worked
 * expectations; this checks the assembly against the C, on the
 * machine, the way BLITCHK checks the BLiTTER against the CPU: the
 * same random mixes through each, compared byte for byte.
 *
 * Each case sets one to four voices - random samples, lengths, loop
 * points, rates and volumes - and mixes a run of blocks with the
 * assembly, then sets the same voices again and mixes the same
 * blocks with stdl_voice_use_c forcing the C. Interrupts are held
 * off for each pass, or the device's own VBL would mix the voices
 * on underneath the test, and the voices are stopped before they
 * come back on.
 *
 * Then it times both, four voices a block, with the device paused so
 * the VBL leaves the voices alone.
 *
 * The result goes to VOICECHK.TXT beside the program, and the screen
 * turns green for a pass or red for a failure; any key quits. Needs
 * an STE or Mega STE - the voice device is DMA sound.
 *
 * Build: m68k-atari-mint-gcc -O2 -Iinclude -Isrc
 *        tests/hatari/voicechk.c libstdl.a -o VOICECHK.TOS
 */
#include <stdio.h>
#include <string.h>
#include <stdl/stdl.h>
#include "stdl_internal.h"

extern int stdl_voice_use_c;
int stdl_voice_mix_block(int8_t *dst);

#define CASES  120
#define BLOCKS 8
#define BLOCK  128
#define SLEN   9000

static uint32_t seed = 0x13579BDF;

static uint32_t rnd(void)
{
    seed = seed * 1103515245u + 12345u;
    return seed >> 8;
}

static int8_t samples[STDL_VOICES][SLEN];

typedef struct {
    uint32_t len, loop, looplen, freq;
    uint8_t vol;
} vparm;

static void setup(const vparm *p, int nv)
{
    int v;
    for (v = 0; v < STDL_VOICES; v++) {
        STDL_StopVoice(v);
    }
    for (v = 0; v < nv; v++) {
        STDL_SetVoice(v, samples[v], p[v].len, p[v].loop, p[v].looplen,
                      p[v].freq, p[v].vol);
    }
}

static void mix(const vparm *p, int nv, int use_c, int8_t *out)
{
    uint16_t sr = stdl_int_off();
    int b;
    stdl_voice_use_c = use_c;
    setup(p, nv);
    for (b = 0; b < BLOCKS; b++) {
        stdl_voice_mix_block(out + b * BLOCK);
    }
    setup(0, 0);                /* or the VBL plays them on */
    stdl_voice_use_c = 0;
    stdl_int_restore(sr);
}

static void pick(vparm *p)
{
    /* short samples and loops of a frame or two are the edge worth
     * hitting, so a third of each is kept small */
    p->len = (rnd() % 3 == 0) ? 1 + rnd() % 8 : 1 + rnd() % SLEN;
    if (rnd() & 1) {
        p->loop = rnd() % p->len;
        p->looplen = (rnd() % 3 == 0) ? 1 : 1 + rnd() % (p->len - p->loop);
    } else {
        p->loop = 0;
        p->looplen = 0;
    }
    p->freq = 500 + rnd() % 40000;
    p->vol = (uint8_t)(rnd() % 65);
}

/* ms to mix `blocks` blocks of four voices with one path */
static long time_path(int use_c, int blocks)
{
    static int8_t out[BLOCK];
    vparm p[STDL_VOICES];
    uint32_t t0;
    int v, b;
    for (v = 0; v < STDL_VOICES; v++) {
        p[v].len = SLEN;
        p[v].loop = 0;
        p[v].looplen = SLEN;
        p[v].freq = 8363 + v * 1000;
        p[v].vol = 48;
    }
    stdl_voice_use_c = use_c;
    setup(p, STDL_VOICES);
    t0 = STDL_GetTicks();
    for (b = 0; b < blocks; b++) {
        stdl_voice_mix_block(out);
    }
    t0 = STDL_GetTicks() - t0;
    stdl_voice_use_c = 0;
    return (long)t0;
}

int main(void)
{
    static int8_t a[BLOCKS * BLOCK], b[BLOCKS * BLOCK];
    STDL_Surface *screen;
    FILE *f;
    char line[120];
    int c, v, i, fails = 0, first = -1;
    long tasm = 0, tc = 0;

    if (STDL_Init(STDL_INIT_VIDEO | STDL_INIT_AUDIO) < 0) {
        return 1;
    }
    screen = STDL_SetVideoMode(320, 200, 4, 0);
    f = fopen("VOICECHK.TXT", "w");
    if (STDL_OpenVoices(6258) != 0) {
        if (f) {
            fprintf(f, "no voice device (%s) - needs an STE\n", STDL_GetError());
            fclose(f);
        }
        STDL_Quit();
        return 1;
    }
    for (v = 0; v < STDL_VOICES; v++) {
        for (i = 0; i < SLEN; i++) {
            samples[v][i] = (int8_t)rnd();
        }
    }
    for (c = 0; c < CASES; c++) {
        vparm p[STDL_VOICES];
        const int nv = 1 + (int)(rnd() % STDL_VOICES);
        for (v = 0; v < nv; v++) {
            pick(&p[v]);
        }
        mix(p, nv, 0, a);
        mix(p, nv, 1, b);
        if (memcmp(a, b, sizeof(a)) != 0) {
            if (first < 0) {
                for (i = 0; a[i] == b[i]; i++) {
                }
                first = c;
                snprintf(line, sizeof(line), "case %d: %d voices, first difference "
                         "at frame %d: asm %d, C %d\n", c, nv, i, a[i], b[i]);
                if (f) {
                    fputs(line, f);
                }
            }
            fails++;
        }
    }
    /* timing with the device paused: nothing plays, so the VBL tick
     * neither mixes nor moves the voices */
    setup(0, 0);
    STDL_PauseVoices();
    while (!STDL_VoicesPaused()) {
    }
    tasm = time_path(0, 400);
    tc = time_path(1, 400);
    setup(0, 0);
    if (f) {
        fprintf(f, "%s: %d of %d cases differ\n", fails ? "FAIL" : "OK", fails, CASES);
        fprintf(f, "400 blocks of 4 voices: asm %ldms, C %ldms\n", tasm, tc);
        fclose(f);
    }
    if (screen) {
        STDL_Rect r = { 0, 0, 320, 200 };
        STDL_SetColour(1, fails ? 0x700 : 0x070);
        STDL_FillRect(screen, &r, 1);
    }
    for (;;) {
        STDL_Event ev;
        if (STDL_PollEvent(&ev) && ev.type == STDL_KEYDOWN) {
            break;
        }
    }
    STDL_CloseVoices();
    STDL_Quit();
    return fails ? 1 : 0;
}
