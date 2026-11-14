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
  `libstdl-cmini.a`, as [building.md](building.md) describes for STDL.
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

The calls it has:

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
| Surfaces | `SDL_CreateRGBSurface`, `SDL_CreateRGBSurfaceWithFormat`, `SDL_CreateRGBSurfaceFrom`, `SDL_FreeSurface`, `SDL_SetSurfacePalette`, `SDL_LockSurface`, `SDL_UnlockSurface`, `SDL_SetColorKey`, `SDL_GetColorKey`, `SDL_SetClipRect`, `SDL_GetClipRect`, `SDL_FillRect`, `SDL_BlitSurface` (`SDL_UpperBlit`), `SDL_LoadBMP`, `SDL_LoadBMP_RW` (uncompressed, 1, 4 or 8 bits: below) |
| Streams | `SDL_RWFromFile`, `SDL_RWFromFP`, `SDL_RWFromMem`, `SDL_RWFromConstMem`, `SDL_AllocRW`, `SDL_FreeRW`, `SDL_RWsize`, `SDL_RWseek`, `SDL_RWtell`, `SDL_RWread`, `SDL_RWwrite`, `SDL_RWclose`, `SDL_ReadU8`, `SDL_ReadLE16`/`BE16`/`LE32`/`BE32`, `SDL_WriteU8`, `SDL_WriteLE16`/`BE16`/`LE32`/`BE32` |
| Log | `SDL_Log`, `SDL_LogVerbose`, `SDL_LogDebug`, `SDL_LogInfo`, `SDL_LogWarn`, `SDL_LogError`, `SDL_LogCritical`, `SDL_LogMessage`, `SDL_LogMessageV`, `SDL_LogSetPriority`, `SDL_LogSetAllPriority` |
| Audio | `SDL_OpenAudio`, `SDL_OpenAudioDevice`, `SDL_PauseAudio`, `SDL_PauseAudioDevice`, `SDL_GetAudioStatus`, `SDL_GetAudioDeviceStatus`, `SDL_LockAudio`, `SDL_LockAudioDevice`, `SDL_UnlockAudio`, `SDL_UnlockAudioDevice`, `SDL_CloseAudio`, `SDL_CloseAudioDevice`, `SDL_QueueAudio`, `SDL_GetQueuedAudioSize`, `SDL_ClearQueuedAudio`, `SDL_MixAudio`, `SDL_MixAudioFormat`, `SDL_LoadWAV`, `SDL_LoadWAV_RW`, `SDL_FreeWAV`, `SDL_GetNumAudioDevices`, `SDL_GetAudioDeviceName`, `SDL_GetNumAudioDrivers`, `SDL_GetAudioDriver`, `SDL_GetCurrentAudioDriver` |
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
  anyway. `SDL_PollEvent` pumps once a VBL, at the first call after
  the display's VBL, and otherwise takes from the queue: a drain loop
  is one pump a frame however many events it takes (it was two for
  each event, 18-19ms of a Falcon's frame with four pads and 16 keys;
  now 3.5ms), and an event comes at most a VBL later than a pump at
  every call would have given it. `SDL_PumpEvents` and the waits pump
  whenever called. The first pump turns on STDL's key repeat (SDL2's keys
  repeat, with `repeat` set), its character translation and its device
  events (`STDL_UseJoystickDeviceEvents`), which say every joystick
  there is then, as SDL2 does for the devices there at start-up, and
  from then each one STDL adds, drops or re-maps, straight after the
  releases it brings: each becomes `SDL_JOYDEVICEADDED` or `REMOVED`
  (and `SDL_CONTROLLERDEVICEADDED` or `REMOVED` with
  `SDL_INIT_GAMECONTROLLER`), a change undone before the next pump
  included. A program that reads STDL's events itself and never pumps
  here keeps STDL as it set it.
- **Keys:** SDL2's scancodes and key codes, from STDL's symbols. An
  Atari keyboard's keys sit where SDL2 puts them on a USB board; Help,
  Undo and the keypad's brackets have SDL2 scancodes of their own.
  `SDL_TEXTINPUT` carries the ASCII character a key types; there is
  no `SDL_TEXTEDITING`. `KMOD_` values are SDL2's, which are STDL's.
- **The mouse** is in the logical size's coordinates, measured from
  its top left. There is no relative mode, wheel or cursor.
