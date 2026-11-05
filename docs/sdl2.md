# The SDL2 add-on: SDL2 programs on a TT or a Falcon

STDL's core is planar: no chunky buffer, no conversion pass, on any
machine. The SDL2 add-on (`addons/sdl2/`) is the one exception, and
it stays apart from the core. It is an SDL2-shaped library for the
256-colour responsive screen (`stdl_responsive.h`) of a stock TT or
Falcon. A program draws byte-per-pixel textures, and the add-on
converts only what changed into the screen's eight planes.

Nothing uses it by accident. `make` does not build it, and
`libstdl.a` does not contain it. Its headers are not on STDL's include
path, so a program gets it only by naming its include path and linking
its archive.

## Using it

```
stcmd make sdl2      # libstdl-sdl2.a, libstdl-sdl2-cmini.a, dist/SDL2DEMO.TOS
m68k-atari-mint-gcc -m68020-60 -msoft-float \
    -I$STDL/addons/sdl2/include/SDL2 -I$STDL/include -c game.c
m68k-atari-mint-gcc -m68000 -o GAME.TOS game.o \
    $STDL/libstdl-sdl2.a $STDL/libstdl.a
```

Use `-I$STDL/addons/sdl2/include` instead if the program includes
`<SDL2/SDL.h>`. Never add STDL's `include/compat`: that directory
holds the SDL 1.2 shim, whose `SDL.h` would be found first, and the
add-on's headers stop the build if they meet it.

- **Compile for the 68020, link for the 68000.** Compile with
  `-m68020-60 -msoft-float`: the add-on's drawing is built that way,
  and a 32-bit multiply would otherwise be a library call. Then link
  with `-m68000`. Linked `-m68020-60`, the program gets the
  toolchain's 68020 libraries, which assume an FPU, and a stock Falcon
  refuses it: "This program requires a 68881".
- **libcmini works too:** link `libstdl-sdl2-cmini.a` and
  `libstdl-cmini.a`, as README's "Building" describes for STDL.
- **Build with `-Werror=implicit-function-declaration`.** An SDL2 call
  the headers do not declare is a compile error by design. gcc 4.6
  only warns about an undeclared call and leaves it to the link, and
  STDL's own SDL 1.2 shim in `libstdl.a` defines some SDL names with
  other signatures.
- **An ST, an STE or a Mega STE gets an error, not a crash.**
  `SDL_Init` and what it calls are built for the 68000, and refuse
  anything but a TT or a Falcon with "This program needs a TT or a
  Falcon". The program's own code, built for the 68020, must not run
  before that check.
- **STDL itself is still there.** A program may call STDL directly
  beside the add-on: its input, its voices, its clock. `SDL_Init`
  calls `STDL_Init`, which does nothing if the program called it
  first. `SDL_Quit` is `STDL_Quit`, which ends both.

`examples/sdl2demo.c` (`SDL2DEMO.TOS`) is a plain SDL2 program on the
add-on, and the pattern to copy.

## What it maps

So far, everything but audio, which comes next:

