/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * What a Falcon makes of a program written for the STE: the desktop
 * it was started from, the mode and colours that desktop gets back,
 * and sound asked for at 6258Hz, which a Falcon's DMA does not have.
 *
 * Prints the machine and Getrez(), and on a Falcon VgetMonitor() and
 * the desktop's VsetMode word, then FC:start. Holds sixteen bars for
 * two seconds, plays a 98Hz triangle for three on a voice opened at
 * 6258, opens the ring device at 6258 to see the rate it gets, and
 * tries STDL_PlaySample at 6258 and at 12517 (a second each). What
 * happened is printed once the program has left its mode, ending
 * FC:done.
 *
 * Take a screenshot of the bars and one of the desktop afterwards, and
 * record the sound. On a Falcon the triangle has to be heard, the ring
 * has to get 12517 and the 6258 sample has to play (from the library's
 * copy at 12517); on an STE all three play at 6258. A Falcon desktop worth trying is one Getrez()
 * cannot describe: Hatari's --tos-res ttlow is 320x480 in 256 colours
 * on VGA, ttmed 640x480 in 16.
 *
 * Built with -DFALCCHK_SCRAMBLE it first does what a Falcon program run
 * before it might have left behind: the DMA playback connected to
 * nothing, at a Falcon clock, and the DAC's mixer taking the ADC alone.
 * The library has to route the sound back for it to be heard, and put
 * the crossbar back as it found it at exit (FC:crossbar set and
 * after have to match).
 *
 *   STCMD_NO_TTY=1 stcmd m68k-atari-mint-gcc -O2 -std=gnu99 \
 *       -Iinclude -Ilib/xpad/src -o dist/FALCCHK.TOS \
 *       tests/hatari/falcchk.c libstdl.a
 *   TOS="<a TOS 4.04 image>" MACHINE=falcon \
 *       EXTRA="--memsize 4 --monitor vga --tos-res ttlow" \
 *       tests/hatari/run.sh fc dist/FALCCHK.TOS 8 \
 *       "waitfor FC:start; sleep 1; shot; waitfor FC:done; sleep 3; shot"
 */
#include <stdio.h>
#include <string.h>
#include <osbind.h>
#include <mint/falcon.h>
#include <stdl/stdl.h>
#include "../../examples/hold.h"

static int8_t tri[64];

#ifdef FALCCHK_SCRAMBLE
static long xb_regs;

static long read_xb(void)
{
    xb_regs = ((long)*(volatile uint16_t *)0xFFFF8930UL << 16)
              | *(volatile uint16_t *)0xFFFF8932UL;
    return 0;
}
#endif

static void silence(void *userdata, uint8_t *stream, int len)
{
    (void)userdata;
    memset(stream, 0, (size_t)len);
}

/* a call's outcome, kept until there is a console to print it on */
static void outcome(char *out, size_t n, int rc, const char *ok)
{
    strncpy(out, rc == 0 ? ok : STDL_GetError(), n - 1);
    out[n - 1] = 0;
}

int main(void)
{
    const STDL_MachineInfo *m;
    STDL_Surface *screen;
    STDL_Colour cols[16];
    STDL_AudioSpec want, got;
    STDL_Rect r;
    char voices[80], ring[80], s6258[80], s12517[80];
    int8_t *buf;
    int i, rc;

    hold_on_exit("FALCCHK");

    if (STDL_Init(STDL_INIT_VIDEO | STDL_INIT_AUDIO) < 0) {
        return 1;
    }
    m = STDL_GetMachineInfo();
    printf("FC:mch=%08lx rez=%d", (unsigned long)m->mch_cookie,
           (int)Getrez());
    if ((m->mch_cookie >> 16) == 3) {
        printf(" monitor=%d vmode=%04x", (int)VgetMonitor(),
               (unsigned)(VsetMode(-1) & 0xFFFF));
    }
    printf("\nFC:start\n");
    fflush(stdout);

    screen = STDL_SetVideoMode(320, 200, 4, 0);
    if (screen == NULL) {
        printf("FC:mode refused: %s\nFC:done\n", STDL_GetError());
        STDL_Quit();
        return 1;
    }
    for (i = 0; i < 16; i++) {
        cols[i].r = (uint8_t)(i * 17);
        cols[i].g = (uint8_t)((i & 3) * 85);
        cols[i].b = (uint8_t)(255 - i * 17);
        cols[i].unused = 0;
        r.x = (int16_t)(i * 20); r.y = 0; r.w = 20; r.h = 200;
        STDL_FillRect(screen, &r, (uint8_t)i);
    }
    STDL_SetColours(screen, cols, 0, 16);
    STDL_Delay(2000);

    /* 64 samples a cycle, continuous across the loop: 98Hz at 6258 */
    for (i = 0; i < 32; i++) {
        tri[i] = (int8_t)(i * 8 - 128);
        tri[63 - i] = tri[i];
    }
#ifdef FALCCHK_SCRAMBLE
    if ((m->mch_cookie >> 16) == 3) {
        (void)Devconnect(DMAPLAY, 0, CLK25M, 1, NO_SHAKE);
        (void)Soundcmd(ADDERIN, 1);
    }
    (void)Supexec(read_xb);
    printf("FC:crossbar set %08lx\n", xb_regs);
#endif
    rc = STDL_OpenVoices(6258);
    outcome(voices, sizeof voices, rc, "opened at 6258");
    if (rc == 0) {
        STDL_SetVoice(0, tri, 64, 0, 64, 6258, 64);
        STDL_Delay(3000);
        STDL_CloseVoices();
    }

    memset(&want, 0, sizeof want);
    want.freq = 6258;
    want.format = STDL_AUDIO_S8;
    want.channels = 1;
    want.samples = 512;
    want.callback = silence;
    rc = STDL_OpenAudio(&want, &got);
    outcome(ring, sizeof ring, rc, "");
    if (rc == 0) {
        snprintf(ring, sizeof ring, "got %d", got.freq);
        STDL_CloseAudio();
    }

    /* ST RAM, which the DMA reads: a second at 12517 */
    buf = (int8_t *)Mxalloc(12518L, 0);
    if (buf == NULL) {
        strcpy(s6258, "no ST RAM");
        strcpy(s12517, "no ST RAM");
    } else {
        for (i = 0; i < 12516; i++) {
            buf[i] = tri[i & 63];
        }
        rc = STDL_PlaySample(buf, 12516, 6258);
        outcome(s6258, sizeof s6258, rc, "played");
        if (rc == 0) {
            STDL_Delay(1000);
            STDL_StopSample();
        }
        rc = STDL_PlaySample(buf, 12516, 12517);
        outcome(s12517, sizeof s12517, rc, "played");
        if (rc == 0) {
            STDL_Delay(1000);
            STDL_StopSample();
        }
    }

    STDL_Quit();
#ifdef FALCCHK_SCRAMBLE
    (void)Supexec(read_xb);
    printf("FC:crossbar after %08lx\n", xb_regs);
#endif
    printf("FC:voices %s\nFC:ring at 6258 %s\n", voices, ring);
    printf("FC:sample at 6258 %s\nFC:sample at 12517 %s\nFC:done\n",
           s6258, s12517);
    if (buf != NULL) {
        Mfree(buf);
    }
    return 0;
}
