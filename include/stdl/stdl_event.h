/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * IKBD-driven input. The event union deliberately mirrors SDL 1.2's
 * shape (types, field names, numbering) to ease porting.
 */

#ifndef STDL_EVENT_H
#define STDL_EVENT_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdl/stdl_types.h>
#include <stdl/stdl_keys.h>

enum {
    STDL_NOEVENT = 0,
    STDL_ACTIVEEVENT,
    STDL_KEYDOWN,
    STDL_KEYUP,
    STDL_MOUSEMOTION,
    STDL_MOUSEBUTTONDOWN,
    STDL_MOUSEBUTTONUP,
    STDL_JOYAXISMOTION,
    STDL_JOYBALLMOTION,
    STDL_JOYHATMOTION,
    STDL_JOYBUTTONDOWN,
    STDL_JOYBUTTONUP,
    STDL_QUIT,
    /* a joystick come or gone, or standing for other sources: only to
     * a program that asks (STDL_UseJoystickDeviceEvents), in SDL 1.2's
     * reserved numbers, as SDL 1.2 has no such event */
    STDL_JOYDEVICEADDED = 14,
    STDL_JOYDEVICEREMOVED = 15,
    STDL_USEREVENT = 24,   /* .. STDL_NUMEVENTS: the game's own */
    STDL_NUMEVENTS = 32
};

#define STDL_RELEASED 0
#define STDL_PRESSED  1

#define STDL_BUTTON_LEFT   1
#define STDL_BUTTON_MIDDLE 2
#define STDL_BUTTON_RIGHT  3

typedef struct {
    uint8_t  scancode;   /* raw IKBD scancode */
    uint16_t sym;        /* STDL_Key */
    uint16_t mod;        /* STDL_Mod bitmask */
    uint16_t unicode;    /* translated character (ASCII subset) */
} STDL_Keysym;

typedef struct {
    uint8_t     type;    /* STDL_KEYDOWN or STDL_KEYUP */
    uint8_t     state;
    STDL_Keysym keysym;
} STDL_KeyboardEvent;

typedef struct {
    uint8_t  type;
    uint8_t  state;      /* current button bitmask */
    uint16_t x, y;
    int16_t  xrel, yrel;
} STDL_MouseMotionEvent;

typedef struct {
    uint8_t  type;
    uint8_t  button;
    uint8_t  state;
    uint16_t x, y;
} STDL_MouseButtonEvent;

typedef struct {
    uint8_t type;
    uint8_t which;
    uint8_t axis;        /* 0 = x, 1 = y */
    int16_t value;       /* -32768 / 0 / 32767 */
} STDL_JoyAxisEvent;

typedef struct {
    uint8_t type;
    uint8_t which;
    uint8_t button;
    uint8_t state;
} STDL_JoyButtonEvent;

typedef struct {
    uint8_t type;
} STDL_QuitEvent;

/* Game-defined events pushed with STDL_PushEvent; the library never
 * generates them and passes them through the queue untouched. */
typedef struct {
    uint8_t type;        /* STDL_USEREVENT or above */
    int     code;
    void   *data1;
    void   *data2;
} STDL_UserEvent;

/* An Xpad pad's d-pad is its hat (STDL_HAT_* bits); an ST joystick's
 * directions are its axes, so it has none. Balls exist on neither:
 * present so ported event switches compile. */
typedef struct {
    uint8_t type;
    uint8_t which;
    uint8_t hat;
    uint8_t value;
} STDL_JoyHatEvent;

#define STDL_HAT_CENTERED 0x00
#define STDL_HAT_UP       0x01
#define STDL_HAT_RIGHT    0x02
#define STDL_HAT_DOWN     0x04
#define STDL_HAT_LEFT     0x08

/* STDL_JOYDEVICEADDED or STDL_JOYDEVICEREMOVED, and the joystick */
typedef struct {
    uint8_t type;
    uint8_t which;
} STDL_JoyDeviceEvent;

typedef struct {
    uint8_t type;
    uint8_t which;
    uint8_t ball;
    int16_t xrel, yrel;
} STDL_JoyBallEvent;

/* never generated on the ST; present so ported switch statements
 * over SDL 1.2 event types still compile */
typedef struct {
    uint8_t type;
    int     w, h;
} STDL_ResizeEvent;

