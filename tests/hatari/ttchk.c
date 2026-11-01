/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * What a TT (or a Falcon) makes of a program written for the ST: the
 * facts the library's TT and Falcon handling rests on, measured on the
 * machine rather than assumed.
 *
 *  - the cookies (_MCH, _CPU, _VDO, _SND), Getrez(), the TT's EgetShift
 *    word or the Falcon's monitor and VsetMode word, the MFP's monitor
 *    bit (GPIP 7, clear on a mono monitor) and, on a 68020 or later,
 *    CACR (bit 0 the instruction cache, bit 8 the data cache);
 *  - the display rate: _frclock counted against the 200Hz timer, on
 *    the desktop, in the library's ST Low, and in TT Low or a Falcon's
 *    8-bit mode;
 *  - Timer B's Display Enable events in a frame, in those modes (an ST
 *    gives 200 in ST Low; the counter is read modulo 256);
 *  - on a TT, whether a write to $FF8240 reaches TT register 0-15 of
 *    $FF8400 (the bank STDL's palette writes land in);
 *  - the library's own view: STDL_SetRefresh's answer, and the machine
 *    info;
 *  - the music's tempo: a silent STM stream's frames played in two
 *    seconds of the 200Hz timer, which has to be 50 a second whatever
 *    the display runs at (TT:music).
 *
 * It holds a test card in ST Low for three seconds (TT:card on stderr),
 * and on a TT or Falcon an 8-bit card for three (TT:card8), then quits
 * and prints the results, ending TT:done. Screenshot the cards (is ST
 * Low centred on a TT, how wide are its bands?) and the desktop after
 * (is it back, in its own mode and colours?).
 *
 * Hatari models a TT's video with the STE's timings, so its rate and
 * Timer B answers on a TT are the emulator's, not the machine's: this
 * is the program to hand to someone with a real TT or Falcon.
 *
 *   STCMD_NO_TTY=1 stcmd m68k-atari-mint-gcc -O2 -std=gnu99 \
 *       -Iinclude -Ilib/xpad/src -o dist/TTCHK.TOS \
 *       tests/hatari/ttchk.c libstdl.a
 *   TOS="<a TOS 3.06 image>" MACHINE=tt LATE="--mmu off" \
 *       tests/hatari/run.sh tt dist/TTCHK.TOS 8 \
 *       "waitfor TT:card;sleep 1;shot;waitfor TT:card8;sleep 1;shot;\
 *        waitfor TT:done;sleep 2;shot"
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <osbind.h>
#include <mint/falcon.h>
#include <stdl/stdl.h>

#define FRCLOCK  (*(volatile uint32_t *)0x466UL)
#define HZ200    (*(volatile uint32_t *)0x4BAUL)
#define GPIP     (*(volatile uint8_t *)0xFFFFFA01UL)
#define IERA     (*(volatile uint8_t *)0xFFFFFA07UL)
#define TBCR     (*(volatile uint8_t *)0xFFFFFA1BUL)
#define TBDR     (*(volatile uint8_t *)0xFFFFFA21UL)
#define STPAL    ((volatile uint16_t *)0xFFFF8240UL)
#define TTPAL    ((volatile uint16_t *)0xFFFF8400UL)

static char report[2048];

/* a silent STM stream: frame 0 turns the mixer off, the rest change
 * nothing; 50 frames a second, looping */
static uint8_t stm[12 + 3 + 499 * 2];

static STDL_Music *silent_music(void)
{
    memcpy(stm, "STM1", 4);
    stm[4] = 0; stm[5] = 50;            /* tick_hz */
    stm[6] = 500 >> 8; stm[7] = 500 & 255;
    stm[12] = 0; stm[13] = 0x80;        /* frame 0: register 7 */
    stm[14] = 0x3F;
    return STDL_MusicFromMemory(stm, sizeof(stm));
}
static size_t rlen;

static void say(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

static void say(const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    rlen += (size_t)vsnprintf(report + rlen, sizeof(report) - rlen, fmt, ap);
    va_end(ap);
    if (rlen >= sizeof(report)) {
        rlen = sizeof(report) - 1;
    }
}

static uint32_t cookie(uint32_t name, int *found)
{
    long *jar = *(long **)0x5A0UL;

    *found = 0;
    if (jar != NULL) {
        for (; jar[0] != 0; jar += 2) {
            if ((uint32_t)jar[0] == name) {
                *found = 1;
                return (uint32_t)jar[1];
            }
        }
    }
    return 0;
}

/* frames a second, to a tenth, over 20 frames; 0 if the VBL stands
 * still (bounded by the 200Hz timer) */
static int vbl_rate10(void)
{
    uint32_t f, t0, t1, start = HZ200;
    int n;

    f = FRCLOCK;
    while (FRCLOCK == f) {
        if (HZ200 - start > 100) {
            return 0;
        }
    }
    t0 = HZ200;
    for (n = 0; n < 20; n++) {
        f = FRCLOCK;
        while (FRCLOCK == f) {
            if (HZ200 - t0 > 400) {
                return 0;
            }
        }
    }
    t1 = HZ200;
    return (int)((20L * 2000L + (long)(t1 - t0) / 2) / (long)(t1 - t0));
}

/* Display Enable events in each of eight frames, modulo 256: Timer B
 * in event-count mode with its interrupt off, read at each VBL */
static void timerb_count(char *out, size_t n)
{
    uint8_t iera = IERA, prev, cur;
    uint32_t f;
    size_t len = 0;
    int i;

    IERA = (uint8_t)(iera & ~1);
    TBCR = 0;
    TBDR = 0;                   /* 0 counts 256 */
    TBCR = 8;                   /* event count */
    f = FRCLOCK;
    while (FRCLOCK == f) {
    }
    prev = TBDR;
    for (i = 0; i < 8; i++) {
        f = FRCLOCK;
        while (FRCLOCK == f) {
        }
        cur = TBDR;
        len += (size_t)snprintf(out + len, n - len, " %d",
                                (int)(uint8_t)(prev - cur));
        prev = cur;
    }
    TBCR = 0;
    IERA = iera;
}

static uint32_t cacr(void)
{
    uint32_t v;

    __asm__ volatile(".word 0x4e7a,0x0002\n\tmove.l %%d0,%0"
                     : "=d"(v) : : "d0");
    return v;
}

static void card4(STDL_Surface *s)
{
    STDL_Colour c[16];
    STDL_Rect r;
    int i;

    for (i = 0; i < 16; i++) {
        c[i].r = (uint8_t)(i & 1 ? 0xFF : i * 16);
        c[i].g = (uint8_t)(i & 2 ? 0xFF : i * 12);
        c[i].b = (uint8_t)(i & 4 ? 0xFF : i * 8);
    }
    c[0].r = c[0].g = c[0].b = 0;
    c[15].r = c[15].g = c[15].b = 0xFF;
    STDL_SetColours(s, c, 0, 16);
    for (i = 0; i < 16; i++) {
        r.x = (int16_t)(i * 20);
        r.y = 0;
        r.w = 20;
        r.h = 200;
        STDL_FillRect(s, &r, (uint8_t)i);
    }
    /* a white frame on the picture's edge: where are its corners? */
    r.x = 0; r.y = 0; r.w = 320; r.h = 2;
    STDL_FillRect(s, &r, 15);
    r.y = 198;
    STDL_FillRect(s, &r, 15);
    r.y = 0; r.w = 2; r.h = 200;
    STDL_FillRect(s, &r, 15);
    r.x = 318;
    STDL_FillRect(s, &r, 15);
}

/* 256 colours in 8 planes: column x of a 320-wide line is colour
 * (x / 20) * 16 + (y / 30), the bars' edges every 20 pixels */
static void card8(uint8_t *scr, int lines, int stride)
{
    int y, g, p;

    for (y = 0; y < lines; y++) {
        uint16_t *w = (uint16_t *)(scr + (long)y * stride);
        for (g = 0; g < 20; g++) {
            for (p = 0; p < 8; p++) {
                uint16_t bits = 0;
                int x;
                for (x = 0; x < 16; x++) {
                    int px = g * 16 + x;
                    int col = (px / 20) * 16 + ((y * 16 / lines) & 15);
                    if (y < 2 || y >= lines - 2 || px < 2 || px >= 318) {
                        col = 255;
                    }
                    if (col & (1 << p)) {
                        bits |= (uint16_t)(0x8000 >> x);
                    }
                }
                w[g * 8 + p] = bits;
            }
        }
    }
}

int main(void)
{
    const STDL_MachineInfo *m;
    STDL_Surface *screen;
    uint32_t mch, cpu, vdo, snd, f;
    int has_mch, has_cpu, has_vdo, has_snd, ismach;
    char tb[64];
    uint8_t *page8 = NULL, *raw = NULL;

    if (STDL_Init(STDL_INIT_VIDEO | STDL_INIT_AUDIO) < 0) {
        return 1;
    }
    m = STDL_GetMachineInfo();
    mch = cookie(0x5F4D4348UL, &has_mch);       /* _MCH */
    cpu = cookie(0x5F435055UL, &has_cpu);       /* _CPU */
    vdo = cookie(0x5F56444FUL, &has_vdo);       /* _VDO */
    snd = cookie(0x5F534E44UL, &has_snd);       /* _SND */
    ismach = (int)(mch >> 16);
    say("TT:_MCH=%08lx%s _CPU=%ld%s _VDO=%08lx%s _SND=%08lx%s\n",
        (unsigned long)mch, has_mch ? "" : "(none)", (long)cpu,
        has_cpu ? "" : "(none)", (unsigned long)vdo,
        has_vdo ? "" : "(none)", (unsigned long)snd,
        has_snd ? "" : "(none)");
    say("TT:desktop rez=%d gpip7=%d", (int)Getrez(), (GPIP & 0x80) ? 1 : 0);
    if (ismach == 2) {
        say(" eshift=%04x", (unsigned)(EgetShift() & 0xFFFF));
    }
    if (ismach == 3) {
        say(" monitor=%d vmode=%04x", (int)VgetMonitor(),
            (unsigned)(VsetMode(-1) & 0xFFFF));
    }
    if (has_cpu && cpu >= 20) {
        say(" cacr=%08lx", (unsigned long)cacr());
    }
    {
        int v = vbl_rate10();
        say("\nTT:desktop vbl=%d.%d/s\n", v / 10, v % 10);
    }
    say("TT:stdl is_ste=%d is_megaste=%d has_blitter=%d refresh=%d\n",
        m->is_ste, m->is_megaste, m->has_blitter, STDL_SetRefresh(0));
#ifndef TTCHK_OLD
    say("TT:stdl is_tt=%d is_falcon=%d cpu=%d has_dma_sound=%d\n",
        m->is_tt, m->is_falcon, (int)m->cpu, m->has_dma_sound);
#endif
    fprintf(stderr, "TT:start\n");

    screen = STDL_SetVideoMode(320, 200, 4, 0);
    if (screen == NULL) {
        say("TT:mode refused: %s\n", STDL_GetError());
        STDL_Quit();
        printf("%sTT:done\n", report);
        return 0;
    }
    card4(screen);
    {
        int v = vbl_rate10();
        say("TT:stlow vbl=%d.%d/s stdl refresh=%d", v / 10, v % 10,
            STDL_SetRefresh(0));
    }
    timerb_count(tb, sizeof(tb));
    say(" timerb%s\n", tb);
    {
        STDL_Music *mus = silent_music();
        if (mus != NULL && STDL_PlayMusic(mus, -1) == 0) {
            int f0, f1;
            f = HZ200;
            while (HZ200 == f) {
            }
            f0 = STDL_MusicFrame();
            f = HZ200;
            while (HZ200 - f < 400) {
            }
            f1 = STDL_MusicFrame();
            STDL_HaltMusic();
            say("TT:music %d.%d frames/s (want 50.0)\n", (f1 - f0) / 2,
                (f1 - f0) % 2 ? 5 : 0);
        } else {
            say("TT:music refused: %s\n", STDL_GetError());
        }
        STDL_FreeMusic(mus);
    }
    fprintf(stderr, "TT:card\n");
    f = HZ200;
    while (HZ200 - f < 600) {
    }

    if (ismach == 2) {
        /* bank 0's $FF8240 against TT register 3 */
        uint16_t was = STPAL[3], tt;
        STPAL[3] = 0x0F0F;      /* STE word: red 15, blue 15 */
        tt = TTPAL[3];
        STPAL[3] = was;
        say("TT:$FF8240[3]=0f0f reads $FF8400[3]=%04x (want 0f0f)\n",
            (unsigned)tt);
    }
    if (ismach == 2 || ismach == 3) {
        raw = (uint8_t *)Mxalloc(153600L + 256, 0);
    }
    if (raw != NULL && (long)raw > 0) {
        uint8_t *phys = (uint8_t *)Physbase();
        int16_t stvmode = (ismach == 3) ? (int16_t)VsetMode(-1) : 0;
        int i, lines = 480, stride = 320;

        page8 = (uint8_t *)(((uint32_t)raw + 255) & ~255UL);
        /* drawn after the mode is set: a resolution change clears
         * the screen, as the console is reset with it */
        if (ismach == 2) {
            (void)Setscreen(page8, page8, 7);   /* TT Low */
            card8(page8, 480, 320);
            for (i = 0; i < 256; i++) {
                TTPAL[i] = (uint16_t)(((i & 15) << 8) | ((i >> 4) << 4)
                                      | ((i * 7) & 15));
            }
            TTPAL[255] = 0x0FFF;
        } else {
            int16_t mode = BPS8 | COL40 | (VgetMonitor() == MON_VGA
                                           ? (VGA | VERTFLAG) : PAL);
            lines = (VgetMonitor() == MON_VGA) ? 240 : 200;
            (void)VsetScreen(page8, page8, 3, mode);
            card8(page8, lines, stride);
            for (i = 0; i < 256; i++) {
                ((volatile uint32_t *)0xFFFF9800UL)[i] =
                    ((uint32_t)((i & 15) * 17) << 24)
                    | ((uint32_t)((i >> 4) * 17) << 16)
                    | (uint32_t)((i * 7 & 15) * 17);
            }
            ((volatile uint32_t *)0xFFFF9800UL)[255] = 0xFFFF00FFUL;
        }
        {
            int v = vbl_rate10();
            say("TT:8bit lines=%d vbl=%d.%d/s", lines, v / 10, v % 10);
        }
        timerb_count(tb, sizeof(tb));
        say(" timerb%s\n", tb);
        fprintf(stderr, "TT:card8\n");
        f = HZ200;
        while (HZ200 - f < 600) {
        }
        if (ismach == 2) {
            (void)Setscreen(phys, phys, 0);
        } else {
            (void)VsetScreen(phys, phys, 3, stvmode);
        }
        card4(screen);
    }

    STDL_Quit();
    if (raw != NULL && (long)raw > 0) {
        Mfree(raw);
    }
    printf("%sTT:done\n", report);
    fflush(stdout);
    return 0;
}
