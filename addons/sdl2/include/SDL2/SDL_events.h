/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * The SDL2 add-on's events, SDL2's types and layouts: STDL's events
 * translated as they are pumped - keys (with SDL2's repeat flag and
 * text input), the mouse in the renderer's logical coordinates, a
 * joystick once opened, a game controller once opened, quit - and the
 * program's own. STDL's key repeat and character translation are
 * turned on by the first pump, so a program that reads STDL's events
 * itself and never pumps here sees no change. The pump is where STDL's
 * timers and audio are serviced too, so a program pumps at least every
 * 150ms, as SDL2 programs do once a frame.
 */

#ifndef STDL_SDL2_EVENTS_H
#define STDL_SDL2_EVENTS_H

#include "SDL_stdinc.h"
#include "SDL_keyboard.h"
#include "SDL_joystick.h"
#include "SDL_gamecontroller.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SDL_RELEASED 0
#define SDL_PRESSED  1

typedef enum {
    SDL_FIRSTEVENT = 0,
    SDL_QUIT = 0x100,
    SDL_WINDOWEVENT = 0x200,    /* never sent: there are no windows */
    SDL_SYSWMEVENT,
    SDL_KEYDOWN = 0x300,
    SDL_KEYUP,
    SDL_TEXTEDITING,
    SDL_TEXTINPUT,
    SDL_MOUSEMOTION = 0x400,
    SDL_MOUSEBUTTONDOWN,
    SDL_MOUSEBUTTONUP,
    SDL_MOUSEWHEEL,
    SDL_JOYAXISMOTION = 0x600,
    SDL_JOYBALLMOTION,
    SDL_JOYHATMOTION,
    SDL_JOYBUTTONDOWN,
    SDL_JOYBUTTONUP,
    SDL_JOYDEVICEADDED,
    SDL_JOYDEVICEREMOVED,
    SDL_CONTROLLERAXISMOTION = 0x650,
    SDL_CONTROLLERBUTTONDOWN,
    SDL_CONTROLLERBUTTONUP,
    SDL_CONTROLLERDEVICEADDED,
    SDL_CONTROLLERDEVICEREMOVED,
    SDL_CONTROLLERDEVICEREMAPPED,
    SDL_USEREVENT = 0x8000,
    SDL_LASTEVENT = 0xFFFF
} SDL_EventType;

typedef struct SDL_CommonEvent {
    Uint32 type;
    Uint32 timestamp;
} SDL_CommonEvent;

typedef struct SDL_WindowEvent {
    Uint32 type;
    Uint32 timestamp;
    Uint32 windowID;
    Uint8 event;
    Uint8 padding1, padding2, padding3;
    Sint32 data1, data2;
} SDL_WindowEvent;

typedef struct SDL_KeyboardEvent {
    Uint32 type;
    Uint32 timestamp;
    Uint32 windowID;
    Uint8 state;
    Uint8 repeat;
    Uint8 padding2, padding3;
    SDL_Keysym keysym;
} SDL_KeyboardEvent;

#define SDL_TEXTINPUTEVENT_TEXT_SIZE 32

typedef struct SDL_TextInputEvent {
    Uint32 type;
    Uint32 timestamp;
    Uint32 windowID;
    char text[SDL_TEXTINPUTEVENT_TEXT_SIZE];
} SDL_TextInputEvent;

typedef struct SDL_MouseMotionEvent {
    Uint32 type;
    Uint32 timestamp;
    Uint32 windowID;
    Uint32 which;
    Uint32 state;
    Sint32 x, y;
    Sint32 xrel, yrel;
} SDL_MouseMotionEvent;

typedef struct SDL_MouseButtonEvent {
    Uint32 type;
    Uint32 timestamp;
    Uint32 windowID;
    Uint32 which;
    Uint8 button;
    Uint8 state;
    Uint8 clicks;
    Uint8 padding1;
    Sint32 x, y;
} SDL_MouseButtonEvent;

/* There is no wheel: the type and its event are here so a program's
 * handler compiles, and none is ever sent. */
typedef enum {
    SDL_MOUSEWHEEL_NORMAL,
    SDL_MOUSEWHEEL_FLIPPED
} SDL_MouseWheelDirection;

typedef struct SDL_MouseWheelEvent {
    Uint32 type;
    Uint32 timestamp;
    Uint32 windowID;
    Uint32 which;
    Sint32 x, y;
    Uint32 direction;
} SDL_MouseWheelEvent;

typedef struct SDL_JoyAxisEvent {
    Uint32 type;
    Uint32 timestamp;
    SDL_JoystickID which;
    Uint8 axis;
    Uint8 padding1, padding2, padding3;
    Sint16 value;
    Uint16 padding4;
} SDL_JoyAxisEvent;

typedef struct SDL_JoyHatEvent {
    Uint32 type;
    Uint32 timestamp;
    SDL_JoystickID which;
    Uint8 hat;
    Uint8 value;
    Uint8 padding1, padding2;
} SDL_JoyHatEvent;

