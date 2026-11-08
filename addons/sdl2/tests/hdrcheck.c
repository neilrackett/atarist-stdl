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

/* the input, surfaces, streams, log and clock */
int stdl_sdl2_hdrcheck_rest(void)
{
    SDL_Event e;
    SDL_Joystick *j;
    SDL_GameController *c;
    SDL_Surface *s;
    SDL_RWops *rw;
    SDL_Rect r;
    Uint32 key = 0;
    int n = 0, x = 0, y = 0;
    const Uint8 *keys = SDL_GetKeyboardState(&n);
    Uint64 t = SDL_GetPerformanceCounter() + SDL_GetTicks64();
    static Uint8 mem[16];

    SDL_StartTextInput();
    while (SDL_PollEvent(&e)) {
        if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE &&
            (e.key.keysym.mod & KMOD_CTRL) && !e.key.repeat) {
            n++;
        }
        if (e.type == SDL_CONTROLLERBUTTONDOWN &&
            e.cbutton.button == SDL_CONTROLLER_BUTTON_START) {
            n++;
        }
    }
    SDL_PumpEvents();
    SDL_FlushEvents(SDL_FIRSTEVENT, SDL_LASTEVENT);
    SDL_EventState(SDL_MOUSEMOTION, SDL_IGNORE);
    e.type = SDL_RegisterEvents(1);
    SDL_PushEvent(&e);
    SDL_WaitEventTimeout(&e, 1);
    n += SDL_GetMouseState(&x, &y) & SDL_BUTTON_LMASK;
    n += SDL_GetModState() + keys[SDL_SCANCODE_SPACE] +
         (int)SDL_GetKeyFromScancode(SDL_GetScancodeFromKey(SDLK_UP));
    j = SDL_JoystickOpen(0);
    n += SDL_JoystickGetAxis(j, 0) + SDL_JoystickGetHat(j, 0) +
         SDL_JoystickNumButtons(j) + (SDL_JoystickName(j) != NULL);
    c = SDL_GameControllerOpen(0);
    n += SDL_GameControllerGetButton(c, SDL_CONTROLLER_BUTTON_A) +
         SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_LEFTX);
    SDL_GameControllerRumble(c, 0xFFFF, 0, 100);
    SDL_GameControllerClose(c);
    SDL_JoystickClose(j);

    s = SDL_CreateRGBSurface(SDL_SWSURFACE, 16, 16, 8, 0, 0, 0, 0);
    SDL_SetColorKey(s, SDL_TRUE, 0);
    SDL_GetColorKey(s, &key);
    r.x = r.y = 0;
    r.w = r.h = 8;
    SDL_SetClipRect(s, &r);
    SDL_FillRect(s, NULL, 3);
    SDL_BlitSurface(s, NULL, s, &r);
    SDL_FreeSurface(s);
    s = SDL_LoadBMP("ART.BMP");
    n += s ? s->format->palette->ncolors : 0;
    SDL_FreeSurface(SDL_LoadBMP_RW(SDL_RWFromConstMem(mem, 16), 1));
    SDL_FreeSurface(s);

    rw = SDL_RWFromMem(mem, (int)sizeof(mem));
    SDL_WriteBE32(rw, 1);
    SDL_RWseek(rw, 0, RW_SEEK_SET);
    n += (int)SDL_ReadBE32(rw) + (int)SDL_RWtell(rw) + (int)SDL_RWsize(rw);
    SDL_RWclose(rw);

    SDL_Log("%d %s", n, SDL_GetPlatform());
    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "%u", (unsigned)key);
    SDL_LogSetPriority(SDL_LOG_CATEGORY_VIDEO, SDL_LOG_PRIORITY_DEBUG);
    return n + x + y + (int)(t / SDL_GetPerformanceFrequency());
}

/* audio */
static void hdrcheck_cb(void *userdata, Uint8 *stream, int len)
{
    SDL_memset(stream, 0, (size_t)len);
    (void)userdata;
}

int stdl_sdl2_hdrcheck_audio(void)
{
    SDL_AudioSpec want, got, wav;
    SDL_AudioDeviceID id;
    Uint8 *buf = NULL, mix[8] = { 0 };
    Uint32 len = 0;
    SDL_zero(want);
    want.freq = 22050;
    want.format = AUDIO_S16SYS;
    want.channels = 2;
    want.samples = 1024;
    want.callback = hdrcheck_cb;
    id = SDL_OpenAudioDevice(NULL, 0, &want, &got,
                             SDL_AUDIO_ALLOW_FREQUENCY_CHANGE);
    SDL_PauseAudioDevice(id, 0);
    SDL_LockAudioDevice(id);
    SDL_UnlockAudioDevice(id);
    if (SDL_LoadWAV("BOOM.WAV", &wav, &buf, &len)) {
        SDL_MixAudioFormat(mix, buf, wav.format, sizeof(mix),
                           SDL_MIX_MAXVOLUME / 2);
        SDL_FreeWAV(buf);
    }
    SDL_CloseAudioDevice(id);
    return (int)SDL_GetAudioDeviceStatus(id) + SDL_GetNumAudioDevices(0) +
           (int)SDL_AUDIO_BITSIZE(got.format) + (int)len;
}
