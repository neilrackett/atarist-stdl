---
name: stdl-sdl2
description: Port an SDL2 program to a stock Atari TT or Falcon with STDL's SDL2 add-on (addons/sdl2), the 256-colour, 320x240, 8-bit-texture SDL2 subset on STDL's responsive screen. Use when porting or writing SDL2 code for a TT or a Falcon, deciding whether an SDL2 program fits the add-on, reworking a program's video, input or audio onto it, or measuring and testing it in Hatari. Covers the build, what maps and what does not, the costs that shape a port, the Atari extensions and the test loop. For SDL 1.2 programs or anything that must run on an ST, use the stdl skill instead.
---

# Porting SDL2 programs to a TT or a Falcon (STDL's SDL2 add-on)

The add-on is an SDL2-shaped library for one screen: 320x240 in 256
colours on a stock TT (TT Low) or Falcon (8 bits on VGA, RGB or a TV),
through STDL's responsive screen. A program draws byte-per-pixel
textures; the add-on converts only the 16-pixel groups each frame
changed into the screen's eight interleaved planes. `docs/sdl2.md` is
the reference; this file is how to use it well.

It is the one chunky part of STDL and stays apart from the core:
`make` does not build it, `libstdl.a` does not contain it, and a
program gets it only by naming its include path and linking its
archive.

## First, does the program fit?

It fits when it already thinks in 256 colours at 320x240 or less and
draws by copying images. It needs real work when it uses any of the
things below; an unmapped call is a **compile error** by design, so the
build lists the work.

| Program uses | On the add-on |
|---|---|
| 8-bit art, one 256-colour palette | fits: `SDL_PIXELFORMAT_INDEX8` textures and surfaces |
| 32-bit textures or surfaces (ARGB8888...) | convert the art to a 256-colour palette; there is no 32-bit path |
| more than 320x240, or renderer scaling | redesign for 320x240; nothing scales, a copy that would fails |
| `SDL_RenderDrawLine`/`Point`/`Rect`, `FillRect` on the renderer, `RenderCopyEx`, blend or colour modes, render targets | not there: draw into the program's own 8-bit buffer instead |
| `SDL_GetWindowSurface`/`UpdateWindowSurface` | not there: a texture over the program's buffer (`STDL_SDL2_CreateTextureFrom`) does the same job |
| threads, `SDL_AddTimer` | not there: no threads; do the work in the main loop |
| float or 32-bit audio, `SDL_AudioStream`, `SDL_AudioCVT` | U8, S8, S16LSB and S16MSB only |
| SDL2_mixer, SDL2_image, SDL2_ttf | not there |
| 320x200 (EGA, DOOM-era games) | fits, centred: on a TT 400 of TT Low's 480 lines, black above and below, so square pixels and a flatter picture than its 4:3 original |

A 16-colour game that does not need SDL2 is better served by STDL's
core (the `stdl` skill): ST Low natively, every ST, no conversion. The
add-on always writes eight planes.

## Building

```
stcmd make sdl2      # in STDL: libstdl-sdl2.a, libstdl-sdl2-cmini.a, dist/SDL2DEMO.TOS
m68k-atari-mint-gcc -m68020-60 -msoft-float -Werror=implicit-function-declaration \
    -I$STDL/addons/sdl2/include/SDL2 -I$STDL/include -c game.c
m68k-atari-mint-gcc -m68000 -o GAME.TOS game.o $STDL/libstdl-sdl2.a $STDL/libstdl.a
```

- **Compile for the 68020 with `-msoft-float`, link with `-m68000`.**
  The 68020 build keeps 32-bit multiplies out of library calls; the
  toolchain's 68020 libraries assume an FPU, and a stock Falcon refuses
  a program linked against them ("This program requires a 68881").
- **`-Werror=implicit-function-declaration`**: gcc 4.6 only warns about
  an undeclared call, and `libstdl.a`'s SDL 1.2 shim defines some SDL
  names with other signatures, so a missing SDL2 call could link to the
  wrong one.
