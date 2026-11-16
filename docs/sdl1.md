# The SDL1 add-on: an SDL 1.2 program running before it is ported

Experimental. The SDL1 add-on (`addons/sdl1/`) gives an SDL 1.2
program the picture SDL 1.2 programs draw, a byte a pixel through a
palette of 256, on every machine: 256 colours on a TT or a Falcon, 16
on an ST, an STE or a Mega STE. It is for the first day of a port: the
program builds and runs with its source as it is, so its logic, input,
sound and loading can be tried on the machines before any drawing is
rewritten. It is not what a port ships on an ST. That is the planar
shim ([porting.md](porting.md)), and this add-on is built to make the
move to it small.

It is a thin layer (6.4K) over the SDL2 add-on's experimental 68000
build ([sdl2.md](sdl2.md), "An experimental build for every machine"),
which does the conversion to planes. Nothing uses it by accident:
`make` does not build it, no release has it, and a program gets it
only by naming its include path and its archives.

## Using it

```
stcmd make sdl1      # libstdl-sdl1.a, libstdl-sdl2-st.a, and the examples
m68k-atari-mint-gcc -m68000 -I$STDL/addons/sdl1/include/SDL1 \
    -I$STDL/include -c game.c
m68k-atari-mint-gcc -m68000 -o GAME.TOS game.o $STDL/libstdl-sdl1.a \
    $STDL/libstdl-sdl2-st.a $STDL/libstdl.a
```

- **Its include path in place of `include/compat`,** never beside it:
  both have an `SDL.h`, and each stops the build if it meets the
  other. `SDL_mixer.h` is there too.
- **Compiled for the 68000,** as it is for every machine. A TT and
  Falcon build of the program can be 68030 code: "A 68030 build",
  below.
- **libcmini:** `libstdl-sdl1-cmini.a` with `libstdl-sdl2-st-cmini.a`
  and `libstdl-cmini.a` ([building.md](building.md)).
- **A call it has not got is a compile error,** as in the planar shim.

Its examples are SDL 1.2's own test programs, byte for byte
(`addons/sdl1/examples/`; `args.c` gives them the command line a TOS
program has not got): `S1SPRITE.TOS` is `testsprite.c`, and
`S1GRAY.TOS` is `graywin.c`, which writes the screen surface's pixels
itself.

## A 68030 build

The archives are 68000 code, which a 68030 runs, and a program's own
68020 or 68030 objects link against them. Three ways, each run in
Hatari on the machines named, with SDL's testsprite and the screen
check on, every flip right:

| The program's objects | Linked with | Run on |
|---|---|---|
| `-m68030 -m68881` | `libstdl-sdl1.a libstdl-sdl2-st.a libstdl.a` | a TT |
| `-m68030 -m68881` | `libstdl-sdl1.a libstdl-sdl2.a libstdl.a` | a TT, a Falcon with a 68882; 25K smaller than the first |
| `-m68020-60 -msoft-float`, linked `-m68000` | `libstdl-sdl1.a libstdl-sdl2.a libstdl.a` | a TT, a stock Falcon |

