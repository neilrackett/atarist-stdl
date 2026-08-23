/* SPDX-License-Identifier: BSD-2-Clause */
/* SPDX-FileCopyrightText: 2026 Neil Rackett */

/*
 * Xpad - the provider half: owning a block and publishing it.
 *
 * Additive to xpad.c, not a replacement for it. A provider links both,
 * and wants to anyway: xpad_find() is how it checks whether something
 * better is already installed before displacing it.
 *
 * A consumer links only xpad.c and carries none of this.
 */

#include <mint/osbind.h>
#include <string.h>

#include "xpad.h"

/* Supexec takes no argument, so a value going in needs a scratch slot.
 * Values coming back travel as the routine's return value. */
static XPAD *xpad_super_arg;

/* ------------------------------------------------------------------ */
/* Cookie jar                                                          */
/* ------------------------------------------------------------------ */

static long xpad_super_publish(void)
{
    uint32_t *slot = xpad_jar_seek();
    uint32_t slots;

    if (!slot)
        return 0;

    if (slot[0]) /* cookie already present, repoint it */
    {
        slot[1] = (uint32_t)xpad_super_arg;
        return 1;
    }

    slots = slot[1]; /* terminator holds the jar capacity */

    if (slots < 2)
        return 0; /* jar is full, caller must enlarge it */

    slot[2] = 0; /* new terminator */
    slot[3] = slots - 1;
    slot[0] = XPAD_COOKIE;
    slot[1] = (uint32_t)xpad_super_arg;

    return 1;
}

static long xpad_super_unpublish(void)
{
    uint32_t *slot = xpad_jar_seek();

    if (!slot || !slot[0])
        return 0;

    /* Shuffle the remainder down, terminator included. */
    for (;;)
    {
        slot[0] = slot[2];
        slot[1] = slot[3];

        if (!slot[0]) /* the terminator, just copied */
            break;

        slot += 2;
    }

    /* Give the vacated pair back. The terminator carries the jar's free
     * slot count and the shuffle above only moved the old one down, so
     * without this a TSR that installs and removes repeatedly walks the
     * count to zero and publishing starts failing. */
    slot[1]++;

    return 1;
}

/* ------------------------------------------------------------------ */
/* Provider                                                            */
/* ------------------------------------------------------------------ */

void xpad_init(XPAD *x, uint8_t pad_count, uint16_t caps,
               const char *provider, XPAD_REQ *req)
{
    /* Hold the header's 1..XPAD_MAX_PADS invariant here, where it can
     * still be honoured. Published out of range it is xpad_find() that
     * refuses, and the provider sees only every consumer failing to
     * find it. */
    if (!pad_count)
        pad_count = 1;
    else if (pad_count > XPAD_MAX_PADS)
        pad_count = XPAD_MAX_PADS;

    memset(x, 0, sizeof(XPAD));

    x->magic = XPAD_MAGIC;
    x->version = XPAD_VERSION;
    x->hdr_size = sizeof(XPAD);
    x->pads_offset = (uint16_t)((uint8_t *)&x->pads[0][0] - (uint8_t *)x);
    x->pad_size = sizeof(XPAD_PAD);
    x->caps = caps;
    x->pad_count = pad_count;
    x->provider = provider;
    x->req = req;

    if (req)
    {
        memset(req, 0, sizeof(XPAD_REQ));
        req->size = sizeof(XPAD_REQ);
    }
}

XPAD_PAD *xpad_back(XPAD *x)
{
    /* Derived through XPAD_PAD_AT() so the provider cannot drift from
     * the stride consumers use. Discarding const is safe: the provider
     * owns the block. */
    return (XPAD_PAD *)XPAD_PAD_AT(x, x->active ^ 1, 0);
}

void xpad_commit(XPAD *x)
{
    x->seq++;
    x->active ^= 1; /* single byte, atomic on 68000 */
}

void xpad_fold_stick(XPAD_PAD *pad, int8_t x, int8_t y, uint8_t threshold)
{
    uint16_t ax = (uint16_t)(x < 0 ? -x : x);
    uint16_t ay = (uint16_t)(y < 0 ? -y : y);
    uint16_t t = threshold;

    /* Radial test, reaching the diagonals a box deadzone would miss.
     * Keep all three operands 16 bit: widening any of them turns a
     * MULU.W into a __mulsi3 libcall on 68000. */
    if (ax * ax + ay * ay < t * t)
        return;

    if (ax > (ay >> 1))
        pad->buttons |= (x < 0) ? XPAD_LEFT : XPAD_RIGHT;

    if (ay > (ax >> 1))
        pad->buttons |= (y < 0) ? XPAD_UP : XPAD_DOWN;
}

int xpad_publish(XPAD *x)
{
    xpad_super_arg = x;

    return Supexec(xpad_super_publish) ? 1 : 0;
}

int xpad_unpublish(void)
{
    return Supexec(xpad_super_unpublish) ? 1 : 0;
}
