/*
 * SDL2DEMO - the SDL2 add-on on a TT or a Falcon (addons/sdl2,
 * docs/sdl2.md). Public domain (CC0).
 *
 * An SDL2 program as it would be written for any SDL2: a window, a
 * renderer, textures and a palette, with what is Atari-only from
 * SDL_stdl.h - the palette given to the renderer, as SDL2's textures
 * have none; the textures SDL_PIXELFORMAT_INDEX8, which the add-on
 * takes and SDL2 itself does not; and a border index, a colour the
 * picture never uses, so that the border round a Falcon's RGB picture
 * is black rather than colour 0 cycling with the background. It shows
 * a background of 224 shades whose colours cycle every frame, eight
 * boxes moving over it, and a ninth, made in a surface, that the arrow
 * keys, a pad's d-pad or the mouse move, with a tone while it moves
 * whose pitch follows its height. Esc, a click or the pad's Start ends
 * it, or thirty seconds.
 *
 * The pattern worth copying is in the frame loop: the screen keeps
 * what was drawn on it, so a frame copies only what changed - each
 * box's old place from the background, then the box in its new one -
 * and the present converts only those groups of 16 pixels. Cycling
 * the colours changes no pixel at all: it is the palette, staged to
 * the blank that shows the frame. A program that clears and redraws
 * the whole screen every frame is right too, and pays for converting
 * all of it: 40ms a frame on a TT, 69 on a Falcon (Hatari).
 *
 * The tone is signed 8-bit mono at 12517Hz, a DMA rate: the ring's
 * own format, which it plays with no conversion. Its callback runs
 * inside SDL_PollEvent, the waits and SDL_Delay, not on a thread of
 * its own, so the frame loop must pump: this one polls every frame.
 *
 * Built by `make sdl2`: compiled -m68020-60 -msoft-float, linked
 * against the 68000 libraries, whose 68020 ones assume an FPU.
 */

#include "SDL.h"
#include "SDL_stdl.h"

#define W      320
#define H      240
#define BOXES  8
#define BOX    24
#define SHADES 224              /* 0-223; the boxes 224-239, 240- unused */
#define RATE   12517            /* a DMA rate */

/* the tone's phase step a sample, 16.16 of a cycle; 0 is silence */
static Uint32 tone_step, tone_phase;

static void tone(void *userdata, Uint8 *stream, int len)
{
    int i;
    (void)userdata;
    for (i = 0; i < len; i++) {
        tone_phase += tone_step;
        stream[i] = (Uint8)(!tone_step ? 0 : (tone_phase & 0x8000) ? 24 : -24);
    }
}

