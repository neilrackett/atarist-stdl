/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * STDL_Event: IKBD-driven input.
 *
 * The TOS ikbdsys vector is replaced with a raw packet parser, which
 * is the only way to see key release events on the ST. The interrupt
 * handler does the minimum - it latches bytes into a ring buffer and
 * accumulators - and STDL_PumpEvents turns them into SDL 1.2-shaped
 * events with keysym translation from the TOS keyboard tables.
 *
 * While STDL owns the keyboard the BIOS console gets no input
 * (printf output still works); everything is restored on quit.
 */

#include <mint/osbind.h>
#include <string.h>

#include "stdl_internal.h"
#include "stdl_xpad.h"

#ifdef __m68k__
#define ACIA_KBD_CTRL   (*(volatile uint8_t *)0xFFFFFC00UL)
#define ACIA_KBD_RX     (*(volatile uint8_t *)0xFFFFFC02UL)
#define ACIA_KBD_TX(b)  (*(volatile uint8_t *)0xFFFFFC02UL = (b))
#define SETTLE_CLOCK    STDL_HZ200
#else
/* host tests (tests/host/test_joystick.c): the ACIA is a byte stream
 * the test feeds and a log of the commands sent, and the quiesce's
 * clock moves on as it is read, so its wait ends */
uint8_t stdl_host_acia_ctrl(void);
uint8_t stdl_host_acia_rx(void);
void stdl_host_acia_tx(uint8_t b);
#define ACIA_KBD_CTRL   stdl_host_acia_ctrl()
#define ACIA_KBD_RX     stdl_host_acia_rx()
#define ACIA_KBD_TX(b)  stdl_host_acia_tx(b)
#define SETTLE_CLOCK    (++stdl_host_clock)
#endif

/* ---------------------------------------------------------------- */
/* interrupt-side state                                             */

#define RING_SIZE 64            /* power of two */
#define RING_MASK (RING_SIZE - 1)

#define TAG_KEY      0x0000     /* low byte: scancode | break bit    */
#define TAG_MOUSEBTN 0x0100     /* low byte: IKBD button bits        */
#define TAG_JOY      0x0200     /* low byte: joystick 1 state        */
#define TAG_JOY0     0x0300     /* low byte: joystick 0, the mouse
                                   port, which is STDL's joystick 1
                                   where a program asked for two
                                   (src/joysticks.c); dropped where
                                   it did not */

static volatile uint16_t stdl_ring[RING_SIZE];
static volatile uint8_t  stdl_ring_head;   /* ISR writes here */
static volatile uint8_t  stdl_ring_tail;   /* pump reads here */
/* whether the IKBD should report the mouse at all; see
 * STDL_EnableMouse in src/mouse.c */
uint8_t stdl_mouse_on = 1;
static volatile int16_t  stdl_mouse_dx;
static volatile int16_t  stdl_mouse_dy;

static volatile uint8_t pkt_pending;   /* bytes left in packet */
static volatile uint8_t pkt_type;
static volatile uint8_t pkt_idx;
static volatile uint8_t pkt_buf[8];
static volatile uint8_t mouse_buttons_isr;

static void ring_push(uint16_t v)
{
    uint8_t next = (uint8_t)((stdl_ring_head + 1) & RING_MASK);
    if (next != stdl_ring_tail) {
        stdl_ring[stdl_ring_head] = v;
        stdl_ring_head = next;
    }
}

static uint8_t packet_len(uint8_t header)
{
    switch (header) {
        case 0xF6: return 7;    /* status                     */
        case 0xF7: return 5;    /* absolute mouse             */
        case 0xF8: case 0xF9:
        case 0xFA: case 0xFB: return 2;   /* relative mouse   */
        case 0xFC: return 6;    /* time of day                */
        case 0xFD: return 2;    /* both joysticks             */
        case 0xFE: case 0xFF: return 1;   /* joystick event   */
        default:   return 0;
    }
}

/* called (via the asm stub below) with the keyboard ACIA holding
 * at least one byte; runs in interrupt context */
void stdl_ikbd_process(void)
{
    while (ACIA_KBD_CTRL & 0x01) {
        uint8_t b = ACIA_KBD_RX;

        if (pkt_pending != 0) {
            pkt_buf[pkt_idx++] = b;
            if (--pkt_pending == 0) {
                switch (pkt_type) {
                    case 0xF8: case 0xF9: case 0xFA: case 0xFB: {
                        uint8_t btn = pkt_type & 3;
                        stdl_mouse_dx += (int8_t)pkt_buf[0];
                        stdl_mouse_dy += (int8_t)pkt_buf[1];
                        if (btn != mouse_buttons_isr) {
                            mouse_buttons_isr = btn;
                            ring_push((uint16_t)(TAG_MOUSEBTN | btn));
                        }
                        break;
                    }
                    case 0xFD:      /* both: port 0, then port 1 */
                    case 0xFE:
                        /* One push for the two: ring_push is
                         * inlined at each call, and a push apiece
                         * made the handler 140 bytes longer where
                         * this makes it 90. */
                        ring_push((uint16_t)(TAG_JOY0 | pkt_buf[0]));
                        if (pkt_type == 0xFE) {
                            break;
                        }
                        pkt_buf[0] = pkt_buf[1];
                        /* fall through */
                    case 0xFF:
                        ring_push((uint16_t)(TAG_JOY | pkt_buf[0]));
                        break;
                    default:
                        break;  /* status / absolute / clock: drop */
                }
            }
        } else if (b >= 0xF6) {
            pkt_type = b;
            pkt_idx = 0;
            pkt_pending = packet_len(b);
        } else {
            ring_push((uint16_t)(TAG_KEY | b));
        }
    }
}

void stdl_ikbd_handler(void);
#ifdef __m68k__
__asm__(
    "\t.text\n"
    "_stdl_ikbd_handler:\n"
    "\tmovem.l %d0-%d1/%a0-%a1,-(%sp)\n"
    "\tjsr _stdl_ikbd_process\n"
    "\tmovem.l (%sp)+,%d0-%d1/%a0-%a1\n"
    "\trts\n");
