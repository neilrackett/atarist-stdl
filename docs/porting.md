# Porting SDL 1.2 code to STDL

The compat header (`include/compat/SDL.h`) maps the common SDL 1.2
surface so ported source stays recognisable. Anything not mapped is
a **compile error by design**: the porter finds the gaps at build
time, not at runtime.

Porting an SDL2 program to a TT or a Falcon only? That is the SDL2
add-on's job, not this shim's: [sdl2.md](sdl2.md). Its headers are
separate, and must never meet this one's.

## Workflow

1. Convert assets with `stdlconv`; verify visually in Hatari.
2. Add `-Iinclude/compat` and build against libcmini and
   `libstdl-cmini.a` ([building.md](building.md)); let compile and link
   errors enumerate the work. libcmini is the default and mintlib the
   exception, for a game that needs something only mintlib has
   (MiNT's processes, signals or sockets, or another symbol the
   libcmini link reports missing that is not worth rewriting): it
   costs about 100K of the RAM a port has to live in. A port moving
   from mintlib sets its stack in the program (`long _stksize`), as
   libcmini's start-up reads that rather than the link's `--stack`,
   and compares a seeded run's frames and saved files with the
   mintlib build's before switching for good; one port's matched
   frame for frame and byte for byte.
3. Replace unsupported calls with STDL primitives.
4. Replace per-pixel drawing with spans or pre-rendered sprites.
5. Align sprites to 16px or enable `STDL_PRESHIFT` - on a plain ST.
   With a BLiTTER the chip shifts sprites and unaligned blits itself
   and pre-shifting only costs RAM, so a port can pre-shift only when
   `STDL_GetMachineInfo()->has_blitter` is 0 - if it double-buffers.
   Onto the displayed screen of a single-buffered program, keyed and
   shifted blits and sprites stay on the CPU, because the chip's
   passes would show between one and the next.
6. Profile on a plain ST first (`MACHINE=st`), then an STE and a Mega
   STE; `dist/TBLITSPD.TOS` gives a baseline, and the BLiTTER takes
   large fills, blits and sprites automatically.
