/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * On-target probe: an STM music stream playing underneath the tone
 * device, with an effect stealing a voice on top. This is the one
 * interaction tests/host cannot reach - music.c is not linked into
 * test_tone, so the host suite only ever exercises the hand-back
 * path against the silence branch - and it is the interaction an
 * ownership bug breaks first, because a tone voice whose owned bit
 * is clear is one the music stream may overwrite every frame.
 *
 * Build it by hand against the library, and keep the stem to eight
 * characters (Hatari's GEMDOS drive maps names to 8.3):
 *
 *   STCMD_NO_TTY=1 stcmd m68k-atari-mint-gcc -O2 -std=gnu99 \
 *       -Iinclude -Iinclude/compat -Ilib/xpad/src \
 *       -o dist/TONEMUS.TOS tests/hatari/tonemus.c libstdl.a
 *
 * Run it with sound and a capture, pointing szYMCaptureFileName in
 * a copy of the Hatari config somewhere writable:
 *
 *   MACHINE=megaste SOUND=on FF=off EXTRA="--configfile <copy>" \
 *     tests/hatari/run.sh tonemus dist/TONEMUS.TOS 10 \
 *     "waitfor TONEMUS: ready;fifo hatari-shortcut recsound;\
 *      sleep 24;fifo hatari-shortcut recsound;sleep 2"
 *
 * Then read the capture as a spectrogram. What it should show, in
 * order: the tone at 220Hz steady while the music's bass keeps
 * changing underneath it; three tones at 220/277/330 with the music
 * silent (the 832 and 994 lines are their own third harmonics); the
 * speaker at 1200 with 220 gone; 220 back when it stops; and the
 * music playing again on all three voices once the tones are off.
 */
#include <stdio.h>
#include "stdl/stdl.h"

static void step(const char *what, int ms)
{
    printf("TONEMUS: %s\r\n", what);
    fflush(stdout);
    STDL_Delay(ms);
}

int main(void)
{
    STDL_Music *mus;

    if (STDL_Init(STDL_INIT_VIDEO | STDL_INIT_AUDIO) < 0) {
        printf("init failed: %s\r\n", STDL_GetError());
        return 1;
    }
    mus = STDL_LoadMusic("DEMO.STM");
    if (mus == NULL) {
        printf("TONEMUS: no DEMO.STM: %s\r\n", STDL_GetError());
        STDL_Quit();
        return 1;
    }
    printf("TONEMUS: ready\r\n");
    fflush(stdout);
    STDL_Delay(4000);               /* time to start recording */
    STDL_PlayMusic(mus, -1);
    step("music alone", 4000);
    STDL_ToneOn(0, STDL_YM_PERIOD(220), 12);
    step("tone A over music", 4000);
    STDL_ToneOn(1, STDL_YM_PERIOD(277), 12);
    STDL_ToneOn(2, STDL_YM_PERIOD(330), 12);
    step("three tones, music should have no voices", 4000);
    STDL_SpeakerOn(1200, 14);
    step("speaker steals one from the tones", 2000);
    STDL_SpeakerOff();
    step("speaker off, tone back", 2000);
    STDL_ToneOff(-1);
    step("tones off, music should have its voices back", 4000);
    STDL_HaltMusic();
    STDL_FreeMusic(mus);
    STDL_Quit();
    printf("TONEMUS: done\r\n");
    return 0;
}