#else
void stdl_ikbd_handler(void)
{
    stdl_ikbd_process();
}
#endif

/* ---------------------------------------------------------------- */
/* install / remove                                                 */

typedef struct {
    long midivec, vkbderr, vmiderr, statvec;
    long mousevec, clockvec, joyvec, midisys, ikbdsys;
} kbdvecs_t;

static long old_ikbdsys;
int stdl_events_installed;

/* How many joysticks a program reads, and the code behind the second
 * to the fourth: STDL_SetNumJoysticks (src/joysticks.c) sets both, so
 * a program that never asks has one and links none of that code. */
uint8_t stdl_joy_count = 1;
const stdl_joyn_ops *stdl_joyn;

/* whether device events are wanted (STDL_UseJoystickDeviceEvents,
 * src/joydev.c): 0 until a program asks */
uint8_t stdl_joy_devices;

/* The joystick has two possible sources and they merge: a stick on the
 * port, and a controller published through Xpad. Kept apart so one
 * releasing cannot clear a direction the other is holding. */
static uint8_t  joy_ikbd;           /* from the packet parser        */
static uint8_t  joy_xpad;           /* from an Xpad provider         */
static int16_t  joy_axis[2];        /* last value reported per axis  */

/* TOS keyboard translation tables, captured before takeover */
static const uint8_t *keytab_unshift;
static const uint8_t *keytab_shift;
static const uint8_t *keytab_caps;

/* capture the TOS keyboard translation tables (idempotent; works
 * before or after the IKBD takeover) */
static void capture_keytabs(void)
{
    typedef struct {
        uint8_t *unshift, *shift, *caps;
    } keytab_t;
    keytab_t *kt;

    if (keytab_unshift != NULL) {
        return;
    }
    kt = (keytab_t *)Keytbl((void *)-1L, (void *)-1L, (void *)-1L);
    keytab_unshift = kt->unshift;
    keytab_shift = kt->shift;
    keytab_caps = kt->caps;
}

/* Send one command byte straight to the keyboard ACIA. Ikbdws would
 * do, but install/remove also run from the terminate vector, where
 * only hardware access is allowed. */
void stdl_ikbd_send(uint8_t b)
{
    while (!(ACIA_KBD_CTRL & 0x02)) { }     /* wait for TDRE */
    ACIA_KBD_TX(b);
}

/* Pause IKBD output and wait for bytes already in flight to drain
 * through whichever handler is currently installed, so the vector
 * can be swapped on a packet boundary. Taking over mid-packet
 * misaligns the parser - IKBD bytes are ~1.3ms apart and a mouse
 * packet is three of them, so launching from the desktop with the
 * mouse still moving makes hitting the middle of one likely. The
 * fallout is nasty: negative mouse deltas ($F8..$FF) then parse as
 * packet headers, $FF as a joystick event whose "state" byte is
 * really the next packet's header, latching phantom held directions
 * and fire that mask the real transitions until the stick is
 * waggled through a full cycle. */
static void ikbd_quiesce(void)
{
    uint32_t settle;

    stdl_ikbd_send(0x13);                        /* pause output */
    settle = SETTLE_CLOCK + 4;              /* ~20ms: command transit
                                               plus a packet in flight */
    while ((int32_t)(SETTLE_CLOCK - settle) < 0) { }
}

void stdl_events_install(void)
{
    kbdvecs_t *kv;

    if (stdl_events_installed) {
        return;
    }
    capture_keytabs();
    ikbd_quiesce();
    kv = (kbdvecs_t *)Kbdvbase();
    old_ikbdsys = kv->ikbdsys;
    kv->ikbdsys = (long)stdl_ikbd_handler;
    pkt_pending = 0;
    mouse_buttons_isr = 0;
    stdl_ikbd_send(0x11);                        /* resume output */
    /* Re-sent here because the mode lives in the keyboard processor
     * and does not survive whatever ran before us. */
    stdl_ikbd_mode();
    stdl_ikbd_send(0x16);                        /* interrogate joysticks:
                                               the $FD reply seeds
                                               joy_state with the true
                                               held state */
    /* A controller published through the cookie jar, if anything is
     * offering one. Nothing above reaches it: we have just taken the
     * IKBD, so TOS will not be dispatching joyvec to anyone. */
    joy_ikbd = 0;
    joy_xpad = 0;
    stdl_xpad_open();
    stdl_events_installed = 1;
}

void stdl_events_remove(void)
{
    /* Before the desktop gets the machine back: a provider holds a
     * rumble until it is replaced, and nothing would replace it. */
    stdl_xpad_rumble_stop(&stdl_pad0);
    if (stdl_joyn != NULL) {
        stdl_joyn->stop();          /* the other pads' rumble */
    }
    if (stdl_events_installed) {
        kbdvecs_t *kv = (kbdvecs_t *)Kbdvbase();
        ikbd_quiesce();
        kv->ikbdsys = old_ikbdsys;
        stdl_ikbd_send(0x11);
        /* Always, whatever the program asked for: the desktop this
         * hands back to needs a pointer, and a game that disabled
         * the mouse - or asked for two joysticks, which takes the
         * mouse port - and then crashed would otherwise leave the
         * machine without one. $08 makes the port a mouse again.
         * Pure ACIA writes, so it is legal from the terminate path
         * too. */
        stdl_ikbd_send(0x08);
        stdl_events_installed = 0;
    }
}

/* What STDL_SetVideoMode does for input, without the mode; the
 * teardown is the same whichever installed it */
int STDL_OpenInput(void)
{
    if (STDL_Init(STDL_INIT_VIDEO) < 0) {
        return -1;
    }
    stdl_events_install();
    return 0;
}

/* ---------------------------------------------------------------- */
/* pump-side state                                                  */

#define QUEUE_SIZE 32

