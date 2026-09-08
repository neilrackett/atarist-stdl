/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * Video-counter probe for real hardware: does $ff8209 move while a
 * line is being fetched? overscan.c's open-time check said "no" on
 * a 16MHz Mega STE that Hatari cannot model, which forces the
 * bottom border onto its timed path; this samples the counter's
 * low byte back-to-back from a Display Enable end, through the
 * blank and the next line's fetch, and prints the raw bytes on a
 * cleared screen, held until a key. Read: a live counter shows a
 * run of one value (parked), then values stepping by 2 or 4 for
 * ~160 bytes, then parked again. A latched one shows only the
 * parked values. Not built by the Makefile:
 *
 *   STCMD_NO_TTY=1 stcmd m68k-atari-mint-gcc -O2 -std=gnu99 \
 *       -Iinclude -o VCPROBE.TOS tests/hatari/vcprobe.c libstdl.a
 */

#include <stdio.h>
#include <stdl/stdl.h>

#define MFP_TBCR (*(volatile uint8_t *)0xFFFFFA1BUL)
#define MFP_TBDR (*(volatile uint8_t *)0xFFFFFA21UL)

#define N 96

static uint8_t lo[N], mid[N];

/* the sampling loops, register-only so the read spacing is the bus
 * and nothing else: about 8 cycles a sample at 8MHz, 2 bytes of
 * counter each */
static void sample_lo(void)
{
    __asm__ volatile(
        "    lea    0xffff8209.w,%%a0\n"
        "    lea    %0,%%a1\n"
        "    move.w %1,%%d0\n"
        "1:  move.b (%%a0),(%%a1)+\n"
        "    dbra   %%d0,1b\n"
        : : "m"(lo[0]), "i"(N - 1) : "d0", "a0", "a1", "memory");
}

static void sample_mid(void)
{
    __asm__ volatile(
        "    lea    0xffff8207.w,%%a0\n"
        "    lea    %0,%%a1\n"
        "    move.w %1,%%d0\n"
        "1:  move.b (%%a0),(%%a1)+\n"
        "    dbra   %%d0,1b\n"
        : : "m"(mid[0]), "i"(N - 1) : "d0", "a0", "a1", "memory");
}

/* the library's lead-measurement loop, one bit a poll: 1 = the
 * counter changed since the previous poll */
static uint8_t bits[N];

static void sample_bits(void)
{
    __asm__ volatile(
        "    lea    0xffff8209.w,%%a0\n"
        "    lea    %0,%%a1\n"
        "    move.w %1,%%d3\n"
        "    move.b (%%a0),%%d0\n"
        "1:  move.b (%%a0),%%d2\n"
        "    cmp.b  %%d2,%%d0\n"
        "    sne    %%d1\n"
        "    and.b  #1,%%d1\n"
        "    move.b %%d1,(%%a1)+\n"
        "    move.b %%d2,%%d0\n"
        "    dbra   %%d3,1b\n"
        : : "m"(bits[0]), "i"(N - 1) : "d0", "d1", "d2", "d3", "a0", "a1", "memory");
}

static void wait_de(void)
{
    uint8_t v = MFP_TBDR;
    long i;
    for (i = 0; i < 200000L && MFP_TBDR == v; i++) {
    }
}

int main(void)
{
    STDL_Surface *screen;
    STDL_Rect all = { 0, 0, 320, 200 };
    uint16_t sr;
    int i, pass;

    if (STDL_Init(STDL_INIT_VIDEO) < 0) {
        return 1;
    }
    screen = STDL_SetVideoMode(320, 200, 4, 0);
    if (screen == NULL) {
        STDL_Quit();
        return 1;
    }
    STDL_FillRect(screen, &all, 0);
    fprintf(stderr, "\033H\033JVCPROBE machine=%08lx blitter=%d\r\n",
            (unsigned long)STDL_GetMachineInfo()->mch_cookie,
            STDL_GetMachineInfo()->has_blitter);

    for (pass = 0; pass < 2; pass++) {
        __asm__ volatile("move.w %%sr,%0\n\tori.w #0x0700,%%sr"
                         : "=d"(sr) :: "memory");
        MFP_TBCR = 0;
        MFP_TBDR = 255;
        MFP_TBCR = 8;                       /* count DE ends */
        wait_de();                          /* somewhere in the picture */
        wait_de();                          /* a line end, mid-picture */
        if (pass == 0) {
            sample_lo();
        } else {
            sample_mid();
        }
        MFP_TBCR = 0;
        *(volatile uint8_t *)0xFFFFFA0BUL = (uint8_t)~0x01;
        __asm__ volatile("move.w %0,%%sr" :: "d"(sr) : "memory");
    }

    {
        __asm__ volatile("move.w %%sr,%0\n\tori.w #0x0700,%%sr" : "=d"(sr) :: "memory");
        MFP_TBCR = 0; MFP_TBDR = 255; MFP_TBCR = 8;
        wait_de(); wait_de();
        sample_bits();
        MFP_TBCR = 0;
        *(volatile uint8_t *)0xFFFFFA0BUL = (uint8_t)~0x01;
        __asm__ volatile("move.w %0,%%sr" :: "d"(sr) : "memory");
    }
    fprintf(stderr, "changed bits, %d polls of the lead loop from a line end:\r\n", N);
    for (i = 0; i < N; i++) {
        fprintf(stderr, "%d%s", bits[i], ((i & 31) == 31) ? "\r\n" : "");
    }
    fprintf(stderr, "lo byte, %d reads from a line end:\r\n", N);
    for (i = 0; i < N; i++) {
        fprintf(stderr, "%02x%s", lo[i], ((i & 15) == 15) ? "\r\n" : " ");
    }
    fprintf(stderr, "mid byte, %d reads from a line end:\r\n", N);
    for (i = 0; i < N; i++) {
        fprintf(stderr, "%02x%s", mid[i], ((i & 15) == 15) ? "\r\n" : " ");
    }
    fprintf(stderr, "VCPROBE END - press a key\r\n");
    for (;;) {
        STDL_Event ev;
        int key = 0;
        while (STDL_PollEvent(&ev)) {
            if (ev.type == STDL_KEYDOWN) {
                key = 1;
            }
        }
        if (key) {
            break;
        }
        STDL_WaitVBL();
    }
    STDL_Quit();
    return 0;
}
