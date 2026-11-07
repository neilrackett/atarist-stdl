/*
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: CC0-1.0
 *
 * STARTER: the shape of a new game on STDL, to copy and grow. One
 * screen of platforms: run and jump with the joystick or the cursor
 * keys (Up, Space or Alt jumps), take the coins, Escape to quit.
 *
 * Each part is done the way a plain 8MHz ST wants it:
 *
 * - The art is one bank, STARTER.BNK, made from three indexed PNGs
 *   (one 16-colour palette, drawn on the ST's 3-bit grid), and the
 *   music is a MIDI file made into a YM stream, both by stdlconv on
 *   the host (Pillow). In STDL's tree the PNGs are examples/art/ and
 *   `make starter-art` is the pipeline; a game copies that rule.
 * - The map is drawn once, into a background surface. Each frame
 *   puts back only what the frame before it on the same page drew
 *   (STDL_Dirty): a 16x16 sprite's box is 128 bytes, or 256 off a
 *   16-pixel group, where the map is 240 tiles.
 * - What changes rarely (a coin taken, the score) is drawn into the
 *   background once, and STDL_DirtyBackground puts it on each page
 *   as the page comes round, rather than it being drawn every frame.
 * - Three pages where there is ST RAM for them (two otherwise): a
 *   frame is drawn while the last one is still on show, and one that
 *   takes longer than a VBL does not wait for the next.
 * - The game steps 50 times a second by the 200Hz clock whatever the
 *   frame rate, so a slow frame never slows the game down.
 * - The joystick arrives as the cursor keys and Alt
 *   (STDL_JoystickKeyEmulation), so one input path serves both.
 * - Music and effects on the YM, which every ST has; effects take
 *   voice C first, and the music gets it back after each.
 * - A clean exit, which prints the score, the frames drawn and the
 *   time they took.
 *
 * A game links libcmini rather than mintlib (docs/building.md); in
 * STDL's tree this example links mintlib, as the other examples do,
 * and it runs either way.
 */

#include <stdio.h>
#include <string.h>
#include <stdl/stdl.h>

/* the stack, which libcmini's start-up and mintlib's both read */
long _stksize = 32768;

#define BANK        "STARTER.BNK"
#define MUSIC       "DEMO.STM"
#define ID_PALETTE  0           /* the bank's chunks (Makefile)      */
#define ID_TILES    1
#define ID_HERO     2
#define ID_FONT     3

#define TILE        16
#define COLS        20
#define ROWS        12
#define TOP         8           /* the score's row, above the map    */

#define T_EMPTY     0           /* tiles in the tileset              */
#define T_GROUND    1
#define T_LEDGE     2
#define T_COIN      3

/* The hero's height in 16ths of a pixel (12.4 fixed point), so that
 * gravity can be finer than a pixel a step; a step is a 50th of a
 * second. A jump climbs 4.5 tiles. */
#define RUN         2           /* pixels a step                     */
#define JUMP        (-96)
#define GRAVITY     4
#define FALL_MAX    96
#define START_X     (6 * TILE)    /* clear of the ledge above      */
#define START_Y     ((TOP + 10 * TILE) << 4)

/* '#' ground, '=' a ledge, 'o' a coin */
static const char *const level[ROWS] = {
    "....................",
    "..o..o.......o..o...",
    "..=====.....=====...",
    "....................",
    "........o..o........",
    ".......======.......",
    "..o..............o..",
    "#####..........#####",
    "#####...o..o...#####",
    "#####..======..#####",
    "#####..........#####",
    "####################",
};

static char map[ROWS][COLS + 1];
static int coins_left;
static unsigned score;

static STDL_Surface *background;
static STDL_Tileset *tiles;
static STDL_Sprite *hero_spr;
static STDL_Font *font;

static struct {
    int x;                      /* pixels                            */
    int y;                      /* 16ths of a pixel                  */
    int vy;
    int on_ground, jump_held;
    int frame, walk;
} hero;

static int pages;               /* 2 or 3                            */

/* A coin rising in pitch, and a short jump chirp. */
static const uint16_t coin_periods[] = {
    STDL_YM_PERIOD(988), STDL_YM_PERIOD(1319), STDL_YM_PERIOD(1976)
};
static const STDL_Sfx coin_sfx = { coin_periods, NULL, 3, 13, 50, 0, NULL };
static const uint16_t jump_periods[] = {
    STDL_YM_PERIOD(330), STDL_YM_PERIOD(392), STDL_YM_PERIOD(494),
    STDL_YM_PERIOD(587)
};
static const STDL_Sfx jump_sfx = { jump_periods, NULL, 4, 10, 25, 0, NULL };

