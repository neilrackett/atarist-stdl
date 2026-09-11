/*
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: CC0-1.0
 *
 * STE hardware scrolling demo: a 512x320 play field scrolled to the
 * pixel with the arrow keys or a joystick, without a byte of it
 * ever being copied.
 *
 * The intended pattern: draw the world (or a window of it one tile
 * larger than the screen) into an ordinary surface whose width is
 * at least 336, then once per frame call STDL_SetScrollOrigin with
 * the window's top-left pixel and wait for the VBL. The library
 * programs the three STE registers in the order the Shifter needs -
 * the base a frame ahead of the offsets - so the picture never
 * jumps when the fine scroll crosses a group boundary. The chequer
 * is drawn with 1-pixel black grid lines every 16 pixels: watching
 * them creep proves the scroll is pixel-exact, and their position
 * in a screenshot reads the offset back.
 *
 * SPACE toggles a bouncing auto-scroll, ESC quits. On a plain ST
 * the call fails cleanly and the program says so.
 */

#include <stdio.h>
#include <stdl/stdl.h>

#define WORLD_W 512
#define WORLD_H 320
#define CELL    16

static void paint(STDL_Surface *world)
{
    STDL_Rect r;
    int cx, cy;

    for (cy = 0; cy < WORLD_H / CELL; cy++) {
        for (cx = 0; cx < WORLD_W / CELL; cx++) {
            /* colour 0 is kept for the grid, so cells cycle 1..15 */
            uint8_t col = (uint8_t)(1 + (cx + cy) % 15);
            r.x = (int16_t)(cx * CELL + 1);
            r.y = (int16_t)(cy * CELL + 1);
            r.w = CELL - 1;
            r.h = CELL - 1;
            STDL_FillRect(world, &r, col);
        }
    }
    /* a solid frame at the world's true edges: scrolling to a corner
     * must show it flush against the screen edge */
    r.x = 0; r.y = 0; r.w = WORLD_W; r.h = 2;
    STDL_FillRect(world, &r, 15);
    r.y = WORLD_H - 2;
    STDL_FillRect(world, &r, 15);
    r.x = 0; r.y = 0; r.w = 2; r.h = WORLD_H;
    STDL_FillRect(world, &r, 15);
    r.x = WORLD_W - 2;
    STDL_FillRect(world, &r, 15);
}

int main(int argc, char *argv[])
{
    STDL_Surface *screen, *world;
    STDL_Colour cols[16];
    int x = 0, y = 0, dx = 1, dy = 1, autoscroll = 0;
    uint32_t t0, frames = 0, late = 0, late2 = 0;
    int i;

    (void)argc; (void)argv;
    if (STDL_Init(STDL_INIT_VIDEO | STDL_INIT_JOYSTICK) < 0) {
        return 1;
    }
    screen = STDL_SetVideoMode(320, 200, 4, 0);
    if (screen == NULL) {
        STDL_Quit();
        return 1;
    }
    if (!STDL_HasHwScroll()) {
        STDL_Quit();
        printf("HWSCROLL: this machine has no STE Shifter, so there "
               "is no hardware scrolling to show.\n");
        return 1;
    }

    /* a plain 16-colour ramp so the cells read as a gradient */
    for (i = 0; i < 16; i++) {
        cols[i].r = (uint8_t)(i * 17);
        cols[i].g = (uint8_t)(255 - i * 12);
        cols[i].b = (uint8_t)((i & 3) * 80);
        cols[i].unused = 0;
    }
    cols[0].r = cols[0].g = cols[0].b = 0;
    cols[15].r = cols[15].g = cols[15].b = 255;
    STDL_SetColours(screen, cols, 0, 16);

    world = STDL_CreateSurface(WORLD_W, WORLD_H);
    if (world == NULL) {
        STDL_Quit();
        printf("HWSCROLL: %s\n", STDL_GetError());
        return 1;
    }
    paint(world);

    if (STDL_SetScrollOrigin(world, 0, 0) < 0) {
        STDL_FreeSurface(world);
        STDL_Quit();
        printf("HWSCROLL: %s\n", STDL_GetError());
        return 1;
    }
    STDL_JoyKeyEmulation(1);        /* a stick drives the arrows */

    t0 = STDL_GetTicks();
    for (;;) {
        STDL_Event ev;
        const uint8_t *keys;

        while (STDL_PollEvent(&ev)) {
            if (ev.type == STDL_QUIT) {
                goto done;
            }
            if (ev.type == STDL_KEYDOWN) {
                if (ev.key.keysym.sym == STDLK_ESCAPE) {
                    goto done;
                }
                if (ev.key.keysym.sym == STDLK_SPACE) {
                    autoscroll = !autoscroll;
                }
            }
        }
        keys = STDL_GetKeyState(NULL);
        if (autoscroll) {
            x += dx;
            y += dy;
            if (x <= 0 || x >= WORLD_W - 320) dx = -dx;
            if (y <= 0 || y >= WORLD_H - 200) dy = -dy;
        } else {
            if (keys[STDLK_LEFT])  x--;
            if (keys[STDLK_RIGHT]) x++;
            if (keys[STDLK_UP])    y--;
            if (keys[STDLK_DOWN])  y++;
        }
        if (x < 0) x = 0;
        if (x > WORLD_W - 320) x = WORLD_W - 320;
        if (y < 0) y = 0;
        if (y > WORLD_H - 200) y = WORLD_H - 200;

        STDL_SetScrollOrigin(world, x, y);
        STDL_WaitVBL();
        if (STDL_ScrollWindowPending()) {
            late++;                 /* not on screen after one VBL */
            STDL_WaitVBL();
            if (STDL_ScrollWindowPending()) {
                late2++;            /* nor after two */
            }
        }
        frames++;
    }
done:
    {
        uint32_t ms = STDL_GetTicks() - t0;
        STDL_ResetScrollWindow();
        STDL_FreeSurface(world);
        STDL_Quit();
        printf("HWSCROLL: %lu frames in %lu ms, %lu still pending after "
               "one VBL, %lu after two\n",
               (unsigned long)frames, (unsigned long)ms,
               (unsigned long)late, (unsigned long)late2);
    }
    return 0;
}
