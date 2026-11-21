/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * The SDL2 add-on's own state, shared between its objects. The
 * add-on reaches the core through its public headers only.
 */

#ifndef STDL_SDL2_INTERNAL_H
#define STDL_SDL2_INTERNAL_H

#include "SDL.h"
#include <stdl/stdl.h>
#include "c2p.h"

/* The ST build (st.h) is for the 68000 and nothing above it: the
 * add-on a TT or a Falcon runs is built for the 68020, and so can
 * never have a line of it. */
#if defined(STDL_SDL2_ST) && defined(__mc68020__)
#error "STDL_SDL2_ST is the experimental ST build's (make sdl2st): -m68000 only"
#endif

/* _MCH 2 is a TT and 3 a Falcon: anything below has no 256-colour
 * screen (init.c says what that leaves out) */
#define STDL_SDL2_NO_256() \
    (STDL_GetMachineInfo()->mch_cookie < 0x00020000UL)

/* SDL_INIT_ bits granted, for SDL_WasInit */
extern Uint32 stdl_sdl2_inited;

/* keymap.c: STDL's key symbols as SDL2's */
SDL_Scancode stdl_sdl2_scancode(unsigned sym);
SDL_Keycode stdl_sdl2_keycode(unsigned sym);

/* video.c: the screen position of the logical size's top left, which
 * the mouse is reported from */
void stdl_sdl2_screen_origin(int *x, int *y);

/*
 * Pixel pairs (STDL_SDL2_RenderCopyPairs): c2ppair.c's side, which the
 * renderer reaches through the pointer stdl_sdl2_copy_pairs gives it,
 * so that a program that never asks for pairs links none of it. conv
 * (c2pp.c's stdl_sdl2_c2pp) converts as stdl_sdl2_c2p does. mark notes
 * a copy of h rows at sx, w wide, in the batch's masks from its first
 * row, by stdl_sdl2_pair_rule.
 */
#ifdef STDL_SDL2_ST
/* an ST's screen's conversion (st.h): h rows of groups into its four
 * planes, the first row through tab, the next through next, in turn */
typedef void stdl_sdl2_st_conv_fn(const Uint8 *src, long sp, Uint8 *dst,
                                  long dp, long groups, long h,
                                  const Uint32 *tab, const Uint32 *next);
#endif

struct stdl_sdl2_pairs {
    stdl_sdl2_c2pp_fn *conv;
    void (*mark)(Uint32 *pair, const Uint32 *whole, const Uint32 *part,
                 int h, int sx, int w, int pairing);
#ifdef STDL_SDL2_ST
    /* an ST's screen (stpair.c, st.h): its pair loop, and its pair
     * tables made from the conversion's */
    stdl_sdl2_st_conv_fn *st_conv;
    void (*st_tabs)(const Uint32 *tab, Uint32 *ptab);
#endif
};

/*
 * The batch's rule for pairs, a row at a time: of a copy's groups m, a
 * pairs copy's go into *pair where no other copy has reached them
 * (*whole less *pair, or *part), and any other copy takes back every
 * one. A group is converted as pairs only if every copy of the batch
 * that reached it was a pairs copy covering it whole.
 */
static __inline__ void stdl_sdl2_pair_rule(Uint32 *pair, const Uint32 *whole,
                                           const Uint32 *part, Uint32 m,
                                           int pairing)
{
    const Uint32 p = *pair;
    *pair = pairing ? p | (m & ~((*whole & ~p) | *part)) : p & ~m;
}

/* video.c: SDL_RenderCopy with its whole groups converted as pairs */
int stdl_sdl2_copy_pairs(SDL_Renderer *r, SDL_Texture *texture,
                         const SDL_Rect *srcrect, const SDL_Rect *dstrect,
                         const struct stdl_sdl2_pairs *pairs);

/* events.c: an event for the queue; 1, 0 ignored, -1 full */
int stdl_sdl2_push(SDL_Event *e);

/* joystick.c: an STDL joystick event, the state it carries kept and
 * passed on to an open joystick and controller; the controller's
 * buttons, read and their changes sent, at a pump that brought its
 * joystick an event */
void stdl_sdl2_joy_event(const STDL_Event *se);
void stdl_sdl2_controller_poll(void);

/* audio.c: the open device's format, or 0 */
SDL_AudioFormat stdl_sdl2_audio_format(void);

/* little-endian fields of the files the add-on reads (BMP, WAV) */
static __inline__ Uint32 stdl_sdl2_le32(const Uint8 *p)
{
    return (Uint32)p[0] | (Uint32)p[1] << 8 | (Uint32)p[2] << 16 |
           (Uint32)p[3] << 24;
}

static __inline__ unsigned stdl_sdl2_le16(const Uint8 *p)
{
    return (unsigned)(p[0] | p[1] << 8);
}

#endif /* STDL_SDL2_INTERNAL_H */
