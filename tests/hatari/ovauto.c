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
 *   measure=0|1|2  0 scaled tables, 1 self-measured (default), 2
 *                  runs every position both ways, scaled first
 *   test=N[,L]     a GLUE test cycle to centre the 60Hz pulse on;
 *                  repeat the line to sweep (default: the library's).
 *                  An optional ,L sets the counter lead in cycles for
 *                  that entry (measured tables only; default the
 *                  library's, 14)
 *   lead=L         the lead for entries without their own
 *   reset=1        after the borders close, switch the Shifter to
 *                  hi-res for a moment and back: does the desktop
 *                  come back with its colours right after a wide=
 *                  run has rotated the plane phase?
 *   wide=N         a test value run with the first version's pulse
 *                  shape instead (60Hz at N-44, 50Hz at N+33, into
 *                  the next line): the known-bad reference
 *
 * For each test value the borders are opened (once; the tables are
 * rebuilt for each value while open), N frames run, and per frame
 * the probe records whether the bottom ISR saw its border open and
 * how many polls the ISR counted from its 50Hz write until line
 * 263's fetch began: the distance from the restore to the line
 * boundary, which is what the Shifter's plane phase depends on. A
 * slipped frame reads like a good one to every timing measurement,
 * so the count stands in for the eye: a run with the old placement
 * (wide=) gives the count a bad frame has, the good positions a
 * larger one, and a position is safe when its whole distribution
 * sits above the bad one's. Written to OVAUTO.TXT and the console
 * after the borders close. Exit 0 when every position ran, 2 for a
 * missing or empty config, 1 when a border would not open.
 *
 *   STCMD_NO_TTY=1 stcmd m68k-atari-mint-gcc -O2 -std=gnu99 \
 *       -Iinclude -o OVAUTO.TOS tests/hatari/ovauto.c libstdl.a
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <osbind.h>
#include <stdl/stdl.h>

extern uint16_t stdl_ovsc_n1[32], stdl_ovsc_n2t, stdl_ovsc_postn;
extern uint16_t stdl_ovsc_test;
extern uint8_t  stdl_ovsc_wide;
extern uint8_t  stdl_ovsc_tick, stdl_ovsc_diag, stdl_ovsc_dpolls, stdl_ovsc_dfirst, stdl_ovsc_dlast;
extern uint8_t  stdl_ovsc_botok, stdl_ovsc_topok;
extern uint16_t stdl_ovsc_cal_gap, stdl_ovsc_cal_wait;
extern uint16_t stdl_ovsc_cal_v0, stdl_ovsc_cal_v1;
extern uint16_t stdl_ovsc_cal_lines, stdl_ovsc_cal_c;
extern uint8_t  stdl_ovsc_cal_live, stdl_ovsc_cal_try, stdl_ovsc_cal_off[5];
extern uint16_t stdl_ovsc_mF, stdl_ovsc_mTURN, stdl_ovsc_mNOP, stdl_ovsc_msamples;
extern int16_t  stdl_ovsc_mFIRST;
extern uint8_t  stdl_ovsc_measured, stdl_ovsc_pre;
extern int16_t  stdl_ovsc_lead;
extern uint16_t stdl_ovsc_found, stdl_ovsc_search_frames;
extern uint16_t stdl_ovsc_pre_cyc, stdl_ovsc_pre_np, stdl_ovsc_pre_nm, stdl_ovsc_pre_parks;
extern void     stdl_ovsc_retable(void);
extern void     stdl_ovsc_shifter_reset(void);

#define MAXTESTS 16

static int   tests[MAXTESTS], wides[MAXTESTS], leads[MAXTESTS], ntests;
static int   lead_default = -1;
static int   frames = 200, force = 0, measure = 1, reset = 0;
static char  mode = 'b';

/* Output goes through GEMDOS directly - Cconws for the console and
 * Fcreate/Fwrite for the file - not through stdio: under a
 * cartridge that hooks GEMDOS (the unattended runner) the stdio
 * path bombed at launch with an address error, while raw calls in
 * another program ran clean. Everything is buffered; nothing is
 * printed or written until the borders are closed and STDL has
 * shut down, so no GEMDOS call runs while the vectors are ours. */
static char  outbuf[4096];
static int   outlen;

static void say(const char *fmt, ...)
{
    char line[200];
    va_list ap;
    int n;
    va_start(ap, fmt);
    n = vsnprintf(line, sizeof line, fmt, ap);
    va_end(ap);
    if (n < 0) {
        return;
    }
    if (n >= (int)sizeof line) {
        n = (int)sizeof line - 1;
    }
    if (outlen + n < (int)sizeof outbuf) {
        memcpy(outbuf + outlen, line, (size_t)n);
        outlen += n;
    }
}

static void flush_file(void)
{
    long h;
    outbuf[outlen] = '\0';
    (void)Cconws(outbuf);
    h = Fcreate("OVAUTO.TXT", 0);
    if (h >= 0) {
        Fwrite((short)h, (long)outlen, outbuf);
        Fclose((short)h);
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
    static char buf[1024];
    long h, n;
    char *p, *end;

    h = Fopen("OVAUTO.CFG", 0);
    if (h < 0) {
        return 0;
    }
    n = Fread((short)h, (long)sizeof buf - 1, buf);
    Fclose((short)h);
    if (n <= 0) {
        return 0;
    }
    buf[n] = '\0';
    end = buf + n;
    for (p = buf; p < end; ) {
        char *line = p, *eq;
        while (p < end && *p != '\n' && *p != '\r') {
            p++;
        }
        while (p < end && (*p == '\n' || *p == '\r')) {
            *p++ = '\0';
        }
        eq = strchr(line, '=');
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
        } else if (strcmp(line, "measure") == 0) {
            measure = atoi(eq);
        } else if (strcmp(line, "lead") == 0) {
            lead_default = atoi(eq);
        } else if (strcmp(line, "reset") == 0) {
            reset = atoi(eq);
        } else if ((strcmp(line, "test") == 0 || strcmp(line, "wide") == 0)
                   && ntests < MAXTESTS) {
            char *c = strchr(eq, ',');
            leads[ntests] = -1;
            if (c != NULL) {
                *c++ = '\0';
                leads[ntests] = atoi(c);
            }
            wides[ntests] = (line[0] == 'w');
            tests[ntests++] = atoi(eq);
        }
    }
    return 1;
}

int main(void)
{
    STDL_Surface *screen;
    int t, h = 0, status = 0;

    if (!read_cfg()) {
        (void)Cconws("OVAUTO: no OVAUTO.CFG\r\n");
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

    for (t = 0; t < ntests * (measure == 2 ? 2 : 1); t++) {
        uint32_t m0, m1;
        int f, open_ok = 0, hist[17], other = 0;
        int i, tt = (measure == 2) ? t / 2 : t;
        int use_measured = (measure == 2) ? (t & 1) : (measure != 0);
        static uint8_t could_measure;

        if (tests[tt] != 0) {
            stdl_ovsc_test = (uint16_t)tests[tt];
        }
        stdl_ovsc_wide = (uint8_t)wides[tt];
        if (leads[tt] >= 0) {
            stdl_ovsc_lead = (int16_t)leads[tt];
        } else if (lead_default >= 0) {
            stdl_ovsc_lead = (int16_t)lead_default;
        }
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
            could_measure = stdl_ovsc_measured;
        }
        stdl_ovsc_measured = (uint8_t)(use_measured && could_measure);
        stdl_ovsc_retable();
        if (force) {
            stdl_ovsc_tick = 0;
        }
        for (i = 0; i < 17; i++) {
            hist[i] = 0;
        }
        m0 = STDL_OverscanMisses();
        STDL_WaitVBL();
        STDL_WaitVBL();
        for (f = 0; f < frames; f++) {
            STDL_Event ev;
            while (STDL_PollEvent(&ev)) {
            }
            STDL_WaitVBL();
            if (mode != 't') {
                open_ok += stdl_ovsc_botok ? 1 : 0;
                if (stdl_ovsc_dpolls < 16) {
                    hist[stdl_ovsc_dpolls]++;
                } else {
                    other++;
                }
            } else {
                open_ok += stdl_ovsc_topok ? 1 : 0;
            }
        }
        m1 = STDL_OverscanMisses();
        if (outlen == 0) {
            say("OVAUTO mode=%c h=%d frames=%d force=%d machine=%08lx\r\n",
                mode, h, frames, force,
                (unsigned long)STDL_GetMachineInfo()->mch_cookie);
            say("cal gap=%u wait=%u v0=%u v1=%u lines=%u c16=%u live=%u tick=%u tries=%u\r\n",
                stdl_ovsc_cal_gap, stdl_ovsc_cal_wait, stdl_ovsc_cal_v0,
                stdl_ovsc_cal_v1, stdl_ovsc_cal_lines, stdl_ovsc_cal_c,
                stdl_ovsc_cal_live, stdl_ovsc_tick, stdl_ovsc_cal_try);
            say("live samples moving<<3|fast=%u,%u,%u,%u,%u (live: moving>=6, fast=0)\r\n",
                stdl_ovsc_cal_off[0], stdl_ovsc_cal_off[1], stdl_ovsc_cal_off[2],
                stdl_ovsc_cal_off[3], stdl_ovsc_cal_off[4]);
            say("search found=%u (test set to %u) in %u frames\r\n",
                stdl_ovsc_found, stdl_ovsc_found ? stdl_ovsc_found + 2 : 0,
                stdl_ovsc_search_frames);
            say("measured=%u samples=%u F=%u turn=%u first=%d nop=%u (bytes x16) pre=%u bytes (moving %u of 512 cycles; polls parked %u moving %u parks %u)\r\n",
                could_measure, stdl_ovsc_msamples, stdl_ovsc_mF,
                stdl_ovsc_mTURN, stdl_ovsc_mFIRST, stdl_ovsc_mNOP,
                stdl_ovsc_pre, stdl_ovsc_pre_cyc, stdl_ovsc_pre_np,
                stdl_ovsc_pre_nm, stdl_ovsc_pre_parks);
        }
        say("%s=%u %s lead=%d open=%d/%d misses=%lu polls[0..15]=",
            wides[tt] ? "wide" : "test", stdl_ovsc_test,
            stdl_ovsc_measured ? "measured" : "scaled",
            stdl_ovsc_measured ? (int)stdl_ovsc_lead : 0, open_ok, frames,
            (unsigned long)(m1 - m0));
        for (i = 0; i < 16; i++) {
            say("%d%s", hist[i], (i < 15) ? "," : "");
        }
        say(" 16+=%d first=%02x last=%02x n1=", other, stdl_ovsc_dfirst,
            stdl_ovsc_dlast);
        for (i = 0; i < 16; i++) {
            say("%u%s", stdl_ovsc_n1[i], (i < 15) ? "," : "");
        }
        say(" n2t=%u\r\n", stdl_ovsc_n2t);
    }
    STDL_CloseBottomBorder();
    STDL_CloseTopBorder();
    if (reset) {
        stdl_ovsc_shifter_reset();
        say("shifter reset done\r\n");
    }
    say("OVAUTO END status=%d\r\n", status);
    STDL_Quit();
    flush_file();
    return status;
}
