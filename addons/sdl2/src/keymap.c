/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * STDL's key symbols (SDL 1.2's values) as SDL2's scancodes and key
 * codes. A symbol that types a character keeps it as its key code, as
 * SDL2's do; the scancode is where SDL2 puts that key on a USB board,
 * which an Atari keyboard's layout matches. Its extra keys have SDL2
 * scancodes of their own: Help, Undo, and the keypad's brackets, which
 * STDL sends as ( and ) (the main keyboard's are Shift and a digit).
 */

#include "internal.h"

/* STDL_Key (0-322) to SDL_Scancode; 0 where SDL2 has nothing */
static const Uint8 sym_scan[STDLK_LAST] = {
    [STDLK_BACKSPACE] = SDL_SCANCODE_BACKSPACE,
    [STDLK_TAB] = SDL_SCANCODE_TAB,
    [STDLK_CLEAR] = SDL_SCANCODE_CLEAR,
    [STDLK_RETURN] = SDL_SCANCODE_RETURN,
    [STDLK_PAUSE] = SDL_SCANCODE_PAUSE,
    [STDLK_ESCAPE] = SDL_SCANCODE_ESCAPE,
    [STDLK_SPACE] = SDL_SCANCODE_SPACE,
    [STDLK_HASH] = SDL_SCANCODE_NONUSHASH,
    [STDLK_QUOTE] = SDL_SCANCODE_APOSTROPHE,
    [STDLK_LEFTPAREN] = SDL_SCANCODE_KP_LEFTPAREN,
    [STDLK_RIGHTPAREN] = SDL_SCANCODE_KP_RIGHTPAREN,
    [STDLK_COMMA] = SDL_SCANCODE_COMMA,
    [STDLK_MINUS] = SDL_SCANCODE_MINUS,
    [STDLK_PERIOD] = SDL_SCANCODE_PERIOD,
    [STDLK_SLASH] = SDL_SCANCODE_SLASH,
    [STDLK_0] = SDL_SCANCODE_0,
    [STDLK_1] = SDL_SCANCODE_1, [STDLK_2] = SDL_SCANCODE_2,
    [STDLK_3] = SDL_SCANCODE_3, [STDLK_4] = SDL_SCANCODE_4,
    [STDLK_5] = SDL_SCANCODE_5, [STDLK_6] = SDL_SCANCODE_6,
    [STDLK_7] = SDL_SCANCODE_7, [STDLK_8] = SDL_SCANCODE_8,
    [STDLK_9] = SDL_SCANCODE_9,
    [STDLK_SEMICOLON] = SDL_SCANCODE_SEMICOLON,
    [STDLK_LESS] = SDL_SCANCODE_NONUSBACKSLASH,
    [STDLK_EQUALS] = SDL_SCANCODE_EQUALS,
    [STDLK_LEFTBRACKET] = SDL_SCANCODE_LEFTBRACKET,
    [STDLK_BACKSLASH] = SDL_SCANCODE_BACKSLASH,
    [STDLK_RIGHTBRACKET] = SDL_SCANCODE_RIGHTBRACKET,
    [STDLK_BACKQUOTE] = SDL_SCANCODE_GRAVE,
    [STDLK_a] = SDL_SCANCODE_A, [STDLK_b] = SDL_SCANCODE_B,
    [STDLK_c] = SDL_SCANCODE_C, [STDLK_d] = SDL_SCANCODE_D,
    [STDLK_e] = SDL_SCANCODE_E, [STDLK_f] = SDL_SCANCODE_F,
    [STDLK_g] = SDL_SCANCODE_G, [STDLK_h] = SDL_SCANCODE_H,
    [STDLK_i] = SDL_SCANCODE_I, [STDLK_j] = SDL_SCANCODE_J,
    [STDLK_k] = SDL_SCANCODE_K, [STDLK_l] = SDL_SCANCODE_L,
    [STDLK_m] = SDL_SCANCODE_M, [STDLK_n] = SDL_SCANCODE_N,
    [STDLK_o] = SDL_SCANCODE_O, [STDLK_p] = SDL_SCANCODE_P,
    [STDLK_q] = SDL_SCANCODE_Q, [STDLK_r] = SDL_SCANCODE_R,
    [STDLK_s] = SDL_SCANCODE_S, [STDLK_t] = SDL_SCANCODE_T,
    [STDLK_u] = SDL_SCANCODE_U, [STDLK_v] = SDL_SCANCODE_V,
    [STDLK_w] = SDL_SCANCODE_W, [STDLK_x] = SDL_SCANCODE_X,
    [STDLK_y] = SDL_SCANCODE_Y, [STDLK_z] = SDL_SCANCODE_Z,
    [STDLK_DELETE] = SDL_SCANCODE_DELETE,
    [STDLK_KP0] = SDL_SCANCODE_KP_0, [STDLK_KP1] = SDL_SCANCODE_KP_1,
    [STDLK_KP2] = SDL_SCANCODE_KP_2, [STDLK_KP3] = SDL_SCANCODE_KP_3,
    [STDLK_KP4] = SDL_SCANCODE_KP_4, [STDLK_KP5] = SDL_SCANCODE_KP_5,
    [STDLK_KP6] = SDL_SCANCODE_KP_6, [STDLK_KP7] = SDL_SCANCODE_KP_7,
    [STDLK_KP8] = SDL_SCANCODE_KP_8, [STDLK_KP9] = SDL_SCANCODE_KP_9,
    [STDLK_KP_PERIOD] = SDL_SCANCODE_KP_PERIOD,
    [STDLK_KP_DIVIDE] = SDL_SCANCODE_KP_DIVIDE,
    [STDLK_KP_MULTIPLY] = SDL_SCANCODE_KP_MULTIPLY,
    [STDLK_KP_MINUS] = SDL_SCANCODE_KP_MINUS,
    [STDLK_KP_PLUS] = SDL_SCANCODE_KP_PLUS,
    [STDLK_KP_ENTER] = SDL_SCANCODE_KP_ENTER,
    [STDLK_KP_EQUALS] = SDL_SCANCODE_KP_EQUALS,
    [STDLK_UP] = SDL_SCANCODE_UP,
    [STDLK_DOWN] = SDL_SCANCODE_DOWN,
    [STDLK_RIGHT] = SDL_SCANCODE_RIGHT,
    [STDLK_LEFT] = SDL_SCANCODE_LEFT,
    [STDLK_INSERT] = SDL_SCANCODE_INSERT,
    [STDLK_HOME] = SDL_SCANCODE_HOME,
    [STDLK_END] = SDL_SCANCODE_END,
    [STDLK_PAGEUP] = SDL_SCANCODE_PAGEUP,
    [STDLK_PAGEDOWN] = SDL_SCANCODE_PAGEDOWN,
    [STDLK_F1] = SDL_SCANCODE_F1, [STDLK_F2] = SDL_SCANCODE_F2,
    [STDLK_F3] = SDL_SCANCODE_F3, [STDLK_F4] = SDL_SCANCODE_F4,
    [STDLK_F5] = SDL_SCANCODE_F5, [STDLK_F6] = SDL_SCANCODE_F6,
    [STDLK_F7] = SDL_SCANCODE_F7, [STDLK_F8] = SDL_SCANCODE_F8,
    [STDLK_F9] = SDL_SCANCODE_F9, [STDLK_F10] = SDL_SCANCODE_F10,
    [STDLK_F11] = SDL_SCANCODE_F11, [STDLK_F12] = SDL_SCANCODE_F12,
    [STDLK_F13] = SDL_SCANCODE_F13, [STDLK_F14] = SDL_SCANCODE_F14,
    [STDLK_F15] = SDL_SCANCODE_F15,
    [STDLK_NUMLOCK] = SDL_SCANCODE_NUMLOCKCLEAR,
    [STDLK_CAPSLOCK] = SDL_SCANCODE_CAPSLOCK,
    [STDLK_SCROLLOCK] = SDL_SCANCODE_SCROLLLOCK,
    [STDLK_RSHIFT] = SDL_SCANCODE_RSHIFT,
    [STDLK_LSHIFT] = SDL_SCANCODE_LSHIFT,
    [STDLK_RCTRL] = SDL_SCANCODE_RCTRL,
    [STDLK_LCTRL] = SDL_SCANCODE_LCTRL,
    [STDLK_RALT] = SDL_SCANCODE_RALT,
    [STDLK_LALT] = SDL_SCANCODE_LALT,
    [STDLK_RMETA] = SDL_SCANCODE_RGUI,
    [STDLK_LMETA] = SDL_SCANCODE_LGUI,
    [STDLK_HELP] = SDL_SCANCODE_HELP,
    [STDLK_PRINT] = SDL_SCANCODE_PRINTSCREEN,
    [STDLK_UNDO] = SDL_SCANCODE_UNDO,
};

SDL_Scancode stdl_sdl2_scancode(unsigned sym)
{
    if (sym >= STDLK_LAST) {
        return SDL_SCANCODE_UNKNOWN;
    }
    return (SDL_Scancode)sym_scan[sym];
}

SDL_Keycode stdl_sdl2_keycode(unsigned sym)
{
    const SDL_Scancode sc = stdl_sdl2_scancode(sym);
    /* the keys under 128 whose SDL2 key code is their scancode's: Clear
     * and Pause, which type nothing, and the keypad's brackets */
    if (sym < 128 && sym != STDLK_CLEAR && sym != STDLK_PAUSE &&
        sym != STDLK_LEFTPAREN && sym != STDLK_RIGHTPAREN) {
        return (SDL_Keycode)sym;
    }
    return sc ? (SDL_Keycode)SDL_SCANCODE_TO_KEYCODE(sc) : SDLK_UNKNOWN;
}

SDL_Scancode SDL_GetScancodeFromKey(SDL_Keycode key)
{
    if (key & SDLK_SCANCODE_MASK) {
        return (SDL_Scancode)(key & ~SDLK_SCANCODE_MASK);
    }
    if (key >= 0 && key < 128) {
        return stdl_sdl2_scancode((unsigned)key);
    }
    return SDL_SCANCODE_UNKNOWN;
}

SDL_Keycode SDL_GetKeyFromScancode(SDL_Scancode scancode)
{
    unsigned sym;
    if (scancode == SDL_SCANCODE_UNKNOWN) {
        return SDLK_UNKNOWN;
    }
    for (sym = 0; sym < STDLK_LAST; sym++) {
        if (stdl_sdl2_scancode(sym) == scancode) {
            return stdl_sdl2_keycode(sym);
        }
    }
    return (SDL_Keycode)SDL_SCANCODE_TO_KEYCODE(scancode);
}
