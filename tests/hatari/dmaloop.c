/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * The other half of VOICESIL's question: is an STE's sound DMA
 * quiet when it loops a buffer of silence that nothing ever
 * writes to?
 *
 * STDL_PlaySampleLoop points the hardware straight at this
 * program's buffer and loops it - no ring, no VBL handler, no
 * mixer, nothing touching the bytes after the initial clear. So:
 *
 *   silent       the hardware looping zeroes is fine, and the
 *                residual click in VOICESIL comes from the voice
 *                device's refill - the CPU writing the ring while
 *                the DMA reads it, or when it does. Software.
 *   still clicks the ring content and the refill are both
 *                innocent; it is the DMA unit or the analogue path
 *                with sound enabled at all.
 *
 * It cannot separate a fault inside the mixer from a fault in
 * writing to the ring at all - with no refill there is neither.
 * That needs a third variant refilling with memset only, which is
 * worth building only if this one comes back silent.
 *
 * Green while looping, ESC quits, and there is deliberately no
 * stop key: SPACE did not reach this program under the test
 * harness, the way it does reach VOICESIL, and a control that does
 * not work is worse than no control. The stopped case is
 * VOICESIL's blue state, which is verified.
 *
 * A separate program rather than a state inside VOICESIL because
 * STDL_PlaySampleLoop after STDL_CloseVoices ends that program -
 * cleanly, no exception logged - while the same call from startup
 * runs indefinitely. That handoff is a library bug in its own
 * right; a probe must not depend on the thing it might be
 * measuring.
 */
#include <stddef.h>
#include <string.h>
#include <stdl/stdl.h>

/* static and padded: nothing allocates near it, nothing writes it
 * once the DMA has it, and the even-address alignment below cannot
 * run off the end */
static int8_t silence[512 + 8];

int main(int argc, char *argv[])
{
    STDL_Surface *screen;
    int8_t *buf;
    int playing;

    (void)argc; (void)argv;
    if (STDL_Init(STDL_INIT_VIDEO | STDL_INIT_AUDIO) < 0) {
        return 1;
    }
    screen = STDL_SetVideoMode(320, 200, 4, 0);
    if (screen == NULL) {
        STDL_Quit();
        return 1;
    }
    memset(silence, 0, sizeof silence);
    buf = (int8_t *)(((uintptr_t)silence + 1) & ~(uintptr_t)1);
    playing = (STDL_PlaySampleLoop(buf, 512, 6258) == 0);

    for (;;) {
        STDL_Event ev;
        STDL_Rect r;

        while (STDL_PollEvent(&ev)) {
            if (ev.type == STDL_KEYDOWN) {
                if (ev.key.keysym.sym == STDLK_ESCAPE) {
                    goto done;
                }
            }
        }
        r.x = 0; r.y = 0; r.w = 320; r.h = 200;
        STDL_FillRect(screen, &r, (uint8_t)(playing ? 10 : 12));
        STDL_WaitVBL();
    }
done:
    STDL_StopSample();
    STDL_Quit();
    return 0;
}
