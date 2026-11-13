---
name: stdl
description: Write Atari ST games and port SDL 1.2 games with STDL, the planar-native SDL-style library. Use when writing STDL code or porting SDL 1.x code for the Atari ST/STE/Mega STE (with 256 colours and 320x240 on a TT or Falcon where the game asks), converting game assets to ST planar format, or debugging planar rendering. Covers the surface format contract, API costs, SDL-to-STDL rewrite patterns, the responsive screens, stdlconv usage and the Hatari test loop.
---

# STDL (Atari ST DirectMedia Layer) - writing and porting games for the Atari ST family

STDL is a planar-native library for writing ST games and porting
SDL 1.2 ones: its API a deliberately incomplete subset of SDL 1.2's.
There is no chunky backbuffer and no c2p pass anywhere: assets are
converted to ST interleaved planar offline, and spans/rects/blits
are the primary verbs. The screen is 320x200, 4 planes, 16 colours
on every machine, unless a game asks for more where a machine has
it (a TT or a Falcon, below).

**What to target.** A plain 8MHz ST with 1MB is the machine a port
has to run on: 68000, no blitter, no DMA audio, 512K-1M of RAM for
code and heap together. Everything above that is progressive
enhancement - use the blitter, DMA sample playback, hardware
scrolling and the Mega STE's 16MHz where they are there, and fall
back cleanly where they are not. `STDL_UseBlitter`, `STDL_HasHwScroll`
and the STE audio calls all report what the machine has, so the
fallback is a branch at init, not a second build.

**Link libcmini, not mintlib, unless the game needs mintlib.** That
is the default for every port: mintlib costs about 100K of the
512K-1M a port has to live in, and buys nothing a game that only
uses stdio, stdlib, string and the GEMDOS/XBIOS bindings calls.
Porting workflow step 2 has the build and the checks.

Requiring an STE is allowed but it is a decision, not a default:
hardware scrolling needs one, and a port built around it should say
so to the person who asked for the port before that design is
locked in. Requiring 16MHz to reach a playable frame rate is never
the answer - if a port only runs on a Mega STE, the rendering needs
fixing, not the target raising. Measure on `MACHINE=st` before
believing any frame-rate claim.

**On a TT or a Falcon an ST game runs as it is**, as an STE in ST
Low: the display rate is measured there (music keeps its tempo at
their 60Hz), a Falcon plays 6258Hz sound at 12517 (`STDL_PlaySample`
from a copy), its crossbar is routed and put back, the desktop's mode
and colours come back at exit, and borders, hardware scrolling and
rasters are refused. A 68030 also has hardware 32-bit multiply and
divide, so most of the cost notes below do not apply there.

**Making an ST game better on a TT or a Falcon?** Ask for each thing
by name, once, before the first `STDL_SetVideoMode`
(`stdl_responsive.h`), and nothing changes for a game that does not:
- `STDL_RESPONSIVE_COLOUR`, with a bpp of 8: 256 colours on a TT or a
  Falcon, its surfaces eight planes in the display's own layout; an
  ST's 16 colours on an ST.
- `STDL_RESPONSIVE_HEIGHT`, with a height of 240: 240 rows on a TT or
  a Falcon, an ST's own 200 there; `STDL_RESPONSIVE_TALL` opens an
  ST's bottom border for 240 rows on every machine.
- `STDL_RESPONSIVE_BANKS`, with the colour: the bank loaders give an
  id's 256-colour art on an 8-plane screen and the ST's chunk
  elsewhere, from one bank - its own art and palette (`stdlconv bank`'s
  `depth=8`, the ST's chunk of the same id `map=PAL`), or planes 4-7
  over the ST's art (`depth=hi` and `low`, colours 16 families of 16),
  for surfaces, sprites, tilesets and palettes. 2.1K, linked by this
  option alone (`docs/format.md` section 6.1).

Then branch on what the screen says - `format->BitsPerPixel` and `h` -
never on the machine. Keep colours 0-15 the ST's palette, so text and
the HUD look the same everywhere, and put the extra shades above them.
What it costs: the colour's code is about 25K of program (on a 512K ST
link libcmini - one port kept 107K free with it); a TT copies what
changed to its doubled lines at every Flip (one port: 4.1ms a frame
with the colour), and its tall 16-colour screen compares the whole
page instead (6.3-7.5ms, more than the colour). An 8-plane surface
does not take a blit, sprite or tile onto a 4-plane one, patterned
polygons or the cursor (each sets the error and draws nothing);
tilesets, `STDL_TRANSPARENT`, the blit flags (UNDER, MARK) and
`STDL_CreateSurfaceFrom8Planes` over a program's own 8-plane block
work as on 4 planes, and `STDL_TRIPLEBUF` gives a third page there
too (153.6K of ST RAM on a TT).

**A TT- or Falcon-only SDL2 port?** Use STDL's SDL2 add-on
(`addons/sdl2/`, `docs/sdl2.md`), and the `stdl-sdl2` skill
(`.claude/skills/stdl-sdl2/SKILL.md`), which covers it: SDL2's window,
renderer and INDEX8 textures on the 256-colour responsive screen, with
its input, surfaces, streams, log and audio. It is the one chunky part
of STDL, kept apart from the core.

**A TT- or Falcon-only SDL 1.2 port?** Use Atari SDL 1.2 (plain TOS, no
MiNT): those machines have 256 colours, and TT line-doubles 320x240 so
one binary covers both. Their 256-colour modes are eight interleaved
planes, so SDL converts its chunky surface at every update; and one
port measured, in Hatari, that SDL's doubled TT mode puts an
`SDL_UpdateRects` box that does not start at the top on line y rather
than 2y (whole-screen `SDL_Flip` is right). STDL's input needs no STDL
screen there: `STDL_OpenInput` takes the keyboard, mouse, joystick and
pad under a mode the program set itself. On an ST there is no chunky
mode at all: SDL degrades to greyscale derived from luminance and c2ps
every frame, which is unusably slow past the basics - which is why
STDL exists.

**Changing STDL itself?** A port often wants a primitive the
library does not have yet, and adding it there beats hand-rolling
one in the game. Read `AGENTS.md` in the STDL repo first: it
carries the build and test loop, the size budget, the asm-with-a-
C-twin rule, and what shipping a change means - a public API is not
done until `docs/` and this skill describe it, and lessons are
written without naming the port they came from.

## Porting? First, does the program fit?

Read this before editing anything: each row below is found out
otherwise only after the port has started. A program fits when it
already thinks in a small palette at a low resolution and draws by
copying images; every row it hits is either a redesign to agree with
the person who asked for the port, or work to plan for.
`docs/limits.md` has the reasons behind each.

| Program uses | On STDL |
|---|---|
| more than 320x200 (640x480, 640x400, a resizable window) | the screen is 320x200 on every ST: redesign the layout for it (a camera over a bigger world, art halved). 240 rows are an option: on every machine with an ST's bottom border open (`STDL_RESPONSIVE_TALL`, `docs/overscan.md`), or only on a TT or a Falcon (`STDL_RESPONSIVE_HEIGHT`, `docs/responsive.md`) |
| more than 16 colours at once (8-bit palettes, palette logic over 256 entries) | 16 at a time from the ST's 512 (an STE's 4096): quantise the art offline (`stdlconv bmp16 --st-palette`) and give the palette an explicit budget (`docs/porting.md`, "Rewrite patterns"). 256 colours only on a TT or a Falcon, as an option (`docs/responsive.md`) |
| 16- or 32-bit colour, alpha blending, colour modulation | not there: no per-pixel alpha, no blending, no blit-time remap. Convert the art to 16 colours; do effects with the palette, and recoloured copies with `STDL_RemapSurface` at load |
| drawing pixel by pixel into a frame buffer (software 3D, raycasters, plasma, pixel effects) | slow in planar, and there is no chunky buffer: rewrite as fills, spans, polygons and point lists (Porting workflow, step 3). 250 points cost 11ms of a 20ms frame on a plain ST, so a full-screen per-pixel effect is a redesign |
| rotation and scaling at run time (SDL_gfx, rotozoom) | not there: pre-render the frames at build time |
| SDL_image (PNG, JPEG, TGA) | not there: convert offline with `stdlconv` - `bmp16` for an image `SDL_LoadBMP` reads, `bank` for sprites, tiles and fonts, `embed` to link a file into the program |
| SDL_ttf | not there: bitmap fonts from a bank, drawn by `STDL_DrawText` (cells at most 16 pixels wide) |
| SDL_net, sockets | not there: no networking. Network play is cut, or kept for a mintlib build on a machine running MiNT and a network stack, outside STDL |
| SDL_mixer | the shim (`include/compat/SDL_mixer.h`) plays `Mix_` chunks on STDL's voices (DMA sound: an STE or later) and `Mix_PlayMusic` as YM music (`stdlconv midi`, or `opm` for YM2151 logs). MOD, OGG and MP3 music have no path: re-score via MIDI. A plain ST has no sample playback: effects go silent there, or become YM effects (`STDL_PlaySfx`) |
| threads, `SDL_AddTimer` | no threads; timer callbacks run only inside `SDL_Delay`. Drive the game from its main loop and `STDL_GetTicks` |
| OpenGL | not there |
| two joysticks, or analogue sticks and hats | one digital stick in the joystick port with one button (`STDL_GetJoystickState(0)`) until the program asks for more: `STDL_SetNumJoysticks(2)` makes the mouse-port stick joystick 1 and turns the mouse off while there are two, so two players cannot have the mouse too; more are Xpad pads, where a provider has them - up to four joysticks with the pads merged into the ports' (the default), six apart (`STDL_UseJoystickMerge(0)`). Analogue sticks, triggers, a hat and 13 buttons come only from an Xpad pad (`STDL_HasPad`) |
| floating point in the game loop | an ST has no FPU: every `float` operation is a library call. Convert hot code to fixed point (`stdl_math.h`, "API cost annotations") |
| more than about 400K of code, data and heap | a plain ST has 512K-1M for everything. Link libcmini (about 100K smaller), load levels from disk rather than all at start-up, and prove the fit at `--memsize 0` and `1` in Hatari ("Testing in Hatari") |
| 60 frames a second, or timing tied to the frame rate | an ST's display is 50Hz (PAL) or 60Hz, and an 8MHz 68000 is the floor: budget a frame on `MACHINE=st`, and pace the game from the clock rather than the frame count |
| long or lower-case file names, `argv` | GEMDOS: upper-case 8.3 names, no arguments from the desktop (Porting workflow, step 4) |

A program that fits few of these is better written again for the ST
than ported: "Writing a new game" below.

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
| `STDL_FillRect`, `STDL_HLine`, `STDL_VLine` | `STDL_BlitSurface` unaligned (shift chain) | `STDL_PutPixel` / `STDL_GetPixel` |
| `STDL_XorVSpans` / `STDL_VSpans` (span lists) | `STDL_XorRect` (CPU only, no BLiTTER) | `STDL_XorPixel` in a loop |
| `STDL_Points` / `STDL_PointsC` (particle fields) | | `STDL_PutPixel` in a loop |
| `STDL_XorVLine` (a few spans) | `STDL_XorVLine` x100+ (batch it instead) | one `STDL_*Line` call per column |
| aligned blits (same `x & 15` phase) | `STDL_Line`, `STDL_Circle` (point batches underneath) | |
| `STDL_BlitTile` (16px aligned) | masked blits (colour key) | any per-pixel loop |
| pre-shifted `STDL_BlitSprite` | `STDL_SetColourKey` (rebuilds mask) | `SDL_MapRGB` per frame |
| `STDL_BlitIndexed8` (chunky frames at draw time) | | per-frame `STDL_SurfaceFromIndexed8` |
| `STDL_BlitSurfaceEx` (baked frame, priority plane) | | hand-rolled planar sprite loops |
| `STDL_DrawChar` (one glyph) | `STDL_SurfaceFromIndexed8` (load time) | `STDL_RemapSurface` per frame |
| `STDL_SetScrollOrigin` (STE, once a frame) | | copying a play field to scroll it on an STE |

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

**Restore a whole background with `NULL`.**
`STDL_BlitSurface(bg, NULL, dst, NULL)` between two surfaces of the
same size - no colour key, no masks, no clipping, a width that is a
multiple of 16 - is one `memcpy` on a machine without a BLiTTER:
19.3ms for 320x200 on a plain 8MHz ST (Hatari, cycle-exact). A
full-size rectangle in place of `NULL`, or `STDL_BlitSurfaceEx`,
takes the row path, which copies rows of whole groups with `movem`,
all of them in one call: 20.7ms (42 before November 2026, a `memcpy`
a row). With a BLiTTER, the BLiTTER does it faster still (17ms on an
STE).

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

Many copies of one sprite - bobs, particles, a wave of enemies -
go in one `STDL_BlitSprites(spr, list, n, dst)` call, a list of
`STDL_SpriteAt {x, y, frame}`. Each one wholly inside the clip skips
`STDL_BlitSprite`'s per-call set-up; anything else is drawn by it.
Measured on a plain ST (SPRCOST): fourteen pre-shifted 16x16 bobs
13.1ms one call each, 11.1ms batched; the same on an emulated STE,
where sprites that short stay on the CPU (below).

