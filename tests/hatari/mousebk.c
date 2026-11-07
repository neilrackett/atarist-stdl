/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * Test fixture: does TOS get its mouse back after a program that asked
 * for two joysticks (STDL_SetNumJoysticks) has gone, by every way out?
 * Two joysticks turn the mouse off and make the mouse port a joystick,
 * and the desktop after needs a pointer; the library promises it on
 * every exit, a crash included.
 *
 * No STDL here: this is the desktop's side. It hooks TOS's own mouse
 * and joystick vectors (Kbdvbase's mousevec and joyvec, which TOS's
 * IKBD handler calls for each packet) to count packets, and measures
 * for two seconds at a time, saying "MEASURE what" first so the run
 * can click the mouse and push the mouse port's stick then:
 *
 *   RESULT what mouse=N joy=N
 *
 * With the mouse on, the clicks are mouse packets and the mouse port's
 * stick sends nothing; with it off, the reverse. The check is tested
 * first on itself: the IKBD told $12 $14 by hand, then $08.
 *
 * Then it runs JOYCHK.TOS beside it (tests/hatari/joychk.c) with "2q",
 * "2e", "2a" and "2b": two joysticks, left by STDL_Quit, exit() alone,
 * abort() and an address error, measuring after each.
 *
 * Run it with tests/hatari/joykeys.cfg, which puts the mouse port's
 * stick on W X A D and F. Built by hand:
 *
 *   STCMD_NO_TTY=1 stcmd m68k-atari-mint-gcc -O2 -std=gnu99 \
 *       tests/hatari/mousebk.c -o dist/MOUSEBK.TOS
 */

#include <stdio.h>
#include <string.h>
#include <mint/osbind.h>
#define HOLD_NO_STDL
#include "../../examples/hold.h"

typedef struct {
    long midivec, vkbderr, vmiderr, statvec;
    long mousevec, clockvec, joyvec, midisys, ikbdsys;
} kbdvecs_t;

volatile long mouse_n, joy_n;
long old_mouse, old_joy;

/* TOS calls these with the packet in a0; count and go on to TOS's */
void mouse_hook(void);
void joy_hook(void);
__asm__(
    "\t.text\n"
    "_mouse_hook:\n"
    "\taddq.l #1,_mouse_n\n"
    "\tmove.l _old_mouse,-(%sp)\n"
    "\trts\n"
    "_joy_hook:\n"
    "\taddq.l #1,_joy_n\n"
    "\tmove.l _old_joy,-(%sp)\n"
    "\trts\n");

static long hook(void)
{
    kbdvecs_t *kv = (kbdvecs_t *)Kbdvbase();

    old_mouse = kv->mousevec;
    old_joy = kv->joyvec;
    kv->mousevec = (long)mouse_hook;
    kv->joyvec = (long)joy_hook;
    return 0;
}

static long unhook(void)
{
    kbdvecs_t *kv = (kbdvecs_t *)Kbdvbase();

    kv->mousevec = old_mouse;
    kv->joyvec = old_joy;
    return 0;
}

static long hz200(void)
{
    return *(volatile long *)0x4BAL;
}

static void measure(const char *what)
{
    long end;

    Supexec(hook);
    mouse_n = joy_n = 0;
    fprintf(stderr, "MEASURE %s\r\n", what);
    end = Supexec(hz200) + 400;
    while (Supexec(hz200) - end < 0) {
    }
    Supexec(unhook);
    fprintf(stderr, "RESULT %s mouse=%ld joy=%ld\r\n", what, mouse_n,
            joy_n);
}

static void ikbd(const char *cmds, int n)
{
    Ikbdws(n - 1, (void *)cmds);
}

/* -DMOUSEBK_ROUTES='"2b"' runs one alone */
#ifndef MOUSEBK_ROUTES
#define MOUSEBK_ROUTES "2q", "2e", "2a", "2b"
#endif

int main(void)
{
    static const char *const routes[] = { MOUSEBK_ROUTES };
    unsigned i;

    hold_on_exit("MOUSEBK");

    fprintf(stderr, "MOUSEBK start\r\n");
    ikbd("\x12\x14", 2);
    measure("control-off");
    ikbd("\x08", 1);
    measure("control-on");

    for (i = 0; i < sizeof(routes) / sizeof(routes[0]); i++) {
        char cmd[8];
        long ret;
        char what[16];

        cmd[0] = 2;
        memcpy(cmd + 1, routes[i], 3);
        ret = Pexec(0, "JOYCHK.TOS", cmd, NULL);
        fprintf(stderr, "child %s returned %ld\r\n", routes[i], ret);
        strcpy(what, "after-");
        strcat(what, routes[i]);
        measure(what);
    }
    fprintf(stderr, "MOUSEBK done\r\n");
    return 0;
}
