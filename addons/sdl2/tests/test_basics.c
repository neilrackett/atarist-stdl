/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * The SDL2 add-on's errors, hints, SDL_Init and version, on the host.
 * The core is stood in for here (STDL_Init, STDL_Quit, STDL_GetError,
 * STDL_GetMachineInfo), so the machine SDL_Init sees can be chosen.
 */

#include "SDL.h"
#include <stdl/stdl.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static STDL_MachineInfo mach;
static int core_inits, core_quits;

int STDL_Init(uint32_t flags)
{
    (void)flags;
    core_inits++;
    return 0;
}

void STDL_Quit(void)
{
    core_quits++;
}

const char *STDL_GetError(void)
{
    return "core error";
}

const STDL_MachineInfo *STDL_GetMachineInfo(void)
{
    return &mach;
}

static int failures;

#define CHECK(c) do { \
        if (!(c)) { \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); \
            failures++; \
        } \
    } while (0)

static int same(const char *a, const char *b)
{
    return a && b && strcmp(a, b) == 0;
}

static void test_errors(void)
{
    char longmsg[300];
    CHECK(same(SDL_GetError(), ""));
    CHECK(SDL_SetError("x %d", 3) == -1);
    CHECK(same(SDL_GetError(), "x 3"));
    SDL_ClearError();
    CHECK(same(SDL_GetError(), ""));
    /* a message longer than the buffer is cut, never overrun (ASan) */
    memset(longmsg, 'e', sizeof(longmsg) - 1);
    longmsg[sizeof(longmsg) - 1] = '\0';
    SDL_SetError("%s", longmsg);
    CHECK(strlen(SDL_GetError()) == 127);
    SDL_OutOfMemory();
    CHECK(same(SDL_GetError(), "Out of memory"));
    SDL_ClearError();
}

static void test_hints(void)
{
    char name[16];
    int i;

    CHECK(SDL_GetHint("A") == NULL);
    CHECK(SDL_SetHint("A", "1") == SDL_TRUE);
    CHECK(same(SDL_GetHint("A"), "1"));
    /* not replaced at a lower priority, replaced at a higher one */
    CHECK(SDL_SetHintWithPriority("A", "2", SDL_HINT_DEFAULT) == SDL_FALSE);
    CHECK(same(SDL_GetHint("A"), "1"));
    CHECK(SDL_SetHintWithPriority("A", "3", SDL_HINT_OVERRIDE) == SDL_TRUE);
    CHECK(SDL_SetHint("A", "4") == SDL_FALSE);
    CHECK(same(SDL_GetHint("A"), "3"));

    /* the environment beats anything below OVERRIDE */
    setenv("STDL_SDL2_TEST_ENV", "env", 1);
    CHECK(SDL_SetHint("STDL_SDL2_TEST_ENV", "set") == SDL_FALSE);
    CHECK(same(SDL_GetHint("STDL_SDL2_TEST_ENV"), "env"));
    CHECK(SDL_SetHintWithPriority("STDL_SDL2_TEST_ENV", "over",
                                  SDL_HINT_OVERRIDE) == SDL_TRUE);
    CHECK(same(SDL_GetHint("STDL_SDL2_TEST_ENV"), "over"));
    unsetenv("STDL_SDL2_TEST_ENV");

    CHECK(SDL_GetHintBoolean("unset", SDL_TRUE) == SDL_TRUE);
    CHECK(SDL_GetHintBoolean("unset", SDL_FALSE) == SDL_FALSE);
    SDL_SetHint("B", "0");
    CHECK(SDL_GetHintBoolean("B", SDL_TRUE) == SDL_FALSE);
    SDL_SetHint("B", "FaLsE");
    CHECK(SDL_GetHintBoolean("B", SDL_TRUE) == SDL_FALSE);
    SDL_SetHint("B", "falsey");
    CHECK(SDL_GetHintBoolean("B", SDL_FALSE) == SDL_TRUE);
    SDL_SetHint("B", "");
    CHECK(SDL_GetHintBoolean("B", SDL_TRUE) == SDL_TRUE);
    SDL_SetHint("B", "1");
    CHECK(SDL_GetHintBoolean("B", SDL_FALSE) == SDL_TRUE);
    CHECK(SDL_SetHint("C", NULL) == SDL_TRUE);
    CHECK(SDL_GetHint("C") == NULL);

    SDL_ClearHints();
    CHECK(SDL_GetHint("A") == NULL);
    CHECK(SDL_SetHint("A", "again") == SDL_TRUE);
    SDL_ClearHints();

    /* sixteen, then a refusal that says so */
    for (i = 0; i < 16; i++) {
        snprintf(name, sizeof(name), "H%d", i);
        CHECK(SDL_SetHint(name, "v") == SDL_TRUE);
    }
    CHECK(SDL_SetHint("H16", "v") == SDL_FALSE);
    CHECK(strstr(SDL_GetError(), "hints") != NULL);
    CHECK(SDL_SetHint("H3", "w") == SDL_TRUE);   /* replacing still works */
    CHECK(same(SDL_GetHint("H3"), "w"));
    SDL_ClearHints();
    SDL_ClearError();
}