static void draw_score(void)
{
    STDL_Rect hud = { 0, 0, COLS * TILE, TOP };
    char text[24];

    sprintf(text, "SCORE %05u  COINS %2d", score, coins_left);
    STDL_FillRect(background, &hud, 0);
    STDL_DrawText(background, font, 8, 0, text, 15);
    STDL_DirtyBackground(&hud);
}

/* The level from the top: the map, the background, the hero. */
static void start_level(void)
{
    int r, c;

    coins_left = 0;
    for (r = 0; r < ROWS; r++) {
        memcpy(map[r], level[r], COLS + 1);
        for (c = 0; c < COLS; c++) {
            const char t = map[r][c];

            STDL_BlitTile(tiles, t == '#' ? T_GROUND : t == '=' ? T_LEDGE
                          : t == 'o' ? T_COIN : T_EMPTY,
                          background, c * TILE, TOP + r * TILE);
            coins_left += t == 'o';
        }
    }
    STDL_DirtyBackground(NULL);         /* every page: the whole map */
    draw_score();
    hero.x = START_X;
    hero.y = START_Y;
    hero.vy = 0;
}

/* Whether a pixel of the screen is solid. Above the map is the score
 * and solid; so is anything off the sides or the bottom. */
static int solid(int px, int py)
{
    char t;

    if (px < 0 || px >= COLS * TILE || py < TOP) {
        return 1;
    }
    py = (py - TOP) >> 4;
    if (py >= ROWS) {
        return 1;
    }
    t = map[py][px >> 4];
    return t == '#' || t == '=';
}

/* The hero's box is 10 pixels wide inside its 16, and one tile tall,
 * so its corners are enough to test. */
static int hits(int x, int y)
{
    return solid(x + 3, y) || solid(x + 12, y)
        || solid(x + 3, y + 15) || solid(x + 12, y + 15);
}

/* One 50th of a second of the game. */
static void step(void)
{
    const uint8_t *keys = STDL_GetKeyState(NULL);
    const int dir = keys[STDLK_RIGHT] - keys[STDLK_LEFT];
    const int jump = keys[STDLK_UP] || keys[STDLK_SPACE] || keys[STDLK_LALT];
    int n, py, ny, cx, cy;

    if (jump && !hero.jump_held && hero.on_ground) {
        hero.vy = JUMP;
        STDL_PlaySfx(&jump_sfx, -1);
    }
    hero.jump_held = jump;

    /* across a pixel at a time, stopping at a wall */
    for (n = RUN; n > 0 && dir != 0; n--) {
        if (hits(hero.x + dir, hero.y >> 4)) {
            break;
        }
        hero.x += dir;
    }

    /* up or down a pixel at a time, stopping at a floor or ceiling */
    hero.vy += GRAVITY;
    if (hero.vy > FALL_MAX) {
        hero.vy = FALL_MAX;
    }
    py = hero.y >> 4;
    ny = (hero.y + hero.vy) >> 4;
    hero.y += hero.vy;
    while (py != ny) {
        const int t = py + (ny > py ? 1 : -1);

        if (hits(hero.x, t)) {
            hero.y = py << 4;
            hero.vy = 0;
            break;
        }
        py = t;
    }
    hero.on_ground = hits(hero.x, (hero.y >> 4) + 1);

    /* a coin under the hero's middle */
    cx = (hero.x + 8) >> 4;
    cy = ((hero.y >> 4) + 8 - TOP) >> 4;
    if (cy >= 0 && cy < ROWS && map[cy][cx] == 'o') {
        STDL_Rect tile = { 0, 0, TILE, TILE };

        map[cy][cx] = '.';
        tile.x = (int16_t)(cx * TILE);
        tile.y = (int16_t)(TOP + cy * TILE);
        STDL_BlitTile(tiles, T_EMPTY, background, tile.x, tile.y);
        STDL_DirtyBackground(&tile);
        score += 10;
        coins_left--;
        draw_score();
        STDL_PlaySfx(&coin_sfx, -1);
        if (coins_left == 0) {
            start_level();
        }
    }

    /* the frame: in the air, walking, or standing */
    if (!hero.on_ground) {
        hero.frame = 3;
    } else if (dir != 0) {
        hero.walk++;
        hero.frame = 1 + ((hero.walk >> 3) & 1);
    } else {
        hero.frame = 0;
    }
}

