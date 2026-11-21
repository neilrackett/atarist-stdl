/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * The SDL1 add-on's SDL2 half (bridge.h): the screen as a window, a
 * renderer, its palette and a texture over the program's pixels, and
 * the SDL2 add-on's BMP loader. Everything here is the SDL2 add-on's
 * public calls; its errors are handed to STDL's, which an SDL 1.2
 * program's SDL_GetError reads.
 */

#include "SDL.h"
#include "SDL_stdl.h"
#include <stdl/stdl.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include "bridge.h"

static SDL_Window *win;
static SDL_Renderer *ren;
static SDL_Palette *pal;
static SDL_Texture *tex;
static unsigned border;

static int fail(void)
{
    STDL_SetError(SDL_GetError());
    return -1;
}

int stdl_sdl1_br_open(void *pixels, int w, int h, int pitch, int *ox,
                      int *oy)
{
    int sx, sy, lx, ly;
    if (!ren) {
        if (SDL_Init(SDL_INIT_VIDEO) < 0 ||
            !(win = SDL_CreateWindow("", SDL_WINDOWPOS_CENTERED,
                                     SDL_WINDOWPOS_CENTERED, w, h, 0)) ||
            !(ren = SDL_CreateRenderer(win, -1, 0)) ||
            !(pal = SDL_AllocPalette(256))) {
            return fail();
        }
        STDL_SDL2_SetRenderPalette(ren, pal);
        /* the loop that reads X as it runs, which is what
         * STDL_SDL2_SetBorderIndex takes for an X from a variable */
        if (border && stdl_sdl2_set_border(ren, border,
                                           &stdl_sdl2_c2p_any) < 0) {
            return fail();
        }
    } else {
        /* a new mode on the screen there is: the old picture goes */
        if (tex) {
            SDL_DestroyTexture(tex);
            tex = NULL;
        }
        STDL_SDL2_ClearScreenNow(ren);
    }
    if (SDL_RenderSetLogicalSize(ren, w, h) < 0 ||
        !(tex = STDL_SDL2_CreateTextureFrom(ren, pixels, w, h, pitch))) {
        return fail();
    }
    /* the picture's place: STDL's mouse against the SDL2 add-on's,
     * which is measured from the logical size's top left */
    STDL_GetMouseState(&sx, &sy);
    SDL_GetMouseState(&lx, &ly);
    *ox = sx - lx;
    *oy = sy - ly;
    return 0;
}

void stdl_sdl1_br_push(int x, int y, int w, int h)
{
    SDL_Rect r;
    r.x = x;
    r.y = y;
    r.w = w;
    r.h = h;
    SDL_RenderCopy(ren, tex, &r, &r);
}

void stdl_sdl1_br_present(void)
{
    SDL_RenderPresent(ren);
}

void stdl_sdl1_br_colours(const void *colours, int first, int n)
{
    if (pal) {
        /* SDL2's SDL_Color is STDL_Colour's four bytes, r, g and b
         * first */
        SDL_SetPaletteColors(pal, (const SDL_Color *)colours, first, n);
        STDL_SDL2_StagePalette(ren);
    }
}

int stdl_sdl1_br_hint(const char *name, const char *value)
{
    return SDL_SetHint(name, value) == SDL_TRUE;
}

int stdl_sdl1_br_border(unsigned x)
{
    if (ren) {
        /* the SDL2 add-on takes it before anything is drawn */
        STDL_SetError("The border index is set before the video mode");
        return -1;
    }
    border = x;
    return 0;
}

int stdl_sdl1_br_check(unsigned long *flips, unsigned long *bad)
{
    STDL_SDL2_Check chk;
    if (!ren || STDL_SDL2_GetCheck(ren, &chk) < 0) {
        return -1;
    }
    *flips = chk.flips;
    *bad = chk.bad;
    return 0;
}

void *stdl_sdl1_br_bmp(const char *file, int *w, int *h, int *pitch,
                       unsigned char *colours, int *ncolours)
{
    SDL_Surface *s = SDL_LoadBMP(file);
    Uint8 *pixels;
    int i, n;
    if (!s && strlen(file) < 128) {
        /* GEMDOS names are upper case, a ported program's seldom: the
         * file's name again in upper case, the directories as they
         * are, as the core's loaders try it (stdl_fopen_ci) */
        char up[128];
        int base = 0;
        for (i = 0; file[i]; i++) {
            up[i] = file[i];
            if (file[i] == '/' || file[i] == '\\') {
                base = i + 1;
            }
        }
        up[i] = 0;
        for (i = base; up[i]; i++) {
            up[i] = (char)toupper((unsigned char)up[i]);
        }
        if (strcmp(up, file) != 0) {
            s = SDL_LoadBMP(up);
        }
    }
    if (!s) {
        fail();
        return NULL;
    }
    pixels = (Uint8 *)malloc((size_t)s->h * (size_t)s->pitch);
    if (!pixels) {
        SDL_FreeSurface(s);
        STDL_SetError("Out of memory");
        return NULL;
    }
    memcpy(pixels, s->pixels, (size_t)s->h * (size_t)s->pitch);
    *w = s->w;
    *h = s->h;
    *pitch = s->pitch;
    n = s->format->palette ? s->format->palette->ncolors : 0;
    n = n > 256 ? 256 : n;
    /* SDL2's SDL_Color is the four bytes, as stdl_sdl1_br_colours has it */
    memcpy(colours, s->format->palette->colors, (size_t)n * 4);
    *ncolours = n;
    SDL_FreeSurface(s);
    return pixels;
}