static void test_init(void)
{
    /* a Mega STE: refused, nothing granted */
    mach.mch_cookie = 0x00010010UL;
    CHECK(SDL_Init(SDL_INIT_VIDEO) == -1);
    CHECK(strstr(SDL_GetError(), "TT or a Falcon") != NULL);
    CHECK(SDL_WasInit(0) == 0);
    /* no cookie jar at all (TOS 1.0x) */
    mach.mch_cookie = 0;
    CHECK(SDL_Init(SDL_INIT_VIDEO) == -1);

    mach.mch_cookie = 0x00020000UL;   /* a TT */
    CHECK(SDL_Init(SDL_INIT_VIDEO) == 0);
    CHECK(SDL_WasInit(SDL_INIT_VIDEO) == SDL_INIT_VIDEO);
    CHECK(SDL_WasInit(SDL_INIT_EVENTS) == SDL_INIT_EVENTS);
    CHECK(SDL_WasInit(SDL_INIT_JOYSTICK) == 0);
    CHECK(SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER) == 0);
    CHECK(SDL_WasInit(SDL_INIT_JOYSTICK) == SDL_INIT_JOYSTICK);
    SDL_QuitSubSystem(SDL_INIT_VIDEO);
    CHECK(SDL_WasInit(SDL_INIT_VIDEO) == 0);
    CHECK(SDL_WasInit(SDL_INIT_GAMECONTROLLER) == SDL_INIT_GAMECONTROLLER);
    /* NOPARACHUTE is not a subsystem and is not reported */
    CHECK(SDL_Init(SDL_INIT_NOPARACHUTE) == 0);
    CHECK((SDL_WasInit(0) & SDL_INIT_NOPARACHUTE) == 0);
    SDL_Quit();
    CHECK(core_quits == 1);
    CHECK(SDL_WasInit(0) == 0);

    mach.mch_cookie = 0x00030000UL;   /* a Falcon */
    CHECK(SDL_Init(SDL_INIT_EVERYTHING) == 0);
    CHECK(SDL_WasInit(0) == SDL_INIT_EVERYTHING);
    SDL_Quit();
    CHECK(core_inits >= 4);
}

static void test_version(void)
{
    SDL_version v;
    SDL_GetVersion(&v);
    CHECK(v.major == 2 && v.minor == 0 && v.patch == 10);
    CHECK(SDL_VERSION_ATLEAST(2, 0, 10));
    CHECK(!SDL_VERSION_ATLEAST(2, 0, 11));
}

int main(void)
{
    test_errors();
    test_hints();
    test_init();
    test_version();
    if (failures) {
        printf("test_basics: %d failures\n", failures);
        return 1;
    }
    printf("test_basics: ok\n");
    return 0;
}
