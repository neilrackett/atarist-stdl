/* SPDX-License-Identifier: BSD-2-Clause */
/* SPDX-FileCopyrightText: 2026 Neil Rackett */

/*
 * Xpad - extended controller input for the Atari ST family
 *
 * A transport-neutral shared state block, published via the cookie jar,
 * that exposes every button, stick and trigger of a modern gamepad to
 * native ST software.
 *
 * A single provider (a TSR, a cartridge-port device driver, a MIDI
 * adapter, a test stub under Hatari) owns the block and refreshes it.
 * Any number of consumers poll it. Consumers never need to know which
 * transport is in use.
 *
 * The block is read-only to consumers, with the sole exception of the
 * optional request area reached through XPAD.req.
 */

#ifndef XPAD_H
#define XPAD_H

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /* ------------------------------------------------------------------ */
    /* Identification                                                      */
    /* ------------------------------------------------------------------ */

#define XPAD_COOKIE 0x58504144UL /* 'XPAD' cookie jar id        */
#define XPAD_MAGIC 0x58504144UL  /* 'XPAD' in XPAD.magic        */

#define XPAD_VER_MAJOR 1
#define XPAD_VER_MINOR 0
#define XPAD_VERSION ((XPAD_VER_MAJOR << 8) | XPAD_VER_MINOR)

#define XPAD_MAX_PADS 4

    /* ------------------------------------------------------------------ */
    /* Button bitmask                                                      */
    /* ------------------------------------------------------------------ */

    /*
     * Positional names are primary. Bit order and the legend aliases below
     * follow linux/input-event-codes.h, so this maps one to one onto the
     * kernel's gamepad codes.
     *
     * Providers report the physical position of a button, not its printed
     * legend, so a Nintendo-style pad still sets XPAD_SOUTH for its lower
     * face button.
     *
     *   !! The kernel aliases BTN_X to north and BTN_Y to west, which is
     *   !! the reverse of a physical Xbox pad and of the W3C Gamepad API
     *   !! standard mapping. XPAD_X and XPAD_Y reproduce that faithfully.
     *   !! Prefer the positional names in new code; reach for the legend
     *   !! aliases only when mirroring kernel-side definitions.
     */

#define XPAD_UP 0x00000001UL
#define XPAD_DOWN 0x00000002UL
#define XPAD_LEFT 0x00000004UL
#define XPAD_RIGHT 0x00000008UL

#define XPAD_SOUTH 0x00000010UL /* BTN_SOUTH  / BTN_A        */
#define XPAD_EAST 0x00000020UL  /* BTN_EAST   / BTN_B        */
#define XPAD_NORTH 0x00000040UL /* BTN_NORTH  / BTN_X        */
#define XPAD_WEST 0x00000080UL  /* BTN_WEST   / BTN_Y        */

#define XPAD_TL 0x00000100UL  /* left shoulder             */
#define XPAD_TR 0x00000200UL  /* right shoulder            */
#define XPAD_TL2 0x00000400UL /* left trigger, digitised   */
#define XPAD_TR2 0x00000800UL /* right trigger, digitised  */

#define XPAD_SELECT 0x00001000UL /* Select / Back / View      */
#define XPAD_START 0x00002000UL  /* Start / Menu / Options    */
#define XPAD_MODE 0x00004000UL   /* Xbox / Home / PS          */

#define XPAD_THUMBL 0x00008000UL /* left stick click          */
#define XPAD_THUMBR 0x00010000UL /* right stick click         */

    /* bits 17..31 reserved, must be zero */

#define XPAD_DPAD (XPAD_UP | XPAD_DOWN | XPAD_LEFT | XPAD_RIGHT)
#define XPAD_FACE (XPAD_SOUTH | XPAD_EAST | XPAD_NORTH | XPAD_WEST)

    /* Legend aliases, exactly as the kernel defines them. Read the warning
     * above before using XPAD_X or XPAD_Y. */

#define XPAD_A XPAD_SOUTH
#define XPAD_B XPAD_EAST
#define XPAD_X XPAD_NORTH
#define XPAD_Y XPAD_WEST

#define XPAD_LB XPAD_TL
#define XPAD_RB XPAD_TR
#define XPAD_LT XPAD_TL2
#define XPAD_RT XPAD_TR2

#define XPAD_BACK XPAD_SELECT
#define XPAD_GUIDE XPAD_MODE
#define XPAD_L3 XPAD_THUMBL
#define XPAD_R3 XPAD_THUMBR

    /* Kernel spells the directions BTN_DPAD_*; kept low here because ST
     * software cares about them more than anything else. */

