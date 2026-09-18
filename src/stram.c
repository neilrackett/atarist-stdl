/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * Allocation the Shifter and the sound DMA can reach; see the
 * comment on stdl_stram_alloc in stdl_internal.h for why it has to
 * exist at all.
 */
#include <osbind.h>
#include "stdl_internal.h"

/* top of ST RAM, in supervisor-only low memory */
#define PHYSTOP (*(volatile uint32_t *)0x42EUL)

void *stdl_stram_alloc(uint32_t bytes)
{
    long p;

    /*
     * Mxalloc mode 0 is "ST RAM only". It arrived in TOS 1.04, and
     * on anything older there is no alt-RAM for Malloc to hand back
     * in the first place, so the fallback is safe rather than
     * merely tolerable. GEMDOS 0.19 is TOS 1.04.
     */
    if (Sversion() >= 0x1900) {
        p = Mxalloc((long)bytes, 0);
    } else {
        p = Malloc((long)bytes);
    }
    if (p <= 0) {
        return NULL;
    }
    /*
     * Checked rather than trusted. The mode argument is a request,
     * an old TOS may not honour it, and the cost of being wrong is
     * a screen of garbage with no error - so the one thing worth
     * spending instructions on is proving the block is where it was
     * asked to be. PHYSTOP is the top of ST RAM.
     */
    if ((uint32_t)p + bytes > PHYSTOP) {
        Mfree(p);
        return NULL;
    }
    return (void *)p;
}

int stdl_is_stram(const void *p, uint32_t bytes)
{
    const uint32_t a = (uint32_t)p;

    return a != 0 && a + bytes <= PHYSTOP;
}

void stdl_stram_free(void *p)
{
    if (p != NULL) {
        Mfree((long)p);
    }
}
