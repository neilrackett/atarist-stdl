---
name: stdl
description: Port SDL 1.2 games to the Atari ST with STDL, the planar-native SDL subset. Use when porting SDL 1.x code to Atari ST/STE/Mega STE, writing STDL code, converting game assets to ST planar format, or debugging planar rendering. Covers the surface format contract, API costs, SDL-to-STDL rewrite patterns, stdlconv usage and the Hatari test loop.
---

# STDL (Atari ST DirectMedia Layer) - porting SDL 1.2 games to the Atari ST

STDL is a deliberately incomplete, planar-native subset of SDL 1.2.
There is no chunky backbuffer and no c2p pass anywhere: assets are
converted to ST interleaved planar offline, and spans/rects/blits
are the primary verbs. The screen is always 320x200, 4 planes,
16 colours.

**Changing STDL itself?** A port often wants a primitive the
library does not have yet, and adding it there beats hand-rolling
one in the game. Read `AGENTS.md` in the STDL repo first: it
carries the build and test loop, the size budget, the asm-with-a-
C-twin rule, and what shipping a change means - a public API is not
done until `docs/` and this skill describe it, and lessons are
written without naming the port they came from.

## The format contract (memorise this)

- Pixels group in 16s; a group = 4 consecutive plane words (8 bytes).
- Pixel `x` is bit `15 - (x & 15)` (MSB first) in each plane word.
- Group address: `pixels + y * stride + (x >> 4) * 8`; screen stride
  is 160.
- Colour index bit `p` comes from plane word `p` (LSB first).
- Transparency masks (and sprite masks): bit set = destination
  preserved. Sprite data interleaves `[mask][p0][p1][p2][p3]` per
  group, with plane bits pre-cleared under the mask so the draw is
  `*dst = (*dst & mask) | *src`.

Full contract: `docs/format.md`. Never invent a different layout.

## API cost annotations

| Cheap - use freely | Careful | Avoid in loops |
|---|---|---|
| `STDL_FillRect`, `STDL_HLine` | `STDL_BlitSurface` unaligned (shift chain) | `STDL_PutPixel` / `STDL_GetPixel` |
| `STDL_XorVSpans` / `STDL_VSpans` (span lists) | `STDL_XorRect` (CPU only, no BLiTTER) | `STDL_XorPixel` in a loop |
| `STDL_Points` / `STDL_PointsC` (particle fields) | | `STDL_PutPixel` in a loop |
| `STDL_XorVLine` (a few spans) | `STDL_XorVLine` x100+ (batch it instead) | one `STDL_*Line` call per column |
| aligned blits (same `x & 15` phase) | `STDL_VLine`, `STDL_Line` | `STDL_Circle` outline |
| `STDL_BlitTile` (16px aligned) | masked blits (colour key) | any per-pixel loop |
| pre-shifted `STDL_BlitSprite` | `STDL_SetColourKey` (rebuilds mask) | `SDL_MapRGB` per frame |
| `STDL_BlitIndexed8` (chunky frames at draw time) | | per-frame `STDL_SurfaceFromIndexed8` |
| `STDL_BlitSurfaceEx` (baked frame, priority plane) | | hand-rolled planar sprite loops |
| `STDL_DrawChar` (one glyph) | `STDL_SurfaceFromIndexed8` (load time) | `STDL_RemapSurface` per frame |

**Bake frames you redraw.** The same frame, colour bank and palette
always produce the same plane words. Clear a scratch mask to all
ones, blit the chunky source into it with `STDL_BlitIndexed8` and no
`STDL_I8_MARK` (the default maintenance clears a bit under each
drawn pixel, leaving the source convention), then compose it with
`STDL_BlitSurfaceEx`. Three things a port measured the hard
way: baking costs about what one chunky draw costs, so bake on
*second* sighting or one-off frames pay for bakes they never reuse;
key the cache on something stable, not on decoded-frame addresses
that move every animation lap; and give the block a group of slack
at both ends, because the unaligned path reads one group either
side of a row. Measure it - in one port the win was ~9% on a quiet
screen and near zero on a busy one.

**Check the colour count first.** If the game uses 4 or 8 colours,
call `STDL_SetPlaneBudget(2)` (or `3`) right after
`STDL_SetVideoMode`: it is a promise that no index `>= 2^N` is ever
drawn, and every primitive then stops maintaining the higher
bitplanes - roughly `N/4` of the memory traffic, for one line of
code. Measured on a plain 8MHz ST, budget 2 makes fills and keyed
blits ~1.4x faster; with a BLiTTER ~1.9x. Caveats: the budget is
global (there is no per-surface override); colours above it are
truncated to the low N bits, so a stray index 7 draws as 3; and any
surface already holding out-of-budget colours when you lower it
will render wrong. Grep the port for every colour literal before
choosing N - the maximum index used decides it, not the palette.

