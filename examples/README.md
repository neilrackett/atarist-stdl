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
| TJOY.TOS     | `testjoystick.c`  | joystick port 1 as SDL joystick 0; 6 axes/13 buttons when an Xpad pad is present, shoulders rumble |
| LOOPWAVE.TOS | `loopwave.c`      | STE/Mega STE DMA sample playback (STDL_Audio)                                    |
| TCURSOR.TOS  | `testcursor.c`    | software mouse cursor with save-under                                            |
| PLAYMUS.TOS  | `playmus.c`       | YM music (stdlconv midi -> STDL_Music) + sample chunks via the SDL_mixer shim    |

## STDL's own

| Program      | Source        | Exercises                                                                        |
| ------------ | ------------- | -------------------------------------------------------------------------------- |
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
and a comment saying why the pattern is the right one.
