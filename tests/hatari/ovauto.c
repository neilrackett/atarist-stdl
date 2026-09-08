/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * Unattended bottom-border probe for real hardware: no keypress,
 * results in a file, an exit status. Driven by OVAUTO.CFG beside
 * the program, one "key=value" a line:
 *
 *   mode=b|t|c     bottom, top-only, or combined (default b)
 *   frames=N       frames per position (default 200)
 *   force=0|1      1 forces the counter path whatever the liveness
 *                  check said (default 0)
 *   test=N         a GLUE test cycle to centre the 60Hz pulse on;
 *                  repeat the line to sweep (default: the library's)
 *   wide=N         a test value run with the first version's pulse
 *                  shape instead (60Hz at N-44, 50Hz at N+33, into
 *                  the next line): the known-bad reference
 *
 * For each test value the borders are opened (once; the tables are
 * rebuilt for each value while open), N frames run, and per frame
 * the probe records whether the bottom ISR saw its border open and
 * the three counter reads the ISR takes at the same CPU-timed
 * offset into lines 263, 264 and 265. Those give two line lengths
 * in counter steps of 4 cycles; a line 263 cut to 508 cycles by a
 * 50Hz write that arrived too late - the plane-phase slip that
 * makes every colour wrong - shows as a slip of +2 against the
 * normal line after it, which a human at the screen otherwise has
 * to judge by eye. Written to OVAUTO.TXT and the console after the
 * borders close. Exit 0 when every position ran, 2 for a missing
 * or empty config, 1 when a border would not open.
 *
 *   STCMD_NO_TTY=1 stcmd m68k-atari-mint-gcc -O2 -std=gnu99 \
 *       -Iinclude -o OVAUTO.TOS tests/hatari/ovauto.c libstdl.a
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdl/stdl.h>

extern uint16_t stdl_ovsc_n1[32], stdl_ovsc_n2t, stdl_ovsc_postn;
extern uint16_t stdl_ovsc_test;
extern uint8_t  stdl_ovsc_wide;
extern uint8_t  stdl_ovsc_tick, stdl_ovsc_diag, stdl_ovsc_dlo[3];
extern uint8_t  stdl_ovsc_botok, stdl_ovsc_topok;
extern uint16_t stdl_ovsc_cal_gap, stdl_ovsc_cal_wait;
extern uint16_t stdl_ovsc_cal_v0, stdl_ovsc_cal_v1;
extern uint16_t stdl_ovsc_cal_lines, stdl_ovsc_cal_c;
extern uint8_t  stdl_ovsc_cal_live, stdl_ovsc_cal_try, stdl_ovsc_cal_off[5];
extern uint16_t stdl_ovsc_mF, stdl_ovsc_mTURN, stdl_ovsc_mNOP, stdl_ovsc_msamples;
extern int16_t  stdl_ovsc_mFIRST;
extern uint8_t  stdl_ovsc_measured;
extern void     stdl_ovsc_retable(void);

#define MAXTESTS 16

static int   tests[MAXTESTS], wides[MAXTESTS], ntests;
static int   frames = 200, force = 0;
static char  mode = 'b';
static FILE *out;

static void say(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    if (out != NULL) {
        va_start(ap, fmt);
        vfprintf(out, fmt, ap);
        va_end(ap);
    }
}

static void paint(STDL_Surface *s)
{
    STDL_Rect r;
    int y;
    r.x = 0; r.y = 0; r.w = 320; r.h = s->h;
    STDL_FillRect(s, &r, 0);
    for (y = 0; y < s->h; y += 8) {
        r.x = 0; r.y = (int16_t)y; r.w = 320; r.h = 4;
        STDL_FillRect(s, &r, (uint8_t)(1 + ((y >> 3) & 7)));
    }
}

static int read_cfg(void)
{
    FILE *f = fopen("OVAUTO.CFG", "r");
    char line[64];
    if (f == NULL) {
        return 0;
    }
    while (fgets(line, sizeof line, f) != NULL) {
        char *eq = strchr(line, '=');
        if (eq == NULL) {
            continue;
        }
        *eq++ = '\0';
        if (strcmp(line, "mode") == 0) {
            mode = (char)eq[0];
        } else if (strcmp(line, "frames") == 0) {
            frames = atoi(eq);
        } else if (strcmp(line, "force") == 0) {
            force = atoi(eq);
        } else if (strcmp(line, "test") == 0 && ntests < MAXTESTS) {
            wides[ntests] = 0;
            tests[ntests++] = atoi(eq);
        } else if (strcmp(line, "wide") == 0 && ntests < MAXTESTS) {
            wides[ntests] = 1;
            tests[ntests++] = atoi(eq);
        }
    }
    fclose(f);
    return 1;
}

int main(void)
{
    STDL_Surface *screen;
    int t, h = 0, status = 0;

    if (!read_cfg()) {
        fprintf(stderr, "OVAUTO: no OVAUTO.CFG\r\n");
        return 2;
    }
    if (ntests == 0) {
        tests[ntests++] = 0;                /* the library's own */
    }
    if (frames < 1) {
        frames = 1;
    }
    if (STDL_Init(STDL_INIT_VIDEO) < 0) {
        return 1;
    }
    screen = STDL_SetVideoMode(320, 200, 4, 0);
    if (screen == NULL) {
        STDL_Quit();
        return 1;
    }
    stdl_ovsc_diag = 1;

    for (t = 0; t < ntests; t++) {
        uint32_t m0, m1;
        int f, open_ok = 0, hist[9], dab[8], ndab = 0, other = 0;
        int i;

        if (tests[t] != 0) {
            stdl_ovsc_test = (uint16_t)tests[t];
        }
        stdl_ovsc_wide = (uint8_t)wides[t];
        if (t == 0) {
            if (mode == 't' || mode == 'c') {
                h = STDL_OpenTopBorder();
            }
            if (mode == 'b' || mode == 'c') {
                h = STDL_OpenBottomBorder();
            }
            if (h == 0) {
                say("OVAUTO open failed: %s\r\n", STDL_GetError());
                status = 1;
                break;
            }
            paint(STDL_GetVideoSurface());
        } else {
            stdl_ovsc_retable();
        }
        if (force) {
            stdl_ovsc_tick = 0;
        }
        for (i = 0; i < 9; i++) {
            hist[i] = 0;
        }
        for (i = 0; i < 8; i++) {
            dab[i] = -1;
        }
        m0 = STDL_OverscanMisses();
        STDL_WaitVBL();
        STDL_WaitVBL();
        for (f = 0; f < frames; f++) {
            STDL_Event ev;
            int a, b, c, d1, d2, slip;
            while (STDL_PollEvent(&ev)) {
            }
            STDL_WaitVBL();
            if (mode != 't') {
                open_ok += stdl_ovsc_botok ? 1 : 0;
                a = stdl_ovsc_dlo[0];
                b = stdl_ovsc_dlo[1];
                c = stdl_ovsc_dlo[2];
                d1 = (b - a) & 0xFF;
                d2 = (c - b) & 0xFF;
                slip = d1 - d2;
                if (slip >= -4 && slip <= 4) {
                    hist[slip + 4]++;
                } else {
                    other++;
                }
                for (i = 0; i < 8; i++) {
                    if (dab[i] == d1) {
                        break;
                    }
                    if (dab[i] < 0) {
                        dab[i] = d1;
                        ndab++;
                        break;
                    }
                }
            } else {
                open_ok += stdl_ovsc_topok ? 1 : 0;
            }
        }
        m1 = STDL_OverscanMisses();
        if (out == NULL) {
            out = fopen("OVAUTO.TXT", "w");
            say("OVAUTO mode=%c h=%d frames=%d force=%d machine=%08lx\r\n",
                mode, h, frames, force,
                (unsigned long)STDL_GetMachineInfo()->mch_cookie);
            say("cal gap=%u wait=%u v0=%u v1=%u lines=%u c16=%u live=%u tick=%u tries=%u\r\n",
                stdl_ovsc_cal_gap, stdl_ovsc_cal_wait, stdl_ovsc_cal_v0,
                stdl_ovsc_cal_v1, stdl_ovsc_cal_lines, stdl_ovsc_cal_c,
                stdl_ovsc_cal_live, stdl_ovsc_tick, stdl_ovsc_cal_try);
            say("live samples off=%u,%u,%u,%u,%u (live when 8<off<156)\r\n",
                stdl_ovsc_cal_off[0], stdl_ovsc_cal_off[1], stdl_ovsc_cal_off[2],
                stdl_ovsc_cal_off[3], stdl_ovsc_cal_off[4]);
            say("measured=%u samples=%u F=%u turn=%u first=%d nop=%u (bytes x16)\r\n",
                stdl_ovsc_measured, stdl_ovsc_msamples, stdl_ovsc_mF,
                stdl_ovsc_mTURN, stdl_ovsc_mFIRST, stdl_ovsc_mNOP);
        }
        say("%s=%u open=%d/%d misses=%lu slip[-4..+4]=",
            wides[t] ? "wide" : "test", stdl_ovsc_test, open_ok, frames,
            (unsigned long)(m1 - m0));
        for (i = 0; i < 9; i++) {
            say("%d%s", hist[i], (i < 8) ? "," : "");
        }
        say(" other=%d dAB=", other);
        for (i = 0; i < ndab; i++) {
            say("%d%s", dab[i], (i < ndab - 1) ? "," : "");
        }
        say(" n1=");
        for (i = 0; i < 16; i++) {
            say("%u%s", stdl_ovsc_n1[i], (i < 15) ? "," : "");
        }
        say(" n2t=%u\r\n", stdl_ovsc_n2t);
    }
    STDL_CloseBottomBorder();
    STDL_CloseTopBorder();
    say("OVAUTO END status=%d\r\n", status);
    if (out != NULL) {
        fclose(out);
    }
    STDL_Quit();
    return status;
}
