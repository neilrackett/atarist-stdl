/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * Shared YM2149 service: the VBL slot, the mixer-register shadow
 * and the voice-ownership protocol that lets music (music.c) and
 * effects (sfx.c) share the chip. Both sides plug in through
 * function pointers so a program linking only one of them doesn't
 * pull in the other.
 *
 * The service never touches YM registers 14/15 (the I/O ports TOS
 * uses for floppy select) and preserves the port-direction bits
 * whenever it writes the mixer.
 */

#include <mint/osbind.h>
#include "stdl_internal.h"

#define YM_SELECT (*(volatile uint8_t *)0xFFFF8800UL)
#define YM_DATA   (*(volatile uint8_t *)0xFFFF8802UL)

#define NVBLS     (*(volatile uint16_t *)0x454UL)
#define VBLQUEUE  (*(void (***)(void))0x456UL)
#define CONTERM   (*(volatile uint8_t *)0x484UL)

volatile uint8_t stdl_ym_owned;         /* bits 0-2 voices, 3 noise */
void (*stdl_music_tick)(void);
void (*stdl_sfx_tick)(void);
void (*stdl_ym_restore_voice)(int voice);

static uint8_t mix_shadow = 0x3F;       /* all off                  */
static int vbl_slot = -1;
static int old_conterm = -1;

void stdl_ym_write(int reg, int val)
{
    YM_SELECT = (uint8_t)reg;
    YM_DATA = (uint8_t)val;
}

void stdl_ym_mix_update(uint8_t clr, uint8_t set)
{
    mix_shadow = (uint8_t)((mix_shadow & ~clr) | set);
    YM_SELECT = 7;
    stdl_ym_write(7, (YM_SELECT & 0xC0) | (mix_shadow & 0x3F));
}

/* an effect has finished with a voice: hand it back to the music
 * stream (if one is installed and playing) or silence it */
void stdl_ym_release_voice(int voice)
{
    if (voice < 0 || voice > 2) {
        return;
    }
    stdl_ym_owned &= (uint8_t)~(1u << voice);
    if (stdl_ym_restore_voice != NULL) {
        stdl_ym_restore_voice(voice);
    } else {
        stdl_ym_write(8 + voice, 0);
        stdl_ym_mix_update(STDL_YM_VOICE_BITS(voice),
                           STDL_YM_VOICE_BITS(voice));
    }
}

/* VBL queue entry (TOS saves registers around queue calls). Music
 * steps first, effects after, so effect voices always win the
 * frame. */
static void ym_vbl(void)
{
    if (stdl_music_tick != NULL) {
        stdl_music_tick();
    }
    if (stdl_sfx_tick != NULL) {
        stdl_sfx_tick();
    }
}

static void ym_shutdown(void)
{
    int v;

    if (vbl_slot >= 0) {
        VBLQUEUE[vbl_slot] = NULL;
        vbl_slot = -1;
    }
    stdl_music_tick = NULL;
    stdl_sfx_tick = NULL;
    stdl_ym_restore_voice = NULL;
    stdl_ym_owned = 0;
    for (v = 0; v < 3; v++) {
        stdl_ym_write(8 + v, 0);
    }
    stdl_ym_mix_update(0x3F, 0x3F);
    if (old_conterm >= 0) {
        CONTERM = (uint8_t)old_conterm;
        old_conterm = -1;
    }
}

/* claim the shared YM VBL slot (music and effects both call this) */
int stdl_ym_install(void)
{
    int i;

    if (vbl_slot >= 0) {
        return 0;
    }
    if (!stdl.initialised) {
        STDL_Init(0);
    }
    /* silence the console key click so it cannot fight over the
     * sound chip; key repeat stays on */
    old_conterm = CONTERM;
    CONTERM = (uint8_t)(old_conterm & ~1);

    for (i = 0; i < NVBLS; i++) {
        if (VBLQUEUE[i] == NULL) {
            VBLQUEUE[i] = ym_vbl;
            vbl_slot = i;
            stdl_shutdown_music = ym_shutdown;
            return 0;
        }
    }
    CONTERM = (uint8_t)old_conterm;
    old_conterm = -1;
    STDL_SetError("no free VBL queue slot for sound");
    return -1;
}