- **`libstdl-sdl2.a` is the SDL2 add-on's own TT and Falcon build**
  (`stcmd make sdl2`), under this layer in place of its 68000 build.
  It has no ST screen: on an ST its `SDL_SetVideoMode` fails ("This
  program needs a TT or a Falcon"), where 68030 code would not have
  run either.
- **Objects built with `-m68881` link the toolchain's 68020-60 C
  library**, which will not start on a machine with no FPU ("requires
  a 68881"), a stock Falcon among them. The third row needs none: the
  68000 C library, and the 68020's instructions in the program alone
  ([sdl2.md](sdl2.md), "Using it").
- **STDL's archives are soft-float.** Nothing of theirs returns a
  float, which is where the two conventions differ, so they link with
  FPU code as they are.

## From here to planes

The half of a program that is no picture is already the planar shim's:
events and keys, the joystick, timers, `SDL_Delay`, audio and the
mixer are the same declarations and the same code under both
(`include/compat/SDL_shared.h`). What moves is the drawing:

1. **Build it on the add-on and run it**, on an ST and on a TT or a
   Falcon. Fix what the machine itself breaks first: file names, RAM,
   the clock ([porting.md](porting.md), "ST-specific gotchas").
2. **Build with `-DSTDL_SDL1_STRICT`.** It renames a surface's
   `pixels`, so every line that reads or writes them itself stops the
   build. Those lines are the port: on planes a surface's pixels are
   plane words. SDL's `testsprite.c` has one (line 45, the colour key
   read from pixel 0,0) and `graywin.c` one (line 73, its gradient).
3. **Rewrite those lines, one at a time, still running here.** A
   single pixel becomes `STDL_GetPixel` or `STDL_PutPixel`, which the
   add-on answers for its own surfaces; a loop of them wants fills,
   spans, blits or sprites ([porting.md](porting.md), "Rewrite
   patterns"), which are SDL's or STDL's calls and run here too where
   they are SDL's.
4. **Count the colours.** `STDL_SDL1_CountColours(screen, NULL)` says
   how many of the 256 a surface's pixels use: a planar port has 16 at
   a time, so a prototype that prints it for its busiest screens says
   how far its art is from fitting.
5. **When the strict build is clean, change the include path and the
   archives** to the planar shim's (`-I$STDL/include/compat`,
   `libstdl.a`), and convert the art to 16 colours (`stdlconv bmp16`).

`S1PORTED.TOS` is the end of that road: STDL's own planar port of
testsprite (`examples/testsprite.c`, which `make` builds as
`TSPRITE.TOS` on the planar shim) built on this add-on from the same
source, with `STDL_SDL1_STRICT`. `diff addons/sdl1/examples/testsprite.c
examples/testsprite.c` is what the port changed.

## What it has

| Area | Calls |
|---|---|
| The screen | `SDL_SetVideoMode` (at most 320x240, 8 bits), `SDL_GetVideoSurface`, `SDL_UpdateRect`, `SDL_UpdateRects`, `SDL_Flip`, `SDL_GetVideoInfo`, `SDL_ListModes`, `SDL_VideoModeOK`, `SDL_VideoDriverName` |
| Surfaces | `SDL_CreateRGBSurface`, `SDL_CreateRGBSurfaceFrom`, `SDL_FreeSurface`, `SDL_DisplayFormat`, `SDL_LoadBMP`, `SDL_LockSurface`, `SDL_UnlockSurface`, `SDL_MUSTLOCK` |
| Drawing | `SDL_BlitSurface` (`SDL_UpperBlit`), `SDL_FillRect`, `SDL_SetColorKey`, `SDL_SetClipRect`, `SDL_GetClipRect` |
| Colours | `SDL_SetColors`, `SDL_SetPalette` (`SDL_LOGPAL`, `SDL_PHYSPAL`), `SDL_MapRGB`, `SDL_GetRGB` |
| The mouse | `SDL_PollEvent`, `SDL_WaitEvent`, `SDL_GetMouseState`, `SDL_WarpMouse`, measured from the picture |
| Everything else | the planar shim's, unchanged: `SDL_Init`, `SDL_InitSubSystem`, `SDL_QuitSubSystem`, `SDL_WasInit`, `SDL_Quit`, events and keys, `SDL_Joystick*`, `SDL_AddTimer` and `SDL_SetTimer`, `SDL_Delay`, `SDL_GetTicks`, `SDL_OpenAudio`, `SDL_getenv`, `SDL_putenv` and the `Mix_` calls ([porting.md](porting.md), "What maps directly") |
| STDL's, for the move | `STDL_GetPixel`, `STDL_PutPixel` on its surfaces |
| Its own | `STDL_SDL1_CountColours`, `STDL_SDL1_SetHint`, `STDL_SDL1_SetBorderIndex`, `STDL_SDL1_GetCheck` |

`STDL_SDL1_SetHint` takes the SDL2 add-on's hints before the first
`SDL_SetVideoMode` ([sdl2.md](sdl2.md) says what each does):
`STDL_SDL1_HINT_PAGES`, `STDL_SDL1_HINT_ST_TALL`,
`STDL_SDL1_HINT_ST_DITHER`, `STDL_SDL1_HINT_CHECK` and
`STDL_SDL1_HINT_COMPARE`. The last is for a TT or a Falcon, as an ST's
screen always compares: an update converts only the 16-pixel groups
whose pixels changed. It pays where most of what a program updates is
as it was (a menu, a status bar, `SDL_Flip` of a screen that mostly
stood still), and costs a read of every pixel where all of it moved.

Every video name is a macro onto a `stdl_sdl1_` link name, as
`libstdl.a`'s own `SDL_Flip`, `SDL_FillRect` and the rest are the
planar shim's.

## Where it differs from SDL 1.2

- **8 bits a pixel, and no other depth.** A screen or surface of
  another depth is refused, the screen unless `SDL_ANYFORMAT` is
  given, when it is 8 bits. A 16- or 32-bit program needs its art in
  one palette first.
- **At most 320x240,** centred. On an ST the first mode's height
  chooses the screen: up to 200 rows ST Low, more the tall screen with
  the bottom border open ([sdl2.md](sdl2.md)).
- **An update is widened to the 16-pixel groups it touches,** which
  the SDL2 add-on compares or converts whole. A pixel written beside
  an updated rectangle, and not yet updated itself, can show with it.
- **Blits map colours as SDL 1.2's do,** each source colour to the
  nearest of the destination's palette where the two differ, so an
  unconverted picture comes out in the screen's colours. The map is
  made again when either palette changes, up to 256 colours each
  against 256: where the screen's colours change often, convert a
  picture once (`SDL_DisplayFormat`) or give it the screen's palette.
- **The screen's colours change at once,** with no flip: a fade's
  steps are `SDL_SetColors` calls. On an ST each is the add-on's
  16-colour reduction again ([sdl2.md](sdl2.md) has its costs).
- **Text printed after the mode is set is drawn on the screen,** as on
  any ST: TOS draws it on whichever page it has, and it stays there.
- **The border and the bands round a small picture are colour 0** on a
  TT and a Falcon. Where that is not black,
  `STDL_SDL1_SetBorderIndex(x)` before the mode names a colour the
  picture never shows (a multiple of 16): the SDL2 add-on's border
  index.
- **No cursor**, alpha, gamma or `SDL_SaveBMP`; timers and audio are
  cooperative, as in the planar shim.

## What a program meets on its first day

These are the planar shim's too ([porting.md](porting.md),
"ST-specific gotchas"), and a program on this add-on meets them
first. One port's 3D game found each of them.

