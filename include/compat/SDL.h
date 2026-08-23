/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * SDL 1.2 compatibility shim: maps the common SDL surface of a
 * ported game onto STDL so the source stays recognisable. Anything
 * not mapped here is a compile error by design - the porter should
 * find the gaps at build time, not at runtime.
 */

#ifndef STDL_COMPAT_SDL_H
#define STDL_COMPAT_SDL_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdl/stdl.h>

/* ---------------------------------------------------------------- */
/* basic types                                                      */

typedef uint8_t  Uint8;
typedef int8_t   Sint8;
typedef uint16_t Uint16;
typedef int16_t  Sint16;
typedef uint32_t Uint32;
typedef int32_t  Sint32;
typedef uint64_t Uint64;
typedef int64_t  Sint64;

typedef enum { SDL_FALSE = 0, SDL_TRUE = 1 } SDL_bool;

#define SDLCALL
#define SDL_LIL_ENDIAN 1234
#define SDL_BIG_ENDIAN 4321
#define SDL_BYTEORDER  SDL_BIG_ENDIAN

#define SDL_TABLESIZE(table) (sizeof(table) / sizeof((table)[0]))

typedef STDL_Rect        SDL_Rect;
typedef STDL_Colour      SDL_Color;
typedef STDL_Palette     SDL_Palette;
typedef STDL_PixelFormat SDL_PixelFormat;
typedef STDL_Surface     SDL_Surface;
typedef STDL_Event       SDL_Event;
typedef STDL_Keysym      SDL_keysym;
typedef int              SDLKey;
typedef int              SDLMod;

/* ---------------------------------------------------------------- */
/* init / video flags (values match SDL 1.2 and STDL)               */

#define SDL_INIT_TIMER      STDL_INIT_TIMER
#define SDL_INIT_AUDIO      STDL_INIT_AUDIO
#define SDL_INIT_VIDEO      STDL_INIT_VIDEO
#define SDL_INIT_JOYSTICK   STDL_INIT_JOYSTICK
#define SDL_INIT_EVERYTHING STDL_INIT_EVERYTHING

#define SDL_SWSURFACE   0x00000000u
#define SDL_HWSURFACE   0x00000001u
#define SDL_OPENGL      0x00000002u
#define SDL_ASYNCBLIT   0x00000004u
#define SDL_OPENGLBLIT  0x0000000Au
#define SDL_RESIZABLE   0x00000010u
#define SDL_NOFRAME     0x00000020u
#define SDL_HWACCEL     0x00000100u
#define SDL_SRCCOLORKEY 0x00001000u
#define SDL_RLEACCELOK  0x00002000u
#define SDL_RLEACCEL    0x00004000u
#define SDL_SRCALPHA    0x00010000u
#define SDL_PREALLOC    0x01000000u
#define SDL_ANYFORMAT   0x10000000u
#define SDL_HWPALETTE   0x20000000u
#define SDL_DOUBLEBUF   0x40000000u
#define SDL_FULLSCREEN  0x80000000u

#define SDL_LOGPAL  0x01
#define SDL_PHYSPAL 0x02

/* ---------------------------------------------------------------- */
/* events                                                           */

#define SDL_NOEVENT         STDL_NOEVENT
#define SDL_ACTIVEEVENT     STDL_ACTIVEEVENT
#define SDL_KEYDOWN         STDL_KEYDOWN
#define SDL_KEYUP           STDL_KEYUP
#define SDL_MOUSEMOTION     STDL_MOUSEMOTION
#define SDL_MOUSEBUTTONDOWN STDL_MOUSEBUTTONDOWN
#define SDL_MOUSEBUTTONUP   STDL_MOUSEBUTTONUP
#define SDL_JOYAXISMOTION   STDL_JOYAXISMOTION
#define SDL_JOYBUTTONDOWN   STDL_JOYBUTTONDOWN
#define SDL_JOYBUTTONUP     STDL_JOYBUTTONUP
#define SDL_QUIT            STDL_QUIT
#define SDL_VIDEORESIZE     16
#define SDL_VIDEOEXPOSE     17
#define SDL_USEREVENT       STDL_USEREVENT

#define SDL_PRESSED  STDL_PRESSED
#define SDL_RELEASED STDL_RELEASED

#define SDL_BUTTON_LEFT   STDL_BUTTON_LEFT
#define SDL_BUTTON_MIDDLE STDL_BUTTON_MIDDLE
#define SDL_BUTTON_RIGHT  STDL_BUTTON_RIGHT

