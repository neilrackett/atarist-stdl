/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * STDL_SDL2_RenderCopyPairs (SDL_stdl.h): a copy whose pixels come in
 * pairs, as a program drawing at half the width has them, converted
 * by the pair loops (c2pp.c, stdl_sdl2_c2pp): 56 instructions a group
 * against 74. This object is reached only through the call, and the
 * renderer reaches the loops only through the pointer it gives, so a
 * program that never asks for pairs links none of them. The loops are
 * an object of their own, so that SDL_stdl_c2p.h's
 * STDL_SDL2_ConvertBoxPairs links them without the renderer this one
 * names (stdl_sdl2_copy_pairs, video.c).
 */

#include "SDL_stdl.h"
#include "internal.h"
#include "c2p.h"

/* A group is converted as pairs only if every copy of the batch that
 * reached it was a pairs copy covering it whole (internal.h) */
static void mark(Uint32 *pair, const Uint32 *whole, const Uint32 *part,
                 int h, int sx, int w, int pairing)
{
    int g0, g1;
    Uint32 m;
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
    m = (Uint32)(((1UL << (g1 + 1)) - 1) & ~((1UL << g0) - 1));
    for (; h > 0; h--, pair++, whole++, part++) {
        const Uint32 p = *pair;
        *pair = pairing ? p | (m & ~((*whole & ~p) | *part)) : p & ~m;
    }
}

static const struct stdl_sdl2_pairs ops = { stdl_sdl2_c2pp, mark };

int STDL_SDL2_RenderCopyPairs(SDL_Renderer *renderer, SDL_Texture *texture,
                              const SDL_Rect *srcrect,
                              const SDL_Rect *dstrect)
{
    return stdl_sdl2_copy_pairs(renderer, texture, srcrect, dstrect, &ops);
}