static STDL_Event queue[QUEUE_SIZE];
static int queue_head, queue_len;

static uint8_t  keystate[STDLK_LAST];
static uint16_t modstate;
static uint8_t  unicode_enabled;
static int      repeat_delay, repeat_interval;
static uint16_t repeat_sym;
static uint8_t  repeat_scan;
static uint16_t repeat_mod;
static uint32_t repeat_next;

static int16_t  mouse_x = STDL_SCREEN_W / 2;
static int16_t  mouse_y = STDL_SCREEN_H / 2;
static uint8_t  mouse_state;        /* STDL button bitmask */
static uint8_t  joy_state;          /* joy_ikbd | joy_xpad          */
static uint8_t  joy1_fire;          /* 0x80 while the right mouse
                                       button - which the IKBD makes
                                       of joystick 1's fire button
                                       whenever mouse reporting is
                                       on - is held */

/*
 * Two or more joysticks: $12 and $14, the mouse off and joystick event
 * reporting, which makes the mouse port a joystick ($FE packets) -
 * $12 alone does not, as a mouse moved on a silenced port sent nothing
 * on a real Mega STE (STDL_EnableMouse's entry in AGENTS.md). One: $08,
 * the mouse, TOS's own mode, and $12 after it to silence it if the
 * program said so. Joystick traffic is untouched by $08 and $12: its
 * packets are $FD, $FE and $FF against the mouse's $F7 to $FB.
 * The bytes chosen first and two sends, as stdl_ikbd_send is inlined
 * at each: four sends were 124 bytes, these 102 (a table and a loop
 * were no smaller).
 */
void stdl_ikbd_mode(void)
{
    const int joys = stdl_joy_count > 1;

    stdl_ikbd_send((uint8_t)(joys ? 0x12 : 0x08));
    if (joys || !stdl_mouse_on) {
        stdl_ikbd_send((uint8_t)(joys ? 0x14 : 0x12));
        /* With the mouse silent, port 1's fire comes in its own
         * packet, and what the right button last said is stale: kept,
         * a release in the packet would get it back, and fire would
         * stay held. */
        joy1_fire = 0;
    }
}

/* A device event, where the program asked for them: the type and
 * `which` sit where a joystick event's do. */
void stdl_joy_device(uint8_t type, int which)
{
    if (stdl_joy_devices) {
        stdl_joy_event(type, which, 0, 0);
    }
}

void stdl_joy_event(uint8_t type, int which, int index, int value)
{
    STDL_Event ev;

    memset(&ev, 0, sizeof(ev));
    ev.jaxis.type = type;
    ev.jaxis.which = (uint8_t)which;
    ev.jaxis.axis = (uint8_t)index;     /* a button's and a hat's too */
    if (type == STDL_JOYAXISMOTION) {
        ev.jaxis.value = (int16_t)value;
    } else {
        ev.jbutton.state = (uint8_t)value;  /* and jhat.value: one byte */
    }
    STDL_PushEvent(&ev);
}

void (*stdl_audio_hook)(void);
void (*stdl_cursor_hook)(int x, int y);
void (*stdl_timer_hook)(void);

int STDL_PushEvent(const STDL_Event *e)
{
    if (queue_len >= QUEUE_SIZE) {
        return -1;
    }
    queue[(queue_head + queue_len) & (QUEUE_SIZE - 1)] = *e;
    queue_len++;
    return 0;
}

/*
 * Pumps that found events waiting with none taken since the last
 * take, counted to 8: 0 or 1 for a program that takes its events, as
 * each pump of a drain follows a take. A program that only pumps and
 * polls (STDL_PumpEvents and STDL_GetJoystickState, or
 * SDL_JoystickUpdate) never empties the queue, and its pads held back
 * for room would never move for it; once eight pumps have passed so,
 * room does not matter to it, and a full queue drops events as it
 * always did.
 */
static uint8_t untaken;

int stdl_event_room(void)
{
    return (untaken < 8) ? QUEUE_SIZE - queue_len : QUEUE_SIZE;
}

static int pop_event(STDL_Event *e)
{
    if (queue_len == 0) {
        return 0;
    }
    *e = queue[queue_head];
    queue_head = (queue_head + 1) & (QUEUE_SIZE - 1);
    queue_len--;
    untaken = 0;
    return 1;
}

/* scancode -> sym for keys the ASCII tables don't cover usefully */
static uint16_t special_sym(uint8_t scan)
{
    switch (scan) {
        case 0x01: return STDLK_ESCAPE;
        case 0x0E: return STDLK_BACKSPACE;
        case 0x0F: return STDLK_TAB;
        case 0x1C: return STDLK_RETURN;
        case 0x1D: return STDLK_LCTRL;
        case 0x2A: return STDLK_LSHIFT;
        case 0x36: return STDLK_RSHIFT;
        case 0x38: return STDLK_LALT;
        case 0x39: return STDLK_SPACE;
        case 0x3A: return STDLK_CAPSLOCK;
        case 0x47: return STDLK_HOME;
        case 0x48: return STDLK_UP;
        case 0x4A: return STDLK_KP_MINUS;
        case 0x4B: return STDLK_LEFT;
        case 0x4D: return STDLK_RIGHT;
        case 0x4E: return STDLK_KP_PLUS;
        case 0x50: return STDLK_DOWN;
        case 0x52: return STDLK_INSERT;
        case 0x53: return STDLK_DELETE;
        case 0x61: return STDLK_UNDO;
        case 0x62: return STDLK_HELP;
        case 0x63: return STDLK_LEFTPAREN;   /* keypad ( */
        case 0x64: return STDLK_RIGHTPAREN;  /* keypad ) */
        case 0x65: return STDLK_KP_DIVIDE;
        case 0x66: return STDLK_KP_MULTIPLY;
        case 0x67: return STDLK_KP7;
        case 0x68: return STDLK_KP8;
        case 0x69: return STDLK_KP9;
        case 0x6A: return STDLK_KP4;
        case 0x6B: return STDLK_KP5;
        case 0x6C: return STDLK_KP6;
        case 0x6D: return STDLK_KP1;
        case 0x6E: return STDLK_KP2;
        case 0x6F: return STDLK_KP3;
        case 0x70: return STDLK_KP0;
        case 0x71: return STDLK_KP_PERIOD;
        case 0x72: return STDLK_KP_ENTER;
        default: break;
    }
    if (scan >= 0x3B && scan <= 0x44) {
        return (uint16_t)(STDLK_F1 + (scan - 0x3B));
    }
    if (scan >= 0x54 && scan <= 0x5D) {      /* shifted F-keys */
        return (uint16_t)(STDLK_F1 + (scan - 0x54));
    }
    return 0;
}

