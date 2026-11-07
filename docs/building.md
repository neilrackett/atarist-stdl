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

## A game's Makefile

If your game's Makefile decides when to rebuild `libstdl.a` by globbing
STDL's sources, glob both directories - not every source is under
`src/`:

```make
STDL_SRCS = $(wildcard $(STDL)/src/*.c) $(wildcard $(STDL)/lib/xpad/src/*.c)
```

A glob that only sees `src/` will not notice an Xpad submodule bump, so
it will happily link an archive built against the previous ABI. Objects
build into `$(STDL)/obj/`, mirroring each source's path
(`obj/src/blit.o`), never beside the sources.

## Testing

`make test` runs the pixel paths, the BLiTTER model, the audio mixer and
the asset loaders natively against reference models, which is far
faster than an emulator and catches out-of-bounds reads. On the
machine, `tests/hatari/run.sh` drives a program in Hatari with console
capture, screenshots and key injection; its header has the command
language. Setting up the emulator and a TOS image (EmuTOS, no ROM
needed) is in the porting skill's "Testing in Hatari" section
([.claude/skills/stdl/SKILL.md](../.claude/skills/stdl/SKILL.md)).
