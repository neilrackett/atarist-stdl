/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * On-target cost of a frame's events: how often the pump runs and how
 * long a frame's drain takes, and how long a key takes from the IKBD
 * to the program. Built against one library and then another, the
 * same run compares them.
 *
 * The cost phase runs FRAMES frames. Each waits for the VBL, pushes
 * EVENTS events of its own (STDL_PushEvent, user events: a busy
 * frame's keys and motion), and drains the queue with STDL_PollEvent,
 * as a game does, timed with the 38,400Hz clock; whatever the IKBD
 * and the pads bring comes too. The pumps are counted through the
 * pump's cursor hook, which only the pump calls.
 *
 *   PC:cost frames F events E pumps P drain U us (u us a frame)
 *
 * The latency phase times keys typed from the script (key b: a is a
 * joystick under tests/hatari/joykeys.cfg), from the
 * ACIA's interrupt - the vector at $118 wrapped to take the time of
 * the first byte after it is armed - to the STDL_KEYDOWN coming out of
 * STDL_PollEvent: first in a loop that only polls ("tight"), then in a
 * loop that polls once a frame, right after the VBL ("frame").
 *
 *   PC:lat tight n N min a avg b max c us
 *
 * It asks for six joysticks apart (STDL_UseJoystickMerge(0),
 * STDL_SetNumJoysticks(6)): both ports alone, or with the four pads of
 * an XPADSTUB.PRG built with -DXPADSTUB_PADS=4 in AUTO\ all six.
 *
 * Built by hand, against the library to measure:
 *
 *   STCMD_NO_TTY=1 stcmd m68k-atari-mint-gcc -O2 -std=gnu99 -Iinclude \
 *       tests/hatari/pumpcost.c libstdl.a -o dist/PUMPCOST.TOS
 *
 * and run with the Hatari script typing keys after "PC:keys":
 * waitfor PC:keys tight, then twenty times key b and sleep 0.3, then
 * waitfor PC:keys frame for the frame loop's twenty.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <stdl/stdl.h>
#include "../../examples/hold.h"

#define FRAMES  250
#define EVENTS  16
#define KEYS    20

#define FRCLOCK (*(volatile uint32_t *)0x466UL)
#define ACIAVEC (*(void (*volatile *)(void))0x118UL)

/* the pump's own hook (event.c), called once a pump and nowhere else */
extern void (*stdl_cursor_hook)(int x, int y);

static volatile uint32_t pumps;

static void count_pump(int x, int y)
{
    (void)x;
    (void)y;
    pumps++;
}

/* The ACIA's interrupt, wrapped: the 200Hz count and Timer C's at the
 * first byte after arming, and whether Timer C's own interrupt was
 * waiting (its count reloaded, the 200Hz count not yet moved on). */
volatile uint32_t pc_acia_hz __attribute__((used));
volatile uint8_t pc_acia_tc __attribute__((used));
volatile uint8_t pc_acia_ip __attribute__((used));
volatile uint8_t pc_acia_armed __attribute__((used));
void (*pc_old118)(void) __attribute__((used));
void pc_acia_wrap(void);
__asm__(
    "   .text\n"
    "   .even\n"
    "   .globl _pc_acia_wrap\n"
    "_pc_acia_wrap:\n"
    "   tst.b   _pc_acia_armed\n"
    "   beq.s   1f\n"
    "   move.b  0xfffffa23.w,_pc_acia_tc\n"
    "   move.l  0x4ba.w,_pc_acia_hz\n"
    "   move.b  0xfffffa0d.w,_pc_acia_ip\n"
    "   clr.b   _pc_acia_armed\n"
    "1: move.l  _pc_old118,-(%sp)\n"
    "   rts\n");

static uint32_t acia_time(void)
{
    uint32_t hz = pc_acia_hz;

    /* Timer C at zero, reloaded, its tick not yet counted */
    if ((pc_acia_ip & 0x20) && pc_acia_tc > 96) {
        hz++;
    }
    return (hz << 7) + (hz << 6) + (uint32_t)(192 - pc_acia_tc);
}

static void wait_vbl(void)
{
    const uint32_t f = FRCLOCK;

    while (FRCLOCK == f) {
    }
}

static void cost(void)
{
    uint32_t total = 0, events = 0;
    int f, i;

    pumps = 0;
    for (f = 0; f < FRAMES; f++) {
        STDL_Event e;
        uint32_t t0;

        wait_vbl();
        t0 = STDL_GetHz38400();
        for (i = 0; i < EVENTS; i++) {
            memset(&e, 0, sizeof(e));
            e.type = STDL_USEREVENT;
            STDL_PushEvent(&e);
        }
        while (STDL_PollEvent(&e)) {
            events++;
        }
        total += STDL_GetHz38400() - t0;
    }
    /* 38,400 a second: 26.04us a count */
    fprintf(stderr, "PC:cost frames %d events %lu pumps %lu drain %lu us "
            "(%lu us a frame)\r\n", FRAMES, (unsigned long)events,
            (unsigned long)pumps, (unsigned long)(total * 625UL / 24UL),
            (unsigned long)(total * 625UL / 24UL / FRAMES));
}

static void latency(int framed)
{
    uint32_t lat[KEYS], lo = 0xFFFFFFFFUL, hi = 0, sum = 0;
    int n = 0;

    fprintf(stderr, "PC:keys %s\r\n", framed ? "frame" : "tight");
    while (n < KEYS) {
        STDL_Event e;
        int down = 0, up = 0;

        pc_acia_armed = 1;
        while (!up) {
            if (framed) {
                wait_vbl();
            }
            while (STDL_PollEvent(&e)) {
                if (e.type == STDL_KEYDOWN && !down) {
                    lat[n] = STDL_GetHz38400() - acia_time();
                    down = 1;
                } else if (e.type == STDL_KEYUP && down) {
                    up = 1;
                }
                if (framed && up) {
                    break;
                }
                if (!framed) {
                    break;
                }
            }
        }
        n++;
    }
    /* each one, in hundreds of microseconds: a stray byte armed on
     * shows as one far out */
    fprintf(stderr, "%s:lats", framed ? "PC-frame" : "PC-tight");
    for (n = 0; n < KEYS; n++) {
        fprintf(stderr, " %lu", (unsigned long)(lat[n] * 625UL / 2400UL));
    }
    fprintf(stderr, "\r\n");
    for (n = 0; n < KEYS; n++) {
        sum += lat[n];
        lo = lat[n] < lo ? lat[n] : lo;
        hi = lat[n] > hi ? lat[n] : hi;
    }
    fprintf(stderr, "PC:lat %s n %d min %lu avg %lu max %lu us\r\n",
            framed ? "frame" : "tight", KEYS,
            (unsigned long)(lo * 625UL / 24UL),
            (unsigned long)(sum * 625UL / 24UL / KEYS),
            (unsigned long)(hi * 625UL / 24UL));
}

int main(void)
{
    hold_on_exit("PUMPCOST");
    if (STDL_Init(STDL_INIT_VIDEO) < 0) {
        return 1;
    }
    STDL_UseJoystickMerge(0);
    STDL_SetNumJoysticks(6);
    STDL_OpenInput();
    stdl_cursor_hook = count_pump;
    fprintf(stderr, "PC:ready joysticks %d\r\n", STDL_NumJoysticks());

    cost();
    cost();

    pc_old118 = ACIAVEC;
    ACIAVEC = pc_acia_wrap;
    latency(0);
    latency(1);
    ACIAVEC = pc_old118;

    stdl_cursor_hook = NULL;
    fprintf(stderr, "PC:done\r\n");
    STDL_Quit();
    return 0;
}
