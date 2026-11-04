/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * Hints, with SDL2's priorities: an environment variable of the same
 * name wins over a hint set at less than SDL_HINT_OVERRIDE, and a
 * hint is not replaced at a lower priority than it was set with.
 * Built for the 68000 with init.c, as a program sets its hints before
 * SDL_Init.
 */

#include "internal.h"

/* a program sets a handful; SDL2's list is unbounded */
#define MAX_HINTS 16

typedef struct {
    char *name;
    char *value;
    SDL_HintPriority priority;
} Hint;

static Hint hints[MAX_HINTS];
static int nhints;

static char *copy(const char *s)
{
    char *d;
    size_t n;
    if (!s) {
        return NULL;
    }
    n = strlen(s) + 1;
    d = (char *)malloc(n);
    if (d) {
        memcpy(d, s, n);
    }
    return d;
}

static Hint *find(const char *name)
{
    int i;
    for (i = 0; i < nhints; i++) {
        if (strcmp(hints[i].name, name) == 0) {
            return &hints[i];
        }
    }
    return NULL;
}

SDL_bool SDL_SetHintWithPriority(const char *name, const char *value,
                                 SDL_HintPriority priority)
{
    Hint *h;
    char *v;
    if (!name) {
        return SDL_FALSE;
    }
    if (priority < SDL_HINT_OVERRIDE && getenv(name)) {
        return SDL_FALSE;
    }
    h = find(name);
    if (h && priority < h->priority) {
        return SDL_FALSE;
    }
    v = copy(value);
    if (value && !v) {
        SDL_OutOfMemory();
        return SDL_FALSE;
    }
    if (!h) {
        if (nhints == MAX_HINTS) {
            free(v);
            SDL_SetError("More than %d hints", MAX_HINTS);
            return SDL_FALSE;
        }
        h = &hints[nhints];
        h->name = copy(name);
        if (!h->name) {
            free(v);
            SDL_OutOfMemory();
            return SDL_FALSE;
        }
        nhints++;
    } else {
        free(h->value);
    }
    h->value = v;
    h->priority = priority;
    return SDL_TRUE;
}

SDL_bool SDL_SetHint(const char *name, const char *value)
{
    return SDL_SetHintWithPriority(name, value, SDL_HINT_NORMAL);
}

const char *SDL_GetHint(const char *name)
{
    const char *env;
    Hint *h;
    if (!name) {
        return NULL;
    }
    env = getenv(name);
    h = find(name);
    if (h && (!env || h->priority == SDL_HINT_OVERRIDE)) {
        return h->value;
    }
    return env;
}

static int is_false(const char *v)
{
    static const char f[] = "false";
    int i;
    if (v[0] == '0') {
        return 1;
    }
    for (i = 0; f[i]; i++) {
        if ((v[i] | 0x20) != f[i]) {
            return 0;
        }
    }
    return v[i] == '\0';
}

SDL_bool SDL_GetHintBoolean(const char *name, SDL_bool default_value)
{
    const char *v = SDL_GetHint(name);
    if (!v || !*v) {
        return default_value;
    }
    return is_false(v) ? SDL_FALSE : SDL_TRUE;
}

void SDL_ClearHints(void)
{
    int i;
    for (i = 0; i < nhints; i++) {
        free(hints[i].name);
        free(hints[i].value);
    }
    nhints = 0;
}
