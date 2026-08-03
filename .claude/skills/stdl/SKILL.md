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
| `STDL_XorVLine` (short spans) | `STDL_XorRect` (CPU only, no BLiTTER) | `STDL_XorPixel` in a loop |
| aligned blits (same `x & 15` phase) | `STDL_VLine`, `STDL_Line` | `STDL_Circle` outline |
| `STDL_BlitTile` (16px aligned) | masked blits (colour key) | any per-pixel loop |
| pre-shifted `STDL_BlitSprite` | `STDL_SetColourKey` (rebuilds mask) | `SDL_MapRGB` per frame |

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
   - 1bpp data -> `STDL_SurfaceFrom1bpp`; paletted byte-per-pixel
     decoders -> `STDL_PutGroup8` then `STDL_SpriteFromSurface`
   - CGA `XOR` overlays -> `STDL_XorRect` / `STDL_XorVLine` /
     `STDL_XorPixel` (draw twice to erase)
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
- Calling `STDL_SetColourKey` per frame (it scans the surface).
- Relying on non-goals: alpha, scaling, threads, >16 colours,
  arbitrary bpp (`docs/limits.md` is the authority).
- Lowercase or long filenames in `fopen`/`STDL_LoadBMP`.

## Reference

- `docs/format.md` - byte-level surface/sprite/bank contract
- `docs/porting.md` - SDL->STDL mapping table, rewrite patterns
- `docs/limits.md` - non-goals; do not work around them, redesign
- `examples/` - ported SDL 1.2 test programs; `testsprite.c` is the
  reference for the masked-blit + dirty-rect flow, `testpalette.c`
  for the 16-colour palette budget technique