- **Set the mode before taking the memory.** The screen's pages have
  to be ST-RAM: about 460K on a TT (three pages of 153.6K), 230-280K
  on a Falcon, 64K more than the desktop's on an ST. From `malloc`
  come a 64K screen surface and, on an ST or with the compare or the
  check, a 77K copy of the screen. A program that takes all the free
  memory for its own heap calls `SDL_SetVideoMode` first. Where a
  third page does not fit there are two, and nothing says so.
- **Only `SDL_Quit` gives the machine back.** `SDL_QuitSubSystem`
  stops nothing. To print after the picture, as a program's error
  exit does: `SDL_CloseAudio`, `SDL_Quit`, then print. `exit` and a
  crash put the desktop back too.
- **The program is in supervisor mode from `SDL_Init` to
  `SDL_Quit`,** so its own `Super(0)` pairs come out wrong: `Super(0)`
  is a toggle, which leaves supervisor mode when called in it, and
  the next read of low memory is a bus error. After `SDL_Init`, read
  the 200Hz counter at `$4BA` as it is, or call `SDL_GetTicks`.
- **Sound is fed where the program pumps,** never from an interrupt:
  `SDL_PollEvent` (`SDL_PollEvent(NULL)` too), `SDL_WaitEvent`,
  `SDL_Delay` and `STDL_PumpEvents`, not `SDL_Flip` or
  `SDL_UpdateRect`. The ring wants it every 150ms, and replays what
  it has when it comes later. A frame on an ST can take several times
  that, so call `SDL_PollEvent(NULL)` from inside the frame's long
  loops, every few rows or columns: it costs little when nothing is
  due. `SDL_LockAudio` is empty, rightly, as the callback never
  interrupts the program.