- **Joysticks: STDL's, by the same numbers.** One, joystick 0 (the
  joystick port and an Xpad provider's pad 0), unless the program asks
  STDL for more: `STDL_SetNumJoysticks(n)` (`stdl_event.h`), called
  directly beside the add-on. 2 makes the mouse port joystick 1 (with
  pad 1) and turns the mouse off while there are two - the IKBD
  reports one or the other - and 3 and 4 add pads 2 and 3 where the
  provider has those slots. With `STDL_UseJoystickMerge(0)` the pads
  are joysticks of their own after the two ports, 2 to 5, six at most;
  when it changes, every joystick whose pad changes stands for other
  sources, so STDL says `REMOVED` and `ADDED` again for each, after the
  releases. Each is an ST joystick (2 axes, 1 button)
  or, with a pad behind it, 6 axes, 13 buttons and a hat, always a
  game controller too, its instance ID its index. Ask before the first
  pump and its device events cover them all; ask later and STDL says
  each change as it makes it (no physical plugging is seen: a pad slot
  with nothing in it is a joystick at rest). Its state is STDL's, open
  or not, as SDL2 keeps a joystick's: the axes and the hat
  (`STDL_GetJoystickAxis`, `STDL_GetJoystickHat`) and the buttons
  (`STDL_JoystickInputHeld`), whether polled from the joystick or the
  controller, so the two agree; events are sent once it is open. A
  controller's buttons are read at a pump that brought its joystick an
  event, a device event included, and their changes sent, so its d-pad
  is the pad's hat or the joystick's directions; one that goes lets go
  of everything first, and comes back at rest.
  Rumble is each pad's own, takes the top 8 bits of each motor and at
  most 65,535ms. The code behind joysticks 1-5 is linked only where
  the program asks; the device events' call, which the first pump
  makes, and the axis and hat readers are 364 bytes in every program
  (`joydev.o` 120, `joyread.o` 244).
- **Surfaces are 8-bit,** `SDL_PIXELFORMAT_INDEX8`, with a palette of
  256; a blit copies bytes, keyed or not, and blits onto itself where
  unkeyed. `SDL_CreateTextureFromSurface` copies the indices: the
  surface's palette is not the screen's, the renderer's is.
  `SDL_LoadBMP` reads 8-bit BMPs, and 1- and 4-bit ones as 8-bit, but
  not RLE or 16 to 32 bits (the next section).
- **The log goes to a file or nowhere,** never to the console. On a TT
  a console write makes the next flip copy the whole page, and the
  text stays in the picture.
- **Audio is one output device on STDL's ring** (`stdl_audio.h`): the
  DMA sound at the nearest of its rates (6258, 12517, 25033 or
  50066Hz; a Falcon has no 6258), resampled from the program's and
  converted to signed 8 bits. The callback still gets SDL2's
  contract: exactly the obtained size each call, in the format and at
  the rate it asked for, never before the first unpause. Its bytes go
  through a buffer of that size on their way to the ring, which asks
  for about 2,048 frames at a time.
  - **It runs where STDL refills the ring:** inside `SDL_PollEvent`
    (at its pump, once a VBL), `SDL_PumpEvents`, the waits and
    `SDL_Delay`, not on a thread. So a program pumps at least every
    150ms while audio plays, and a refill while `SDL_LockAudio` is held
    plays silence.
  - **Formats:** `AUDIO_U8`, `AUDIO_S8`, `AUDIO_S16LSB` and
    `AUDIO_S16MSB`, mono or stereo. Float and 32 bits are refused
    without `SDL_AUDIO_ALLOW_FORMAT_CHANGE`, which gives
    `AUDIO_S16SYS`: converting a float a sample with no FPU costs more
    than the sound is worth. SDL2's converters (`SDL_AudioCVT`,
    `SDL_AudioStream`) are not here.
  - **The cheap spec is the ring's own:** signed 8 bits at a DMA rate,
    which it plays with no conversion. `SDL_AUDIO_ALLOW_FREQUENCY_CHANGE`
    gives the DMA rate nearest the one asked for. Anything else is
    converted at every refill: STDL's `stdl_audio.h` gives the costs.
  - **WAV files** are uncompressed PCM, 8 or 16 bits; `stdlconv wav`
    converts the rest offline. Mixing and WAV reading are objects of
    their own, linked only if called.

## Getting art in: `stdlconv bmp8` and `SDL_LoadBMP`

The screen has one palette, so every image a program shows at once
must use it. `stdlconv bmp8` (`tools/stdlconv/stdlconv.py`, on the host
with Python 3 and Pillow) converts a program's images together into
uncompressed 8-bit BMPs that all carry the same palette, and
`SDL_LoadBMP` loads each as an INDEX8 surface with it:

