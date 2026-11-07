/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * The SDL2 add-on's hints: named strings a program sets to steer the
 * library, kept with SDL2's priorities - an environment variable of
 * the same name beats anything but SDL_HINT_OVERRIDE. The names here
 * are SDL2's own that programs commonly set; the add-on accepts them
 * and honours what it can (each says), so the program builds as it
 * stands. Atari-only hints are in SDL_stdl.h.
 */

#ifndef STDL_SDL2_HINTS_H
#define STDL_SDL2_HINTS_H

#include "SDL_stdinc.h"

#ifdef __cplusplus
extern "C" {
#endif

/* accepted and ignored: there is no scaling to filter */
#define SDL_HINT_RENDER_SCALE_QUALITY "SDL_RENDER_SCALE_QUALITY"
/* accepted and ignored: there is one renderer */
#define SDL_HINT_RENDER_DRIVER        "SDL_RENDER_DRIVER"
/* accepted and ignored: Flip always waits for the display */
#define SDL_HINT_RENDER_VSYNC         "SDL_RENDER_VSYNC"
/* accepted and ignored: there is no window system */
#define SDL_HINT_VIDEO_HIGHDPI_DISABLED "SDL_VIDEO_HIGHDPI_DISABLED"
#define SDL_HINT_FRAMEBUFFER_ACCELERATION "SDL_FRAMEBUFFER_ACCELERATION"
#define SDL_HINT_NO_SIGNAL_HANDLERS   "SDL_NO_SIGNAL_HANDLERS"
#define SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS \
    "SDL_JOYSTICK_ALLOW_BACKGROUND_EVENTS"

typedef enum {
    SDL_HINT_DEFAULT,
    SDL_HINT_NORMAL,
    SDL_HINT_OVERRIDE
} SDL_HintPriority;

#define SDL_SetHintWithPriority stdl_sdl2_SetHintWithPriority
#define SDL_SetHint             stdl_sdl2_SetHint
#define SDL_GetHint             stdl_sdl2_GetHint
#define SDL_GetHintBoolean      stdl_sdl2_GetHintBoolean
#define SDL_ClearHints          stdl_sdl2_ClearHints

extern SDL_bool SDL_SetHintWithPriority(const char *name, const char *value,
                                        SDL_HintPriority priority);
extern SDL_bool SDL_SetHint(const char *name, const char *value);
extern const char *SDL_GetHint(const char *name);
extern SDL_bool SDL_GetHintBoolean(const char *name, SDL_bool default_value);
extern void SDL_ClearHints(void);

#ifdef __cplusplus
}
#endif

#endif /* STDL_SDL2_HINTS_H */