static uint16_t scan_to_sym(uint8_t scan)
{
    uint16_t sym = special_sym(scan);
    uint8_t ascii;

    if (sym != 0) {
        return sym;
    }
    ascii = keytab_unshift ? keytab_unshift[scan] : 0;
    if (ascii >= 'A' && ascii <= 'Z') {
        return (uint16_t)(ascii + 32);
    }
    if (ascii >= 32 && ascii < 127) {
        return ascii;
    }
    return STDLK_UNKNOWN;
}

static uint16_t scan_to_unicode(uint8_t scan)
{
    const uint8_t *tab = keytab_unshift;
    uint8_t c;

    if (!unicode_enabled || tab == NULL) {
        return 0;
    }
    if (modstate & STDL_KMOD_SHIFT) {
        tab = keytab_shift;
    } else if (modstate & STDL_KMOD_CAPS) {
        tab = keytab_caps;
    }
    c = tab[scan];
    if (c == 0 || c >= 128) {
        return 0;
    }
    if ((modstate & STDL_KMOD_CTRL) && (c & 0x40)) {
        return (uint16_t)(c & 0x1F);
    }
    return c;
}

static void push_key(uint8_t type, uint8_t scan, uint16_t sym,
                     uint16_t unicode, uint16_t mod_extra)
{
    STDL_Event ev;

    memset(&ev, 0, sizeof(ev));
    ev.key.type = type;
    ev.key.state = (type == STDL_KEYDOWN) ? STDL_PRESSED
                                          : STDL_RELEASED;
    ev.key.keysym.scancode = scan;
    ev.key.keysym.sym = sym;
    ev.key.keysym.mod = (uint16_t)(modstate | mod_extra);
    ev.key.keysym.unicode = unicode;
    STDL_PushEvent(&ev);
}

/* mod_extra tags synthesised events (STDL_KMOD_JOYSTICK) */
static void handle_scancode(uint8_t code, uint32_t now,
                            uint16_t mod_extra)
{
    uint8_t scan = code & 0x7F;
    int released = (code & 0x80) != 0;
    uint16_t sym = scan_to_sym(scan);

    /* track modifiers before building the event's mod field so the
     * modifier key's own event carries its new state, like SDL */
    switch (scan) {
        case 0x2A: /* lshift */
            if (released) modstate &= ~STDL_KMOD_LSHIFT;
            else          modstate |= STDL_KMOD_LSHIFT;
            break;
        case 0x36: /* rshift */
            if (released) modstate &= ~STDL_KMOD_RSHIFT;
            else          modstate |= STDL_KMOD_RSHIFT;
            break;
        case 0x1D: /* control */
            if (released) modstate &= ~STDL_KMOD_LCTRL;
            else          modstate |= STDL_KMOD_LCTRL;
            break;
        case 0x38: /* alternate */
            if (released) modstate &= ~STDL_KMOD_LALT;
            else          modstate |= STDL_KMOD_LALT;
            break;
        case 0x3A: /* caps lock toggles on make */
            if (!released) modstate ^= STDL_KMOD_CAPS;
            break;
        default:
            break;
    }

    if (sym != STDLK_UNKNOWN && sym < STDLK_LAST) {
        if (released) {
            if (!keystate[sym]) {
                return;         /* spurious break */
            }
            keystate[sym] = 0;
        } else {
            if (keystate[sym]) {
                return;         /* IKBD makes don't auto-repeat, but
                                   be safe against duplicates */
            }
            keystate[sym] = 1;
        }
    }

    if (released) {
        if (sym == repeat_sym) {
            repeat_sym = 0;
        }
        push_key(STDL_KEYUP, scan, sym, 0, mod_extra);
    } else {
        push_key(STDL_KEYDOWN, scan, sym, scan_to_unicode(scan),
                 mod_extra);
        if (repeat_delay > 0) {
            repeat_sym = sym;
            repeat_scan = scan;
            repeat_mod = mod_extra;
            repeat_next = now + (uint32_t)repeat_delay;
        }
    }
}

static uint8_t joykey_enabled;

/* joystick state bits for up/down/left/right/fire, and the
 * scancodes they emulate (default: arrows + left Alt) */
static const uint8_t joykey_bits[5] = {
    0x01, 0x02, 0x04, 0x08, 0x80
};
static uint8_t joykey_scan[STDL_JOYSTICK_INPUTS] = {
    0x48, 0x50, 0x4B, 0x4D, 0x38
};

/* Which pad-only buttons were held last time, so their key traffic can
 * be diffed the way the joystick byte is. Bit n is button n, and bit 0
 * is unused: button 0 is fire, and arrives through joykey_bits. */
static uint16_t joykey_held;

/* synthesise mapped key traffic from joystick changes, going
 * through handle_scancode so key state, repeats and modifiers all
 * behave exactly like real keys */
static void joykey_emulate(uint8_t state, uint8_t changed,
                           uint32_t now)
{
    int i;

    for (i = 0; i < 5; i++) {
        if ((changed & joykey_bits[i]) && joykey_scan[i] != 0) {
            handle_scancode((uint8_t)(joykey_scan[i]
                | ((state & joykey_bits[i]) ? 0 : 0x80)), now,
                STDL_KMOD_JOYSTICK);
        }
    }
}