typedef union {
    uint8_t               type;
    STDL_KeyboardEvent    key;
    STDL_MouseMotionEvent motion;
    STDL_MouseButtonEvent button;
    STDL_JoyAxisEvent     jaxis;
    STDL_JoyButtonEvent   jbutton;
    STDL_JoyHatEvent      jhat;
    STDL_JoyBallEvent     jball;
    STDL_JoyDeviceEvent   jdevice;
    STDL_QuitEvent        quit;
    STDL_ResizeEvent      resize;
    STDL_UserEvent        user;
} STDL_Event;

/*
 * The pump (STDL_PumpEvents) is where everything the program has not
 * asked for happens: the IKBD's keys, mouse and joystick packets
 * become events, the pads are read, key repeat runs, the DMA ring is
 * refilled and the SDL 1.2 shim's timers fire.
 *
 * STDL_PollEvent pumps once a VBL: when the VBL count (_frclock) has
 * moved since the last pump, or there has been none, and otherwise
 * only takes from the queue, serving the timers and the audio at most
 * once a 200Hz tick, as STDL_Delay does. So a drain loop,
 * while (STDL_PollEvent(&e)), pumps once a frame however many events
 * it takes, and a program that takes one event a frame, or only
 * looks (STDL_PollEvent(NULL)), still has the IKBD read and the audio
 * refilled every VBL, well inside the 150ms the audio needs. An event
 * comes at most a VBL later than a pump on every call would have
 * given it, and no later for a program that polls once a frame. A
 * VBL that does not count (a program's own VBL handler leaving
 * _frclock alone, or the VBL masked) gets a pump every 25ms at most
 * from the 200Hz clock instead.
 *
 * STDL_PumpEvents always pumps, and STDL_WaitEvent pumps every time
 * it looks (every 5ms). STDL_PumpEventsIfDue is STDL_PollEvent's own
 * step: the pump if one is due, returning 1, or the timers and audio,
 * returning 0. It is for a layer with a queue of its own over STDL's
 * (the SDL2 add-on), so that its pump keeps the same beat.
 */
int  STDL_PollEvent(STDL_Event *e);
int  STDL_WaitEvent(STDL_Event *e);
int  STDL_PushEvent(const STDL_Event *e);
void STDL_PumpEvents(void);
int  STDL_PumpEventsIfDue(void);

/*
 * Take the keyboard, mouse, joystick and pad without setting a video
 * mode. STDL_SetVideoMode does this itself, so most programs never
 * need it. It is for a program that keeps the screen it was started
 * on - a tool that prints, as examples/checkkeys.c does, in medium
 * resolution, which a mode would turn to low, or on a mono monitor,
 * where STDL_SetVideoMode refuses - or drives the screen itself, and
 * wants the events all the same. A TT's or a Falcon's 256 colours are
 * the library's own screen (stdl_responsive.h).
 *
 * Runs STDL_Init if nothing has yet. Returns 0, or -1 when STDL_Init
 * fails. Calling it again, or setting a mode afterwards, changes
 * nothing. STDL_Quit and the terminate path give the keyboard back
 * as they do after a mode, and STDL_Quit leaves alone a screen the
 * library never set. BIOS console input is dead from here on, as it
 * is under a mode (docs/limits.md); console output still works.
 */
int  STDL_OpenInput(void);

/* Key state array indexed by STDL_Key, valid after each pump. */
const uint8_t *STDL_GetKeyState(int *numkeys);
uint16_t       STDL_GetModState(void);
const char    *STDL_GetKeyName(uint16_t sym);

int  STDL_EnableKeyRepeat(int delay, int interval);
int  STDL_EnableUNICODE(int enable);
#define STDL_DEFAULT_REPEAT_DELAY    500
#define STDL_DEFAULT_REPEAT_INTERVAL 30

uint8_t STDL_GetMouseState(int *x, int *y);

