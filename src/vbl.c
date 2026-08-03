/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * Public VBL callbacks.
 *
 * STDL's cooperative model has exactly one hole - the 50Hz vertical
 * blank, where the YM sound tick already lives - and ports kept
 * reaching through it by poking the TOS queue at $456 themselves.
 * That works right up to the point the program dies without running
 * its clean-up, and then the queue entry is a pointer into memory
 * GEMDOS has given to somebody else. Here it is as an API, with the
 * removal wired into STDL's shutdown (including the terminate vector,
 * so an abort() cannot leave a live entry behind).
 *
 * Its own translation unit on purpose: the archive links whole
 * objects, so a program that never installs a callback must not pay
 * for the code that would.
 */

#include "stdl_internal.h"

/* Which queue slots are ours. TOS ships eight; the mask is 16 bits
 * and the loops clamp to that, so a patched nvbls cannot overrun it. */
#define VBL_MAX_SLOTS 16

static uint16_t vbl_owned;

static int vbl_slots(void)
{
    int n = STDL_NVBLS;

    return (n > VBL_MAX_SLOTS) ? VBL_MAX_SLOTS : n;
}

/* Runs from restore_all and from the GEMDOS terminate vector: vector
 * writes only, no GEMDOS and no heap. */
static void vbl_shutdown(void)
{
    int i;

    for (i = 0; i < VBL_MAX_SLOTS; i++) {
        if (vbl_owned & (uint16_t)(1u << i)) {
            STDL_VBLQUEUE[i] = NULL;
        }
    }
    vbl_owned = 0;
}

int STDL_AddVBL(void (*fn)(void))
{
    uint16_t sr;
    int i, n;

    if (fn == NULL) {
        STDL_SetError("null VBL callback");
        return -1;
    }
    if (!stdl.initialised && STDL_Init(0) < 0) {
        return -1;
    }
    n = vbl_slots();

    /* a queue slot is a long: the VBL can land between the two word
     * writes that install one, so keep it out of the middle */
    sr = stdl_int_off();
    for (i = 0; i < n; i++) {
        if (STDL_VBLQUEUE[i] == fn) {
            stdl_int_restore(sr);
            return 0;                       /* already installed */
        }
    }
    for (i = 0; i < n; i++) {
        if (STDL_VBLQUEUE[i] == NULL) {
            STDL_VBLQUEUE[i] = fn;
            vbl_owned |= (uint16_t)(1u << i);
            stdl_shutdown_vbl = vbl_shutdown;
            stdl_int_restore(sr);
            return 0;
        }
    }
    stdl_int_restore(sr);
    STDL_SetError("no free VBL queue slot");
    return -1;
}

void STDL_RemoveVBL(void (*fn)(void))
{
    uint16_t sr;
    int i, n;

    if (fn == NULL || !stdl.initialised) {
        return;
    }
    n = vbl_slots();
    sr = stdl_int_off();
    for (i = 0; i < n; i++) {
        if (STDL_VBLQUEUE[i] == fn) {
            STDL_VBLQUEUE[i] = NULL;
            vbl_owned &= (uint16_t)~(1u << i);
        }
    }
    stdl_int_restore(sr);
}
