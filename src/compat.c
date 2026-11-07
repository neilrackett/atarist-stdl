/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * SDL 1.2 compatibility functions that need more than a macro.
 */

#include "stdl_internal.h"
#include "stdl_xpad.h"
#include <SDL.h>

/* ---------------------------------------------------------------- */
/* video                                                            */

int SDL_Flip(SDL_Surface *screen)
{
    (void)screen;
    STDL_Flip();
    return 0;
}

/* STDL_FillRect implements SDL 1.2's clip-and-write-back contract
 * (including origin translation) natively */
int SDL_FillRect(SDL_Surface *dst, SDL_Rect *dstrect, Uint32 color)
{
    if (dst == NULL) {
        return -1;
    }
    STDL_FillRect(dst, dstrect, (uint8_t)color);
    return 0;
}

SDL_Surface *SDL_CreateRGBSurface(Uint32 flags, int width, int height,
                                  int depth, Uint32 Rmask, Uint32 Gmask,
                                  Uint32 Bmask, Uint32 Amask)
{
    (void)flags; (void)depth;
    (void)Rmask; (void)Gmask; (void)Bmask; (void)Amask;
    return STDL_CreateSurface(width, height);
}

SDL_Surface *SDL_DisplayFormat(SDL_Surface *surface)
{
    SDL_Surface *d = STDL_DuplicateSurface(surface);

    if (d != NULL) {
        /* already planar: mark it fast so ported speed heuristics
         * pick the blit path instead of direct pixel access */
        d->flags |= STDL_HWACCEL;
    }
    return d;
}

int SDL_SetColorKey(SDL_Surface *surface, Uint32 flag, Uint32 key)
{
    return STDL_SetColourKey(surface,
                             (flag & SDL_SRCCOLORKEY) ? 1 : 0,
                             (uint8_t)key);
}

/*
 * SDL_LOGPAL updates the logical palette (blit mapping, MapRGB);
 * SDL_PHYSPAL programs the hardware registers without touching the
 * logical palette - which is exactly how palette fades avoid
 * disturbing the drawing colours.
 */
int SDL_SetPalette(SDL_Surface *surface, int flags, SDL_Color *colors,
                   int firstcolor, int ncolors)
{
    int i;

    if (surface == NULL || colors == NULL) {
        return 0;
    }
    if (flags & SDL_LOGPAL) {
        STDL_Palette *pal = surface->format ? surface->format->palette
                                            : NULL;
        if (pal != NULL) {
            for (i = 0; i < ncolors
                 && firstcolor + i < pal->ncolors; i++) {
                pal->colors[firstcolor + i] = colors[i];
            }
        }
    }
    /* SDL 1.2 semantics: only SDL_PHYSPAL touches the registers -
     * the LOGPAL/PHYSPAL split is exactly what palette fades rely
     * on (SDL_SetColors maps to LOGPAL | PHYSPAL) */
    if ((flags & SDL_PHYSPAL) && (surface->flags & STDL_SCREEN)) {
        for (i = 0; i < ncolors && firstcolor + i < 16; i++) {
            const uint16_t w = STDL_HWColour(colors[i].r, colors[i].g,
                                             colors[i].b);

            /* staged with the rest when STDL_StagePalette (or an
             * open border) is staging; STDL_SetColour never is */
            if (stdl_pal_raw_hook == NULL
                || !stdl_pal_raw_hook(firstcolor + i, w)) {
                STDL_SetColour(firstcolor + i, w);
            }
        }
    }
    return 1;
}

/* ---------------------------------------------------------------- */
/* video info                                                       */

static SDL_VideoInfo videoinfo;
static SDL_Rect mode_rect = { 0, 0, 320, 200 };
static SDL_Rect *mode_list[2] = { &mode_rect, NULL };

const SDL_VideoInfo *SDL_GetVideoInfo(void)
{
    STDL_Surface *screen = STDL_GetVideoSurface();

    memset(&videoinfo, 0, sizeof(videoinfo));
    videoinfo.hw_available = 1;
    videoinfo.wm_available = 0;
    videoinfo.blit_hw = STDL_GetMachineInfo()->has_blitter;
    videoinfo.blit_hw_CC = STDL_GetMachineInfo()->has_blitter;
    videoinfo.blit_fill = STDL_GetMachineInfo()->has_blitter;
    videoinfo.video_mem = 32;           /* one 32K frame */
    videoinfo.current_w = 320;
    videoinfo.current_h = 200;
    videoinfo.vfmt = (screen != NULL) ? screen->format : NULL;
    return &videoinfo;
}

SDL_Rect **SDL_ListModes(SDL_PixelFormat *format, Uint32 flags)
{
    (void)format; (void)flags;
    return mode_list;
}

