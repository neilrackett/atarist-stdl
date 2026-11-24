/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * STDL_SDL2_RenderCopyPairs (SDL_stdl.h): a copy whose pixels come in
 * pairs, converted by the pair loops (c2pp.c says how, and why they
 * are an object of their own). This object is reached only through the
 * call, and the renderer reaches the loops only through the pointer it
 * gives, so a program that never asks for pairs links none of them.
 */

#include "SDL_stdl.h"
#include "internal.h"
#include "c2p.h"
#ifdef STDL_SDL2_ST
#include "st.h"
#endif

/* a copy's groups by the batch's rule (internal.h), from its first row */
static void mark(stdl_sdl2_mask *pair, const stdl_sdl2_mask *whole,
                 const stdl_sdl2_mask *part, int h, int sx, int w,
                 int pairing)
{
    int g0, g1;
    stdl_sdl2_mask m;
    if (pairing) {
        g0 = (sx + 15) >> 4;
        g1 = ((sx + w) >> 4) - 1;
    } else {
        g0 = sx >> 4;
        g1 = (sx + w - 1) >> 4;
    }
    if (g0 > g1) {
        return;
    }
    m = STDL_SDL2_SPAN(g0, g1 + 1);
    for (; h > 0; h--, pair++, whole++, part++) {
        stdl_sdl2_pair_rule(pair, whole, part, m, pairing);
    }
}

static const struct stdl_sdl2_pairs ops = {
    stdl_sdl2_c2pp, mark,
#ifdef STDL_SDL2_ST
    /* and an ST's screen's own (stpair.c), in the experimental build */
    stdl_sdl2_st_c2pp, stdl_sdl2_st_pairtabs
#endif
};

int STDL_SDL2_RenderCopyPairs(SDL_Renderer *renderer, SDL_Texture *texture,
                              const SDL_Rect *srcrect,
                              const SDL_Rect *dstrect)
{
    return stdl_sdl2_copy_pairs(renderer, texture, srcrect, dstrect, &ops);
}