/*
 * The same, for the buttons an ST joystick does not have. They come
 * from a pad rather than the packet parser, so they are polled and
 * diffed here rather than arriving as a changed mask.
 *
 * Button 0 is skipped: it is fire, and joykey_emulate already reports
 * it from the merged joystick byte, where a real stick reaches it too.
 */
static void joykey_emulate_buttons(uint32_t now)
{
    uint16_t now_held = 0;
    uint16_t changed;
    int i;

    /* no pad, or none merged into joystick 0, reads at rest */
    for (i = 1; i < STDL_XPAD_BUTTONS; i++) {
        if (stdl_xpad_button(&stdl_pad0, i)) {
            now_held = (uint16_t)(now_held | (1u << i));
        }
    }

    changed = (uint16_t)(now_held ^ joykey_held);
    if (changed == 0) {
        return;
    }

    for (i = 1; i < STDL_XPAD_BUTTONS; i++) {
        int input = STDL_JOYSTICK_FIRE + i;

        if (!(changed & (1u << i)) || joykey_scan[input] == 0) {
            continue;
        }
        handle_scancode((uint8_t)(joykey_scan[input]
            | ((now_held & (1u << i)) ? 0 : 0x80)), now,
            STDL_KMOD_JOYSTICK);
    }

    joykey_held = now_held;
}

/* Send break codes for every pad button we are currently holding a key
 * down for, so switching emulation off cannot strand one. */
static void joykey_release_buttons(uint32_t now)
{
    int i;

    for (i = 1; i < STDL_XPAD_BUTTONS; i++) {
        int input = STDL_JOYSTICK_FIRE + i;

        if ((joykey_held & (1u << i)) && joykey_scan[input] != 0) {
            handle_scancode((uint8_t)(joykey_scan[input] | 0x80), now,
                            STDL_KMOD_JOYSTICK);
        }
    }
    joykey_held = 0;
}

/* Whether the input behind a binding is being held right now, wherever
 * it comes from. */
static int joykey_input_held(int input)
{
    if (input < 5) {
        return (joy_state & joykey_bits[input]) != 0;
    }
    return (joykey_held & (1u << (input - STDL_JOYSTICK_FIRE))) != 0;
}

/* find the ST scancode producing a keysym; 0 if the key doesn't
 * exist on the keyboard */
static uint8_t sym_to_scan(uint16_t sym)
{
    int scan;

    if (sym == 0) {
        return 0;
    }
    capture_keytabs();
    for (scan = 1; scan <= 0x72; scan++) {
        if (scan_to_sym((uint8_t)scan) == sym) {
            return (uint8_t)scan;
        }
    }
    return 0;
}

void STDL_JoystickKeyEmulation(int enable)
{
    uint8_t want = (uint8_t)(enable != 0);

    if (want == joykey_enabled) {
        return;
    }
    /*
     * Emulation is driven from changes, so switching it on while a
     * direction is already held would emulate nothing until the player
     * let go: turning it on mid-game, or turning it on at all with a
     * pad resting against a corner, would silently do nothing. Latch
     * what is held now, and release it again on the way out so a
     * synthetic key cannot be left stuck down. STDL_JoystickKeyMapping
     * already does exactly this across a remap.
     */
    if (want) {
        joykey_enabled = 1;
        if (joy_state != 0) {
            joykey_emulate(joy_state, joy_state, STDL_GetTicks());
        }
        /* joykey_held is already zero here, so this presses whatever
         * the pad is holding rather than diffing against stale state. */
        joykey_emulate_buttons(STDL_GetTicks());
    } else {
        if (joy_state != 0) {
            joykey_emulate(0, joy_state, STDL_GetTicks());
        }
        joykey_release_buttons(STDL_GetTicks());
        joykey_enabled = 0;
    }
}

uint8_t stdl_joy_ikbd(void)
{
    return joy_ikbd;
}

/*
 * Joysticks 1-5, for the three readers below: there only below the
 * count, which is above one only once STDL_SetNumJoysticks has set
 * stdl_joyn. One call out of line, shared, so that what the index
 * costs a program reading joystick 0 alone is a test and a branch a
 * reader.
 */
static __attribute__((noinline)) int joyn_read(int which, int what)
{
    return ((unsigned)which < stdl_joy_count) ? stdl_joyn->read(which, what)
                                              : 0;
}

int STDL_HasPad(int which)
{
    if (which != 0) {
        return joyn_read(which, STDL_JOYN_PAD);
    }
    /* apart, pad 0 is a joystick of its own (STDL_UseJoystickMerge),
     * and joystick 0's slot -1 */
    return stdl_pad0.slot >= 0 && stdl_xpad_present();
}

int STDL_JoystickInputHeld(int which, int input)
{
    if (which != 0) {
        return joyn_read(which, input);
    }
    if (input < 0 || input >= STDL_JOYSTICK_INPUTS) {
        return 0;
    }
    if (input < 5) {
        /* joy_state is already the merged byte, so a stick on the port
         * and a pad both reach these five. */
        return (joy_state & joykey_bits[input]) != 0;
    }
    /* no pad, or none merged into joystick 0, reads at rest */
    return stdl_xpad_button(&stdl_pad0, input - STDL_JOYSTICK_FIRE);
}

int STDL_JoystickKeyBind(int input, uint16_t sym)
{
    uint32_t now = STDL_GetTicks();
    uint8_t scan;
    int held;

    if (input < 0 || input >= STDL_JOYSTICK_INPUTS) {
        return 0;
    }

    scan = sym_to_scan(sym);
    held = joykey_enabled && joykey_input_held(input);

    /* Move a held input across cleanly: up on the old key, down on the
     * new one, so a rebind mid-press cannot strand either. */
    if (held && joykey_scan[input] != 0) {
        handle_scancode((uint8_t)(joykey_scan[input] | 0x80), now,
                        STDL_KMOD_JOYSTICK);
    }

    joykey_scan[input] = scan;

    if (held && scan != 0) {
        handle_scancode(scan, now, STDL_KMOD_JOYSTICK);
    }

    return (sym != 0 && scan != 0);
}