int SDL_VideoModeOK(int width, int height, int bpp, Uint32 flags)
{
    (void)width; (void)height; (void)bpp; (void)flags;
    return 4;                           /* everything becomes 320x200x4 */
}

char *SDL_VideoDriverName(char *namebuf, int maxlen)
{
    if (namebuf == NULL || maxlen < 1) {
        return NULL;
    }
    strncpy(namebuf, "stdl", (size_t)maxlen - 1);
    namebuf[maxlen - 1] = '\0';
    return namebuf;
}

/* ---------------------------------------------------------------- */
/* joystick: STDL's joysticks as SDL's, by the same numbers. Stick 0 is
 * the physical joystick port (port 1) with pad 0, its state from the
 * IKBD packets the event module already parses; this is a pure veneer.
 * Sticks 1-5 are there only where the program asked for them
 * (STDL_SetNumJoysticks), and are read from the state the pump keeps
 * for them, stdl_joyn, so a port that never asks links none of that
 * code. */

static int joystick_open[STDL_MAX_JOYSTICKS];

/* a handle's stick: anything not one of ours is stick 0, which every
 * handle was when there was one */
static int joy_index(SDL_Joystick *joystick)
{
    const int *p = (const int *)joystick;

    if (p > joystick_open && p < joystick_open + STDL_MAX_JOYSTICKS) {
        return (int)(p - joystick_open);
    }
    return 0;
}

/* a stick's pad record: joystick 0's, or joysticks.c's, which is
 * there for every stick a handle can name */
static const stdl_pad_t *pad_of(int index)
{
    return index ? &stdl_joyn->pads[index] : &stdl_pad0;
}

int SDL_NumJoysticks(void)
{
    return STDL_NumJoysticks();
}

const char *SDL_JoystickName(int index)
{
    static const char *const ports[2] = {
        "Atari ST joystick port 1", "Atari ST joystick port 0"
    };
    static const char *const pads[4] = {
        "Xpad pad 0", "Xpad pad 1", "Xpad pad 2", "Xpad pad 3"
    };

    if (index < 0 || index >= STDL_NumJoysticks()) {
        return NULL;
    }
    /* sticks 2-5 are pads alone, pad 2 and 3 merged or 0 to 3 apart
     * (STDL_UseJoystickMerge): the slot from its record */
    return index < 2 ? ports[index] : pads[pad_of(index)->slot & 3];
}

SDL_Joystick *SDL_JoystickOpen(int index)
{
    if (index < 0 || index >= STDL_NumJoysticks()) {
        STDL_SetError("no such joystick");
        return NULL;
    }
    joystick_open[index] = 1;
    return (SDL_Joystick *)&joystick_open[index];
}

void SDL_JoystickClose(SDL_Joystick *joystick)
{
    joystick_open[joy_index(joystick)] = 0;
}

int SDL_JoystickIndex(SDL_Joystick *joystick)
{
    return joy_index(joystick);
}

int SDL_JoystickOpened(int index)
{
    return (index >= 0 && index < STDL_MAX_JOYSTICKS)
        ? joystick_open[index] : 0;
}

int SDL_JoystickNumAxes(SDL_Joystick *joystick)
{
    /* Two for a plain stick, six when a pad is there: both sticks and
     * the two triggers. Reported only when a provider is present, so a
     * port sees the geometry it would have seen before unless the
     * hardware is actually richer, which is how desktop SDL behaves
     * when a pad is plugged in. Each stick asks its own slot. */
    return STDL_HasPad(joy_index(joystick)) ? STDL_XPAD_AXES : 2;
}

int SDL_JoystickNumBalls(SDL_Joystick *joystick)
{
    (void)joystick;
    return 0;
}

int SDL_JoystickNumHats(SDL_Joystick *joystick)
{
    /* A d-pad is a hat. An ST joystick is not: its directions are the
     * axes, and reporting a hat as well would double every press. */
    return STDL_HasPad(joy_index(joystick)) ? 1 : 0;
}

int SDL_JoystickNumButtons(SDL_Joystick *joystick)
{
    return STDL_HasPad(joy_index(joystick)) ? STDL_XPAD_BUTTONS : 1;
}

/* the core's readers, as the events carry them */
Sint16 SDL_JoystickGetAxis(SDL_Joystick *joystick, int axis)
{
    return STDL_GetJoystickAxis(joy_index(joystick), axis);
}

Uint8 SDL_JoystickGetButton(SDL_Joystick *joystick, int button)
{
    /* Button 0 is fire from either source, the port or the pad; the
     * rest are the pad's, STDL's inputs from fire on. */
    return (button >= 0 && button < STDL_XPAD_BUTTONS
            && STDL_JoystickInputHeld(joy_index(joystick),
                                      STDL_JOYSTICK_FIRE + button))
        ? SDL_PRESSED : SDL_RELEASED;
}