#define XPAD_DPAD_UP XPAD_UP
#define XPAD_DPAD_DOWN XPAD_DOWN
#define XPAD_DPAD_LEFT XPAD_LEFT
#define XPAD_DPAD_RIGHT XPAD_RIGHT

    /* ------------------------------------------------------------------ */
    /* Pad type and per-pad flags                                          */
    /* ------------------------------------------------------------------ */

#define XPAD_TYPE_NONE 0     /* nothing connected in this slot     */
#define XPAD_TYPE_JOYSTICK 1 /* digital stick, one button          */
#define XPAD_TYPE_GAMEPAD 2  /* digital pad, unknown family        */
#define XPAD_TYPE_XBOX 3
#define XPAD_TYPE_PLAYSTATION 4
#define XPAD_TYPE_NINTENDO 5
#define XPAD_TYPE_KEYBOARD 6 /* synthesised from the ST keyboard   */

#define XPAD_PAD_ANALOG 0x01 /* stick and trigger fields valid */
#define XPAD_PAD_WIRELESS 0x02
#define XPAD_PAD_LOWBATT 0x04

    /* ------------------------------------------------------------------ */
    /* Provider capabilities                                               */
    /* ------------------------------------------------------------------ */

#define XPAD_CAP_ANALOG 0x0001  /* at least one pad reports axes  */
#define XPAD_CAP_RUMBLE 0x0002  /* req->rumble is honoured        */
#define XPAD_CAP_LED 0x0004     /* req->led is honoured           */
#define XPAD_CAP_HOTPLUG 0x0008 /* pad type may change at runtime */

    /* ------------------------------------------------------------------ */
    /* Layout                                                              */
    /* ------------------------------------------------------------------ */

    /*
     * Axes are signed, -127..127, screen oriented: +x is right, +y is down.
     * Triggers are unsigned, 0..255.
     *
     * Providers fold stick direction into the d-pad bits after applying a
     * deadzone, so a consumer that only wants digital directions can ignore
     * the analogue fields entirely.
     */

    typedef struct
    {
        uint32_t buttons;
        int8_t lx, ly;  /* left stick                        */
        int8_t rx, ry;  /* right stick                       */
        uint8_t lt, rt; /* analogue triggers                 */
        uint8_t type;   /* XPAD_TYPE_*, 0 when disconnected  */
        uint8_t flags;  /* XPAD_PAD_*                        */
    } XPAD_PAD;         /* XPAD_PAD_SIZE_V1 bytes            */

    /*
     * The v1 pad size, frozen. Every major version 1 provider publishes
     * at least this much, so pad_size below it means a broken provider
     * rather than an older one, and xpad_find() refuses the block.
     * A later revision may grow XPAD_PAD, which raises pad_size above
     * this; it never lowers it.
     */
#define XPAD_PAD_SIZE_V1 12

    /*
     * Bytes of XPAD that are frozen on every architecture: magic through
     * active. Everything after that holds pointers, whose size and
     * alignment differ between the ST and a modern host, which is why
     * hdr_size and pads_offset are published rather than assumed.
     */
#define XPAD_HDR_FIXED 18

    /*
     * Optional request area. Writable by consumers, polled by the provider.
     * Reached through XPAD.req, which is NULL when unsupported. Bump seq
     * after writing so the provider can spot a change cheaply.
     */

    typedef struct
    {
        uint16_t size; /* sizeof(XPAD_REQ)      */
        uint8_t seq;
        uint8_t reserved;
        uint8_t rumble[XPAD_MAX_PADS][2]; /* low, high freq motor  */
        uint8_t led[XPAD_MAX_PADS];       /* player index or hue   */
    } XPAD_REQ;

    /*
     * The published block.
     *
     * pads is double buffered. The provider fills the inactive buffer then
     * writes active, which is a single byte and therefore atomic on 68000.
     * seq is bumped before active flips, which is what makes the
     * consumer's recheck meaningful.
     *
     * The two buffers sit pad_count * pad_size apart, not XPAD_MAX_PADS
     * apart, so a block reporting fewer pads than the maximum leaves the
     * tail of the declared array unused. XPAD_PAD_AT() and xpad_back()
     * both derive that stride, so neither can drift from the other.
     *
     * Consumers must locate pads through pads_offset, pad_size and
     * pad_count rather than indexing the array directly, so that a later
     * revision can grow XPAD_PAD without breaking existing binaries.
     * XPAD_PAD_AT() does this.
     */

    typedef struct
    {
        uint32_t magic;       /* XPAD_MAGIC                        */
        uint16_t version;     /* XPAD_VERSION                      */
        uint16_t hdr_size;    /* sizeof(XPAD)                      */
        uint16_t pads_offset; /* offsetof(XPAD, pads)              */
        uint16_t pad_size;    /* sizeof(XPAD_PAD)                  */
        uint16_t caps;        /* XPAD_CAP_*                        */
        uint16_t seq;         /* incremented on every commit       */
        uint8_t pad_count;    /* slots present, 1..XPAD_MAX_PADS   */
        uint8_t active;       /* front buffer index, 0 or 1        */
        const char *provider; /* NUL terminated, eg "MD/Sidepad 1.1" */
        XPAD_REQ *req;        /* NULL when unsupported             */
        XPAD_PAD pads[2][XPAD_MAX_PADS];
    } XPAD;