typedef struct SDL_JoyButtonEvent {
    Uint32 type;
    Uint32 timestamp;
    SDL_JoystickID which;
    Uint8 button;
    Uint8 state;
    Uint8 padding1, padding2;
} SDL_JoyButtonEvent;

/* sent by the pump for each joystick STDL has, the first pump's and
 * any the program asks STDL for later (STDL_SetNumJoysticks); REMOVED
 * for each it gives up. `which` is the index, which is also the
 * instance ID. */
typedef struct SDL_JoyDeviceEvent {
    Uint32 type;
    Uint32 timestamp;
    Sint32 which;
} SDL_JoyDeviceEvent;

typedef struct SDL_ControllerDeviceEvent {
    Uint32 type;
    Uint32 timestamp;
    Sint32 which;
} SDL_ControllerDeviceEvent;

typedef struct SDL_ControllerAxisEvent {
    Uint32 type;
    Uint32 timestamp;
    SDL_JoystickID which;
    Uint8 axis;
    Uint8 padding1, padding2, padding3;
    Sint16 value;
    Uint16 padding4;
} SDL_ControllerAxisEvent;

typedef struct SDL_ControllerButtonEvent {
    Uint32 type;
    Uint32 timestamp;
    SDL_JoystickID which;
    Uint8 button;
    Uint8 state;
    Uint8 padding1, padding2;
} SDL_ControllerButtonEvent;

typedef struct SDL_QuitEvent {
    Uint32 type;
    Uint32 timestamp;
} SDL_QuitEvent;

typedef struct SDL_UserEvent {
    Uint32 type;
    Uint32 timestamp;
    Uint32 windowID;
    Sint32 code;
    void *data1;
    void *data2;
} SDL_UserEvent;

typedef union SDL_Event {
    Uint32 type;
    SDL_CommonEvent common;
    SDL_WindowEvent window;
    SDL_KeyboardEvent key;
    SDL_TextInputEvent text;
    SDL_MouseMotionEvent motion;
    SDL_MouseButtonEvent button;
    SDL_MouseWheelEvent wheel;
    SDL_JoyAxisEvent jaxis;
    SDL_JoyHatEvent jhat;
    SDL_JoyButtonEvent jbutton;
    SDL_JoyDeviceEvent jdevice;
    SDL_ControllerAxisEvent caxis;
    SDL_ControllerDeviceEvent cdevice;
    SDL_ControllerButtonEvent cbutton;
    SDL_QuitEvent quit;
    SDL_UserEvent user;
    Uint8 padding[56];
} SDL_Event;

typedef enum
{
    SDL_ADDEVENT,
    SDL_PEEKEVENT,
    SDL_GETEVENT
} SDL_eventaction;

#define SDL_QUERY   (-1)
#define SDL_IGNORE  0
#define SDL_DISABLE 0
#define SDL_ENABLE  1

#define SDL_PumpEvents        stdl_sdl2_PumpEvents
#define SDL_PollEvent         stdl_sdl2_PollEvent
#define SDL_WaitEvent         stdl_sdl2_WaitEvent
#define SDL_WaitEventTimeout  stdl_sdl2_WaitEventTimeout
#define SDL_PushEvent         stdl_sdl2_PushEvent
#define SDL_FlushEvent        stdl_sdl2_FlushEvent
#define SDL_FlushEvents       stdl_sdl2_FlushEvents
#define SDL_EventState        stdl_sdl2_EventState
#define SDL_RegisterEvents    stdl_sdl2_RegisterEvents
#define SDL_PeepEvents        stdl_sdl2_PeepEvents

extern void SDL_PumpEvents(void);
extern int SDL_PollEvent(SDL_Event *event);
extern int SDL_WaitEvent(SDL_Event *event);
extern int SDL_WaitEventTimeout(SDL_Event *event, int timeout);
/* 1, or 0 when the event's type is ignored, or -1 when the queue is
 * full (64) */
extern int SDL_PushEvent(SDL_Event *event);
extern void SDL_FlushEvent(Uint32 type);
extern void SDL_FlushEvents(Uint32 minType, Uint32 maxType);
extern Uint8 SDL_EventState(Uint32 type, int state);
extern Uint32 SDL_RegisterEvents(int numevents);
/* The queue as it is, with no pump, as SDL2's: up to numevents of the
 * types minType to maxType looked at (SDL_PEEKEVENT) or taken
 * (SDL_GETEVENT) from its head, or events added (SDL_ADDEVENT, as
 * SDL_PushEvent adds them). The number, or -1. */
extern int SDL_PeepEvents(SDL_Event *events, int numevents,
                          SDL_eventaction action, Uint32 minType,
                          Uint32 maxType);

#define SDL_GetEventState(type) SDL_EventState(type, SDL_QUERY)

#ifdef __cplusplus
}
#endif

#endif /* STDL_SDL2_EVENTS_H */