Uint8 SDL_JoystickGetHat(SDL_Joystick *joystick, int hat)
{
    return hat == 0 ? STDL_GetJoystickHat(joy_index(joystick)) : 0;
}

void SDL_JoystickUpdate(void)
{
    STDL_PumpEvents();
}

int SDL_JoystickEventState(int state)
{
    (void)state;
    return SDL_ENABLE;      /* joystick events are always delivered */
}

/* ---------------------------------------------------------------- */

char *SDL_AudioDriverName(char *namebuf, int maxlen)
{
    if (namebuf == NULL || maxlen < 1) {
        return NULL;
    }
    strncpy(namebuf, "stdl-dma", (size_t)maxlen - 1);
    namebuf[maxlen - 1] = '\0';
    return namebuf;
}

/* documented non-goals (docs/limits.md): fail cleanly */
int SDL_SetAlpha(SDL_Surface *surface, Uint32 flags, Uint8 alpha)
{
    (void)surface; (void)flags; (void)alpha;
    STDL_SetError("alpha blending is a documented non-goal");
    return -1;
}

int SDL_SaveBMP(SDL_Surface *surface, const char *file)
{
    (void)surface; (void)file;
    STDL_SetError("SDL_SaveBMP not implemented");
    return -1;
}

int SDL_SetGamma(float r, float g, float b)
{
    (void)r; (void)g; (void)b;
    STDL_SetError("gamma is a documented non-goal");
    return -1;
}

int SDL_SetGammaRamp(const Uint16 *r, const Uint16 *g, const Uint16 *b)
{
    (void)r; (void)g; (void)b;
    STDL_SetError("gamma is a documented non-goal");
    return -1;
}

/* ---------------------------------------------------------------- */
/* cooperative timers                                               */
/*
 * There are no threads: callbacks fire inside SDL_Delay (and only
 * there), which is where the SDL 1.2 test programs sit while they
 * expect timers to run. Ports needing finer timing should call the
 * 200Hz counter directly via STDL_GetTicks.
 */

#define MAX_TIMERS 8

typedef struct {
    int active;
    int legacy;                 /* SDL_SetTimer-style callback */
    Uint32 interval;
    Uint32 next_fire;
    SDL_NewTimerCallback callback;
    SDL_TimerCallback legacy_callback;
    void *param;
} compat_timer_t;

static compat_timer_t timers[MAX_TIMERS];

static void run_timers(void)
{
    Uint32 now = STDL_GetTicks();
    int i;

    for (i = 0; i < MAX_TIMERS; i++) {
        compat_timer_t *t = &timers[i];
        if (!t->active || (int32_t)(now - t->next_fire) < 0) {
            continue;
        }
        {
            Uint32 next = t->legacy
                ? t->legacy_callback(t->interval)
                : t->callback(t->interval, t->param);
            if (next == 0) {
                t->active = 0;
            } else {
                t->interval = next;
                t->next_fire = now + next;
            }
        }
    }
}

/* the native delay services the timer/audio hooks itself */
void SDL_Delay(Uint32 ms)
{
    STDL_Delay(ms);
}

SDL_TimerID SDL_AddTimer(Uint32 interval, SDL_NewTimerCallback callback,
                         void *param)
{
    int i;

    if (callback == NULL || interval == 0) {
        return NULL;
    }
    for (i = 1; i < MAX_TIMERS; i++) {
        if (!timers[i].active) {
            timers[i].active = 1;
            timers[i].legacy = 0;
            timers[i].interval = interval;
            timers[i].next_fire = STDL_GetTicks() + interval;
            timers[i].callback = callback;
            timers[i].param = param;
            stdl_timer_hook = run_timers;
            return (SDL_TimerID)&timers[i];
        }
    }
    STDL_SetError("too many timers");
    return NULL;
}

SDL_bool SDL_RemoveTimer(SDL_TimerID id)
{
    compat_timer_t *t = (compat_timer_t *)id;

    if (t == NULL || t < timers || t >= timers + MAX_TIMERS
        || !t->active) {
        return SDL_FALSE;
    }
    t->active = 0;
    return SDL_TRUE;
}

/* legacy single-timer interface; slot 0 is reserved for it */
int SDL_SetTimer(Uint32 interval, SDL_TimerCallback callback)
{
    timers[0].active = 0;
    if (interval != 0 && callback != NULL) {
        timers[0].active = 1;
        timers[0].legacy = 1;
        timers[0].interval = interval;
        timers[0].next_fire = STDL_GetTicks() + interval;
        timers[0].legacy_callback = callback;
        stdl_timer_hook = run_timers;
    }
    return 0;
}