| Area | Calls |
|---|---|
| Start and stop | `SDL_Init`, `SDL_InitSubSystem`, `SDL_QuitSubSystem`, `SDL_WasInit`, `SDL_Quit`, `SDL_GetVersion` (2.0.10) |
| Errors, hints | `SDL_SetError`, `SDL_GetError`, `SDL_ClearError`, `SDL_SetHint`, `SDL_SetHintWithPriority`, `SDL_GetHint`, `SDL_GetHintBoolean`, `SDL_ClearHints` |
| Window | `SDL_CreateWindow` (at most 320x240), `SDL_DestroyWindow`, `SDL_GetWindowSize`, `SDL_SetWindowTitle` (nothing to do) |
| Renderer | `SDL_CreateRenderer`, `SDL_DestroyRenderer`, `SDL_RenderSetLogicalSize`, `SDL_RenderGetLogicalSize`, `SDL_SetRenderDrawColor`, `SDL_RenderClear`, `SDL_RenderCopy`, `SDL_RenderPresent` |
| Textures | `SDL_CreateTexture`, `SDL_CreateTextureFromSurface`, `SDL_DestroyTexture`, `SDL_UpdateTexture`, `SDL_LockTexture`, `SDL_UnlockTexture`, `SDL_QueryTexture` |
| Palettes | `SDL_AllocPalette`, `SDL_SetPaletteColors`, `SDL_FreePalette` |
| Events | `SDL_PollEvent`, `SDL_PumpEvents`, `SDL_WaitEvent`, `SDL_WaitEventTimeout`, `SDL_PushEvent`, `SDL_FlushEvent`, `SDL_FlushEvents`, `SDL_EventState`, `SDL_GetEventState`, `SDL_RegisterEvents` |
| Keyboard | `SDL_GetKeyboardState`, `SDL_GetModState`, `SDL_StartTextInput`, `SDL_StopTextInput`, `SDL_IsTextInputActive`, `SDL_GetScancodeFromKey`, `SDL_GetKeyFromScancode` |
| Mouse | `SDL_GetMouseState`, `SDL_ShowCursor` (there is no cursor) |
| Joystick | `SDL_NumJoysticks`, `SDL_JoystickNameForIndex`, `SDL_JoystickOpen`, `SDL_JoystickClose`, `SDL_JoystickName`, `SDL_JoystickInstanceID`, `SDL_JoystickNumAxes`, `SDL_JoystickNumButtons`, `SDL_JoystickNumHats`, `SDL_JoystickGetAxis`, `SDL_JoystickGetButton`, `SDL_JoystickGetHat`, `SDL_JoystickRumble`, `SDL_JoystickEventState` |
| Game controller | `SDL_IsGameController`, `SDL_GameControllerOpen`, `SDL_GameControllerClose`, `SDL_GameControllerName`, `SDL_GameControllerNameForIndex`, `SDL_GameControllerGetAttached`, `SDL_GameControllerGetJoystick`, `SDL_GameControllerGetButton`, `SDL_GameControllerGetAxis`, `SDL_GameControllerRumble`, `SDL_GameControllerEventState` |
| Surfaces | `SDL_CreateRGBSurface`, `SDL_CreateRGBSurfaceWithFormat`, `SDL_CreateRGBSurfaceFrom`, `SDL_FreeSurface`, `SDL_SetSurfacePalette`, `SDL_LockSurface`, `SDL_UnlockSurface`, `SDL_SetColorKey`, `SDL_GetColorKey`, `SDL_SetClipRect`, `SDL_GetClipRect`, `SDL_FillRect`, `SDL_BlitSurface` (`SDL_UpperBlit`) |
| Streams | `SDL_RWFromFile`, `SDL_RWFromFP`, `SDL_RWFromMem`, `SDL_RWFromConstMem`, `SDL_AllocRW`, `SDL_FreeRW`, `SDL_RWsize`, `SDL_RWseek`, `SDL_RWtell`, `SDL_RWread`, `SDL_RWwrite`, `SDL_RWclose`, `SDL_ReadU8`, `SDL_ReadLE16`/`BE16`/`LE32`/`BE32`, `SDL_WriteU8`, `SDL_WriteLE16`/`BE16`/`LE32`/`BE32` |
| Log | `SDL_Log`, `SDL_LogVerbose`, `SDL_LogDebug`, `SDL_LogInfo`, `SDL_LogWarn`, `SDL_LogError`, `SDL_LogCritical`, `SDL_LogMessage`, `SDL_LogMessageV`, `SDL_LogSetPriority`, `SDL_LogSetAllPriority` |
| Clock, platform | `SDL_GetTicks`, `SDL_GetTicks64`, `SDL_Delay`, `SDL_GetPerformanceCounter`, `SDL_GetPerformanceFrequency` (38,400), `SDL_GetPlatform` ("Atari") |

Every name is a macro onto a `stdl_sdl2_` link name, so the archive
defines no `SDL_` symbol. That keeps it clear of the 1.2 shim's
`SDL_Delay`, `SDL_FillRect` and the rest, inside `libstdl.a`. A call
not in the table is a compile error by design.

## Where it differs from SDL2

All of these come from the machine:

- **One screen, 320x240 in 256 colours.** It is TT Low (a row on two
  lines) on a TT. On a Falcon it is an 8-bit mode on VGA, or on RGB
  and a TV, where it is in overscan, 384 pixels a line with the
  picture in the middle. A window is at most 320x240 and is centred.
  The screen stays up after `SDL_DestroyWindow`, and a later window
  takes it again.
- **Textures are `SDL_PIXELFORMAT_INDEX8` only:** a byte a pixel
  through one palette of 256, which the registers hold exactly. SDL2
  itself refuses palettised textures. The screen has one palette, so
  the renderer has one: `STDL_SDL2_SetRenderPalette` gives it, and
  `SDL_SetPaletteColors` on it changes the colours. A 32-bit texture
  would need a colour lookup for every pixel before the conversion,
  more than a frame has time for.
