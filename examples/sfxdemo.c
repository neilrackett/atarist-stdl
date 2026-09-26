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
 *  - per-step noise (`noises`): a gunshot, a noise crack over a
 *    tone thump on one voice, and an explosion whose noise sinks
 *    as it fades
 *  - STDL_JoyKeyEmulation: the joystick quits like a key would
 *
 * Timeline (console prints each step):
 *   0s  splash + music     6s  zap effect (auto voice)
 *   3s  speaker 1kHz       8s  noise explosion
 *   4s  speaker off       10s  three gunshots
 *                         11s  sinking explosion
 *                         14s  done
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdl/stdl.h>

/* descending zap: 2000Hz -> 250Hz over half a second */
static uint16_t zap_periods[25];
static const STDL_Sfx zap = {
    zap_periods, NULL, 25, 13, 20, 0, NULL
};

/* noise explosion: fixed noise, fading volume */
static const uint16_t boom_periods[12] = {
    28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28
};
static const uint8_t boom_volumes[12] = {
    15, 15, 14, 13, 12, 10, 8, 7, 5, 4, 2, 1
};
static const STDL_Sfx boom = {
    boom_periods, boom_volumes, 12, 15, 40, 28, NULL
};

/* Gunshot, with per-step noise: a bright crack of noise over a low
 * tone thump for the first steps, then the noise alone dying away.
 * Tone and noise share the voice, so it costs the music one voice,
 * not two. */
static const uint16_t shot_periods[6] = { 1200, 1500, 0, 0, 0, 0 };
static const uint8_t shot_noises[6] = { 3, 5, 8, 11, 14, 17 };
static const uint8_t shot_volumes[6] = { 15, 13, 11, 8, 5, 2 };
static const STDL_Sfx shot = {
    shot_periods, shot_volumes, 6, 0, 20, 0, shot_noises
};

/* Explosion, with per-step noise: the noise period climbs as the
 * volume falls, so the roar sinks into a rumble instead of fading
 * at one pitch. No tone at all - periods of 0. */
static const uint16_t sink_periods[20];
static const uint8_t sink_noises[20] = {
    4, 6, 8, 10, 12, 14, 16, 18, 20, 21,
    22, 23, 24, 25, 26, 27, 28, 29, 30, 31
};
static const uint8_t sink_volumes[20] = {
    15, 15, 15, 14, 14, 13, 13, 12, 11, 10,
    9, 8, 7, 6, 5, 4, 3, 2, 2, 1
};
static const STDL_Sfx sink = {
    sink_periods, sink_volumes, 20, 0, 60, 0, sink_noises
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

    for (i = 0; i < 3; i++) {
        wait_until(10000 + i * 300);
        printf("gunshot (voice %d)\n", STDL_PlaySfx(&shot, -1));
    }

    wait_until(11000);
    printf("sinking explosion (voice %d)\n", STDL_PlaySfx(&sink, -1));

    wait_until(14000);
    printf("done\n");
    STDL_HaltMusic();
    STDL_FreeMusic(music);
    STDL_Quit();
    return 0;
}
