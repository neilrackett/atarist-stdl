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
int fake_tall, fake_st_rows = 240;  /* TALL asked for; what it gives */
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
    (void)banks;
    /* the add-on asks for the colour and the display's height alone;
     * its ST build for the tall screen alone, an ST's border with it */
    if (what == STDL_RESPONSIVE_TALL && tall && st_tall) {
        fake_tall = 1;
        return 0;
    }
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
    /* an ST's tall screen: the rows its border gives, two pages */
    if (bpp == 4 && h > 200) {
        h = fake_tall ? fake_st_rows : 200;
        if (h > 200) {
            flags &= ~STDL_TRIPLEBUF;
        }
    }
    /* 8 planes of 240 rows as the layout has them, or an ST's four,
     * 160 bytes a row, which the add-on's ST build asks for */
    fake_stride = bpp == 4 ? 160 : fake_layout == FAKE_TT ? 640
                : fake_layout == FAKE_RGB ? 384 : 320;
    fake_margin = bpp != 4 && fake_layout == FAKE_RGB ? 32 : 0;
    fake_npages = ((flags & STDL_TRIPLEBUF) && fake_third) ? 3 : 2;
    for (i = 0; i < fake_npages; i++) {
        fake_page[i] = (Uint8 *)calloc((size_t)fake_stride * h, 1);
    }
    fake_shown = 0;
    back = 1;
    memset(&surf, 0, sizeof(surf));
    memset(&fmt, 0, sizeof(fmt));
    fmt.BitsPerPixel = (uint8_t)bpp;
    fmt.BytesPerPixel = 1;
    surf.format = &fmt;
    surf.w = 320;
    surf.h = (uint16_t)h;
    surf.stride = (uint16_t)fake_stride;
    surf.planes = (uint8_t)bpp;
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
int fake_rumble_low, fake_rumble_high, fake_rumble_ms, fake_rumbles;
int fake_joys = 1, fake_pads[6], fake_rumble_which;
uint32_t fake_helds[6];

/* STDL's queue, and what its next pump brings (fake_event); the
 * pumps' beat a VBL count of the test's own */
static STDL_Event events[64], pending[64];
static int nevents, evhead, npending;
uint32_t fake_vbl;
int fake_pumps, fake_idles;
static uint32_t pumped_vbl;
static int pumped;

void fake_event(const STDL_Event *e)
{
    if (npending < 64) {
        pending[npending++] = *e;
    }
}

void STDL_PumpEvents(void)
{
    int i;
    fake_pumps++;
    pumped = 1;
    pumped_vbl = fake_vbl;
    if (evhead == nevents) {
        evhead = nevents = 0;
    }
    for (i = 0; i < npending && nevents < 64; i++) {
        events[nevents++] = pending[i];
    }
    npending = 0;
}