- **Never add `$STDL/include/compat`** (the SDL 1.2 shim's `SDL.h`):
  the add-on's headers stop the build if they meet it. Use
  `-I.../include` instead of `.../include/SDL2` for `<SDL2/SDL.h>`.
- **libcmini:** `libstdl-sdl2-cmini.a` with `libstdl-cmini.a`.
- **C++ works**: the headers compile as C++98 under g++ 4.6.
- **An ST gets a message, not a crash**: `SDL_Init` is built for the
  68000 and refuses anything but a TT or a Falcon. Keep the program's
  own 68020 code from running before `SDL_Init`.
- `SDL2DEMO.TOS` (`examples/sdl2demo.c`) is the pattern to copy.

## The shape of a good port

**Draw into the program's own 8-bit buffer and copy what changed.**

```c
SDL_SetHint(STDL_SDL2_HINT_PAGES, "3");          /* the default */
SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO);
win = SDL_CreateWindow("Game", SDL_WINDOWPOS_CENTERED,
                       SDL_WINDOWPOS_CENTERED, 320, 200, 0);
ren = SDL_CreateRenderer(win, -1, 0);
STDL_SDL2_SetBorderIndex(ren, 0xF0);            /* if colour 0 is not black */
pal = SDL_AllocPalette(256);
STDL_SDL2_SetRenderPalette(ren, pal);
tex = STDL_SDL2_CreateTextureFrom(ren, screenbuf, 320, 200, 320);

for (;;) {
    while (SDL_PollEvent(&ev)) { ... }          /* also serves audio */
    draw(screenbuf, &dirty);                     /* the program's own drawing */
    SDL_SetPaletteColors(pal, cols, 0, 256);     /* only if they changed */
    SDL_RenderCopy(ren, tex, &dirty, &dirty);    /* what changed, not the screen */
    SDL_RenderPresent(ren);
}
```

- **The screen keeps what was drawn.** SDL2 leaves the back buffer
  undefined after a present, so this is within its rules. Clearing and
  redrawing everything is still correct, just expensive (below).
- **A texture over the program's buffer costs no copy**
  (`STDL_SDL2_CreateTextureFrom`). Its pixels are read when the add-on
  converts, at the present or when the next copy comes from another
  texture, so leave them alone until then.
- **The palette:** one per renderer, given by
  `STDL_SDL2_SetRenderPalette`; `SDL_SetPaletteColors` on it changes the
  screen's colours, staged to the blank that shows the next page. A
  colour change alone is cheap, so palette effects (fades, flashes,
  cycling) are nearly free. **Change the palette once a frame, not an
  entry at a time**: one port set 32 entries a frame with a call each,
  which cost a Falcon 1.8ms a frame against one call.
- **Fades between presents:** `STDL_SDL2_StagePalette` stages the
  colours with no present, a step a blank.
- **The border index:** a TT's and a Falcon's border is register 0,
  and the picture's colour 0 lands there too. If colour 0 is not black,
  give an index the program never shows, a multiple of 16, as a
  constant: `STDL_SDL2_SetBorderIndex(ren, 0x70)` costs nothing over
  none (a variable costs 4.5%).
- **Logical size:** at most 320x240, centred, x on a 16-pixel group.

## What things cost (Hatari, a TT at 32MHz, a Falcon at 16MHz)

- **A whole 320x240 frame converts in about 40ms on a TT and 70ms on a
  Falcon** (a full 320x200: about 34ms on a TT). A program that converts
  the whole screen every frame is capped near 25 and 14 frames a second
  by that alone. Copy only what changed.
- **A copy costs a Falcon about 1,060 cycles** (141 instructions, most
  of their time spent fetching them from ST-RAM) when both rectangles
  are given, the same size and inside the texture and the logical
  size; anything else is clipped first and costs more. Send fewer,
  taller boxes: one port sent a box per eight-row band of changed
  blocks, 16 copies a frame; merging bands that need the same blocks
  into one box took that to 9.5.
- **Round boxes out to 16-pixel groups** where the program can: a box
  of whole groups is an OR a row in the batch, one that starts or ends
  inside a group needs its partial groups merged under a pixel mask.
- **A TT writes every row to both of TT Low's lines**, so it is bound
  by its writes to ST-RAM. Its TT-RAM is fast: the add-on allocates
  textures and its working memory with `malloc`, which a program loaded
  into TT-RAM gets from there (one port's play 10% faster with it).
- **Three pages by default**: a frame is drawn while the display still
  shows the last; with two, the present waits for the blank that shows
  its page. A third page costs 153.6K of ST-RAM on a TT, 77-92K on a
  Falcon.
- **On a 68030, where a library routine sits in the instruction cache
  matters.** Its cache is 256 bytes, direct-mapped. A hot loop that
  calls `memcpy` or `memset` for every row or span can evict itself and
  the library routine on every call, and since the add-on's size moves
  the C library between builds, such a loop's speed jumps from build to
  build (one port: 0.4-1.8ms a frame on a Falcon). Copy and fill short
  runs inline in hot loops.

## Input, timing and audio

- **Pump at least every 150ms** (`SDL_PollEvent`, `SDL_PumpEvents`, the
  waits, `SDL_Delay`): that is where STDL serves its timers and audio.
  The first pump turns on key repeat and character translation and
  sends `SDL_JOYDEVICEADDED` (and the controller's), as SDL2 does.
- **Keys** have SDL2's scancodes and key codes (saved key maps keep
  working); `SDL_TEXTINPUT` carries ASCII; no `SDL_TEXTEDITING`.
- **The mouse** is in the logical size's coordinates; no relative
  mode, wheel or cursor.
- **One joystick, 0**: an ST joystick or an Xpad pad, always a game
  controller too; rumble on a pad.
- **Time:** `SDL_GetTicks`, `SDL_GetTicks64`, the performance counter
  at 38,400Hz, `SDL_Delay`.
- **Audio** is one device on STDL's ring and the DMA sound. The
  callback keeps SDL2's contract (exactly the obtained size, the format
  and rate asked for, never before the first unpause) but runs at the
  pump, not on a thread, and plays silence while `SDL_LockAudio` is
  held. The cheap spec is the ring's own: signed 8 bits at a DMA rate
  (`SDL_AUDIO_ALLOW_FREQUENCY_CHANGE` gives the nearest of 6258, 12517,
  25033 and 50066Hz; a Falcon has no 6258). Anything else is converted
  every refill.
- **`SDL_Log` goes to a file or nowhere** (`STDL_SDL2_HINT_LOGFILE`),
  never the console: on a TT a console write makes the next flip copy
  the whole page, text and all.
- STDL itself stays usable beside the add-on (its input, voices and
  clock): `SDL_Init` calls `STDL_Init`, which does nothing if the
  program called it first, and `SDL_Quit` is `STDL_Quit`.

## Testing in Hatari

- **Cycle-exact, always**: `--cpu-exact on --compatible on`.
- **A TT:** TOS 3.06, `--machine tt --mmu off` (with the MMU emulated
  the emulator falls to half real time and its timing is no truer).
  TT-RAM needs `--addr24 off` as well (`--memsize 2 --ttram 4 --addr24
  off` is 2MB of ST-RAM and 4MB of TT-RAM).
- **A Falcon:** TOS 4.04, `--machine falcon --fpu none`, `--monitor vga`
  or `rgb` (RGB's line is 384 pixels with the picture centred, so test
  both).
- **`--confirm-quit false`** whenever Hatari runs with a `HOME` or
  config of its own: its default asks to confirm a quit, and a finished
  emulator then waits on that dialog for ever.
- **`SDL_VIDEODRIVER=dummy`** runs Hatari with no window, which also
  keeps keys typed on the desktop away from the test.
- **The screen check:** `SDL_SetHint(STDL_SDL2_HINT_CHECK, "1")` before
  the renderer keeps a chunky model of the screen and compares it,
  converted, with every page before it goes up; `STDL_SDL2_GetCheck`
  says how many flips differed and where the first was. It costs 230K
  and a page's conversion a flip, so it is for testing. Break the
  program's pushes on purpose (a box a row short) and see it fail
  before trusting it.
- **Hashing what was shown:** `STDL_SDL2_GetShownPage` gives the page
  last shown and `STDL_SDL2_GetRegisterColours` its colours, for a
  program that records a frame hash.
- **Timing:** `SDL_stdl_bench.h` gives the present's conversion and
  flip times (not a supported call). For where the time goes, Hatari's
  profiler: load the program's symbols at its TEXT address (Hatari's
  `info basepage` shows it), `profile on` and `profile save` from the
  command fifo, at real speed. A function reached by a branch rather
  than a call is counted as its caller's.
- Hatari's 68030 timing is approximate: confirm figures that matter on
  a real machine.

## Rules for changing the add-on itself

STDL's `AGENTS.md`, "The SDL2 add-on", has them: the core stays
byte-identical, every public name is a macro onto a `stdl_sdl2_` link
name, `make sdl2` checks the archive (no `SDL_` symbols, headers as
gnu99 and C++98, `SDL2_MAX`, no libgcc calls), and a change to the
compositor is tested against `test_render`'s chunky model.
