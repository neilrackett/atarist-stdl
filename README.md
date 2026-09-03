<p align="center">
  <img src="docs/img/stdl-logo.png" alt="STDL - Atari ST DirectMedia Layer" width="480">
</p>

# STDL - Atari ST DirectMedia Layer

An easier way to port SDL games to the Atari ST, by [Neil Rackett](https://neilrackett.com/atarist).

## Introduction

STDL is a planar-native library for porting SDL 1.2 games to the Atari ST.

### Why not just use SDL?

SDL 1.2 is great for TT and Falcon, which both support chunky at 256 colours,
but on a stock ST there's no chunky mode and SDL needs c2p on every frame.
The framework overhead therefore means you'll maybe get 5-6fps if you're lucky,
and even then there's no colour support and everything's greyscale.

STDL is 16 colours, four bitplanes, and no conversion step. 50-60fps is truly
achievable.

### What STDL does differently

There is no chunky backbuffer and no c2p anywhere in the pipeline: ST
interleaved planar is the native format, assets are converted offline, and
spans, rects and blits are the primary verbs.

For engines that decode byte-per-pixel sprite frames at runtime,
`STDL_BlitIndexed8` draws chunky frames straight into the planes through a
colour map, (one pass, no backbuffer) `STDL_CreateSurfaceFrom` wraps
engine-owned framebuffers as surfaces, `STDL_BlitSurfaceEx` composes planar
frames against a foreground/priority plane, and `STDL_Voice` hardware-mixes
four sample voices from the VBL for module-style music.
See `examples/chunky.c`.

Modern controllers work through the ordinary SDL joystick API: when an
[Xpad](https://github.com/neilrackett/atarist-xpad) provider is present
(a SidecarTridge running MD/Sidepad, for example), joystick 0 grows to
six axes, thirteen buttons and a hat with real analogue values, and the
joystick key emulation picks the pad up too. Ports need no changes:
button 0 stays fire, axes 0 and 1 stay the left stick, and with no
provider everything reads exactly as a plain ST joystick.

`STDL_OpenTopBorder` trades the top border for 28 extra lines - 228
visible instead of 200 on any ST, one timer interrupt per frame and
no cycle counting - so a game whose world is taller than 200 lines shows
it whole instead of cropping or squashing. `STDL_OpenBottomBorder` opens
the other end (245 lines, seamless: the flick that fools the GLUE is
placed from the Shifter's video counter with loops calibrated against
real scanlines when the border opens, so it lands on any CPU speed),
and opening both combines them automatically into a 273-row screen
filling the display edge to edge. While any border is open, each
BLiTTER operation is placed from the beam's position so it ends before
the border's timing window, or is split around it, so a blit never
holds the CPU off the border and throughput stays within 14-28% of
the no-border figure; palette writes are staged to the vertical
blanking so they can never flash mid-frame. See `examples/overscan.c`.

Anything that cannot be done cheaply in planar has been removed rather than
emulated; see [docs/limits.md](docs/limits.md).

## Layout

| Path              | Contents                                                                            |
| ----------------- | ----------------------------------------------------------------------------------- |
| `include/stdl/`   | public headers, one per module                                                      |
| `include/compat/` | SDL.h and SDL_mixer.h - the SDL 1.2 compatibility shims                             |
| `src/`            | video, surface, surfacefrom, draw, blit, blit8, blitter, planes, palette, event,    |
|                   | cursor, time, dirty, vbl, sprite, drawchar, indexed, asset, bmp, degas, audio,      |
|                   | voice, music, sfx, ym, compat, mixer                                                |
| `tools/stdlconv/` | asset converter: image quantise + planar, sprite/tile/font banks, Degas PI1,        |
|                   | WAV (incl. MS ADPCM) to STE DMA rates, MIDI to YM music, C-array embedding          |
| `examples/`       | ported SDL 1.2 test programs + original STDL demos and their assets (public domain) |
| `docs/`           | format.md (the contract), porting.md, limits.md, design-overview.md                 |
| `.claude/skills/` | the `stdl` porting skill (auto-discovered by Claude Code in this repo)              |
| `dist/`           | build output: .TOS binaries + copied assets (untracked; doubles as a GEMDOS drive)  |

## Building

The easiest way is to cross-compile with `m68k-atari-mint-gcc` using
[atarist-toolkit-docker](https://github.com/sidecartridge/atarist-toolkit-docker):

```
stcmd make
```

This produces `libstdl.a` in the project root and all of the example programs
in `dist/`, which doubles as a Hatari GEMDOS drive for testing:

```
hatari --machine megaste dist/TSPRITE.TOS
```

Clone with `--recursive`, or run `git submodule update --init`: Xpad is
a submodule at `lib/xpad`, and the library does not build without it.

**If your game's Makefile decides when to rebuild `libstdl.a` by
globbing STDL's sources, glob both directories.** Not every source is
under `src/` any more:

```make
STDL_SRCS = $(wildcard $(STDL)/src/*.c) $(wildcard $(STDL)/lib/xpad/src/*.c)
```

A glob that only sees `src/` will not notice an Xpad submodule bump, so
it will happily link an archive built against the previous ABI. Objects
also build into `$(STDL)/obj/` now rather than beside the sources, so
anything looking for `$(STDL)/src/*.o` wants `$(STDL)/obj/src/*.o`.
An earlier build may have left orphaned `.o` files next to the sources;
`make clean` no longer removes those, so delete them once by hand.

## Status

320x200, 4 planes, 16 colours. The library and the SDL 1.2
test-suite ports below all run under EmuTOS/TOS on Hatari:

| Example      | Exercises                                                                        |
| ------------ | -------------------------------------------------------------------------------- |
| TBITMAP.TOS  | 1bpp expansion, band fills, mouse events                                         |
| GRAYWIN.TOS  | fills, clipping, palette                                                         |
| TESTWIN.TOS  | BMP loading, surface blits, palette fades                                        |
| TSPRITE.TOS  | colour-keyed sprite blits, dirty rects, FPS                                      |
| TPALETTE.TOS | palette animation on a 16-entry budget                                           |
| CHECKKEY.TOS | raw IKBD keyboard: key-ups, modifiers, unicode                                   |
| TTIMER.TOS   | 200Hz timing, cooperative timers                                                 |
| TBLITSPD.TOS | blit throughput baseline                                                         |
| TVIDINFO.TOS | capability report + fill/blit/flip benchmarks                                    |
| TKEYS.TOS    | keysym name table dump                                                           |
| TJOY.TOS     | joystick port 1 as SDL joystick 0; 6 axes/13 buttons when an Xpad pad is present |
| LOOPWAVE.TOS | STE/Mega STE DMA sample playback (STDL_Audio)                                    |
| TCURSOR.TOS  | software mouse cursor with save-under                                            |
| PLAYMUS.TOS  | YM music (stdlconv midi -> STDL_Music) + DMA chunks via the SDL_mixer shim       |
| SFXDEMO.TOS  | Degas splash, YM effects stealing/restoring music voices, joystick key emulation |
| BLITCHK.TOS  | BLiTTER vs CPU byte-identical verification at two plane budgets + timing         |
| VBLCHK.TOS   | 50Hz VBL callbacks, then an abnormal exit - the desktop coming back is the pass  |
| OVERSCAN.TOS | border overscan: T top (228), B bottom (245), both combine (273), SPACE closes   |

Large same-phase fills and blits are BLiTTER-accelerated where the
hardware has one (fills 16.7 -> 50 FPS, aligned blits 8.3 -> 25 FPS
on an emulated Mega STE); the CPU paths remain the correctness
reference, verified byte-identical on target by BLITCHK.TOS.

Games that do not need 16 colours can say so:
`STDL_SetPlaneBudget(2)` promises no colour index above 3 and every
primitive - fills, spans, blits, sprites, tiles, text, the XOR ops
and the BLiTTER passes - stops maintaining the top two bitplanes.
Measured with BLITCHK.TOS: 1.4x on a stock ST, 1.9x with a
BLiTTER. The default is 4 planes and is bit-for-bit unchanged.

## Ports

The API has been proven and optimised against three formerly SDL-based
games, each in its own repository:

| Game                                                             | What it proved                                                                   |
| ---------------------------------------------------------------- | -------------------------------------------------------------------------------- |
| [FreeNukum](https://downloads.neilrackett.com/atarist-freenukum) | Replaced a bespoke planar shim; renders pixel-identically                        |
| [Sopwith](https://downloads.neilrackett.com/atarist-sopwith)     | STDL backend built beside its hand-written native ST one, to compare performance |
| [Koules](https://downloads.neilrackett.com/atarist-koules)       | 256-colour SVGALIB game, ported to 16 colours and fixed-point physics            |

Sopwith was used as a benchmark, being the only one with a
hand-optimised native ST backend to measure against. The STDL backend
began roughly 1.6x slower than native code on a stock 8MHz ST and is
now around 1.3x.

That is the useful part: the XOR primitives, the batched span and
point primitives, the plane budget and `STDL_PlaySample` all exist
because a real game needed them and profiling said so. Koules also
gave `STDL_Dirty` its first real workout, which was the point of
choosing it.

The gaps the three ports worked around became the rest of the API:
`STDL_SurfaceFromIndexed8` (all three hand-rolled a chunky-to-planar
decoder), `STDL_RemapSurface` (Sopwith recolours every symbol per
faction), `STDL_DrawChar` (Sopwith builds a one-character string 350
times a frame) and `STDL_AddVBL` (Sopwith pokes the TOS VBL queue at
$456 directly, because STDL claimed a slot for its own sound tick
and never offered one). The last of those also fixed a crash: a
program that dies without running `atexit` - a failed `assert`, an
`abort`, a bus error - used to leave a VBL queue entry pointing into
memory GEMDOS had already handed to somebody else, which turned an
out-of-memory into a machine panic one frame later. STDL now cleans
up from the GEMDOS terminate vector, whichever way the program goes.

Because the linker's granularity on m68k-atari-mint is the object
file, each of those lives in its own translation unit and costs
nothing to a program that does not call it: adding all four, and
moving `STDL_SurfaceFrom1bpp` in beside them, made every one of the
three games _smaller_.

Two findings worth repeating for anyone porting: gcc 4.6 turns
`y * stride` into a `__mulsi3` call costing ~270 cycles, which is
more than the pixel work in a short span; and the `SDL_mixer` shim
software-mixes, which measured at 36-75% of an 8MHz CPU - use
`STDL_PlaySample` for effects that matter.

## Porting with an LLM

The repo ships a porting guide written for AI agents
([.claude/skills/stdl/SKILL.md](.claude/skills/stdl/SKILL.md)): the surface
format contract, API costs, SDL-to-STDL rewrite patterns, stdlconv usage and
the Hatari test loop. It is plain markdown, so it works with any agent:

- **Claude Code** discovers it automatically in this repo (`/stdl`); when
  porting a game in its own repository, symlink it in or install user-wide:

  ```
  mkdir -p path/to/game/.claude/skills
  ln -s /path/to/atarist-stdl/.claude/skills/stdl path/to/game/.claude/skills/stdl
  ```

- **Other agents** (Codex, Cursor, Gemini CLI, ...): reference the file from
  the game repo's AGENTS.md or rules, or paste it into context.

## Licence

Library, headers, converter and build files are licensed under the
[GNU Lesser General Public License version 2.1 (LGPL-2.1)](https://www.gnu.org/licenses/old-licenses/lgpl-2.1.en.html);
see [LICENSE](LICENSE).

Examples, both ports from SDL 1.2 and original STDL demos, are dedicated to the
public domain under
[Creative Commons Zero 1.0 Deed (CC0-1.0)](https://creativecommons.org/publicdomain/zero/1.0/deed.en),
so they can be used as starting points without licence concerns; see
[examples/LICENSE](examples/LICENSE).