- **No scaling.** A copy's source and destination are the same size;
  one that would scale fails, with the error set. The logical size is
  at most 320x240 and is centred, x on a 16-pixel group, so a 320x200
  program sits in the middle of the 240 rows.
- **The screen keeps what was drawn.** SDL2 leaves the back buffer
  undefined after a present, so this is within its rules. A program
  that clears and redraws every frame is still right here, and pays
  only for what it draws. A program that copies just what changed
  pays for just that, which is the pattern `SDL2DEMO` shows.
- **`SDL_RenderClear` clears the whole screen,** as SDL2 clears its
  whole target, in the palette entry nearest the draw colour (the
  lowest of equals). `STDL_SDL2_SetRenderDrawIndex` names the entry
  directly.
- **A present with nothing drawn does not flip.** It stages any colour
  change, and with `SDL_RENDERER_PRESENTVSYNC` it waits for the
  display.
- **Pump at least every 150ms.** The pump (`SDL_PollEvent`,
  `SDL_PumpEvents`, the waits) is where STDL serves its timers and
  audio and ends a pad's rumble, as an SDL2 program pumps once a frame
  anyway. The first pump turns on STDL's key repeat (SDL2's keys
  repeat, with `repeat` set) and its character translation, and sends
  `SDL_JOYDEVICEADDED` (and `SDL_CONTROLLERDEVICEADDED` with
  `SDL_INIT_GAMECONTROLLER`), as SDL2 does for a device there at
  start-up. A program that reads STDL's events itself and never pumps
  here keeps STDL as it set it.
- **Keys:** SDL2's scancodes and key codes, from STDL's symbols. An
  Atari keyboard's keys sit where SDL2 puts them on a USB board; Help,
  Undo and the keypad's brackets have SDL2 scancodes of their own.
  `SDL_TEXTINPUT` carries the ASCII character a key types; there is
  no `SDL_TEXTEDITING`. `KMOD_` values are SDL2's, which are STDL's.
- **The mouse** is in the logical size's coordinates, measured from
  its top left. There is no relative mode, wheel or cursor.
- **One joystick, 0:** an ST joystick (2 axes, 1 button) or an Xpad
  pad (6 axes, 13 buttons, a hat), always a game controller too, and
  never plugged in or out. Its state is kept whether it is open or
  not, as SDL2 keeps it; events are sent once it is open. A game
  controller's buttons are read from STDL's held inputs once a pump,
  so its d-pad is the pad's hat or the joystick's directions. Rumble
  takes the top 8 bits of each motor and at most 65,535ms.
- **Surfaces are 8-bit,** `SDL_PIXELFORMAT_INDEX8`, with a palette of
  256; a blit copies bytes, keyed or not, and blits onto itself where
  unkeyed. `SDL_CreateTextureFromSurface` copies the indices: the
  surface's palette is not the screen's, the renderer's is.
- **The log goes to a file or nowhere,** never to the console. On a TT
  a console write makes the next flip copy the whole page, and the
  text stays in the picture.

## How a frame is drawn

The screen is tracked in groups of 16 pixels, 20 to a row. Each page
keeps a mask of the groups it owes: those drawn on the screen since
that page was last drawn on.

A copy joins a batch: its texture, its place on the screen, its whole
groups, and the groups it covers in part.

- **When a batch is converted.** Normally at the present. It is
  converted sooner when the next copy comes from another texture or
  another place in this one, or when its texture is updated, locked
  or destroyed. That is SDL2's own rule for a texture's changes.
- **Whole groups** go straight from texture memory through the
  conversion.
- **A partial group** is first caught up from the page last shown,
  then merged plane by plane under its pixel mask.

The present has four steps:
1. It converts the last batch.
2. It copies into the page, from the page last shown, the groups the
   page owes that this frame did not draw. On a TT it reads a row's
   first line and writes both, so STDL's Flip has nothing to copy.
3. It hands this frame's groups to the other pages' masks.
4. It stages the colours and flips.

So each page is right whatever the program does with its pictures
between presents, and a frame costs what it drew and what the page
missed, not the screen.

`STDL_SDL2_CreateTextureFrom` lays a texture over a program's own
8-bit buffer with no copy. Its pixels are read when the batch is
converted, so the program should leave them alone until then, or
change them only after a present or a copy from elsewhere. A program
that draws into its own buffer and copies the boxes it changed pays
for no copy of its own.

## The conversion and its cost

The conversion is ten merges a group with the assembler's writes
spread among the next group's work. Its loop, 184-196 bytes, sits in
the 68030's 256-byte instruction cache, and the build fails if it
outgrows 240 bytes. Boxes under 4 groups go through its C twin.