* **Known to affect an open border**: disk access, a mouse nobody
  reads, resident firmware and a speed change, all measured on
  hardware and all invisible in Hatari - see
  [overscan.md](overscan.md#what-disturbs-an-open-border). Load
  assets before opening a border.
* **Alt-RAM and the hardware**: the Shifter and the sound DMA can
  only reach ST RAM. A program linked with the ALTALLOC flag - the
  toolchain default - gets alt-RAM back from `malloc` on any
  machine that has some, so any buffer the *hardware* reads has to
  come from `Mxalloc` with mode 0 instead. The library does this
  for everything it allocates itself (screen pages, the overscan
  pages, both DMA sound rings) as of v1.10.1; what is left to you
  is any buffer you hand to `STDL_PlaySample` or
  `STDL_PlaySampleLoop`, which now refuse a block outside ST RAM
  rather than playing noise.
  Do not respond by clearing ALTALLOC. Alt-RAM is what makes a 1MB
  machine viable for a large port - one 594KB game dies at its
  first allocation without it - so the fix is surgical: hardware
  buffers in ST RAM, everything else wherever there is room. The
  symptom when this is wrong is a screen of garbage or silence with
  no error at all, reported from an accelerated STF and an STE both
  with alt-RAM enabled.
* **Turn the mouse off if you never read it**: `STDL_EnableMouse(0)`
  stops the IKBD reporting it. Every movement is a three-byte
  packet, one ACIA interrupt per byte about 1.3ms apart, parsed
  whether or not anybody asked, and with a border open those
  interrupts flicker it ([overscan.md](overscan.md)). Joystick
  traffic is unaffected, the library puts reporting back when it
  releases the keyboard including down the terminate path, and
  `STDL_GetMouseState` simply stops changing rather than failing.
  Two joysticks (`STDL_SetNumJoysticks`) turn it off as well, and the
  mouse port's stick reports instead.
* **Pausing the voice device**: an open sound DMA clicks about once
  every thirty seconds on a real STE even looping pure silence, and
  that is hardware rather than anything the library does. If your
  port has long silent stretches - menus, cutscenes, a title screen
  - `STDL_PauseVoices()` stops the DMA without giving up the
  16640-entry volume table, which `STDL_OpenVoices` spends about a
  frame building. `STDL_ResumeVoices()` restarts it, with the first
  audio about 20ms later.
  Pause only asks: the DMA stops from the tick once the ring has
  drained to silence, so a stop never lands mid-waveform and never
  cuts a playing voice off. That makes it safe to call the moment
  `STDL_VoiceActive` reports all four idle, and pause-then-resume
  inside that window costs nothing because the DMA never stopped.
  **Call `STDL_ResumeVoices()` unconditionally, never behind a
  `STDL_VoicesPaused()` test** - cancelling a pause that has not
  been taken yet is most of its job, and skipping it leaves the
  request standing, which stops the device later with no error and
  no sound. One port measured its own idle pattern before adopting
  this: one unbroken 61.8-second idle stretch across intro, title
  and first cutscene, and in gameplay 44 gaps with a maximum of
  1.16s, so a two-second hysteresis fires once per quiet stretch
  and never during play.
  The shape of a real caller, from the port that asked for this:
  poll `STDL_VoiceActive` on all four voices once a frame, and two
  seconds after the last goes quiet call `STDL_PauseVoices` once;
  call `STDL_ResumeVoices` immediately before every `STDL_SetVoice`,
  at every call site. Drive that poll from wherever your platform
  layer pumps events rather than from the game loop - a cutscene
  player usually does not run the game loop, and cutscenes are
  where the idle time is. Measured in that port: the DMA stops 8.4s
  in and does not restart until 68.3s, across two cutscenes and a
  title screen, with no pause or resume at all during play.
  The transitions cost nothing audible: measured on a real STE,
  cycling pause and resume once a second clicked *less* than the
  same device left open and idle, which is what happens when the
  hardware's baseline click only accrues while the DMA is running.
  So the time spent paused is time without it.
* **Display rate**: `STDL_SetRefresh(50)` or `(60)` switches the
  sync rate, `-1` queries, and the return value is the rate that
  actually took effect rather than the one asked for. A PAL machine
  boots at 50Hz and an NTSC one at 60; 60Hz gives ten more frames a
  second with the same 320x200 picture. The VBL rate moves with it,
  so time from `STDL_GetTicks` rather than counting frames -
  `STDL_Music`, the `STDL_SetVoiceTick` sequencer and `STDL_Sfx`
  steps (from v1.12.0) compensate; `STDL_AddVBL` callbacks are
  per-tick by definition and cannot. A border forces
  50Hz while it is open (the overscan tricks are PAL-timed) and
  applies the request on the final close. Some period TVs will not
  sync 60Hz on a PAL machine, so check the return value.
* **Mega STE speed**: `STDL_Init` switches a Mega STE to 16MHz with
  its cache on. `STDL_UseMegaSteSpeedup(0)` leaves the machine as
  the user set it (call it before `STDL_Init` to prevent the switch,
  or after to undo it), and 2 and 3 select the clock without and
  with the cache. Use it to compare speeds inside one binary rather
  than believing two builds: `examples/blitchk.c` does. Change
  speed with any border closed ([overscan.md](overscan.md)).

7. Verify on plain ST at 8MHz - the correctness floor, and the
   machine the port is for. The blitter, DMA audio, hardware
   scrolling and 16MHz are enhancements to use where present, not
   requirements to lean on: a port that needs an STE (hardware
   scrolling does) should be an explicit decision, and one that
   needs 16MHz to be playable needs its rendering fixed instead.

## What maps directly

| SDL 1.2 | STDL | Notes |
|---|---|---|
| `SDL_Init` / `SDL_Quit` | `STDL_Init` / `STDL_Quit` | enters supervisor mode for the program's lifetime |
| `SDL_SetVideoMode` | `STDL_SetVideoMode` | w/h/bpp ignored, always 320x200x4, unless the program asked for a responsive option (`stdl_responsive.h`): then bpp 8 is the colour and h over 200 the tall screen |
| `SDL_BlitSurface` | `STDL_BlitSurface` | full SDL semantics incl. dstrect writeback |
| `SDL_FillRect` | wrapper | colour is a palette index, not mapped RGB |
| `SDL_Flip` | `STDL_Flip` | VBL-synced; page flip with `SDL_DOUBLEBUF`, including while an overscan border is open; `STDL_TRIPLEBUF` adds a third page so a frame longer than a VBL is not rounded up to whole VBLs |
| `SDL_UpdateRect(s)` | no-op | single-buffer rendering is direct to screen RAM; on a responsive 8-plane screen it says what the program wrote to the pixels itself |
| `SDL_MapRGB` | `STDL_MapRGB` | nearest palette index |
| `SDL_LockSurface` | no-op | surfaces are always addressable |
| `SDL_MUSTLOCK` | `0` | |
| `SDL_DisplayFormat` | duplicate | already planar; result flagged `SDL_HWACCEL` |
| `SDL_SetColorKey` | `STDL_SetColourKey` | builds the transparency mask (a snapshot); key >= 16 = use the existing mask, no scan - for surfaces whose transparency is built during decode |
| `SDL_LoadBMP` | `STDL_LoadBMP` | uncompressed 1/4/8bpp, max 16 colours |
| `SDL_SetColors` / `SDL_SetPalette` | palette module | `SDL_LOGPAL` / `SDL_PHYSPAL` honoured; `STDL_StagePalette(1)` lands changes in the vertical blank |
| events / keysyms / `SDL_GetKeyState` | event module | numbering matches SDL 1.2; the keyboard is taken at `STDL_SetVideoMode`, or by `STDL_OpenInput` for a program that sets no mode. `SDL_PollEvent` pumps once a VBL and otherwise only takes from the queue, so a drain loop is one pump a frame; `SDL_PumpEvents` and `SDL_WaitEvent` always pump |
| `SDL_GetTicks` / `SDL_Delay` | time module | 200Hz clock: 5ms resolution |
| `SDL_AddTimer` / `SDL_SetTimer` | compat | cooperative: callbacks fire inside `SDL_Delay` and `SDL_PollEvent`, once a 200Hz tick at most |
| `SDL_OpenAudio` / `SDL_LoadWAV` | `STDL_Audio` | STE DMA; cooperative refill, 2% of an 8MHz STE plus your callback when it writes signed 8-bit at a DMA rate straight into the ring; other formats are converted at every refill (13% for unsigned 8-bit mono at 12517Hz, 48% for 16-bit stereo at 22050); PCM WAVs only (`stdlconv wav`) |
| `Mix_PlayMusic` etc (SDL_mixer) | `STDL_Music` | YM register streams from `stdlconv midi` (or `stdlconv opm` for YM2151 music logged as VGM); works on every ST. Natively: `STDL_MusicFromMemory`, `STDL_MusicFrame`/`STDL_MusicLength`/`STDL_SeekMusic` for position, `STDL_GetMusicRegisters` for meters |
| `Mix_PlayChannel` / `Mix_LoadWAV` | mixer shim | up to 4 chunks on the `STDL_Voice` mixer, from the VBL (STE only): 5% of an 8MHz STE for one chunk at 6258Hz and 20% for four, 8% and 40% at 12517 - the rate `Mix_OpenAudio` picks for a PC game's 22050; half scale at full volume |
| one-shot game effects | `STDL_PlaySample` | monophonic, but the DMA reads your buffer directly: no refill, no mixing, no per-frame cost |
| PC-speaker sound | `STDL_SpeakerOn/Off` | immediate YM tone; steals voice A from music, restores after |
| PC-speaker sequences | `STDL_PlaySfx` | step-array effects (periods+volumes), tone or noise, auto voice; optional per-step noise for tone and noise together (gunshots, sinking explosions) |
| music as live notes (MIDI-note streams, a tracker of your own) | `STDL_ToneOn/Set/Off` | 16 note slots onto the 3 YM voices, last-note priority; above `STDL_Music`, below effects. `STDL_ToneOpen` once from the main line if the notes come from your own timer or VBL callback |
| AdLib / IMF music and effects (OPL2 register streams) | `STDL_OplWrite` | one call per register write; channels 0-8 become tone slots 0-8, the replay rate stays the game's |
| `SDL_Joystick*` | compat veneer | port 1 as stick 0: 2 digital axes, 1 button (6 axes, 13 buttons and a hat with an Xpad pad). More only where the port asks: `STDL_SetNumJoysticks(2)` makes the mouse port stick 1 (the mouse off), and more add pads - up to 4 sticks merged, 6 apart (`STDL_UseJoystickMerge(0)`); no hotplug, so open what `SDL_NumJoysticks` says after asking. `SDL_JoystickGetAxis` and `GetHat` are `STDL_GetJoystickAxis` and `STDL_GetJoystickHat`, `GetButton` `STDL_JoystickInputHeld` |
| `SDL_CreateCursor` / `SDL_SetCursor` | `STDL_Cursor` | software save-under cursor, max 32x32 |

## Game services (extracted from the FreeNukum and Sopwith ports)

* **Scrolling cameras**: `STDL_SetSurfaceOrigin(stripe, 0, camera_y)`
  lets a short surface stripe stand in for a tall level - blits and
  rect fills translate their coordinates, so the game keeps using
  level coordinates unchanged.
* **Composition**: filling with `STDL_TRANSPARENT` punches holes in
  a masked surface; blits onto masked surfaces maintain the mask;
  `STDL_SurfaceIsOpaque` is a cached query for skipping backdrop
  under solid tiles; `STDL_CreateMask` attaches a mask without a
  colour-key scan.
* **Byte-per-pixel source art**: `STDL_SurfaceFromIndexed8(bytes, w,
  h, stride, keycolour)` converts a chunky, one-byte-per-pixel image
  to a planar surface in one call - the thing every port so far has
  hand-rolled. `stride` is the source row pitch, so a frame lifts
  straight out of a wider sheet, and `keycolour` is a source byte
  value that becomes transparent (`-1` for none). Freeze the result
  with `STDL_SpriteFromSurface`.
* **Recolouring the same artwork**: `STDL_RemapSurface(s, map)`
  rewrites the pixels through a 16-entry index map, leaving the
  transparency mask alone - team colours, faction colours, damage
  flashes. Do it once per variant at load time and blit the variants
  normally; there is deliberately no blit-time remap (`docs/limits.md`
  explains why planar data cannot do one cheaply).
* **Runtime asset decoding**: `STDL_PutGroup` writes one 16-pixel
  group (planes + mask) - for games that decode proprietary data
  files at load in a format nothing else matches;
  `STDL_PutGroup8` does the same for the 8-pixel half-group when
  the source data is byte-granular. Pair either with
  `STDL_SetColourKey(s, 1, 16)`, which enables masked blits from
  the mask the decoder wrote instead of scanning for a key colour.
* **A 50Hz tick**: `STDL_AddVBL(fn)` / `STDL_RemoveVBL(fn)` put a
  callback in a TOS VBL queue slot, for the one job the cooperative
  services cannot do - stepping a sequencer at a rate that does not
  follow how long drawing takes. It is an interrupt, with the tight
  contract in `include/stdl/stdl_vbl.h`: no GEMDOS, no allocation, no
  YM writes while STDL owns the chip. Everything else belongs on the
  pump. Ports that reached into `_vblqueue` at $456 themselves should
  move to this: STDL removes its callbacks on every exit path,
  including the ones that never run `atexit`.
* **Hardware scrolling (STE)**: `STDL_SetScrollOrigin(world, x, y)`
  shows the 320x200 window whose top-left pixel is (x, y) of a
  surface wider and taller than the screen - to the pixel, and
  without copying anything: the STE's video base, LINEWIDTH and
  HSCROLL registers do the work. `STDL_SetScrollWindow(base, stride,
  xfine)` is the same for a raw planar block. Keep the surface at
  least 336 pixels wide (the Shifter fetches one extra group per line
  while the fine scroll is non-zero) and its stride at most 670
  bytes (LINEWIDTH is one byte of words). The base is latched three
  lines before the VBL and the offsets are not, so the VBL writes the
  base, the video counter and both offsets together before the fetch
  begins - no frame shows a mismatched pair, and a request is on
  screen at the next VBL however late in the frame it was made (a
  frame later only if that VBL runs late into the picture);
  `STDL_ScrollWindowPending` says when it has landed,
  for a page-flipping caller that must not draw into the page still
  being fetched. `STDL_HasHwScroll` is the test to make at init: on a
  plain ST the calls fail cleanly (the next bullet is what to do
  there). Do not mix with `STDL_Flip`: both program the
  base; `STDL_ResetScrollWindow` puts the stock registers back for a
  program that returns to page flipping mid-run (`STDL_Quit` and the
  terminate path do it anyway). See `examples/hwscroll.c`, and the
  skill's scrolling notes for three ways to lay out the play field -
  strips spread over the frames that uncover them, a base that walks
  forward instead of wrapping, and a page duplicated below itself for
  endless vertical scrolling.
* **Scrolling without the STE's registers** (a plain ST, or a TT's or a
  Falcon's 8-plane screen, which has none): it is a copy, and the copy
  is most of the frame. Measured with `tests/hatari/scrlcost.c` in
  Hatari, a redraw of a 320x200 screen of 16x16 tiles is 97.9ms on a
  plain ST, so redraw only the strip that comes into view (a column of
  13 tiles, 4.9ms). Keep the tiles in a background buffer bigger than
  the screen that sprites never touch, copy the window into the back
  page with `STDL_BlitSurface` and a srcrect, and draw the sprites on
  top: nothing then needs restoring, flipped pages included. The whole
  page in one `memcpy` is 19.3ms on a plain ST, a whole 50Hz frame, and
  a window out of a wider buffer 20.7ms, as the library copies rows of
  whole groups with `movem` (26.4 across a ring's seams, where each row
  is two pieces). So a full-screen scroller there is a 25Hz game, with
  room for sprites: with eight pre-shifted ones and a new column a
  frame is about 33ms (an estimate from the parts). Smooth horizontal
  scrolling on a plain ST needs a ring for each pixel phase (37.6K each)
  and an aligned copy from the one that matches x: 43.3ms a frame with
  16 rings, a pixel a frame. On a TT's or a Falcon's 8 planes the window
  is 8.2ms or 16.5, and 12.4 or 24.9 at a sub-group x, so one ring
  scrolls a pixel a frame without pre-shifted copies. A surface blitted
  onto itself comes out right whichever way it moves, but rightwards
  off whole groups it goes through a buffer, several times a copy's
  cost (`limits.md`). On a TT, Flip's
  present adds 8.9ms to any frame that changed the whole screen. The
  skill's scrolling notes have the measured table, the 8-plane figures
  and the recipes.
* **Splash screens**: `STDL_ShowDegas("SPLASH.PI1")` after
  SetVideoMode shows a Degas picture with its palette while the
  game loads (`stdlconv pi1` converts, `stdlconv embed` makes C
  arrays for linked-in splashes).
* **Joystick for keyboard games**: `STDL_JoystickKeyEmulation(1)` turns
  stick changes into key events (tagged `STDL_KMOD_JOYSTICK`, key
  state included) - keyboard-bound games need no joystick code at
  all. The default mapping is arrows + left Alt;
  `STDL_JoystickKeyMapping(STDLK_w, STDLK_s, STDLK_a, STDLK_d,
  STDLK_SPACE)` rebinds it to the game's own convention (returns
  how many keys resolved, so bad bindings are caught; 0 leaves an
  input unmapped). It is joystick 0's alone: a second player's stick
  sends joystick events and never keys.