- **A rate the DMA sound has not got is resampled**: 11025Hz plays at
  12517, the callback still given what it asked for. Signed 8 bits at
  a DMA rate (6258, 12517, 25033 or 50066Hz) is the cheap one
  ([porting.md](porting.md), "What maps directly"). A plain ST has no
  DMA sound: `SDL_OpenAudio` fails there, and the program runs silent.
- **Draw into the screen surface, not into a buffer copied to it,**
  and update what changed (`SDL_UpdateRects`) where the program knows
  it: `SDL_Flip` hands over the whole screen.

## What it costs

SDL's testsprite, 16 sprites on a 320x200 screen, in Hatari
(cycle-exact; `S1SPRITE.TOS` built to end by itself after 20 seconds):

| | Frames a second |
|---|---|
| ST, STE (8MHz) | 2.3 |
| Mega STE | 4.2 |
| TT | 30.1 |
| Falcon, VGA | 15.1 |

A real TT takes 2.3 times as long as Hatari's over the same code and
three to four times over copies into ST-RAM ([sdl2.md](sdl2.md)), so
take those two as an order of magnitude. On an ST it is for seeing a
program run, not for playing it.

A program on it is larger than its planar port: `S1SPRITE.TOS` is
225,656 bytes against `TSPRITE.TOS`'s 187,423 (mintlib is 104K of
each). The add-on is 6.4K, the SDL2 add-on's 68000 build 33K, and STDL
58K, 18K of it the 8-plane routines a TT and a Falcon draw with. A
program that calls `SDL_Delay`, a timer or a joystick links the planar
shim's `compat.o` for them, which brings 14K of planar drawing it
never runs (`draw.o`); splitting that object would save it.

## Testing it

- **`test_sdl1` (`addons/sdl1/tests`, run by `make ci`):**
  - surfaces against a model that draws a pixel at a time: random
    fills, blits clipped every way, keyed, onto themselves and between
    palettes, colour keys, clip rectangles, palette changes, the two
    pixel calls, over 6,000 steps, every surface compared after each;
  - the screen on the SDL2 add-on itself, on its stand-in for STDL: a
    TT, a Falcon on VGA and on RGB, two pages and three, three border
    indices, five picture sizes. After every update the page shown is
    decoded a pixel at a time and must be the screen surface as it was
    at each update, widened as above; the registers must be the
    colours set; the mouse must be measured from the picture;
  - `test_sdl1_st`, against the SDL2 add-on's 68000 build, runs an
    ST's screens first, where the SDL2 add-on's own check must agree
    at every flip.

  35 breakages of the add-on, each made on purpose, all failed it (one
  only once the test modelled the screen surface's own palette).
- **On the machine** (Hatari, cycle-exact): `S1SPRITE` and `S1GRAY`
  built with the screen check on
  (`SDL1_EXTRA="-DSDL1_RUN=15 -DSDL1_CHECK"`) on an ST, an STE, a Mega
  STE, a TT and a Falcon on VGA: every flip as its updates made it. No
  real machine has run it.
- **`make sdl1` checks** that the archives define no `SDL_` symbol,
  that the headers compile as gnu99 and as C++98 with warnings as
  errors, that they stop where they meet the planar shim's, and that
  `STDL_SDL1_STRICT` stops a line that touches a surface's pixels and
  nothing else.
