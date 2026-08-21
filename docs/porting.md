# Porting SDL 1.2 code to STDL

The compat header (`include/compat/SDL.h`) maps the common SDL 1.2
surface so ported source stays recognisable. Anything not mapped is
a **compile error by design**: the porter finds the gaps at build
time, not at runtime.

## Workflow

1. Convert assets with `stdlconv`; verify visually in Hatari.
2. Add `-Iinclude/compat` and build; let compile and link errors
   enumerate the work.
3. Replace unsupported calls with STDL primitives.
4. Replace per-pixel drawing with spans or pre-rendered sprites.
5. Align sprites to 16px or enable `STDL_PRESHIFT`.
6. Profile on Mega STE (`dist/TBLITSPD.TOS` gives the baseline;
   the blitter accelerates large same-phase fills/blits
   automatically - align to 16px to benefit).
7. Verify on plain ST at 8MHz - the correctness floor.

## What maps directly

| SDL 1.2 | STDL | Notes |
|---|---|---|
| `SDL_Init` / `SDL_Quit` | `STDL_Init` / `STDL_Quit` | enters supervisor mode for the program's lifetime |
| `SDL_SetVideoMode` | `STDL_SetVideoMode` | w/h/bpp ignored; always 320x200x4 |
| `SDL_BlitSurface` | `STDL_BlitSurface` | full SDL semantics incl. dstrect writeback |
| `SDL_FillRect` | wrapper | colour is a palette index, not mapped RGB |
| `SDL_Flip` | `STDL_Flip` | VBL-synced; page flip with `SDL_DOUBLEBUF` |
| `SDL_UpdateRect(s)` | no-op | single-buffer rendering is direct to screen RAM |
| `SDL_MapRGB` | `STDL_MapRGB` | nearest palette index |
| `SDL_LockSurface` | no-op | surfaces are always addressable |
| `SDL_MUSTLOCK` | `0` | |
| `SDL_DisplayFormat` | duplicate | already planar; result flagged `SDL_HWACCEL` |
| `SDL_SetColorKey` | `STDL_SetColourKey` | builds the transparency mask (a snapshot); key >= 16 = use the existing mask, no scan - for surfaces whose transparency is built during decode |
| `SDL_LoadBMP` | `STDL_LoadBMP` | uncompressed 1/4/8bpp, max 16 colours |
| `SDL_SetColors` / `SDL_SetPalette` | palette module | `SDL_LOGPAL` / `SDL_PHYSPAL` honoured |
| events / keysyms / `SDL_GetKeyState` | event module | numbering matches SDL 1.2 |
| `SDL_GetTicks` / `SDL_Delay` | time module | 200Hz clock: 5ms resolution |
| `SDL_AddTimer` / `SDL_SetTimer` | compat | cooperative: callbacks fire inside `SDL_Delay` |
| `SDL_OpenAudio` / `SDL_LoadWAV` | `STDL_Audio` | STE DMA; cooperative refill; PCM WAVs only (`stdlconv wav`) |
| `Mix_PlayMusic` etc (SDL_mixer) | `STDL_Music` | YM register streams from `stdlconv midi`; works on every ST |
| `Mix_PlayChannel` / `Mix_LoadWAV` | mixer shim | up to 4 chunks software-mixed over the DMA device (STE only) - **costs real CPU**, see below |
| one-shot game effects | `STDL_PlaySample` | monophonic, but the DMA reads your buffer directly: no refill, no mixing, no per-frame cost |
| PC-speaker sound | `STDL_SpeakerOn/Off` | immediate YM tone; steals voice A from music, restores after |
| PC-speaker sequences | `STDL_PlaySfx` | step-array effects (periods+volumes), tone or noise, auto voice |
| `SDL_Joystick*` | compat veneer | port 1 as stick 0: 2 digital axes, 1 button |
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
* **Splash screens**: `STDL_ShowDegas("SPLASH.PI1")` after
  SetVideoMode shows a Degas picture with its palette while the
  game loads (`stdlconv pi1` converts, `stdlconv embed` makes C
  arrays for linked-in splashes).
* **Joystick for keyboard games**: `STDL_JoyKeyEmulation(1)` turns
  stick changes into key events (tagged `STDL_KMOD_JOYSTICK`, key
  state included) - keyboard-bound games need no joystick code at
  all. The default mapping is arrows + left Alt;
  `STDL_JoyKeyMapping(STDLK_w, STDLK_s, STDLK_a, STDLK_d,
  STDLK_SPACE)` rebinds it to the game's own convention (returns
  how many keys resolved, so bad bindings are caught; 0 leaves an
  input unmapped).
* **Filenames**: loaders retry with an uppercased name, so
  `"icon.bmp"` works on GEMDOS.

## Rewrite patterns

**Direct pixel access.** There is no chunky buffer; `memset` rows
or `pixels[y*pitch+x]` writes corrupt planar data. Rewrite:

* gradient / row fills -> `SDL_FillRect` band per row or
  `STDL_HLine`
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
* **Overlapping dirty-rect sprites** (testsprite-style erase/redraw)
  show tearing between overlapping sprites at ST frame rates; use
  `STDL_Dirty` restore or a back buffer for real games.
* **Key repeat**: the IKBD sends no auto-repeats; STDL synthesises
  them only after `SDL_EnableKeyRepeat`, matching SDL.
* **Slow frames swallow key presses**: when a frame takes longer
  than the press, KEYDOWN and KEYUP arrive in the same event poll,
  and game code that treats a flag as "held" sees nothing. Apply
  releases at the *start of the next* poll instead of immediately,
  so every press stays visible for at least one full frame
  (REminiscence's cutscene-skip key was lost this way at 2fps).
* **Old engine code vs gcc 4.6 at -O2**: build ported engines with
  `-fno-strict-aliasing` before trusting any crash. REminiscence's
  unmodified game logic was miscompiled under strict-aliasing rules
  into layout-dependent heap corruption - address/bus errors far
  from the real cause. (Map exception PCs to symbols with
  `tests/hatari/map-crash.sh`.)
* **libc memcpy in per-frame paths**: mintlib's memcpy overhead
  dominates short copies. 160 small per-line memcpys cost
  REminiscence ~13ms/frame on an 8MHz ST; inline word-copy loops
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
  on something stable - REminiscence's first attempt keyed on
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