int main(int argc, char *argv[])
{
    static Uint8 bg[W * H], box[BOX * BOX];
    SDL_Window *win;
    SDL_Renderer *ren;
    SDL_Texture *bgtex, *boxtex, *metex;
    SDL_Surface *mine, *pattern;
    SDL_GameController *pad;
    SDL_AudioSpec want;
    SDL_AudioDeviceID audio;
    SDL_Palette *pal;
    SDL_Color c[256];
    SDL_Rect pos[BOXES], me, inner;
    SDL_Event e;
    const Uint8 *keys = SDL_GetKeyboardState(NULL);
    int dx[BOXES], dy[BOXES];
    Uint32 start;
    int x, y, i, ox, oy, quit = 0;
    (void)argc;
    (void)argv;

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER |
                 SDL_INIT_AUDIO) < 0) {
        /* an ST: SDL_Init says so, built for the 68000 to get here */
        printf("SDL2DEMO: %s\n", SDL_GetError());
        return 1;
    }
    win = SDL_CreateWindow("SDL2DEMO", SDL_WINDOWPOS_CENTERED,
                           SDL_WINDOWPOS_CENTERED, W, H, 0);
    ren = win ? SDL_CreateRenderer(win, -1, SDL_RENDERER_PRESENTVSYNC)
              : NULL;
    if (!ren) {
        printf("SDL2DEMO: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    /* before anything is drawn: 0xF0 is never shown, so it is the one
     * that lands on register 0, the border's */
    STDL_SDL2_SetBorderIndex(ren, 0xF0);

    /* the background's shades, the boxes' 16, and black for the rest */
    pal = SDL_AllocPalette(256);
    for (i = 0; i < 256; i++) {
        c[i].r = c[i].g = c[i].b = 0;
        c[i].a = 255;
    }
    for (i = 0; i < SHADES; i++) {
        c[i].r = (Uint8)(i < SHADES / 2 ? i * 2 : 255 - (i - SHADES / 2) * 2);
        c[i].g = (Uint8)(i * 255 / (SHADES - 1));
        c[i].b = (Uint8)(255 - i * 255 / (SHADES - 1));
    }
    for (i = 0; i < 16; i++) {
        c[SHADES + i].r = c[SHADES + i].g = (Uint8)(i * 17);
        c[SHADES + i].b = 255;
    }
    SDL_SetPaletteColors(pal, c, 0, 256);
    STDL_SDL2_SetRenderPalette(ren, pal);

    for (y = 0; y < H; y++) {
        for (x = 0; x < W; x++) {
            bg[y * W + x] = (Uint8)((x + y * 2) % SHADES);
        }
    }
    for (y = 0; y < BOX; y++) {
        for (x = 0; x < BOX; x++) {
            box[y * BOX + x] = (Uint8)(SHADES + ((x ^ y) & 15));
        }
    }
    bgtex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_INDEX8,
                              SDL_TEXTUREACCESS_STATIC, W, H);
    boxtex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_INDEX8,
                               SDL_TEXTUREACCESS_STATIC, BOX, BOX);
    SDL_UpdateTexture(bgtex, NULL, bg, W);
    SDL_UpdateTexture(boxtex, NULL, box, BOX);

    /* the ninth box, drawn in a surface: the boxes' brightest colour
     * round a piece of their pattern, blitted from a surface over the
     * pattern's own pixels */
    mine = SDL_CreateRGBSurface(0, BOX, BOX, 8, 0, 0, 0, 0);
    pattern = SDL_CreateRGBSurfaceFrom(box, BOX, BOX, 8, BOX, 0, 0, 0, 0);
    SDL_FillRect(mine, NULL, SHADES + 15);
    inner.x = inner.y = 4;
    inner.w = inner.h = BOX - 8;
    SDL_BlitSurface(pattern, &inner, mine, &inner);
    metex = SDL_CreateTextureFromSurface(ren, mine);
    SDL_FreeSurface(pattern);
    SDL_FreeSurface(mine);
    me.x = (W - BOX) / 2;
    me.y = (H - BOX) / 2;
    me.w = me.h = BOX;
    pad = SDL_GameControllerOpen(0);

    /* the tone; the demo goes on silent without it */
    SDL_zero(want);
    want.freq = RATE;
    want.format = AUDIO_S8;
    want.channels = 1;
    want.samples = 256;
    want.callback = tone;
    audio = SDL_OpenAudioDevice(NULL, 0, &want, NULL, 0);
    if (audio) {
        SDL_PauseAudioDevice(audio, 0);
    }
    for (i = 0; i < BOXES; i++) {
        pos[i].x = 20 + i * 33;
        pos[i].y = 10 + i * 25;
        pos[i].w = pos[i].h = BOX;
        dx[i] = 1 + (i % 3);
        dy[i] = 2 - (i % 4);
        if (dy[i] == 0) {
            dy[i] = 1;
        }
    }

    /* the first frame draws everything */
    SDL_RenderCopy(ren, bgtex, NULL, NULL);
    start = SDL_GetTicks();
    while (!quit && !SDL_TICKS_PASSED(SDL_GetTicks(), start + 30000)) {
        /* where the ninth box was, before anything moves it */
        SDL_RenderCopy(ren, bgtex, &me, &me);
        ox = me.x;
        oy = me.y;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT || e.type == SDL_MOUSEBUTTONDOWN ||
                (e.type == SDL_KEYDOWN &&
                 e.key.keysym.sym == SDLK_ESCAPE) ||
                (e.type == SDL_CONTROLLERBUTTONDOWN &&
                 e.cbutton.button == SDL_CONTROLLER_BUTTON_START)) {
                quit = 1;
            } else if (e.type == SDL_MOUSEMOTION) {
                me.x = e.motion.x - BOX / 2;
                me.y = e.motion.y - BOX / 2;
            }
        }
        /* held keys and buttons, read as they stand */
        me.x += 3 * (keys[SDL_SCANCODE_RIGHT] - keys[SDL_SCANCODE_LEFT]);
        me.y += 3 * (keys[SDL_SCANCODE_DOWN] - keys[SDL_SCANCODE_UP]);
        if (pad) {
            me.x += 3 * (SDL_GameControllerGetButton(
                             pad, SDL_CONTROLLER_BUTTON_DPAD_RIGHT) -
                         SDL_GameControllerGetButton(
                             pad, SDL_CONTROLLER_BUTTON_DPAD_LEFT));
            me.y += 3 * (SDL_GameControllerGetButton(
                             pad, SDL_CONTROLLER_BUTTON_DPAD_DOWN) -
                         SDL_GameControllerGetButton(
                             pad, SDL_CONTROLLER_BUTTON_DPAD_UP));
        }
        me.x = me.x < 0 ? 0 : me.x > W - BOX ? W - BOX : me.x;
        me.y = me.y < 0 ? 0 : me.y > H - BOX ? H - BOX : me.y;
        /* 880Hz at the top to 220 at the bottom, while it moves; the
         * lock keeps the callback from seeing it half changed */
        SDL_LockAudioDevice(audio);
        tone_step = (me.x != ox || me.y != oy)
                    ? (Uint32)(880 - me.y * 660 / (H - BOX)) * 65536 / RATE
                    : 0;
        SDL_UnlockAudioDevice(audio);
        for (i = 0; i < BOXES; i++) {
            /* the background back where the box was, then the box */
            SDL_RenderCopy(ren, bgtex, &pos[i], &pos[i]);
            pos[i].x += dx[i];
            pos[i].y += dy[i];
            if (pos[i].x < 0 || pos[i].x > W - BOX) {
                dx[i] = -dx[i];
                pos[i].x += 2 * dx[i];
            }
            if (pos[i].y < 0 || pos[i].y > H - BOX) {
                dy[i] = -dy[i];
                pos[i].y += 2 * dy[i];
            }
        }
        for (i = 0; i < BOXES; i++) {
            SDL_RenderCopy(ren, boxtex, NULL, &pos[i]);
        }
        SDL_RenderCopy(ren, metex, NULL, &me);
        /* the background's shades move on a step: colours alone */
        for (i = 0; i < SHADES; i++) {
            c[i] = pal->colors[(i + 1) % SHADES];
        }
        SDL_SetPaletteColors(pal, c, 0, SHADES);
        SDL_RenderPresent(ren);
    }

    if (audio) {
        SDL_CloseAudioDevice(audio);
    }
    if (pad) {
        SDL_GameControllerClose(pad);
    }
    SDL_DestroyTexture(metex);
    SDL_DestroyTexture(boxtex);
    SDL_DestroyTexture(bgtex);
    SDL_DestroyRenderer(ren);
    SDL_FreePalette(pal);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
