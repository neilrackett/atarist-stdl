/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * Joysticks: the IKBD's packets, one joystick or up to six
 * (STDL_SetNumJoysticks), against a fake ACIA - a byte stream the test
 * feeds to the real packet parser (event.c's stdl_ikbd_process) and a
 * log of the commands the library sends - and a fake Xpad provider,
 * read through Xpad's own consumer (lib/xpad/src/xpad.c).
 *
 * What it holds the code to:
 * - one joystick is what it always was: port 1 and pad 0 as joystick
 *   0, the mouse on, the mouse port's packets dropped, the commands
 *   sent at install and removal unchanged;
 * - two or more turn the mouse off and the mouse port into joystick 1
 *   ($12 and $14, at install or at once), and every way back gives the
 *   mouse back ($08), with what the program asked of STDL_EnableMouse;
 * - each joystick's events carry its own `which`, and its state, inputs
 *   and pad are its own; a port and its pad merge;
 * - pads 1-3 report exactly as pad 0 does (the same pad in two slots
 *   gives the same events), merged or apart (STDL_UseJoystickMerge);
 * - no pad event is lost to a full queue, and a program that pumps
 *   without taking still sees its pads move;
 * - each pad's rumble is its own and ends on time, at a drop and at
 *   exit;
 * - the SDL 1.2 shim counts, opens and reads every joystick there is.
 *
 * Each case runs in a child process of its own, as the library's state
 * (the provider looked up once, the keyboard taken) is a process's.
 */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>

#include "stdl_internal.h"
#include "stdl_xpad.h"
#include "xpad.h"
#include <SDL.h>

/* event.c's, the ACIA interrupt's handler */
void stdl_ikbd_handler(void);

/* ------------------------------------------------------------------ */
/* the machine                                                         */

static uint8_t rx[256];
static unsigned rx_head, rx_tail;
static uint8_t tx[64];
static int ntx;

uint8_t stdl_host_acia_ctrl(void)
{
    return (uint8_t)(0x02 | (rx_head != rx_tail ? 0x01 : 0));
}

uint8_t stdl_host_acia_rx(void)
{
    return rx[rx_tail++ & 255];
}

void stdl_host_acia_tx(uint8_t b)
{
    if (ntx < (int)sizeof(tx)) {
        tx[ntx++] = b;
    }
}

typedef struct {
    long midivec, vkbderr, vmiderr, statvec;
    long mousevec, clockvec, joyvec, midisys, ikbdsys;
} kbdvecs_t;

static kbdvecs_t kbdvecs;

void *Kbdvbase(void)
{
    return &kbdvecs;
}

static uint8_t keytab_plain[128];

void *Keytbl(void *unshift, void *shift, void *caps)
{
    static struct { uint8_t *unshift, *shift, *caps; } kt;

    (void)unshift; (void)shift; (void)caps;
    kt.unshift = kt.shift = kt.caps = keytab_plain;
    return &kt;
}

void *stdl_host_xpad;

uint32_t STDL_GetTicks(void)
{
    return stdl_host_clock * 5;
}

void STDL_Delay(uint32_t ms)
{
    stdl_host_clock += ms / 5;
}

/* the VBL count, moved apart from the 200Hz clock (STDL_HOST_FRCLOCK),
 * and time.c's service between pumps, counted */
volatile uint32_t stdl_host_frclock;
static int idles;

void stdl_idle_services(void)
{
    idles++;
}

/* video.c's and the screen's, which compat.c names */
void STDL_Flip(void)
{
}

static STDL_MachineInfo mach;

const STDL_MachineInfo *STDL_GetMachineInfo(void)
{
    return &mach;
}

/* ------------------------------------------------------------------ */
/* checking                                                            */

static int failures;

#define CHECK(c) do { \
    if (!(c)) { \
        fprintf(stderr, "  FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); \
        failures++; \
    } \
} while (0)

/* the IKBD sends these bytes, and the ACIA interrupt runs */
static void ikbd(int n, ...)
{
    va_list ap;
    int i;

    va_start(ap, n);
    for (i = 0; i < n; i++) {
        rx[rx_head++ & 255] = (uint8_t)va_arg(ap, int);
    }
    va_end(ap);
    stdl_ikbd_handler();
}

static int tx_is(int n, ...)
{
    va_list ap;
    int i, ok = (n == ntx);

    va_start(ap, n);
    for (i = 0; i < n; i++) {
        const int b = va_arg(ap, int);

        if (ok && tx[i] != b) {
            ok = 0;
        }
    }
    va_end(ap);
    if (!ok) {
        fprintf(stderr, "  sent:");
        for (i = 0; i < ntx; i++) {
            fprintf(stderr, " $%02X", tx[i]);
        }
        fprintf(stderr, "\n");
    }
    ntx = 0;
    return ok;
}

/* what a pump brought */
static STDL_Event ev[256];
static int nev;

/* A frame: the pump, then a program's drain, which pumps no more in
 * the same VBL; what it brings is added to ev[]. */
static void frame(void)
{
    STDL_Event e;

    STDL_PumpEvents();
    while (STDL_PollEvent(&e)) {
        if (nev < 256) {
            ev[nev++] = e;
        }
    }
}

static void pump(void)
{
    nev = 0;
    frame();
}

/* n frames' worth: a pad held back for room comes at a later pump */
static void frames(int n)
{
    nev = 0;
    while (n-- > 0) {
        frame();
    }
}

static int n_joy_events(int which)
{
    int i, n = 0;

    for (i = 0; i < nev; i++) {
        if ((ev[i].type == STDL_JOYAXISMOTION
             || ev[i].type == STDL_JOYBUTTONDOWN
             || ev[i].type == STDL_JOYBUTTONUP
             || ev[i].type == STDL_JOYHATMOTION)
            && ev[i].jaxis.which == which) {
            n++;
        }
    }
    return n;
}

static int has_axis(int which, int axis, int value)
{
    int i;

    for (i = 0; i < nev; i++) {
        if (ev[i].type == STDL_JOYAXISMOTION && ev[i].jaxis.which == which
            && ev[i].jaxis.axis == axis && ev[i].jaxis.value == value) {
            return 1;
        }
    }
    return 0;
}

static int has_button(int which, int button, int down)
{
    int i;

    for (i = 0; i < nev; i++) {
        if (ev[i].type == (down ? STDL_JOYBUTTONDOWN : STDL_JOYBUTTONUP)
            && ev[i].jbutton.which == which
            && ev[i].jbutton.button == button
            && ev[i].jbutton.state == down) {
            return 1;
        }
    }
    return 0;
}

static int n_type(int type)
{
    int i, n = 0;

    for (i = 0; i < nev; i++) {
        n += ev[i].type == type;
    }
    return n;
}

/* ------------------------------------------------------------------ */
/* a provider                                                          */

static XPAD block;
static XPAD_REQ req;

/* a provider's block, made as Xpad's own provider half makes one */
static void provider(int pads, uint16_t caps)
{
    xpad_init(&block, (uint8_t)pads, caps, "test", &req);
    stdl_host_xpad = &block;
}

/* slot i's pad, in the buffer the consumer reads */
static XPAD_PAD *slot(int i)
{
    return (XPAD_PAD *)XPAD_PAD_AT(&block, block.active, i);
}

/* ------------------------------------------------------------------ */
/* cases                                                               */

/* One joystick: exactly what a program that never asks always had. */
static void one_joystick(void)
{
    CHECK(STDL_NumJoysticks() == 1);
    /* the IKBD is told nothing before the library has the keyboard,
     * STDL_Init or not: a $12 then would outlive the program */
    CHECK(STDL_Init(STDL_INIT_VIDEO) == 0);
    STDL_EnableMouse(0);
    STDL_EnableMouse(1);
    CHECK(tx_is(0));
    STDL_OpenInput();
    /* quiesce, resume, the mouse ($08, TOS's own mode), interrogate */
    CHECK(tx_is(4, 0x13, 0x11, 0x08, 0x16));
    /* the readers past the one joystick there is, joysticks.c not
     * linked in: nothing */
    CHECK(stdl_joyn == NULL);
    CHECK(STDL_GetJoystickAxis(1, STDL_JOYSTICK_AXIS_X) == 0);
    CHECK(STDL_GetJoystickHat(1) == 0);

    ikbd(3, 0xFD, 0x00, 0x00);
    pump();
    CHECK(nev == 0);

    ikbd(2, 0xFF, 0x01);                    /* port 1: up */
    pump();
    CHECK(nev == 1 && has_axis(0, 1, -32768));
    CHECK(STDL_GetJoystickState(0) == 0x01);

    /* the mouse port's packets are dropped */
    ikbd(2, 0xFE, 0x84);
    pump();
    CHECK(nev == 0);
    CHECK(STDL_GetJoystickState(1) == 0);
    CHECK(STDL_JoystickInputHeld(1, STDL_JOYSTICK_FIRE) == 0);

    /* fire, stolen into the right mouse button and folded back */
    ikbd(3, 0xF9, 0x00, 0x00);
    pump();
    CHECK(n_type(STDL_MOUSEBUTTONDOWN) == 1);
    CHECK(has_button(0, 0, 1));
    CHECK(STDL_GetJoystickState(0) == 0x81);
    CHECK(STDL_JoystickInputHeld(0, STDL_JOYSTICK_FIRE) == 1);

    CHECK(STDL_EnableMouse(0) == 1);
    CHECK(tx_is(2, 0x08, 0x12));
    CHECK(STDL_EnableMouse(1) == 0);
    CHECK(tx_is(1, 0x08));

    /* the SDL 1.2 shim: one stick, as it always said */
    CHECK(SDL_NumJoysticks() == 1);
    CHECK(SDL_JoystickOpen(1) == NULL);
    CHECK(SDL_JoystickName(1) == NULL);
    {
        SDL_Joystick *j = SDL_JoystickOpen(0);

        CHECK(j != NULL && SDL_JoystickIndex(j) == 0);
        CHECK(SDL_JoystickOpened(0) == 1);
        CHECK(SDL_JoystickNumAxes(j) == 2 && SDL_JoystickNumButtons(j) == 1
              && SDL_JoystickNumHats(j) == 0);
        CHECK(SDL_JoystickGetAxis(j, 1) == -32768);
        CHECK(SDL_JoystickGetButton(j, 0) == SDL_PRESSED);
        SDL_JoystickClose(j);
        CHECK(SDL_JoystickOpened(0) == 0);
        /* any handle not one of ours is stick 0, as before */
        CHECK(SDL_JoystickIndex(NULL) == 0);
    }

    stdl_events_remove();
    CHECK(tx_is(3, 0x13, 0x11, 0x08));
}

/* Two, asked for before the library takes the keyboard. */
static void two_ports(void)
{
    CHECK(STDL_SetNumJoysticks(2) == 2);
    CHECK(STDL_NumJoysticks() == 2);
    CHECK(tx_is(0));                        /* not ours yet */
    STDL_OpenInput();
    CHECK(tx_is(5, 0x13, 0x11, 0x12, 0x14, 0x16));

    /* the interrogation's reply seeds both: port 0 first */
    ikbd(3, 0xFD, 0x84, 0x02);
    pump();
    CHECK(nev == 3);
    CHECK(ev[0].type == STDL_JOYAXISMOTION && ev[0].jaxis.which == 1
          && ev[0].jaxis.axis == 0 && ev[0].jaxis.value == -32768);
    CHECK(ev[1].type == STDL_JOYBUTTONDOWN && ev[1].jbutton.which == 1);
    CHECK(ev[2].type == STDL_JOYAXISMOTION && ev[2].jaxis.which == 0
          && ev[2].jaxis.axis == 1 && ev[2].jaxis.value == 32767);
    CHECK(STDL_GetJoystickState(1) == 0x84);
    CHECK(STDL_GetJoystickState(0) == 0x02);
    CHECK(STDL_JoystickInputHeld(0, STDL_JOYSTICK_FIRE) == 0);
    CHECK(STDL_JoystickInputHeld(1, STDL_JOYSTICK_LEFT) == 1);
    CHECK(STDL_JoystickInputHeld(1, STDL_JOYSTICK_FIRE) == 1);
    CHECK(STDL_JoystickInputHeld(1, STDL_JOYSTICK_UP) == 0);
    CHECK(STDL_JoystickInputHeld(1, STDL_JOYSTICK_EAST) == 0);
    CHECK(STDL_JoystickInputHeld(0, STDL_JOYSTICK_DOWN) == 1);
    CHECK(STDL_JoystickInputHeld(2, STDL_JOYSTICK_DOWN) == 0);
    CHECK(STDL_HasPad(1) == 0);

    ikbd(2, 0xFE, 0x00);
    pump();
    CHECK(nev == 2 && has_axis(1, 0, 0) && has_button(1, 0, 0));

    /* port 1's fire is in its own packet now */
    ikbd(2, 0xFF, 0x80);
    pump();
    CHECK(has_button(0, 0, 1) && has_axis(0, 1, 0));
    ikbd(2, 0xFF, 0x00);
    pump();
    CHECK(has_button(0, 0, 0));
    /* and a mouse packet in flight at a switch cannot move it: a
     * right button pressed and let go */
    ikbd(3, 0xF9, 0x00, 0x00);
    pump();
    CHECK(n_type(STDL_MOUSEBUTTONDOWN) == 1);
    CHECK(STDL_GetJoystickState(0) == 0x00 && n_joy_events(0) == 0);
    ikbd(2, 0xFF, 0x80);
    ikbd(3, 0xF8, 0x00, 0x00);
    pump();
    CHECK(STDL_GetJoystickState(0) == 0x80 && has_button(0, 0, 1));
    CHECK(has_button(0, 0, 0) == 0);

    /* key emulation is joystick 0's alone */
    STDL_JoystickKeyEmulation(1);
    pump();
    nev = 0;
    ikbd(2, 0xFE, 0x01);
    pump();
    CHECK(n_type(STDL_KEYDOWN) == 0 && has_axis(1, 1, -32768));
    ikbd(2, 0xFF, 0x81);
    pump();
    CHECK(n_type(STDL_KEYDOWN) == 1);
    STDL_JoystickKeyEmulation(0);

    /* the mouse stays off whatever is asked, which is kept: the port
     * stays a joystick ($12 $14 again) */
    CHECK(STDL_EnableMouse(1) == 1);
    CHECK(STDL_EnableMouse(0) == 1);
    CHECK(tx_is(4, 0x12, 0x14, 0x12, 0x14));

    /* the SDL 1.2 shim: both sticks */
    CHECK(SDL_NumJoysticks() == 2);
    CHECK(strcmp(SDL_JoystickName(1), "Atari ST joystick port 0") == 0);
    CHECK(SDL_JoystickOpen(2) == NULL);
    {
        SDL_Joystick *j0 = SDL_JoystickOpen(0);
        SDL_Joystick *j1 = SDL_JoystickOpen(1);

        CHECK(j1 != NULL && j1 != j0);
        CHECK(SDL_JoystickIndex(j1) == 1 && SDL_JoystickIndex(j0) == 0);
        CHECK(SDL_JoystickOpened(1) == 1);
        CHECK(SDL_JoystickNumAxes(j1) == 2 && SDL_JoystickNumButtons(j1) == 1
              && SDL_JoystickNumHats(j1) == 0);
        CHECK(SDL_JoystickGetAxis(j1, 1) == -32768);
        CHECK(SDL_JoystickGetAxis(j1, 0) == 0);
        CHECK(SDL_JoystickGetButton(j1, 0) == SDL_RELEASED);
        CHECK(SDL_JoystickGetButton(j0, 0) == SDL_PRESSED);
        SDL_JoystickClose(j1);
        CHECK(SDL_JoystickOpened(1) == 0 && SDL_JoystickOpened(0) == 1);
    }

    /* the mouse comes back at the end, as the program asked or not */
    stdl_events_remove();
    CHECK(tx_is(3, 0x13, 0x11, 0x08));
}

/* Switching while the library has the keyboard, and back. */
static void switching(void)
{
    STDL_OpenInput();
    ntx = 0;
    ikbd(3, 0xF9, 0x00, 0x00);              /* fire, as the right button */
    pump();
    CHECK(STDL_GetJoystickState(0) == 0x80);

    CHECK(STDL_SetNumJoysticks(2) == 2);
    CHECK(tx_is(3, 0x12, 0x14, 0x16));
    /* the reply: fire is not held on either port */
    ikbd(3, 0xFD, 0x01, 0x00);
    pump();
    CHECK(has_axis(1, 1, -32768) && has_button(0, 0, 0));
    CHECK(STDL_GetJoystickState(0) == 0x00);

    /* the same number again tells the IKBD nothing */
    CHECK(STDL_SetNumJoysticks(2) == 2);
    CHECK(tx_is(0));
    /* three to six with no pads are the two ports */
    CHECK(STDL_SetNumJoysticks(4) == 2 && STDL_SetNumJoysticks(3) == 2);
    CHECK(STDL_SetNumJoysticks(6) == 2);
    CHECK(tx_is(0));
    CHECK(STDL_SetNumJoysticks(0) == -1 && STDL_SetNumJoysticks(7) == -1);
    CHECK(STDL_NumJoysticks() == 2);

    /* back to one: joystick 1 lets go, and the mouse is back */
    nev = 0;
    CHECK(STDL_SetNumJoysticks(1) == 1);
    CHECK(tx_is(2, 0x08, 0x16));
    pump();
    CHECK(nev == 1 && has_axis(1, 1, 0));
    CHECK(STDL_GetJoystickState(1) == 0);
    ikbd(2, 0xFE, 0x0F);
    pump();
    CHECK(nev == 0);

    /* with the mouse switched off, it stays off on the way back */
    STDL_EnableMouse(0);
    CHECK(tx_is(2, 0x08, 0x12));
    STDL_SetNumJoysticks(2);
    CHECK(tx_is(3, 0x12, 0x14, 0x16));
    STDL_SetNumJoysticks(1);
    CHECK(tx_is(3, 0x08, 0x12, 0x16));
    STDL_EnableMouse(1);
    CHECK(tx_is(1, 0x08));

    STDL_SetNumJoysticks(2);
    stdl_events_remove();
    CHECK(tx_is(6, 0x12, 0x14, 0x16, 0x13, 0x11, 0x08));
}

/* Four pads, and the ports beside the first two. */
static void four_pads(void)
{
    int i;

    provider(4, XPAD_CAP_ANALOG | XPAD_CAP_RUMBLE);
    for (i = 0; i < 4; i++) {
        slot(i)->type = XPAD_TYPE_GAMEPAD;
    }
    CHECK(STDL_SetNumJoysticks(4) == 4);
    for (i = 0; i < 4; i++) {
        CHECK(STDL_HasPad(i) == 1);
    }
    CHECK(STDL_HasPad(4) == 0 && STDL_HasPad(-1) == 0);
    STDL_OpenInput();
    pump();
    CHECK(nev == 0);                         /* all at rest */

    /* pad 1 and the mouse port merge: the further pushed wins */
    slot(1)->lx = 100;
    slot(1)->buttons = XPAD_TL;
    pump();
    CHECK(nev == 2 && has_axis(1, 0, 25800) && has_button(1, 4, 1));
    CHECK(STDL_JoystickInputHeld(1, STDL_JOYSTICK_TL) == 1);
    ikbd(2, 0xFE, 0x04);                    /* the port: left */
    pump();
    CHECK(nev == 1 && has_axis(1, 0, -32768));
    CHECK(STDL_GetJoystickState(1) == 0x04);
    ikbd(2, 0xFE, 0x00);
    pump();
    CHECK(nev == 1 && has_axis(1, 0, 25800));
    /* a pad's fire and the port's are one button: neither releases
     * what the other holds */
    slot(1)->buttons = XPAD_TL | XPAD_SOUTH;
    pump();
    CHECK(has_button(1, 0, 1));
    ikbd(2, 0xFE, 0x80);
    ikbd(2, 0xFE, 0x00);
    pump();
    CHECK(nev == 0 && (STDL_GetJoystickState(1) & 0x80));

    /* pads 2 and 3: the triggers, the hat, the other stick */
    slot(2)->lt = 255;
    slot(2)->buttons = XPAD_UP | XPAD_START;
    slot(3)->ry = -127;
    pump();
    CHECK(has_axis(2, 4, 32767) && has_axis(2, 1, 0) == 0);
    CHECK(has_button(2, 9, 1));
    CHECK(STDL_GetJoystickState(2) == 0x01);
    CHECK(STDL_JoystickInputHeld(2, STDL_JOYSTICK_START) == 1);
    CHECK(STDL_JoystickInputHeld(2, STDL_JOYSTICK_UP) == 1);
    CHECK(has_axis(3, 3, -32766));
    CHECK(n_joy_events(0) == 0);

    /* the SDL 1.2 shim */
    CHECK(SDL_NumJoysticks() == 4);
    CHECK(strcmp(SDL_JoystickName(3), "Xpad pad 3") == 0);
    {
        SDL_Joystick *j2 = SDL_JoystickOpen(2);
        SDL_Joystick *j3 = SDL_JoystickOpen(3);

        CHECK(SDL_JoystickNumAxes(j2) == 6 && SDL_JoystickNumButtons(j2) == 13
              && SDL_JoystickNumHats(j2) == 1);
        CHECK(SDL_JoystickGetAxis(j2, 4) == 32767);
        CHECK(SDL_JoystickGetAxis(j2, 5) == -32768);
        CHECK(SDL_JoystickGetAxis(j3, 3) == -32766);
        CHECK(SDL_JoystickGetAxis(j3, 6) == 0);
        CHECK(SDL_JoystickGetButton(j2, 9) == SDL_PRESSED);
        CHECK(SDL_JoystickGetButton(j2, 8) == SDL_RELEASED);
        CHECK(SDL_JoystickGetHat(j2, 0) == 0x01);
        CHECK(SDL_JoystickGetHat(j3, 0) == 0);
    }

    /* each pad's rumble its own, ended by the pump on time */
    CHECK(STDL_PadRumble(2, 10, 20, 100) == 0);
    CHECK(req.rumble[2][0] == 10 && req.rumble[2][1] == 20);
    CHECK(req.rumble[0][0] == 0 && req.rumble[1][0] == 0
          && req.rumble[3][0] == 0);
    CHECK(req.seq == 1);
    CHECK(STDL_PadRumble(3, 30, 0, 1000) == 0);
    CHECK(req.rumble[3][0] == 30 && req.seq == 2);
    stdl_host_clock += 19;                   /* 95ms */
    pump();
    CHECK(req.rumble[2][0] == 10);
    stdl_host_clock += 2;
    pump();
    CHECK(req.rumble[2][0] == 0 && req.rumble[2][1] == 0 && req.seq == 3);
    CHECK(req.rumble[3][0] == 30);
    /* 0, 0 stops it */
    CHECK(STDL_PadRumble(3, 0, 0, 100) == 0);
    CHECK(req.rumble[3][0] == 0 && req.seq == 4);
    /* not a joystick there is */
    CHECK(STDL_PadRumble(4, 1, 1, 100) == -1);
    /* a provider that stops honouring it: refused, and stopped first */
    CHECK(STDL_PadRumble(1, 50, 50, 1000) == 0 && req.rumble[1][0] == 50);
    block.caps = XPAD_CAP_ANALOG;
    CHECK(STDL_PadRumble(1, 60, 60, 1000) == -1);
    CHECK(req.rumble[1][0] == 0 && req.rumble[1][1] == 0);
    block.caps = XPAD_CAP_ANALOG | XPAD_CAP_RUMBLE;

    /* dropping joysticks: what they held is let go, their rumble ends */
    CHECK(STDL_PadRumble(3, 40, 40, 5000) == 0);
    pump();
    CHECK(STDL_SetNumJoysticks(2) == 2);
    CHECK(req.rumble[3][0] == 0);
    pump();
    CHECK(has_axis(2, 4, -32768) && has_button(2, 9, 0));
    CHECK(has_axis(3, 3, 0));
    CHECK(STDL_GetJoystickState(2) == 0 && STDL_JoystickInputHeld(2, STDL_JOYSTICK_START) == 0);
    slot(2)->buttons = XPAD_EAST;
    pump();
    CHECK(n_joy_events(2) == 0);
    CHECK(STDL_PadRumble(2, 1, 1, 100) == -1);

    /* and every exit ends every pad's */
    CHECK(STDL_PadRumble(1, 70, 0, 5000) == 0 && req.rumble[1][0] == 70);
    stdl_events_remove();
    CHECK(req.rumble[1][0] == 0);
}

/* Every exit ends every pad's rumble, the terminate path's included
 * (stdl_events_remove is what it calls). */
static void exit_rumble(void)
{
    int i;

    provider(4, XPAD_CAP_RUMBLE);
    CHECK(STDL_SetNumJoysticks(4) == 4);
    STDL_OpenInput();
    for (i = 0; i < 4; i++) {
        CHECK(STDL_PadRumble(i, (uint8_t)(10 + i), 1, 5000) == 0);
        CHECK(req.rumble[i][0] == 10 + i);
    }
    pump();
    stdl_events_remove();
    for (i = 0; i < 4; i++) {
        CHECK(req.rumble[i][0] == 0 && req.rumble[i][1] == 0);
    }
}

/*
 * The same pad in slot 0 and in slot 2 makes the same events but for
 * `which`: pads 1-3 are pad 0's code's twin (joysticks.c against
 * event.c and stdl_xpad.c), and this is what keeps them so.
 */
static void same_as_pad0(void)
{
    static const struct {
        uint32_t buttons;
        int8_t lx, ly, rx, ry;
        uint8_t lt, rt;
    } steps[] = {
        { 0, 0, 0, 0, 0, 0, 0 },
        { XPAD_SOUTH, 0, 0, 0, 0, 0, 0 },
        { XPAD_SOUTH | XPAD_RIGHT | XPAD_TR, 90, -3, 0, 0, 0, 0 },
        { XPAD_NORTH | XPAD_LEFT | XPAD_UP, -127, -127, 5, -5, 200, 7 },
        { XPAD_THUMBL | XPAD_THUMBR | XPAD_MODE | XPAD_SELECT, 0, 0,
          127, 127, 255, 255 },
        { XPAD_EAST | XPAD_WEST | XPAD_TL | XPAD_TL2 | XPAD_TR2
          | XPAD_START | XPAD_DOWN, 1, 1, 0, 0, 0, 0 },
        { 0, 0, 0, 0, 0, 0, 0 },
    };
    unsigned s;
    int k;

    provider(4, XPAD_CAP_ANALOG);
    /* held from the start: the first poll reports the classic five
     * (an axis and fire here) and none of the rest */
    for (k = 0; k < 4; k += 2) {
        slot(k)->buttons = XPAD_SOUTH | XPAD_EAST | XPAD_UP;
        slot(k)->lx = 40;
        slot(k)->rx = 50;
        slot(k)->lt = 9;
    }
    CHECK(STDL_SetNumJoysticks(3) == 3);
    STDL_OpenInput();
    pump();
    CHECK(nev == 4);
    CHECK(has_axis(0, 0, 40 * 258) && has_axis(2, 0, 40 * 258));
    CHECK(has_button(0, 0, 1) && has_button(2, 0, 1));
    for (s = 0; s < sizeof(steps) / sizeof(steps[0]); s++) {
        STDL_Event a[32], b[32];
        int na = 0, nb = 0, e;

        /* both slots at once: a step can be 17 events a pad, more than
         * STDL's 32-event queue takes for two, and the one held back
         * comes at the next frame's pump */
        for (k = 0; k < 4; k += 2) {
            XPAD_PAD *p = slot(k);

            p->buttons = steps[s].buttons;
            p->lx = steps[s].lx;
            p->ly = steps[s].ly;
            p->rx = steps[s].rx;
            p->ry = steps[s].ry;
            p->lt = steps[s].lt;
            p->rt = steps[s].rt;
        }
        frames(2);
        for (e = 0; e < nev; e++) {
            CHECK(ev[e].jaxis.which == 0 || ev[e].jaxis.which == 2);
            if (ev[e].jaxis.which == 0 && na < 32) {
                a[na++] = ev[e];
            } else if (ev[e].jaxis.which == 2 && nb < 32) {
                b[nb++] = ev[e];
            }
        }
        CHECK(na == nb);
        CHECK(s == 0 || na > 0);
        for (k = 0; k < na && k < nb; k++) {
            b[k].jaxis.which = 0;
            CHECK(memcmp(&a[k], &b[k], sizeof(a[k])) == 0);
        }
        CHECK(STDL_GetJoystickState(0) == STDL_GetJoystickState(2));
        for (k = 0; k < STDL_JOYSTICK_INPUTS; k++) {
            CHECK(STDL_JoystickInputHeld(0, k) == STDL_JoystickInputHeld(2, k));
        }
    }
    {
        SDL_Joystick *j0 = SDL_JoystickOpen(0);
        SDL_Joystick *j2 = SDL_JoystickOpen(2);

        slot(0)->rx = slot(2)->rx = 33;
        slot(0)->rt = slot(2)->rt = 99;
        slot(0)->buttons = slot(2)->buttons = XPAD_WEST | XPAD_DOWN;
        pump();
        for (k = 0; k < 6; k++) {
            CHECK(SDL_JoystickGetAxis(j0, k) == SDL_JoystickGetAxis(j2, k));
        }
        for (k = 0; k < 13; k++) {
            CHECK(SDL_JoystickGetButton(j0, k) == SDL_JoystickGetButton(j2, k));
        }
        CHECK(SDL_JoystickGetHat(j0, 0) == SDL_JoystickGetHat(j2, 0));
    }
}

/* A provider with fewer slots than asked for; one with none. */
static void few_slots(void)
{
    provider(3, 0);
    CHECK(STDL_SetNumJoysticks(4) == 3);
    CHECK(STDL_HasPad(2) == 1 && STDL_HasPad(3) == 0);
    CHECK(SDL_NumJoysticks() == 3);
    CHECK(SDL_JoystickOpen(3) == NULL);
}

static void two_slots(void)
{
    provider(2, 0);
    CHECK(STDL_SetNumJoysticks(4) == 2);
    CHECK(STDL_SetNumJoysticks(3) == 2);
    CHECK(STDL_HasPad(1) == 1);
    CHECK(SDL_NumJoysticks() == 2);
    {
        SDL_Joystick *j1 = SDL_JoystickOpen(1);

        CHECK(SDL_JoystickNumAxes(j1) == 6);
    }
    /* no rumble offered */
    CHECK(STDL_PadRumble(1, 1, 1, 10) == -1);
}

static void one_slot(void)
{
    provider(1, XPAD_CAP_RUMBLE);
    CHECK(STDL_SetNumJoysticks(2) == 2);
    CHECK(STDL_HasPad(0) == 1 && STDL_HasPad(1) == 0);
    {
        SDL_Joystick *j1 = SDL_JoystickOpen(1);

        CHECK(SDL_JoystickNumAxes(j1) == 2 && SDL_JoystickNumHats(j1) == 0);
    }
    CHECK(STDL_PadRumble(1, 1, 1, 10) == -1);
    CHECK(req.seq == 0);
}

/* Pad 0 with one joystick: pads 1-3 say nothing. */
static void pads_with_one(void)
{
    provider(4, XPAD_CAP_ANALOG);
    slot(0)->type = slot(1)->type = XPAD_TYPE_GAMEPAD;
    slot(0)->buttons = XPAD_RIGHT;
    slot(1)->buttons = XPAD_LEFT | XPAD_SOUTH;
    STDL_OpenInput();
    pump();
    /* a d-pad is the hat, and the state byte; the axes are the sticks */
    CHECK(STDL_GetJoystickState(0) == 0x08 && nev == 0);
    slot(0)->lx = -127;
    slot(1)->lx = 127;
    pump();
    CHECK(nev == 1 && has_axis(0, 0, -32766));
    CHECK(n_joy_events(1) == 0);
    CHECK(STDL_NumJoysticks() == 1 && SDL_NumJoysticks() == 1);
    CHECK(STDL_HasPad(0) == 1 && STDL_HasPad(1) == 0);
}

static const char *only;     /* argv[1]: one case, by name */

/* the pads in four states of their own: a direction, a button, a stick */
static void four_states(void)
{
    int i;

    for (i = 0; i < 4; i++) {
        slot(i)->type = XPAD_TYPE_GAMEPAD;
    }
    slot(0)->buttons = XPAD_RIGHT | XPAD_SOUTH | XPAD_TR;
    slot(0)->lx = 100;
    slot(1)->buttons = XPAD_LEFT | XPAD_EAST;
    slot(1)->lx = -60;
    slot(2)->buttons = XPAD_UP | XPAD_START;
    slot(2)->lt = 128;
    slot(3)->buttons = XPAD_DOWN | XPAD_SOUTH;
    slot(3)->ry = 127;
}

/* Apart: the ports first, then the four pads, six joysticks. */
static void apart_six(void)
{
    SDL_Joystick *j;

    provider(4, XPAD_CAP_ANALOG | XPAD_CAP_RUMBLE);
    four_states();
    CHECK(STDL_UseJoystickMerge(-1) == 1);
    CHECK(STDL_UseJoystickMerge(0) == 1);
    CHECK(STDL_UseJoystickMerge(-1) == 0);
    CHECK(STDL_SetNumJoysticks(6) == 6);
    CHECK(STDL_HasPad(0) == 0 && STDL_HasPad(1) == 0);
    CHECK(STDL_HasPad(2) && STDL_HasPad(3) && STDL_HasPad(4)
          && STDL_HasPad(5) && !STDL_HasPad(6));
    STDL_OpenInput();
    /* the mouse off as for any two or more */
    CHECK(tx_is(5, 0x13, 0x11, 0x12, 0x14, 0x16));
    pump();
    /* pad 0 is joystick 2's, not joystick 0's */
    CHECK(n_joy_events(0) == 0 && STDL_GetJoystickState(0) == 0);
    CHECK(STDL_JoystickInputHeld(0, STDL_JOYSTICK_TR) == 0);
    CHECK(has_axis(2, 0, 25800) && has_button(2, 0, 1));
    CHECK(STDL_GetJoystickState(2) == 0x88);
    CHECK(STDL_JoystickInputHeld(2, STDL_JOYSTICK_TR) == 1);
    CHECK(STDL_GetJoystickState(3) == 0x04 && has_axis(3, 0, -15480));
    /* pad 1 holds its east button, button 1, not the west, 2 */
    CHECK(STDL_JoystickInputHeld(3, STDL_JOYSTICK_EAST) == 1);
    CHECK(STDL_JoystickInputHeld(3, STDL_JOYSTICK_WEST) == 0);
    CHECK(STDL_JoystickInputHeld(4, STDL_JOYSTICK_START) == 1);
    CHECK(STDL_GetJoystickState(5) == 0x82);
    /* the ports alone on 0 and 1: pad 1 is joystick 3's */
    ikbd(2, 0xFF, 0x01);
    ikbd(2, 0xFE, 0x08);
    pump();
    CHECK(nev == 2 && has_axis(0, 1, -32768) && has_axis(1, 0, 32767));
    CHECK(STDL_GetJoystickState(1) == 0x08);
    /* each pad's rumble by its slot; joystick 0 has no pad */
    CHECK(STDL_PadRumble(0, 1, 1, 100) == -1);
    CHECK(STDL_PadRumble(2, 10, 20, 100) == 0);
    CHECK(req.rumble[0][0] == 10 && req.rumble[0][1] == 20);
    CHECK(STDL_PadRumble(5, 30, 40, 100) == 0 && req.rumble[3][0] == 30);
    /* the SDL 1.2 shim: six sticks, pads named by their slots */
    CHECK(SDL_NumJoysticks() == 6);
    CHECK(strcmp(SDL_JoystickName(1), "Atari ST joystick port 0") == 0);
    CHECK(strcmp(SDL_JoystickName(2), "Xpad pad 0") == 0);
    CHECK(strcmp(SDL_JoystickName(5), "Xpad pad 3") == 0);
    j = SDL_JoystickOpen(0);
    CHECK(SDL_JoystickNumAxes(j) == 2 && SDL_JoystickNumButtons(j) == 1);
    CHECK(SDL_JoystickGetButton(j, 5) == SDL_RELEASED);
    j = SDL_JoystickOpen(5);
    CHECK(j != NULL && SDL_JoystickNumAxes(j) == 6);
    CHECK(SDL_JoystickGetAxis(j, 3) == 32766);
    CHECK(SDL_JoystickGetButton(j, 0) == SDL_PRESSED);
    stdl_events_remove();
    CHECK(req.rumble[0][0] == 0 && req.rumble[3][0] == 0);
    CHECK(tx_is(3, 0x13, 0x11, 0x08));
}

/* Merged to apart and back with everything held: whatever changes
 * hands is let go, rumble with it, and the new owners pick up. */
static void switch_merge(void)
{
    provider(4, XPAD_CAP_ANALOG | XPAD_CAP_RUMBLE);
    four_states();
    CHECK(STDL_SetNumJoysticks(6) == 4);        /* merged, four at most */
    STDL_OpenInput();
    pump();
    pump();
    ikbd(2, 0xFE, 0x01);                        /* the mouse port: up */
    pump();
    CHECK(STDL_GetJoystickState(0) == 0x88 && STDL_HasPad(0));
    CHECK(STDL_GetJoystickState(1) == 0x05);    /* port up, pad 1 left */
    CHECK(STDL_JoystickInputHeld(3, STDL_JOYSTICK_FIRE) == 1);
    CHECK(STDL_PadRumble(3, 50, 50, 5000) == 0 && req.rumble[3][0] == 50);
    CHECK(STDL_PadRumble(0, 60, 60, 5000) == 0 && req.rumble[0][0] == 60);
    ntx = 0;

    /* apart: six now; 1-3 let go of their pads at once */
    nev = 0;
    CHECK(STDL_UseJoystickMerge(0) == 1);
    CHECK(STDL_NumJoysticks() == 6);
    CHECK(tx_is(0));                            /* the mouse stays off */
    CHECK(req.rumble[0][0] == 0 && req.rumble[3][0] == 0);
    CHECK(STDL_HasPad(0) == 0 && STDL_HasPad(1) == 0 && STDL_HasPad(5));
    /* pad 0's own inputs are gone from joystick 0 at once */
    CHECK(STDL_JoystickInputHeld(0, STDL_JOYSTICK_TR) == 0);
    CHECK(STDL_GetJoystickState(1) == 0x01);    /* its port kept */
    CHECK(STDL_JoystickInputHeld(1, STDL_JOYSTICK_EAST) == 0);
    CHECK(STDL_GetJoystickState(3) == 0);       /* pad 3 let go */
    frames(3);                                  /* a pad a frame, room
                                                   permitting */
    /* joystick 0 lets go of pad 0 at the pump; joystick 1 of pad 1 */
    CHECK(has_button(0, 0, 0) && has_button(0, 5, 0) && has_axis(0, 0, 0));
    CHECK(has_button(1, 1, 0) && has_axis(1, 0, 0));
    CHECK(has_button(3, 0, 0) && has_axis(3, 3, 0));
    CHECK(STDL_GetJoystickState(0) == 0);
    /* and the pads' new joysticks pick them up */
    CHECK(STDL_GetJoystickState(2) == 0x88 && STDL_GetJoystickState(5) == 0x82);
    CHECK(has_button(2, 0, 1) && has_button(5, 0, 1));
    /* as joystick 0's at the start: the five a stick has are news, the
     * rest held for polling only (pad 0 holds TR, button 5) */
    CHECK(has_button(2, 5, 1) == 0);
    CHECK(STDL_JoystickInputHeld(2, STDL_JOYSTICK_TR) == 1);

    /* back to merged: six asked for is four again */
    CHECK(STDL_UseJoystickMerge(1) == 0);
    CHECK(STDL_NumJoysticks() == 4);
    frames(3);
    CHECK(has_button(5, 0, 0) && has_axis(5, 3, 0));     /* dropped */
    CHECK(has_button(2, 0, 0));                          /* pad 0 went */
    CHECK(STDL_GetJoystickState(0) == 0x88 && STDL_HasPad(0));
    CHECK(STDL_GetJoystickState(1) == 0x05 && STDL_HasPad(1));
    CHECK(STDL_GetJoystickState(3) == 0x82);
    /* joystick 0's pad 0 is back as at the start: the five, no more */
    CHECK(has_button(0, 0, 1) && has_button(0, 5, 1) == 0);
}

/* One joystick apart: the port alone, pad 0 nobody's, and no
 * joysticks.c needed for it. */
static void apart_one(void)
{
    provider(4, XPAD_CAP_ANALOG | XPAD_CAP_RUMBLE);
    four_states();
    STDL_UseJoystickMerge(0);
    STDL_OpenInput();
    pump();
    pump();
    CHECK(nev == 0 && STDL_GetJoystickState(0) == 0);
    CHECK(STDL_HasPad(0) == 0 && STDL_PadRumble(0, 1, 1, 10) == -1);
    CHECK(stdl_joyn == NULL && STDL_NumJoysticks() == 1);
    CHECK(SDL_NumJoysticks() == 1);
    {
        SDL_Joystick *j = SDL_JoystickOpen(0);

        CHECK(SDL_JoystickNumAxes(j) == 2 && SDL_JoystickNumHats(j) == 0);
    }
    /* merged again: pad 0 joins at the next pump */
    STDL_UseJoystickMerge(1);
    pump();
    CHECK(STDL_GetJoystickState(0) == 0x88 && has_button(0, 0, 1));
}

/*
 * Fire held on port 1 while the mouse reports arrives as the right
 * button, folded into joystick 0's fire. The mouse turned off then:
 * fire's release comes in port 1's own packet, which must not get the
 * old right button back, or fire stays held for good.
 */
static void fire_mouse_off(void)
{
    STDL_OpenInput();
    ikbd(3, 0xF9, 0x00, 0x00);              /* right button: fire */
    pump();
    CHECK(STDL_GetJoystickState(0) == 0x80);
    STDL_EnableMouse(0);
    ikbd(2, 0xFF, 0x00);                    /* fire let go */
    pump();
    CHECK(STDL_GetJoystickState(0) == 0x00);
    CHECK(has_button(0, 0, 0));
    /* a mouse packet in flight as the mouse went off, the right button
     * pressed: no fire, as the port's packets carry it now */
    ikbd(3, 0xF8, 0x00, 0x00);
    pump();
    STDL_EnableMouse(1);
    STDL_EnableMouse(0);
    ikbd(3, 0xF9, 0x00, 0x00);
    ikbd(2, 0xFF, 0x00);
    pump();
    CHECK(STDL_GetJoystickState(0) == 0x00);
    /* and with two joysticks, the same; the ISR reports the buttons
     * only when they change, so let go first */
    STDL_EnableMouse(1);
    ikbd(3, 0xF8, 0x00, 0x00);
    ikbd(3, 0xF9, 0x00, 0x00);
    pump();
    CHECK(STDL_GetJoystickState(0) == 0x80);
    STDL_SetNumJoysticks(2);
    ikbd(2, 0xFF, 0x00);
    pump();
    CHECK(STDL_GetJoystickState(0) == 0x00);
}

/* every input of a pad: all the buttons, both sticks and triggers */
static void everything(XPAD_PAD *p, int on)
{
    p->buttons = on ? 0x1FFFFUL & ~(XPAD_DOWN | XPAD_RIGHT) : 0;
    p->lx = p->ly = p->rx = p->ry = (int8_t)(on ? -127 : 0);
    p->lt = p->rt = (uint8_t)(on ? 255 : 0);
}

static int n_of(int which, int type)
{
    int i, n = 0;

    for (i = 0; i < nev; i++) {
        n += ev[i].type == type && ev[i].jaxis.which == which;
    }
    return n;
}

/*
 * Four pads, everything pressed at once and let go again: about twenty
 * events a pad, eighty in all against a queue of 32. A program taking
 * its events once a frame gets every one, a pad a frame (the pump
 * runs once a VBL, and reads a pad only with room for twenty); none
 * is lost, so every press has its release.
 */
static void none_lost(void)
{
    int i, j;

    provider(4, XPAD_CAP_ANALOG);
    for (i = 0; i < 4; i++) {
        slot(i)->type = XPAD_TYPE_GAMEPAD;
    }
    STDL_UseJoystickMerge(0);
    CHECK(STDL_SetNumJoysticks(6) == 6);
    STDL_OpenInput();
    pump();
    for (j = 0; j < 2; j++) {
        for (i = 0; i < 4; i++) {
            everything(slot(i), j == 0);
        }
        pump();
        CHECK(nev == 20);                   /* pad 0, joystick 2 */
        frame();
        frame();
        frame();
        for (i = 2; i < 6; i++) {
            /* buttons 0 to 12, six axes, the hat */
            CHECK(n_of(i, j == 0 ? STDL_JOYBUTTONDOWN : STDL_JOYBUTTONUP)
                  == 13);
            CHECK(n_of(i, STDL_JOYAXISMOTION) == 6);
            CHECK(n_of(i, STDL_JOYHATMOTION) == 1);
        }
        CHECK(nev == 4 * 20);
    }
}

/* A program that only pumps and polls never empties the queue; its
 * pads must not wait for room that never comes. */
static void poll_only(void)
{
    int k;

    provider(4, XPAD_CAP_ANALOG);
    slot(0)->type = slot(2)->type = XPAD_TYPE_GAMEPAD;
    STDL_UseJoystickMerge(0);
    CHECK(STDL_SetNumJoysticks(3) == 3);
    STDL_OpenInput();
    for (k = 0; k < 40; k++) {
        ikbd(2, 0xFF, (k & 1) ? 0x01 : 0x02);   /* port 1, to fill it */
        STDL_PumpEvents();
    }
    slot(0)->buttons = XPAD_UP | XPAD_START;
    STDL_PumpEvents();
    CHECK(STDL_GetJoystickState(2) == 0x01);
    CHECK(STDL_JoystickInputHeld(2, STDL_JOYSTICK_START) == 1);
    slot(0)->buttons = 0;
    STDL_PumpEvents();
    CHECK(STDL_GetJoystickState(2) == 0x00);
}

/*
 * A pad as it was is no news (three long compares, and the pump skips
 * the rest), for joystick 0's record and the others'; a change in any
 * of the three longs is news. Apart, pad 0 lets go once and is then
 * not read at all. The pump would find nothing to say either way, so
 * only the reading itself can show the work skipped.
 */
static void quiet(void)
{
    stdl_pad_t *p2;

    provider(4, XPAD_CAP_ANALOG);
    slot(0)->type = slot(2)->type = XPAD_TYPE_GAMEPAD;
    CHECK(STDL_SetNumJoysticks(3) == 3);
    STDL_OpenInput();
    pump();
    p2 = &stdl_joyn->pads[2];
    CHECK(stdl_xpad_read(&stdl_pad0) == 0 && stdl_xpad_read(p2) == 0);
    slot(0)->buttons = XPAD_A;              /* the first long */
    slot(2)->rx = 40;                       /* the second */
    CHECK(stdl_xpad_read(&stdl_pad0) == 1 && stdl_xpad_read(p2) == 1);
    pump();
    CHECK(stdl_xpad_read(&stdl_pad0) == 0 && stdl_xpad_read(p2) == 0);
    slot(0)->lt = slot(2)->rt = 200;        /* the third */
    CHECK(stdl_xpad_read(&stdl_pad0) == 1 && stdl_xpad_read(p2) == 1);
    pump();
    /* apart: pad 0 let go at the next pump, and after that not read */
    STDL_UseJoystickMerge(0);
    pump();
    CHECK(has_button(0, 0, 0));             /* A, fire */
    slot(0)->buttons = XPAD_B;
    CHECK(stdl_xpad_read(&stdl_pad0) == 0);
}

/*
 * Joystick 0's pad waits for room as the others' do. The queue holds
 * joystick 1's twenty when pad 0 changes everything: its own twenty
 * come at the next frame's pump, once the drain has made room; with
 * key emulation, whose keys come with them (three here: up, left and
 * fire), once the queue is empty.
 */
static void pad0_waits(void)
{
    int k;

    provider(4, XPAD_CAP_ANALOG);
    slot(0)->type = slot(1)->type = XPAD_TYPE_GAMEPAD;
    CHECK(STDL_SetNumJoysticks(2) == 2);
    STDL_OpenInput();
    pump();
    for (k = 0; k < 2; k++) {
        const int type = k ? STDL_JOYBUTTONUP : STDL_JOYBUTTONDOWN;

        if (k) {
            STDL_JoystickKeyEmulation(1);   /* three downs at once */
        }
        everything(slot(1), !k);
        STDL_PumpEvents();                  /* joystick 1's, untaken */
        everything(slot(0), !k);
        pump();
        /* no room for pad 0 at this frame's pump: joystick 1's only */
        CHECK(n_of(0, type) == 0 && n_of(1, type) == 13);
        nev = 0;
        frame();
        CHECK(n_of(0, type) == 13);
        CHECK(n_of(0, STDL_JOYAXISMOTION) == 6 && n_of(0, STDL_JOYHATMOTION) == 1);
        CHECK(n_type(STDL_KEYUP) == (k ? 3 : 0));
        CHECK(nev == (k ? 23 : 20));
    }
}

/* Apart and merged again with no pump between: pad 0 let go of
 * nothing, so what it lets go of afterwards is still news. */
static void flicked(void)
{
    provider(4, XPAD_CAP_ANALOG);
    slot(0)->type = XPAD_TYPE_GAMEPAD;
    STDL_OpenInput();
    pump();
    slot(0)->buttons = XPAD_EAST;           /* button 1 */
    pump();
    CHECK(has_button(0, 1, 1));
    STDL_UseJoystickMerge(0);
    STDL_UseJoystickMerge(1);
    slot(0)->buttons = 0;
    pump();
    CHECK(has_button(0, 1, 0));
}

/* where in ev[] joystick `which`'s first event of `type` is, or the
 * end */
static int at(int type, int which)
{
    int i;

    for (i = 0; i < nev; i++) {
        if (ev[i].type == type && ev[i].jaxis.which == which) {
            return i;
        }
    }
    return nev;
}

/* the device events in ev[]: "+1" ADDED 1, "-1" REMOVED 1 */
static const char *devs(void)
{
    static char d[64];
    int i, n = 0;

    for (i = 0; i < nev && n < 60; i++) {
        if (ev[i].type == STDL_JOYDEVICEADDED
            || ev[i].type == STDL_JOYDEVICEREMOVED) {
            d[n++] = ev[i].type == STDL_JOYDEVICEADDED ? '+' : '-';
            d[n++] = (char)('0' + ev[i].jdevice.which);
        }
    }
    d[n] = '\0';
    return d;
}

/*
 * Device events, for a program that asks: ADDED for every joystick
 * there is at the asking, then each change straight after the
 * releases it brings - a change undone between two pumps included,
 * 2 to 1 to 2 and the merge set twice, which a layer comparing counts
 * at its pump never saw. None for a program that never asks, as the
 * SDL 1.2 shim never does.
 */
static void devices(void)
{
    provider(4, XPAD_CAP_ANALOG);
    four_states();
    CHECK(STDL_SetNumJoysticks(2) == 2);
    STDL_OpenInput();
    frames(2);
    STDL_SetNumJoysticks(1);
    STDL_SetNumJoysticks(2);
    frames(2);
    CHECK(strcmp(devs(), "") == 0);

    CHECK(STDL_UseJoystickDeviceEvents(1) == 0);
    CHECK(STDL_UseJoystickDeviceEvents(-1) == 1);
    pump();
    CHECK(strcmp(devs(), "+0+1") == 0);
    frames(2);

    /* joystick 1 holds pad 1's left and east (button 1) */
    STDL_SetNumJoysticks(1);
    STDL_SetNumJoysticks(2);
    pump();
    CHECK(strcmp(devs(), "-1+1") == 0);
    CHECK(has_button(1, 1, 0) && has_axis(1, 0, 0));
    CHECK(at(STDL_JOYBUTTONUP, 1) < at(STDL_JOYDEVICEREMOVED, 1));
    CHECK(at(STDL_JOYAXISMOTION, 1) < at(STDL_JOYDEVICEREMOVED, 1));
    /* and picks the pad up again after it is back */
    CHECK(at(STDL_JOYDEVICEADDED, 1) < nev
          && has_axis(1, 0, -60 * 258));
    frames(2);

    /* joystick 0 holds pad 0's fire, right and TR */
    STDL_UseJoystickMerge(0);
    STDL_UseJoystickMerge(1);
    pump();
    CHECK(strcmp(devs(), "-0+0-1+1-0+0-1+1") == 0);
    CHECK(has_button(0, 0, 0) && has_button(0, 5, 0));
    CHECK(at(STDL_JOYBUTTONUP, 0) < at(STDL_JOYDEVICEREMOVED, 0));
    frames(2);

    /* off: none */
    CHECK(STDL_UseJoystickDeviceEvents(0) == 1);
    STDL_SetNumJoysticks(1);
    STDL_UseJoystickMerge(0);
    pump();
    CHECK(strcmp(devs(), "") == 0);
}

/*
 * The readers, as the events carry them: every axis a joystick's
 * events last said, and its hat; 0 for a joystick, an axis or a pad
 * that is not there.
 */
static void readers(void)
{
    int i, a;

    provider(4, XPAD_CAP_ANALOG);
    four_states();
    STDL_UseJoystickMerge(0);
    CHECK(STDL_SetNumJoysticks(6) == 6);
    STDL_OpenInput();
    frames(2);
    ikbd(2, 0xFE, 0x04);                    /* the mouse port: left */
    for (i = 0; i < 4; i++) {
        slot(i)->rx = (int8_t)(10 * i - 20);
        slot(i)->lt = (uint8_t)(40 * i);
    }
    frames(4);
    CHECK(nev >= 7);                        /* the port and six pad axes */
    for (i = 0; i < nev; i++) {
        if (ev[i].type == STDL_JOYAXISMOTION) {
            a = 0;
            /* the last word on it */
            for (a = i + 1; a < nev; a++) {
                if (ev[a].type == STDL_JOYAXISMOTION
                    && ev[a].jaxis.which == ev[i].jaxis.which
                    && ev[a].jaxis.axis == ev[i].jaxis.axis) {
                    break;
                }
            }
            if (a == nev) {
                CHECK(STDL_GetJoystickAxis(ev[i].jaxis.which,
                                           ev[i].jaxis.axis)
                      == ev[i].jaxis.value);
            }
        }
    }
    CHECK(STDL_GetJoystickAxis(1, STDL_JOYSTICK_AXIS_X) == -32768);
    CHECK(STDL_GetJoystickAxis(2, STDL_JOYSTICK_AXIS_X) == 100 * 258);
    CHECK(STDL_GetJoystickAxis(4, STDL_JOYSTICK_AXIS_RX) == 0);
    CHECK(STDL_GetJoystickAxis(5, STDL_JOYSTICK_AXIS_RX) == 10 * 258);
    CHECK(STDL_GetJoystickHat(2) == STDL_HAT_RIGHT);
    CHECK(STDL_GetJoystickHat(4) == STDL_HAT_UP);
    CHECK(STDL_GetJoystickHat(5) == STDL_HAT_DOWN);
    /* no pad: the ports' own, and nothing else */
    CHECK(STDL_GetJoystickAxis(0, STDL_JOYSTICK_AXIS_LT) == 0);
    CHECK(STDL_GetJoystickAxis(1, STDL_JOYSTICK_AXIS_RT) == 0);
    CHECK(STDL_GetJoystickHat(0) == 0 && STDL_GetJoystickHat(1) == 0);
    /* nothing there */
    CHECK(STDL_GetJoystickAxis(6, 0) == 0 && STDL_GetJoystickAxis(-1, 0) == 0);
    CHECK(STDL_GetJoystickAxis(2, STDL_JOYSTICK_AXES) == 0);
    CHECK(STDL_GetJoystickAxis(2, -1) == 0);
    CHECK(STDL_GetJoystickHat(6) == 0 && STDL_GetJoystickHat(-1) == 0);
    STDL_SetNumJoysticks(2);
    CHECK(STDL_GetJoystickAxis(2, STDL_JOYSTICK_AXIS_X) == 0);
    CHECK(STDL_GetJoystickHat(5) == 0);
    /* merged: joystick 0 the port and pad 0 */
    STDL_UseJoystickMerge(1);
    frames(2);
    CHECK(STDL_GetJoystickAxis(0, STDL_JOYSTICK_AXIS_X) == 100 * 258);
    CHECK(STDL_GetJoystickHat(0) == STDL_HAT_RIGHT);
    CHECK(STDL_GetJoystickAxis(0, STDL_JOYSTICK_AXIS_RX) == -20 * 258);
}

/* the pumps, counted through the pump's cursor hook, which only the
 * pump calls */
static int pumps;

static void count_pump(int x, int y)
{
    (void)x;
    (void)y;
    pumps++;
}

/*
 * The pump's beat. STDL_PollEvent pumps at its first call and then
 * once a VBL: a drain of a frame's events is one pump however many it
 * takes, a key typed in the middle of a frame waits for the next VBL,
 * and between pumps the timers and audio are served instead (time.c's
 * idle services). A VBL that does not count gives a pump every five
 * ticks of the 200Hz clock. STDL_PumpEvents and STDL_WaitEvent pump
 * whenever they are called.
 */
static void beat(void)
{
    STDL_Event e;
    int i, n;

    STDL_OpenInput();
    stdl_cursor_hook = count_pump;
    ikbd(8, 0x1E, 0x9E, 0x30, 0xB0, 0x2E, 0xAE, 0x20, 0xA0);
    n = 0;
    idles = 0;
    while (STDL_PollEvent(&e)) {
        n++;
    }
    CHECK(n == 8 && pumps == 1);            /* the first call pumps */
    CHECK(idles == 8);                      /* the other eight serve */

    /* a key in the same VBL waits for the next */
    ikbd(2, 0x1E, 0x9E);
    CHECK(STDL_PollEvent(&e) == 0 && pumps == 1);
    stdl_host_frclock++;
    CHECK(STDL_PollEvent(&e) && e.type == STDL_KEYDOWN && pumps == 2);
    CHECK(STDL_PollEvent(&e) && e.type == STDL_KEYUP && pumps == 2);

    /* only looking: still a pump a VBL, the events left where they are */
    ikbd(1, 0x1E);
    for (i = 0; i < 5; i++) {
        stdl_host_frclock++;
        for (n = 0; n < 10; n++) {
            STDL_PollEvent(NULL);
        }
    }
    CHECK(pumps == 7 && STDL_PollEvent(NULL) == 1);
    CHECK(STDL_PollEvent(&e) && e.type == STDL_KEYDOWN);

    /* a VBL that does not count: five ticks of the 200Hz clock */
    stdl_host_clock += 4;
    STDL_PollEvent(NULL);
    CHECK(pumps == 7);
    stdl_host_clock += 1;
    STDL_PollEvent(NULL);
    CHECK(pumps == 8);

    /* asked for, always */
    STDL_PumpEvents();
    STDL_PumpEvents();
    CHECK(pumps == 10);

    /* a wait pumps at its first look, with no delay */
    ikbd(1, 0x9E);
    n = (int)stdl_host_clock;
    CHECK(STDL_WaitEvent(&e) && e.type == STDL_KEYUP && pumps == 11);
    CHECK((int)stdl_host_clock == n);
    stdl_cursor_hook = NULL;
}

/* key repeats over `ms` of 50Hz frames, each pumped and drained */
static int repeats(int ms)
{
    STDL_Event e;
    int n = 0;

    for (; ms > 0; ms -= 20) {
        stdl_host_clock += 4;
        stdl_host_frclock++;
        while (STDL_PollEvent(&e)) {
            n += e.type == STDL_KEYDOWN;
        }
    }
    return n;
}

/*
 * Key repeat keeps its own beat with a pump a VBL: 30ms repeats are
 * 33 a second, not 25 (each timed from a 50Hz pump, they came every
 * 40ms). A program away for a second gets one, not the lost ones.
 */
static void repeat_beat(void)
{
    STDL_Event e;

    STDL_OpenInput();
    STDL_EnableKeyRepeat(100, 30);
    ikbd(1, 0x48);                          /* up, a key with a sym
                                               of its own here */
    while (STDL_PollEvent(&e)) {
    }
    CHECK(repeats(80) == 0);                /* the delay */
    CHECK(repeats(920) == 31);              /* at 100, then every 30 */
    stdl_host_clock += 200;                 /* a second away */
    stdl_host_frclock += 50;
    CHECK(repeats(20) == 1);
    CHECK(repeats(20) == 0);                /* the next 30ms on */
    CHECK(repeats(20) == 1);
}

static void run(const char *name, void (*fn)(void))
{
    pid_t pid;
    int status = 0;

    if (only != NULL && strcmp(only, name) != 0) {
        return;
    }
    fflush(stdout);
    pid = fork();

    if (pid == 0) {
        failures = 0;                   /* the parent's are not ours */
        fn();
        _exit(failures ? 1 : 0);
    }
    waitpid(pid, &status, 0);
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        fprintf(stderr, "test_joystick: %s FAILED (%s %d)\n", name,
                WIFEXITED(status) ? "exit" : "signal",
                WIFEXITED(status) ? WEXITSTATUS(status) : WTERMSIG(status));
        failures++;
    }
}

int main(int argc, char **argv)
{
    only = argc > 1 ? argv[1] : NULL;
    run("one joystick", one_joystick);
    run("two ports", two_ports);
    run("switching", switching);
    run("four pads", four_pads);
    run("pads 1-3 as pad 0", same_as_pad0);
    run("three slots", few_slots);
    run("two slots", two_slots);
    run("exit ends every rumble", exit_rumble);
    run("one slot", one_slot);
    run("pads, one joystick", pads_with_one);
    run("apart, six", apart_six);
    run("merged and apart", switch_merge);
    run("apart, one", apart_one);
    run("fire, then the mouse off", fire_mouse_off);
    run("eighty events, none lost", none_lost);
    run("pumped, never taken", poll_only);
    run("a pad as it was, not reported", quiet);
    run("pad 0 waits for room", pad0_waits);
    run("apart and back between pumps", flicked);
    run("the pump's beat", beat);
    run("key repeat's own beat", repeat_beat);
    run("device events, asked for", devices);
    run("axes and hats, read", readers);
    if (failures) {
        fprintf(stderr, "test_joystick: FAILED\n");
        return 1;
    }
    printf("test_joystick: all passed\n");
    return 0;
}
