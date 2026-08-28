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
Produces `libstdl.a` and `dist/` (17 example .TOS binaries + assets
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
- **A new primitive gets its own source file.**
  `-ffunction-sections` is unsupported on m68k-atari-mint, so there
  is no section garbage collection and the linker's granularity is
  the object file: anything added to an object every program links
  is charged to every port, whether or not it calls the new
  function. One primitive (or one family a caller would want all
  of) per translation unit keeps that bill with the programs that
  actually ask for it - `src/vbl.c`, `src/indexed.c` and
  `src/drawchar.c` are the pattern. It works in both directions:
  moving `STDL_SurfaceFrom1bpp` out of `surface.o`, which every
  program links, and in with the other source-art converters made
  all three ported games *smaller* while the library gained three
  APIs. Measure it - rebuild the games and compare the .TOS sizes.
- **A flag that changes mask handling has to reach the fast paths
  too.** blit.c's shortcuts bypass the mask entirely: whole-group
  `memcpy`, the `memset` that clears mask words, and every BLiTTER
  pass (which does its own upkeep with HOP/OP). Adding
  `STDL_BLIT_UNDER`/`MARK` meant *excluding* those routes when
  either flag is set, not teaching them the flag. A new blit flag
  wants the same audit, plus a host test asserting the zero-flag
  call is byte-identical to the entry point it wraps - that is what
  keeps "additive" honest.
- **A blit-timing change needs a live scene, not just BLITCHK.**
  Re-setting the BLiTTER's busy bit inside the completion poll (the
  demo "restart idiom") passes BLITCHK byte-identical at every plane
  budget with a border open, and measures 30% faster - and corrupts
  palette fades into wrong colours on screen. BLITCHK compares CPU
  and BLiTTER output one operation at a time; it cannot see a
  read-modify-write on the control register racing the hardware's
  own state across a frame, nor a blit re-armed with a spent line
  count. So: in non-hog mode start the blit once and poll, the
  hardware resumes itself between bus slices - and anything that
  changes *how* a blit is driven (mode, restart, interleaving with
  interrupts) gets watched in a real scene with fades before it
  ships, not just measured.
- **After touching blit/fill paths**: run `dist/BLITCHK.TOS` - it
  randomises fills/blits and compares the CPU and BLiTTER paths
  byte-for-byte on target. Both paths must stay identical;
  `STDL_UseBlitter(0)` forces CPU.
- **Inline asm in a pixel path is paired with its C twin.** blit8.c
  is the pattern: the hand-written 68000 gather sits under
  `#ifdef __m68k__` with the identical loop in C as the `#else` -
  the C build is what tests/host exercises, and an on-target run
  comparing both against a per-pixel model gates the asm (gcc 4.6
  earned this: it compiled the C loop at ~210 cycles/pixel by
  spilling accumulators and calling __mulsi3 per group). Remember
  the 68000's (d8,An,Xn) mode takes an 8-BIT displacement only.
- **Audio/music**: record Hatari output (`hatari-shortcut recsound`,
  path from the `szYMCaptureFileName` config key) and verify
  spectrally; PLAYMUS's DEMO.STM is note-exact by construction. The
  voice mixer (voice.c) is host-tested end to end through the fake
  TOS queue and DMA counter in tests/host/test_voice.c - steer
  `stdl_host_dma_pos` and call the queue slot like the interrupt
  would.
- Verify on `--machine st` too: plain 8MHz ST is the correctness
  floor; the blitter and DMA audio are optional hardware.

## Shipping a change

- **A public API change is not done until the docs and the skill
  say so.** Adding, changing or deprecating anything in
  `include/stdl/` means updating, in the same change:
  `docs/format.md` if it touches the byte-level contract or the
  mask rules, `docs/porting.md` if a port would meet it,
  `.claude/skills/stdl/SKILL.md` (the cost table, the API list, and
  any pattern worth teaching), and `README.md` if it belongs in the
  headline list. A header comment alone does not count - nobody
  porting a game reads the headers first. Check it the cheap way
  before committing: grep the new symbol across `docs/ README.md
  .claude/` and see it come back.
- **New functionality needs an example that exercises it.** The
  examples are how a port author sees an API working before
  committing to it, and they ship as .TOS binaries on the release
  page - so they are the API's real acceptance test on hardware.
  Extend an existing example when the feature belongs to a demo
  that already runs (a new blit flag joins the one drawing
  sprites); add a new one when it does not fit, remembering the
  Makefile rule and the `EXAMPLES` list, or it never gets built or
  shipped. Keep them small and CC0, show the intended usage rather
  than every parameter, and say in a comment why the pattern is the
  right one - `examples/chunky.c` is the shape to copy.
- **State the honest measured number, not the hoped-for one.** If a
  pattern in the skill has a performance claim, it carries what was
  measured, including when that is "near zero in a busy scene". An
  agent reading the skill is deciding whether to spend a day on it.
