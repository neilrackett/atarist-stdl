# How STDL is put together

A map for anyone changing the library itself: where things are and how
the pieces reach each other. The rules, and the measurements behind
them, are in [AGENTS.md](../AGENTS.md); this is what makes them easier
to place. It names mechanisms and groups rather than every file, so that
it stays true when a file is added - `ls src/` is the file list.

## What a program links

The archive links whole objects: m68k-atari-mint has no section garbage
collection, so the object file is the unit a program pays for, and the
library is cut into one primitive, or one family a caller wants all of,
per file.

A link map shows what that buys (`-Wl,-Map`, its "Archive member
included because of" section; 2026-11-07):

- A program that inits, sets a mode, polls events and flips links eight
  objects, 14.8K of text: `video`, `event`, `time`, `palette`, `planes`,
  `stram`, and Xpad's `stdl_xpad` and `xpad`.
- Filling a rectangle and blitting a surface adds `surface`, `draw`,
  `blit` and `blitter`, 29.7K more. Those and `sprite` are the pixel
  path, held to `PIXEL_MAX` by the build's `sizecheck`.
- Everything else is linked only by a program that calls into it. The
  responsive screens' objects have a budget of their own, `RESP_MAX`.

Check a change's effect the same way: link a small program before and
after, and compare the member list and the .TOS size.

## How optional code reaches the core

The core never names an optional module, or it would link it. The
optional module, when opened, sets a function pointer that an object
every program links already defines and calls when it is not NULL. The
pointers are declared in `src/stdl_internal.h`:

| Hook                                          | Set by                                           | Called from                                          |
| --------------------------------------------- | ------------------------------------------------ | ---------------------------------------------------- |
| `stdl_shutdown_*`                             | each module that installs something, as it opens | `release_hardware()` in `video.c`, on every exit     |
| `stdl_audio_hook`, `stdl_timer_hook`, `stdl_cursor_hook` | the ring device, SDL timers (`compat.c`), the cursor | the event pump and the native delays                 |
| `stdl_music_tick`, `stdl_tone_tick`, `stdl_sfx_tick` | music, tones, effects                            | `ym.c`'s VBL slot, in that order                     |
| `stdl_pal_apply_hook`, `stdl_pal_raw_hook`, `stdl_pal_write` | palette staging, the responsive screen           | `palette.c`, `video.c`                               |
| `stdl_blit_policy`, `stdl_ovsc_flip`          | `overscan.c`, while a border is open             | `blitter.c`, `STDL_Flip`                             |
| `stdl_resp_video`, `stdl_resp`, `stdl_resp_bank` | `STDL_UseResponsive`, inline in its header       | the mode set and Flip; every pixel call on an 8-plane surface; the asset loaders |

`STDL_UseResponsive` is `always_inline` so that a constant argument
names only the tables of the options asked for. A new optional feature
follows the same pattern: a pointer in `stdl_internal.h`, defined in an
object already linked, set by the new object.

## From `STDL_Init` to exit

1. **`STDL_Init`** takes supervisor mode (asking `Super(1)` first, since
   `Super(0)` is a toggle) and keeps it until exit. It reads the cookie
   jar for the machine and its sound, switches a Mega STE to 16MHz,
   starts the 200Hz clock, records whether there is a BLiTTER (never a
   Falcon's) and where ST RAM ends, and measures a TT's or a Falcon's
   display rate. It installs the terminate handler and an `atexit`.
2. **`STDL_SetVideoMode`** offers the mode to the responsive screen
   first, if the program asked for one, and otherwise sets ST Low. The
   first page is the desktop's (`Physbase()`); the pages it allocates
   come from `stdl_stram_alloc`, since the Shifter cannot see alt-RAM.
   It takes the keyboard, as does `STDL_OpenInput` for a program that
   sets no mode.
3. **The pump.** `STDL_PollEvent` and the native delays run the
   cooperative services through the hooks above: SDL timers, the audio
   ring's refill, the cursor. Nothing in them runs from an interrupt.
4. **Exit.** `STDL_Quit` or the `atexit` runs `restore_all()`:
   `release_hardware()`, then the frees, then leaving supervisor mode.
   `abort()`, a failed `assert` and a
   TOS exception skip `atexit` and reach Pterm, so the GEMDOS terminate
   vector (`etv_term`, `$408`) calls `release_hardware()` alone: vectors
   and registers only, no GEMDOS and no heap, because the process is
   already being torn down. Shutdown work belongs in that function, on
   the right side of that line.

## Interrupts, and who owns what

The model is cooperative. These are the only things that run from an
interrupt, and each is put back by `release_hardware()`:

| What                                        | Where                                    | Owner         | While                         |
| ------------------------------------------- | ---------------------------------------- | ------------- | ----------------------------- |
| The YM tick: music, tones, effects          | a TOS VBL queue slot                     | `ym.c`        | any of them is playing        |
| `STDL_AddVBL` callbacks                     | VBL queue slots                          | `vbl.c`       | added                         |
| ...including the voice mixer, palette staging, STE scrolling, the rasters' table restart | through `STDL_AddVBL`  | their modules | open                          |
| IKBD packets                                | `ikbdsys`, from `Kbdvbase()`             | `event.c`     | the keyboard is taken         |
| The borders                                 | Timers A and B; prefixes on the VBL (`$70`) and Timer C | `overscan.c`  | a border is open              |
| Rasters                                     | Timer B; a prefix on Timer C             | `raster.c`    | rasters are open              |
| TT console output                           | the `xconout` vectors (`$57E`)           | `responsive.c`| a responsive screen on a TT   |
| Termination                                 | `etv_term`, through `Setexc(0x102)`      | `video.c`     | from init to exit             |

Timer B has one owner at a time, `stdl_timerb_owner`, and the borders
and rasters each refuse to open while the other holds it. The Timer C
prefixes only lower the CPU mask inside TOS's 200Hz handler, so the
line-locked timers can interrupt it.

## Where drawing chooses its path

- **8 planes or 4.** Every public pixel call tests the surface's
  `planes` and hands an 8-plane one to the responsive table
  (`planes8*.c`). On an ST that is a compare and a branch not taken.
- **The plane budget** (`planes.c`): how many of the four planes the
  paths maintain.
- **CPU or BLiTTER.** Only where `stdl.mach.has_blitter` is set and the
  program allows it: an inline test, then `stdl_blitter_wants`
  (`blitter.c`, out of line), whose size rules `tests/hatari/blitcost.c`
  fits. The CPU keeps what the beam would show between passes on a
  single-buffered screen (`stdl_drawn_visibly`), and surfaces beyond
  ST RAM. With a border open, `stdl_blit_policy` places each operation
  against the beam.
- **The composition flags** (`UNDER`, `MARK`) exclude the fast paths,
  the whole-group copies and every BLiTTER pass, rather than teaching
  them the flag.
- **Assembly** sits under `#ifdef __m68k__` with its C twin as the
  `#else`; the host builds the twin, and a CHK runner compares the two
  on the machine.

## How it is checked

- **`tests/host/`** (`make test`) compiles the sources natively with
  ASan, `stdl_internal.h` stubbing the hardware. The BLiTTER runs there
  against a model of the chip (`blitmodel.c`), and the sound tick
  against a register-file stand-in.
- **`tests/hatari/`** has the on-target runners: the CHK programs run
  the host suites, or compare the asm with its C twin, on the machine;
  the cost probes time paths (`sprcost.c`, `blitcost.c`); and `run.sh`
  drives any program in Hatari.
- **The cross build** fails on `sizecheck`, on a new libgcc arithmetic
  call (`libcallcheck`, `tools/libcalls.sh`), and on `overscan.c`
  growing a call into the rest of the library (`ovhostcheck`).
- **CI** runs `make ci`: the host suites, the cross build, the libcmini
  archive, the SDL2 add-on and the bundle.

## Apart from the core

- **`src/overscan_host.c`** builds `overscan.c` a second way, for a
  program that runs the machine itself and links neither the library
  nor a C library ([overscan.md](overscan.md)).
- **`addons/sdl2/`** is the SDL2 add-on. It has its own make targets
  and flags, reaches STDL through public headers only, and leaves every
  core object unchanged ([sdl2.md](sdl2.md), and AGENTS.md's section on
  it).
- **`lib/xpad/`** is a submodule. The library takes its consumer half,
  and nothing under `lib/` is edited here.