* **Two players, or up to six**: `STDL_SetNumJoysticks(2)`, once,
  before or after `STDL_SetVideoMode`, gives two joysticks: 0 is the
  joystick port, 1 the mouse port, the mouse off while there are two
  (the IKBD reports both ports then, and the mouse port cannot be
  both). More add an Xpad provider's pads where it has them, and the
  call returns how many there are. By default each port is merged
  with the pad of its number (0 with pad 0, 1 with pad 1), as
  joystick 0 always has been, and pads 2 and 3 are joysticks 2 and 3:
  four at most. `STDL_UseJoystickMerge(0)` keeps them apart instead:
  the two ports, then pads 0 to 3 as joysticks 2 to 5, six at most -
  for more players, or a port and a pad that are two players'. It can
  change at any time; whatever changes hands is released first. A
  joystick keeps its number when pads come and go. Events carry it in
  `which`; `STDL_GetJoystickState(which)`,
  `STDL_JoystickInputHeld(which, input)`, `STDL_HasPad(which)`,
  `STDL_GetJoystickAxis(which, axis)` and `STDL_GetJoystickHat(which)`
  poll it, and the SDL 1.2 shim opens every one and reads them through
  those. `STDL_UseJoystickDeviceEvents(1)` adds `STDL_JOYDEVICEADDED`
  and `STDL_JOYDEVICEREMOVED` as joysticks come, go or are re-mapped,
  each after the releases it brings; SDL 1.2 has no such event, and
  the shim never asks for them. Asking for 1 again gives
  the mouse back, and so does every exit, a crash included. A program
  that never asks links none of it: the hooks, the readers' index, the
  merge setting and the device events' push cost every program about
  590 bytes (and the SDL 1.2 shim about 510 more, with the axis and
  hat readers, `joyread.o`, it reads every stick through), the code
  behind the other five is 1.3K of its own (`joysticks.o`), and the
  setting's call 160 bytes (`joymerge.o`). No pad event is lost to the
  32-event queue: a pad waits for room, its events a pump late.