Pre-shifted sprites (`STDL_PRESHIFT`) cost 16x RAM and make
unaligned blits as cheap as aligned ones. Large same-phase fills
and blits are BLiTTER-accelerated automatically when the hardware
has one (fills hit the 50Hz VBL cap; aligned unmasked blits ~3x,
masked ~1.7x); unaligned blits stay on the CPU shift chain
(160x100 ~50ms, masked ~100ms on emulated Mega STE), so align to
16px or pre-shift to benefit. `STDL_UseBlitter(0)` forces CPU
paths for debugging; BLITCHK.TOS verifies both paths on target.

## Porting workflow

1. `python3 tools/stdlconv/stdlconv.py bmp16 in.png OUT.BMP
   --st-palette [--colors N] [--keycolor RRGGBB]` for each image;
   `--keycolor` pins the exact colour key at index 15 and keeps it
   out of the quantiser. Use `stdlconv bank` for sprite/tile/font
   banks (chunk spec syntax in the tool's docstring),
   `stdlconv wav in.wav OUT.WAV --rate 12517` for sample audio
   (decodes MS ADPCM, resamples to an exact STE DMA rate: 6258/
   12517/25033/50066), and `stdlconv midi in.mid OUT.STM` for
   music (renders MIDI to a YM2149 register stream - 3 square
   voices, last-note priority, drums to noise; expect a chiptune
   cover, not the original mix).
2. Build with `-Iinclude/compat` so `#include "SDL.h"` resolves to
   the shim; link `libstdl.a`. Compile errors = the porting TODO
   list.
3. Mechanical rewrites (see `docs/porting.md`):
   - direct pixel writes -> `SDL_FillRect` bands / `STDL_HLine`
     spans / `STDL_PutPixel` only for tiny overlays
   - 1bpp data -> `STDL_SurfaceFrom1bpp`; byte-per-pixel (chunky)
     art -> `STDL_SurfaceFromIndexed8(bytes, w, h, stride, key)`
     then `STDL_SpriteFromSurface`. Only reach for `STDL_PutGroup` /
     `STDL_PutGroup8` when the source is some format of its own
   - the same art in several colour schemes (team/faction colours,
     damage flashes) -> `STDL_RemapSurface(s, map)` once per
     variant at load, then blit normally. There is no blit-time
     remap and there will not be one: see `docs/limits.md`
   - text drawn a character at a time -> `STDL_DrawChar`, not a
     one-character string through `STDL_DrawText`
   - CGA `XOR` overlays -> `STDL_XorRect` / `STDL_XorVLine` /
     `STDL_XorPixel` (draw twice to erase)
   - a *loop* of single pixels (particles, starfields) -> fill an
     `STDL_Point` array and make one `STDL_Points` call, or
     `STDL_PointsC` with a parallel colour array when the field is
     multi-coloured (do not sort into colour runs to avoid it: the
     sort costs more than the per-point colour does); erase next
     frame with a second call in the background colour
   - a *loop* of short spans (terrain profiles, column fields,
     raycaster walls) -> fill an `STDL_Span` array and make one
     `STDL_XorVSpans` / `STDL_VSpans` / `STDL_XorHSpans` /
     `STDL_HSpans` call: same result, but the clip setup, colour
     dispatch and row-address multiply are paid once for the whole
     list instead of once per span (1.6x on Sopwith's terrain)
   - 256-colour palette logic -> explicit 16-entry budget
     (sprite colours / effect colours / key+UI slices)
   - `SDL_SetPalette(SDL_PHYSPAL)` fades work unchanged on 16 entries
4. GEMDOS: uppercase 8.3 filenames, no argv from the desktop,
   stdout buffered (stderr is not), console prints onto the screen.
5. Test loop: `STCMD_NO_TTY=1 stcmd make` then run in Hatari:
   `hatari --machine megaste --tos <tos.img> path/to/PROG.TOS`
   (Hatari GEMDOS-mounts the containing directory as C:). For
   automation use `--conout 2` (console to stdout), `--cmd-fifo`
   (`hatari-shortcut screenshot`, `hatari-event keypress <ST
   scancode>`, `hatari-event doubleclick`), `--fast-forward on`.
   Verify on plain ST (`--machine st`) - the 8MHz correctness floor.

## Game services (use instead of reinventing)

- Scrolling camera: `STDL_SetSurfaceOrigin(stripe, 0, cam_y)` -
  short stripe as a windowed level, game keeps level coordinates.
- Composition: `STDL_TRANSPARENT` fills punch holes in masked
  surfaces; blits maintain destination masks; `STDL_SurfaceIsOpaque`
  (cached) skips backdrop under solid tiles; `STDL_PutGroup` for
  runtime asset decoders; `STDL_CreateMask` for built-up surfaces.
- Sound effects: `STDL_SpeakerOn/Off` = PC-speaker idiom on voice A;
  `STDL_PlaySfx` = step sequences (tone or noise) on auto voices.
  Both steal voices from STDL_Music and hand them back restored.
  These are free: they run off the 50Hz VBL sound tick.
- Engine-owned framebuffers: `STDL_CreateSurfaceFrom(pixels, w, h,
  stride, mask, maskstride)` wraps caller-owned planar blocks (and an
  optional mask plane) as surfaces - pointer-swappable, memcpy-able,
  still drawable by every primitive. `STDL_BlitIndexed8` draws
  runtime-decoded chunky sprite frames straight into one, through a
  per-call 16-entry colour map, with flip/column-major variants and
  destination-mask composition (`STDL_I8_UNDER` = pass behind marked
  foreground, `STDL_I8_MARK` = mark what you draw).
- Priority planes for surface blits: `STDL_BlitSurfaceEx(src,
  srcrect, dst, dstrect, flags)` is `STDL_BlitSurface` plus
  `STDL_BLIT_UNDER` / `STDL_BLIT_MARK`, the same two flags at the
  same bit values as the indexed path. Use it to compose frames you
  baked once (see "Bake frames you redraw" above) without writing a
  planar blit of your own. `flags == 0` is exactly
  `STDL_BlitSurface`, BLiTTER fast paths included.
- Frame pacing and profiling: `STDL_GetHz200()` is the raw 200Hz
  system counter, for finer grain than `STDL_GetTicks` and for
  pacing in ticks. It counts from boot, so take differences - a
  per-phase timer around the frame is how one port found
  that an icon was being re-decoded every frame, 18ms of a 93ms
  frame nobody would have guessed at.
- Sample music: `STDL_OpenVoices(rate)` is a fixed-function 4-voice
  sample mixer (Paula-style loop/period/volume) driven from the VBL
  with a 50Hz sequencer hook (`STDL_SetVoiceTick`) - module music
  without the ring device's callback cost. STE only; voices, the
  ring device and `STDL_PlaySample` are mutually exclusive DMA
  owners.
- Sample effects: `STDL_PlaySample(buf, bytes, rate)` points the STE
  DMA at your buffer and the hardware reads it once - no ring, no
  refill, **no per-frame cost** (`STDL_PlaySampleLoop` for ambient
  loops). Stop before freeing a playing buffer - the DMA reads it
  live. Monophonic, so arbitrate by
  priority yourself. Prefer it to `STDL_OpenAudio`/`Mix_PlayChannel`
  for game effects: those are mixing devices, and software-mixing
  four channels at 6258Hz measured **36-75% of an 8MHz STE** (still
  25-43% on a 16MHz Mega STE) in Koules - the same effects through
  `STDL_PlaySample` measured 0%. Reach for the ring device only when
  you genuinely need a continuous mixed stream. Feed it signed 8-bit
  mono at an exact DMA rate (`stdlconv wav --rate 6258`); the WAVs
  `stdlconv` writes are unsigned, so flip the sign bit once at load.
- A steady 50Hz tick: `STDL_AddVBL(fn)` / `STDL_RemoveVBL(fn)` claim
  a TOS VBL queue slot. This is the only interrupt STDL hands out and
  the only thing that should ever have one - a sequencer step whose
  timing must not follow the frame. Never poke `_vblqueue` at $456 by
  hand: STDL removes its callbacks on every exit path including the
  ones that skip `atexit`, and a live queue entry pointing into freed
  memory panics the machine. The callback contract (no GEMDOS, no
  allocation, no drawing, no YM writes while STDL owns the chip) is
  in `include/stdl/stdl_vbl.h`.
- Splash: `STDL_ShowDegas("SPLASH.PI1")` (make with `stdlconv pi1`).
- Keyboard games + joystick: `STDL_JoyKeyEmulation(1)`, rebindable
  with `STDL_JoyKeyMapping(up, down, left, right, fire)` keysyms
  (default arrows + left Alt; `STDL_KMOD_JOYSTICK` tagged; check
  the return value = keys resolved).

## Anti-patterns (reject these in review)

- Introducing a chunky buffer + conversion pass: that is c2p and
  breaks the design. Redesign around spans/tiles/sprites.
- `STDL_PutPixel` (or `screen->pixels[...]` arithmetic) inside a
  frame loop.
- Assuming `screen->pitch == screen->w` or byte-per-pixel access.
- Assuming unaligned blits are free; align to 16 or pre-shift.
- Calling `STDL_SetColourKey` or `STDL_RemapSurface` per frame (both
  walk the whole surface; they are load-time calls).
- Installing a VBL handler by writing `_vblqueue` ($456) directly
  instead of `STDL_AddVBL`, or doing real work inside one.
- Relying on non-goals: alpha, scaling, threads, >16 colours,
  arbitrary bpp (`docs/limits.md` is the authority).
- Assuming DMA audio is free because "the hardware plays it":
  true of `STDL_PlaySample`, false of the ring device, whose
  callback and resample run on the CPU inside the pump.
- Lowercase or long filenames in `fopen`/`STDL_LoadBMP`.

## Reference

- `docs/format.md` - byte-level surface/sprite/bank contract
- `docs/porting.md` - SDL->STDL mapping table, rewrite patterns
- `docs/limits.md` - non-goals; do not work around them, redesign
- `examples/` - ported SDL 1.2 test programs; `testsprite.c` is the
  reference for the masked-blit + dirty-rect flow, `testpalette.c`
  for the 16-colour palette budget technique, and `chunky.c` for the
  engine-porting APIs (CreateSurfaceFrom's two-views-of-one-block
  pattern, BlitIndexed8 with MARK/UNDER/flips, the STDL_Voice
  sequencer tick, GetHz200)
