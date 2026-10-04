/*
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: CC0-1.0
 *
 * STDL example program, dedicated to the public domain so it can
 * be used as a starting point without licence concerns.
 */
/*
 * musicvu - YM music from memory, with a VU meter, a position bar
 * and seeking.
 *
 * The stream is played where it lies with STDL_MusicFromMemory. A
 * demo would link it in (stdlconv's C-array output) and be one file;
 * this reads DEMO.STM into a buffer, which is the same call. The
 * three bars are the voices' volumes from STDL_GetMusicRegisters -
 * the stream's own registers, so drawing them never touches the
 * chip - and the line along the bottom is STDL_MusicFrame against
 * STDL_MusicLength. Sync effects to the music's frame, not to a
 * counter of your own: the music's tick is what the listener hears.
 *
 * LEFT and RIGHT jump five seconds (STDL_SeekMusic: every voice is
 * right straight away, not just the ones the next frame changes),
 * SPACE pauses, ESC quits.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdl/stdl.h>

#define SEEK_FRAMES 250         /* five seconds of a 50Hz stream */

static void *load_file(const char *name, uint32_t *bytes)
{
    FILE *f = fopen(name, "rb");
    void *buf = NULL;
    long n;

    if (f == NULL) {
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n > 0 && (buf = malloc((size_t)n)) != NULL
        && fread(buf, 1, (size_t)n, f) != (size_t)n) {
        free(buf);
        buf = NULL;
    }
    fclose(f);
    *bytes = (uint32_t)n;
    return buf;
}

int main(int argc, char *argv[])
{
    static const STDL_Colour pal[4] = {
        { 0, 0, 0, 0 }, { 240, 64, 32, 0 }, { 64, 224, 64, 0 },
        { 64, 96, 255, 0 }
    };
    STDL_Surface *s;
    STDL_Music *m;
    uint32_t bytes;
    void *stm;
    int len, quit = 0;

    (void)argc;
    (void)argv;
    s = STDL_SetVideoMode(320, 200, 4, STDL_DOUBLEBUF);
    if (s == NULL) {
        fprintf(stderr, "video: %s\n", STDL_GetError());
        return 1;
    }
    STDL_SetColours(s, pal, 0, 4);
    stm = load_file("DEMO.STM", &bytes);
    m = stm != NULL ? STDL_MusicFromMemory(stm, bytes) : NULL;
    if (m == NULL || STDL_PlayMusic(m, -1) < 0) {
        STDL_Quit();
        fprintf(stderr, "DEMO.STM: %s\n", STDL_GetError());
        return 1;
    }
    len = STDL_MusicLength(m);

    while (!quit) {
        STDL_Event e;
        uint8_t regs[14];
        STDL_Rect r;
        int v, at;

        while (STDL_PollEvent(&e)) {
            if (e.type != STDL_KEYDOWN) {
                continue;
            }
            at = STDL_MusicFrame();
            switch (e.key.keysym.sym) {
            case STDLK_ESCAPE:
                quit = 1;
                break;
            case STDLK_LEFT:
                STDL_SeekMusic(at > SEEK_FRAMES ? at - SEEK_FRAMES : 0);
                break;
            case STDLK_RIGHT:
                STDL_SeekMusic(at + SEEK_FRAMES < len ? at + SEEK_FRAMES
                                                      : len - 1);
                break;
            case STDLK_SPACE:
                if (STDL_PausedMusic()) {
                    STDL_ResumeMusic();
                } else {
                    STDL_PauseMusic();
                }
                break;
            default:
                break;
            }
        }

        STDL_FillRect(s, NULL, 0);
        STDL_GetMusicRegisters(regs);
        for (v = 0; v < 3; v++) {
            /* bit 4: the voice follows the envelope - show it full */
            int level = (regs[8 + v] & 0x10) ? 15 : (regs[8 + v] & 15);

            r.x = (int16_t)(48 + v * 80);
            r.w = 64;
            r.h = (uint16_t)(level * 10);
            r.y = (int16_t)(170 - r.h);
            STDL_FillRect(s, &r, (uint8_t)(1 + v));
        }
        at = STDL_MusicFrame();
        r.x = 0;
        r.y = 186;
        r.w = STDL_DivU16(STDL_MulU16(at, 320), (unsigned)len);
        r.h = 6;
        STDL_FillRect(s, &r, 2);
        STDL_Flip();
    }

    STDL_HaltMusic();
    STDL_FreeMusic(m);
    free(stm);
    STDL_Quit();
    return 0;
}