/*
 * The index arithmetic stays 16 bit deliberately. A 68000 has no 32-bit
 * multiply, so widening it to int turns this macro into a __mulsi3
 * libcall on the path every consumer polls each frame.
 */
#define XPAD_PAD_AT(x, buf, i)                                                  \
    ((const XPAD_PAD *)((const uint8_t *)(x) + (x)->pads_offset +               \
                        (uint16_t)((uint16_t)(buf) * (uint16_t)(x)->pad_count + \
                                   (uint16_t)(i)) *                             \
                            (uint16_t)(x)->pad_size))

    /* ------------------------------------------------------------------ */
    /* Consumer API                                                        */
    /* ------------------------------------------------------------------ */

    /*
     * Whether a block is one this header can read: right magic, a major
     * version that is understood, and layout fields describing a pad
     * area that fits inside the block. Everything downstream trusts
     * pads_offset, pad_size and pad_count, so they are checked here once
     * rather than on every read.
     *
     * xpad_find() applies this to what it finds. A provider can apply it
     * to a block it has just built, before publishing something no
     * consumer would accept.
     */
    int xpad_valid(const XPAD *x);

    /*
     * Locate the published block. Returns NULL when no provider is
     * present or the block does not satisfy xpad_valid(). Safe to call
     * from user mode; uses Supexec internally. Call once at startup and
     * cache the result.
     */
    const XPAD *xpad_find(void);

    /*
     * Copy one pad's state into out, free of tearing. Returns 1 on success,
     * 0 if the index is out of range or the read could not be made
     * consistent. Fields beyond the provider's pad_size are zeroed, so out
     * is always fully initialised on success.
     */
    int xpad_read(const XPAD *x, int index, XPAD_PAD *out);

    /* Number of slots reporting a connected pad. */
    int xpad_connected(const XPAD *x);

    /*
     * The request area, or NULL when the provider offers none or offers
     * one smaller than this header describes. Reach req through this and
     * never through XPAD.req directly: a consumer built against a later
     * header would otherwise write fields an older provider never
     * allocated, which is the one direction in this design where a
     * consumer can corrupt a provider.
     *
     * A non-NULL return means the memory is there, not that anything
     * acts on it. Check caps for XPAD_CAP_RUMBLE and XPAD_CAP_LED to
     * learn what the provider honours.
     */
    XPAD_REQ *xpad_req(const XPAD *x);

    /* ------------------------------------------------------------------ */
    /* Provider API                                                        */
    /* ------------------------------------------------------------------ */

    /*
     * Initialise a block you own. Fills every descriptive field, clears both
     * buffers and sets active to 0. Does not install the cookie.
     */
    void xpad_init(XPAD *x, uint8_t pad_count, uint16_t caps,
                   const char *provider, XPAD_REQ *req);

    /* The buffer you may write. Never the one consumers are reading. */
    XPAD_PAD *xpad_back(XPAD *x);

    /* Publish the back buffer. Call once per refresh, after filling it. */
    void xpad_commit(XPAD *x);

    /*
     * Apply a radial deadzone to a stick and fold the result into the d-pad
     * bits of buttons. threshold is 0..127, typically 40. Call for each
     * stick you want mapped before xpad_commit().
     */
    void xpad_fold_stick(XPAD_PAD *pad, int8_t x, int8_t y, uint8_t threshold);

    /*
     * Install or remove the XPAD cookie. Returns 1 on success, and 0 if
     * the jar is full: enlarging it is the caller's job, and has to
     * happen before a TSR goes resident. Uses Supexec internally.
     */
    int xpad_publish(XPAD *x);
    int xpad_unpublish(void);

    /* ------------------------------------------------------------------ */
    /* Internal                                                            */
    /* ------------------------------------------------------------------ */

    /*
     * Not part of the ABI. Shared between xpad.c and xpad_provider.c
     * because both halves walk the jar, and duplicating the walk is how
     * the two would drift apart.
     *
     * Returns the slot holding the XPAD cookie, the terminator when the
     * cookie is absent, or 0 when there is no jar. **Supervisor mode
     * only**: the jar pointer lives at 0x5A0 and the ST bus errors on
     * user mode access below 0x800. Call it from inside Supexec, as
     * xpad_find() and xpad_publish() do, or not at all.
     */
    uint32_t *xpad_jar_seek(void);

#ifdef __cplusplus
}
#endif

#endif /* XPAD_H */