/* keysyms: STDL uses SDL 1.2's values */
#define SDLK_FIRST     0
#define SDLK_UNKNOWN   STDLK_UNKNOWN
#define SDLK_BACKSPACE STDLK_BACKSPACE
#define SDLK_TAB       STDLK_TAB
#define SDLK_RETURN    STDLK_RETURN
#define SDLK_ESCAPE    STDLK_ESCAPE
#define SDLK_CLEAR     STDLK_CLEAR
#define SDLK_PAUSE     STDLK_PAUSE
#define SDLK_SPACE     STDLK_SPACE
#define SDLK_DELETE    STDLK_DELETE
#define SDLK_EXCLAIM      STDLK_EXCLAIM
#define SDLK_QUOTEDBL     STDLK_QUOTEDBL
#define SDLK_HASH         STDLK_HASH
#define SDLK_DOLLAR       STDLK_DOLLAR
#define SDLK_AMPERSAND    STDLK_AMPERSAND
#define SDLK_QUOTE        STDLK_QUOTE
#define SDLK_LEFTPAREN    STDLK_LEFTPAREN
#define SDLK_RIGHTPAREN   STDLK_RIGHTPAREN
#define SDLK_ASTERISK     STDLK_ASTERISK
#define SDLK_PLUS         STDLK_PLUS
#define SDLK_COMMA        STDLK_COMMA
#define SDLK_MINUS        STDLK_MINUS
#define SDLK_PERIOD       STDLK_PERIOD
#define SDLK_SLASH        STDLK_SLASH
#define SDLK_COLON        STDLK_COLON
#define SDLK_SEMICOLON    STDLK_SEMICOLON
#define SDLK_LESS         STDLK_LESS
#define SDLK_EQUALS       STDLK_EQUALS
#define SDLK_GREATER      STDLK_GREATER
#define SDLK_QUESTION     STDLK_QUESTION
#define SDLK_AT           STDLK_AT
#define SDLK_LEFTBRACKET  STDLK_LEFTBRACKET
#define SDLK_BACKSLASH    STDLK_BACKSLASH
#define SDLK_RIGHTBRACKET STDLK_RIGHTBRACKET
#define SDLK_CARET        STDLK_CARET
#define SDLK_UNDERSCORE   STDLK_UNDERSCORE
#define SDLK_BACKQUOTE    STDLK_BACKQUOTE
#define SDLK_0 STDLK_0
#define SDLK_1 STDLK_1
#define SDLK_2 STDLK_2
#define SDLK_3 STDLK_3
#define SDLK_4 STDLK_4
#define SDLK_5 STDLK_5
#define SDLK_6 STDLK_6
#define SDLK_7 STDLK_7
#define SDLK_8 STDLK_8
#define SDLK_9 STDLK_9
#define SDLK_a STDLK_a
#define SDLK_b STDLK_b
#define SDLK_c STDLK_c
#define SDLK_d STDLK_d
#define SDLK_e STDLK_e
#define SDLK_f STDLK_f
#define SDLK_g STDLK_g
#define SDLK_h STDLK_h
#define SDLK_i STDLK_i
#define SDLK_j STDLK_j
#define SDLK_k STDLK_k
#define SDLK_l STDLK_l
#define SDLK_m STDLK_m
#define SDLK_n STDLK_n
#define SDLK_o STDLK_o
#define SDLK_p STDLK_p
#define SDLK_q STDLK_q
#define SDLK_r STDLK_r
#define SDLK_s STDLK_s
#define SDLK_t STDLK_t
#define SDLK_u STDLK_u
#define SDLK_v STDLK_v
#define SDLK_w STDLK_w
#define SDLK_x STDLK_x
#define SDLK_y STDLK_y
#define SDLK_z STDLK_z
#define SDLK_KP0 STDLK_KP0
#define SDLK_KP1 STDLK_KP1
#define SDLK_KP2 STDLK_KP2
#define SDLK_KP3 STDLK_KP3
#define SDLK_KP4 STDLK_KP4
#define SDLK_KP5 STDLK_KP5
#define SDLK_KP6 STDLK_KP6
#define SDLK_KP7 STDLK_KP7
#define SDLK_KP8 STDLK_KP8
#define SDLK_KP9 STDLK_KP9
#define SDLK_KP_PERIOD   STDLK_KP_PERIOD
#define SDLK_KP_DIVIDE   STDLK_KP_DIVIDE
#define SDLK_KP_MULTIPLY STDLK_KP_MULTIPLY
#define SDLK_KP_MINUS    STDLK_KP_MINUS
#define SDLK_KP_PLUS     STDLK_KP_PLUS
#define SDLK_KP_ENTER    STDLK_KP_ENTER
#define SDLK_UP        STDLK_UP
#define SDLK_DOWN      STDLK_DOWN
#define SDLK_RIGHT     STDLK_RIGHT
#define SDLK_LEFT      STDLK_LEFT
#define SDLK_INSERT    STDLK_INSERT
#define SDLK_HOME      STDLK_HOME
#define SDLK_END       STDLK_END
#define SDLK_PAGEUP    STDLK_PAGEUP
#define SDLK_PAGEDOWN  STDLK_PAGEDOWN
#define SDLK_F1  STDLK_F1
#define SDLK_F2  STDLK_F2
#define SDLK_F3  STDLK_F3
#define SDLK_F4  STDLK_F4
#define SDLK_F5  STDLK_F5
#define SDLK_F6  STDLK_F6
#define SDLK_F7  STDLK_F7
#define SDLK_F8  STDLK_F8
#define SDLK_F9  STDLK_F9
#define SDLK_F10 STDLK_F10
#define SDLK_F11 STDLK_F11
#define SDLK_F12 STDLK_F12
#define SDLK_F13 STDLK_F13
#define SDLK_F14 STDLK_F14
#define SDLK_F15 STDLK_F15
#define SDLK_NUMLOCK   STDLK_NUMLOCK
#define SDLK_CAPSLOCK  STDLK_CAPSLOCK
#define SDLK_SCROLLOCK STDLK_SCROLLOCK
#define SDLK_RSHIFT    STDLK_RSHIFT
#define SDLK_LSHIFT    STDLK_LSHIFT
#define SDLK_RCTRL     STDLK_RCTRL
#define SDLK_LCTRL     STDLK_LCTRL
#define SDLK_RALT      STDLK_RALT
#define SDLK_LALT      STDLK_LALT
#define SDLK_RMETA     STDLK_RMETA
#define SDLK_LMETA     STDLK_LMETA
#define SDLK_HELP      STDLK_HELP
#define SDLK_PRINT     STDLK_PRINT
#define SDLK_UNDO      STDLK_UNDO
#define SDLK_LAST      STDLK_LAST

