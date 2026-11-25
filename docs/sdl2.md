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
  first. `SDL_Quit` is `STDL_Quit`, which ends both. `STDL_Init`
  writes STDL's one-line banner to the console;
  `STDL_UseBanner(0)` before `SDL_Init` leaves it out.

`examples/sdl2demo.c` (`SDL2DEMO.TOS`) is a plain SDL2 program on the
add-on, and the pattern to copy.

## What it maps

The calls it has:

| Area | Calls |
|---|---|
| Start and stop | `SDL_Init`, `SDL_InitSubSystem`, `SDL_QuitSubSystem`, `SDL_WasInit`, `SDL_Quit`, `SDL_GetVersion` (2.0.10) |
| Errors, hints | `SDL_SetError`, `SDL_GetError`, `SDL_ClearError`, `SDL_SetHint`, `SDL_SetHintWithPriority`, `SDL_GetHint`, `SDL_GetHintBoolean`, `SDL_ClearHints` |
| Window | `SDL_CreateWindow` (at most 320x240, or 640x480 in the experimental build: below), `SDL_DestroyWindow`, `SDL_GetWindowSize`, `SDL_SetWindowTitle` (nothing to do), `SDL_GetDesktopDisplayMode` (the largest window there can be) |
| Renderer | `SDL_CreateRenderer`, `SDL_DestroyRenderer`, `SDL_RenderSetLogicalSize`, `SDL_RenderGetLogicalSize`, `SDL_SetRenderDrawColor`, `SDL_RenderClear`, `SDL_RenderCopy`, `SDL_RenderPresent` |
| Textures | `SDL_CreateTexture`, `SDL_CreateTextureFromSurface`, `SDL_DestroyTexture`, `SDL_UpdateTexture`, `SDL_LockTexture`, `SDL_UnlockTexture`, `SDL_QueryTexture` |
| Palettes | `SDL_AllocPalette`, `SDL_SetPaletteColors`, `SDL_FreePalette` |
| Events | `SDL_PollEvent`, `SDL_PumpEvents`, `SDL_WaitEvent`, `SDL_WaitEventTimeout`, `SDL_PushEvent`, `SDL_PeepEvents`, `SDL_FlushEvent`, `SDL_FlushEvents`, `SDL_EventState`, `SDL_GetEventState`, `SDL_RegisterEvents` |
| Keyboard | `SDL_GetKeyboardState`, `SDL_GetModState`, `SDL_StartTextInput`, `SDL_StopTextInput`, `SDL_IsTextInputActive`, `SDL_GetScancodeFromKey`, `SDL_GetKeyFromScancode` |
| Mouse | `SDL_GetMouseState`, `SDL_GetRelativeMouseState`, `SDL_SetRelativeMouseMode`, `SDL_GetRelativeMouseMode`, `SDL_WarpMouseInWindow`, `SDL_ShowCursor` (there is no cursor) |
| Joystick | `SDL_NumJoysticks`, `SDL_JoystickNameForIndex`, `SDL_JoystickOpen`, `SDL_JoystickClose`, `SDL_JoystickName`, `SDL_JoystickInstanceID`, `SDL_JoystickNumAxes`, `SDL_JoystickNumButtons`, `SDL_JoystickNumHats`, `SDL_JoystickGetAxis`, `SDL_JoystickGetButton`, `SDL_JoystickGetHat`, `SDL_JoystickRumble`, `SDL_JoystickEventState`, `SDL_JoystickGetDeviceGUID`, `SDL_JoystickGetGUID`, `SDL_JoystickGetGUIDString`, `SDL_JoystickGetGUIDFromString` |
| Game controller | `SDL_IsGameController`, `SDL_GameControllerOpen`, `SDL_GameControllerClose`, `SDL_GameControllerName`, `SDL_GameControllerNameForIndex`, `SDL_GameControllerGetAttached`, `SDL_GameControllerGetJoystick`, `SDL_GameControllerGetButton`, `SDL_GameControllerGetAxis`, `SDL_GameControllerRumble`, `SDL_GameControllerEventState`, `SDL_GameControllerTypeForIndex`, `SDL_GameControllerGetType` |
| Surfaces | `SDL_CreateRGBSurface`, `SDL_CreateRGBSurfaceWithFormat`, `SDL_CreateRGBSurfaceFrom`, `SDL_FreeSurface`, `SDL_SetSurfacePalette`, `SDL_LockSurface`, `SDL_UnlockSurface`, `SDL_SetColorKey`, `SDL_GetColorKey`, `SDL_SetClipRect`, `SDL_GetClipRect`, `SDL_FillRect`, `SDL_BlitSurface` (`SDL_UpperBlit`), `SDL_LoadBMP`, `SDL_LoadBMP_RW` (uncompressed, 1, 4 or 8 bits: below) |
| Streams | `SDL_RWFromFile`, `SDL_RWFromFP`, `SDL_RWFromMem`, `SDL_RWFromConstMem`, `SDL_AllocRW`, `SDL_FreeRW`, `SDL_RWsize`, `SDL_RWseek`, `SDL_RWtell`, `SDL_RWread`, `SDL_RWwrite`, `SDL_RWclose`, `SDL_ReadU8`, `SDL_ReadLE16`/`BE16`/`LE32`/`BE32`, `SDL_WriteU8`, `SDL_WriteLE16`/`BE16`/`LE32`/`BE32` |
| Log | `SDL_Log`, `SDL_LogVerbose`, `SDL_LogDebug`, `SDL_LogInfo`, `SDL_LogWarn`, `SDL_LogError`, `SDL_LogCritical`, `SDL_LogMessage`, `SDL_LogMessageV`, `SDL_LogSetPriority`, `SDL_LogSetAllPriority` |
| Audio | `SDL_OpenAudio`, `SDL_OpenAudioDevice`, `SDL_PauseAudio`, `SDL_PauseAudioDevice`, `SDL_GetAudioStatus`, `SDL_GetAudioDeviceStatus`, `SDL_LockAudio`, `SDL_LockAudioDevice`, `SDL_UnlockAudio`, `SDL_UnlockAudioDevice`, `SDL_CloseAudio`, `SDL_CloseAudioDevice`, `SDL_QueueAudio`, `SDL_GetQueuedAudioSize`, `SDL_ClearQueuedAudio`, `SDL_MixAudio`, `SDL_MixAudioFormat`, `SDL_LoadWAV`, `SDL_LoadWAV_RW`, `SDL_FreeWAV`, `SDL_GetNumAudioDevices`, `SDL_GetAudioDeviceName`, `SDL_GetNumAudioDrivers`, `SDL_GetAudioDriver`, `SDL_GetCurrentAudioDriver` |
| Byte order, paths | `SDL_BYTEORDER`, `SDL_Swap16`/`32`/`64`, `SDL_SwapLE16`/`32`/`64`, `SDL_SwapBE16`/`32`/`64` (`SDL_endian.h`), `SDL_GetBasePath`, `SDL_GetPrefPath` (`SDL_filesystem.h`: none, below) |
| Clock, platform | `SDL_GetTicks`, `SDL_GetTicks64`, `SDL_Delay`, `SDL_GetPerformanceCounter`, `SDL_GetPerformanceFrequency` (38,400), `SDL_GetPlatform` ("Atari") |

