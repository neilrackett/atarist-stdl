# Working on STDL

STDL is a planar-native subset of SDL 1.2 for the Atari ST. This
file is for agents/contributors changing the **library itself**.

For *porting games with* STDL, read
[`.claude/skills/stdl/SKILL.md`](.claude/skills/stdl/SKILL.md)
instead - it is plain markdown usable by any agent or human (the
format contract, API costs, rewrite patterns, tooling and test
loop). Claude Code additionally auto-discovers it as the `/stdl`
skill.

## Build

```
STCMD_NO_TTY=1 stcmd make        # non-interactive shells (agents, CI)
stcmd make                        # interactive terminal
```

Cross-compiles with m68k-atari-mint-gcc 4.6.4 via
[atarist-toolkit-docker](https://github.com/sidecartridge/atarist-toolkit-docker).
Produces `libstdl.a` and `dist/` (16 example .TOS binaries + assets
copied from `examples/assets/`). The build must stay at **zero
warnings** with the Makefile's `-Wall -Wextra`.

## Verification expectations

- **Pixel paths first**: `make test` (native clang + ASan, no
  cross toolchain) runs `tests/host/` - fills/blits/sprites against
  a per-pixel reference model, composition semantics, the cursor's
  save-under, and the asset loaders. `stdl_internal.h` stubs the
  hardware and m68k asm when `__m68k__` is undefined. Run this
  before any emulator debugging - it is far faster and catches
  guard-band overreads. Keep it warning-free too.
- **On target**: `tests/hatari/run.sh NAME dist/PROG.TOS BOOT_WAIT
  "cmd;cmd;..."` drives a program in Hatari with console capture,
  screenshots, key injection (raw ST scancodes, not SDL codes) and
  console-marker waits - see the script header for the command
  language and the TOS/FF/MACHINE environment overrides. Output
  lands in `tests/hatari/out/`. Sound verification: record with
  `hatari-shortcut recsound` (file path comes from the
  `szYMCaptureFileName` key in the Hatari config).
- **Size is a test result too.** `stcmd make` runs a `sizecheck`
  target that fails the build if the pixel-path objects outgrow
  `PIXEL_MAX` in the Makefile. The archive links whole objects into
  programs with 512K-1M of RAM to share between code and heap, so
  library text spends the port's memory: a 12K growth in the
  pixel-path objects (four `STDL_PLANE_DISPATCH` instantiations,
  three of them unreachable at the default budget) is what stopped
  FreeNukum fitting in 1M - it ran out of heap mid-level,
  `STDL_CreateSurface` returned NULL and the game dereferenced it.
  **`tests/host` cannot see this class of bug at all** - it compiles
  the same sources natively, where neither text size nor heap
  pressure matters - so the check lives in the cross build, and
  `tests/host/test_footprint.c` covers the heap half by asserting
  a surface is one metadata allocation, not three.
- **After touching blit/fill paths**: run `dist/BLITCHK.TOS` - it
  randomises fills/blits and compares the CPU and BLiTTER paths
  byte-for-byte on target. Both paths must stay identical;
  `STDL_UseBlitter(0)` forces CPU.
- **Audio/music**: record Hatari output (`hatari-shortcut recsound`,
  path from the `szYMCaptureFileName` config key) and verify
  spectrally; PLAYMUS's DEMO.STM is note-exact by construction.
- Verify on `--machine st` too: plain 8MHz ST is the correctness
  floor; the blitter and DMA audio are optional hardware.

## Non-negotiable constraints

- `docs/format.md` is the frozen byte-level contract (surfaces,
  masks, sprites, banks, STM). Change behaviour to match the doc,
  not the doc to match a bug. `docs/limits.md` lists deliberate
  non-goals - do not implement around them.
- Mask convention everywhere: **bit set = destination preserved**.
- YM2149: never touch registers 14/15 (TOS floppy select); all r7
  writes must preserve the port-direction bits (go through
  `stdl_ym_mix_update`). Effects/music share voices via
  `stdl_ym_owned` ownership - see src/ym.c.
- The library runs in supervisor mode between `STDL_Init` and exit.
  Low memory (cookie jar at $5A0, hz200 at $4BA) bus-errors in user
  mode. `Super()` enter/exit at different stack depths crashes -
  see `exit_supervisor()` in src/video.c before changing it.
- Cooperative model: no interrupts except the VBL sound tick
  (ym.c) and the IKBD handler (event.c). Services (audio refill,
  compat timers, cursor) run from the pump and the native delays.

## 68000 / gcc 4.6.4 performance notes

- 32-bit multiply/divide are library calls; keep them out of inner
  loops (use error-accumulator resampling, strided pointers, `&15`
  and `>>4` instead of `%`/`/`).
- Carrying loop values in **arrays** spills to the stack and is
  slower than refetching from RAM; use scalars or walking pointers
  (measured: the blit shift chain regressed 18->13fps with carried
  arrays, 19fps with walking pointers).
- gcc 4.6 does not unswitch loops: hoist loop-invariant branches
  (edge masks, format dispatch) manually; peel first/last
  iterations.

## Conventions

- C (gnu99), 4-space indent, ~72-column comments, one module per
  file, `STDL_Module_Verb()` naming. LGPL header (SPDX) on library
  files; examples are CC0.
- Values shared with SDL 1.2 (surface flags, keysyms, INIT flags,
  audio formats, event numbering) are numerically identical to
  SDL's - keep new ones that way.
- Compat behaviour belongs in compat.c/mixer.c as thin wrappers;
  if a wrapper needs real logic, the logic probably belongs in the
  library (see STDL_FillRect's SDL write-back contract).
- Commits: no Co-Authored-By trailers.