* **The joystick calls take the joystick first**:
  `STDL_GetJoystickState(0)`, `STDL_JoystickInputHeld(0, input)`,
  `STDL_HasPad(0)` and `STDL_PadRumble(0, low, high, ms)` are what a
  one-player game calls. They took the number, and their names that
  say Joystick, in v1.23.0; the `stdl` skill's "Moving to a newer
  STDL" says what older code changes.
* **Rumble**: SDL 1.2 has none, so it is an addition rather than a
  mapping. `STDL_PadRumble(0, low, high, ms)` pulses an Xpad pad's heavy
  and light motors (0-255 each) and returns -1 when there is no pad
  or it has no motors, so call it unconditionally at the moment it
  belongs to - a hit, a landing, an explosion. The event pump ends
  the pulse, so it runs long while the game is not pumping.
  The first argument is the joystick, so each player's pad rumbles on
  its own.
* **Filenames**: loaders retry with an uppercased name, so
  `"icon.bmp"` works on GEMDOS.

### A world taller than 200 lines

A game designed for 224 or 240 lines has three ways onto the ST's
200: drop lines (artefacts), crop (hides play area), or open a
border - 228 lines with the top, 245 with the bottom, 273 with both,
on any 50Hz ST. The borders and rasters (colour registers changed
down the picture) have a page of their own: [overscan.md](overscan.md).

