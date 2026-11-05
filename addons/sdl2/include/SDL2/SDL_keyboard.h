/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * The SDL2 add-on's keyboard: STDL's keys as SDL2's keysyms, the state
 * of every key by scancode, and text input as SDL_TEXTINPUT events of
 * one character each (STDL's translation, ASCII).
 */

#ifndef STDL_SDL2_KEYBOARD_H
#define STDL_SDL2_KEYBOARD_H

#include "SDL_stdinc.h"
#include "SDL_keycode.h"
#include "SDL_rect.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct SDL_Keysym {
    SDL_Scancode scancode;
    SDL_Keycode sym;
    Uint16 mod;
    Uint32 unused;
} SDL_Keysym;

#define SDL_GetKeyboardState    stdl_sdl2_GetKeyboardState
#define SDL_GetModState         stdl_sdl2_GetModState
#define SDL_StartTextInput      stdl_sdl2_StartTextInput
#define SDL_StopTextInput       stdl_sdl2_StopTextInput
#define SDL_IsTextInputActive   stdl_sdl2_IsTextInputActive
#define SDL_GetScancodeFromKey  stdl_sdl2_GetScancodeFromKey
#define SDL_GetKeyFromScancode  stdl_sdl2_GetKeyFromScancode

/* SDL_NUM_SCANCODES entries, 1 while the key is down, as of the last
 * pump */
extern const Uint8 *SDL_GetKeyboardState(int *numkeys);
extern SDL_Keymod SDL_GetModState(void);
/* on until stopped, as SDL2 has it where there is no on-screen
 * keyboard */
extern void SDL_StartTextInput(void);
extern void SDL_StopTextInput(void);
extern SDL_bool SDL_IsTextInputActive(void);
extern SDL_Scancode SDL_GetScancodeFromKey(SDL_Keycode key);
extern SDL_Keycode SDL_GetKeyFromScancode(SDL_Scancode scancode);

#ifdef __cplusplus
}
#endif

#endif /* STDL_SDL2_KEYBOARD_H */