/*
 * Whether the IKBD reports the mouse at all. 1 enables, 0 disables,
 * -1 queries; returns the previous setting. Callable before or
 * after STDL_Init, and re-applied whenever the library takes the
 * keyboard, so it survives the vector swap.
 *
 * A port that never reads the mouse should turn it off. Every
 * movement is a three-byte packet, one ACIA interrupt per byte
 * about 1.3ms apart, and each one is parsed whether or not anybody
 * asked. The ACIA is MFP channel 6, below the overscan timers at 13
 * and 8, so it cannot preempt them - but an interrupt already in
 * service delays them, and a border's flick has to land on an exact
 * cycle. Same shape as the disk finding in docs/porting.md: not a
 * preemption, a delay.
 *
 * Measured on a Mega STE with the bottom border open and the mouse
 * moved continuously: the border flickers and the missed-window
 * count climbs steadily. With reporting disabled, the same movement
 * produces neither. Note what that also rules out - a mouse on port
 * 0 does not come back as joystick 0 traffic once it is silenced,
 * which was the obvious way for this fix to change the shape of the
 * problem rather than remove it.
 *
 * Joystick traffic is untouched - its packets are $FD, $FE and $FF
 * against the mouse's $F7 to $FB - so a joystick-only game can
 * disable the mouse and keep its stick.
 *
 * Including its fire button, which was worth checking rather than
 * assuming. While the mouse reports, the IKBD strips fire out of
 * joystick 1's packets and delivers it as the right mouse button
 * instead, and event.c folds it back; the question was whether
 * turning the mouse off would leave the bit stolen with nothing to
 * steal it into, silently costing a joystick game its fire button.
 * It does not: measured with a real stick on a Mega STE, fire and
 * all four directions work with reporting on and off alike. The
 * fold simply stops being needed.
 *
 * While disabled, STDL_GetMouseState keeps returning the last
 * position and buttons it had; it does not fail, it simply stops
 * changing. The library always puts mouse reporting back when it
 * releases the keyboard, including down the terminate path, so a
 * program that disables it and then crashes still hands the desktop
 * a working pointer.
 *
 * With two joysticks or more (STDL_SetNumJoysticks) the mouse port is
 * joystick 1 and the mouse is off whatever this says: it records the
 * setting, which applies when the program goes back to one. Before
 * the library has the keyboard (STDL_SetVideoMode or STDL_OpenInput)
 * it records it too, and the IKBD is told when the library takes it:
 * only then is the mouse sure to be given back.
 */
int STDL_EnableMouse(int enable);
void    STDL_WarpMouse(uint16_t x, uint16_t y);

/*
 * The joystick calls take the joystick's number, `which`, first: 0 is
 * the joystick port (the IKBD's joystick 1) - with an Xpad provider's
 * pad 0 merged into it, unless the program keeps them apart
 * (STDL_UseJoystickMerge) - and the only joystick a program has until
 * it asks for more (STDL_SetNumJoysticks, below). A joystick there is
 * not reads 0, and its rumble returns -1. (The number, and the names
 * that say Joystick, came in v1.23.0: the stdl skill's "Moving to a
 * newer STDL".)
 *
 * Joystick `which`'s state byte, reported as events too with the same
 * `which`: bit 0 up, 1 down, 2 left, 3 right, 7 fire.
 */
uint8_t STDL_GetJoystickState(int which);

/*
 * More than one joystick, for two players or more. A program reads one
 * until it asks for more:
 *
 *   1  (the default) joystick 0 and the mouse, as always.
 *   2  two joysticks: the mouse goes off and the mouse port becomes
 *      joystick 1 (the IKBD's $12 and $14), the second stick plugged
 *      in where the mouse was. On an ST the mouse port is a mouse or a
 *      joystick, so any number from 2 turns the mouse off.
 *   3 to 6  as 2, with an Xpad provider's pads as the joysticks after,
 *      where the provider has the slots: up to 4 merged, up to 6
 *      apart, as the next comment says.
 *
 * Joystick i has fixed sources, so a player keeps their number when
 * another's pad comes or goes:
 *
 *        merged (the default)       apart
 *   0    the joystick port, pad 0   the joystick port
 *   1    the mouse port, pad 1      the mouse port
 *   2    pad 2                      pad 0
 *   3    pad 3                      pad 1
 *   4    -                          pad 2
 *   5    -                          pad 3
 *
 * Merged, a stick in a port and the pad of the same number are one
 * joystick, as joystick 0's always were: either works, and one cannot
 * release what the other holds, so where pads are present the first
 * two pads are the two joysticks with the ports still live beside
 * them. A pad slot with no pad in it is a joystick at rest, as a port
 * with no stick in it is.
 *
 * Returns how many joysticks there are now (STDL_NumJoysticks): n, or
 * fewer where a provider has fewer pad slots (3 to 6 asked for with
 * none gives 2), and 4 at most merged. -1, and no change, for n
 * outside 1 to STDL_MAX_JOYSTICKS. Callable before or after
 * STDL_Init, as STDL_EnableMouse is: the IKBD is told when the library
 * takes the keyboard, or at once if it has it.
 *
 * While there are two or more, the mouse is off whatever
 * STDL_EnableMouse says (it records what it is asked, for when the
 * program goes back to 1), STDL_GetMouseState keeps the last position
 * and buttons, and the software cursor stops. Going back to 1 gives
 * the mouse back; so do STDL_Quit and every exit, a crash included,
 * as for STDL_EnableMouse. A joystick dropped by asking for fewer
 * sends the releases of whatever it held first, and then, where the
 * program asked for device events (STDL_UseJoystickDeviceEvents), its
 * STDL_JOYDEVICEREMOVED; one added says STDL_JOYDEVICEADDED.
 *
 * Joystick key emulation (STDL_JoystickKeyEmulation and the bindings)
 * is joystick 0's alone: the others send joystick events and are
 * polled, and never make keys.
 *
 * Its own object: a program that never calls it links none of it,
 * whichever joystick it passes the calls here.
 */