## More colours and lines on a TT or a Falcon

An ST game runs on a TT or a Falcon as it is (ST-specific gotchas,
below). What they add - 256 colours, and 240 lines - a game asks for by
name with `STDL_UseResponsive` (`stdl_responsive.h`), once, before the
first `STDL_SetVideoMode`, and one binary serves every machine:
[responsive.md](responsive.md).

## Rewrite patterns

**Direct pixel access.** There is no chunky buffer; `memset` rows
or `pixels[y*pitch+x]` writes corrupt planar data. Rewrite:

* gradient / row fills -> `SDL_FillRect` band per row or
  `STDL_HLine`
* filled polygons (a software rasteriser's triangles, vector
  graphics) -> `STDL_FillPolygon` per convex face, with
  `STDL_DRAW_SET` or XOR/OR/CLEAR into the planes of a colour.
  Shared edges tile exactly (top-left rule), and the edges are
  stepped without a span list. Triangles take a faster path of
  their own, so a mesh's faces are best left as triangles.
* dithered or fill-patterned faces (PICO-8's `fillp`, ordered-dither
  shading, fog, screen-door transparency) ->
  `STDL_FillPolygonPattern` with an `STDL_Pattern` built once by
  `STDL_MakePattern(&pat, col0, col1, bits, transparent)`. Vertices
  are 12.4 fixed point (`STDL_PointSub`), so a projected face keeps
  its sub-pixel position and moves smoothly; the coverage rule is
  `STDL_FillPolygon`'s, exact on those vertices, so faces sharing an
  edge still tile. The pattern is anchored to the surface (y & 3,
  x & 3), so neighbouring faces dither as one. A solid colour is
  just a pattern with col0 == col1.