/* Draw a frame on the page coming up, and show it. */
static void draw(void)
{
    STDL_Surface *page = STDL_GetVideoSurface();
    STDL_Rect box;

    /* put back what this page's last frame drew over the map, and
     * whatever of the map has changed since the page was last shown */
    STDL_DirtyRestore(page);

    box.x = (int16_t)hero.x;
    box.y = (int16_t)(hero.y >> 4);
    box.w = TILE;
    box.h = TILE;
    STDL_BlitSprite(hero_spr, hero.frame, page, box.x, box.y);
    STDL_DirtyPush(&box);

    STDL_Flip();
}

static int load(void)
{
    static STDL_Colour colours[16];
    STDL_Palette pal;
    /* pre-shifted sprites draw fastest on a plain ST; a BLiTTER
     * shifts them itself, and pre-shifting would only cost RAM */
    const uint32_t shift = STDL_GetMachineInfo()->has_blitter
                         ? 0 : STDL_PRESHIFT;

    pal.ncolors = 16;
    pal.colors = colours;
    tiles = STDL_LoadTileset(BANK, ID_TILES);
    hero_spr = STDL_LoadSprite(BANK, ID_HERO, shift);
    font = STDL_LoadFont(BANK, ID_FONT);
    background = STDL_CreateSurface(COLS * TILE, 200);
    if (tiles == NULL || hero_spr == NULL || font == NULL
        || background == NULL || STDL_LoadPalette(BANK, ID_PALETTE, &pal) < 0) {
        return -1;
    }
    STDL_SetPalette(STDL_GetVideoSurface(), &pal);
    return 0;
}

int main(void)
{
    STDL_Surface *screen;
    STDL_Music *music;
    STDL_Event ev;
    uint32_t last, now, steps, ms;
    unsigned long frames = 0;
    int running = 1;

    if (STDL_Init(STDL_INIT_VIDEO | STDL_INIT_AUDIO) < 0) {
        fprintf(stderr, "STARTER: %s\n", STDL_GetError());
        return 1;
    }
    screen = STDL_SetVideoMode(320, 200, 4, STDL_TRIPLEBUF);
    if (screen == NULL) {
        fprintf(stderr, "STARTER: %s\n", STDL_GetError());
        STDL_Quit();
        return 1;
    }
    pages = (screen->flags & STDL_TRIPLEBUF) ? 3 : 2;
    if (load() < 0 || STDL_DirtyInit(background, 16) < 0) {
        fprintf(stderr, "STARTER: %s\n", STDL_GetError());
        STDL_Quit();
        return 1;
    }
    STDL_JoystickKeyEmulation(1);
    music = STDL_LoadMusic(MUSIC);
    if (music != NULL) {            /* the game plays on without it */
        STDL_PlayMusic(music, -1);
    }
    fprintf(stderr, "STARTER:ready pages=%d\n", pages);

    start_level();
    ms = STDL_GetTicks();
    last = STDL_GetHz200();
    while (running) {
        while (STDL_PollEvent(&ev)) {
            if (ev.type == STDL_QUIT || (ev.type == STDL_KEYDOWN
                && ev.key.keysym.sym == STDLK_ESCAPE)) {
                running = 0;
            }
        }
        /* 200Hz ticks, 4 a step; after a long stall (a disk load),
         * catch up at most four steps rather than run them all */
        now = STDL_GetHz200();
        steps = (now - last) >> 2;
        if (steps > 4) {
            steps = 4;
            last = now;
        } else {
            last += steps << 2;
        }
        while (steps-- > 0) {
            step();
        }
        draw();
        frames++;
    }

    ms = STDL_GetTicks() - ms;
    STDL_HaltMusic();
    STDL_FreeMusic(music);
    STDL_DirtyQuit();
    STDL_FreeSprite(hero_spr);
    STDL_FreeTileset(tiles);
    STDL_FreeFont(font);
    STDL_FreeSurface(background);
    STDL_Quit();
    printf("STARTER score=%u frames=%lu ms=%lu\n", score, frames,
           (unsigned long)ms);
    return 0;
}