int STDL_JoystickKeyMapping(uint16_t up, uint16_t down, uint16_t left,
                       uint16_t right, uint16_t fire)
{
    const uint16_t syms[5] = { up, down, left, right, fire };
    uint8_t held = joy_state;
    int i, resolved = 0;

    /* release currently-held inputs on their old keys so no
     * synthetic key gets stuck across the remap */
    if (joykey_enabled && held != 0) {
        joykey_emulate(0, held, STDL_GetTicks());
    }
    for (i = 0; i < 5; i++) {
        joykey_scan[i] = sym_to_scan(syms[i]);
        if (syms[i] != 0 && joykey_scan[i] != 0) {
            resolved++;
        }
    }
    /* re-press held inputs on the new keys */
    if (joykey_enabled && held != 0) {
        joykey_emulate(held, held, STDL_GetTicks());
    }
    return resolved;
}

static void handle_joy(uint8_t state)
{
    uint8_t changed = (uint8_t)(state ^ joy_state);
    int axis;

    /*
     * Axes report on a change of value, not of direction bits. The two
     * are not the same thing once a pad is involved: a stick moving
     * inside the provider's deadzone changes no bit, and a joystick
     * pushed the way a pad is already held changes no bit either, yet
     * both change what SDL_JoystickGetAxis() returns. Diffing the value
     * is the only version where an event and a poll cannot disagree.
     */
    for (axis = 0; axis < 2; axis++) {
        int16_t value = stdl_xpad_axis_merged(&stdl_pad0, axis, joy_ikbd);

        if (value == joy_axis[axis]) {
            continue;
        }
        joy_axis[axis] = value;
        stdl_joy_event(STDL_JOYAXISMOTION, 0, axis, value);
    }

    /* Buttons and key emulation are digital, so a bit has to move. */
    if (changed == 0) {
        return;
    }
    if (changed & 0x80) {
        stdl_joy_event((state & 0x80) ? STDL_JOYBUTTONDOWN
                                      : STDL_JOYBUTTONUP,
                       0, 0, (state & 0x80) != 0);
    }
    if (joykey_enabled) {
        joykey_emulate(state, changed, STDL_GetTicks());
    }
    joy_state = state;
}

static void handle_mouse_buttons(uint8_t ikbd_bits)
{
    /* IKBD relative-mouse header bits: 1 = right, 2 = left */
    static const uint8_t buttons[2] = {
        STDL_BUTTON_LEFT, STDL_BUTTON_RIGHT
    };
    uint8_t new_state = 0;
    uint8_t changed;
    int i;

    if (ikbd_bits & 2) new_state |= 1 << (STDL_BUTTON_LEFT - 1);
    if (ikbd_bits & 1) new_state |= 1 << (STDL_BUTTON_RIGHT - 1);
    changed = (uint8_t)(new_state ^ mouse_state);
    for (i = 0; i < 2; i++) {
        uint8_t bit = (uint8_t)(1 << (buttons[i] - 1));
        STDL_Event ev;

        if (!(changed & bit)) {
            continue;
        }
        memset(&ev, 0, sizeof(ev));
        ev.button.type = (new_state & bit)
            ? STDL_MOUSEBUTTONDOWN : STDL_MOUSEBUTTONUP;
        ev.button.button = buttons[i];
        ev.button.state = (new_state & bit)
            ? STDL_PRESSED : STDL_RELEASED;
        ev.button.x = (uint16_t)mouse_x;
        ev.button.y = (uint16_t)mouse_y;
        STDL_PushEvent(&ev);
    }
    mouse_state = new_state;
}

/* The mouse's reach: the screen's rows - 240 on a responsive or tall
 * screen, an open border's on an ST - or an ST's 200 with none up. It
 * was 200 always, which left a 240-row screen's bottom 40 rows out of
 * the mouse's reach. */
static void clamp_mouse(void)
{
    const STDL_Surface *s = STDL_GetVideoSurface();
    const int16_t rows = s ? s->h : STDL_SCREEN_H;
    if (mouse_x < 0) mouse_x = 0;
    if (mouse_y < 0) mouse_y = 0;
    if (mouse_x >= STDL_SCREEN_W) mouse_x = STDL_SCREEN_W - 1;
    if (mouse_y >= rows) mouse_y = (int16_t)(rows - 1);
}

/* harvest accumulated mouse deltas into one motion event */
static void flush_motion(void)
{
    STDL_Event ev;
    int16_t dx, dy;
    uint16_t sr = stdl_int_off();

    dx = stdl_mouse_dx;
    dy = stdl_mouse_dy;
    stdl_mouse_dx = 0;
    stdl_mouse_dy = 0;
    stdl_int_restore(sr);

    if (dx == 0 && dy == 0) {
        return;
    }
    mouse_x = (int16_t)(mouse_x + dx);
    mouse_y = (int16_t)(mouse_y + dy);
    clamp_mouse();
    memset(&ev, 0, sizeof(ev));
    ev.motion.type = STDL_MOUSEMOTION;
    ev.motion.state = mouse_state;
    ev.motion.x = (uint16_t)mouse_x;
    ev.motion.y = (uint16_t)mouse_y;
    ev.motion.xrel = dx;
    ev.motion.yrel = dy;
    STDL_PushEvent(&ev);
}

/*
 * Joystick 0 as its pad record now reads, merged with the port: its
 * events and its emulated keys. The pump's, after a reading, and the
 * merge setting's (src/joymerge.c), when pad 0 leaves and joystick 0
 * lets go of what it held.
 */
