/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * The log: a line a message, "INFO: ..." as SDL2 writes them, to the
 * file the hint STDL_SDL2_HINT_LOGFILE names, opened at the first
 * message written and flushed after each, so a crash keeps what came
 * before it (GEMDOS closes the file when the program ends). With no
 * hint, nowhere: SDL_log.h says why never the console. The priorities
 * are SDL2's: the application from INFO, assertions from WARN, tests
 * from VERBOSE, the rest CRITICAL only.
 */

#include "internal.h"
#include "SDL_stdl.h"

#define CATEGORIES (SDL_LOG_CATEGORY_CUSTOM + 1)

static Uint8 prio[CATEGORIES];
static int prio_set;
static FILE *logfile;
static int logfile_failed;

static const char *const prio_name[SDL_NUM_LOG_PRIORITIES] = {
    NULL, "VERBOSE", "DEBUG", "INFO", "WARN", "ERROR", "CRITICAL"
};

static int slot(int category)
{
    return category >= 0 && category < CATEGORIES ? category
                                                  : SDL_LOG_CATEGORY_CUSTOM;
}

static void defaults(void)
{
    int i;
    for (i = 0; i < CATEGORIES; i++) {
        prio[i] = SDL_LOG_PRIORITY_CRITICAL;
    }
    prio[SDL_LOG_CATEGORY_APPLICATION] = SDL_LOG_PRIORITY_INFO;
    prio[SDL_LOG_CATEGORY_ASSERT] = SDL_LOG_PRIORITY_WARN;
    prio[SDL_LOG_CATEGORY_TEST] = SDL_LOG_PRIORITY_VERBOSE;
    prio_set = 1;
}

void SDL_LogSetAllPriority(SDL_LogPriority priority)
{
    int i;
    for (i = 0; i < CATEGORIES; i++) {
        prio[i] = (Uint8)priority;
    }
    prio_set = 1;
}

void SDL_LogSetPriority(int category, SDL_LogPriority priority)
{
    if (!prio_set) {
        defaults();
    }
    prio[slot(category)] = (Uint8)priority;
}

void SDL_LogMessageV(int category, SDL_LogPriority priority, const char *fmt,
                     va_list ap)
{
    if (priority < SDL_LOG_PRIORITY_VERBOSE ||
        priority >= SDL_NUM_LOG_PRIORITIES) {
        return;
    }
    if (!prio_set) {
        defaults();
    }
    if (priority < prio[slot(category)]) {
        return;
    }
    if (!logfile) {
        const char *name = SDL_GetHint(STDL_SDL2_HINT_LOGFILE);
        if (!name || !*name || logfile_failed) {
            return;
        }
        logfile = fopen(name, "a");
        if (!logfile) {
            logfile_failed = 1;
            return;
        }
    }
    fprintf(logfile, "%s: ", prio_name[priority]);
    vfprintf(logfile, fmt, ap);
    fputc('\n', logfile);
    fflush(logfile);
}

void SDL_LogMessage(int category, SDL_LogPriority priority, const char *fmt,
                    ...)
{
    va_list ap;
    va_start(ap, fmt);
    SDL_LogMessageV(category, priority, fmt, ap);
    va_end(ap);
}

void SDL_Log(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    SDL_LogMessageV(SDL_LOG_CATEGORY_APPLICATION, SDL_LOG_PRIORITY_INFO, fmt,
                    ap);
    va_end(ap);
}

#define LOG_AT(name, p)                                                    \
    void name(int category, const char *fmt, ...)                          \
    {                                                                      \
        va_list ap;                                                        \
        va_start(ap, fmt);                                                 \
        SDL_LogMessageV(category, p, fmt, ap);                             \
        va_end(ap);                                                        \
    }

LOG_AT(SDL_LogVerbose, SDL_LOG_PRIORITY_VERBOSE)
LOG_AT(SDL_LogDebug, SDL_LOG_PRIORITY_DEBUG)
LOG_AT(SDL_LogInfo, SDL_LOG_PRIORITY_INFO)
LOG_AT(SDL_LogWarn, SDL_LOG_PRIORITY_WARN)
LOG_AT(SDL_LogError, SDL_LOG_PRIORITY_ERROR)
LOG_AT(SDL_LogCritical, SDL_LOG_PRIORITY_CRITICAL)
