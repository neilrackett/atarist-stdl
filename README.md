<p align="center">
  <img src="docs/img/stdl-logo.png" alt="STDL - Atari ST DirectMedia Layer" width="480">
</p>

# STDL - Atari ST DirectMedia Layer

An easier way to create games for the Atari ST, TT & Falcon,
by [Neil Rackett](https://neilrackett.com/atarist).

## Introduction

STDL is a planar-native library for creating high performance games and porting
SDL content to the Atari ST, TT & Falcon.

It enables you to create a single binary that works on any computer in the
Atari ST family, with optional progressive enhancement baked into its core:

- Create games that run in ST low res on any ST-compatible computer
- Add 256 colour graphics for enhanced experiences on TT and Falcon
- Expand to 240 rows on TT and Falcon, or on every machine using ST overscan

### Why not just use SDL 1.2?

If you only want to port SDL 1.2 (the most recent version for the Atari ST
family) content to TT and/or Falcon, it's a good option, but on a stock ST
it's just not viable: c2p and the framework overhead mean it's slow and uses
around half the RAM on a 512KB ST, and it only works in greyscale.

With STDL, full colour at 50-60fps on a 520 ST is truly achievable.

### SDL2 add-on for TT and Falcon

Porting SDL2 games to a stock TT or Falcon? STDL's optional SDL2 add-on
(`addons/sdl2/`, [docs/sdl2.md](docs/sdl2.md)) makes it easy, drawing
256-colour textures on STDL's responsive screen and converting only what's
changed each frame. The art comes in through SDL2's own `SDL_LoadBMP`, from
8-bit BMPs that `stdlconv bmp8` makes sharing the screen's one palette.

It is the one place STDL is chunky, it is never part of the core library, and
a program gets it only by asking for it.

## What STDL does differently

There is no chunky backbuffer and no c2p anywhere in the core library's
pipeline: interleaved planar is the native format, assets are converted
offline, and spans, rects and blits are the primary verbs. Anything that
cannot be done cheaply in planar has been removed rather than emulated; see
[docs/limits.md](docs/limits.md).

The target is a plain 8MHz ST. Everything above it - the BLiTTER, DMA sound,
STE hardware scrolling, a Mega STE's 16MHz, and a TT's or a Falcon's 256
colours for a program that asks - is detected at run time and used where
present, so one binary serves every machine. Each primitive is its own object file, so a program links only what
it calls.

- **Drawing**: fills, spans, blits, sprites, tiles and text, BLiTTER-accelerated
  where the machine has one and verified byte-identical with the CPU paths.
  `STDL_SetPlaneBudget(2)` stops maintaining planes a game never uses (1.4x on
  a stock ST, 1.9x with a BLiTTER). Convex polygons, flat or pattern-filled
  (`STDL_FillPolygon`, `STDL_FillPolygonPattern`), for flat-shaded 3D, glenz
  and dithered shading.
- **Engine ports**: `STDL_BlitIndexed8` draws byte-per-pixel frames straight
  into the planes through a colour map, `STDL_CreateSurfaceFrom` wraps an
  engine's own framebuffers, and `STDL_BlitSurfaceEx` composes against a
  priority plane. See `examples/chunky.c`.
- **Display**: 256 colours and 240 rows on a TT or a Falcon
  ([docs/responsive.md](docs/responsive.md)); on an ST, the top and bottom
  borders open for up to 273 rows and Timer B rasters change colours down the
  picture ([docs/overscan.md](docs/overscan.md)). STE hardware scrolling to the
  pixel (`STDL_SetScrollOrigin`), 50 or 60Hz (`STDL_SetRefresh`), and palette
  changes held for the vertical blank (`STDL_StagePalette`).
- **Sound**: YM music converted from MIDI or YM2151 VGM, played from memory,
  seekable and readable for meters; YM effects and live notes; and on an STE,
  DMA samples and four mixed sample voices for module-style music
  (`STDL_Voice`, which the SDL_mixer shim plays its chunks on).
- **Input**: keyboard, mouse and joystick through SDL's own API. Modern
  controllers work through [Xpad](https://github.com/neilrackett/atarist-xpad)
  (a SidecarTridge running MD/Sidepad, for example) with analogue axes,
  thirteen buttons and rumble, and ports need no changes. Two players,
  or up to six with pads, on `STDL_SetNumJoysticks`. `STDL_OpenInput`
  takes the input without setting a video mode, for tools.
- **68000 arithmetic**: `<stdl/stdl_math.h>` is the 68000's own 16-bit
  multiply and divide, because gcc 4.6 turns any product it cannot prove
  16-bit into a `__mulsi3` call eight times the price; `tools/libcalls.sh`
  lists every such call in a program's objects.
- **Clean exits**: VBL slots, vectors, palette, resolution and sound are put
  back however the program ends - `abort()`, a failed `assert` or a bus error
  included.

## Documentation

| Doc                                                | For                                                                       |
| -------------------------------------------------- | ------------------------------------------------------------------------- |
| [docs/building.md](docs/building.md)               | building the library, linking a game, setting up a game's or a fork's repository |
| [docs/porting.md](docs/porting.md)                 | porting SDL 1.2 code: workflow, what maps, rewrite patterns, ST gotchas   |
| [docs/format.md](docs/format.md)                   | the byte-level contract: surfaces, masks, sprites, banks, music           |
| [docs/limits.md](docs/limits.md)                   | what STDL will not do, and why                                            |
| [docs/responsive.md](docs/responsive.md)           | 256 colours and 240 rows on a TT or a Falcon                              |
| [docs/overscan.md](docs/overscan.md)               | borders and rasters on an ST                                              |
| [docs/sdl2.md](docs/sdl2.md)                       | the SDL2 add-on for a TT or a Falcon                                      |
| [examples/README.md](examples/README.md)           | the example programs                                                      |
| [docs/internals.md](docs/internals.md)             | how the library is put together, for contributors                         |
| [docs/design-overview.md](docs/design-overview.md) | the original design sketch, kept for its reasoning                        |

## Layout

| Path              | Contents                                                                                       |
| ----------------- | ---------------------------------------------------------------------------------------------- |
| `include/stdl/`   | public headers, one per module                                                                 |
| `include/compat/` | `SDL.h` and `SDL_mixer.h`, the SDL 1.2 compatibility shims                                     |
| `src/`            | the library, one primitive or family per file                                                  |
| `lib/xpad/`       | the Xpad controller submodule                                                                  |
| `addons/sdl2/`    | the optional SDL2 add-on, built by `make sdl2`                                                 |
| `tools/`          | `stdlconv`, the asset converter (images, banks, Degas, WAV, MIDI and VGM to YM), and `libcalls.sh` |
| `examples/`       | ported SDL 1.2 tests and STDL demos, with their assets (public domain)                         |
| `tests/`          | host tests (`make test`) and on-target runners for Hatari                                      |
| `.claude/skills/` | the porting skills (below)                                                                     |
| `dist/`           | build output: .TOS binaries and assets (untracked; doubles as a GEMDOS drive)                  |

## Building

```
git clone --recursive https://github.com/neilrackett/atarist-stdl
cd atarist-stdl
stcmd make
```

This cross-compiles `libstdl.a` and the examples into `dist/` with
[atarist-toolkit-docker](https://github.com/sidecartridge/atarist-toolkit-docker),
and `dist/` doubles as a Hatari GEMDOS drive:

```
hatari --machine ste dist/TSPRITE.TOS
```

**Link your game against libcmini, not mintlib, unless it needs mintlib** -
it is about 100K smaller. [docs/building.md](docs/building.md) has the link
lines, the SDL2 add-on's build, the tests, and what a game's Makefile needs
to know.

## Examples

The examples - SDL 1.2's own tests ported, demos of STDL's features and two
on-target checks - are built into `dist/` and ship as
`stdl-examples.zip` on the
[release page](https://github.com/neilrackett/atarist-stdl/releases/tag/latest).
All run under EmuTOS or TOS in Hatari; [examples/README.md](examples/README.md)
lists them. A new game starts best from `STARTER.TOS`
([examples/starter.c](examples/starter.c)): one screen of platforms built
the way a plain ST wants, with its art and music pipeline in the Makefile.

## Ports

The API has been proven and optimised against multiple formerly SDL-based
games, each in its own repository:

| Game                                                             | What it proved                                                                   |
| ---------------------------------------------------------------- | -------------------------------------------------------------------------------- |
| [FreeNukum](https://downloads.neilrackett.com/atarist-freenukum) | Replaced a bespoke planar shim; renders pixel-identically                        |
| [Sopwith](https://downloads.neilrackett.com/atarist-sopwith)     | STDL backend built beside its hand-written native ST one, to compare performance |
| [Koules](https://downloads.neilrackett.com/atarist-koules)       | 256-colour SVGALIB game, ported to 16 colours and fixed-point physics            |

Sopwith was used as a benchmark, being the only one with a hand-optimised
native ST backend to measure against. The STDL backend began roughly 1.6x
slower than native code on a stock 8MHz ST and is now around 1.3x.

Much of the API exists because one of these games needed it and profiling
said so: the XOR primitives, batched spans and points, the plane budget,
`STDL_PlaySample`, `STDL_SurfaceFromIndexed8`, `STDL_RemapSurface`,
`STDL_DrawChar` and `STDL_AddVBL`.

## Porting with an LLM

The repo ships a guide to porting and writing games, written for AI agents
([.claude/skills/stdl/SKILL.md](.claude/skills/stdl/SKILL.md)): whether a
program fits, the surface format contract, API costs, SDL-to-STDL rewrite
patterns, starting a new game, stdlconv usage and the Hatari test loop. An SDL2 program for a TT or a Falcon has its own,
[.claude/skills/stdl-sdl2/SKILL.md](.claude/skills/stdl-sdl2/SKILL.md), for
the SDL2 add-on. They are plain markdown, so they work with any agent:

- **Claude Code** discovers it automatically in this repo (`/stdl`); when
  porting a game in its own repository, symlink it in or install user-wide:

  ```
  mkdir -p path/to/game/.claude/skills
  ln -s /path/to/atarist-stdl/.claude/skills/stdl path/to/game/.claude/skills/stdl
  ln -s /path/to/atarist-stdl/.claude/skills/stdl-sdl2 path/to/game/.claude/skills/stdl-sdl2
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