* tiny overlays (FPS counters, debug text) -> `STDL_PutPixel`
  (slow path, fine for a handful of pixels) or `STDL_DrawText`
* 1bpp bitmap expansion -> `STDL_SurfaceFrom1bpp`; byte-per-pixel
  images -> `STDL_SurfaceFromIndexed8`
* load-time transforms (flips, remaps) -> `STDL_GetPixel` /
  `STDL_PutPixel` loops are acceptable off the hot path; a colour
  remap has a primitive, `STDL_RemapSurface`
* **text drawn a character at a time** (status bars, scores, ported
  CGA console code) -> `STDL_DrawChar(dst, font, x, y, ch, col)`
  rather than assembling a one-character string for `STDL_DrawText`.
  Identical output; what it saves is the per-call setup, which is
  what is left once the plane budget has halved the memory traffic
  (Sopwith makes 350 of these calls a frame)
* CGA/EGA `XOR` writes (erasable overlays, terrain outlines, tracer
  bullets) -> `STDL_XorRect` / `STDL_XorHLine` / `STDL_XorVLine` /
  `STDL_XorPixel`; drawing the shape twice restores the destination,
  so no save-under is needed. Only the planes selected by the colour
  are touched.
* **particle fields / starfields** (a loop of single pixels) -> fill
  an `STDL_Point` array and make one `STDL_Points` call, or
  `STDL_PointsC` with a parallel colour array if the field is
  multi-coloured. Same result as `STDL_PutPixel` per entry, but the
  clip setup, colour dispatch and plane-word selection are paid once
  for the list; erase the field next frame with a second call in the
  background colour and it needs no save-under and no dirty
  rectangle each (measured 1.5x against the equivalent one-pixel
  `STDL_HSpans` list on a plain ST, and 3.4x against `STDL_PutPixel`
  in a loop). Do not sort the list into colour runs to use the
  one-colour call: on a plain ST at 250 particles the sort and the
  fifteen calls it enables cost 23ms against 13.5 for a single
  `STDL_PointsC`