#define STDL_MAX_JOYSTICKS 6
int STDL_SetNumJoysticks(int n);
int STDL_NumJoysticks(void);

/*
 * Whether each port and the Xpad pad of its number are one joystick:
 * 1 merges them (the default), 0 keeps them apart, the pads becoming
 * joysticks of their own after the two ports (the table above), and
 * -1 asks. Returns the previous setting, as STDL_UseBlitter does.
 *
 * Apart is for more players than the ports and two merged pads give
 * (six with four pads), or for a port and a pad that should be two
 * players' each. With one joystick it leaves joystick 0 the port
 * alone. It changes nothing about the mouse: the number of joysticks
 * does that.
 *
 * Callable at any time. The joysticks re-map at once: one whose
 * sources change hands sends the releases of what it held, as one
 * dropped does, and its new pad starts at rest. The number of
 * joysticks follows (6 asked for merged is 4, and 6 again apart).
 *
 * An object of its own, a few dozen bytes.
 */
int STDL_UseJoystickMerge(int on);

/*
 * Device events: STDL_JOYDEVICEREMOVED and STDL_JOYDEVICEADDED, with
 * the joystick in `which`, as joysticks come and go
 * (STDL_SetNumJoysticks) or stand for other sources - every one whose
 * pad slot changes at an STDL_UseJoystickMerge, joystick 0 always -
 * each straight after the releases it sends, so a program sees what a
 * joystick let go of before it goes. A change and its undoing between
 * two pumps (2 to 1 to 2, the merge set twice) are each said.
 *
 * Off until a program asks, as one written before them would not know
 * them, and the SDL 1.2 shim never asks (SDL 1.2 has no such event).
 * 1 turns them on and says ADDED for every joystick there is now, as
 * SDL2 does at start-up; 0 turns them off; -1 asks. Returns the
 * previous setting. They take room in the queue as any event does.
 *
 * An object of its own; the SDL2 add-on asks for them at its first
 * pump, for its SDL_JOYDEVICEADDED and REMOVED.
 */
int STDL_UseJoystickDeviceEvents(int on);

/* When enabled, joystick 0's changes also synthesise keyboard events
 * (tagged STDL_KMOD_JOYSTICK, with STDL_GetKeyState updated) so
 * keyboard-driven games work with the stick unmodified. Joystick
 * events are still delivered. The default mapping is the arrows
 * with left Alt as fire. The other joysticks never make keys. */
void STDL_JoystickKeyEmulation(int enable);

/*
 * Inputs that can be bound to a key. The four directions and fire are
 * everything an ST joystick has; the rest exist only when a modern
 * controller is present, and stay unbound and silent otherwise, so a
 * game may bind them unconditionally.
 *
 * STDL_JOYSTICK_INPUTS is how many inputs there are, not joysticks
 * (STDL_NumJoysticks says that).
 *
 * STDL_JOYSTICK_FIRE and STDL_JOYSTICK_SOUTH are the same input: the fire
 * button and a pad's lower face button are one thing.
 */
#define STDL_JOYSTICK_UP      0
#define STDL_JOYSTICK_DOWN    1
#define STDL_JOYSTICK_LEFT    2
#define STDL_JOYSTICK_RIGHT   3
#define STDL_JOYSTICK_FIRE    4
#define STDL_JOYSTICK_SOUTH   4
#define STDL_JOYSTICK_EAST    5
#define STDL_JOYSTICK_WEST    6
#define STDL_JOYSTICK_NORTH   7
#define STDL_JOYSTICK_TL      8   /* left shoulder                     */
#define STDL_JOYSTICK_TR      9   /* right shoulder                    */
#define STDL_JOYSTICK_TL2     10  /* left trigger, past halfway        */
#define STDL_JOYSTICK_TR2     11  /* right trigger, past halfway       */
#define STDL_JOYSTICK_SELECT  12
#define STDL_JOYSTICK_START   13
#define STDL_JOYSTICK_MODE    14  /* the Xbox/Home/PS button           */
#define STDL_JOYSTICK_THUMBL  15
#define STDL_JOYSTICK_THUMBR  16
#define STDL_JOYSTICK_INPUTS   17

