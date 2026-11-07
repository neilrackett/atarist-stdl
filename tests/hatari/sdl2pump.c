/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * On-target cost of a frame's events through the SDL2 add-on, as
 * pumpcost.c measures the core's: six joysticks apart (the four pads
 * of an XPADSTUB.PRG built with -DXPADSTUB_PADS=4, in AUTO\), each
 * open as a game controller, FRAMES frames, each waiting for the VBL,
 * pushing EVENTS key events into STDL (a busy frame's keys, which the
 * add-on turns into SDL's) and draining with SDL_PollEvent. STDL's
 * pumps are counted through its pump's cursor hook, which only the
 * pump calls.
 *
 *   S2P:cost frames F events E pumps P drain U us (u us a frame)
 *   S2P:lat tight|frame n N min a avg b max c us
 *
 * The latency is a key typed from the script (key b), from the ACIA's
 * interrupt to its SDL_KEYDOWN out of SDL_PollEvent, as in
 * pumpcost.c.
 *
 *   STCMD_NO_TTY=1 stcmd make sdl2
 *   STCMD_NO_TTY=1 stcmd m68k-atari-mint-gcc -O2 -std=gnu99 \
 *       -m68020-60 -msoft-float -Iinclude -Iaddons/sdl2/include/SDL2 \
 *       -c -o obj/sdl2pump.o tests/hatari/sdl2pump.c
 *   STCMD_NO_TTY=1 stcmd m68k-atari-mint-gcc -m68000 \
 *       -o dist/SDL2PUMP.TOS obj/sdl2pump.o libstdl-sdl2.a libstdl.a
 */

#include "SDL.h"

#include <stdio.h>
#include <string.h>

#include <stdl/stdl.h>
#include "../../examples/hold.h"

#define FRAMES  250
#define EVENTS  16
#define KEYS    20

#define FRCLOCK (*(volatile Uint32 *)0x466UL)
#define ACIAVEC (*(void (*volatile *)(void))0x118UL)

extern void (*stdl_cursor_hook)(int x, int y);

static volatile Uint32 pumps;

static void count_pump(int x, int y)
{
    (void)x;
    (void)y;
    pumps++;
}

volatile Uint32 pc_acia_hz __attribute__((used));
volatile Uint8 pc_acia_tc __attribute__((used));
volatile Uint8 pc_acia_ip __attribute__((used));
volatile Uint8 pc_acia_armed __attribute__((used));
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

static Uint32 acia_time(void)
{
    Uint32 hz = pc_acia_hz;

    if ((pc_acia_ip & 0x20) && pc_acia_tc > 96) {
        hz++;
    }
    return hz * 192 + (Uint32)(192 - pc_acia_tc);
}

static void wait_vbl(void)
{
    const Uint32 f = FRCLOCK;

    while (FRCLOCK == f) {
    }
}

static void cost(void)
{
    Uint32 total = 0, events = 0;
    int f, i;

    pumps = 0;
    for (f = 0; f < FRAMES; f++) {
        SDL_Event e;
        STDL_Event se;
        Uint32 t0;

        wait_vbl();
        t0 = STDL_GetHz38400();
        for (i = 0; i < EVENTS; i++) {
            memset(&se, 0, sizeof(se));
            se.type = (i & 1) ? STDL_KEYUP : STDL_KEYDOWN;
            se.key.keysym.scancode = 0x1E;
            se.key.keysym.sym = 'a';
            STDL_PushEvent(&se);
        }
        while (SDL_PollEvent(&e)) {
            events++;
        }
        total += STDL_GetHz38400() - t0;
    }
    fprintf(stderr, "S2P:cost frames %d events %lu pumps %lu drain %lu us "
            "(%lu us a frame)\r\n", FRAMES, (unsigned long)events,
            (unsigned long)pumps, (unsigned long)(total * 625UL / 24UL),
            (unsigned long)(total * 625UL / 24UL / FRAMES));
}

static void latency(int framed)
{
    Uint32 lat[KEYS], lo = 0xFFFFFFFFUL, hi = 0, sum = 0;
    int n = 0;

    fprintf(stderr, "S2P:keys %s\r\n", framed ? "frame" : "tight");
    while (n < KEYS) {
        SDL_Event e;
        int down = 0, up = 0;

        pc_acia_armed = 1;
        while (!up) {
            if (framed) {
                wait_vbl();
            }
            while (SDL_PollEvent(&e)) {
                if (e.type == SDL_KEYDOWN && !down && !e.key.repeat) {
                    lat[n] = STDL_GetHz38400() - acia_time();
                    down = 1;
                } else if (e.type == SDL_KEYUP && down) {
                    up = 1;
                }
                if (!framed || up) {
                    break;
                }
            }
        }
        n++;
    }
    /* each one, in hundreds of microseconds: a stray byte armed on
     * shows as one far out */
    fprintf(stderr, "%s:lats", framed ? "S2P-frame" : "S2P-tight");
    for (n = 0; n < KEYS; n++) {
        fprintf(stderr, " %lu", (unsigned long)(lat[n] * 625UL / 2400UL));
    }
    fprintf(stderr, "\r\n");
    for (n = 0; n < KEYS; n++) {
        sum += lat[n];
        lo = lat[n] < lo ? lat[n] : lo;
        hi = lat[n] > hi ? lat[n] : hi;
    }
    fprintf(stderr, "S2P:lat %s n %d min %lu avg %lu max %lu us\r\n",
            framed ? "frame" : "tight", KEYS,
            (unsigned long)(lo * 625UL / 24UL),
            (unsigned long)(sum * 625UL / 24UL / KEYS),
            (unsigned long)(hi * 625UL / 24UL));
}

int main(int argc, char **argv)
{
    SDL_Event e;
    int i;

    hold_on_exit("SDL2PUMP");

    (void)argc;
    (void)argv;
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER) < 0) {
        return 1;
    }
    STDL_UseJoystickMerge(0);
    STDL_SetNumJoysticks(6);
    STDL_OpenInput();
    while (SDL_PollEvent(&e)) {
    }
    for (i = 0; i < SDL_NumJoysticks(); i++) {
        SDL_GameControllerOpen(i);
    }
    stdl_cursor_hook = count_pump;
    fprintf(stderr, "S2P:ready joysticks %d\r\n", SDL_NumJoysticks());

    cost();
    cost();

    pc_old118 = ACIAVEC;
    ACIAVEC = pc_acia_wrap;
    latency(0);
    latency(1);
    ACIAVEC = pc_old118;

    stdl_cursor_hook = NULL;
    fprintf(stderr, "S2P:done\r\n");
    SDL_Quit();
    return 0;
}
