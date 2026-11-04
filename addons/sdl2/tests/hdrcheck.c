/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * The add-on's public headers, compiled by `make sdl2` as gnu99 and as
 * C++98 under g++ 4.6 with warnings as errors (a C++ port includes
 * them as they are), and once more with the SDL 1.2 shim's guard
 * defined, which must fail. Compiled only, never linked.
 */

#include "SDL.h"
#include "SDL_stdl.h"
#include "SDL_stdl_bench.h"

int stdl_sdl2_hdrcheck(void)
{
    SDL_version v;
    SDL_Window *w;
    SDL_Renderer *r;
    SDL_Texture *t;
    SDL_Palette *pal;
    SDL_Rect box;
    STDL_SDL2_Check chk;
    const Uint8 *base;
    long bytes;
    Uint32 conv, flip;
    static Uint8 pixels[16 * 16];

    SDL_GetVersion(&v);
    SDL_SetHint(SDL_HINT_RENDER_VSYNC, "1");
    SDL_SetHint(STDL_SDL2_HINT_PAGES, "3");
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        return SDL_SetError("%s %d", SDL_GetError(), v.major);
    }
    w = SDL_CreateWindow("check", SDL_WINDOWPOS_CENTERED,
                         SDL_WINDOWPOS_CENTERED, 320, 240, SDL_WINDOW_SHOWN);
    r = SDL_CreateRenderer(w, -1, SDL_RENDERER_PRESENTVSYNC);
    STDL_SDL2_SetBorderIndex(r, 0x70);
    pal = SDL_AllocPalette(256);
    STDL_SDL2_SetRenderPalette(r, pal);
    t = SDL_CreateTexture(r, SDL_PIXELFORMAT_INDEX8,
                          SDL_TEXTUREACCESS_STREAMING, 16, 16);
    SDL_UpdateTexture(t, NULL, pixels, 16);
    box.x = box.y = 0;
    box.w = box.h = 16;
    SDL_SetRenderDrawColor(r, 0, 0, 0, SDL_ALPHA_OPAQUE);
    SDL_RenderClear(r);
    SDL_RenderCopy(r, t, NULL, &box);
    SDL_RenderPresent(r);
    STDL_SDL2_GetShownPage(r, &base, &bytes);
    STDL_SDL2_GetCheck(r, &chk);
    STDL_SDL2_GetPresentTimes(r, &conv, &flip, 1);
    SDL_DestroyTexture(t);
    SDL_FreePalette(pal);
    SDL_DestroyRenderer(r);
    SDL_DestroyWindow(w);
    SDL_Delay(1);
    SDL_Quit();
    return SDL_GetHintBoolean(SDL_HINT_RENDER_VSYNC, SDL_FALSE)
           + (int)SDL_WasInit(0) + (int)SDL_GetTicks() + (int)bytes;
}
