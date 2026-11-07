# STDL examples

`make` builds these into `dist/` with the assets they load, and each
release ships them as `stdl-examples.zip` on the
[release page](https://github.com/neilrackett/atarist-stdl/releases/tag/latest).
They all run under EmuTOS or TOS in Hatari; `dist/` doubles as its
GEMDOS drive:

```
hatari --machine ste dist/TSPRITE.TOS
```

Each source file's header comment says what it shows and which keys it
takes. The examples are dedicated to the public domain (CC0,
[LICENSE](LICENSE)), so take whatever is useful as a starting point.

## Starting a new game

| Program     | Source      | Exercises                                                                  |
| ----------- | ----------- | -------------------------------------------------------------------------- |
| STARTER.TOS | `starter.c` | the shape of a game to copy: one screen of platforms, the art from a bank, a background put back under the sprite by `STDL_Dirty`, three pages, a fixed 50Hz step, joystick or keys, YM music and effects. 50 frames a second on a plain ST in Hatari |

Its art is three indexed PNGs in `art/` sharing one 16-colour palette,
and its music `assets/DEMO.MID`. `make starter-art`, run on the host
(the bank needs Python 3 with Pillow), makes `assets/STARTER.BNK` and
`assets/DEMO.STM` from them with `stdlconv`: the Makefile's rule is the
pipeline to copy into a game's own Makefile. The results are tracked, so
`make` alone never needs Python.

## Ported from SDL 1.2's tests

| Program      | Source            | Exercises                                                                        |
| ------------ | ----------------- | -------------------------------------------------------------------------------- |
| TBITMAP.TOS  | `testbitmap.c`    | 1bpp expansion, band fills, mouse events                                         |
| GRAYWIN.TOS  | `graywin.c`       | fills, clipping, palette                                                         |
| TESTWIN.TOS  | `testwin.c`       | BMP loading, surface blits, palette fades                                        |
| TSPRITE.TOS  | `testsprite.c`    | colour-keyed sprite blits, dirty rects, FPS                                      |
| TPALETTE.TOS | `testpalette.c`   | palette animation on a 16-entry budget; `-stage` lands each change in the blank  |
| CHECKKEY.TOS | `checkkeys.c`     | raw IKBD keyboard, no video mode: key-ups, modifiers, unicode                    |
| TTIMER.TOS   | `testtimer.c`     | 200Hz timing, cooperative timers                                                 |
| TBLITSPD.TOS | `testblitspeed.c` | blit throughput baseline                                                         |
| TVIDINFO.TOS | `testvidinfo.c`   | capability report + fill/blit/flip benchmarks                                    |
| TKEYS.TOS    | `testkeys.c`      | keysym name table dump                                                           |
| TJOY.TOS     | `testjoystick.c`  | joystick port 1 as SDL joystick 0; 6 axes/13 buttons when an Xpad pad is present, shoulders rumble; keys 1-6 ask for that many joysticks (`STDL_SetNumJoysticks`: 2 is the mouse port as stick 1, more are Xpad pads), M merges or parts the pads (`STDL_UseJoystickMerge`: apart, pads 0-3 are sticks 2-5), each stick drawn in its own colour |
| LOOPWAVE.TOS | `loopwave.c`      | STE/Mega STE DMA sample playback (STDL_Audio)                                    |
| TCURSOR.TOS  | `testcursor.c`    | software mouse cursor with save-under                                            |
| PLAYMUS.TOS  | `playmus.c`       | YM music (stdlconv midi -> STDL_Music) + sample chunks via the SDL_mixer shim    |

## STDL's own

| Program      | Source        | Exercises                                                                        |
| ------------ | ------------- | -------------------------------------------------------------------------------- |
| JOYBOXES.TOS | `joyboxes.c`  | two joysticks, two boxes: `STDL_SetNumJoysticks(2)` makes the mouse port joystick 1, `STDL_GetJoystickState(0)` and `(1)` move a box each, fire lights it; the mouse comes back at exit |
| CHUNKY.TOS   | `chunky.c`    | engine-porting APIs: chunky sprites through a colour map, a borrowed playfield, priority masks, four-voice music |
| SFXDEMO.TOS  | `sfxdemo.c`   | Degas splash, YM effects stealing/restoring music voices, joystick key emulation |
| TONEDEMO.TOS | `tonedemo.c`  | live notes on the three YM voices: a fourth note displacing and returning        |
| OPLDEMO.TOS  | `opldemo.c`   | an OPL2 register stream (the IMF games' music format) replayed as YM notes       |
| MUSICVU.TOS  | `musicvu.c`   | YM music from memory: VU meter from the stream's registers, position, seeking    |
| POLYDEMO.TOS | `polydemo.c`  | convex polygons XORed into single planes, a glenz look; T: triple buffering      |
| PATDEMO.TOS  | `patdemo.c`   | pattern-filled polygons: Bayer dither shading, a screen door, sub-pixel motion   |
| HWSCROLL.TOS | `hwscroll.c`  | STE hardware scrolling: a 512x320 field scrolled to the pixel, nothing copied    |
| OVERSCAN.TOS | `overscan.c`  | border overscan: T top (228), B bottom (245), both combine (273), SPACE closes   |
| OVHOST.TOS   | `ovhost.c`    | both borders in a program that owns the machine: its own VBL, three pages, no libstdl.a |
| RASTERS.TOS  | `rasters.c`   | Timer B rasters: copper bars in colour 0 edge to edge, a gradient in colour 15   |
| RESPDEMO.TOS | `respdemo.c`  | one binary, each machine's best screen: 256 colours and 240 rows on a TT or Falcon |
| SDL2DEMO.TOS | `sdl2demo.c`  | (`make sdl2`) the SDL2 add-on on a TT or a Falcon: INDEX8 textures, palette cycling, copying what changed, a box made in a surface and moved by the keys, a pad or the mouse, with a tone |

## Checks

| Program     | Source      | Exercises                                                                  |
| ----------- | ----------- | -------------------------------------------------------------------------- |
| BLITCHK.TOS | `blitchk.c` | BLiTTER vs CPU byte-identical verification at three plane budgets + timing |
| VBLCHK.TOS  | `vblchk.c`  | 50Hz VBL callbacks, then an abnormal exit - the desktop coming back is the pass |

## Adding one

An example added to the Makefile's `EXAMPLES` list is built by `make`
and ships on the release page; add its row here too. `chunky.c` is the
shape to copy: small, the intended usage rather than every parameter,
and a comment saying why the pattern is the right one. An example whose
assets are made from sources gets a host-only rule like `starter-art`,
with the results tracked in `assets/`. An example that runs to its end
by itself, rather than until a key or Escape, calls `hold_on_exit(NULL)`
(`hold.h`) first thing in `main`: on a real machine it would otherwise
drop back to the desktop and take what it printed with it.