- **Do not name the game or project a lesson came from.** Ports are
  the maintainer's to announce, and some are not public yet. Write
  "one port", "a game conversion", "an engine that decodes frames
  at runtime" - the lesson is the useful part, and the attribution
  gets added later by the maintainer once a port is released.

## Non-negotiable constraints

- `docs/format.md` is the frozen byte-level contract (surfaces,
  masks, sprites, banks, STM). Change behaviour to match the doc,
  not the doc to match a bug. `docs/limits.md` lists deliberate
  non-goals - do not implement around them.
- Mask convention everywhere: **bit set = destination preserved**.
  The two flags that instead read a destination mask as a
  foreground/priority plane - `UNDER` (protect marked pixels) and
  `MARK` (set bits under drawn pixels) - share their bit values
  between `STDL_BlitIndexed8` and `STDL_BlitSurfaceEx` so one flag
  set serves a scene built from both. Keep new ones aligned.
- YM2149: never touch registers 14/15 (TOS floppy select); all r7
  writes must preserve the port-direction bits (go through
  `stdl_ym_mix_update`). Effects/music share voices via
  `stdl_ym_owned` ownership - see src/ym.c.
- The library runs in supervisor mode between `STDL_Init` and exit.
  Low memory (cookie jar at $5A0, hz200 at $4BA) bus-errors in user
  mode. `Super(0)` is a *toggle*: calling it when the caller is
  already supervisor drops to user mode, so `STDL_Init` asks
  `Super(1)` first and only claims - and later gives back - the mode
  when it was ours. `Super()` enter/exit at different stack depths
  crashes - see `exit_supervisor()` in src/video.c before changing
  it.
- **Termination is not just `atexit`.** STDL installs a GEMDOS
  terminate-vector handler (etv_term at $0408, reached through Setexc
  vector number 0x102) that puts back everything which would outlive
  the process - VBL slots, the IKBD vector, palette,
  resolution, DMA - because `abort()`, a failed `assert()` and a TOS
  exception all reach `Pterm` without running `atexit` (and libcmini
  runs the handlers it does have in registration order, not LIFO).
  Anything that handler runs may touch hardware and vectors only:
  no GEMDOS calls, no heap, no leaving supervisor mode. Keep new
  shutdown work in `release_hardware()` on the right side of that
  line.
- **Joystick input has two sources and they merge, never override.**
  A stick on port 1 arrives as IKBD packets; a modern controller
  arrives as an Xpad block found through the cookie jar
  (`src/stdl_xpad.c`). `event.c` keeps `joy_ikbd` and `joy_xpad`
  apart and ORs them, so one releasing cannot clear what the other
  holds, and either alone works. Xpad matters here specifically
  because STDL replaces `ikbdsys`: TOS never dispatches `joyvec`
  while a game runs, so a provider that injects there cannot reach
  us, and one that publishes a block can.
- `src/xpad.c` is vendored from atarist-xpad unmodified under
  BSD-2-Clause. Do not edit it; update it from upstream. It is the
  consumer half only: upstream keeps the provider helpers in a
  separate `xpad_provider.c` precisely so a library like this does not
  carry them, there being no section garbage collection on
  m68k-atari-mint to drop them for us. Only the test fixture in
  `tests/hatari/` needs that file, because it publishes a block.
- Cooperative model: no interrupts except the VBL sound tick
  (ym.c), the public `STDL_AddVBL` callbacks (vbl.c) and the IKBD
  handler (event.c). Services (audio refill, compat timers, cursor)
  run from the pump and the native delays.

## 68000 / gcc 4.6.4 performance notes

- 32-bit multiply/divide are library calls; keep them out of inner
  loops (use error-accumulator resampling, strided pointers, `&15`
  and `>>4` instead of `%`/`/`).
- Variable shifts cost 8+2n cycles each, so two of them per word is
  a fixed tax whatever the shift amount. Building the value from a
  32-bit window needs one: `(((uint32_t)hi << 16) | lo) >> n` rather
  than `(hi << r) | (lo >> n)`, because the `<< 16` half compiles to
  a free `swap`. Worth 1ms/frame in the unaligned blit path.
- Do not hand-unroll a four-iteration plane loop hoping to save the
  loop overhead: four live plane words spill, and it measured slower
  than the rolled version.
- Carrying loop values in **arrays** spills to the stack and is
  slower than refetching from RAM; use scalars or walking pointers
  (measured: the blit shift chain regressed 18->13fps with carried
  arrays, 19fps with walking pointers).
- gcc 4.6 does not unswitch loops: hoist loop-invariant branches
  (edge masks, format dispatch) manually; peel first/last
  iterations.
- **Mega STE benchmark numbers are code-layout-sensitive.** Its
  16MHz mode leans on a small cache, so an unrelated rebuild can
  move a frame-time figure ~10% either way (measured: one port's
  squash-mode frame went 34.8 -> 43.1ms from added code that never
  executes in that mode, while a plain ST timed both builds
  identical to 0.01ms). Before believing a Mega STE regression,
  re-measure on `--machine st`: no cache, no layout luck. Compare
  A-vs-B features within one binary where possible.

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