/*
 * Whether joystick `which` has a controller richer than an ST
 * joystick behind it - an Xpad pad slot, pad `which` - i.e. whether
 * the inputs above beyond STDL_JOYSTICK_FIRE can ever be held on it.
 */
int STDL_HasPad(int which);

/*
 * Joystick `which`'s axes as its events carry them (STDL_JOYAXISMOTION's
 * axis), -32768 to 32767: X and Y the port's directions and the pad's
 * left stick, whichever is pushed further; then the pad's right stick
 * and its triggers, a trigger at rest -32768. A joystick that is not
 * there, an axis out of range, and the pad's axes without a pad read
 * 0. And its hat, the pad's d-pad (STDL_HAT_*), 0 without a pad.
 *
 * Their own object: a program that reads its sticks through
 * STDL_GetJoystickState and the events links none of it.
 */
#define STDL_JOYSTICK_AXIS_X   0
#define STDL_JOYSTICK_AXIS_Y   1
#define STDL_JOYSTICK_AXIS_RX  2   /* right stick                      */
#define STDL_JOYSTICK_AXIS_RY  3
#define STDL_JOYSTICK_AXIS_LT  4   /* left trigger                     */
#define STDL_JOYSTICK_AXIS_RT  5   /* right trigger                    */
#define STDL_JOYSTICK_AXES     6
int16_t STDL_GetJoystickAxis(int which, int axis);
uint8_t STDL_GetJoystickHat(int which);

/*
 * Rumble joystick `which`'s pad for `ms` milliseconds. `low` drives
 * the low-frequency (heavy) motor and `high` the high-frequency
 * (light) one, 0 to 255 each; which of those a pad actually has is up
 * to the pad. Each pad's rumble is its own: a new call replaces
 * whatever that pad is running, and 0, 0 stops it.
 *
 * Returns 0 when the pad's provider honours rumble and -1 otherwise -
 * no pad, one without motors, or no such joystick - so a game can
 * call it unconditionally. The pulse is ended by the event pump once
 * `ms` has passed, so a program that stops pumping keeps it running
 * until it pumps again; STDL_Quit and the terminate path end every
 * pad's too.
 */
int STDL_PadRumble(int which, uint8_t low, uint8_t high, uint16_t ms);

/*
 * Whether an input of joystick `which` is held right now. Takes the
 * same STDL_JOYSTICK_* identifiers, which do double duty: bind them to
 * keys for a game that reads the keyboard (joystick 0's), or poll them
 * for a game that has its own notion of buttons and wants them
 * directly.
 *
 * The four directions and fire come from the joystick's port or its
 * pad, whichever is driving. The rest exist only on a pad and read 0
 * without one, so a game can poll them unconditionally.
 */
int STDL_JoystickInputHeld(int which, int input);

/*
 * Bind one input to a key, or to 0 to unbind it. Returns non-zero when
 * the key resolved to something the ST keyboard actually has.
 *
 * This is how a keyboard-driven game becomes playable on a controller
 * without touching its input code: bind the pad's buttons to the keys
 * the game already reads. Safe to call while emulation is enabled; a
 * held input is released on its old key first, so nothing sticks.
 */
int STDL_JoystickKeyBind(int input, uint16_t sym);

/*
 * Change which keys the joystick emulates. Each argument is an
 * STDL_Key (e.g. STDLK_SPACE); 0 leaves that input unmapped. Only
 * keys that exist on the ST keyboard can be emulated - the return
 * value is the number of arguments successfully resolved to a key,
 * so a game can verify its bindings. Safe to call while enabled
 * (held directions are released on the old keys first).
 *
 * Shorthand for binding the five an ST joystick has; the pad-only
 * inputs above are left alone, so the two can be mixed.
 */
int STDL_JoystickKeyMapping(uint16_t up, uint16_t down, uint16_t left,
                       uint16_t right, uint16_t fire);

#ifdef __cplusplus
}
#endif

#endif /* STDL_EVENT_H */