```
python3 $STDL/tools/stdlconv/stdlconv.py bmp8 art/title.png art/sprites.png \
    -o dist --tt --zero 000000 --key FF00FF
```

writes `dist/TITLE.BMP` and `dist/SPRITES.BMP` (each input's name, upper
case, as TOS keeps it). The palette is the art's own colours when they
fit, and otherwise a median cut over every image at once, refined by
k-means. Art already indexed through one shared palette keeps its
indices, since a program may count on them (a key, a range it cycles),
unless `--zero`, `--key`, `--border` or `--colors` needs entries of its
own.

| Option | What it does |
|---|---|
| `-o DIR` | where the BMPs go (default `.`) |
| `--colors N` | at most N entries, 2-256 (default 256) |
| `--tt` | the palette snapped to a TT's 4 bits a channel, rounded, so a TT shows it as written. A Falcon keeps 6 bits and shows each of those 16 levels within 2.4 of 255. Without it, a TT shows each channel's top 4 bits, up to 15 of 255 darker than the file says, and a Falcon its top 6 |
| `--key RRGGBB[=N]` | pixels of exactly that colour, and transparent ones, as index N (default the last), which no other colour takes: give N to `SDL_SetColorKey` |
| `--zero RRGGBB` | index 0 that colour, otherwise one like any other. Register 0 is also the border round a TT's or a Falcon's picture (above), so `000000` keeps it black with no border index |
| `--border X` | index X (a multiple of 16) left unused and black, for art whose colour 0 is not black: `STDL_SDL2_SetBorderIndex(renderer, X)` |
| `--dither` | Floyd-Steinberg onto the palette (default none, so flat areas stay flat) |
| `--pal FILE` | the palette alone as well, 4 bytes an entry (r, g, b, 255), to read into an `SDL_Color` array for `SDL_SetPaletteColors` |

Loading them is SDL2's own code. The screen's palette can come from
any of the files, as they all carry it:

```c
SDL_Surface *title = SDL_LoadBMP("TITLE.BMP");
SDL_Surface *sprites = SDL_LoadBMP("SPRITES.BMP");
SDL_SetPaletteColors(pal, title->format->palette->colors, 0,
                     title->format->palette->ncolors);
STDL_SDL2_SetRenderPalette(renderer, pal);
SDL_SetColorKey(sprites, SDL_TRUE, 255);        /* --key's index */
tex = SDL_CreateTextureFromSurface(renderer, title);
```

A program that loads its art through SDL2_image changes `IMG_Load` to
`SDL_LoadBMP` and its files' names to the converted ones; it gets an
`SDL_Surface` either way.

`SDL_LoadBMP` and `SDL_LoadBMP_RW` take uncompressed BMPs of 1, 4 or 8
bits a pixel, bottom-up or top-down, with Windows' 40-byte header or a
later one (V2 to V5). The surface is always `SDL_PIXELFORMAT_INDEX8`,
1- and 4-bit pixels a byte each, with the file's palette, its
`ncolors` the file's count, as SDL2 gives it. What it refuses, it
refuses with the error set in SDL2's words: RLE ("Compressed BMP files
not supported"), 16, 24 or 32 bits ("24-bpp BMP images are not
supported"), OS/2's 12-byte header, which SDL2 does read ("BMP file
with bad dimensions (0x0)", as SDL2 says of a header it does not
know), and a file cut short ("Error reading from datastream"). It is
an object of its own, 1,036 bytes, linked only by a program that calls
it.

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

A copy itself costs a Falcon about 1,060 cycles in Hatari, 141
instructions most of whose time goes on fetching them from ST-RAM,
when both rectangles are given, are the same size and lie inside the
texture and the logical size; anything else is clipped first. So
fewer, taller boxes are cheaper than many short ones: one port that
sent each eight-row band's changed blocks as a box of its own made 16
copies a frame, and sending bands that need the same blocks as one box
took that to 9.5 and the copies from 1.09ms a frame to 0.70.

## The conversion and its cost

The conversion is ten merges a group with the assembler's writes
spread among the next group's work, and a box of any size is one
call. Its loop, with the code that moves it from row to row, is
214-240 bytes and sits in the 68030's 256-byte instruction cache; the
build fails if it outgrows 240 bytes.

A whole 320x240 frame into ST-RAM, in Hatari, ms:

| | X = 0 | fixed X | X at run time |
|---|---|---|---|
| TT (rows on two lines) | 31.02 | 31.94 | 33.06 |
| Falcon on VGA | 59.99 | 61.58 | 63.70 |

