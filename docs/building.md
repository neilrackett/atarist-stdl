# Building STDL and linking a game

## The library and the examples

STDL cross-compiles with `m68k-atari-mint-gcc` 4.6.4, most easily in
[atarist-toolkit-docker](https://github.com/sidecartridge/atarist-toolkit-docker):

```
git clone --recursive https://github.com/neilrackett/atarist-stdl
cd atarist-stdl
stcmd make                  # STCMD_NO_TTY=1 stcmd make in scripts and CI
```

Clone with `--recursive`, or run `git submodule update --init`
afterwards: Xpad is a submodule at `lib/xpad`, and the library does not
build without it.

`make` produces `libstdl.a` in the project root and the example
programs in `dist/`, with the assets they load
([examples/README.md](../examples/README.md)). `dist/` doubles as a
Hatari GEMDOS drive:

```
hatari --machine ste dist/TSPRITE.TOS
```

Other targets:

| Target        | Builds                                                                                                |
| ------------- | ----------------------------------------------------------------------------------------------------- |
| `make cmini`  | `libstdl-cmini.a`, the library against libcmini's headers (below)                                   |
| `make sdl2`   | the SDL2 add-on, only when asked: `libstdl-sdl2.a`, `libstdl-sdl2-cmini.a` and `dist/SDL2DEMO.TOS` ([sdl2.md](sdl2.md)) |
| `make sdl1`   | the experimental SDL1 add-on, only when asked: `libstdl-sdl1.a`, the SDL2 add-on's 68000 build it draws through, and SDL 1.2's own tests as `dist/S1*.TOS` ([sdl1.md](sdl1.md)) |
| `make test`   | the host test suite, natively with ASan: no cross toolchain needed                                    |
| `make libstdl-arith020.o` | the object a 68000 program links for a 68020's multiply and divide where it has one (`make` builds it too; below) |
| `arith020.mk` | not a target: `STDL_ARITH020`, that object and the link flags that go with it (below) |
| `make bundle` | `stdl-examples.zip`, the example binaries and their assets, as on the release page                    |
| `make ci`     | everything CI runs, in order; run it on the host, not inside the container                            |

## Linking a game: libcmini or mintlib

**Link your game against libcmini, not mintlib, unless it needs
mintlib.** The examples link mintlib, the toolchain's default, but a
game that uses stdio, stdlib, string and the GEMDOS/XBIOS bindings and
nothing of MiNT's own (processes, signals, sockets) is about 100K
smaller on libcmini - one port went from 242,624 bytes to 141,551, and
on a 512K ST from 23K free after loading to 107K. Build STDL against
libcmini's headers as well, since `libstdl.a` will not mix with it:

```
stcmd make libstdl-cmini.a
m68k-atari-mint-gcc -nostdlib -L$CMINI/lib -Wl,--traditional-format \
    -o GAME.TOS $CMINI/lib/crt0.o objs... libstdl-cmini.a -lcmini -lgcc
```

Compile the game's own sources with `-I$CMINI/include` (`CMINI` is
`/freemint/libcmini` in the toolchain container), and set its stack in
the program (`long _stksize = 32768;`), which libcmini's start-up reads
in place of the link's `--stack`. A port moving from mintlib has checks
to make before switching for good: step 2 of the
[porting workflow](porting.md#workflow).

A game that does need mintlib links `libstdl.a` with the toolchain's
defaults, as the examples do:

```
m68k-atari-mint-gcc -o GAME.TOS objs... libstdl.a
```

Either way, compile the game with STDL's `include/` on the include path,
and a ported SDL 1.2 program with `include/compat` as well.

## A 68020's multiply and divide in a 68000 program

gcc 4.6 compiles every 32-bit multiply and divide for the 68000 as a
call of libgcc's `__mulsi3`, `__udivsi3`, `__divsi3`, `__umodsi3` or
`__modsi3`, and 64-bit ones as `__muldi3`, `__divdi3` and the rest, on
a TT or a Falcon as on an ST. A program built for the whole ST family
can have the 68020's own instructions on the machines that have them
without a second build: link it with `STDL_ARITH020`, from
`arith020.mk`, before the libraries.

```make
include $(STDL)/arith020.mk

$(TARGET): $(OBJS) $(STDL_LIB)
	$(CC) -nostdlib -L$(CMINI)/lib -Wl,--traditional-format -o $@ \
	    $(CMINI)/lib/crt0.o $(OBJS) $(STDL_ARITH020) $(STDL_LIB) \
	    -lcmini -lgcc
```

The same for `libstdl.a` and `libstdl-cmini.a`. `STDL_ARITH020` is
`libstdl-arith020.o`, beside `arith020.mk` in STDL's directory, and the
flags; without make they are

```
$STDL/libstdl-arith020.o
-Wl,--wrap=__mulsi3 -Wl,--wrap=__udivsi3 -Wl,--wrap=__divsi3
-Wl,--wrap=__umodsi3 -Wl,--wrap=__modsi3 -Wl,--wrap=__muldi3
-Wl,--wrap=__divdi3
```

`make` builds the object with the archives, and the release page has
it beside them; a game's Makefile that builds STDL's archive asks for
it too (`$(MAKE) -C $(STDL) libstdl-cmini.a libstdl-arith020.o`).

- **What it does.** ld's `--wrap` sends every call of those seven, the
  program's, STDL's, the C library's and libgcc's own, to
  `libstdl-arith020.o` (`src/arith020.c`, 560 bytes). On a 68020 or
  later each is one `muls.l`, `divu.l`, `divs.l` (with
  `divul.l`/`divsl.l` for the remainders); `__muldi3` and `__divdi3`
  take the 64-bit forms on a 68020 to 68040 only, as a 68060 has none,
  `__muldi3` one `muls.l` where both operands are 32-bit numbers widened
  (gcc's `(long long)a * b`) and three multiplies otherwise. On a 68000
  the call goes on to libgcc's. The answers are libgcc's to the bit,
  including what the instructions cannot give, which goes to libgcc as
  well: -2^31 / -1, and a 64-bit division whose divisor or quotient
  needs more than 32 bits. Dividing by 0 takes the zero-divide exception
  on both paths, as libgcc's own `divu.w` does (bombs under TOS,
  `SIGFPE` under MiNT). The source changes not at all.
- **An object, named on the line**, so it is linked whatever the
  program's own objects call: the C library and libgcc come after the
  archives, and their calls (printf's divides) are wrapped too. Before
  the libraries, as it calls libgcc's own routines on a 68000. It calls
  the system through its traps and no C library, so the one object
  serves libcmini and mintlib programs.
- **How it knows the CPU**, on the first call, so it is right before
  `STDL_Init`, before `main` (the C library's start-up divides) and
  without the rest of STDL linked. The 32-bit routines ask the CPU
  itself: `lea (0,a0,d0.l*2)` scales the index on a 68020 and later,
  and a 68000 or 68010 ignores the scale. No supervisor mode, no cookie
  jar, no system call, so it is good in user mode under MiNT's memory
  protection and from an interrupt. The 64-bit routines need the
  `_CPU` cookie to tell a 68060, and ask it at their first call on a
  68020 or later: `Ssystem(S_GETCOOKIE)` where there is one (MiNT,
  MagiC), else `Supexec`, both good from either mode. That one is a
  system call, so a program whose first 64-bit multiply or divide
  could be in an interrupt routine does one in `main` first. No
  cookie (a 68020 card in an ST with no jar) keeps libgcc's 64-bit
  routines.
- **What it costs an ST**: 41 cycles a call, the test that sends it on
  to libgcc's, and libgcc's own inner calls pay it as well (its
  `__modsi3` calls `__divsi3` and `__mulsi3`). Measured in Hatari on
  an 8MHz STE, cycles a call, libgcc alone and with the flags:

  | Routine                     | libgcc | with the flags |
  | --------------------------- | -----: | -------------: |
  | `__mulsi3`                  |    234 |     275 (+18%) |
  | `__udivsi3`, 16-bit divisor |    341 |     382 (+12%) |
  | `__divsi3`                  |    525 |     607 (+16%) |
  | `__modsi3`                  |    964 |   1,130 (+17%) |
  | `__muldi3`                  |  1,191 |   1,313 (+10%) |
  | `__divdi3`                  |  4,182 |   4,633 (+11%) |

  So a game for the ST alone leaves the flags out.
- **What it gains a TT or a Falcon** has not been timed on either.
  Hatari's 68030 gives the multiply and divide instructions next to no
  time of their own: the `divu.l` path measured the same as an empty
  call, and libgcc's `__mulsi3`, three `mulu.w`, 84 cycles with the
  caches off. Its figures are for everything but those instructions,
  so the table adds each one's time from the MC68030 manual's tables
  (about 28 cycles a `mulu.w`, 44 a `divu.w` or a 32-bit `mul.l`, 78
  a `divu.l`, 90 a `divs.l`). Hatari's TT, TOS 3.06, ST RAM, caches
  off, cycles at 32MHz, then the estimate with the instructions:

  | Routine                     | libgcc (Hatari) | 68020 path (Hatari) | libgcc, est. | 68020, est. | gain |
  | --------------------------- | --------------: | ------------------: | -----------: | ----------: | ---: |
  | `__mulsi3`                  |              84 |                  -4 |          168 |          40 |   4x |
  | `__udivsi3`, 16-bit divisor |             144 |                  -4 |          232 |          74 |   3x |
  | `__udivsi3`, 17-bit divisor |             210 |                  -3 |          310 |          75 |   4x |
  | `__divsi3`                  |             321 |                   4 |          409 |          94 |   4x |
  | `__umodsi3`                 |             509 |                   4 |          681 |          82 |   8x |
  | `__modsi3`                  |             686 |                   4 |          858 |          94 |   9x |
  | `__muldi3`, 32x32 bits      |             754 |                  44 |        1,034 |          88 |  12x |
  | `__divdi3`, 64 by 32 bits   |           2,916 |                  52 |   over 3,000 |         142 | 20x+ |

  (the empty call's time taken off, which the 68020 path's few
  instructions undercut; `__muldi3` of operands wider than 32 bits
  takes three multiplies, 52 cycles in Hatari, about 184 estimated). A
  Falcon's are the same cycles at 16MHz.
  With the caches on, Hatari's figures move by where the link put the
  loop and the routine in the 256-byte instruction cache, and are not
  used. In one port's TT frame libgcc's arithmetic was 31ms of 227
  (Hatari's profiler, so without those instructions either); the same
  sources compiled for the 68030 ran 190ms. `MULDIV.TOS`, the example,
  projects points with `x * fov / z`, through the wrappers and through
  libgcc's own routines by name: in Hatari a TT 178,560 a second
  against 85,760, a Falcon 92,160 against 42,624, and an STE 6,912
  against 7,424.
- **A program without it** links none of it and is byte for byte what
  it was: every example, and a small program on each archive, linked
  the same before and after it came.
- **The check** is `tests/hatari/arithchk.c` (ARITHCHK): each routine
  against libgcc's on edge and random values, on both paths, the
  registers kept, the CPU asked, dividing by 0, and the times above.
  It ran clean on Hatari's STE, TT and Falcon, and on its TT as a
  68020, 68040 and 68060 under EmuTOS (the 68060 kept libgcc's 64-bit
  routines). No real TT, Falcon or MiNT has run it.

## Setting up a game or a fork

What follows was tried end to end: a small SDL 1.2 program in a
repository of its own, STDL added from GitHub as below, built in the
toolchain container and run in Hatari as a plain ST.

### Where STDL goes

A git submodule at `extern/stdl`, pinned to a release tag:

```
git submodule add https://github.com/neilrackett/atarist-stdl extern/stdl
git -C extern/stdl checkout v1.23.0          # the release you want
git add extern/stdl
git submodule update --init --recursive      # STDL's own submodule, lib/xpad
```

- **A submodule, built from source:** the game compiles the library with
  the toolchain it uses itself, so the headers and the archive always
  agree. A release's prebuilt `libstdl-cmini.a` needs that release's
  headers anyway.
- **A tag:** it says which release the game is on, and moving to the
  next one is a commit of its own that can be measured. The line
  `STDL_Init` writes to the console (`STDL v1.30.0 |
  neilrackett.com/atarist`, [porting.md](porting.md)) takes its
  version from it: STDL's Makefile asks git which tag the tree is at,
  so build the library through that Makefile, inside the clone or the
  submodule. A tree git cannot describe (an archive of the source)
  gets the line with no version; `make STDL_VERSION=v1.30.0` says it
  by hand. So does a build where git cannot see the repository: a
  submodule's is in the game's `.git`, so start `stcmd` in the game's
  directory (`stcmd make -C extern/stdl ...`), not inside the
  submodule, where the container would hold STDL's files alone.
- **`extern/`:** it keeps STDL apart from the game's own sources, so a
  glob over `src/` never picks up STDL's files, and a reader can see
  what is vendored.
- **The container sees only the directory `stcmd` starts in**, so STDL
  must be inside the game's repository. A checkout beside it
  (`../stdl`) is invisible to the build.

Stage the tag (`git add extern/stdl`) before `git submodule update`:
run first, the update puts the submodule back on the commit recorded
when it was added, which is `main` as it was that day. Whoever clones
the game later needs `git clone --recursive`, or `git submodule update
--init --recursive` after the clone. STDL's guides for AI agents then come from
the submodule too, as relative links, so they follow it from release to
release:

```
mkdir -p .claude/skills
ln -s ../../extern/stdl/.claude/skills/stdl .claude/skills/stdl
ln -s ../../extern/stdl/.claude/skills/stdl-sdl2 .claude/skills/stdl-sdl2
```

### The ST build's Makefile

Beside the game's own build, as `Makefile.atari`, run from the
repository's root with `stcmd make -f Makefile.atari`:

```make
STDL     = extern/stdl
CMINI   ?= /freemint/libcmini
CROSS    = m68k-atari-mint-
CC       = $(CROSS)gcc
CFLAGS   = -O2 -fomit-frame-pointer -std=gnu99 -Wall -MMD -MP -DPORT_STDL \
           -I$(STDL)/include -I$(STDL)/include/compat -I$(CMINI)/include
OBJDIR   = build-atari
SRCS     = src/game.c
OBJS     = $(SRCS:%.c=$(OBJDIR)/%.o)
TARGET   = dist/GAME.TOS

STDL_LIB  = $(STDL)/libstdl-cmini.a
STDL_SRCS = $(wildcard $(STDL)/src/*.c $(STDL)/include/stdl/*.h \
                       $(STDL)/include/compat/*.h $(STDL)/lib/xpad/src/*.c)

all: $(TARGET)

$(STDL_LIB): $(STDL_SRCS)
	$(MAKE) -C $(STDL) libstdl-cmini.a

$(OBJDIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

$(TARGET): $(OBJS) $(STDL_LIB)
	@mkdir -p $(dir $@)
	$(CC) -nostdlib -L$(CMINI)/lib -Wl,--traditional-format -o $@ \
	    $(CMINI)/lib/crt0.o $(OBJS) $(STDL_LIB) -lcmini -lgcc
	$(CROSS)strip $@

clean:
	rm -rf $(OBJDIR) $(TARGET)

-include $(OBJS:.o=.d)
.PHONY: all clean
```

- **libcmini, as above.** The program sets its own stack, under the ST
  build's macro: `long _stksize = 32768;`.
- **`STDL_SRCS` globs every directory STDL's sources are in**, so a
  change to STDL rebuilds the archive. A glob that sees only `src/`
  misses an Xpad bump (`lib/xpad/src/`) and links an archive built
  against the previous ABI. STDL builds its objects into
  `$(STDL)/obj/`, never beside its sources.
- **`-I$(STDL)/include/compat`** is what a ported SDL 1.2
  program needs, so that `#include "SDL.h"` finds STDL's shim. A game
  written for STDL from the start leaves it out and includes
  `<stdl/stdl.h>`.
- **The objects go in a directory of their own** (`build-atari/`), so
  the ST build and the desktop build never see each other's.
- `dist/` is the Hatari drive: put the program's data files beside
  `GAME.TOS` there (Testing, below).

### Assets

`stdlconv`'s image commands (`bmp16`, `bmp8`, `pi1`, `bank`) need Python 3 with
Pillow, and the toolchain container has Python 3 without Pillow. So
convert art on the host (`pip install pillow`), and keep the converted
files in the repository, so the container's build never needs them;
`midi`, `opm`, `wav` and `embed` need only Python. A Makefile rule
from the source art to the converted file keeps the pipeline written
down: `examples/README.md` points at one. The converted files are the
program's data, not inputs to its build, so the container never needs
them: track them where someone without Pillow must still be able to
run the game (STDL tracks its examples' in `examples/assets/`), or
make them into `dist/` with the program.

### What git should ignore

Before the first `git add`, so that neither the build's output nor a
TOS image is committed (the image under `tmp/tos/` is copyrighted, or
EmuTOS's to ship, and not the game's):

```
build-atari/
dist/
tmp/
```

That suits a `dist/` that holds only what the Makefile makes. Data
files tracked beside the program belong elsewhere (`data/`, say), with
a rule that copies them into `dist/`.

### Shipping

A double-sided 3.5-inch floppy holds 720K, a little over 700K of it
free for files once formatted (a single-sided one half that), but
nearly every player now runs games from a hard disk or a cartridge
that stands in for one, so a game that outgrows a floppy ships as a
folder (a zip) to copy there.

### Keeping the desktop build

A fork usually wants its desktop build to keep working:

- **Leave the game's own build as it is**, and add the ST build beside
  it (`Makefile.atari` above), never in place of it.
- **`#include "SDL.h"`** works for both builds: the desktop build finds
  SDL's own header through `sdl-config --cflags` (`-I.../include/SDL`),
  the ST build finds the shim through `-I$(STDL)/include/compat`.
  Rewrite `#include <SDL/SDL.h>` to that form, as the shim has no
  `SDL/` directory.
- **Put the ST's own lines under a macro the ST Makefile defines**
  (`-DPORT_STDL` above): `_stksize`, STDL calls the desktop does not
  have (`STDL_JoystickKeyEmulation`, the asset loaders), and the converted
  art's file names. The compiler's own `__m68k__` is the wrong test: a
  desktop SDL build for another 68000 target defines it too. Every path
  the macro does not cover keeps the game's behaviour exactly, so the
  desktop build stays a reference to compare the port with.

## Testing

`make test` runs the pixel paths, the BLiTTER model, the audio mixer and
the asset loaders natively against reference models, which is far
faster than an emulator and catches out-of-bounds reads. On the
machine, `tests/hatari/run.sh` drives a program in Hatari with console
capture, screenshots and key injection; its header has the command
language. Setting up the emulator and a TOS image (EmuTOS, no ROM
needed) is in the porting skill's "Testing in Hatari" section
([.claude/skills/stdl/SKILL.md](../.claude/skills/stdl/SKILL.md)).
