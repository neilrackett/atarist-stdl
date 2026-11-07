/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * The SDL2 add-on's log: to the file the hint STDL_SDL2_HINT_LOGFILE
 * names (SDL_stdl.h), a line a message, or nowhere. Never to the
 * console: on a TT a console write makes the next flip copy the whole
 * page, and the text stays in the picture.
 */

#ifndef STDL_SDL2_LOG_H
#define STDL_SDL2_LOG_H

#include "SDL_stdinc.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    SDL_LOG_CATEGORY_APPLICATION,
    SDL_LOG_CATEGORY_ERROR,
    SDL_LOG_CATEGORY_ASSERT,
    SDL_LOG_CATEGORY_SYSTEM,
    SDL_LOG_CATEGORY_AUDIO,
    SDL_LOG_CATEGORY_VIDEO,
    SDL_LOG_CATEGORY_RENDER,
    SDL_LOG_CATEGORY_INPUT,
    SDL_LOG_CATEGORY_TEST,
    SDL_LOG_CATEGORY_CUSTOM = 19
} SDL_LogCategory;

typedef enum {
    SDL_LOG_PRIORITY_VERBOSE = 1,
    SDL_LOG_PRIORITY_DEBUG,
    SDL_LOG_PRIORITY_INFO,
    SDL_LOG_PRIORITY_WARN,
    SDL_LOG_PRIORITY_ERROR,
    SDL_LOG_PRIORITY_CRITICAL,
    SDL_NUM_LOG_PRIORITIES
} SDL_LogPriority;

#define SDL_Log                stdl_sdl2_Log
#define SDL_LogVerbose         stdl_sdl2_LogVerbose
#define SDL_LogDebug           stdl_sdl2_LogDebug
#define SDL_LogInfo            stdl_sdl2_LogInfo
#define SDL_LogWarn            stdl_sdl2_LogWarn
#define SDL_LogError           stdl_sdl2_LogError
#define SDL_LogCritical        stdl_sdl2_LogCritical
#define SDL_LogMessage         stdl_sdl2_LogMessage
#define SDL_LogMessageV        stdl_sdl2_LogMessageV
#define SDL_LogSetAllPriority  stdl_sdl2_LogSetAllPriority
#define SDL_LogSetPriority     stdl_sdl2_LogSetPriority

/* INFO and above are written, as in SDL2 for the application */
extern void SDL_Log(const char *fmt, ...) SDL_PRINTF_VARARG_FUNC(1);
extern void SDL_LogVerbose(int category, const char *fmt, ...) SDL_PRINTF_VARARG_FUNC(2);
extern void SDL_LogDebug(int category, const char *fmt, ...) SDL_PRINTF_VARARG_FUNC(2);
extern void SDL_LogInfo(int category, const char *fmt, ...) SDL_PRINTF_VARARG_FUNC(2);
extern void SDL_LogWarn(int category, const char *fmt, ...) SDL_PRINTF_VARARG_FUNC(2);
extern void SDL_LogError(int category, const char *fmt, ...) SDL_PRINTF_VARARG_FUNC(2);
extern void SDL_LogCritical(int category, const char *fmt, ...) SDL_PRINTF_VARARG_FUNC(2);
extern void SDL_LogMessage(int category, SDL_LogPriority priority,
                           const char *fmt, ...) SDL_PRINTF_VARARG_FUNC(3);
extern void SDL_LogMessageV(int category, SDL_LogPriority priority,
                            const char *fmt, va_list ap);
extern void SDL_LogSetAllPriority(SDL_LogPriority priority);
extern void SDL_LogSetPriority(int category, SDL_LogPriority priority);

#ifdef __cplusplus
}
#endif

#endif /* STDL_SDL2_LOG_H */