Narrow boxes cost a little more a group, for the row's start: a
frame's worth of boxes two groups wide, a sprite's, takes a fixed X
36.47ms on a TT and 69.11 on a Falcon. Until November 2026 boxes that
narrow went through the C twin, at 68.40 and 95.50, and whole frames
took 40.36 and 70.37.

A frame that changes a tenth of the screen pays about a tenth of that,
plus the copies its pages missed.

Every time here is Hatari's. A real TT takes 2.3 times as long as
Hatari's TT over the same compiled code, and 3.0 to 3.9 times over
copies into ST-RAM, where the conversion writes (2026-11-13, TOS 3.06
on both); a real Falcon has not been timed. Compare builds by these
figures, and take a frame budget from a machine.
`tests/hatari/cpuchk.c` (CPUCHK) measures the difference on one.

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

## An experimental build for every machine

`make sdl2st` builds the add-on a second time, for the 68000:
`libstdl-sdl2-st.a` and `libstdl-sdl2-st-cmini.a`, and the demo as
`dist/SDL2ST.TOS`. A program linked with it runs on every machine. On
a TT or a Falcon it has the 256-colour screen and shows what the
add-on built for them shows, frame for frame; on an ST, an STE or a
Mega STE it has a 16-colour screen that is for testing (below). It is
experimental: `make sdl2` does not build it, the archives a TT or a
Falcon links have none of it, it is not in a release and has no size
budget, no real machine has run it, and what it does may change.

```
STCMD_NO_TTY=1 stcmd make sdl2st
m68k-atari-mint-gcc -m68000 -Werror=implicit-function-declaration \
    -I$STDL/addons/sdl2/include/SDL2 -I$STDL/include -c game.c
m68k-atari-mint-gcc -m68000 -o GAMEST.TOS game.o \
    $STDL/libstdl-sdl2-st.a $STDL/libstdl.a
```

The program's source does not change: it is compiled for the 68000
and linked against the other archive. The machine is asked at
`SDL_CreateWindow` (`_MCH`, as `SDL_Init` asks it in the other build).

- **On a TT or a Falcon it is the other build's screen**, through the
  same compositor and the same assembler, which has no instruction a
  68000 lacks. One port's game, built both ways, showed the same page
  and colours at every one of a replay's 640 flips on a TT and on a
  Falcon on VGA (Hatari). What differs is that the program and the
  compositor are 68000 code, with a 32-bit multiply a library call:
  that game's frames took 0-5% longer on a TT (18.67ms against 18.69,
  21.87 against 20.76) and 6-7% on a Falcon (32.90 against 30.97,
  43.01 against 40.33), the conversion's share 3.9ms against 3.1 and
  7.5 against 6.0. A program for those machines alone still wants the
  other build.

- **None of it reaches the 68020 build.** Every line of it is under
  `STDL_SDL2_ST`, which only `make sdl2st` defines, and
  `internal.h` stops the build if that macro meets a compiler
  targeting anything above a 68000. Adding it left all 66 of the
  add-on's objects byte for byte as they were.

### An ST's screen, for testing

It is there to try a program's logic, input and sound where there is
no TT or Falcon to hand, not to play it: the picture is a rough
16-colour one and a whole screen takes an 8MHz ST 0.64s.

- **The screen follows the first window's height**, as the mode is
  set once a program. A window of up to 200 rows gets ST Low, all of it there. A taller one gets STDL's tall
  screen (`STDL_RESPONSIVE_TALL`, `docs/responsive.md`): 240 rows with
  an ST's bottom border open, so a 320x240 window is all there too.
  That screen has two pages, the border's own, whatever
  `STDL_SDL2_HINT_PAGES` says, and the border's rules apply
  (`docs/overscan.md`: on a real machine disk access and a moving
  mouse make it flicker). If the border cannot open the screen is ST
  Low and shows the middle 200 rows of the add-on's 240. The mouse is
  reported to match either way.
- **The palette is reduced to 16 colours** each time it changes: 15
  slots and black. The palette's colours start in one box, and the
  box widest in any channel is cut at the middle of that channel until
  there are 15; a slot shows the mean of its box. So up to 15 colours
  beside black are shown exactly, and more are shared out by the
  palette alone, with no count of the pixels using them. Register 0
  is black, the border's on an ST, and takes the palette's blacks and
  the border index, which needs no XOR here.