#define KMOD_NONE   STDL_KMOD_NONE
#define KMOD_LSHIFT STDL_KMOD_LSHIFT
#define KMOD_RSHIFT STDL_KMOD_RSHIFT
#define KMOD_LCTRL  STDL_KMOD_LCTRL
#define KMOD_RCTRL  STDL_KMOD_RCTRL
#define KMOD_LALT   STDL_KMOD_LALT
#define KMOD_RALT   STDL_KMOD_RALT
#define KMOD_LMETA  0x0400
#define KMOD_RMETA  0x0800
#define KMOD_NUM    STDL_KMOD_NUM
#define KMOD_CAPS   STDL_KMOD_CAPS
#define KMOD_MODE   STDL_KMOD_MODE
#define KMOD_CTRL   STDL_KMOD_CTRL
#define KMOD_SHIFT  STDL_KMOD_SHIFT
#define KMOD_ALT    STDL_KMOD_ALT
/* STDL extension: the event was synthesised from the joystick
 * (STDL_JoyKeyEmulation), so a game can give e.g. joystick-up its
 * own meaning without affecting the cursor key */
#define KMOD_JOYSTICK STDL_KMOD_JOYSTICK

#define SDL_DEFAULT_REPEAT_DELAY    STDL_DEFAULT_REPEAT_DELAY
#define SDL_DEFAULT_REPEAT_INTERVAL STDL_DEFAULT_REPEAT_INTERVAL

/* ---------------------------------------------------------------- */
/* video info                                                       */

typedef struct {
    uint32_t hw_available : 1;
    uint32_t wm_available : 1;
    uint32_t UnusedBits1  : 6;
    uint32_t UnusedBits2  : 1;
    uint32_t blit_hw      : 1;
    uint32_t blit_hw_CC   : 1;
    uint32_t blit_hw_A    : 1;
    uint32_t blit_sw      : 1;
    uint32_t blit_sw_CC   : 1;
    uint32_t blit_sw_A    : 1;
    uint32_t blit_fill    : 1;
    uint32_t UnusedBits3  : 16;
    uint32_t video_mem;
    SDL_PixelFormat *vfmt;
    int current_w;
    int current_h;
} SDL_VideoInfo;