void stdl_pad0_report(uint32_t now)
{
    joy_xpad = stdl_xpad_classic(&stdl_pad0);
    handle_joy((uint8_t)(joy_ikbd | joy_xpad));
    stdl_xpad_events(&stdl_pad0, 0);
    if (joykey_enabled) {
        joykey_emulate_buttons(now);
    }
}

/*
 * When the pump last ran: the VBL count, and the 200Hz count for a
 * VBL that does not count (a program's own VBL handler that leaves
 * _frclock alone, or the VBL masked), where a pump every five ticks,
 * a 50Hz frame and a little more, keeps the events coming. Both 0
 * until the first pump, which is then due at once: the 200Hz count
 * runs from boot, and is past 5 in its first 25ms.
 */
static uint32_t pump_vbl, pump_hz;

int STDL_PumpEventsIfDue(void)
{
    if (STDL_FRCLOCK == pump_vbl && STDL_HZ200 - pump_hz < 5) {
        stdl_idle_services();
        return 0;
    }
    STDL_PumpEvents();
    return 1;
}

void STDL_PumpEvents(void)
{
    uint32_t now = STDL_GetTicks();

    pump_vbl = STDL_FRCLOCK;
    pump_hz = STDL_HZ200;

    /* An empty queue needs no reset: only a take empties it, and a
     * take resets the count (pop_event). */
    if (queue_len != 0 && untaken < 8) {
        untaken++;
    }

    /*
     * An Xpad provider has no packets to deliver, so it is polled here
     * rather than arriving through the ring. The classic five inputs
     * are merged with the real joystick and pushed through handle_joy,
     * which means axes 0 and 1, button 0 and the key emulation all
     * behave exactly as they do for a stick in port 1. Everything a
     * 1980 joystick cannot express is reported separately below.
     *
     * Merged rather than either winning: they are different physical
     * devices, so a stick in the port and a pad both work, and either
     * on its own works.
     */
    if (stdl_xpad_present()) {
        stdl_xpad_rumble_tick(&stdl_pad0, now);
        /*
         * Read only with room in the queue for all it could say - its
         * twenty events, and with key emulation its keys too, up to
         * seventeen, so the queue empty (every input bound and moving
         * in one pump would pass even that) - or a full queue would
         * lose what the reading had already taken as reported. Left,
         * it is read at a pump with room: late, not lost
         * (stdl_event_room says when room matters). And not when it
         * has nothing to say (stdl_xpad_read).
         */
        if (stdl_event_room() >= (joykey_enabled ? QUEUE_SIZE
                                                 : STDL_XPAD_EVENTS)
            && stdl_xpad_read(&stdl_pad0)) {
            stdl_pad0_report(now);
        }
    }
    if (stdl_joyn != NULL) {
        stdl_joyn->poll(now);       /* the other pads */
    }

    /* drain the interrupt-side ring */
    for (;;) {
        uint16_t item;
        uint16_t sr = stdl_int_off();

        if (stdl_ring_tail == stdl_ring_head) {
            stdl_int_restore(sr);
            break;
        }
        item = stdl_ring[stdl_ring_tail];
        stdl_ring_tail = (uint8_t)((stdl_ring_tail + 1) & RING_MASK);
        stdl_int_restore(sr);

        switch (item & 0xFF00) {
            case TAG_KEY:
                handle_scancode((uint8_t)item, now, 0);
                break;
            case TAG_MOUSEBTN:
                /* motion first, so the click carries up-to-date
                 * coordinates */
                flush_motion();
                handle_mouse_buttons((uint8_t)item);
                /* While mouse reporting is on - and TOS leaves it
                 * on - the IKBD strips the fire bit from joystick
                 * 1's packets and reports that button as the right
                 * mouse button instead; software cannot tell the
                 * two apart.  Fold it back into the joystick state
                 * (keeping the mouse event above) so fire works
                 * whether or not a game also uses the mouse.
                 * After STDL_EnableMouse(0) this stops happening -
                 * the IKBD hands fire back to joystick 1's own
                 * packet, measured with a real stick on a Mega STE -
                 * so the fold goes inert rather than wrong.
                 * Only while the mouse reports, then: with it off, or
                 * with two joysticks, fire is in the packet, and a
                 * mouse packet still in flight at the switch must not
                 * move it (stdl_ikbd_mode clears what it said). */
                if (stdl_mouse_on && stdl_joy_count == 1) {
                    joy1_fire = (uint8_t)(item << 7);   /* bit 0 */
                    joy_ikbd = (uint8_t)((joy_ikbd & 0x0F)
                                         | joy1_fire);
                    handle_joy((uint8_t)(joy_ikbd | joy_xpad));
                }
                break;
            case TAG_JOY:
                /* OR rather than overwrite: a direction change in
                 * this packet must not release a fire button that
                 * is being held down over on the mouse side */
                joy_ikbd = (uint8_t)((item & 0xFF) | joy1_fire);
                handle_joy((uint8_t)(joy_ikbd | joy_xpad));
                break;
            case TAG_JOY0:
                if (stdl_joyn != NULL) {
                    stdl_joyn->port0((uint8_t)item);
                }
                break;
            default:
                break;
        }
    }
    flush_motion();

    /*
     * Synthesise key repeats, one a pump at most. On their own beat
     * (the next one an interval after the last was due, not after this
     * pump), so that a pump a VBL gives the interval asked on average:
     * the default 30ms came every 40ms at 50Hz when each was timed from
     * the pump. More than an interval behind (the program away), the
     * beat starts again from now rather than sending the lost ones.
     */
    if (repeat_sym != 0 && repeat_delay > 0
        && (int32_t)(now - repeat_next) >= 0) {
        push_key(STDL_KEYDOWN, repeat_scan, repeat_sym,
                 scan_to_unicode(repeat_scan), repeat_mod);
        repeat_next += (uint32_t)(repeat_interval > 0 ? repeat_interval
                                                      : 1);
        if ((int32_t)(now - repeat_next) >= 0) {
            repeat_next = now + (uint32_t)(repeat_interval > 0
                                           ? repeat_interval : 1);
        }
    }

    /* cooperative services */
    if (stdl_timer_hook != NULL) {
        stdl_timer_hook();
    }
    if (stdl_audio_hook != NULL) {
        stdl_audio_hook();
    }
    if (stdl_cursor_hook != NULL) {
        stdl_cursor_hook(mouse_x, mouse_y);
    }
}