- **A palette change can cost a screen.** Each page holds slots, not
  colours, so when the reduction moves any colour to another slot the
  next page drawn on is converted again whole, from a chunky copy of
  the screen the renderer keeps (76.8K), and the other pages catch up
  from it. The map in use
  stays while it shows the new colours nearly as well as a new one
  would (its error within a quarter of the new one's), and then only
  the 16 registers change: a fade to black and back keeps one map.
- **Where it differs from the TT's and Falcon's:**
  - a present flips for a new map though nothing was drawn, and
    `STDL_SDL2_StagePalette` presents when the map moves, what was
    copied so far with it;
  - `STDL_SDL2_GetRegisterColours` gives the 16 registers in its first
    16 entries, and `STDL_SDL2_GetShownPage` the page's rows, 160
    bytes each (32,000 bytes, or the tall screen's 38,400);
  - `STDL_SDL2_HINT_CHECK` works, and compares the page's rows;
  - sound needs an STE's DMA sound, as STDL's ring does: on a plain ST
    `SDL_OpenAudio` fails;
  - the add-on's size and libgcc checks (`sdl2check`) are not run on
    it: on a 68000 a 32-bit multiply is a library call.
- **What it costs** on ST Low (Hatari; an ST at 8MHz, a Mega STE at
  16MHz with its cache; the tall screen's whole screen is a fifth more
  rows, not timed):

  | | ST | Mega STE |
  |---|---|---|
  | A whole 320x200 screen copied and presented | 644ms | 355ms |
  | A 64x48 box | 54ms | 31ms |
  | A palette change that keeps the map | 96ms | 49ms |
  | A fade's step (256 colours, to black in 16) | 205ms | 110ms |
  | A clear and its present | 121ms | 87ms |

  The conversion is a table lookup a pixel in C, about 60 cycles; the
  fade's step was 759ms on an ST before the map was kept.
- **Tested** by `test_render_st` on the host (`make ci` runs it):
  `test_render.c` built with `STDL_SDL2_ST`, the page shown against
  the model's middle rows put through the renderer's map a pixel
  at a time, on the tall screen's 240 rows, on 200 where the border
  did not open and on a border cut short, with two pages and three
  and border indices 0 and 0x70, and the reduction against what it
  promises. Then it runs `test_render`'s 18 TT and Falcon screens
  through this build, which must show them as the other does. Eleven
  breakages of the ST path each failed it, and ten more of it as it
  stands. `SDL2RCST` is the same on the machine (`test_render.c` says
  how to build it), and passed against the real STDL on Hatari's ST,
  tall, and on its TT and its Falcon on VGA; `SDL2ST.TOS` showed all
  240 rows on an STE. The border has not been seen on
  a real machine with this.

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
  - `test_audio`: the callback on a stand-in for STDL's ring that asks
    for what the ring asks. Exactly the obtained size every call,
    never before the unpause, and its bytes reaching the ring in order
    through pauses and locks. Then the spec's negotiation, the queue,
    SDL2's mixing arithmetic against a reference, and WAV files.
  - `test_bmp`: `SDL_LoadBMP` on BMPs written a pixel at a time, of 1,
    4 and 8 bits, every width from 1 to 35, either way up, with each
    header; what it refuses; every prefix of a file, cut short. Then
    the round trip: `bmp8conv.py` converts art with each of `stdlconv
    bmp8`'s options and checks what it promises, and each file must
    load as Pillow reads it, with the core's 4-bit example BMPs,
    which other tools wrote. Skipped without Pillow.
- **`SDL2AUD` (`tests/hatari/sdl2aud.c`):** the audio on the machine's
  DMA sound. For a 16-bit stereo spec at 22050, signed 8 bits at a DMA
  rate and 6258Hz, every callback the obtained size, and the rate the
  callback is drained at within 3% of the one asked for; then the
  queue.
- **`SDL2CHK` (`tests/hatari/sdl2chk.c`):** every variant of the
  conversion's assembler against its C on the machine, and their
  times.
- **`SDL2RCHK`:** `test_render.c` built for the machine against the
  real STDL. `addons/sdl2/tests/test_render.c` says how to build it.
- **`SDL2BMP` (`tests/hatari/sdl2bmp.c`):** art `stdlconv bmp8` made
  (`sdl2bmp.py`), loaded with `SDL_LoadBMP` from the drive with the
  core's 4-bit `SAIL.BMP`, each hashed as `sdl2bmp.py` hashes Pillow's
  reading of it, and shown. On a TT and a Falcon on VGA and RGB in
  Hatari the hashes matched, and the screen matched the picture to
  the pixel, its colours exact on a TT and within 3 of 255 on a
  Falcon.
