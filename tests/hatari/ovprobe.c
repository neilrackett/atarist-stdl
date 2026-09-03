/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * Bottom-border probe: opens the bottom border (and the top too if
 * a file called OVTOP.FLG sits beside the program), prints the
 * calibration src/overscan.c arrived at, holds the border open for
 * 250 frames and prints the miss counter three times - after
 * opening, after painting, and at the end. Not built by the
 * Makefile; it reads the library's internal tables, so it links
 * against the objects of the tree it is measuring:
 *
 *   STCMD_NO_TTY=1 stcmd m68k-atari-mint-gcc -O2 -std=gnu99 \
 *       -Iinclude -o tests/hatari/out/OVPROBE.TOS \
 *       tests/hatari/ovprobe.c libstdl.a
 *   TOS=... MACHINE=st FF=off \
 *       EXTRA="--cpu-exact on --trace video_sync --trace-file t.txt" \
 *       tests/hatari/run.sh OVPROBE tests/hatari/out/OVPROBE.TOS 25 \
 *       "waitfor OVPROBE END;sleep 0.5"
 *
 * The trace then lists every write to $ff820a with its line and
 * cycle: the 60Hz write must sit on line 262 inside 377..500 and
 * the 50Hz one on line 263 before cycle 52 (or on 262 past 502).
 * With OVZERO.FLG beside the program the dbra tables are zeroed
 * after opening, which is how the fixed costs in ovsc_table() were
 * measured: the write positions are then the poll's read position
 * plus the path's own cost. OVTICK.FLG forces the Timer B timed
 * path, for measuring it on a machine whose counter is live.
 */

#include <stdio.h>
#include <stdl/stdl.h>

extern uint16_t stdl_ovsc_n1[16];
extern uint16_t stdl_ovsc_n2, stdl_ovsc_n2t, stdl_ovsc_postn;
extern uint8_t  stdl_ovsc_tick;
extern uint8_t  stdl_ovsc_l262lo, stdl_ovsc_l262mid, stdl_ovsc_tbseen;

static void paint(STDL_Surface *s)
{
    STDL_Rect r;
    int y;

    r.x = 0; r.y = 0; r.w = 320; r.h = (uint16_t)s->h;
    STDL_FillRect(s, &r, 1);
    /* a line every four rows and a diagonal: a dropped or shifted
     * row breaks both */
    for (y = 0; y < s->h; y += 4) {
        r.x = 16; r.y = (int16_t)y; r.w = 288; r.h = 1;
        STDL_FillRect(s, &r, (uint8_t)(3 + ((y >> 2) & 3)));
    }
    for (y = 0; y < s->h; y++) {
        r.x = (int16_t)(8 + (y % 300)); r.y = (int16_t)y; r.w = 2; r.h = 1;
        STDL_FillRect(s, &r, 15);
    }
    r.x = 0; r.y = 0; r.w = 320; r.h = 1; STDL_FillRect(s, &r, 15);
    r.y = (int16_t)(s->h - 1); STDL_FillRect(s, &r, 15);
    r.x = 0; r.y = 0; r.w = 1; r.h = (uint16_t)s->h; STDL_FillRect(s, &r, 15);
    r.x = 319; STDL_FillRect(s, &r, 15);
}

int main(int argc, char *argv[])
{
    STDL_Surface *screen;
    FILE *f;
    int i, h, top = 0, zero = 0, tick = 0, toponly = 0;
    uint32_t frames, m_open, m_paint;

    (void)argc; (void)argv;
    if ((f = fopen("OVTOP.FLG", "r")) != NULL) { top = 1; fclose(f); }
    if ((f = fopen("OVZERO.FLG", "r")) != NULL) { zero = 1; fclose(f); }
    if ((f = fopen("OVTOPONLY.FLG", "r")) != NULL) { toponly = 1; top = 1; fclose(f); }
    if ((f = fopen("OVTICK.FLG", "r")) != NULL) { tick = 1; fclose(f); }

    if (STDL_Init(STDL_INIT_VIDEO) < 0) {
        return 1;
    }
    screen = STDL_SetVideoMode(320, 200, 4, 0);
    if (screen == NULL) {
        STDL_Quit();
        return 1;
    }
    if (top) {
        STDL_OpenTopBorder();
    }
    h = toponly ? STDL_GetVideoSurface()->h : STDL_OpenBottomBorder();
    fprintf(stderr, "OVPROBE h=%d err=%s\n", h, h ? "" : STDL_GetError());
    fprintf(stderr, "OVPROBE l262=%02x%02x tick=%u n2=%u n2t=%u postn=%u n1=",
            stdl_ovsc_l262mid, stdl_ovsc_l262lo, stdl_ovsc_tick,
            stdl_ovsc_n2, stdl_ovsc_n2t, stdl_ovsc_postn);
    for (i = 0; i < 16; i++) {
        fprintf(stderr, "%u ", stdl_ovsc_n1[i]);
    }
    fprintf(stderr, "\n");
    m_open = STDL_OverscanMisses();
    if (zero) {
        for (i = 0; i < 16; i++) {
            stdl_ovsc_n1[i] = 0;
        }
        stdl_ovsc_n2 = 0;
        stdl_ovsc_n2t = 0;
    }
    if (tick) {
        stdl_ovsc_tick = 1;
    }
    paint(screen);
    m_paint = STDL_OverscanMisses();
    for (frames = 0; frames < 250; frames++) {
        STDL_Event ev;
        while (STDL_PollEvent(&ev)) {
        }
        STDL_WaitVBL();
    }
    fprintf(stderr, "OVPROBE END misses open=%lu paint=%lu run=%lu tbseen=%u\n",
            (unsigned long)m_open, (unsigned long)m_paint,
            (unsigned long)STDL_OverscanMisses(), stdl_ovsc_tbseen);
    STDL_CloseBottomBorder();
    STDL_CloseTopBorder();
    STDL_Quit();
    return 0;
}