/* Once a VBL (STDL_PumpEventsIfDue): a drain of k events was k + 1
 * pumps, each reading every pad and the ring again - 17 a frame and
 * 6.4ms of an 8MHz ST's frame for a frame's 16 events and two sticks,
 * 18.9ms with four pads as well (PUMPCOST); 2.8 and 3.0ms now. */
int STDL_PollEvent(STDL_Event *e)
{
    STDL_PumpEventsIfDue();
    if (e == NULL) {
        return queue_len != 0;
    }
    return pop_event(e);
}

/* A wait pumps every time it looks, as an explicit STDL_PumpEvents
 * does: it is idle, and its 5ms delay is its beat. STDL_PollEvent then
 * only takes, as the pump has just run. */
int STDL_WaitEvent(STDL_Event *e)
{
    for (;;) {
        STDL_PumpEvents();
        if (STDL_PollEvent(e)) {
            return 1;
        }
        STDL_Delay(5);
    }
}

const uint8_t *STDL_GetKeyState(int *numkeys)
{
    if (numkeys != NULL) {
        *numkeys = STDLK_LAST;
    }
    return keystate;
}

uint16_t STDL_GetModState(void)
{
    return modstate;
}

int STDL_EnableKeyRepeat(int delay, int interval)
{
    repeat_delay = delay;
    repeat_interval = interval;
    if (delay <= 0) {
        repeat_sym = 0;
    }
    return 0;
}

int STDL_EnableUNICODE(int enable)
{
    int old = unicode_enabled;
    if (enable >= 0) {
        unicode_enabled = (uint8_t)(enable != 0);
    }
    return old;
}

uint8_t STDL_GetMouseState(int *x, int *y)
{
    if (x != NULL) *x = mouse_x;
    if (y != NULL) *y = mouse_y;
    return mouse_state;
}

void STDL_WarpMouse(uint16_t x, uint16_t y)
{
    STDL_Event ev;
    const int16_t ox = mouse_x, oy = mouse_y;

    mouse_x = (int16_t)(x > STDL_SCREEN_W ? STDL_SCREEN_W : x);
    mouse_y = (int16_t)(y > 0x7FFF ? 0x7FFF : y);
    clamp_mouse();
    memset(&ev, 0, sizeof(ev));
    ev.motion.type = STDL_MOUSEMOTION;
    ev.motion.state = mouse_state;
    ev.motion.x = (uint16_t)mouse_x;
    ev.motion.y = (uint16_t)mouse_y;
    ev.motion.xrel = (int16_t)(mouse_x - ox);
    ev.motion.yrel = (int16_t)(mouse_y - oy);
    STDL_PushEvent(&ev);
}

uint8_t STDL_GetJoystickState(int which)
{
    if (which != 0) {
        return (uint8_t)joyn_read(which, STDL_JOYN_STATE);
    }
    return joy_state;
}

int STDL_NumJoysticks(void)
{
    return stdl_joy_count;
}

const char *STDL_GetKeyName(uint16_t sym)
{
    static char buf[2];

    switch (sym) {
        case STDLK_BACKSPACE: return "backspace";
        case STDLK_TAB:       return "tab";
        case STDLK_RETURN:    return "return";
        case STDLK_ESCAPE:    return "escape";
        case STDLK_SPACE:     return "space";
        case STDLK_DELETE:    return "delete";
        case STDLK_UP:        return "up";
        case STDLK_DOWN:      return "down";
        case STDLK_RIGHT:     return "right";
        case STDLK_LEFT:      return "left";
        case STDLK_INSERT:    return "insert";
        case STDLK_HOME:      return "home";
        case STDLK_END:       return "end";
        case STDLK_KP0:       return "[0]";
        case STDLK_KP1:       return "[1]";
        case STDLK_KP2:       return "[2]";
        case STDLK_KP3:       return "[3]";
        case STDLK_KP4:       return "[4]";
        case STDLK_KP5:       return "[5]";
        case STDLK_KP6:       return "[6]";
        case STDLK_KP7:       return "[7]";
        case STDLK_KP8:       return "[8]";
        case STDLK_KP9:       return "[9]";
        case STDLK_KP_PERIOD:   return "[.]";
        case STDLK_KP_DIVIDE:   return "[/]";
        case STDLK_KP_MULTIPLY: return "[*]";
        case STDLK_KP_MINUS:  return "[-]";
        case STDLK_KP_PLUS:   return "[+]";
        case STDLK_KP_ENTER:  return "enter";
        case STDLK_CAPSLOCK:  return "caps lock";
        case STDLK_RSHIFT:    return "right shift";
        case STDLK_LSHIFT:    return "left shift";
        case STDLK_RCTRL:     return "right ctrl";
        case STDLK_LCTRL:     return "left ctrl";
        case STDLK_RALT:      return "right alt";
        case STDLK_LALT:      return "left alt";
        case STDLK_HELP:      return "help";
        case STDLK_UNDO:      return "undo";
        default: break;
    }
    if (sym >= STDLK_F1 && sym <= STDLK_F15) {
        static char fbuf[4];
        int n = sym - STDLK_F1 + 1;
        fbuf[0] = 'f';
        if (n >= 10) {
            fbuf[1] = '1';
            fbuf[2] = (char)('0' + n - 10);
            fbuf[3] = '\0';
        } else {
            fbuf[1] = (char)('0' + n);
            fbuf[2] = '\0';
        }
        return fbuf;
    }
    if (sym > 32 && sym < 127) {
        buf[0] = (char)sym;
        buf[1] = '\0';
        return buf;
    }
    return "unknown key";
}