* **many short spans in a loop** (terrain profiles, column fields,
  raycaster walls, scanline shapes) -> fill an `STDL_Span` array
  and make one `STDL_VSpans` / `STDL_HSpans` / `STDL_XorVSpans` /
  `STDL_XorHSpans` call. A one- or two-pixel span costs far more to
  call than to draw, and the batched form pays the clip setup,
  colour dispatch and row-address multiply once for the whole list
  instead of once per span - measured at 1.6x on Sopwith's terrain
  outline. The result is defined to be identical to the per-span
  calls, so it is a drop-in change.

**Palette budget.** 16 entries replace 256. Divide them explicitly
(the ported `testpalette` reserves 0-7 for the sprite, 8-14 for
effects, 15 for the colour key / UI) and quantise assets to the
slice they own with `stdlconv bmp16 --colors N`.

**Colour keys.** Quantisation must not merge or approximate the key
colour. `stdlconv bmp16 --keycolor RRGGBB` keeps the exact key at
index 15 and excludes it from quantisation.

**Per-pixel game loops.** Rendering that plots pixels every frame
(plasma, scaled sprites, software 3D) has no cheap planar
equivalent; redesign around spans, tiles and sprites, or pre-render.

**Fewer colours than 16.** Count the colour indices the game
actually draws (not the palette size - the maximum index used). If
it is under 4 or under 8, `STDL_SetPlaneBudget(2)` / `(3)` after
`STDL_SetVideoMode` tells STDL to stop maintaining the higher
bitplanes, and every fill, span, blit, sprite, tile, glyph and
BLiTTER pass moves proportionally less memory. The catch is that
the promise covers the whole program: any path that writes a higher
index - including raw planar pokes into `surface->pixels` that
bypass the API - has to be found first, because the budget
truncates colours to the low N bits rather than rejecting them. See
`docs/format.md` for the exact contract.

## ST-specific gotchas

* **Filenames**: GEMDOS is 8.3 and uppercase; load `"ICON.BMP"`,
  not `"icon.bmp"`.
* **stdout is buffered**: console output may appear only at exit.
  `fprintf(stderr, ...)` is unbuffered.
* **The console prints onto your screen**: printf during play will
  scribble over low-res screen RAM. Fine for tests, not for games.
* **Arguments**: programs launched from the desktop get no argv;
  make sure defaults are sensible.
* **A TT or a Falcon runs an STE program**, in ST Low, and the
  library covers where they differ:
  - **Rate.** Their display rate is not the sync register's, so it is
    measured at `STDL_Init` and after each `STDL_SetVideoMode` (about
    a third of a second there); music, effects and the voice tick
    divide by it. Hatari's TT runs ST Low at 60, a Falcon on VGA its
    desktop at 60. `STDL_SetRefresh` changes nothing there: it measures
    and returns the rate.
  - **Sound.** DMA sound is decided by the `_SND` cookie (a TT's can be
    switched off). A Falcon has no 6258Hz: the voices, the SDL_mixer
    shim and `STDL_OpenAudio` play at 12517 there, and
    `STDL_PlaySample` plays a 6258Hz buffer from a copy at 12517. Its
    crossbar is routed DMA-to-DAC on the first start and put back at
    exit, so a Falcon program run before cannot leave yours silent.
  - **Desktop.** A Falcon's is a VIDEL mode `Getrez()` cannot describe
    and a TT's uses more than 16 colours, so the mode and all 256
    colour registers come back at exit; the monitor,
    not `Getrez()`, says whether it is mono (a TT shows ST High on
    colour, TT High only on mono).
  - **Refused there:** borders, hardware scrolling and rasters (Timer B
    counts their doubled lines in a way not yet measured on the
    machines). A Falcon's BLiTTER is left alone: the 68030's data cache
    does not see its writes.
  `tests/hatari/ttchk.c` and `tests/hatari/falcchk.c` check all of it.
* **Many copies of one sprite**: `STDL_BlitSprites` draws a list of
  positions and frames in one call, and `STDL_DirtyWholeGroups(1)`
  makes their restores plain copies of whole groups, where the scene
  allows it.
