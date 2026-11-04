/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * Palettes, as SDL2 keeps them: colours, a version that moves on with
 * every change, and a reference count.
 */

#include "internal.h"

SDL_Palette *SDL_AllocPalette(int ncolors)
{
    SDL_Palette *p;
    if (ncolors < 1) {
        SDL_InvalidParamError("ncolors");
        return NULL;
    }
    p = (SDL_Palette *)malloc(sizeof(*p));
    if (!p) {
        SDL_OutOfMemory();
        return NULL;
    }
    p->colors = (SDL_Color *)malloc((size_t)ncolors * sizeof(SDL_Color));
    if (!p->colors) {
        free(p);
        SDL_OutOfMemory();
        return NULL;
    }
    memset(p->colors, 0xFF, (size_t)ncolors * sizeof(SDL_Color));
    p->ncolors = ncolors;
    p->version = 1;
    p->refcount = 1;
    return p;
}

void SDL_FreePalette(SDL_Palette *palette)
{
    if (!palette || --palette->refcount > 0) {
        return;
    }
    free(palette->colors);
    free(palette);
}

int SDL_SetPaletteColors(SDL_Palette *palette, const SDL_Color *colors,
                         int firstcolor, int ncolors)
{
    int status = 0;
    if (!palette) {
        return SDL_InvalidParamError("palette");
    }
    if (firstcolor < 0 || ncolors < 0 || firstcolor > palette->ncolors) {
        return SDL_InvalidParamError("firstcolor");
    }
    /* as SDL2: what fits is set, and the call says it was cut */
    if (firstcolor + ncolors > palette->ncolors) {
        ncolors = palette->ncolors - firstcolor;
        status = -1;
    }
    if (ncolors > 0 && colors != palette->colors + firstcolor) {
        memcpy(palette->colors + firstcolor, colors,
               (size_t)ncolors * sizeof(SDL_Color));
    }
    if (++palette->version == 0) {
        palette->version = 1;
    }
    return status;
}