Pre-shifted sprites (`STDL_PRESHIFT`) cost 16x RAM and make
unaligned sprites as cheap as aligned ones - on a plain ST. With a
BLiTTER, `STDL_BlitSprite` hands sprites to the chip, which shifts
them itself: unshifted sprites averaged over positions measured
2.4-3.5x faster on an emulated STE (a 32x32 6.5ms -> 1.9ms). Aligned
ones (pre-shifted, or unshifted on a group edge) under 32 rows stay
on the CPU, which beats the chip's eight passes there whatever the
width; from 32 rows they cost what unshifted ones do. So there
pre-shifting mostly spends RAM (a 32x32 sprite is 15KB pre-shifted,
640 bytes not): it buys short sprites 11% on an STE (16x16 0.96ms
against 1.08) and a third on a Mega STE. A port that
must run on a plain ST and fit in its RAM can decide at init:
pre-shift only when `STDL_GetMachineInfo()->has_blitter` is 0. With
a border open, pre-shifted sprites stay on the CPU, where they are
faster. All of this assumes `STDL_DOUBLEBUF`: onto the displayed
screen of a single-buffered program, keyed and shifted blits and
sprites stay on the CPU, because the chip draws them in passes over
the whole rectangle (a keyed blit XORs, ANDs and XORs every plane)
and the beam shows the rectangle between them - on a real Mega STE
sprites flashed white, tore and vanished. Single-buffered, pre-shift
as for a plain ST. Fills and blits are
BLiTTER-accelerated automatically when the hardware has one and the
operation is big enough to repay its set-up - unaligned blits too,
which the BLiTTER shifts itself: on an emulated STE a 64x32
unaligned blit went from 7.4ms to 1.4ms plain and 12.4 to 3.9ms
keyed, and a 16x16 from 2.2 to 1.1ms. Small aligned copies stay on
the CPU, which now beats the BLiTTER for them (every copy up to
128x16 on an STE). On a plain ST the unaligned shift chain is the
path, so align to 16px or pre-shift there. `STDL_UseBlitter(0)`
forces CPU paths for debugging; BLITCHK.TOS verifies both paths on
target.

**Your own arithmetic: 16 bits, or a library call.** The 68000
multiplies 16 by 16 and divides 32 by 16. gcc 4.6 uses those
instructions only when it can see, where it chooses, that both
operands are 16-bit values of the same signedness; anything else
becomes a call to `__mulsi3` (about 350 cycles on a plain ST against
40 for `muls.w`) or `__divsi3` (630 against 140). It is silent and
it moves with the surrounding code, so ports keep finding it with
the profiler, one site at a time. Measured with gcc 4.6.4 at `-O2`:

- `int * int` is always a call. So are mixed widths:
  `uint8_t * uint8_t`, `int16_t * int`, `int16_t * uint16_t`.
- In a loop, a value derived from the counter loses its 16-bit cast.
  The induction-variable pass rewrites it as a 32-bit variable, so
  `(int32_t)(int16_t)dx * (int16_t)dx` with `dx = x - 80` is a call
  inside the loop and `muls.w` outside it. Declaring `dx` as
  `int16_t` does not help. One demo's plasma precalc made 40,860 of
  these calls in eight seconds of loading.
- At `-O1` every product is a call: the widening pass runs only at
  `-O2`/`-Os`.
- Division by anything but a power of two is a call, constants
  included (`x / 12` on an `int`), and so is `%` (`text[i % len]` in
  a scroller). A `short` divided by a constant is the exception: it
  becomes a `muls.w` by the reciprocal.
- Subtracting pointers to a struct whose size is not a power of two
  divides: `q - spans` for 6-byte spans multiplies by 0xAAAAAAAB.

Use `<stdl/stdl_math.h>` (in `stdl.h`) on anything that runs per
pixel, per row, per object or per frame. `STDL_MulS16`/`STDL_MulU16`
(16x16->32), `STDL_DivS16`/`STDL_DivU16` (32/16, quotient must fit)
and `STDL_FixDiv` (`(a << 16) / b` for a 16.16 slope, about 330
cycles against 630) are the instruction whatever surrounds them.
`STDL_FixMul(a, b)` is the 16.16 product,
`(int32_t)(((int64_t)a * b) >> 16)` bit for bit (rounding toward
minus infinity, as PICO-8 does), from four `mulu.w`: 324 cycles on a plain ST
against 1404 for that C, a `__muldi3` call (MATHCHK).
Products with a constant can stay plain C (`y * 160` compiles to
shifts and adds for an `int`, `muls.w #160` for an `int16_t`).
Precompute a reciprocal or a table instead of
dividing per element, and step an index instead of taking `%`.

Then make it a build failure rather than a profiling discovery.
`tools/libcalls.sh` in the STDL repo lists every libgcc arithmetic
call an object makes, per function, and with `-a` fails on any that
an allowlist does not name. The library checks its own build this
way (`tests/libcalls.allow`). A port's Makefile can do the same:

```make
libcallcheck: $(OBJS)
	sh $(STDL)/tools/libcalls.sh -a libcalls.allow $(OBJS)
```

Run it without `-a` first to see what is there today. Allow the
load-time calls with a reason each, fix the per-frame ones, and the
next one stops the build. Soft float (`__mulsf3`, `__floatsisf`,
`__fixdfsi`...) shows up in the same list.

## Writing a new game

Start from STDL's starter rather than an empty file:
`examples/starter.c` (STARTER.TOS) is a one-screen platformer that
holds 50 frames a second on a plain ST in Hatari, built the way the rest
of this skill says to build things. From an empty directory:

1. **The repository.** `git init`, then STDL as a submodule at
   `extern/stdl` pinned to a release tag that has the starter (v1.23.0
   or later), and the ST Makefile: `docs/building.md`, "Setting up a
   game or a fork", has both, and both have been run. A new game uses
   STDL's own API, so leave `-I$(STDL)/include/compat` out of that
   Makefile's `CFLAGS` (and `-DPORT_STDL`, which only a fork needs)
   and include `<stdl/stdl.h>`. Ignore what the build and the tests
   make, before the first `git add`: `printf 'build-atari/\ndist/\ntmp/\n'
   > .gitignore`. `tmp/` is where the TOS image goes, which is not the
   game's to ship; `dist/` holds only what the Makefile makes.
2. **The starter.** Copy `extern/stdl/examples/starter.c` to
   `src/game.c`, its art (`examples/art/*.png`) to `art/`, and its
   music (`examples/assets/DEMO.MID`) to `music/`. Then paste the art
   rule below into the Makefile as it is, after its `all` rule so that
   `all` stays the default (it is STDL's `starter-art` rule with the
   paths already changed, writing into `dist/` beside the program;
   recipe lines start with a tab). `make -f Makefile.atari art` runs it on the host,
   which needs Python 3 with Pillow (the toolchain container has no
   Pillow); then `stcmd make -f Makefile.atari` builds the program.
   Rename the files as the game takes shape: the program names them
   in its `BANK` and `MUSIC` defines.
3. **Run it** in Hatari, `MACHINE=st` first. "Testing in Hatari"
   below has the emulator and the TOS image (into `tmp/tos/`); then
   this plays it at real speed (`FF=off`), jumps, runs right, takes
   two screenshots and quits:

   ```
   SDL_VIDEODRIVER=dummy MACHINE=st FF=off TOS=tmp/tos/etos256uk.img \
     EXTRA="--cpu-exact on --compatible on" \
     extern/stdl/tests/hatari/run.sh smoke dist/GAME.TOS 8 \
     "waitfor STARTER:ready;sleep 1;keydown 0x48;sleep 0.3;keyup 0x48;sleep 0.3;shot;keydown 0x4d;sleep 1;keyup 0x4d;shot;keydown 0x01;sleep 0.3;keyup 0x01;waitfor score="
   ```

   `STARTER:ready` is the marker the program prints once it is
   running, and `score=` is in its exit line, which also gives the
   frames drawn and the milliseconds they took: keep that line, and
   the frame rate is measured every run. The screenshots land in
   `extern/stdl/tests/hatari/out/shots_smoke/`, beside the driver.

```make
STDLCONV = python3 $(STDL)/tools/stdlconv/stdlconv.py
art: dist/STARTER.BNK dist/DEMO.STM
dist/STARTER.BNK: art/tiles.png art/hero.png art/font.png
	@mkdir -p dist
	$(STDLCONV) bank $@ palette:0:art/tiles.png \
	    tileset:1:art/tiles.png:tile=16x16 \
	    sprite:2:art/hero.png:framew=16:key=0 \
	    font:3:art/font.png:cell=8x8:first=32
dist/DEMO.STM: music/DEMO.MID
	@mkdir -p dist
	$(STDLCONV) midi $< $@
.PHONY: art
```

What the starter's shape is, and why (its header says the same):
- **The map is drawn once**, into a background surface, and each frame
  puts back only what the frame before it on that page drew
  (`STDL_DirtyRestore`, then `STDL_DirtyPush` for each sprite drawn).
  Redrawing a screen of 16x16 tiles costs 97.9ms on a plain ST, five
  frames (SCRLCOST); putting back a 16x16 sprite's box moves 128 bytes,
  or 256 off a 16-pixel group.
- **What changes rarely goes into the background**, and
  `STDL_DirtyBackground(&rect)` puts it on each page as the page comes
  round (a coin taken, the score; `NULL` for a new level), rather than
  it being drawn on every frame.
- **Three pages** (`STDL_TRIPLEBUF`; it falls back to two, so test the
  flags): one frame is drawn while the last is still showing.
- **A fixed step from the 200Hz clock** (`STDL_GetHz200`, four ticks a
  step), whatever the frame rate, with a cap on how many steps one
  frame may catch up after a stall.
- **Input as keys**: `STDL_JoystickKeyEmulation(1)` makes the joystick the
  cursor keys and left Alt, and `STDL_GetKeyState` reads both at once.
- **Events drained once a frame**: `while (STDL_PollEvent(&e))` pumps
  once a VBL however many it takes (the IKBD's ring, the pads, key
  repeat, the audio refill), and serves only the timers and audio
  between; a drain of 16 events was 17 pumps and 6.4ms of a plain ST's
  frame before November 2026, 2.8ms now, and 18.9 against 3.0 with four
  pads. A loop that polls between frames sees an event up to a VBL
  later; `STDL_PumpEvents` always pumps.
- **Sound on the YM**, which every ST has: music from MIDI
  (`stdlconv midi`), effects as `STDL_Sfx` tables of
  `STDL_YM_PERIOD(hz)`. DMA samples (`STDL_PlaySample`, `STDL_Voice`)
  are an STE's extra, with the YM as the fallback.
- **Pre-shifted sprites only without a BLiTTER** (`has_blitter` in
  `STDL_GetMachineInfo`): the chip shifts them itself.

**The art pipeline.** Draw the art as indexed PNGs that share one
16-colour palette, its colours on the ST's 3-bit grid (each channel 0,
36, 73, 109, 146, 182, 219 or 255), colour 0 the background and the
sprites' key. `stdlconv bank` keeps an indexed image's indices as they
are, so every chunk agrees with the palette chunk; art that is not
indexed is quantised image by image, and two images then number their
colours differently. Convert art from elsewhere as one sheet, through
`stdlconv bmp16 --st-palette` (one palette), then cut it into the
bank's images with a tool that keeps the indices. A font
is a strip of cells, colour 0 the paper and anything else ink. For a
game that also wants 256 colours on a TT or a Falcon, the same bank
carries both (`depth=8` and `map=PAL`, `docs/responsive.md`).