* **Overlapping dirty-rect sprites** (testsprite-style erase/redraw)
  show tearing between overlapping sprites at ST frame rates; use
  `STDL_Dirty` restore or a back buffer for real games. The two
  combine: `STDL_DirtyRestore(screen)` then draw and push, every
  frame, and with `STDL_DOUBLEBUF` (or `STDL_TRIPLEBUF`) each page
  keeps its own list, so the back page is erased of what was drawn
  into it last time rather than of what is on screen. When the
  background itself changes (a score drawn into it, a door, a new
  level), `STDL_DirtyBackground(&rect)` (`NULL` for all of it) puts the
  change on every page as each comes round.
* **Key repeat**: the IKBD sends no auto-repeats; STDL synthesises
  them only after `SDL_EnableKeyRepeat`, matching SDL.
* **Slow frames swallow key presses**: when a frame takes longer
  than the press, KEYDOWN and KEYUP arrive in the same event poll,
  and game code that treats a flag as "held" sees nothing. Apply
  releases at the *start of the next* poll instead of immediately,
  so every press stays visible for at least one full frame
  (a port lost its cutscene-skip key this way at 2fps).
* **Old engine code vs gcc 4.6 at -O2**: build ported engines with
  `-fno-strict-aliasing` before trusting any crash. One port's
  unmodified game logic was miscompiled under strict-aliasing rules
  into layout-dependent heap corruption - address/bus errors far
  from the real cause. (Map exception PCs to symbols with
  `tests/hatari/map-crash.sh`.)
* **Multiplies and divides that are function calls**: gcc 4.6 uses
  the 68000's 16-bit `muls.w`/`divs.w` only when it can see both
  operands are 16-bit and of one signedness. `int * int`, two bytes
  multiplied, a value from a loop counter, `/` or `%` by a
  non-power-of-two (constants included) and `-O1` builds all call
  libgcc instead, at about 350 and 630 cycles a time on a plain ST.
  Use `STDL_MulS16`/`STDL_MulU16`, `STDL_DivS16`/`STDL_DivU16`,
  `STDL_FixDiv` and `STDL_FixMul` (a 16.16 product, truncated toward
  minus infinity exactly as `(int32_t)(((int64_t)a * b) >> 16)` -
  PICO-8's multiply - where that C is a `__muldi3` call) from
  `stdl_math.h` on per-frame paths, and run `tools/libcalls.sh` over
  the port's objects to list every call per function. With `-a` and
  an allowlist it fails the build on a new one.
* **libc memcpy in per-frame paths**: mintlib's memcpy overhead
  dominates short copies. 160 small per-line memcpys cost
  one port ~13ms/frame on an 8MHz ST; inline word-copy loops
  cut the same work to ~3ms. Reserve memcpy for bulk moves.
* **Runtime-decoded chunky art**: engines that RLE-decode sprite
  frames to byte-per-pixel scratch at draw time should draw them
  with `STDL_BlitIndexed8` (one pass, per-call colour map, flip and
  column-major variants, destination-mask composition) - never with
  a per-frame `STDL_SurfaceFromIndexed8`, which allocates and
  converts twice per draw.
* **Frames drawn many times**: when the same frame, colour bank and
  palette recur (an animation cycle), bake it once into a planar
  surface and afterwards compose it with `STDL_BlitSurfaceEx`, whose
  `STDL_BLIT_UNDER`/`STDL_BLIT_MARK` give the same priority
  semantics as the chunky path. To bake: clear the scratch mask to
  all ones (every pixel transparent), then blit the chunky source
  with `STDL_BlitIndexed8` and *no* `STDL_I8_MARK` - its default
  maintenance clears a mask bit under each pixel it draws, which
  leaves exactly the source convention (bit set = destination
  preserved). Measure before committing to it: baking costs about
  what one chunky draw costs, so a frame drawn once pays for a bake
  it never reuses (bake on second sighting), and the cache must key
  on something stable - one port's first attempt keyed on
  decoded-frame addresses that moved every animation lap, so it
  never hit. Give the block a group of slack at *both* ends, as
  `STDL_CreateSurfaceFrom` asks: the unaligned path reads one group
  either side of a row.
* **256-colour palette logic on 16 hardware colours**: quantise the
  logical palette to 16 and remap at decode/draw time. Keep the
  quantiser's hardware-slot assignment *sticky* across palette
  changes (match new colours to the nearest previous slot), or
  content baked under the old mapping displays in permuted colours.
  Detect pure-brightness changes (fades) and rescale the registers
  without rebuilding the mapping.