/* ---------------------------------------------------------------- */
/* API mapping                                                      */

#define SDL_Init(flags)        STDL_Init(flags)
#define SDL_InitSubSystem(f)   STDL_Init(f)
#define SDL_Quit               STDL_Quit
#define SDL_GetError()         ((char *)STDL_GetError())

#define SDL_SetVideoMode       STDL_SetVideoMode
#define SDL_GetVideoSurface    STDL_GetVideoSurface
#define SDL_UpdateRects(s, n, r) STDL_UpdateRects((n), (r))
#define SDL_UpdateRect(s, x, y, w, h) ((void)(s))

#define SDL_LockSurface(s)     ((void)(s), 0)
#define SDL_UnlockSurface(s)   ((void)(s))
#define SDL_MUSTLOCK(s)        (0)

#define SDL_FreeSurface        STDL_FreeSurface
#define SDL_BlitSurface        STDL_BlitSurface
#define SDL_SetColors(s, c, f, n) STDL_SetColours((s), (c), (f), (n))
#define SDL_MapRGB(fmt, r, g, b) ((Uint32)STDL_MapRGB((fmt), (r), (g), (b)))
#define SDL_GetRGB(p, fmt, r, g, b) STDL_GetRGB((p), (fmt), (r), (g), (b))

#define SDL_PollEvent          STDL_PollEvent
#define SDL_WaitEvent          STDL_WaitEvent
#define SDL_PushEvent(e)       STDL_PushEvent(e)
#define SDL_PumpEvents         STDL_PumpEvents
#define SDL_GetKeyState        STDL_GetKeyState
#define SDL_GetModState()      ((SDLMod)STDL_GetModState())
#define SDL_GetKeyName(k)      ((char *)STDL_GetKeyName((uint16_t)(k)))
#define SDL_EnableUNICODE      STDL_EnableUNICODE
#define SDL_EnableKeyRepeat    STDL_EnableKeyRepeat
#define SDL_GetMouseState      STDL_GetMouseState
#define SDL_WarpMouse(x, y)    STDL_WarpMouse((uint16_t)(x), (uint16_t)(y))

#define SDL_GetTicks           STDL_GetTicks

/* string/memory helpers ported code leans on */
#define SDL_snprintf snprintf
#define SDL_memset   memset
#define SDL_memcpy   memcpy
#define SDL_malloc   malloc
#define SDL_free     free

/* window-manager stubs: always fullscreen, no cursor drawn */
#define SDL_WM_SetCaption(title, icon) ((void)0)
#define SDL_WM_ToggleFullScreen(s)     ((void)(s), 1)
#define SDL_QuitSubSystem(flags)       ((void)(flags))

/* software cursor (see stdl_cursor.h for the save-under caveats) */
typedef STDL_Cursor SDL_Cursor;
#define SDL_CreateCursor(data, mask, w, h, hx, hy) \
        STDL_CreateCursor((data), (mask), (w), (h), (hx), (hy))
#define SDL_SetCursor  STDL_SetCursor
#define SDL_GetCursor  STDL_GetCursor
#define SDL_FreeCursor STDL_FreeCursor
#define SDL_ShowCursor STDL_ShowCursorCtl
#define SDL_ENABLE  1
#define SDL_DISABLE 0

/* ---------------------------------------------------------------- */
/* joystick: the physical joystick port (port 1) as stick 0         */

typedef struct SDL_Joystick SDL_Joystick;
int  SDL_NumJoysticks(void);
const char *SDL_JoystickName(int index);
SDL_Joystick *SDL_JoystickOpen(int index);
void SDL_JoystickClose(SDL_Joystick *joystick);
int  SDL_JoystickIndex(SDL_Joystick *joystick);
int  SDL_JoystickOpened(int index);
int  SDL_JoystickNumAxes(SDL_Joystick *joystick);
int  SDL_JoystickNumBalls(SDL_Joystick *joystick);
int  SDL_JoystickNumHats(SDL_Joystick *joystick);
int  SDL_JoystickNumButtons(SDL_Joystick *joystick);
Sint16 SDL_JoystickGetAxis(SDL_Joystick *joystick, int axis);
Uint8  SDL_JoystickGetButton(SDL_Joystick *joystick, int button);
Uint8  SDL_JoystickGetHat(SDL_Joystick *joystick, int hat);
void SDL_JoystickUpdate(void);
int  SDL_JoystickEventState(int state);