**Tile maps.** STDL draws a tile (`STDL_BlitTile`, at an x on a
16-pixel group, clipped to the surface's clip rectangle) and has no map
format and no level-editor import: keep the map as the game's own array
or file, as the starter does, and draw it into the background when a
level or a screen starts (20x13 tiles: 97.9ms on a plain ST, 27.0 on a
TT, so never every frame). After that a frame redraws only what
changed: a tile that changes (about 0.38ms on a plain ST), the strip
that scrolls into view (a column of 13 tiles 4.9ms; a strip of 20
tiles 11.8ms 16 rows high, and still 4.4 one row high, as each tile is
a call), and the sprites. A flip-screen game redraws the background
only between screens. A scrolling game is the plain ST's hard case: with
a full-screen playfield a 25Hz game, as copying the window alone takes
a whole 50Hz frame (20.7ms), but one with room for sprites in its two
frames. "Game services", "Scrolling without scroll registers", has
what each way costs.

## Porting workflow

1. `python3 tools/stdlconv/stdlconv.py bmp16 in.png OUT.BMP
   --st-palette [--colors N] [--keycolor RRGGBB]` for each image;
   `--keycolor` pins the exact colour key at index 15 and keeps it
   out of the quantiser. Use `stdlconv bank` for sprite/tile/font
   banks (chunk spec syntax in the tool's docstring),
   `stdlconv wav in.wav OUT.WAV --rate 12517` for sample audio
   (decodes MS ADPCM, resamples to an exact STE DMA rate: 6258/
   12517/25033/50066), and `stdlconv midi in.mid OUT.STM` for
   music (renders General MIDI to a YM2149 register stream - 3
   square voices shared one per instrument before chords, by
   General MIDI program (bass, melody, then pads), with envelopes,
   vibrato and drums as noise or falling-tone bursts; the programs
   shape each part, so keep them in the file; expect a chiptune
   cover, not the original mix). Music that was written for a
   YM2151 (arcade, X68000) and logged as VGM goes through
   `stdlconv opm in.vgm OUT.STM` instead: each FM channel's own
   envelope and pitch, the three loudest parts (two on A and B,
   the third on C with the drums, so `STDL_PlaySfx`'s voice C
   costs the least), `-v` naming the instruments taken for drums.
   Log a driver's output to VGM rather than re-scoring it.
2. Set the fork up first: STDL as a submodule at `extern/stdl` pinned
   to a release tag, and an ST Makefile beside the game's own build,
   which keeps working (`docs/building.md`, "Setting up a game or a
   fork", has the commands and a Makefile that has been run). Build
   with `-Iinclude/compat` so `#include "SDL.h"` resolves to
   the shim, against libcmini, and link `libstdl-cmini.a`. Compile
   errors = the porting TODO list.

   **libcmini is the default; mintlib is the exception, for a game
   that needs something only mintlib has** - MiNT's POSIX extras
   (processes, signals, sockets), or whatever else the libcmini link
   reports missing that is not worth rewriting. Start every port on
   libcmini and move to mintlib (`libstdl.a`) only for a reason you
   can name; do not reach for mintlib because the toolchain links it
   by default. What it buys: about 100K of program. One port's game
   went from 242,624 bytes to 141,551, and on a 512K ST its free
   memory after loading from 23K to 107K - with the TT colour code of
   `stdl_responsive.h` linked in as well. Same frames (a seeded run's
   screen hashes matched on an STE, a TT and a Falcon), same saved
   files byte for byte, and its loading was faster (the art 1.47s to
   1.20s, the sound set-up 535ms to 50ms). The STDL objects must be
   built against libcmini's headers too - `libstdl.a` will not mix
   with it:

       make libstdl-cmini.a
       m68k-atari-mint-gcc ... -I$CMINI/include -c game.c
       m68k-atari-mint-gcc -nostdlib -L$CMINI/lib \
           -Wl,--traditional-format -o GAME.TOS $CMINI/lib/crt0.o \
           objs... libstdl-cmini.a -lcmini -lgcc

   (`CMINI` is `/freemint/libcmini` in the toolchain container.)
   Then:
   - set the stack in the program, `long _stksize = 32768;`:
     libcmini's start-up reads that, not the link's `--stack`;
   - read the link errors: a missing symbol is a mintlib feature the
     game used, and the call to rewrite or keep mintlib for;
   - `malloc` comes from `Mxalloc`, so a free-memory probe with
     `Malloc(-1)` still tells the truth;
   - prove the fit at `--memsize 0` (below); a port moving from
     mintlib compares a seeded run's frames and saved files with the
     mintlib build's before switching for good.
3. Mechanical rewrites (see `docs/porting.md`):
   - direct pixel writes -> `SDL_FillRect` bands / `STDL_HLine`
     spans / `STDL_PutPixel` only for tiny overlays
   - 1bpp data -> `STDL_SurfaceFrom1bpp`; byte-per-pixel (chunky)
     art -> `STDL_SurfaceFromIndexed8(bytes, w, h, stride, key)`
     then `STDL_SpriteFromSurface` (frames side by side, any width:
     a strip of 20-pixel frames needs no padding). Only reach for `STDL_PutGroup` /
     `STDL_PutGroup8` when the source is some format of its own.
     Load-time costs on a plain ST: the conversion about 80 cycles a
     pixel (96 keyed), and `STDL_PRESHIFT` 280ms for one 64x64
     sprite, so a loader converting a screenful of art is counting
     tenths of a second, not seconds
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
   - filled polygons (flat-shaded 3D, glenz, vector fonts) ->
     `STDL_FillPolygon(dst, pts, n, col, op)` for each convex face,
     not a span list built by hand. `op` is `STDL_DRAW_SET`, or
     `STDL_DRAW_XOR`/`STDL_DRAW_OR`/`STDL_DRAW_CLEAR` on the planes
     `col` has, which is how a glenz draws each face into a plane of
     its own. Edges follow the rectangle's rule (top and left
     inside), so faces sharing an edge tile with no seam, which XOR
     needs. Like the other drawing primitives it ignores the surface
     origin. A rectangle's four corners with an op is a plane-masked
     rectangle fill. A face inside the clip's columns, at most 256
     rows tall, SET or XORed onto a surface with no mask, is filled
     entirely in 68000 with both edges in registers; others take
     column tables. XOR/OR/CLEAR write only the colour's planes, so
     a one-plane glenz face is one word a group. Measured on a plain
     ST (PRIMCOST): a 160-row diamond XORed in 12.5ms against 23.9
     through `STDL_XorHSpans`; a 22-row triangle 1.47ms against
     2.26 as spans, 1.18ms in one plane; 24 one-plane glenz-like
     triangles 47ms (85 before this path). Set-up is about 4400
     cycles a polygon, so from those figures only faces under about
     ten rows are cheaper as spans. Triangles take a path of their own
     (sorted vertices, one cross product, three edges, no walk), about
     a thousand cycles cheaper: 2, 8 and 22 rows in 0.50, 0.79 and
     1.51ms, the glenz 44ms, 80 faces of a small mesh (~12 pixels
     each) 48ms against 58 by the general walk; on a Mega STE a 2-row
     triangle 0.26ms and the mesh 25ms. A mesh whose faces index one
     array of projected vertices loses nothing by copying a face's
     three corners into a polygon for the call: an entry taking the
     vertices in place was built, and measured under 1% faster on a
     plain ST and level on a Mega STE. `examples/polydemo.c`
   - dithered faces, fog and screen doors (PICO-8's `fillp`) ->
     `STDL_FillPolygonPattern(dst, pts, n, &pat)`, with `pat` built
     once by `STDL_MakePattern(&pat, col0, col1, bits, transparent)`
     (bits: 4x4, bit 15 top-left, row-major; set bits take col1, or
     are left alone when `transparent`). Vertices are 12.4 fixed point
     (`STDL_PointSub`) and the rule is `STDL_FillPolygon`'s, exact on
     them: shared edges tile, integer vertices << 4 with a solid
     pattern draw the identical pixels, and a face moving by 1/16
     pixel changes only the pixels it crosses. The pattern is anchored
     to the surface, so a shaded mesh dithers as one. Opaque rows store
     whole groups straight from the pattern, so a dither costs what a
     solid colour does. Measured on a plain ST (PRIMCOST): a 2-row
     triangle 0.65ms, 22 rows 1.87ms, a 20-row quad 2.25ms, a
     160-row diamond 14.0ms, against 0.70, 1.72, 1.99 and 12.7ms for
     `STDL_FillPolygon` SET. Triangles take a path of their own; any
     face's set-up is about 680 cycles an edge, and a row about 390
     cycles for one group and 500 for two, so a frame of many small
     faces is set-up and row bound - merge faces where the model
     allows. Transparent rows merge every group (a 160-row diamond
     21.7ms). A face far taller than the view - a road's nearest,
     its ends hundreds of rows off the screen - costs only the rows
     it draws: a quad 800 rows tall cut to 200 by the clip 25.4ms.
     On a masked surface, or for a polygon over 2047 pixels tall or
     wide, it takes the C path, which measured 2.3-4.3x slower than
     the 68000 one. `examples/patdemo.c`
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
5. Test loop: `STCMD_NO_TTY=1 stcmd make`, then run the .TOS in
   Hatari through `tests/hatari/run.sh` - setup (emulator, a TOS
   image), the command language and the pitfalls are in "Testing
   in Hatari" below. Verify on plain ST (`MACHINE=st`) before
   calling anything done - it is the 8MHz correctness floor, and
   the machine the port is for.

## Testing in Hatari

Two things the repository does not ship, both one-time setup: the
emulator and a TOS ROM image. Nothing else is needed - a program's
directory is GEMDOS drive C:, so `dist/` with the .TOS and its
assets is the whole disk.

**Hatari.** The driver defaults to the macOS app bundle at
`/Applications/Hatari.app/Contents/MacOS/hatari` (the official
builds are on framagit.org/hatari/releases; hatari-emu.org links
them). `brew install hatari` on macOS or the distro package on
Linux instead put a `hatari` binary on PATH, in which case set
`HATARI=hatari`. Verified with 2.6.1; any recent 2.x has the
command fifo and `--conout` the driver relies on.

**A TOS image.** EmuTOS is free, redistributable and runs
everything in this library, so download it rather than hunting for
a ROM. The ports keep it under `tmp/tos/` (gitignored - it is a
256K binary and not the port's to ship):

```
mkdir -p tmp/tos
curl -sfL -o tmp/tos/emutos.zip \
  "https://sourceforge.net/projects/emutos/files/emutos/1.4/emutos-256k-1.4.zip/download"
unzip -jo tmp/tos/emutos.zip emutos-256k-1.4/etos256uk.img -d tmp/tos
export TOS=$PWD/tmp/tos/etos256uk.img
```

Releases are on SourceForge, not GitHub (the GitHub releases page
has no assets). Use the 256K build for `st`, `ste` and `megaste`. It
has no TT or Falcon video, NVRAM or second MFP (its readme says so): a
TT or a Falcon takes the 512K build, below. The language suffix only
picks the keyboard layout and the driver injects raw scancodes, so
any of them will do. A real TOS ROM (1.04, 2.06) is copyrighted
and not downloadable; if the maintainer has one, `TOS=` takes it
the same way. Two reasons to want one: EmuTOS's own footprint is
~169KB (screen included), so a 512K fit that fails under EmuTOS at
`--memsize 0` may be fine under the TOS 1.0x a real 520ST runs -
measure before "fixing" the game; and a release gets a run under
the TOS its users will have.

**A TT or a Falcon.** EmuTOS's 512K build, from the same release,
boots both, and every one of STDL's TT and Falcon checks said the same
under it as under TOS 3.06 and 4.04, the ROMs the library was measured
on (STDL's `AGENTS.md` has the list and the differences). So a
256-colour or 240-row screen, or the SDL2 add-on, can be tested
without a ROM:

```
curl -sfL -o tmp/tos/emutos512.zip \
  "https://sourceforge.net/projects/emutos/files/emutos/1.4/emutos-512k-1.4.zip/download"
unzip -jo tmp/tos/emutos512.zip emutos-512k-1.4/etos512uk.img -d tmp/tos
TOS512=$PWD/tmp/tos/etos512uk.img
X="--cpu-exact on --compatible on --memsize 4"
RUN=extern/stdl/tests/hatari/run.sh       # tests/hatari/run.sh in STDL itself

SDL_VIDEODRIVER=dummy TOS=$TOS512 MACHINE=tt EXTRA="$X" \
  $RUN tt dist/GAME.TOS 8 "waitfor ready;shot"
SDL_VIDEODRIVER=dummy TOS=$TOS512 MACHINE=falcon MONITOR=vga EXTRA="$X" \
  $RUN fv dist/GAME.TOS 8 "waitfor ready;shot"
SDL_VIDEODRIVER=dummy TOS=$TOS512 MACHINE=falcon MONITOR=rgb EXTRA="$X" \
  $RUN fr dist/GAME.TOS 8 "waitfor ready;shot"
```

- **The machines.** The driver adds `--mmu off` on a TT and a Falcon
  (an emulated MMU cost a TT check 46-47s of the host's time against
  29-32, and its timing is no truer) and `--fpu none` on a Falcon, as
  a stock one has none; `LATE=` can still change either
  (`LATE="--fpu 68882"`).
- **A Falcon on both monitors.** `MONITOR=vga` and `MONITOR=rgb` give a
  program different modes: an 8-plane screen is mode word 0x0113 on
  VGA, and on RGB 0x0023 at 50Hz or 0x0003 at 60Hz, the desktop's PAL
  or NTSC, with a 384-pixel line and the picture centred. PAL or NTSC
  comes from Hatari's NVRAM (PAL in a fresh configuration). A TT needs
  no `MONITOR`; `MONITOR=mono` gives it a TT High desktop, where STDL
  refuses its screens.
- **TT-RAM:** `EXTRA="--cpu-exact on --compatible on --memsize 2"
  LATE="--ttram 4 --addr24 off"` is 2MB of ST-RAM and 4MB of TT-RAM.
  Without `--addr24 off` Hatari stops at a question before TOS starts,
  which with no window nobody answers: the program never runs.
- **The desktop it starts from.** Hatari's autostart gives TOS 4.04 an
  ST Low desktop, but EmuTOS the NVRAM's mode: 640x480 in 16 colours on
  VGA, 640x400 interlaced on RGB. Name one when it matters (a program
  that must put the desktop back): `EXTRA="$X --tos-res stlow"`, or
  `ttlow`, `ttmed`, `sthigh`; both TOSes then start from the same mode.
- **What EmuTOS does differently**, none of it the library's:
  - `Getrez()` under a VIDEL mode: 2 under 320x480 in 256 colours,
    where TOS 4.04 says 7 (neither describes the mode; STDL does not
    ask it there);
  - console text printed in an 8-bit mode is magenta, where TOS's is
    white, so a screenshot compared across the two TOSes differs
    there;
  - free memory: the largest ST-RAM block on a 4MB TT 3679K against
    TOS 3.06's 3670K, on a Falcon with an ST Low desktop 3798K against
    3762K, and hundreds of K apart from other desktop modes. Measure a
    memory fit under the TOS its users have;
  - timings: on a TT the same program measured up to 10% apart, either
    way, between the two TOSes; on a Falcon they matched. Compare
    timings under one TOS only;
  - its boot takes another time, so a screenshot a fixed time after the
    start catches another moment: shoot after a marker the program
    prints to stderr, or in a burst (`shot;sleep 0.3;shot;...`).
- Hatari's 68030 is fast as well as approximate. A real TT took 1.5
  to 2.3 times as long as Hatari's TT over the same binary (2026-11-11,
  cycle-exact with the data cache on; about twice for calls, for code
  larger than the 256-byte instruction cache and for memory read in
  order), and nobody has timed a real Falcon. So every TT and Falcon
  time here and in `docs/` is Hatari's: compare two builds by it, and
  never decide from it that a frame fits. `tests/hatari/cpuchk.c`
  (CPUCHK) measures the factor on a machine, and
  `tests/hatari/sccmp.py` compares its log with Hatari's. The
  display's rate and Timer B are for a real machine to settle too.

**The driver.** From a port that vendors STDL the path is
`stdl/tests/hatari/run.sh` or `extern/stdl/tests/hatari/run.sh`:

```
TOS=tmp/tos/etos256uk.img EXTRA="--memsize 4" \
  extern/stdl/tests/hatari/run.sh smoke dist/GAME.TOS 8 \
  "waitfor ready;shot;keydown 0x1c;sleep 0.3;keyup 0x1c;sleep 2;shot"
```

The third argument is seconds to wait for EmuTOS to boot and the
program to start (8 is enough with fast-forward on). Then the
;-separated script runs: `sleep N`, `waitfor STR` (polls the
console log), `waitfile STR PATH` (polls a file the program writes
on C:, for output that never reaches the console), `shot`,
`keydown`/`keyup`/`key VAL` (an ST scancode: 0x1c Return, 0x01
Escape, 0x39 space, the cursor keys 0x48 up, 0x50 down, 0x4b left and
0x4d right, 0x38 Alt; or a single alphanumeric),
`text STR`, `click`, and `fifo CMD` for anything else Hatari's
command fifo accepts. Console output lands in
`out/NAME.log` beside the driver (`extern/stdl/tests/hatari/out/`
from a game's repository; STDL ignores it), echoed at the end,
Hatari's own messages in `out/NAME.err`, screenshots in
`out/shots_NAME/`.
Environment: `MACHINE=st|ste|megaste|tt|falcon` (default ste - an
8MHz CPU, so the timing is honest, with the blitter and DMA audio
present to exercise; `st` is the correctness floor and `megaste`
measures what 16MHz adds; `tt` and `falcon` want the 512K image,
above), `MONITOR=vga|rgb|mono` (a Falcon's monitor), `FF=off`
for screenshots that must be at real speed, `SOUND=on` with
`fifo hatari-shortcut recsound` to record audio, `EXTRA=` for any
other Hatari option, and `LATE=` for one `--machine` would reset
(`--fpu 68882`), which goes after it. Interactively, the same run is
`hatari --tos $TOS --machine ste --memsize 4 dist/GAME.TOS`.

Pitfalls, each of which has cost a day:

- **`key VAL` is press and release in the same pump.** Any game
  that polls key *state* rather than latching a press edge never
  sees it and looks like it has no keyboard input at all. Always
  `keydown VAL;sleep 0.3;keyup VAL`.
- **A black screenshot is usually an early screenshot**, not a
  crash. The driver boots with `--fast-boot on`, and with `FF=off`
  EmuTOS still takes a few seconds before the program's first
  frame (about 4 for a small program on an ST). `waitfor` a marker the
  program prints to stderr (immediate; stdout is line-buffered and
  arrives late) and shoot after that. Markers also print on the ST
  screen, so put them before the video mode is set or expect them
  in the shot.
- **Fast-forward is safe for the program's clock and unsafe for
  the script's.** The 200Hz counter is emulated time, so a
  benchmark timed on it reads the same either way (measured, both
  `FF=on` and `FF=off`). What breaks is everything measured in host
  seconds: a `sleep 40` in the command script is many emulated
  seconds under FF, so injected keys land after the scene they were
  aimed at, and a sound recording's length stops meaning anything.
  Screenshots break too - Hatari repeats stale frames while fast
  forwarding, so a shot can be several frames old. The rule two
  ports converged on: **`FF=on` for a benchmark with no input and
  no audio, `FF=off` for anything involving key timing, a capture
  or a picture.** It is a whole-run setting; the fifo toggle is not
  a command (`hatari-shortcut fastforward` breaks the fifo) so it
  cannot be flipped mid-run.
- **`--memsize` takes integer MiB and `0` means 512K.** `0.5` is
  not valid. Default is 1M; a game that needs more wants
  `EXTRA="--memsize 4"`, and the 512K/1M fit is still a claim to
  test at `--memsize 0` and `1`.
- **Program stems are 8 characters.** Hatari's GEMDOS drive maps
  names to 8.3, so `BLITCOST2.TOS` beside `BLITCOST.TOS` silently
  runs the old one. Same for every asset name.
- **`AUTO\` on the test drive runs at boot.** The program's
  directory is drive C:, so anything in its `AUTO\` folder runs
  before the program. The joystick fixture `XPADSTUB.PRG`
  (tests/hatari/xpadstub.c, dropped in `AUTO\` to test TJOY)
  reports right + fire held permanently, and one left behind made
  a game with joy-key emulation walk right on its own. When input
  moves by itself, look in `AUTO\` before looking in the game,
  and remove the stub once the joystick test is done.
- **Both joystick ports can be driven from a script.** Hatari
  emulates a stick from the keyboard, and a letter sent through the
  fifo goes through its key handling, which hands a joystick's key to
  the joystick: `EXTRA="--configfile tests/hatari/joykeys.cfg"` puts
  port 1 (joystick 0) on I K J L and H and the mouse port on W X A D
  and F, and `keydown i` / `keyup i` move them (in lower case:
  `keydown L` moved neither port). With one joystick the
  mouse port's keys do nothing, as on the machine. `--trace
  ikbd_cmds` logs every command the IKBD gets, which is how a mode
  switch or the mouse given back is seen.
- **A sound capture needs the program's own marker, not a sleep.**
  `hatari-shortcut recsound` records from when it fires, and with
  `FF=off` the boot and the program's loading take seconds that
  vary with the machine and the program, so a fixed sleep can
  record the boot and stop before the sound. A demo that plays for four seconds then
  leaves a capture that is digitally all zero, which reads exactly
  like a broken sound path - several hours went into a library that
  was working. Print a marker at start-up **to stderr**, which is
  unbuffered, `waitfor` it and start recording then; a marker sent
  to stdout can sit in the buffer until the program exits, so
  waiting on one syncs to the end of the run. Confirm from the
  capture itself before believing anything: a quick check of the
  peak sample says whether there is audio at all, and only then is
  a spectrum worth reading.
- **`waitfor`/`waitfile` patterns must be single words**, because
  the script is word-split on its way to the harness. And the
  harness has no watchdog: a program that hangs hangs the run, so
  do not leave an unattended sweep without something that ends it.
- **A run that never ends is Hatari asking a question.** Its own
  default asks before it quits, and with a fresh configuration (a
  `HOME` or `--configfile` of its own) and no window the question has
  no answer: the emulator ignored the driver's TERM on st, ste, tt and
  falcon alike. The driver passes `--confirm-quit false` and sends -9
  ten seconds after a TERM that did not end the emulator, saying so on
  stderr. A runner of your own wants the same flag, and its own -9.
- **On the machine a finished program takes its results with it.** A
  check or a benchmark that runs to its end drops back to the desktop,
  and what it printed is gone before anyone looks. STDL's examples and
  probes call `hold_on_exit()` (`examples/hold.h`) first thing in
  `main`: every way out says "Press a key." and waits, and given a
  name it also writes everything printed to NAME.LOG (needed where the
  program printed while it had a screen of its own, which STDL takes
  away at the end). A port's own test programs want the same. Under
  `run.sh` it costs nothing: the output reaches the log as it is
  printed and the script ends the emulator.
- **macOS has no `timeout`.** Wrapping `run.sh` in it fails with
  an empty log and no screenshots, which reads exactly like the
  program dying on boot. The script bounds its own waits; do not
  add another.
- **Crashes report a PC, not a symbol.** `NAME.err` carries the
  "Bus Error"/"Address Error" line with `PC=$xxxxxx`;
  `tests/hatari/map-crash.sh` maps it to a function from the
  unstripped ELF and the runtime address of `main` (have the
  program print `&main` once at startup). A PC in `$e0xxxx` or
  `$fcxxxx` is inside the ROM - the fault is in what the program
  asked TOS to do, not where the report points.

### The CPU profiler

Hatari's profiler attributes cycles to addresses, which beats
ablation and bisecting for finding where time actually goes - one
port used it to find a 17-second startup that bisecting had blamed
on the wrong component entirely (it was `stat` in mintlib
converting DOS timestamps through `mktime`, ~120ms a call on a
68000).

The recipe, because several steps are not obvious:

1. Copy the **unstripped** ELF over the `.TOS` on the test drive.
   The profiler needs symbols the stripped binary does not have.
2. Load the symbols once the program is running:
   `hatari-debug symbols prg` reads them straight from the program's
   own a.out symbol table (Hatari 2.6.1 answered "Loaded 1057
   symbols" for one demo). A C++ port may still need a symbol file
   built with `nm`, demangled and with the characters Hatari's loader
   will not take stripped, or the symbols silently do not load.
3. Around a marker you have synced on, drive it through the fifo:
   `hatari-debug profile on`, then `hatari-debug profile save
   <path>` **while profiling is still on**. `profile off` frees the
   data, and a save after it fails with "no CPU profiling data
   available". `profile symbols N` and `profile addresses N` printed
   nothing when sent through the fifo, so work from the saved file.
4. Read the saved file's call-graph lines (`0xADDR: caller = calls
   type i/i/cycles e/e/cycles, ..., _name`). They carry each
   function's inclusive and exclusive cycles, and they are where
   `___mulsi3` and friends show up with their callers. In one demo's
   saves the per-address section was empty. A call still running at
   the save is missing from the graph, so save between frames, not
   in the middle of a long one.
5. Convert with the machine's clock - one run measured
   `ms = cycles / 8021` - and attribute exclusive cycles per object
   file by address range. The TEXT base was `0x149f2` under TOS
   2.06 with 4MB; it moves with TOS and memory size, so read it
   rather than assuming it.

Note `hatari-shortcut fastforward` is **not** a fifo command and
breaks the fifo if you send it.

### Measuring, without fooling yourself

A port measured its frame rate for weeks at about 28fps on a Mega
STE. The real figure was 5.9, and nothing in the emulator was
lying: every check that had been made was one that a slow game
passes. These are the ones that would have caught it, cheapest
first.

- **A blit costs a fixed sum before it moves a pixel.** Measured on
  an emulated STE, an unmasked same-phase blit is about 0.42ms of
  clipping, phase and dispatch whatever its size, and 0.23ms on a
  16MHz Mega STE. At 20-60 blits a frame that is 5-25ms of frame
  time in setup alone, so fewer, larger blits beat many small ones
  even when the pixel count is identical. `tests/hatari/blitcost.c`
  in the STDL repo measures it on your machine.
- **Before believing a library bump cost you time, check the code
  ran.** A port lost 27% of its frame rate across one STDL commit,
  reproducibly, on a plain STE. The cause was not in that commit:
  the blit it blamed was never called during gameplay, a guard in
  the port's own code saw to that. The commit changed the size of an
  object file, which moved everything after it, and the port's frame
  sat on a vertical-blank boundary. Build the library with
  `-DSTDL_BLIT_STATS` and read `stdl_blit_calls` (and
  `stdl_spr_calls` for `STDL_BlitSprite`): a counter that
  says zero ends the investigation in one run. The row counters
  beside them say which path the rows took - BLiTTER
  (`stdl_blit_blitter`), inline copy, long rows (`stdl_blit_memcpy`,
  the movem copy), per-group merge
  (`stdl_blit_merge`), or the unaligned shift chain
  (`stdl_blit_shift`) - and `stdl_spr_rows`, `stdl_spr_shift` and
  `stdl_spr_blitter` do the same for sprites. (`stdl_blit_force`,
  in the same build, sends every operation the BLiTTER could take to
  it regardless of size; it is for BLITCHK, not for a port.) A frame time that
  steps by exactly one frame period is a missed boundary, not a
  slowdown - look for what waits, not for what got slower.
- **With a border open, the BLiTTER loses to the CPU.** Measured on
  an emulated STE, copies to the screen at 8 rows tall: the CPU path
  wins at every width up to 256 pixels, and at full width the
  BLiTTER goes from 615 to 860 when the bottom border opens while
  the CPU path stays at 740. The overscan blit policy has to place
  each BLiTTER operation against the beam, and that costs more than
  the BLiTTER saves at these sizes. A port running overscan should
  try `STDL_UseBlitter(0)` and measure; one such port finds it costs
  nothing either way, which says the same thing.
- **Have the program say which path it took.** A library fast path
  can be gated on something invisible - an alignment, a flag, a
  machine feature - and when it declines, the picture is identical
  and only the clock knows. A port and the library once measured
  the same blit correctly and disagreed by 1.85x for exactly that
  reason. Print the condition next to the timing, not just the
  timing.
- **Say which path a performance claim is about.** The library has a
  CPU path and a BLiTTER path and picks between them per blit, so a
  number that does not say which one ran cannot be acted on. Force
  each with `STDL_UseBlitter(0)` and `(1)` and quote both; a
  difference that appears only when the BLiTTER is *allowed* but
  never used is a cost in the decision, not in the blit.
- **A 200Hz tick is 5ms, not a millisecond.** Frames divided by a
  `STDL_GetHz200` delta and called per-second is five times too
  high - the arithmetic is `12800 / ticks`, not `64000 / ticks`.
  Five times is the dangerous factor, because it turns an
  unplayable 6fps into a respectable-looking 29. `STDL_GetTicks` is
  the one returning milliseconds; swapping between them proves
  nothing, since `GetTicks` is that same counter times five.
  Measured: both report identically with `FF=on` and `FF=off`, so
  fast-forward does not distort either.
- **Print something whose answer you already know.** The strongest
  check costs nothing: derive a known quantity from the same
  timestamps. One engine's internal clock is 70Hz by design, so
  printing it beside the frame rate turns a scale error into an
  obvious 14 or 350. Every game has such a number.
- **When a timing result looks impossible, count events instead.**
  An integer no clock can distort settles it: that port counted
  blits per frame, got 4 to 6, and a 200ms frame issuing four blits
  says immediately that the cost is not where it was assumed. It
  was a per-frame bookkeeping loop doing 32-bit divides, not the
  pixel paths.
- **Relink the same objects in a different order to measure your
  own noise floor.** Layout moves frame times on *every* ST-class
  machine, not only where there is a cache to blame - "no cache, so
  layout cannot matter" is the reasoning that has now caught two
  people here. The control is cheap and needs no run-time switch:
  link the identical `.o` files in reverse order, so the code is
  the same and only the addresses differ, and measure both. One
  port did exactly that on `--machine ste` and found per-scene
  frame times moving -3.29% to +1.86%, with 13 of 39 scenes moving
  1.2% or more - which was the size of the win it had just
  reported.
  The way out is aggregation, not despair. Over all 39 matched
  scenes the same control moved -0.29%: layout noise is large per
  scene and very nearly cancels across them. So quote an aggregate
  over every matched scene, never a per-scene delta, and run the
  reverse-link control alongside to establish that run's floor.
  Measured that way the same change came out -3.97% against a
  -0.29% floor - real, about fourteen times the noise, and twice
  what eyeballing a handful of matched log lines had suggested.
  Where that noise comes from is narrower than "layout", though: the
  library's own timing sweep (SPRCOST), built from the same objects
  linked forwards and in reverse on `--machine st`, timed identically
  to the tenth of a microsecond on all 85 cases. A 68000 with no
  cache does not care where an instruction sits, so the per-scene
  movement a whole game sees comes through something the link order
  moves along with the code - where the heap's blocks land, which
  side of a VBL a frame finishes on. Keep running the control; the
  floor it measures is real, only its cause is not the code's
  address. And a change inside a big C function is not a layout
  change: it can move that function's timings by several percent
  through register allocation alone, which the control cannot see.
- **Run the same binary twice as well, and quote neither result
  without both floors.** The cheaper control is the one that gets
  skipped, because a deterministic demo is assumed to repeat
  exactly. It does, in aggregate: three pairings of three runs of
  one unchanged binary agreed to +0.06%, +0.06% and -0.00%. Per
  scene the same binary disagreed with itself by -1.9% to +2.8%,
  with no code moved and nothing to do with layout - a 5ms clock
  over a short window quantises that coarsely. So there are two
  independent reasons a per-scene delta cannot be quoted, and the
  reverse-link control alone would let you believe layout was the
  whole story. The pairing to use: **the same binary twice for the
  measurement floor, the same objects relinked for the layout
  floor, and the result stated against both.** The change above
  finished at -3.97/-4.03/-4.02% against three separate baselines,
  which is where to stop refining and call it -4.0%.
- **Prove a speed-up with a control that changes one thing.** A fix
  that reshuffles the heap can look like a fix that works. Keep the
  mechanism and remove only what you think is incidental - allocate
  the spare word but skip the alignment, add the instrumentation
  without the change - and see whether the gain survives. Padding
  the binary or profiling it are not that test: they move the
  symptom without saying which half moved it.
- **Check against the wall clock.** With `FF=off` emulated time is
  real time, so counting the program's own log lines over a known
  real window is an independent check on anything it measures
  itself.
- **A recorded demo is the best benchmark and the worst
  speedometer.** Attract-mode or demo playback is deterministic, so
  frame times repeat across runs and two builds can be compared at
  all - hand-driven play cannot do that. But an engine's demo path
  often has its own timing: one pins the tick to the recorded rate
  and skips the clamp the game uses, so playback runs in slow
  motion by design and reads about 28Hz against a 70Hz nominal
  clock. Deterministic and wrong are compatible. Benchmark on the
  demo, judge speed from play.
- **Repeatability is a list of things held still.** Machine type, a
  *specific* TOS image (not whichever EmuTOS is to hand), the
  `--memsize`, the same config file on the GEMDOS drive, no input,
  a fixed seed, a fixed frame count, and the program's own clock
  for the timing. With all of those pinned, one port's 512-frame
  benchmark repeats to 0.01ms across runs on `st` and `ste` - good
  enough to see a 0.4ms change. A Mega STE will not do this at any
  price: its cache moves frame times ~10% with code layout, so
  compare CPU modes inside one binary through a run-time switch
  (measured that way in one run: 24.95ms a frame at 16MHz+cache,
  39.59 at 16MHz without it, 41.45 at 8MHz) and never across
  builds.
- **Match runs by a counter, not by hope.** Two runs of the same
  demo still drift, so print clock-independent counters (tiles
  changed, blits issued, which path they took) in the same log line
  as the timings and pair lines between runs by those counters.
  That is comparing the same scene instead of assuming two runs
  lined up, and it is what lets a 1-2% difference mean anything
  when run-to-run noise is larger than that.
- **A diagnostic gated on the same condition as the code it probes
  can never fire, and its silence reads like good news.** Count how
  often it ran and print that beside its result, or a null result
  means nothing.
- **An agent's estimate from static structure is a hypothesis, and
  a cheap one to falsify.** A review agent costed a function at
  3-10ms a frame from its 987 call sites in the source. Counted at
  run time it ran 11-21 times a frame, about 3.5ms. Grep measures
  the source, not the frame - and a confident number derived from
  structure reads exactly like a measured one in a review, so the
  habit that matters is asking which of the two you are holding
  before you plan work around it.
- **Build the diagnostic to separate the two suspects, not just to
  notice the failure.** Hardware scrolling that fails by jumping a
  whole 16-pixel group with the fine offset stuck looks identical
  whether the port never asked for a fine offset or asked for all
  sixteen and got none - one is the port's bug and one is the
  library's or the machine's. One port settled it before the test
  by accumulating a bitmask of which offsets it requested over 256
  frames and printing it once: near-`0xFFFF` with the picture stuck
  points at the library or the Shifter, a couple of low bits points
  at the port. A shift, an or and a compare per frame.
  And note what makes it trustworthy: the mask has been seen to
  take more than one value - `0xFDFF` and `0xFFDD` on some runs,
  `0xFFFF` on others. A coverage check that has only ever read
  all-ones is indistinguishable from one that is not measuring
  anything, but one short reading is weak evidence too. **Two
  distinct values observed is the bar** - the same bar as the
  provenance field below, and worth applying to any check whose
  healthy state is a fixed value. The same applies to whatever identifies the run:
  `STDL_GetMachineInfo`'s class and `_MCH` cookie went in beside
  that mask so a log read later could not be a coin flip between
  two machines - and the two cookies were seen to differ
  (`0x00010000` on an STE, `0x00010010` on a Mega STE) only because
  the probe was run on both to collect baselines, not because
  anyone set out to check it. A provenance field nobody has seen
  take two values looks like evidence and carries none, and running
  a check against two inputs by default is what exposes that
  cheaply. Foresight is not required and should not be relied on. Do not compare coverage between
  machines, though: a clamp that resolves differently at 8 and
  16MHz makes a fixed frame window cover different amounts of the
  same demo, so masks that disagree across machines are not a
  finding.
- **A port's own vocabulary collides with the library's.** Grepping
  a port for "border" to find out whether it opens an ST overscan
  border found a flag, a call, an options-menu item and four
  graphics chunks in one engine, every one of them about the EGA
  border colour or the game's own UI frame, and none of them
  touching overscan. The same trap waits on "surface", "blit",
  "palette" and "sprite". Search for calls into the library's API,
  not for the words it uses.
- **Say when a result is emulator-only.** A port can be developed
  entirely in Hatari and be perfectly healthy there while nobody
  has yet put it on hardware - which makes every claim it carries,
  including whether a library path works at all, an open question
  rather than a validated result. Label them as such; the cost of
  the other habit is on this page several times over.
- **Check that the thing you are timing actually happened - in
  both runs, separately.** The rule below is not enough on its own
  when the measurement is a comparison. A port comparing two 25s
  captures got burst counts of 490 before a fix and 18 after, which
  reads as a spectacular improvement until you notice the two
  recordings had caught different amounts of game activity, and
  that the 490 were the artefact itself crossing the threshold
  rather than sounds. Confirm the content of A and of B before
  comparing their numbers; a deterministic script is what makes
  that possible.
- **Check that the thing you are timing actually happened.** A
  20-second benchmark of "the cost of music" measured a track that
  had ended 4.5 seconds in; the frame time alone could never have
  shown it. Record with `recsound` and plot RMS per window (numpy
  is enough) - the same check settled a music-versus-effects
  balance complaint to the decibel.
- **Static evidence proves nothing about speed.** A game at 6fps
  screenshots identically to one at 30, and sounds identical too.
  Watch it move, or count frames.
- **Have the binary name itself in its own log.** A stamp printed
  at start-up (`git describe`, plus a `-dirty` marker) means a log
  coming back from someone else's machine says which build produced
  it, and a stale file in the ship directory announces itself
  instead of being reconstructed later. Generate it as a **header**
  rewritten only when the value changes, never as a `-D` on the
  compile line: a changed `-D` does not trigger a rebuild (below),
  so a `-D` stamp goes stale exactly when it matters and a stale
  build then prints the wrong commit confidently. A wrong
  provenance field is worse than none, because it reassures.
- **A rule placed above `all` in a makefile silently becomes the
  default goal.** `make` then builds that one thing, produces
  nothing else and exits zero. Pin `.DEFAULT_GOAL`, and check for
  the artifact rather than the exit status - a build that appears
  to work while producing nothing is the same family as everything
  else on this page.
- **A harness that writes into the directory you ship from will
  eventually ship the wrong binary.** Swapping profiling and test
  builds in and out of a deploy directory left an instrumented
  build sitting where the release build belonged; it was caught on
  a checksum. Check the artifact you are about to hand over rather
  than trusting that the build ran.
- **A changed `-D` flag does not rebuild anything.** Objects are
  newer than the sources, so `make EXTRA_CFLAGS=-DFOO` without a
  clean measures the previous binary - including, once, a run whose
  instrumentation was simply absent and read as a catastrophic
  result. This library's own build now tracks header dependencies
  for the same reason; a port's may not.

## Game services (use instead of reinventing)

- Scrolling camera: `STDL_SetSurfaceOrigin(stripe, 0, cam_y)` -
  short stripe as a windowed level, game keeps level coordinates.
- Hardware scrolling on an STE: `STDL_SetScrollOrigin(world, x, y)`
  shows the 320x200 window at pixel (x, y) of a surface at least 336
  wide (stride up to 670 bytes) by programming the STE's video base,
  LINEWIDTH and HSCROLL from the VBL - no copy, no CPU cost. The
  Shifter latches the base three lines before the VBL while the
  offsets apply at once, so the VBL writes the base, the video
  counter itself and the two offsets together while the fetch has not
  begun: every request, however late in the frame, is on screen at
  the next VBL, base and offsets as a matched pair (measured with
  `tests/hatari/hwlatch.c`: late requests 0/100 on the next frame
  before, 100/100 after). A VBL running late into the picture falls
  back to base first, offsets a frame later, and
  `STDL_ScrollWindowPending` says when a request has landed. This
  is the tile-engine model: keep a play field one tile larger than
  the screen in a page-flipped pair of buffers, move the window by
  the pixel, redraw only the strip that scrolls in. Test
  `STDL_HasHwScroll` at init (a plain ST has no scroll registers: the
  entry after this one); do not mix with `STDL_Flip`.
  `examples/hwscroll.c`.
  Three ways to arrange the play field, all with the existing calls
  (patterns from a mature STE engine, not measured in STDL - time
  your own frame before and after):
  - **Spread the strip across the frames that uncover it.** Redrawing
    a whole 16-pixel strip at each tile boundary is a spike every
    16 pixels. Keep one tile of margin and draw, each frame, only the
    part the scroll has uncovered since last time: scanlines
    `[old_fine, new_fine)` of every tile in the incoming row, or a
    band of the incoming column's tiles (whole-group blits, so the
    aligned fast paths). The strip is complete as it comes into view,
    and the work per frame follows the distance moved. Double-
    buffered, each page keeps its own last position, so its update
    covers two frames of movement.
  - **Horizontal scan-walk, no wrap.** Give the buffer a fixed stride
    (say 168 bytes, 21 groups) and let the window's base walk forward
    8 bytes per 16 pixels: `STDL_SetScrollWindow(buf + (x >> 4) * 8,
    stride, x & 15)`. Tile column `c` of tile row `ty` then lives at
    group `c % 21` of surface row `16 * ty + c / 21` - the bytes past a
    row's end are the next row's start, which has already scrolled
    off. Unlimited scrolling to the right for about half a byte of
    padding per pixel of total travel, and no copy ever.
  - **Vertical loop-back.** Keep a duplicate of the page directly
    below it and write every tile row to both; the window may then
    straddle the join, and scrolling vertically never ends. Twice
    the memory and twice the row writes.
- Scrolling without scroll registers (a plain ST; a TT's or a
  Falcon's 8-plane screen, which has none; an STE program that flips
  pages): scrolling is a copy, and the copy is most of the frame.
  `tests/hatari/scrlcost.c` (SCRLCOST) times the ways of doing it on a
  320x200 screen of 16x16 tiles, in Hatari, cycle-exact, in
  milliseconds a frame (the page checked pixel by pixel against a model
  of the map after each; November 2026, STDL's whole-group window copy
  and 8-plane shifted copy in):

  | | plain ST | STE, BLiTTER | TT | Falcon |
  |---|---|---|---|---|
  | redraw 20x13 tiles | 97.9 | 97.9 | 27.0 | 48.9 |
  | a column of 13 tiles | 4.9 | 4.9 | 1.4 | 2.5 |
  | whole page (`STDL_BlitSurface`, NULL srcrect) | 19.3 | 17.1 | 8.2 | 16.5 |
  | a window out of a wider buffer (srcrect) | 20.7 | 17.1 | 8.2 | 16.5 |
  | a window out of a ring, across both seams | 26.4 | 19.2 | 8.8 | 17.7 |
  | a window at a sub-group x | 132-164 | 17.5 | 12.4 | 24.9 |
  | smooth, a pixel a frame, the best way measured (worst frame) | 43.3 (45.8) | 24.3 (30.5) | 13.1 (17.5) | 26.2 (35.1) |
  | 8 pre-shifted 16x16 sprites, drawn / and restored | 7.6 / 14.0 | 7.7 / 14.9 | 1.8 / 6.7 | 3.4 / 13.0 |

  On a TT, Flip's present adds 8.9ms after a frame that changed the
  whole screen (1.2 after a column).
  - **Never redraw the map a frame.** Draw it once; afterwards draw only
    the strip that comes into view.
  - **One copy a frame, from a background no sprite touches.** Keep the
    map's tiles in a buffer bigger than the screen; each frame the
    camera moves, copy the window into the back page with
    `STDL_BlitSurface` and a srcrect, draw the new strip into the
    buffer, and draw the sprites on top. The copy rewrites the whole
    page, so nothing needs restoring, and flipped pages need no care.
    While the camera stands still, restore the sprites' rectangles
    from the buffer instead (`STDL_Dirty` restores from any background
    surface).
  - **On a plain ST that is a 25Hz game, with room for the sprites.**
    The copy alone is a whole 50Hz frame: one `memcpy` of the 32,000-byte
    page 19.3ms, a window out of a wider buffer through
    `STDL_BlitSurface` 20.7 (the library copies rows of whole groups
    with `movem.l`, as a loop of the game's own would, 19.5; it was a
    `memcpy` a row, 38.0, before November 2026). With eight pre-shifted
    sprites (7.6) and a new column of tiles (4.9) a coarse-scrolling
    frame is about 33ms (an estimate from the parts), inside two
    frames. A ring's window is 26.4: every row is two pieces across the
    seam, each with its own overhead. To do better: lay the buffer out
    so the window is one piece of whole rows (the scan-walk above,
    borrowed as a surface at the window's address each frame,
    `STDL_CreateSurfaceFrom(base, 320, 200, 168, NULL, 0)`), make the
    playfield smaller than the screen (a copy costs its bytes: a
    256x160 window is 64% of the screen's), or scroll in pushes (a
    still frame with eight sprites restored and drawn is 14.0ms).
  - **Smooth horizontal scrolling on a plain ST needs pre-shifted
    rings.** Everything that shifts at run time is out: a window at a
    sub-group x 132-164ms (the 4-plane shift chain), a whole-page
    `roxl` 37ms before its new column, the map as pre-shifted sprites
    266ms. Keep a ring for each pixel phase you support, each drawn
    16/K pixels further left; a frame is an aligned copy from the ring
    that matches x, plus the new column in one ring, drawn as
    pre-shifted tile sprites (`STDL_SpriteFromSurface(tiles, 16,
    STDL_PRESHIFT)`: 15.4ms for 13 tiles, against 32.7 as unaligned
    blits). Sixteen rings, a pixel a frame: 43.3ms on average, 45.8 the
    worst frame, the copy across the seams and the column - three
    frames before sprites. RAM decides K: a 336x224 ring is 37.6K, so 16
    rings 602K, 4 (4-pixel steps) 150K, 2 (8-pixel steps) 75K; with
    fewer rings most frames are the copy alone.
  - **Vertical scrolling costs the same at any speed**: any number of
    rows is the same copy (23.5-29.6ms in place on a plain ST, strip
    included).
  - **Shifting a page in place** (`STDL_BlitSurface` from a surface
    borrowed over it at `pixels + 8`, or `+ N * 160`, onto one at
    `pixels`, or a surface onto itself) is right in any direction
    (since November 2026: STDL runs the rows the way the overlap
    needs). Leftwards and upwards it is the copy (19.3ms, plus the
    strip); downwards the same from the bottom (23.8ms with the strip
    on a plain ST, 22.3 on an STE); rightwards by whole groups a
    `memmove` a row (44.8ms with the column), where one `memmove` of an
    unpadded page of your own is 24.2; rightwards by less, through a
    buffer, 161ms for a pixel on a plain ST and 77 on an STE
    (`docs/limits.md`). In place pays only for push scrolling: a page
    with sprites on it needs a clean background shifted as well, and a
    page under `STDL_Flip` holds the picture from two frames back
    (three triple-buffered), so it shifts by the camera's movement
    since then.
  - **With a BLiTTER**, `STDL_BlitSurface` copies a window at any x in
    17.1-17.5ms on an STE (8.8-9.0 on a Mega STE in Hatari, where the
    CPU's aligned window is 19.0): a ring scrolled a pixel a frame is
    24.3ms (worst 30.5). An STE's scroll registers cost nothing,
    though; this is for a program that cannot use them.
  - **A plain ST's video base moves only in 8-line steps** (its base is
    256-byte aligned, and lcm(160, 256) = 1280 bytes), so coarse
    vertical hardware scrolling is possible in principle. STDL has no
    call for it: `STDL_SetScrollWindow` refuses a plain ST, and
    `STDL_Flip` owns the base.
  - **On a TT's or a Falcon's 8-plane screen** a page is 64,000 bytes.
    Aligned scrolling is the window copy (8.2ms on a TT, 16.5 on a
    Falcon), plus a TT's present; a playfield inset off the groups at
    its own phase (304 pixels at x = 8) 9.3 and 18.6. A window at a
    sub-group x is 12.4ms on a TT and 24.9 on a Falcon (it was 117-125
    and 236, a C path, until November 2026), so smooth scrolling there
    no longer needs rings for
    speed: one ring and a shifted window, a pixel a frame, is 21.2ms on
    a TT (worst 41.8, the frame that draws a column) and 42.4 on a
    Falcon (worst 83.8). Sixteen rings with sprite columns are still
    quicker, 13.1 (17.5) and 26.2 (35.1), for 1.2MB (75.3K a ring). In
    place, through a surface borrowed with
    `STDL_CreateSurfaceFrom8Planes` (the screen's own stride, two
    display lines on a TT) or the screen onto itself, any direction is
    right: leftwards 9.7ms on a TT and 19.0 on a Falcon with the column,
    downwards 9.8 and 19.2 with the strip, rightwards by a group 12.5
    and 24.9 with the column, by a pixel 45.6 and 91.3. On a TT, tell
    Flip about writes made behind the library's back with
    `STDL_UpdateRects(0, NULL)`.
- Frames longer than a VBL: `STDL_SetVideoMode(320, 200, 4,
  STDL_TRIPLEBUF)`. Double-buffered, `STDL_Flip` waits for the VBL
  after its request before the other page is free, so every frame
  is rounded up to whole VBLs - half a VBL idle on average, a 34ms
  frame taking 40. With a third page the next frame starts at once
  and the flip waits only if the previous request has not yet been
  taken, which in a frame longer than a VBL it has: TRIPCHK's frames
  of a VBL and a half take 60 VBLs for 40 against 80, and one 3D
  game's race went from 147 to 135ms a frame on a Mega STE. A frame
  shorter than a VBL gains nothing (it runs at the display's rate,
  every frame shown, as double-buffered). The third page costs 32K
  of ST RAM (on a TT's or a Falcon's responsive screen, a page of its
  own: 153.6K on a TT); without it the mode stays double-buffered, so
  test the surface's flags for `STDL_TRIPLEBUF`. `STDL_Dirty` keeps a list for
  each of the three pages. With a border open the overscan module's
  own two pages flip as before. `examples/polydemo.c` (T switches).
- Erasing sprites: `STDL_DirtyInit(background, max)` once, then each
  frame `STDL_DirtyRestore(screen)`, draw, and `STDL_DirtyPush` each
  rectangle drawn. Restores come from the clean background, so pushing
  a rectangle twice or pushing too many costs time, never pixels (an
  overflow restores the page whole). Each page keeps its own list, so
  it works as-is with `STDL_DOUBLEBUF` - the back page is erased of
  what was drawn into it two frames ago - and with `STDL_TRIPLEBUF`,
  three frames ago. Before that change (v1.13.2
  and earlier) the single list erased the wrong page; a double-buffered
  port that pushed both frames' rectangles to compensate can stop.
  Keep the background the page's shape - same size and stride, no
  origin on either, no mask on the page, and no colour key on the
  background - and with the BLiTTER absent or refused
  (`STDL_UseBlitter(0)`) at plane budget 4, each rectangle is
  restored by offset, skipping the general
  blit's set-up: measured on a plain ST, 16 restores of 16x16 at
  group edges went from 9.9ms to 6.1ms, and at x & 15 = 5 from 18.6
  to 12.7 (the edge groups' merges are most of what is left).
  `STDL_DirtyWholeGroups(1)` restores the whole groups a rectangle
  touches instead, a plain copy: 12.7 -> 7.9ms for those sixteen. It
  also repaints the pixels beside each rectangle from the background,
  so use it only where everything on the page is background or a
  pushed sprite, not where a HUD is drawn straight onto it.
  When the background itself changes (a door opens, a score drawn into
  it, a new level), draw the change into the background surface and
  call `STDL_DirtyBackground(&rect)` (`NULL` for all of it) before the
  frame's restore: every page then puts the rectangle back as it next
  comes round, a page not yet restored included, where a game would
  otherwise keep a list of its own and count the pages. Each change is
  a rectangle in each page's list; past `max_rects`, or with `NULL`,
  each page is restored whole once. Its own object: a program that
  never calls it links none of it. `examples/starter.c`.
- Composition: `STDL_TRANSPARENT` fills punch holes in masked
  surfaces; blits maintain destination masks; `STDL_SurfaceIsOpaque`
  (cached) skips backdrop under solid tiles; `STDL_PutGroup` for
  runtime asset decoders; `STDL_CreateMask` for built-up surfaces.
- YM music: `STDL_LoadMusic("SONG.STM")` (from `stdlconv midi` or
  `stdlconv opm`), or
  `STDL_MusicFromMemory(data, bytes)` for a stream linked into the
  program, which plays in place without a copy. `STDL_MusicFrame()`
  is the music's own clock, in stream frames, so sync effects to it,
  not to a counter of your own. `STDL_SeekMusic(frame)` jumps,
  playing or paused, in `0 .. STDL_MusicLength(m) - 1`. It writes
  every voice's state at once, or on resume when paused. Ship one
  song and seek, not a copy per entry point.
  `STDL_GetMusicRegisters(regs)` returns the stream's 14 registers
  for VU meters without touching the chip; volumes 8-10 with bit 4
  set follow the envelope. `examples/musicvu.c`.
- Sound effects: `STDL_SpeakerOn/Off` = PC-speaker idiom on voice A;
  `STDL_PlaySfx` = step sequences (tone or noise) on auto voices;
  give it a `noises` array (v1.12.0) and each step has its own
  noise period, sounding with the step's tone on the same voice -
  a crack over a thump for a gunshot, a noise that sinks as an
  explosion fades. Steps keep their ms at 60Hz.
  Both steal voices from STDL_Music and hand them back restored.
  These are free: they run off the 50Hz VBL sound tick.
- Music that arrives as notes at run time (an OPL register stream
  such as IMF, a MIDI-note stream): `STDL_ToneOn(slot, period, vol)`
  / `STDL_ToneSet` / `STDL_ToneOff` on 16 slots; the device plays the
  three newest on the chip, above STDL_Music and below effects, and
  costs nothing per frame while nothing changes. `examples/tonedemo.c`.
  Call `STDL_ToneOpen` once from the main line when the notes come
  from your own timer or VBL callback: the first key installs the
  sound service otherwise, and that must not happen inside an
  interrupt. For OPL2 streams (IMF, the id/Apogee games) call
  `STDL_OplWrite(reg, val)` per write from your own replay loop and it
  keys tone slots 0-8 for you; `STDL_OplReset` at start and end, from
  the main line, which opens the device for you.
  `examples/opldemo.c`.
- Mega STE speed: `STDL_Init` takes the machine to 16MHz with its
  cache. Change it only while any border is closed - opening one
  measures the flick's instruction costs on the machine, so a
  calibration taken at one clock is wrong at the other. The next
  open after a speed change re-measures; a border left open across
  it does not, and on real hardware a 16MHz table at 8MHz does not
  open the bottom border at all.
  `examples/overscan.c` brackets its speed key that way. `STDL_UseMegaSteSpeedup(0)` leaves it as the user set it,
  before `STDL_Init` to prevent the switch or after to undo it; 2
  and 3 are the clock without and with the cache. It exists so a
  port can A/B the two speeds in one binary, which is the only
  honest way to measure there - and so a port can decline the
  switch, since a game that needs 16MHz to be playable has a
  rendering problem, not a hardware one.
- Borders plus the BLiTTER on a Mega STE: no workaround needed
  from v1.15.1. Earlier versions lost the bottom border at 16MHz
  with the cache on whenever a frame drew (a demo's overscan part:
  36% of frames), and the advice was `STDL_UseBlitter(0)`. The
  cause was the cache - one-word lines, emptied by every BLiTTER
  operation - leaving the border interrupt's timing path partly
  cold; it now warms all of it (the same demo: 1 frame in 1500). If
  a port pinned to an older STDL flickers there, bump it before
  reaching for `STDL_UseBlitter(0)`.
- Known to affect an open border, all measured on hardware and all
  invisible in Hatari. These are costs to weigh, not prohibitions:
  a port can do any of them and accept the border it gets.
  **Disk access**: one file opened, read and closed per frame gave
  5 frames of 300 with the border open from a hard drive and 3 of
  300 from a floppy - both media, so changing storage is not a way
  out. Load assets before opening, or close the border around a
  read. **Drawing volume at 8MHz**: one port's border flickers in
  proportion to how much it draws, and survives 8MHz and the CPU
  path, where the Mega STE cache fix above cannot be the reason;
  not understood - a small fixed region looks safe where a large
  varying one does not. **Resident firmware**: a cartridge driver
  holding interrupts off did it too, and stopped when unloaded.
- Alt-RAM: the Shifter and the sound DMA reach ST RAM only, and a
  program linked ALTALLOC (the toolchain default) gets alt-RAM from
  `malloc` wherever a machine has some. The library allocates its
  own hardware-visible buffers with `Mxalloc` mode 0 from v1.10.1;
  yours is any buffer passed to `STDL_PlaySample*`, which refuses a
  block outside ST RAM rather than playing noise. Do not clear
  ALTALLOC to dodge it - alt-RAM is what makes a 1MB machine viable
  for a big port - and note the failure mode when it is wrong: a
  garbled screen or silence, with no error.
- Mouse: `STDL_EnableMouse(0)` stops the IKBD reporting it, 1 puts
  it back, -1 queries, and it returns the previous setting. A port
  that never reads the mouse should call it once - every movement
  is three ACIA interrupts about 1.3ms apart, parsed regardless.
  With a border open it is worth more than the cycles: the ACIA
  cannot preempt the border timers but it can delay them, and a
  flick lands on an exact cycle, so moving a mouse flickers the
  screen on hardware - measured, with the missed-window count
  climbing while it moves and neither happening once reporting is
  off. Two joysticks (`STDL_SetNumJoysticks`) turn it off too, the
  mouse port then being joystick 1; `STDL_EnableMouse` only records
  what it is asked until the program goes back to one.
  Joysticks are unaffected ($FD/$FE/$FF against
  the mouse's $F7-$FB) **including fire**, which is worth saying
  because the IKBD steals joystick 1's fire bit into the right
  mouse button while the mouse reports: with reporting off it hands
  it back to the joystick packet, measured with a real stick on a
  Mega STE. Reporting is restored when the library releases the
  keyboard, and `STDL_GetMouseState` stops changing rather than
  failing. `examples/overscan.c`, M.
- Input without a video mode: `STDL_OpenInput()` takes the keyboard,
  mouse, joystick and pad as `STDL_SetVideoMode` does, and leaves the
  screen alone - for a tool that prints on the screen it was started
  on, in any resolution (`examples/checkkeys.c`), a mono monitor's
  included, which `STDL_SetVideoMode` refuses, or a program that drives
  the screen itself. A TT's or a Falcon's 256 colours are STDL's own
  screen (`stdl_responsive.h`). It runs `STDL_Init` if nothing has, a
  mode set later changes nothing, and the keyboard goes back at
  `STDL_Quit` and down the terminate path. A screen mode the program
  set is the program's to put back: the library restores only one it
  set.
- Display rate: `STDL_SetRefresh(50 | 60)` switches the sync rate
  and returns the rate that actually took effect, `-1` queries. PAL
  machines boot at 50 and NTSC at 60, and 60Hz buys ten more frames
  a second at the same 320x200 - worth having for a game already
  inside its frame budget and worth nothing for one that is not.
  Three things bite. The VBL rate moves with it, so anything timed
  by counting frames runs 20% fast at 60Hz (`STDL_Music`, the
  `STDL_SetVoiceTick` sequencer and `STDL_Sfx` steps are rate-aware
  and compensate; `STDL_AddVBL` callbacks are per-tick by
  definition and do not) -
  time from `STDL_GetTicks` where it matters. Overscan is PAL-timed,
  so a border forces 50Hz while it is open and the request lands on
  the final close, which means a port cannot have both. And 60Hz on
  a PAL machine is outside what some period TVs will sync, which is
  why it is opt-in and why the return value is the rate rather than
  an acknowledgement - ask, then believe the answer.
  `examples/vblchk.c` measures the VBL at both rates.
- Rasters (colour changes down the screen): `STDL_OpenRasters(lines,
  reg1, reg2)` changes one or two colour registers every `lines`
  lines from a table. Fill `STDL_RasterTable()` and call
  `STDL_ShowRasters(keep)` with each flip; `STDL_CloseRasters()`
  when done (`<stdl/stdl_raster.h>`, in `stdl.h`). With keep = 1 the
  next table starts as a copy of this one, so change only what moves;
  rewrite every entry and pass 0 to skip the copy. Colour 0 is the
  border too, so its bars run edge to edge. Measured on a plain ST
  (RASTCHK) with colour 0 first, every second line costs 11% of the
  machine (12% with a second register) and every line 22% (25%);
  another first register is a few points dearer. Timer B is the
  borders' as well, so rasters and an open border refuse each
  other. The terminate path puts the vectors back, which a
  hand-rolled raster handler leaves pointing into freed memory.
  While open, the two registers are the table's. `examples/rasters.c`.
- Palette changes on the frame boundary: `STDL_StagePalette(1)`
  holds every change made through `STDL_SetPalette`,
  `STDL_SetColours`, `STDL_FadeTo` and the SDL shim's
  `SDL_SetColors`/`SDL_SetPalette(SDL_PHYSPAL)`, and writes it in the
  next vertical blank. Unstaged, a palette cycle or fade written from
  the main loop lands wherever the beam is, and that frame shows the
  old colours above it and the new below. Measured in Hatari with a
  colour switched at mid-screen every frame (`tests/hatari/palstg.c`):
  99 and 100 frames of 100 split unstaged, 1 and 2 staged (the
  switch-over frames). It costs up to a
  frame of delay and a VBL slot; an open border stages on its own
  account. `STDL_SetColour` (raw word) is never staged, for raster
  tricks. A port that staged its own palette from an `STDL_AddVBL`
  callback can drop it. `TPALETTE -stage`.
- Constant colour tables: `STDL_HW444(0xC72)` turns an 0xRGB with
  four bits a channel (0-15, the STE's scale) into the hardware word
  at compile time. The register does not take 0xRGB as written: its
  nibbles keep a channel's lowest bit in bit 3, so a raw 0xC72 shows
  green, not orange. ST-style 0x777 values (0-7) are already valid
  words.
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
  `STDL_BlitSurface`, BLiTTER fast paths included, save the
  whole-surface `memcpy` (see "Restore a whole background").
- Frame pacing and profiling: `STDL_GetHz200()` is the raw 200Hz
  system counter, for finer grain than `STDL_GetTicks` and for
  pacing in ticks. It counts from boot, so take differences - a
  per-phase timer around the frame is how one port found
  that an icon was being re-decoded every frame, 18ms of a 93ms
  frame nobody would have guessed at. For the parts of a frame
  shorter than a tick, `STDL_GetHz38400()` is the same clock with
  the count inside each tick (26us), unless the program reprograms
  Timer C.
- Sample music: `STDL_OpenVoices(rate)` is a fixed-function 4-voice
  sample mixer (Paula-style loop/period/volume) driven from the VBL
  with a 50Hz sequencer hook (`STDL_SetVoiceTick`, 50 a second at
  a 60Hz display too) - module music
  without the ring device's callback cost, though not free: four
  busy voices at 6258Hz take 25% of an 8MHz STE, measured, before
  the sequencer's own tick. Voices whose volumes add up to 128 or
  less (a module at 32 a channel or under) cannot clip, so they mix
  as bytes with no 16-bit sums and no clamp pass: one port's
  cutscenes, module music at 16 a channel, took 5.3% less of an
  8MHz STE for it. Music played under effects at a quarter volume
  gets that for nothing. Short loops cost no more than long ones:
  each time a voice wraps the mixer pays about 800 cycles, so a loop
  under 1024 frames that repeats until stopped is played from a copy
  of the voice's own, written out to 1024 frames or more (8K of heap
  for the four), and kept for the voice's next note of the same loop -
  a module channel's run of one instrument copies once. STE only;
  voices, the
  ring device and `STDL_PlaySample` are mutually exclusive DMA
  owners.
  An open device is not quite silent on hardware: with the DMA
  looping silence and nothing writing to it, a real STE clicks
  about once every thirty seconds. That is the DMA or the analogue
  path, no software reaches it - worth knowing before you go
  hunting your own code, since until v1.8.2 the library added a
  further 3-6x on top of it. `STDL_PauseVoices()` stops the DMA
  without giving up the volume table that `STDL_OpenVoices` spends
  a frame building, and `STDL_ResumeVoices()` starts it again about
  20ms before the first audio; `STDL_VoicesPaused()` reports the
  hardware rather than the request. Pause only asks - the stop
  happens from the tick once the ring has drained to silence, so it
  never lands mid-waveform and never cuts a voice off, which makes
  it safe to call as soon as all four voices are idle. Call resume
  **unconditionally**, never behind a `VoicesPaused` test: during a
  drain, cancelling the request is the whole job, and skipping it
  silences the device later with no error. Pausing is measured free
  on a real STE: cycling pause and resume once a second clicked
  *less* than the same device left open, so the transitions are
  inaudible and the 30-second baseline only accrues while the DMA
  runs - a port that pauses through its menus and cutscenes removes
  it for that whole stretch.
  Volume is a table, not a multiply, and scaling an 8-bit sample
  into an 8-bit table is lossy at low volumes - at `vol` 16 only
  the loudest eighth of the range survives. Since v1.8.1 a voice at
  64 is the sample halved, so two at full volume sum to the range
  exactly and a third or fourth loud voice clips rather than wraps;
  before that it was quartered, which cost another 6dB of quiet
  detail and made an ambient effect at `vol` 16 inaudible. If a
  port's effects sound thin, raise their `vol` before reaching for
  the mixer - and mix music and effects at levels that assume two
  loud voices, not four.
  **Re-check any balance you hold against these voices when
  upgrading past v1.8.1.** That change gained one port's effects
  6dB (-26.0 to -20.3 dBFS peak on identical content) and it had to
  move its YM music from 60 to 70 to match, -24.7 to -21.4. The
  correction lives in the port, not the library, and left unmade it
  surfaces months later as "the sound is wrong now", a long way
  from the upgrade that caused it.
- Sample effects: `STDL_PlaySample(buf, bytes, rate)` points the STE
  DMA at your buffer and the hardware reads it once - no ring, no
  refill, **no per-frame cost** (`STDL_PlaySampleLoop` for ambient
  loops). Stop before freeing a playing buffer - the DMA reads it
  live. A Falcon has no 6258Hz: there a 6258Hz buffer plays from a
  copy at 12517 the library makes and keeps. Monophonic, so arbitrate by
  priority yourself. It is the one path that costs nothing; when a
  game needs sounds on top of each other, the SDL_mixer shim
  (`Mix_PlayChannel`) plays up to four chunks on the voice mixer from
  the VBL, **5% of an 8MHz STE for one chunk at 6258Hz, 9% for two
  and 20% for four** (8%, 16% and 40% at 12517; about half on a Mega
  STE). Before
  v1.13.0 it mixed in C over the ring device and measured 36-75% in
  Koules - the same effects through `STDL_PlaySample` measured 0%. A
  chunk at full volume plays at half scale, a voice at 64: 6dB quieter
  than before v1.13.0, so re-check effects you balanced against the
  music. Reach for the ring device (`STDL_OpenAudio`) only when you
  genuinely need a continuous stream of your own: it costs 2% for a
  callback that does nothing, plus whatever the callback does, in the
  pump - if the callback writes signed 8-bit at an exact DMA rate
  (`stdlconv wav --rate 6258`), straight into the ring. Any other
  format is converted at every refill: 13% for unsigned 8-bit mono at
  12517Hz, 48% for a PC game's 16-bit stereo at 22050. The WAVs
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
- Taller than 200 lines: `STDL_OpenTopBorder()` removes the top
  border on any 50Hz ST - 228 visible lines instead of 200, added
  above the normal picture. The screen surface is updated in place
  (pixels, h, clip), so drawing code that reads `screen->h` needs no
  changes; check the return (228, or 0 on TT/Falcon) and keep the
  200-line mapping as the fallback. `STDL_DOUBLEBUF` works with a
  border open: the module allocates a second tall page and
  `STDL_Flip` swaps them, which is what a game wants if it draws a
  whole frame and shows it - drawing then never races the beam, at
  the cost of about 37K for the second page.
  Know what the flip costs before adopting it: it is VBL-synced, so
  a frame that takes 34-40ms waits for the next vertical blank and
  is quantised to 40ms, which is 25fps on a 50Hz display. A port
  whose frame already sits just over a VBL boundary pays a whole
  frame period for the tear-free picture, and should measure its
  drawing pass first - the fix is to get the frame under the
  boundary, not to avoid the flip; without a border open,
  `STDL_TRIPLEBUF` (below) removes the rounding. A 60Hz
  base screen is switched to 50Hz while a border is open and
  restored on close - opening a border is an active choice and it
  always takes effect on ST-class hardware. Cost is one Timer A interrupt plus a
  bounded two-line poll per frame - measured +2.1ms/frame on an
  8MHz ST in one port, and that was the full-screen blit of the 24
  extra content lines, not the trick itself. No cycle counting, so
  it holds at any CPU speed. A missed window (a long interrupts-off
  section) shows one normal-bordered frame and self-recovers, and
  the ISR skips the sync flip entirely when it wakes up late, so a
  miss can never glitch mid-frame; `STDL_OverscanMisses()` counts
  them - steady increments mean something stalls the CPU every
  frame. Read it before closing: every open restarts it, and so
  does closing one border of two (the other is re-programmed), so
  one port's exit report, read after closing its borders one at a
  time, said 0 whatever had happened. The classic culprit is a hog-mode BLiTTER blit (one port's
  cutscenes missed 191 frames of one sequence exactly this way).
  STDL's own blits cannot do it: while a border is open every
  BLiTTER operation is placed from the beam's position - the video
  counter in the picture, a Timer B stopwatch in the blanking - so
  it ends a few lines before the next timing window, is split
  around the window when it would not (the part before runs, the
  policy waits out the window while the ISR runs, the rest
  follows), and stays in hog mode. Measured on an emulated STE,
  100 back-to-back full-screen fills: 940ms with no border, 1070ms
  with the bottom open, 1110ms top, 1200ms both - 14-28% slower,
  where shared mode was 2.1x (1885ms); 2000 64x32 blits 3600ms ->
  4845-5300ms, the per-operation decision costing 35-47% on blits
  that small, so batch sprite work where you can. The border is
  then lost in about one frame in 1600 beyond the one in which it
  opens (none on a plain ST). The ISRs themselves cost under 1% of
  an 8MHz frame each (measured 0.9% top, 1.0% bottom; a CPU-bound
  loop ran 1.4% slower with one border open, 2.5% with both). A
  game driving the BLiTTER itself must use non-hog mode or keep
  hog blits short and off frame lines 30-36, 259-265 and 310-1.
  Claims MFP Timer A and Timer B's counter;
  `STDL_CloseTopBorder()` gives everything back. On a CRT the
  picture sits ~27 lines higher than stock - geometry, not a bug.
  `STDL_OpenBottomBorder()` is the same trade at the other end: 245
  seamless lines, content at rows 200..244. The GLUE only leaves a
  62-cycle window for the sync flick that fools its border test
  without also starting the next line at 60Hz timing, so the ISR
  reads the Shifter's video counter mid-line to know where the beam
  is and runs out the distance with dbra loops calibrated, the
  first time the border opens, against the machine's own scanlines
  (one to two frames with interrupts masked, once per process; it
  finds the picture start from the Display Enable events themselves,
  then measures the flick's own instruction costs on the machine by
  running them against a scratch byte and reading the counter back,
  and re-measures anything no ST could produce, then spends about a
  third of a second - the bottom border flickering - finding where
  the GLUE's border test sits against the counter, which moves from
  boot to boot on real hardware and cannot be a constant). The flick
  is a short 60Hz pulse across the GLUE's border test, restored
  inside line 262 - a restore landing in line 263 leaves the
  Shifter's plane phase a word out for the whole frame on real
  hardware, every colour wrong with every shape in place, and no
  emulator renders that. Measured on a real 16MHz Mega STE (TOS
  2.06): the search took 40 frames, then 300 consecutive frames
  opened with no miss and the same poll count on every one, and the
  picture held solid with the right colours, alone and combined
  with the top. In Hatari the placement lands on the same cycle
  every frame on a plain ST, an STE and the 16MHz Mega STE, with no
  miss over 1500 frames. Not yet run on a real plain ST or STE.
  Resident firmware that holds interrupts off (one cartridge's
  network stack did) makes the ISR late and the border flicker; it
  is not a placement fault. Each border costs an interrupt plus two
  to three lines of polling per frame, under 1% of an 8MHz frame. TOS's 200Hz Timer C handler would hold
  either interrupt off for over two lines at a time (EmuTOS
  measured at ~1140 cycles), so while a border is open its vector
  carries a prefix that lowers the CPU mask to 5 inside the
  handler, letting the border timers nest into it instead of
  firing lines early and spinning; the VBL, which that cannot
  help, is read against a Timer B stopwatch so a late VBL still
  arms Timer A on time.
  Opening both combines automatically into 273 seamless rows, and
  closing one drops back to the other alone; every Open returns
  the height the screen ended up with, so repaint after any
  transition. STDL's blits are kept clear of the flicks by the
  placement above; a program's own hog-mode BLiTTER operation in
  flight across one stretches it past its window - that frame
  shows a border, or its first extra line at 60Hz timing.
  Two things change under the hood while any border is open, both
  because display fetch starts at line 34 instead of 63: palette
  writes are staged and drained inside the blanking by a VBL
  callback (never a mid-frame colour flash; latency one frame), and
  the port should VBL-sync its full-page copies - the CPU loop
  writes rows faster than the beam displays them, so a copy started
  at the VBL stays ahead and cannot tear, where an unsynced one
  races the beam and loses intermittently (measured: constant
  ghosting in one port's cutscenes until synced).
  See `examples/overscan.c`.
- Borders without the library: a program that owns the machine
  (its own VBL and interrupts, its own page flips and colours, maybe
  no C library) compiles `src/overscan_host.c` alone with
  `-Iinclude` and links it instead of `libstdl.a`, never with it. Do
  not compile overscan.c with copies of STDL's internals instead: one
  port did, and a release that gave overscan.c a new hook broke it
  silently; the host object needs only libgcc's arithmetic and STDL's
  build checks that it stays so. `STDL_OverscanHostInit(&host)` (page,
  screen to put back, `_MCH`, BLiTTER yes/no), the usual
  `STDL_Open*Border`/`Close*Border`, and two calls from the program's
  VBL for each of its own flips: `STDL_OverscanPageNext(page)` with
  the write to the video base, `STDL_OverscanPageShown()` a VBL later
  (both first in a VBL that does both). Call them only while a border
  stays open, never across an open or a close. The VBL must count
  `_frclock` ($466); row 0 of each page stays colour 0, since the top
  border's lead line shows it a word out of step.
  `STDL_OverscanRelease()` is the exception handler's: vectors,
  timers and sync rate, nothing else. `examples/ovhost.c` (three
  pages at 50Hz, both borders): 0 misses in 300 frames on ST, STE and
  Mega STE in Hatari, about 400 with either flip call left out.
- Unaligned blits carry the port: a sprite that lands on arbitrary
  x goes through the shift chain, and its four-plane merge is
  hand-written 68000 in blit.c (`blit_merge4`, C twin under the
  `#else`). gcc 4.6 spilled the merged word to the stack once per
  plane; the asm keeps it in registers. Worth 0.9ms/frame in one
  port's gameplay (28.7 -> 29.5 fps on a Mega STE) with output
  verified pixel-identical. Do not expect a bulk `movem` copier to
  help the small stuff: the same port's dirty-block restore and
  screen update move 8-24 bytes per line, where saving and
  restoring twelve registers costs more than the move (measured
  slower on both an STE and a Mega STE, reverted).
- Splash: `STDL_ShowDegas("SPLASH.PI1")` (make with `stdlconv pi1`).
- Keyboard games + joystick: `STDL_JoystickKeyEmulation(1)`, rebindable
  with `STDL_JoystickKeyMapping(up, down, left, right, fire)` keysyms
  (default arrows + left Alt; `STDL_KMOD_JOYSTICK` tagged; check
  the return value = keys resolved).
- Modern controllers work through the same API, with no port
  changes: when an Xpad provider is present, joystick 0 grows to 6
  axes (both sticks, both triggers), 13 buttons and a hat, and the
  axes report real analogue values. Button 0 stays fire and axes 0/1
  stay the left stick, so a port written for a plain ST joystick
  keeps working and a port that asks `SDL_JoystickNumButtons()`
  gets more. Key emulation picks a pad up too.
  `STDL_PadRumble(0, low, high, ms)` pulses its motors (0-255 each,
  -1 when there is no pad or no motors, so call it unconditionally);
  the pump ends the pulse, and STDL_Quit and the terminate path end
  one still running.
- Two players, or up to six: `STDL_SetNumJoysticks(n)` (n up to
  `STDL_MAX_JOYSTICKS`, 6), before or after the mode, returns how
  many there are (`STDL_NumJoysticks`). 1 is the default and what
  every program had before: joystick 0 and the mouse. 2 is two
  joysticks, the mouse port as joystick 1 with the mouse off (the
  IKBD's $12 and $14; Hatari's trace and a probe show both ports
  reporting) - on an ST the mouse port is a mouse or a joystick, so
  any number from 2 turns the mouse off. More are an Xpad provider's
  pads, only where it has the slots, so 4 asked for with no provider
  gives 2. Each joystick's sources are fixed, by
  `STDL_UseJoystickMerge`:
  - merged (1, the default): 0 the joystick port and pad 0, 1 the
    mouse port and pad 1, 2 and 3 pads 2 and 3 - four at most. A port
    and its pad are one joystick, as joystick 0's always were, so
    where pads are present the first two pads are the two joysticks
    and the ports still work beside them;
  - apart (0): 0 the joystick port, 1 the mouse port, 2 to 5 pads 0
    to 3 - six at most, for more players or a port and a pad that are
    two players'. With one joystick it leaves joystick 0 the port
    alone.

  The setting can change at any time: the joysticks re-map at once,
  whatever changes hands is released first, and the count follows (6
  asked for is 4 merged and 6 apart). Events carry the number in
  `which`, and the joystick calls take it first:
  `STDL_GetJoystickState(which)`, `STDL_JoystickInputHeld(which,
  input)`, `STDL_HasPad(which)`, `STDL_PadRumble(which, low, high,
  ms)` (since v1.23.0: "Moving to a newer STDL" below), and the axes
  and the hat as the events carry them, `STDL_GetJoystickAxis(which,
  STDL_JOYSTICK_AXIS_X ...)` and `STDL_GetJoystickHat(which)`
  (`STDL_HAT_*`), 0 where there is no such joystick or pad. A program
  that wants to know when one comes, goes or is re-mapped asks for
  device events, `STDL_UseJoystickDeviceEvents(1)`: ADDED for every
  joystick there is then, and from then `STDL_JOYDEVICEREMOVED` and
  `STDL_JOYDEVICEADDED` (`e.jdevice.which`), each straight after the
  releases it brings, a change undone before the next pump included.
  Off by default, as older programs and the SDL 1.2 shim do not know
  them. The SDL 1.2 shim counts and opens them all; it
  has no hotplug, so after asking, close the sticks and open what
  `SDL_NumJoysticks` says (`examples/testjoystick.c`, keys 1-6 and
  M). Key emulation stays joystick 0's: a second player reads events
  or polls. The smallest two-player program is
  `examples/joyboxes.c` (JOYBOXES): `STDL_SetNumJoysticks(2)`, then
  `STDL_GetJoystickState(0)` and `(1)` move a box each. Asking for 1 again gives the mouse back, as do STDL_Quit,
  exit, abort() and the terminate path - checked in Hatari by a
  program run after each (`tests/hatari/mousebk.c`). The queue holds
  32 events; a pad is read only when there is room for all it could
  say, so none is lost and some come a pump late (a program that
  pumps and polls without taking events still sees its pads move).
  A program that never asks links none of it (every program pays
  about 590 bytes for the hooks, the index, the setting, the queue's
  room and the device events' push; the rest is `joysticks.o`, 1.3K,
  the setting's call `joymerge.o`, 160 bytes, the device events'
  `joydev.o`, 120, and the readers' `joyread.o`, 244).

## Moving to a newer STDL

What a program written against an older STDL changes to build against
a newer one, newest release first. A release that changes a call
already released says so here, with what a caller changes; the build
finds every old use.

### v1.23.0: joysticks, more of them and named so

New, with nothing to change: `STDL_SetNumJoysticks`,
`STDL_NumJoysticks`, `STDL_UseJoystickMerge`,
`STDL_UseJoystickDeviceEvents` with its events `STDL_JOYDEVICEADDED`
and `STDL_JOYDEVICEREMOVED` (`STDL_JoyDeviceEvent`, `e.jdevice`),
`STDL_GetJoystickAxis` with `STDL_JOYSTICK_AXIS_*`,
`STDL_GetJoystickHat` with `STDL_HAT_*`, `STDL_PumpEventsIfDue`
(`STDL_PollEvent` now pumps once a VBL; a drain loop needs no change),
and `STDL_DirtyBackground` (a change to the background put on every
page as it comes round: a program that kept its own list of
background changes for each page can hand them to it).

For more than one joystick (`STDL_SetNumJoysticks`), four calls take
the joystick's number first, and 0 is the one a program had before:

| Before | Now |
|---|---|
| `STDL_GetJoyState()` | `STDL_GetJoystickState(0)` |
| `STDL_JoyInputHeld(i)` | `STDL_JoystickInputHeld(0, i)` |
| `STDL_HavePad()` | `STDL_HasPad(0)` |
| `STDL_PadRumble(l, h, ms)` | `STDL_PadRumble(0, l, h, ms)` |

And the joystick's names say Joystick, not Joy - the key emulation's
calls and every input:

| Before | Now |
|---|---|
| `STDL_JoyKeyEmulation` | `STDL_JoystickKeyEmulation` |
| `STDL_JoyKeyBind` | `STDL_JoystickKeyBind` |
| `STDL_JoyKeyMapping` | `STDL_JoystickKeyMapping` |
| `STDL_JOYKEY_UP` | `STDL_JOYSTICK_UP` |
| `STDL_JOYKEY_DOWN` | `STDL_JOYSTICK_DOWN` |
| `STDL_JOYKEY_LEFT` | `STDL_JOYSTICK_LEFT` |
| `STDL_JOYKEY_RIGHT` | `STDL_JOYSTICK_RIGHT` |
| `STDL_JOYKEY_FIRE` | `STDL_JOYSTICK_FIRE` |
| `STDL_JOYKEY_SOUTH` | `STDL_JOYSTICK_SOUTH` |
| `STDL_JOYKEY_EAST` | `STDL_JOYSTICK_EAST` |
| `STDL_JOYKEY_WEST` | `STDL_JOYSTICK_WEST` |
| `STDL_JOYKEY_NORTH` | `STDL_JOYSTICK_NORTH` |
| `STDL_JOYKEY_TL` | `STDL_JOYSTICK_TL` |
| `STDL_JOYKEY_TR` | `STDL_JOYSTICK_TR` |
| `STDL_JOYKEY_TL2` | `STDL_JOYSTICK_TL2` |
| `STDL_JOYKEY_TR2` | `STDL_JOYSTICK_TR2` |
| `STDL_JOYKEY_SELECT` | `STDL_JOYSTICK_SELECT` |
| `STDL_JOYKEY_START` | `STDL_JOYSTICK_START` |
| `STDL_JOYKEY_MODE` | `STDL_JOYSTICK_MODE` |
| `STDL_JOYKEY_THUMBL` | `STDL_JOYSTICK_THUMBL` |
| `STDL_JOYKEY_THUMBR` | `STDL_JOYSTICK_THUMBR` |
| `STDL_JOYKEY_COUNT` | `STDL_JOYSTICK_INPUTS` (how many inputs, not joysticks: that is `STDL_NumJoysticks`) |

So `STDL_GetJoyState()` becomes `STDL_GetJoystickState(0)` and
`STDL_JoyInputHeld(STDL_JOYKEY_START)` becomes
`STDL_JoystickInputHeld(0, STDL_JOYSTICK_START)`. The events and their
structs keep SDL's names (`STDL_JOYAXISMOTION`, `STDL_JoyAxisEvent`
and the rest), as do the names that said Joystick already
(`STDL_INIT_JOYSTICK`, `STDL_KMOD_JOYSTICK`).

The build finds every one: a renamed input does not compile
("undeclared"), nor does a call with the old arguments ("too few
arguments"); a renamed or removed call (`STDL_JoyKeyEmulation`,
`STDL_HavePad`, ...) gcc 4.6 compiling C only warns about ("implicit
declaration") before the link fails on it ("undefined reference");
build with `-Werror=implicit-function-declaration` to have it at
compile time. A program reading the joystick through the SDL 1.2 shim
(`SDL_Joystick*`) or the SDL2 add-on needs no change.

## Anti-patterns (reject these in review)

- Introducing a chunky buffer + conversion pass: that is c2p and
  breaks the design. Redesign around spans/tiles/sprites. (The SDL2
  add-on, for SDL2 programs on a TT or a Falcon only, is the one
  place that is the design, and it stays out of the core: the
  `stdl-sdl2` skill.)
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
  callback (and a resample, where its format is not the hardware's)
  runs on the CPU inside the pump, and of the voice mixer, which
  mixes in the VBL.
- Lowercase or long filenames in `fopen`/`STDL_LoadBMP`.
- `int * int`, `/` or `%` in anything per pixel, per object or per
  frame: libgcc calls. Use `stdl_math.h` and gate the build with
  `tools/libcalls.sh` (see "Your own arithmetic" above).
- Linking mintlib (`libstdl.a` and the toolchain's default libc)
  without a mintlib-only feature the game uses: about 100K of RAM
  for nothing. Build against libcmini (`libstdl-cmini.a`, porting
  workflow step 2).

## Reference

- `docs/format.md` - byte-level surface/sprite/bank contract
- `docs/porting.md` - SDL->STDL mapping table, rewrite patterns
- `docs/building.md` - building, libcmini or mintlib, link lines, and
  setting up a game's or a fork's repository (STDL as a submodule, a
  Makefile, keeping the desktop build)
- `docs/overscan.md` - ST borders and rasters, and what disturbs a border
- `docs/responsive.md` - 256 colours and 240 rows on a TT or a Falcon
- `docs/limits.md` - non-goals; do not work around them, redesign
- `examples/` - ported SDL 1.2 test programs; `testsprite.c` is the
  reference for the masked-blit + dirty-rect flow, `testpalette.c`
  for the 16-colour palette budget technique, and `chunky.c` for the
  engine-porting APIs (CreateSurfaceFrom's two-views-of-one-block
  pattern, BlitIndexed8 with MARK/UNDER/flips, the STDL_Voice
  sequencer tick, GetHz200)
