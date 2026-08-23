/* SPDX-License-Identifier: BSD-2-Clause */
/* SPDX-FileCopyrightText: 2026 Neil Rackett */

/*
 * Xpad - the consumer half: finding a published block and reading it.
 *
 * All a game needs. A provider links xpad_provider.c as well; the two
 * are separate objects because there is no section garbage collection
 * on m68k-atari-mint, so a consumer that linked both would carry the
 * publishing helpers into every binary without ever calling them.
 *
 * Free of dependencies beyond osbind.h so it drops into a game loop, a
 * TSR or a device driver unchanged.
 */

#include <mint/osbind.h>
#include <string.h>

#include "xpad.h"

/* The cookie jar pointer lives at 0x5A0 on an ST. The host harness
 * predefines this to a variable it controls, so the jar walking below is
 * exercised by the tests rather than only by hardware. */
#ifndef XPAD_JAR
#define XPAD_JAR (*(uint32_t **)0x5A0L)
#endif

/* ------------------------------------------------------------------ */
/* Cookie jar                                                          */
/* ------------------------------------------------------------------ */

/*
 * Shared with the provider half, which is why it is not static. See
 * xpad.h for the contract, including the supervisor mode requirement.
 */
uint32_t *xpad_jar_seek(void)
{
    uint32_t *jar = XPAD_JAR;

    if (!jar)
        return 0;

    while (jar[0] && jar[0] != XPAD_COOKIE)
        jar += 2;

    return jar;
}

static long xpad_super_find(void)
{
    uint32_t *slot = xpad_jar_seek();

    if (!slot || !slot[0])
        return 0;

    return (long)slot[1];
}

/* ------------------------------------------------------------------ */
/* Consumer                                                            */
/* ------------------------------------------------------------------ */

int xpad_valid(const XPAD *x)
{
    uint32_t span;

    if (!x || x->magic != XPAD_MAGIC)
        return 0;

    if ((x->version >> 8) != XPAD_VER_MAJOR)
        return 0;

    if (!x->pad_count || x->pad_count > XPAD_MAX_PADS)
        return 0;

    /* A v1 provider publishes at least the v1 pad. Anything smaller is
     * broken rather than old, and the floor is what lets the readers
     * take type and flags from their fixed offsets without checking. */
    if (x->pad_size < XPAD_PAD_SIZE_V1)
        return 0;

    /* The pad area must start past the fields read above and finish
     * inside the block the provider says it has. Checking it once here
     * is what allows XPAD_PAD_AT() to be bare arithmetic on the path
     * consumers walk every frame. Order matters: bound pads_offset
     * before subtracting it, or a wild value wraps the subtraction. */
    if (x->pads_offset < XPAD_HDR_FIXED || x->pads_offset > x->hdr_size)
        return 0;

    /* 32 bit: a later revision may grow pad_size past what 16 would
     * hold here. This runs at discovery, not per frame, so the libcall
     * a 32-bit multiply costs on 68000 does not matter. */
    span = (uint32_t)x->pad_count * 2UL * (uint32_t)x->pad_size;

    return span <= (uint32_t)(x->hdr_size - x->pads_offset);
}

const XPAD *xpad_find(void)
{
    const XPAD *x = (const XPAD *)Supexec(xpad_super_find);

    return xpad_valid(x) ? x : 0;
}

int xpad_read(const XPAD *x, int index, XPAD_PAD *out)
{
    volatile const uint8_t *active;
    volatile const uint16_t *seq;
    const XPAD_PAD *slot[2];
    uint16_t size;
    int tries;

    if (!x || !out || index < 0 || index >= x->pad_count)
        return 0;

    active = &x->active;
    seq = &x->seq;

    /* Both buffer addresses up front. There are only ever two, so the
     * retry loop can select between them instead of repeating
     * XPAD_PAD_AT's multiply on every attempt. */
    slot[0] = XPAD_PAD_AT(x, 0, index);
    slot[1] = XPAD_PAD_AT(x, 1, index);

    size = x->pad_size;
    if (size > sizeof(XPAD_PAD))
        size = sizeof(XPAD_PAD);

    /* Zero only the tail the copy cannot reach, and only once: the copy
     * below rewrites the first size bytes on every attempt. */
    if (size < sizeof(XPAD_PAD))
        memset((uint8_t *)out + size, 0, sizeof(XPAD_PAD) - size);

    for (tries = 3; tries; tries--)
    {
        uint8_t a = *active;
        uint16_t s = *seq;

        /* A provider on this major version always fills the whole pad,
         * so this is the path taken every time until XPAD_PAD grows.
         * Assigning the struct lets the compiler inline the copy; the
         * memcpy is the libcall this exists to avoid. */
        if (size == sizeof(XPAD_PAD))
            *out = *slot[a & 1];
        else
            memcpy(out, slot[a & 1], size);

        if (*active == a && *seq == s)
            return 1;
    }

    return 0;
}

int xpad_connected(const XPAD *x)
{
    const uint8_t *p;
    uint16_t stride;
    int i, n = 0;

    if (!x)
        return 0;

    /* Walk by pad_size instead of calling XPAD_PAD_AT per slot, which
     * would repeat its multiply on every iteration. active is masked
     * because a provider mid-flip is the one field no validation at
     * discovery can pin down. */
    stride = x->pad_size;
    p = (const uint8_t *)XPAD_PAD_AT(x, x->active & 1, 0);

    for (i = 0; i < x->pad_count; i++, p += stride)
    {
        if (((const XPAD_PAD *)p)->type != XPAD_TYPE_NONE)
            n++;
    }

    return n;
}

XPAD_REQ *xpad_req(const XPAD *x)
{
    if (!x || !x->req)
        return 0;

    /* Refuse an area smaller than this header describes rather than let
     * the caller write past what the provider allocated. All or
     * nothing: a partial area would need every field checked at every
     * use, which no caller would keep doing. */
    if (x->req->size < sizeof(XPAD_REQ))
        return 0;

    return x->req;
}