#define SDL_HAT_CENTERED 0x00
#define SDL_HAT_UP       0x01
#define SDL_HAT_RIGHT    0x02
#define SDL_HAT_DOWN     0x04
#define SDL_HAT_LEFT     0x08

/* ---------------------------------------------------------------- */
/* audio: STE/Mega STE DMA playback (see stdl_audio.h)              */

typedef STDL_AudioSpec SDL_AudioSpec;
#define AUDIO_U8     STDL_AUDIO_U8
#define AUDIO_S8     STDL_AUDIO_S8
#define AUDIO_S16LSB STDL_AUDIO_S16LSB
#define AUDIO_S16MSB STDL_AUDIO_S16MSB
#define AUDIO_S16    STDL_AUDIO_S16LSB
#define AUDIO_S16SYS STDL_AUDIO_S16SYS

typedef enum {
    SDL_AUDIO_STOPPED = STDL_AUDIO_STOPPED,
    SDL_AUDIO_PLAYING = STDL_AUDIO_PLAYING,
    SDL_AUDIO_PAUSED  = STDL_AUDIO_PAUSED
} SDL_audiostatus;

#define SDL_OpenAudio(desired, obtained) \
        STDL_OpenAudio((desired), (obtained))
#define SDL_CloseAudio    STDL_CloseAudio
#define SDL_PauseAudio    STDL_PauseAudio
#define SDL_GetAudioStatus() ((SDL_audiostatus)STDL_GetAudioStatus())
#define SDL_LoadWAV(file, spec, buf, len) \
        STDL_LoadWAV((file), (spec), (buf), (len))
#define SDL_FreeWAV       STDL_FreeWAV
#define SDL_LockAudio()   ((void)0)
#define SDL_UnlockAudio() ((void)0)
char *SDL_AudioDriverName(char *namebuf, int maxlen);

/* documented non-goals; fail cleanly (see docs/limits.md) */
int SDL_SetAlpha(SDL_Surface *surface, Uint32 flags, Uint8 alpha);
int SDL_SaveBMP(SDL_Surface *surface, const char *file);
int SDL_SetGamma(float r, float g, float b);
int SDL_SetGammaRamp(const Uint16 *r, const Uint16 *g, const Uint16 *b);

#define SDL_LoadBMP STDL_LoadBMP

int  SDL_Flip(SDL_Surface *screen);
int  SDL_FillRect(SDL_Surface *dst, SDL_Rect *dstrect, Uint32 color);
SDL_Surface *SDL_CreateRGBSurface(Uint32 flags, int width, int height,
                                  int depth, Uint32 Rmask, Uint32 Gmask,
                                  Uint32 Bmask, Uint32 Amask);
SDL_Surface *SDL_DisplayFormat(SDL_Surface *surface);
int  SDL_SetColorKey(SDL_Surface *surface, Uint32 flag, Uint32 key);
int  SDL_SetPalette(SDL_Surface *surface, int flags, SDL_Color *colors,
                    int firstcolor, int ncolors);
const SDL_VideoInfo *SDL_GetVideoInfo(void);
SDL_Rect **SDL_ListModes(SDL_PixelFormat *format, Uint32 flags);
int  SDL_VideoModeOK(int width, int height, int bpp, Uint32 flags);
char *SDL_VideoDriverName(char *namebuf, int maxlen);
void SDL_Delay(Uint32 ms);

/* cooperative timers: fire inside SDL_Delay / SDL_PollEvent */
typedef Uint32 (*SDL_NewTimerCallback)(Uint32 interval, void *param);
typedef Uint32 (*SDL_TimerCallback)(Uint32 interval);
typedef void *SDL_TimerID;
int  SDL_SetTimer(Uint32 interval, SDL_TimerCallback callback);
SDL_TimerID SDL_AddTimer(Uint32 interval, SDL_NewTimerCallback callback,
                         void *param);
SDL_bool SDL_RemoveTimer(SDL_TimerID id);

#ifdef __cplusplus
}
#endif

#endif /* STDL_COMPAT_SDL_H */
