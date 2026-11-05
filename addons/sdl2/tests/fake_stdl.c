/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * The STDL calls the SDL2 add-on makes, stood in for on the host: a
 * responsive 256-colour screen in a TT's layout (a row on two lines,
 * 640 bytes), a Falcon's on VGA (320) or on RGB (384, the picture 32
 * bytes in), with two or three pages that STDL_Flip moves through as
 * STDL does, and registers that STDL_SetColours writes; and STDL's
 * input, events queued by the test and the state it sets. The add-on
 * reaches STDL through these public calls alone, so what it draws and
 * what it reports can be checked here.
 */

#include <stdl/stdl.h>
#include <stdlib.h>
#include <string.h>

#include "fake_stdl.h"

int fake_layout;                /* FAKE_TT, FAKE_VGA or FAKE_RGB */
int fake_third = 1;             /* ST-RAM for a third page */
int fake_npages;
int fake_shown;                 /* the page on display */
int fake_flips, fake_waits, fake_stagings;
STDL_Colour fake_regs[256];
Uint8 *fake_page[3];
long fake_stride, fake_margin;
STDL_MachineInfo fake_mach;

static STDL_Surface surf;
static STDL_PixelFormat fmt;
static int back;

struct stdl_resp_model { int unused; };
struct stdl_resp_banks { int unused; };
const struct stdl_resp_model stdl_resp_colour = { 0 }, stdl_resp_tall = { 0 };
const struct stdl_resp_banks stdl_resp_banks = { 0 };

void stdl_resp_tall_st(int h)
{
    (void)h;
}

int stdl_use_responsive(unsigned what, const struct stdl_resp_model *colour,
                        const struct stdl_resp_model *tall,
                        void (*st_tall)(int h),
                        const struct stdl_resp_banks *banks)
{
    (void)tall;
    (void)st_tall;
    (void)banks;
    /* the add-on asks for the colour and the display's height alone */
    return (what == (STDL_RESPONSIVE_COLOUR | STDL_RESPONSIVE_HEIGHT) &&
            colour) ? 0 : -1;
}

int STDL_Init(uint32_t flags)
{
    (void)flags;
    return 0;
}

void STDL_Quit(void)
{
}

const char *STDL_GetError(void)
{
    return "fake";
}

const STDL_MachineInfo *STDL_GetMachineInfo(void)
{
    return &fake_mach;
}

STDL_Surface *STDL_SetVideoMode(int w, int h, int bpp, uint32_t flags)
{
    int i;
    (void)w;
    (void)h;
    (void)bpp;
    fake_stride = fake_layout == FAKE_TT ? 640 : fake_layout == FAKE_RGB ? 384
                                                                          : 320;
    fake_margin = fake_layout == FAKE_RGB ? 32 : 0;
    fake_npages = ((flags & STDL_TRIPLEBUF) && fake_third) ? 3 : 2;
    for (i = 0; i < fake_npages; i++) {
        fake_page[i] = (Uint8 *)calloc((size_t)fake_stride * 240, 1);
    }
    fake_shown = 0;
    back = 1;
    memset(&surf, 0, sizeof(surf));
    memset(&fmt, 0, sizeof(fmt));
    fmt.BitsPerPixel = 8;
    fmt.BytesPerPixel = 1;
    surf.format = &fmt;
    surf.w = 320;
    surf.h = 240;
    surf.stride = (uint16_t)fake_stride;
    surf.planes = 8;
    surf.flags = STDL_SCREEN | STDL_DOUBLEBUF |
                 (fake_npages == 3 ? STDL_TRIPLEBUF : 0);
    surf.pixels = fake_page[back] + fake_margin;
    return &surf;
}

void STDL_Flip(void)
{
    fake_shown = back;
    back = (back + 1) % fake_npages;
    surf.pixels = fake_page[back] + fake_margin;
    fake_flips++;
}

void STDL_WaitVBL(void)
{
    fake_waits++;
}

int STDL_StagePalette(int on)
{
    (void)on;
    return 0;
}

int STDL_SetColours(STDL_Surface *s, const STDL_Colour *cols, int first, int n)
{
    (void)s;
    memcpy(fake_regs + first, cols, (size_t)n * sizeof(*cols));
    fake_stagings++;
    return 0;
}

static uint32_t clock_ticks;

uint32_t STDL_GetHz38400(void)
{
    return clock_ticks += 7;
}

uint32_t STDL_GetTicks(void)
{
    return clock_ticks;
}

void STDL_Delay(uint32_t ms)
{
    clock_ticks += ms * 38;
}

/* ------------------------------------------------------------------ */
/* input                                                               */

uint16_t fake_mod;
int fake_mouse_x, fake_mouse_y;
uint8_t fake_mouse_buttons;
int fake_repeat_delay = -1, fake_repeat_interval = -1, fake_unicode = -1;
int fake_pad;
uint32_t fake_held;
int fake_rumble_low, fake_rumble_high, fake_rumble_ms, fake_rumbles;

static STDL_Event events[64];
static int nevents, evhead;

void fake_event(const STDL_Event *e)
{
    if (nevents < 64) {
        events[nevents++] = *e;
    }
}

void fake_key(int down, unsigned sym, unsigned mod, unsigned unicode)
{
    STDL_Event e;
    memset(&e, 0, sizeof(e));
    e.type = down ? STDL_KEYDOWN : STDL_KEYUP;
    e.key.type = e.type;
    e.key.state = down ? STDL_PRESSED : STDL_RELEASED;
    e.key.keysym.sym = (uint16_t)sym;
    e.key.keysym.mod = (uint16_t)mod;
    e.key.keysym.unicode = (uint16_t)unicode;
    fake_event(&e);
}

int STDL_PollEvent(STDL_Event *e)
{
    if (evhead == nevents) {
        evhead = nevents = 0;
        return 0;
    }
    *e = events[evhead++];
    return 1;
}

uint16_t STDL_GetModState(void)
{
    return fake_mod;
}

uint8_t STDL_GetMouseState(int *x, int *y)
{
    if (x) {
        *x = fake_mouse_x;
    }
    if (y) {
        *y = fake_mouse_y;
    }
    return fake_mouse_buttons;
}

int STDL_EnableKeyRepeat(int delay, int interval)
{
    fake_repeat_delay = delay;
    fake_repeat_interval = interval;
    return 0;
}

int STDL_EnableUNICODE(int enable)
{
    const int was = fake_unicode;
    fake_unicode = enable;
    return was;
}

int STDL_HavePad(void)
{
    return fake_pad;
}

int STDL_JoyInputHeld(int input)
{
    return (fake_held >> input) & 1;
}

int STDL_PadRumble(uint8_t low, uint8_t high, uint16_t ms)
{
    if (!fake_pad) {
        return -1;
    }
    fake_rumble_low = low;
    fake_rumble_high = high;
    fake_rumble_ms = ms;
    fake_rumbles++;
    return 0;
}
