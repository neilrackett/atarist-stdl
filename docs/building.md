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
| `make test`   | the host test suite, natively with ASan: no cross toolchain needed                                    |
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
  next one is a commit of its own that can be measured.
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
