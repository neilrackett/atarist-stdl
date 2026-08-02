/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * Key symbols and modifiers. Values match SDL 1.2 so ported switch
 * statements and keysym tables work unchanged.
 */

#ifndef STDL_KEYS_H
#define STDL_KEYS_H

typedef enum {
    STDLK_UNKNOWN    = 0,
    STDLK_BACKSPACE  = 8,
    STDLK_TAB        = 9,
    STDLK_CLEAR      = 12,
    STDLK_RETURN     = 13,
    STDLK_PAUSE      = 19,
    STDLK_ESCAPE     = 27,
    STDLK_SPACE      = 32,
    STDLK_EXCLAIM    = 33,
    STDLK_QUOTEDBL   = 34,
    STDLK_HASH       = 35,
    STDLK_DOLLAR     = 36,
    STDLK_AMPERSAND  = 38,
    STDLK_QUOTE      = 39,
    STDLK_LEFTPAREN  = 40,
    STDLK_RIGHTPAREN = 41,
    STDLK_ASTERISK   = 42,
    STDLK_PLUS       = 43,
    STDLK_COMMA      = 44,
    STDLK_MINUS      = 45,
    STDLK_PERIOD     = 46,
    STDLK_SLASH      = 47,
    STDLK_0 = 48, STDLK_1, STDLK_2, STDLK_3, STDLK_4,
    STDLK_5, STDLK_6, STDLK_7, STDLK_8, STDLK_9,
    STDLK_COLON      = 58,
    STDLK_SEMICOLON  = 59,
    STDLK_LESS       = 60,
    STDLK_EQUALS     = 61,
    STDLK_GREATER    = 62,
    STDLK_QUESTION   = 63,
    STDLK_AT         = 64,
    STDLK_LEFTBRACKET  = 91,
    STDLK_BACKSLASH    = 92,
    STDLK_RIGHTBRACKET = 93,
    STDLK_CARET      = 94,
    STDLK_UNDERSCORE = 95,
    STDLK_BACKQUOTE  = 96,
    STDLK_a = 97, STDLK_b, STDLK_c, STDLK_d, STDLK_e, STDLK_f,
    STDLK_g, STDLK_h, STDLK_i, STDLK_j, STDLK_k, STDLK_l, STDLK_m,
    STDLK_n, STDLK_o, STDLK_p, STDLK_q, STDLK_r, STDLK_s, STDLK_t,
    STDLK_u, STDLK_v, STDLK_w, STDLK_x, STDLK_y, STDLK_z,
    STDLK_DELETE     = 127,

    STDLK_KP0 = 256, STDLK_KP1, STDLK_KP2, STDLK_KP3, STDLK_KP4,
    STDLK_KP5, STDLK_KP6, STDLK_KP7, STDLK_KP8, STDLK_KP9,
    STDLK_KP_PERIOD   = 266,
    STDLK_KP_DIVIDE   = 267,
    STDLK_KP_MULTIPLY = 268,
    STDLK_KP_MINUS    = 269,
    STDLK_KP_PLUS     = 270,
    STDLK_KP_ENTER    = 271,
    STDLK_KP_EQUALS   = 272,
    STDLK_UP          = 273,
    STDLK_DOWN        = 274,
    STDLK_RIGHT       = 275,
    STDLK_LEFT        = 276,
    STDLK_INSERT      = 277,
    STDLK_HOME        = 278,
    STDLK_END         = 279,
    STDLK_PAGEUP      = 280,
    STDLK_PAGEDOWN    = 281,
    STDLK_F1 = 282, STDLK_F2, STDLK_F3, STDLK_F4, STDLK_F5,
    STDLK_F6, STDLK_F7, STDLK_F8, STDLK_F9, STDLK_F10,
    STDLK_F11 = 292, STDLK_F12, STDLK_F13, STDLK_F14, STDLK_F15,
    STDLK_NUMLOCK     = 300,
    STDLK_CAPSLOCK    = 301,
    STDLK_SCROLLOCK   = 302,
    STDLK_RSHIFT      = 303,
    STDLK_LSHIFT      = 304,
    STDLK_RCTRL      = 305,
    STDLK_LCTRL      = 306,
    STDLK_RALT       = 307,
    STDLK_LALT       = 308,
    STDLK_RMETA      = 309,
    STDLK_LMETA      = 310,
    STDLK_HELP       = 315,
    STDLK_PRINT      = 316,
    STDLK_UNDO       = 322,
    STDLK_LAST
} STDL_Key;

typedef enum {
    STDL_KMOD_NONE   = 0x0000,
    STDL_KMOD_LSHIFT = 0x0001,
    STDL_KMOD_RSHIFT = 0x0002,
    STDL_KMOD_LCTRL  = 0x0040,
    STDL_KMOD_RCTRL  = 0x0080,
    STDL_KMOD_LALT   = 0x0100,
    STDL_KMOD_RALT   = 0x0200,
    STDL_KMOD_NUM    = 0x1000,
    STDL_KMOD_CAPS   = 0x2000,
    STDL_KMOD_MODE   = 0x4000,
    /* set on key events synthesised from the joystick when
     * STDL_JoyKeyEmulation is enabled */
    STDL_KMOD_JOYSTICK = 0x8000
} STDL_Mod;

#define STDL_KMOD_CTRL  (STDL_KMOD_LCTRL | STDL_KMOD_RCTRL)
#define STDL_KMOD_SHIFT (STDL_KMOD_LSHIFT | STDL_KMOD_RSHIFT)
#define STDL_KMOD_ALT   (STDL_KMOD_LALT | STDL_KMOD_RALT)

#endif /* STDL_KEYS_H */
