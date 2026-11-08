/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * IKBDMODE: which IKBD commands turn the mouse port into a joystick,
 * on the real chip. STDL_SetNumJoysticks(2) sends $12 (mouse off) and
 * $14 (joystick event reporting); Hatari reports the mouse port after
 * $12 alone, so it cannot say which of the two the HD6301's firmware
 * needs, and a commercial two-player game sends $15 alone. This asks
 * the machine.
 *
 * It counts the packets the chip sends with a parser of its own on the
 * IKBD vector (STDL's is put back before it quits), so nothing of
 * STDL's input stands between the chip and the counts. Keys choose
 * what is sent, each after $08 (relative mouse, TOS's own mode):
 *
 *   1  $08 alone           the control: a mouse, and port 1's stick
 *   2  $14 alone           does a joystick mode free the mouse port?
 *   3  $12 alone           does mouse off alone make it a joystick?
 *   4  $12 $14             what STDL sends today
 *   5  $15, $16 a frame    interrogation, as that commercial game
 *
 * The red box follows the joystick port (port 1), the blue box the
 * mouse port (port 0); fire lights a box white. The top lines show the
 * mode and, since it was chosen, the mouse packets, $FE (port 0), $FF
 * (port 1) and $FD (both) the chip sent. Escape leaves, and the counts
 * of every mode tried go to IKBDMODE.LOG and the console.
 *
 * On the machine: a joystick in each port; in each mode move the stick
 * in the mouse port and press its fire, then plug a mouse in there,
 * move it and click. If mode 2 moves the blue box and counts $FE with
 * no mouse packets, $14 is enough and $12 is not needed.
 *
 * Its text is the starter's font (STARTER.BNK beside it, from `make`);
 * without it the boxes still move and the log is still written.
 *
 * Built by hand (keep the stem to eight characters):
 *
 *   STCMD_NO_TTY=1 stcmd m68k-atari-mint-gcc -O2 -std=gnu99 -Iinclude \
 *       -Ilib/xpad/src -o dist/IKBDMODE.TOS tests/hatari/ikbdmode.c \
 *       libstdl.a
 *
 * In Hatari (the emulator's IKBD, not the chip: a check that the probe
 * works), both ports from the keyboard, the mouse port on W X A D F:
 *
 *   SDL_VIDEODRIVER=dummy MACHINE=st FF=off TOS=tmp/tos/etos256uk.img \
 *     EXTRA="--configfile tests/hatari/joykeys.cfg --cpu-exact on \
 *     --compatible on" tests/hatari/run.sh ikbd dist/IKBDMODE.TOS 8 \
 *     "waitfor IKBDMODE:ready;key 2;sleep 0.5;keydown a;sleep 0.5;
 *      keyup a;shot;key 0x01;waitfor IKBDMODE:done"
 */

#include <stdio.h>
#include <string.h>
#include <mint/osbind.h>
#include <stdl/stdl.h>

#define ACIA_CTRL (*(volatile uint8_t *)0xFFFFFC00UL)
#define ACIA_DATA (*(volatile uint8_t *)0xFFFFFC02UL)

#define MODES   5
#define BOX     24
#define HUD     24              /* rows of text at the top           */

static const char *const mode_name[MODES + 1] = {
    "", "1: $08 ALONE", "2: $14 ALONE", "3: $12 ALONE", "4: $12 $14",
    "5: $15, $16 A FRAME"
};

/* what the parser saw since the mode was chosen, written in the ACIA
 * interrupt and read by the main loop: mouse packets, $FE, $FF, $FD */
enum { C_MOUSE, C_FE, C_FF, C_FD, KINDS };
static volatile uint16_t count[KINDS];
static volatile uint8_t port0, port1;    /* last stick bytes          */
static volatile uint8_t want_mode;       /* a digit key pressed       */
static volatile uint8_t want_quit;

static uint8_t pkt_type, pkt_left, pkt_idx, pkt_buf[8];

static uint8_t packet_len(uint8_t header)
{
    switch (header) {
        case 0xF6: return 7;
        case 0xF7: return 5;
        case 0xF8: case 0xF9: case 0xFA: case 0xFB: return 2;
        case 0xFC: return 6;
        case 0xFD: return 2;
        default:   return 1;    /* $FE, $FF */
    }
}

void ikm_process(void)
{
    while (ACIA_CTRL & 0x01) {
        const uint8_t b = ACIA_DATA;

        if (pkt_left != 0) {
            pkt_buf[pkt_idx++] = b;
            if (--pkt_left != 0) {
                continue;
            }
            if (pkt_type >= 0xF8 && pkt_type <= 0xFB) {
                count[C_MOUSE]++;
            } else if (pkt_type == 0xFE) {
                count[C_FE]++;
                port0 = pkt_buf[0];
            } else if (pkt_type == 0xFF) {
                count[C_FF]++;
                port1 = pkt_buf[0];
            } else if (pkt_type == 0xFD) {
                count[C_FD]++;
                port0 = pkt_buf[0];
                port1 = pkt_buf[1];
            }
        } else if (b >= 0xF6) {
            pkt_type = b;
            pkt_idx = 0;
            pkt_left = packet_len(b);
        } else if (b == 0x01) {
            want_quit = 1;                       /* Escape            */
        } else if (b >= 0x02 && b <= 0x01 + MODES) {
            want_mode = (uint8_t)(b - 0x01);     /* the digit keys    */
        }
    }
}

void ikm_handler(void);
__asm__(
    "\t.text\n"
    "\t.even\n"
    "_ikm_handler:\n"
    "\tmovem.l %d0-%d1/%a0-%a1,-(%sp)\n"
    "\tjsr _ikm_process\n"
    "\tmovem.l (%sp)+,%d0-%d1/%a0-%a1\n"
    "\trts\n");

static void ikbd_send(uint8_t b)
{
    while (!(ACIA_CTRL & 0x02)) {
    }
    ACIA_DATA = b;
}

/* $08 first, so every mode starts from TOS's own */
static void set_mode(int m)
{
    ikbd_send(0x08);
    switch (m) {
        case 2: ikbd_send(0x14); break;
        case 3: ikbd_send(0x12); break;
        case 4: ikbd_send(0x12); ikbd_send(0x14); break;
        case 5: ikbd_send(0x15); break;
        default: break;
    }
}

static uint16_t tally[MODES + 1][KINDS];

/* add what a mode saw to its tally, and start counting afresh */
static void close_mode(int m)
{
    int k;

    for (k = 0; k < KINDS; k++) {
        tally[m][k] += count[k];
        count[k] = 0;
    }
}

typedef struct {
    long midivec, vkbderr, vmiderr, statvec;
    long mousevec, clockvec, joyvec, midisys, ikbdsys;
} kbdvecs_t;

int main(void)
{
    static const uint8_t colour[2] = { 1, 2 };
    static uint8_t tried[MODES + 1];
    int x[2] = { 216, 80 }, y[2] = { 112, 112 };
    STDL_Surface *background;
    STDL_Font *font;
    kbdvecs_t *kv;
    long old_ikbdsys;
    int mode = 1, i;
    FILE *log;

    if (STDL_Init(STDL_INIT_VIDEO) < 0
        || STDL_SetVideoMode(320, 200, 4, STDL_DOUBLEBUF) == NULL) {
        fprintf(stderr, "IKBDMODE: %s\n", STDL_GetError());
        return 1;
    }
    STDL_SetColour(0, STDL_HWColour(0, 0, 0));
    STDL_SetColour(1, STDL_HWColour(224, 32, 32));
    STDL_SetColour(2, STDL_HWColour(64, 96, 255));
    STDL_SetColour(15, STDL_HWColour(255, 255, 255));
    font = STDL_LoadFont("STARTER.BNK", 3);     /* text, if it is there */
    background = STDL_CreateSurface(320, 200);
    if (background == NULL || STDL_DirtyInit(background, 8) < 0) {
        fprintf(stderr, "IKBDMODE: %s\n", STDL_GetError());
        STDL_Quit();
        return 1;
    }
    fprintf(stderr, "IKBDMODE:ready\n");

    /* the parser of our own; STDL's comes back before STDL_Quit, which
     * then gives the desktop its mouse as always */
    kv = (kbdvecs_t *)Kbdvbase();
    old_ikbdsys = kv->ikbdsys;
    kv->ikbdsys = (long)ikm_handler;
    set_mode(mode);
    tried[mode] = 1;

    while (!want_quit) {
        STDL_Surface *page = STDL_GetVideoSurface();
        STDL_Rect hud = { 0, 0, 320, HUD };
        char line[48];

        if (want_mode != 0) {
            close_mode(mode);
            mode = want_mode;
            want_mode = 0;
            set_mode(mode);
            tried[mode] = 1;
            port0 = port1 = 0;
        }
        if (mode == 5) {
            ikbd_send(0x16);            /* interrogate: $FD comes back */
        }

        STDL_DirtyRestore(page);
        STDL_FillRect(page, &hud, 0);
        if (font != NULL) {
            sprintf(line, "MODE %s", mode_name[mode]);
            STDL_DrawText(page, font, 4, 0, line, 15);
            sprintf(line, "MOUSE %u FE %u FF %u FD %u", count[C_MOUSE],
                    count[C_FE], count[C_FF], count[C_FD]);
            STDL_DrawText(page, font, 4, 8, line, 15);
            STDL_DrawText(page, font, 4, 16, "KEYS 1-5 MODE, ESC QUIT", 15);
        }
        for (i = 0; i < 2; i++) {
            /* box 0: the joystick port (port 1), box 1: the mouse port */
            const uint8_t s = i == 0 ? port1 : port0;
            STDL_Rect box;

            if (s & 0x01) y[i] -= 2;
            if (s & 0x02) y[i] += 2;
            if (s & 0x04) x[i] -= 2;
            if (s & 0x08) x[i] += 2;
            x[i] = x[i] < 0 ? 0 : x[i] > 320 - BOX ? 320 - BOX : x[i];
            y[i] = y[i] < HUD ? HUD : y[i] > 200 - BOX ? 200 - BOX : y[i];
            box.x = (int16_t)x[i];
            box.y = (int16_t)y[i];
            box.w = BOX;
            box.h = BOX;
            STDL_FillRect(page, &box, (s & 0x80) ? 15 : colour[i]);
            STDL_DirtyPush(&box);
        }
        STDL_Flip();
    }

    close_mode(mode);
    ikbd_send(0x08);
    kv->ikbdsys = old_ikbdsys;
    STDL_DirtyQuit();
    STDL_FreeSurface(background);
    if (font != NULL) {
        STDL_FreeFont(font);
    }
    STDL_Quit();

    log = fopen("IKBDMODE.LOG", "w");
    for (i = 1; i <= MODES; i++) {
        if (!tried[i]) {
            continue;
        }
        printf("IKBDMODE %s: mouse %u fe %u ff %u fd %u\n", mode_name[i],
               tally[i][0], tally[i][1], tally[i][2], tally[i][3]);
        if (log != NULL) {
            fprintf(log, "%s: mouse %u fe %u ff %u fd %u\r\n", mode_name[i],
                    tally[i][0], tally[i][1], tally[i][2], tally[i][3]);
        }
    }
    if (log != NULL) {
        fclose(log);
    }
    printf("IKBDMODE:done\n");
    return 0;
}