A whole 320x240 frame into ST-RAM, in Hatari, ms:

| | X = 0 | fixed X | X at run time |
|---|---|---|---|
| TT (rows on two lines) | 39.85 | 40.36 | 42.07 |
| Falcon on VGA | 69.22 | 70.37 | 73.34 |

A frame that changes a tenth of the screen pays about a tenth of that,
plus the copies its pages missed.

## The border index

A TT's and a Falcon's border, the frame round the screen's memory that
nothing drawn can reach (a Falcon's on RGB or a TV), is register 0's
colour, and register 0 is the program's colour 0. A program whose
colour 0 is not black names a palette index it never shows, X, a
multiple of 16:

```c
STDL_SDL2_SetBorderIndex(renderer, 0xF0);   /* before anything is drawn */
```

X goes to register 0 and is held black. Every pixel is XORed with X as
it is converted, which inverts some of planes 4-7, and the palette goes
to the registers permuted to match: register i takes colour i ^ X.

The call is inline. With a constant X it links that X's conversion
alone, about 900 bytes, which costs what X = 0 does. With an X known
only at run time it links the loop that reads it, about 4.5% slower.

## Extensions (`SDL_stdl.h`)

A program includes `SDL_stdl.h` itself:

| Call or hint | What it is for |
|---|---|
| `STDL_SDL2_HINT_PAGES` | `"2"` or `"3"` (the default) pages. Three let a frame be drawn while the display still waits for the last; a third costs 153.6K of ST-RAM on a TT and 77-92K on a Falcon, and without the room STDL gives two |
| `STDL_SDL2_HINT_LOGFILE` | the file `SDL_Log` and the rest write to, a line a message, flushed after each. Without it they write nothing. SDL2's priorities: the application's from INFO, assertions' from WARN, tests' from VERBOSE, the rest CRITICAL only |
| `STDL_SDL2_HINT_CHECK` | `"1"`: the screen check. The renderer keeps a chunky model of the screen, drawn as each copy and clear is made, and compares it, converted, with every page before it flips. It costs 230K and a page's conversion a flip, and is for testing; `STDL_SDL2_GetCheck` reads it |
| `STDL_SDL2_SetRenderPalette` | the renderer's palette, staged at each present when its version has moved on |
| `STDL_SDL2_SetRenderDrawIndex` | the clear colour as an index |
| `STDL_SDL2_CreateTextureFrom` | a texture over the program's own pixels, no copy |
| `STDL_SDL2_SetBorderIndex` | the border index, above |
| `STDL_SDL2_ClearScreenNow` | the page on show cleared at once, margins and all, with no flip, the others when next drawn on. For a program that clears its screen without presenting a frame |
| `STDL_SDL2_StagePalette` | the colours staged now, with no present: a fade's steps |
| `STDL_SDL2_GetRegisterColours` | the 256 colours as last staged, in register order |
| `STDL_SDL2_GetShownPage` | the page last shown and its size, for a program that hashes what it showed |

`SDL_stdl_bench.h` is not a supported call, and may change: it gives
the present's conversion and flip times for a bench.

## Testing it

- **Host tests (`addons/sdl2/tests`, run by `make ci`):**
  - `test_basics`: errors, hints and `SDL_Init` on each machine.
  - `test_c2p`: the conversion's C against a pixel-by-pixel reference.
  - `test_render`: the renderer against a chunky model of its screen,
    drawn a pixel at a time with none of the renderer's arithmetic.
    After every flip, the page shown must be the model converted, byte
    for byte. It runs on a stand-in for STDL's screen, as a TT, a
    Falcon on VGA and on RGB, with two pages and three, and with
    border indices 0, 0x70 and 0x30.
  - `test_events`: STDL's events, queued in the stand-in, read back as
    SDL2's: keys (every symbol's scancode one key's), text, the mouse
    from the logical size, the joystick and controller, the queue.
  - `test_surface`: blits against a pixel-by-pixel reference over
    random rectangles, clipped every way, keyed or not, onto another
    surface and onto themselves; a texture made from a surface against
    the page it shows on; streams in memory and in a file; the log.
- **`SDL2CHK` (`tests/hatari/sdl2chk.c`):** every variant of the
  conversion's assembler against its C on the machine, and their
  times.
- **`SDL2RCHK`:** `test_render.c` built for the machine against the
  real STDL. `addons/sdl2/tests/test_render.c` says how to build it.