int STDL_PumpEventsIfDue(void)
{
    if (pumped && fake_vbl == pumped_vbl) {
        fake_idles++;
        return 0;
    }
    STDL_PumpEvents();
    return 1;
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
    STDL_PumpEventsIfDue();
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

/* As STDL's: each joystick's pad and held inputs, there only while
 * there are that many */
int STDL_NumJoysticks(void)
{
    return fake_joys;
}

/* STDL's device events, as the core pushes them: ADDED for each one
 * there is when they are asked for, then each change */
int fake_devices;

static void fake_device(int added, int which)
{
    STDL_Event e;
    if (fake_devices) {
        memset(&e, 0, sizeof(e));
        e.type = added ? STDL_JOYDEVICEADDED : STDL_JOYDEVICEREMOVED;
        e.jdevice.which = (uint8_t)which;
        fake_event(&e);
    }
}

int STDL_UseJoystickDeviceEvents(int on)
{
    const int was = fake_devices;
    int i;
    if (on >= 0 && (on != 0) != was) {
        fake_devices = on != 0;
        for (i = 0; on && i < fake_joys; i++) {
            fake_device(1, i);
        }
    }
    return was;
}

/* as STDL_SetNumJoysticks says it: each one gone, each one come */
void fake_set_joys(int n)
{
    int i;
    for (i = 1; i < 6; i++) {
        if (i < fake_joys && i >= n) {
            fake_device(0, i);
        } else if (i >= fake_joys && i < n) {
            fake_device(1, i);
        }
    }
    fake_joys = n;
}

/* as STDL_UseJoystickMerge says it with every slot changing: joystick
 * 0 gone and back, then each of the others gone and each there now */
void fake_set_merge(int n)
{
    int i;
    fake_device(0, 0);
    fake_device(1, 0);
    for (i = 1; i < 6; i++) {
        if (i < fake_joys) {
            fake_device(0, i);
        }
        if (i < n) {
            fake_device(1, i);
        }
    }
    fake_joys = n;
}

/* the axes and hats as STDL's readers say them */
int fake_axes[6][6], fake_hats[6];

int16_t STDL_GetJoystickAxis(int which, int axis)
{
    return (which >= 0 && which < fake_joys && axis >= 0 && axis < 6)
        ? (int16_t)fake_axes[which][axis] : 0;
}

uint8_t STDL_GetJoystickHat(int which)
{
    return (which >= 0 && which < fake_joys) ? (uint8_t)fake_hats[which] : 0;
}

static int fake_there(int which)
{
    return which >= 0 && which < fake_joys;
}

int STDL_HasPad(int which)
{
    return fake_there(which) && fake_pads[which];
}

int STDL_JoystickInputHeld(int which, int input)
{
    return fake_there(which) ? (int)((fake_helds[which] >> input) & 1) : 0;
}

int STDL_PadRumble(int which, uint8_t low, uint8_t high, uint16_t ms)
{
    if (!STDL_HasPad(which)) {
        return -1;
    }
    fake_rumble_low = low;
    fake_rumble_high = high;
    fake_rumble_ms = ms;
    fake_rumble_which = which;
    fake_rumbles++;
    return 0;
}

/* ------------------------------------------------------------------ */
/* the ring device                                                     */

int fake_audio_open, fake_audio_paused, fake_audio_falcon;
STDL_AudioSpec fake_audio_spec;
Uint8 fake_audio_out[1 << 20] = { 0 };
Uint32 fake_audio_outlen;
static int dma_rate;
static uint32_t rate_err;
static Uint8 scratch[1 << 16];

/* a half ring's worth of the callback, as ring.c's fill_frames asks:
 * 2,048 DMA frames' worth of source frames, carrying the remainder */
static void pull(Uint8 *dst)
{
    const int fb = fake_audio_spec.channels *
                   ((fake_audio_spec.format & 0xFF) / 8);
    const uint32_t want = 2048u * (uint32_t)fake_audio_spec.freq + rate_err;
    const int frames = (int)(want / (uint32_t)dma_rate);
    rate_err = want % (uint32_t)dma_rate;
    memset(dst, fake_audio_spec.silence, (size_t)frames * fb);
    if (fake_audio_spec.callback) {
        fake_audio_spec.callback(fake_audio_spec.userdata, dst, frames * fb);
    }
    if (dst != scratch) {
        return;
    }
    if (fake_audio_outlen + (uint32_t)(frames * fb) <= sizeof(fake_audio_out)) {
        memcpy(fake_audio_out + fake_audio_outlen, dst, (size_t)frames * fb);
        fake_audio_outlen += (uint32_t)(frames * fb);
    }
}

int STDL_OpenAudio(STDL_AudioSpec *desired, STDL_AudioSpec *obtained)
{
    static const int rates[4] = { 6258, 12517, 25033, 50066 };
    static Uint8 ring[1 << 16];
    int i, best = fake_audio_falcon ? 1 : 0;
    if (fake_audio_open) {
        return -1;
    }
    if (desired->channels < 1 || desired->channels > 2 ||
        (desired->format != STDL_AUDIO_U8 && desired->format != STDL_AUDIO_S8 &&
         desired->format != STDL_AUDIO_S16LSB &&
         desired->format != STDL_AUDIO_S16MSB)) {
        return -1;
    }
    for (i = best; i < 4; i++) {
        if (abs(rates[i] - desired->freq) < abs(rates[best] - desired->freq)) {
            best = i;
        }
    }
    dma_rate = rates[best];
    rate_err = 0;
    fake_audio_spec = *desired;
    if (!fake_audio_spec.samples) {
        fake_audio_spec.samples = 1024;
    }
    fake_audio_spec.silence = desired->format == STDL_AUDIO_U8 ? 0x80 : 0;
    *desired = fake_audio_spec;
    if (obtained) {
        *obtained = fake_audio_spec;
        obtained->freq = dma_rate;
        obtained->format = STDL_AUDIO_S8;
        obtained->silence = 0;
    }
    /* ring.c primes both halves through the callback */
    pull(ring);
    pull(ring);
    fake_audio_open = 1;
    fake_audio_paused = 1;
    return 0;
}

void STDL_CloseAudio(void)
{
    fake_audio_open = 0;
}

void STDL_PauseAudio(int pause_on)
{
    if (fake_audio_open) {
        fake_audio_paused = pause_on != 0;
    }
}

STDL_audiostatus STDL_GetAudioStatus(void)
{
    return !fake_audio_open ? STDL_AUDIO_STOPPED
           : fake_audio_paused ? STDL_AUDIO_PAUSED : STDL_AUDIO_PLAYING;
}

void fake_audio_refill(void)
{
    if (fake_audio_open && !fake_audio_paused) {
        pull(scratch);
    }
}
