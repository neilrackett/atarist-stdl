/*
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: CC0-1.0
 *
 * STDL example program, dedicated to the public domain so it can
 * be used as a starting point without licence concerns.
 */
/*
 * sfxdemo - YM effects coexisting with YM music, plus a Degas
 * splash and joystick key emulation.
 *
 * STDL native example exercising the v1 game-services set:
 *  - STDL_ShowDegas splash while "loading"
 *  - STDL_Music playing DEMO.STM in the background
 *  - STDL_SpeakerOn: an immediate tone steals voice A (the melody)
 *    for a second, then the music voice is restored mid-stream
 *  - STDL_PlaySfx: a descending zap and a noise explosion on
 *    auto-allocated voices over the music
 *  - STDL_JoyKeyEmulation: the joystick quits like a key would
 *
 * Timeline (console prints each step):
 *   0s  splash + music     6s  zap effect (auto voice)
 *   3s  speaker 1kHz       8s  noise explosion
 *   4s  speaker off       12s  done
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdl/stdl.h>

/* descending zap: 2000Hz -> 250Hz over half a second */
static uint16_t zap_periods[25];
static const STDL_Sfx zap = {
    zap_periods, NULL, 25, 13, 20, 0
};

/* noise explosion: fixed noise, fading volume */
static const uint16_t boom_periods[12] = {
    28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28
};
static const uint8_t boom_volumes[12] = {
    15, 15, 14, 13, 12, 10, 8, 7, 5, 4, 2, 1
};
static const STDL_Sfx boom = {
    boom_periods, boom_volumes, 12, 15, 40, 28
};

static void wait_until(uint32_t ms)
{
    STDL_Event e;

    while (STDL_GetTicks() < ms) {
        while (STDL_PollEvent(&e)) {
            if (e.type == STDL_KEYDOWN
                && e.key.keysym.sym == STDLK_ESCAPE) {
                printf("(escape)\n");
                STDL_Quit();
                exit(0);
            }
        }
        STDL_Delay(10);
    }
}

int main(int argc, char *argv[])
{
    STDL_Music *music;
    int i;

    if (STDL_Init(STDL_INIT_VIDEO | STDL_INIT_AUDIO) < 0) {
        fprintf(stderr, "init failed: %s\n", STDL_GetError());
        return 1;
    }
    if (STDL_SetVideoMode(320, 200, 4, 0) == NULL) {
        fprintf(stderr, "video failed: %s\n", STDL_GetError());
        return 1;
    }
    if (STDL_ShowDegas("SPLASH.PI1") < 0) {
        printf("(no splash: %s)\n", STDL_GetError());
    }
    STDL_JoyKeyEmulation(1);    /* stick = arrows + Alt = keys */

    for (i = 0; i < 25; i++) {
        zap_periods[i] = STDL_YM_PERIOD(2000 - i * 70);
    }

    music = STDL_LoadMusic("DEMO.STM");
    if (music == NULL) {
        fprintf(stderr, "music failed: %s\n", STDL_GetError());
        STDL_Quit();
        return 1;
    }
    printf("music on\n");
    STDL_PlayMusic(music, -1);

    wait_until(3000);
    printf("speaker 1kHz (steals the melody voice)\n");
    STDL_SpeakerOn(1000, 13);

    wait_until(4000);
    printf("speaker off (melody voice restored)\n");
    STDL_SpeakerOff();

    wait_until(6000);
    printf("zap (voice %d)\n", STDL_PlaySfx(&zap, -1));

    wait_until(8000);
    printf("boom (voice %d)\n", STDL_PlaySfx(&boom, -1));

    wait_until(12000);
    printf("done\n");
    STDL_HaltMusic();
    STDL_FreeMusic(music);
    STDL_Quit();
    return 0;
}