Every name is a macro onto a `stdl_sdl2_` link name, so the archive
defines no `SDL_` symbol. That keeps it clear of the 1.2 shim's
`SDL_Delay`, `SDL_FillRect` and the rest, inside `libstdl.a`. A call
not in the table is a compile error by design.

## Where it differs from SDL2

All of these come from the machine:

- **One screen, 320x240 in 256 colours.** It is TT Low (a row on two
  lines) on a TT. On a Falcon it is an 8-bit mode on VGA, or on RGB
  and a TV, where the 200-line mode is made 240 lines tall by opening
  the VIDEL's window, as TOS's overscan modes do. A window is at most
  320x240 and is centred.
  The experimental 68000 build gives a first window wider than 320
  640x480 in 16 colours on a TT or a Falcon ("640x480 in 16 colours",
  below); `SDL_GetDesktopDisplayMode` says which a program can have.
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
  its top left. There is no wheel or cursor (`SDL_MOUSEWHEEL` and
  its event are defined, so a program's handler compiles, and never
  come).
  `SDL_GetRelativeMouseState` gives the motion events' `xrel` and
  `yrel` summed since it was last called (the IKBD's own deltas, so
  they keep coming at the screen's edge), counted whether or not the
  program takes the events or ignores them, as SDL2 counts them.
  `SDL_SetRelativeMouseMode` is accepted and changes nothing: there is
  no cursor to hide or desktop to keep the mouse in.
  `SDL_WarpMouseInWindow` moves STDL's pointer, which sends a motion
  event, as SDL2's does.
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
  A joystick's GUID, which programs keep in their settings to find
  the same device again, is `"STDL"` and its index, zeros after:
  `5354444c0000...` is joystick 0. STDL numbers joysticks by where they
  come from, so a pad found behind one later leaves its GUID as it
  was. The string calls are inline in `SDL_joystick.h`.
  `SDL_GameControllerTypeForIndex` and `SDL_GameControllerGetType` say
  `SDL_CONTROLLER_TYPE_UNKNOWN`: STDL does not know which pad it is.
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
  not RLE or 16 to 32 bits (the next section). `SDL_FillRect` fills a
  320x200 surface in 4.1ms on a TT and 8.1 on a Falcon, as fast as
  their memory takes it, and a 16x16 box in 53us and 105 (Hatari,
  ST-RAM). A smaller box costs more in the call than in its pixels: an
  8x8 one takes 29us on a TT, where a program's own loop of bytes
  takes 21.
- **Byte order:** the swaps are inline (a rotate, or rotate, swap,
  rotate), no call and no link symbol; on the 68000 family the
  little-endian ones are the ones that swap.
- **No base or preferences path.** `SDL_GetBasePath` and
  `SDL_GetPrefPath` return NULL with the error set, as SDL2 does where
  it cannot find one; a program then uses the current folder, which
  TOS makes the program's own when it is started from the desktop.
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

### Copies compared

A program that tracks what it drew on, not what changed, copies a
good deal the screen already has: a sprite put back and drawn again
where it stood is the same pixels as the frame before. With

```c
SDL_SetHint(STDL_SDL2_HINT_COMPARE, "1");
```

before the renderer is created, the renderer keeps a chunky copy of
the screen (76.8K), compares each copy of whole groups with it and
puts only the groups that differ into the batch. A group the screen
already has is then a compare, some 70 cycles on a 68030, in place of
a conversion, and no page owes it.

- **What is compared:** copies whose x and width on the screen are
  multiples of 16. A copy that covers part of a group goes the usual
  way, as does a pairs copy and any copy into a batch that has one.
- **The pixels are read at the copy** as well as when the batch is
  converted, between which a program already leaves them alone.
- **A present after copies that changed nothing still flips.**
- **It is a hint.** Without the memory for the chunky copy the
  renderer goes on without it. The screen check keeps the same copy,
  so the two together cost what the check costs.
- **`STDL_SDL2_SetCompare(renderer, on)` turns it on or off** at any
  time, between copies too, for a program whose copies are mostly the
  screen's at some times and mostly new at others, or some of each in
  one frame ("Off for one copy of a frame", below). Off, the renderer
  stops keeping the chunky copy (unless the check keeps it): a copy
  costs what it does without one and an AND a row, which takes the
  groups it touches, in part too, out of those the chunky copy has,
  and a clear takes them all. The memory stays. On again, every other
  group is compared as if it had never been off. A copy that reaches a
  group taken out converts that row of the copy whole, and the chunky
  copy has it from then on, so the first frame after a long time off
  converts what it copies as it would without the compare. A clear
  with it on makes the chunky copy whole again. Turned on for a
  renderer made without the hint, it allocates the chunky copy then
  (76.8K), with none of the screen in it, and returns -1 with the
  error set where there is no memory.

It pays where most of what is copied has not changed. One port's game
pushes every 8x8 block it drew on, and in play three groups in four of
those were on screen already. Over ten recorded sessions in Hatari a
frame's work fell 14% on both machines, from 11.60ms to 9.90 on a TT
and from 22.84 to 19.57 on a Falcon, the add-on's share of it from
4.38ms to 2.72 and from 8.70 to 5.49. The one session where over half
the groups pushed had changed gained nothing (a Falcon's 27.38ms to
27.54). Its cutscenes, where most of what a frame pushes is new, did
3-4% less work over eight scenes, from 13% less in one to 6% more in
another. A program whose every copy is new pixels pays a read of each
for nothing.

One port's game shows both kinds: its view, all new pixels every
frame, fills the screen above the status bar or sits in a border that
never changes, as the player sizes it. With every frame copied whole,
the compare cost it 2-4% with the view at full size and saved 4-13%
with it smaller (Hatari, a TT), so it turns the compare on whenever
the view is smaller.

#### Off for one copy of a frame

A game's frame is often both kinds at once: a playfield or a 3D view
that is new in nearly every pixel, and status displays round it that
change now and then. Turn the compare off for the view's copy alone:

```c
STDL_SDL2_SetCompare(renderer, 0);
SDL_RenderCopy(renderer, texture, &view, &view);     /* all new */
STDL_SDL2_SetCompare(renderer, 1);
SDL_RenderCopy(renderer, texture, &beside, &beside); /* seldom new */
SDL_RenderCopy(renderer, texture, &below, &below);
```

The view is then neither read nor kept, and the displays are compared
as before, since only the groups the view's copy touches leave the
chunky copy. Two rules make it pay:

- **Keep the two kinds in different groups.** Round the view's box out
  to whole 16-pixel groups and give the displays the groups that are
  left. A compared copy that shares a group with the view's on some
  row converts that row of itself whole, every frame.
- **The program needs no copy of the screen of its own.** One that
  cannot say what it changed copies each display's whole box every
  frame and leaves finding the changes to the compare.

One port's game has a playfield 17 groups wide and 184 rows high, new
every frame, with status displays beside and below it, and copies its
8-bit frame from its own buffer. A frame of a level on a TT at 16MHz
(Hatari, 300 frames, the same pictures in each):

| | a frame | of it showing the screen |
|---|---|---|
| every copy compared (the hint alone) | 122.1ms | 70.4ms |
| no compare | 108.2ms | 56.6ms |
| the compare off for the playfield's copy | 104.5ms | 53.1ms |

Its first version compared the displays itself, against a copy of the
screen of its own, and sent the renderer only what had changed, with
no compare in the renderer: 104.9ms. And in v1.32.0, where turning the
compare off dropped the whole chunky copy, the calls above took
115.0ms, more than no compare at all, as every display was converted
every frame.

In the experimental 68000 build an ST's screen always compares, and
`STDL_SDL2_SetCompare` changes nothing there, so a program written
this way runs on it unchanged.

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

### Pixel pairs

A program that draws at half the width - a game's low detail, each
pixel twice - can say so: `STDL_SDL2_RenderCopyPairs` is
`SDL_RenderCopy` with the program's word that, in the groups the copy
covers whole, the pixels at screen x and x + 1 (x even) are the same.
Those groups are converted from their 8 distinct pixels, 56
instructions a group against 74 (the loops 178-204 bytes in the
cache):

| A whole 320x240 frame, ms | whole | as pairs, X = 0 | as pairs, X at run time |
|---|---|---|---|
| TT (rows on two lines) | 31.03 | 25.60 | 27.82 |
| Falcon on VGA | 59.80 | 48.80 | 53.02 |

A group is converted as pairs only if every copy of it before the
present (or before a copy from another texture) was a pairs copy that
covered it whole. Anything else drawn over the picture - a game's
messages, its menu - is copied with `SDL_RenderCopy`, and the groups
that copy touches are converted whole, whichever came first. That is
the whole of the bookkeeping: copy the view as pairs and everything
else as before.

The pixels are read when converted, so they must be in pairs then.
Ones that are not show one of each pair (the first in a group's first
half, the second in its second); the screen check
(`STDL_SDL2_HINT_CHECK`) finds every such frame. Only a program that
calls it links the pair loops and their bookkeeping (1,584 bytes: the
loops' own object, which a program converting by itself calls
directly, below, 1,404); every program pays 324 bytes for the batch's
mask of them. A fixed border index takes the run-time XOR's pair
loops. In the experimental 68000 build an ST's screen has pair loops
of its own, about half the time of its whole conversion ("An ST's
screen, for testing", below).

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

The call is inline. With a constant X it links that X's loops alone,
684-696 bytes beside X = 0's, which cost what X = 0's do. With an X
known only at run time it links the loops that read it (`c2pany.o`,
704 bytes), about 4.5% slower; until November 2026 such a call linked
all fifteen fixed X's loops as well, about 10K.

## Converting a program's own screen (`SDL_stdl_c2p.h`)

A program that draws its own byte-per-pixel screen and puts it on a
TT's or a Falcon's 256-colour screen itself, with no `SDL_Renderer` and
perhaps no SDL at all, can call the conversion alone:

```c
#include <SDL2/SDL_stdl_c2p.h>      /* -I$STDL/addons/sdl2/include */

STDL_SDL2_ConvertBox(src, src_pitch, dst, dst_pitch, groups, rows,
                     two_lines, x);
STDL_SDL2_ConvertBoxPairs(src, src_pitch, dst, dst_pitch, groups, rows,
                          two_lines, x);
```

- **The box:** `groups` 16-pixel groups by `rows` rows, from `src` (its
  first row's first pixel, `src_pitch` bytes a row, any address on a
  68030) to `dst` (its first group on its first line, `dst_pitch` bytes
  a line), eight planes a word each, 16 bytes a group. With `two_lines`
  each row goes to two lines `dst_pitch` apart, a TT's doubled rows in
  TT Low, and `dst` moves on `2 * dst_pitch` a row. Nothing happens for
  a box of no groups or no rows; `groups` is 32,768 at most.
- **`x` is the border index** (above): 0, or a multiple of 16 up to
  0xF0. Both calls are inline, so a constant X links its own loops and
  an X from a variable the loops that read it.
- **Pairs** are the pair loops of "Pixel pairs", above, for pixels the
  program drew twice across: every pixel 2i the same as 2i + 1. X = 0
  has loops of its own and any other X takes the run-time ones.
- **What it links:** the conversion's objects and nothing else, no
  renderer and no `libstdl.a` (a program using STDL's screen links that
  for the screen): X = 0's loops and the C twin, 1,452 bytes; a fixed
  X's loops 684-696 more, or the run-time X's 704; the pair loops, both
  X's, 1,404. A program converting whole pixels with X = 0 and pairs
  links 2,856 bytes of the add-on. `make sdl2` links a program calling
  both (`addons/sdl2/tests/c2plink.c`) against each archive with
  nothing else of STDL, where the renderer would fail for want of it.
- **The header declares nothing of SDL**, so a program with no SDL.h,
  or another SDL's, includes it from `-I$STDL/addons/sdl2/include`, which
  puts no `SDL.h` on the include path. `SDL_stdl.h` includes it too.
- **A 68000 program converting only on a TT or a Falcon** (one binary
  for every machine, an ST taking another way to its screen) compiles
  with `-m68000`, links `libstdl-sdl2.a` (or `libstdl-sdl2-cmini.a`
  with libcmini), the released archives, and calls the conversion only
  once it knows it has 8 planes. Their loops have no instruction a
  68000 lacks; the C round them is built for the 68020 and runs only
  where the program calls it. The experimental `libstdl-sdl2-st.a` has
  the same conversion built for the 68000 and gives the same pages
  (SDL2CHK built `-m68000` against each, on Hatari's TT), but is not
  in a release.
- **On STDL's responsive screen** (`stdl_responsive.h`): `dst` is the
  screen's `pixels` plus the box's row times its `pitch` and its group
  times 16; on a TT `pitch` is two lines, so `two_lines` is 1 and
  `dst_pitch` is `pitch / 2`, and Flip has nothing to copy; on a Falcon
  `two_lines` is 0 and `dst_pitch` is `pitch`. `examples/sdl2c2p.c`
  (`SDL2C2P.TOS`) is the pattern: a 68000 program with its picture
  drawn at half the width, P switching between the two calls, the time
  on a bar.

**Pairs pay in whole boxes.** A group converted as pairs saves the
same however the box is cut, about 1.1us on a TT and 2.3 on a Falcon
(56 instructions against 74), but each call costs some 40us on a TT and
80 on a Falcon, its registers saved and its loop set up and drained.
Times through these calls in Hatari (SDL2CHK, `--cpu-exact`), X = 0,
ms a 320x240 frame, whole pixels against pairs:

| | TT (rows on two lines) | Falcon on VGA |
|---|---|---|
| one call | 31.00 against 25.59 | 59.46 against 48.61 |
| a call a row | 41.48 against 35.35 | 79.38 against 66.89 |
| a call a group | 215.11 against 196.39 | 412.45 against 358.05 |
| a call a row, the rows taking turns between the calls (pairs and whole), against every row whole | 39.48 against 41.48 | 75.26 against 79.38 |

So the box comes first: the view as one call rather than a call a row
saves more than pairs do. Called back to back like this the pairs still
save something in every row, less where the calls take turns (each
call's loop pushing the other's out of the 68030's instruction cache).
Inside a program's frame they can save nothing. One port's 3D view,
320 pixels wide, on a TT with 2MB of ST-RAM and 4MB of TT-RAM in
Hatari: its conversion took 20.5ms a frame, and 16.7 with the whole
view's box as one pairs call; called a row at a time, rows of pairs
and whole rows as its drawing said, the pairs measured no faster than
whole pixels, and runs of single groups sent as boxes of their own
saved nothing. Convert the view as one box of pairs, and what is drawn
over it at full width (a weapon, messages) as boxes of whole pixels
after it.

## Extensions (`SDL_stdl.h`)

A program includes `SDL_stdl.h` itself:

| Call or hint | What it is for |
|---|---|
| `STDL_SDL2_HINT_PAGES` | `"2"` or `"3"` (the default) pages. Three let a frame be drawn while the display still waits for the last; a third costs 153.6K of ST-RAM on a TT and 76.8K on a Falcon, and without the room STDL gives two |
| `STDL_SDL2_HINT_LOGFILE` | the file `SDL_Log` and the rest write to, a line a message, flushed after each. Without it they write nothing. SDL2's priorities: the application's from INFO, assertions' from WARN, tests' from VERBOSE, the rest CRITICAL only |
| `STDL_SDL2_HINT_COMPARE` | `"1"`: copies of whole groups are compared with a chunky copy of the screen (76.8K) and only the groups that differ are converted, for a program that copies what it drew on, changed or not: "Copies compared", above |
| `STDL_SDL2_SetCompare` | that compare on or off from here on, at any time, for one copy of a frame too: "Copies compared", above |
| `STDL_SDL2_HINT_CHECK` | `"1"`: the screen check. The renderer keeps a chunky model of the screen, drawn as each copy and clear is made, and compares it, converted, with every page before it flips. It costs 230K and a page's conversion a flip, and is for testing; `STDL_SDL2_GetCheck` reads it |
| `STDL_SDL2_SetRenderPalette` | the renderer's palette, staged at each present when its version has moved on |
| `STDL_SDL2_SetRenderDrawIndex` | the clear colour as an index |
| `STDL_SDL2_CreateTextureFrom` | a texture over the program's own pixels, no copy |
| `STDL_SDL2_SetBorderIndex` | the border index, above |
| `STDL_SDL2_RenderCopyPairs` | `SDL_RenderCopy` for pixels in pairs, converted at 56 instructions a group against 74: "Pixel pairs", above |
| `STDL_SDL2_ConvertBox`, `STDL_SDL2_ConvertBoxPairs` | the conversion alone, whole pixels or pairs, for a program that converts its own screen with no renderer (`SDL_stdl_c2p.h`, which `SDL_stdl.h` includes): "Converting a program's own screen", above |
| `STDL_SDL2_ClearScreenNow` | the page on show cleared at once, margins and all, with no flip, the others when next drawn on. For a program that clears its screen without presenting a frame |
| `STDL_SDL2_StagePalette` | the colours staged now, with no present: a fade's steps |
| `STDL_SDL2_GetRegisterColours` | the 256 colours as last staged, in register order |
| `STDL_SDL2_GetShownPage` | the page last shown and its size, for a program that hashes what it showed |

`SDL_stdl_bench.h` is not a supported call, and may change: it gives
the present's conversion and flip times for a bench.

## An experimental build for every machine

`make sdl2st` builds the add-on a second time, for the 68000:
`libstdl-sdl2-st.a` and `libstdl-sdl2-st-cmini.a`, and the demo as
`dist/SDL2ST.TOS`, and again as `dist/SDL2ST16.TOS`, which gives its
own 16 colours on an ST (below). A program linked with it runs on every machine. On
a TT or a Falcon it has the 256-colour screen and shows what the
add-on built for them shows, frame for frame; on an ST, an STE or a
Mega STE it has a 16-colour screen that is for testing (below). It is
experimental: `make sdl2` does not build it, the archives a TT or a
Falcon links have none of it, it is not in a release and has no size
budget, no real machine has run it, and what it does may change. The
experimental SDL1 add-on ([sdl1.md](sdl1.md)) draws through it: SDL
1.2's 8-bit picture for a program not yet ported to planes.

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
16-colour one and a whole screen of new pixels takes an 8MHz ST 0.74s.

- **The screen follows the first window's height**, as the mode is
  set once a program. A window of up to 200 rows gets ST Low, all of
  it there. A taller one gets STDL's tall screen
  (`STDL_RESPONSIVE_TALL`, `docs/responsive.md`): 240 rows with an ST's
  bottom border open, so a 320x240 window is all there too. That
  screen has two pages, the border's own, whatever
  `STDL_SDL2_HINT_PAGES` says, so every present waits for the blank
  (10ms a frame of one port's game on a Mega STE), and the border's
  rules apply (`docs/overscan.md`: on a real machine disk access and a
  moving mouse make it flicker). If the border cannot open the screen
  is ST Low and shows the middle 200 rows of the add-on's 240. The
  mouse is reported to match either way.
  `SDL_SetHint(STDL_SDL2_HINT_ST_TALL, "0")` before the window keeps
  ST Low whatever the height: the middle 200 rows, three pages and no
  wait.
- **Only what changed is converted.** A copy of whole groups (x and
  width multiples of 16, pixels at an even address, an even pitch) is
  compared with the chunky copy of the screen, a group at a time, and
  only the groups that differ are converted. A program that pushes
  the blocks it drew on, changed or not, pays a compare for most of
  them: one port's game converted 6ms a frame on a Mega STE where it
  had converted 20. Other copies are converted whole.
- **Pixel pairs** (`STDL_SDL2_RenderCopyPairs`) have a conversion of
  their own: a group's eight pairs are eight lookups where any other
  group's pixels are sixteen, 382 cycles a group against 734 by the
  instructions' counts (24 a pixel against 46). The rule is the TT's
  and the Falcon's: a group goes through it only if every copy of it
  before the present was a pairs copy covering it whole. Here pairs
  copies are compared too, as every copy of whole groups is, and an
  ordinary copy that changes nothing still takes back the groups it
  covers.
  - **Pixels not in pairs** show the second of each pair, the pixel at
    x + 1 at an even x too. A pair is read as one word, so pairs from
    an odd address or with an odd pitch are converted whole, as any
    copy.
  - **Its tables** are 8K, the two rows' sets of four, made from the
    conversion's own tables, so a group in pairs comes out as the whole
    conversion gives it by construction. They are allocated at the
    first pairs copy (a program that never asks has none, and links
    none of the code, `stpair.o`'s 328 bytes; the renderer is 392 bytes
    larger in this build for it) and made again whenever the maps move:
    15ms on an 8MHz ST and 9 on a Mega STE, beside the 28 and 15 the
    conversion's own take and the whole screen converted again that a
    move costs. Without the memory a pairs copy is converted as any.
  - **What it buys**, in Hatari (`tests/hatari/sdl2stp.c`, ST Low, every
    group new every frame, a frame's copies and present less its
    flip's wait):

    | ms | ST, 8MHz | Mega STE |
    |---|---|---|
    | The loops alone, a 320x168 view | 343 -> 179 | 181 -> 104 |
    | A frame, all 320x200 in pairs | 767 -> 574 | 409 -> 320 |
    | A frame, a 320x168 view in pairs over a 32-row bar of whole pixels | 767 -> 606 | 410 -> 335 |
    | A frame, all in pairs, the program's own 16 colours | 517 -> 325 | 292 -> 203 |

    The last row is the rest of a frame of new pixels: noting the
    colours each changed group brings (`st_seen16`, 16 lookups a
    group, pairs or not) is about 250ms of an ST's 767 and 117 of a
    Mega STE's 409, and the program's own 16 note none. Noting only
    the second pixel of each pair would halve that for pairs, at the
    price of the test's model knowing which groups were converted as
    pairs; not done.
- **The 16 colours are made from the colours on the screen**, not
  from the palette as a whole: 15 slots and black. A program's
  palette has many colours its picture does not show, and every one
  took a share of the 16. The colours seen start in one box, and the
  box widest in any channel is cut at the middle of that channel until
  there are 15; a slot shows the mean of its box, as the machine shows
  it (8 levels a channel on an ST, 16 on an STE). Up to 15 colours on
  screen beside black are so shown exactly, and a colour not on screen
  goes to the nearest slot until it is. Register 0 is black, the
  border's on an ST, and takes the palette's blacks and the border
  index, which needs no XOR here.
  - **Which colours are on screen**: counted from the chunky copy of
    the screen when a palette change has left the maps not fitting (a
    room's change, a new picture), then each colour a copy or a clear
    brings noted as it comes: a group a copy changes is checked, 16
    lookups. A colour that goes stays noted until the next count, so a
    program that never changes its palette keeps every colour it has
    shown, and its 16 tend toward those of the palette as a whole.
  - **A checkerboard**, on by default: a colour between two of the 16
    is shown as both, pixel about, where the mix is nearer than either
    alone by enough to be worth the pattern. On a sharp display the
    pattern shows; `SDL_SetHint(STDL_SDL2_HINT_ST_DITHER, "0")` before
    the renderer turns it off.
  - **What it bought**, scored on the host over 15 frames of one
    port's game against the 256 colours a TT showed (the error a pixel
    after a 2x2 average): the palette as a whole 84 on an ST and 83 on
    an STE, the colours on screen 47 and 26, with the checkerboard 38
    and 23 (`st.c` has what else was tried and scored worse).
- **A program's own 16** (`STDL_SDL2_SetSTColours(renderer, map,
  colours)`): a program that already chooses an ST's colours, a port
  with an ST version of its own, gives the map from its 256 to the 16
  slots and the slots' colours, and the add-on shows them as given from
  the next present or staging, until a NULL map hands the choice back.
  Nothing is counted or noted meanwhile. `SDL2ST16.TOS`
  (`examples/sdl2demo.c` built with `SDL2DEMO_ST16`) is the pattern:
  its map made once, eight bands of its cycling background's shades,
  seven of its boxes' and black, each slot's colour its band's middle
  entry given again as the palette cycles; S switches between those
  and the add-on's own.
  - A new map is a screen converted again, and with the checkerboard
    its second slots are made then, from the colours given and the
    palette as it is. Under the same map new colours, as at a fade's
    steps, and palette changes are the 16 registers, as on an ST
    without the add-on, so the program gives the 16 again when they
    change. Second slots made again at each change of the palette or
    the 16, as first written, would move at a fade's last step, where
    every colour is black and each second slot falls back to its
    first, and a move is the whole screen converted again; that
    version was replaced before it was run.
  - The colours on screen change with what the screen shows, so the
    same room could come out in other colours from one visit to the
    next. One port's game, fed from its ST version's palette, showed
    that version's colours on every screen, with nothing counted or
    noted: 2-4% less a frame in play on an STE and a Mega STE, and 8%
    less work in its cutscenes on a Mega STE.
- **A palette change can cost a screen.** Each page holds slots, not
  colours, so when the maps move the next page drawn on is converted
  again whole, from a chunky copy of the screen the renderer keeps
  (76.8K), and the other pages catch up from it. The maps in use stay
  while they show the colours on screen nearly as well as they did
  when made (their error within a quarter of what it was), measured
  against the slots' own means rather than the machine's levels, so a
  fade to black and back keeps them at every step. Colours that come
  on screen later can move them too: one port's game made them again
  4 times in a replay of 638 frames, once for its room and three times
  as colours came.
- **Where it differs from the TT's and Falcon's:**
  - a present flips for a new map though nothing was drawn, and
    `STDL_SDL2_StagePalette` presents when the maps move, the add-on's
    or the program's, what was copied so far with it;
  - `STDL_SDL2_GetRegisterColours` gives the 16 registers in its first
    16 entries, and `STDL_SDL2_GetShownPage` the page's rows, 160
    bytes each (32,000 bytes, or the tall screen's 38,400);
  - `STDL_SDL2_HINT_CHECK` works, and compares the page's rows;
  - sound needs an STE's DMA sound, as STDL's ring does: on a plain ST
    `SDL_OpenAudio` fails;
  - a texture has at most 32,767 rows and bytes a row, as rows are
    found with the 68000's 16-bit multiply;
  - the add-on's size and libgcc checks (`sdl2check`) are not run on
    it: on a 68000 a 32-bit multiply is a library call.
- **What it costs** on ST Low (Hatari; an ST at 8MHz, a Mega STE at
  16MHz with its cache; the tall screen's whole screen is a fifth more
  rows, not timed):

  | | ST | Mega STE |
  |---|---|---|
  | A whole 320x200 screen, every pixel new | 743ms | 415ms |
  | The same screen pushed again as it was | 162ms | 105ms |
  | A 64x48 box, new | 41ms | 23ms |
  | A palette change that keeps the maps | 42ms | 22ms |
  | A fade's step (256 colours, to black in 16) | 135ms | 73ms |
  | A new palette: a count, new maps, the screen again | 1,000ms | 566ms |
  | A clear and its present | 68ms | 50ms |

  A present of little is held to the display's rate on three pages
  (the box as it was: 19ms, a blank). The conversion is assembler, a
  table lookup a pixel: 644ms a screen on an ST in gcc's C, 516 in
  assembler with a table for each of four pixel places. Checking each
  changed group for colours not seen is what makes new pixels dearer
  (529ms a screen and 30ms a box before it): in one port's play, 3ms of
  a Mega STE's 58ms frame. Checking only a group's longs that changed
  was slower. A present went through all 240 rows of its bookkeeping
  three times, 7ms on an ST whatever was drawn, until it kept the rows
  a frame touched. The fade's step was 759ms before the map was kept.
- **Tested** by `test_render_st` on the host (`make ci` runs it):
  `test_render.c` built with `STDL_SDL2_ST`, the page shown against
  the model's middle rows put through the renderer's two maps a pixel
  at a time, on the tall screen's 240 rows, on 200 where the border
  did not open and on a border cut short, with two pages and three and
  border indices 0 and 0x70, and the reduction against what it
  promises, and ST Low by the hint; the pair loop against a conversion
  a pixel at a time and against the whole loop (rows in pairs byte for
  byte, rows not as their second pixels), and pairs copies on the
  screen, compared, with ordinary copies over them, from an odd
  address and before any map has moved. Then it runs `test_render`'s
  TT and Falcon screens through this build, which must show them as
  the other does. Eleven breakages of the ST path each failed it, ten
  more of it as it stood at v1.24.0, fifteen of the changed-group
  compare, the rows kept and the conversion's tables (a sixteenth, a
  page's owed rows never cleared, only copies more), and seventeen of
  the colours seen and the reduction: the test notes what its own
  model shows, counts it where the renderer should, and expects the
  renderer's colours seen, maps and registers from that; and fourteen
  of the pixel pairs (the tables, the loop's C, the marking of
  compared copies, the address test). `SDL2RCST` is the same on the
  machine (`test_render.c` says how to build it), and passed against
  the real STDL on Hatari's ST, tall, and on its TT and its Falcon on
  VGA; with the pairs, on an ST, an STE, a Mega STE, a TT and a Falcon
  on VGA (the pair loop's assembler with two of its tables swapped
  failed it on the ST); `SDL2ST.TOS` showed all 240 rows on an STE.
  The border has not been seen on a real machine with this.

### 640x480 in 16 colours on a TT or a Falcon

In this build a first window wider than 320 gets STDL's wide screen
(`STDL_RESPONSIVE_WIDE`, [responsive.md](responsive.md)): TT Medium on a
TT, a Falcon's own 640x480 in 16 colours on VGA, and on RGB or a TV
640x400 interlaced opened to 480 lines. It is drawn as an ST's screen
is, through the same 16-colour path: the colours made from those on
screen or given (`STDL_SDL2_SetSTColours`), the checkerboard and its
hint, every copy of whole groups compared, pixel pairs. A program asks
what it can have before it makes the window:

```c
SDL_DisplayMode m;
SDL_GetDesktopDisplayMode(0, &m);   /* 640x480 here on a TT or a Falcon */
win = SDL_CreateWindow("", SDL_WINDOWPOS_CENTERED,
                       SDL_WINDOWPOS_CENTERED, m.w, m.h, 0);
```

- **The first window decides**, as on an ST: one wider than 320 sets up
  the wide screen, and a later one must fit it; one of 320 sets up the
  256-colour screen, on which a wider one is refused. A smaller
  logical size is centred, x on a group.
- **An ST refuses it** ("A window is at most 320x240"), and no mode is
  set: `SDL_GetDesktopDisplayMode` says 320x240 there, and in the 68020
  build everywhere.
- **A Falcon's RGB that cannot open to 480 lines** gives 640x400, which
  shows the screen's middle 400 rows, as ST Low shows 200 of 240.
- **Memory**: three of the display's pages are 461K of ST RAM, and the
  renderer keeps a chunky copy of the screen as an ST's does (300K
  here) and 69K of rows, so allow 850K.
- **A row is 40 groups**, so this build's masks of groups are 64 bits
  (`stdl_sdl2_mask`, internal.h), never shifted by a variable (a libgcc
  call there: spans and bits come from a table), and its rows the
  renderer's own allocation. On an ST's own screen that is 25K of rows
  where 32-bit masks took 17.3K. The 68020 build's are the 32-bit
  masks they were: its objects are byte for byte as before but for
  gcc's numbering of static locals in `timer.o` and `rwops.o`, and
  `display.o`, 212 bytes, which only `SDL_GetDesktopDisplayMode` links.
- **What it costs** (Hatari; this build's 68000 code on a 68030):
  - `SDL2WIDE.TOS` (`examples/sdl2demo.c` built with `SDL2DEMO_WIDE`:
    nine boxes put back and drawn again over a 640x480 background each
    frame) presents in 12ms a frame on a TT and 26ms on a Falcon on
    VGA, against 9ms at 320x240 in 256 colours on a TT. The rest of its
    frame is the demo's own, about as long at either size.
  - With its 224 background colours cycled every frame (`SDL2WIDE C`)
    the present takes 137ms on a TT: a cycle makes new maps, and new
    maps convert the whole 640x480 page again. Cycle colours on this
    screen only where the maps keep (a fade's steps do), or give the
    program's own 16.
  - SDL 1.2's testsprite, 16 sprites, through the SDL1 add-on: 13.9
    frames a second on a TT and 6.9 on a Falcon on VGA at 640x480,
    against 34.3 and 17.4 at 320x200 in 256 colours ([sdl1.md](sdl1.md)).
  - A real TT takes 2.3 times as long as Hatari's (above), so take these
    as an order of magnitude.
- **Tested**: `test_render_st` runs `test_render`'s model on the wide
  screen of a TT and a Falcon, at 480 rows and 400, with two pages and
  three and border indices 0 and 0x70, and `SDL_GetDesktopDisplayMode`
  everywhere; a compare that dropped groups 32-39, a catch-up and a
  partial group each truncated to 32 bits all failed it. `SDL2RCWD`
  (`test_render.c` built with `STDL_SDL2_TEST_WIDE`, the only test of
  the compare's assembler in its passes of 32 groups) passed on
  Hatari's TT and Falcon on VGA and RGB. `SDL2WIDE` showed all 640x480
  on the TT and the Falcon on VGA.

## Testing it

- **Host tests (`addons/sdl2/tests`, run by `make ci`):**
  - `test_basics`: errors, hints and `SDL_Init` on each machine.
  - `test_c2p`: the conversion's C against a pixel-by-pixel reference,
    and the public entries (`SDL_stdl_c2p.h`), whole pixels and pairs,
    X a constant and a variable, against the same.
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
    surface and onto themselves; fills against a byte-at-a-time one,
    every box at each of a long's four addresses, with nothing outside
    the box changed; a texture made from a surface against
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
  times; then the public entries the same way, and their times in one
  call, a call a row and a call a group. Built `-m68000` it is a 68000
  program converting on a TT or a Falcon, against `libstdl-sdl2.a` or
  the experimental `libstdl-sdl2-st.a`.
- **`SDL2RCHK`:** `test_render.c` built for the machine against the
  real STDL. `addons/sdl2/tests/test_render.c` says how to build it,
  and `SDL2RCHC`, the same with copies compared
  (`STDL_SDL2_HINT_COMPARE`), whose assembler only the machine runs,
  and `SDL2RCHT` with the compare turned on and off as it goes
  (`STDL_SDL2_SetCompare`) and the add-on's own check off, so that the
  chunky copy goes unkept while the compare is off.
- **`SDL2STP` (`tests/hatari/sdl2stp.c`):** pixel pairs on an ST's
  screen in the experimental 68000 build, timed: the pair loop against
  the whole one (and the same pages from both), their tables' making,
  and frames of new pixels in pairs and not, the table under "An ST's
  screen, for testing".
- **`SDL2BMP` (`tests/hatari/sdl2bmp.c`):** art `stdlconv bmp8` made
  (`sdl2bmp.py`), loaded with `SDL_LoadBMP` from the drive with the
  core's 4-bit `SAIL.BMP`, each hashed as `sdl2bmp.py` hashes Pillow's
  reading of it, and shown. On a TT and a Falcon on VGA and RGB in
  Hatari the hashes matched, and the screen matched the picture to
  the pixel, its colours exact on a TT and within 3 of 255 on a
  Falcon.
