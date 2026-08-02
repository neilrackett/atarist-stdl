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

#define ACIA_KBD_CTRL (*(volatile uint8_t *)0xFFFFFC00UL)
#define ACIA_KBD_DATA (*(volatile uint8_t *)0xFFFFFC02UL)

/* ---------------------------------------------------------------- */
/* interrupt-side state                                             */

#define RING_SIZE 64            /* power of two */
#define RING_MASK (RING_SIZE - 1)

#define TAG_KEY      0x0000     /* low byte: scancode | break bit    */
#define TAG_MOUSEBTN 0x0100     /* low byte: IKBD button bits        */
#define TAG_JOY      0x0200     /* low byte: joystick 1 state        */

static volatile uint16_t stdl_ring[RING_SIZE];
static volatile uint8_t  stdl_ring_head;   /* ISR writes here */
static volatile uint8_t  stdl_ring_tail;   /* pump reads here */
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
        uint8_t b = ACIA_KBD_DATA;

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
                    case 0xFD:
                        ring_push((uint16_t)(TAG_JOY | pkt_buf[1]));
                        break;
                    case 0xFE:
                        break;  /* joystick 0 = mouse port, unused */
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
__asm__(
    "\t.text\n"
    "_stdl_ikbd_handler:\n"
    "\tmovem.l %d0-%d1/%a0-%a1,-(%sp)\n"
    "\tjsr _stdl_ikbd_process\n"
    "\tmovem.l (%sp)+,%d0-%d1/%a0-%a1\n"
    "\trts\n");

/* ---------------------------------------------------------------- */
/* install / remove                                                 */

typedef struct {
    long midivec, vkbderr, vmiderr, statvec;
    long mousevec, clockvec, joyvec, midisys, ikbdsys;
} kbdvecs_t;

static long old_ikbdsys;
static int  events_installed;

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

void stdl_events_install(void)
{
    kbdvecs_t *kv;

    if (events_installed) {
        return;
    }
    capture_keytabs();
    kv = (kbdvecs_t *)Kbdvbase();
    old_ikbdsys = kv->ikbdsys;
    kv->ikbdsys = (long)stdl_ikbd_handler;
    events_installed = 1;
}

void stdl_events_remove(void)
{
    if (events_installed) {
        kbdvecs_t *kv = (kbdvecs_t *)Kbdvbase();
        kv->ikbdsys = old_ikbdsys;
        events_installed = 0;
    }
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
static uint8_t  joy_state;

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

static int pop_event(STDL_Event *e)
{
    if (queue_len == 0) {
        return 0;
    }
    *e = queue[queue_head];
    queue_head = (queue_head + 1) & (QUEUE_SIZE - 1);
    queue_len--;
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
static uint8_t joykey_scan[5] = { 0x48, 0x50, 0x4B, 0x4D, 0x38 };

void STDL_JoyKeyEmulation(int enable)
{
    joykey_enabled = (uint8_t)(enable != 0);
}

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

int STDL_JoyKeyMapping(uint16_t up, uint16_t down, uint16_t left,
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
    static const uint8_t axes[2][2] = {
        /* axis 0 (x): left bit 2, right bit 3 */
        { 0x04, 0x08 },
        /* axis 1 (y): up bit 0, down bit 1 */
        { 0x01, 0x02 },
    };
    uint8_t changed = (uint8_t)(state ^ joy_state);
    STDL_Event ev;
    int axis;

    if (changed == 0) {
        return;
    }
    for (axis = 0; axis < 2; axis++) {
        uint8_t neg = axes[axis][0], pos = axes[axis][1];
        if (changed & (neg | pos)) {
            memset(&ev, 0, sizeof(ev));
            ev.jaxis.type = STDL_JOYAXISMOTION;
            ev.jaxis.which = 0;
            ev.jaxis.axis = (uint8_t)axis;
            ev.jaxis.value = (state & neg) ? -32768
                           : (state & pos) ? 32767 : 0;
            STDL_PushEvent(&ev);
        }
    }
    if (changed & 0x80) {
        memset(&ev, 0, sizeof(ev));
        ev.jbutton.type = (state & 0x80) ? STDL_JOYBUTTONDOWN
                                         : STDL_JOYBUTTONUP;
        ev.jbutton.which = 0;
        ev.jbutton.button = 0;
        ev.jbutton.state = (state & 0x80) ? STDL_PRESSED
                                          : STDL_RELEASED;
        STDL_PushEvent(&ev);
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
    if (mouse_x < 0) mouse_x = 0;
    if (mouse_y < 0) mouse_y = 0;
    if (mouse_x >= STDL_SCREEN_W) mouse_x = STDL_SCREEN_W - 1;
    if (mouse_y >= STDL_SCREEN_H) mouse_y = STDL_SCREEN_H - 1;
    memset(&ev, 0, sizeof(ev));
    ev.motion.type = STDL_MOUSEMOTION;
    ev.motion.state = mouse_state;
    ev.motion.x = (uint16_t)mouse_x;
    ev.motion.y = (uint16_t)mouse_y;
    ev.motion.xrel = dx;
    ev.motion.yrel = dy;
    STDL_PushEvent(&ev);
}

void STDL_PumpEvents(void)
{
    uint32_t now = STDL_GetTicks();

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
                break;
            case TAG_JOY:
                handle_joy((uint8_t)item);
                break;
            default:
                break;
        }
    }
    flush_motion();

    /* synthesise key repeats */
    if (repeat_sym != 0 && repeat_delay > 0
        && (int32_t)(now - repeat_next) >= 0) {
        push_key(STDL_KEYDOWN, repeat_scan, repeat_sym,
                 scan_to_unicode(repeat_scan), repeat_mod);
        repeat_next = now + (uint32_t)(repeat_interval > 0
                                       ? repeat_interval : 1);
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

int STDL_PollEvent(STDL_Event *e)
{
    STDL_PumpEvents();
    if (e == NULL) {
        return queue_len != 0;
    }
    return pop_event(e);
}

int STDL_WaitEvent(STDL_Event *e)
{
    for (;;) {
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

    if (x >= STDL_SCREEN_W) x = STDL_SCREEN_W - 1;
    if (y >= STDL_SCREEN_H) y = STDL_SCREEN_H - 1;
    memset(&ev, 0, sizeof(ev));
    ev.motion.type = STDL_MOUSEMOTION;
    ev.motion.state = mouse_state;
    ev.motion.x = x;
    ev.motion.y = y;
    ev.motion.xrel = (int16_t)(x - mouse_x);
    ev.motion.yrel = (int16_t)(y - mouse_y);
    mouse_x = (int16_t)x;
    mouse_y = (int16_t)y;
    STDL_PushEvent(&ev);
}

uint8_t STDL_GetJoyState(void)
{
    return joy_state;
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
