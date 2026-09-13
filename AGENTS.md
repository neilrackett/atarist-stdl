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
Produces `libstdl.a` and `dist/` (the example .TOS binaries + assets
copied from `examples/assets/`; `make bundle` zips them for the
release page). The build must stay at **zero
warnings** with the Makefile's `-Wall -Wextra`.

## Verification expectations

- **Pixel paths first**: `make test` (native clang + ASan, no
  cross toolchain) runs `tests/host/` - fills/blits/sprites against
  a per-pixel reference model, composition semantics, the cursor's
  save-under, and the asset loaders. `stdl_internal.h` stubs the
  hardware and m68k asm when `__m68k__` is undefined. Run this
  before any emulator debugging - it is far faster and catches
  guard-band overreads. Keep it warning-free too.
- **CI runs both builds on every push** (`.github/workflows/ci.yml`):
  the host tests, then the cross build with `sizecheck`, then the
  libcmini archive. Two things it sees that a developer's Mac does
  not, so check them before blaming the runner: ASan on Linux turns
  on LeakSanitizer, which macOS has none of, and its filesystem is
  case-sensitive, which caught `stdl_fopen_ci` uppercasing whole
  paths. A pushed `v*` tag additionally publishes `libstdl.a`,
  `libstdl-cmini.a` and `stdl-examples.zip` (`make bundle`: the
  example binaries and the assets they load) to the rolling
  `latest` release (`.github/workflows/release.yml`), so the
  version tag is the release and nothing is uploaded by hand. An
  example added to the Makefile's `EXAMPLES` list therefore ships
  on the release page; one built by hand and left in `dist/` does
  not, which is deliberate.
- **On target**: `tests/hatari/run.sh NAME dist/PROG.TOS BOOT_WAIT
  "cmd;cmd;..."` drives a program in Hatari with console capture,
  screenshots, key injection (raw ST scancodes, not SDL codes) and
  console-marker waits - see the script header for the command
  language and the TOS/FF/MACHINE environment overrides. Output
  lands in `tests/hatari/out/`. Emulator and TOS-image setup (an
  EmuTOS download, no ROM needed) is in the skill's "Testing in
  Hatari" section. Sound verification: record with
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
- **A line-locked MFP interrupt needs lines of grace, and its waits
  need short bounds.** The CPU takes an MFP interrupt only once it
  is below level 6, and TOS's 200Hz Timer C handler keeps it there
  for over two scanlines at a time (EmuTOS measured at ~1140
  cycles), drifting across any fixed frame position by 165 cycles a
  frame - so an ISR that fires on the line it needs loses a run of
  consecutive frames every twenty seconds. overscan.c answers it
  with a prefix on the Timer C vector that lowers the CPU mask to 5
  inside TOS's handler, so the border timers (MFP channels 13 and
  8) nest into it; the VBL (level 4) still cannot, so the ISR that
  ends a frame leaves Timer B running as a stopwatch and the VBL
  prefix shortens Timer A's count by however late it finds itself.
  Anything new that must land on a line wants the same two
  measures, or lines of grace and a poll. And a poll
  that waits for a Display Enable event which is not coming (the
  border failed to open, so there are none until the next frame)
  must give up within a few lines: an 8ms wait at MFP priority
  holds off the VBL, which arms the timer late, which repeats the
  failure - measured at a dozen frames before the bound existed.
- **Hatari's video counter reads are not cycle-correct at 16MHz.**
  `Video_CalculateAddress` compares CPU cycles against 8MHz line
  positions, so under `--machine megaste` (or `--cpuclock 16`)
  $ff8209 runs at double speed and parks at the line end from cycle
  ~192. The bottom-border code checks for this at open time and
  times from Timer B instead; its video-counter path can only be
  verified with `--machine st` or `ste`, and real 16MHz hardware
  is expected to take the counter path. `tests/hatari/ovprobe.c`
  prints which path a run took and the calibration behind it.
- **A blit placed "before the window" starts lines later than the
  decision.** overscan.c's blit policy decides from the beam's
  position, but the decision-to-first-bus-cycle latency (register
  writes, the completion poll, the next plane's call) is two to
  three scanlines, and a Timer C tick can land in between; pieces
  placed with two lines to spare started inside the ISR window once
  every few hundred frames, and a wrong estimate is self-sustaining
  (a missed border parks the video counter at row 200, which read
  against the open layout is a line in the picture while the beam
  is in the blank - so the estimator has to know which borders
  opened this frame). Measure that latency with a write to a traced
  MFP register (`--trace mfp_write` shows the position) rather than
  inferring it, and keep the reserve a measured number. Also: the
  blitter-trace lines with a ROM `pc` are EmuTOS's own console
  blits, not the library's.
- **The emulator renders from the video counter; the Shifter's plane
  phase is not modelled, and only real hardware shows it.** The first
  bottom-border flick restored 50Hz 21 cycles into line 263, which
  Hatari's GLUE model calls harmless (it fixes a line's length at
  cycle 52); the real GLUE fixes it at the boundary, line 263 came
  out 508 cycles, the frame stopped being a multiple of the Shifter's
  16-cycle plane cycle, and every colour was wrong with every shape
  in place - a plane rotation moves no single-plane pixel, so
  "geometry intact" does not clear the fetch. The video counter was
  exact throughout. A sync-rate pulse must end inside the line it
  started in; the top border always did. When a symptom appears
  only on hardware, the quickest lever is a set of variant binaries
  each undoing one change, and a probe that prints one item per
  line on a cleared screen and holds - a photographed printout with
  a duplicate overlay was misread 131 for 13 and cost half a day.
- **Scaling 8MHz instruction costs by a measured loop speed is not a
  16MHz timing model.** A cached 16MHz CPU on an 8MHz bus placed the
  flick's writes plus or minus five cycles where the emulator showed
  none; overscan.c now measures the flick's own instruction sequence
  on the machine at open time (against a scratch byte mid-line, the
  counter read back as the clock) and builds its tables from that.
  A timing diagnostic that reads the counter must keep the read
  inside the line's fetch (cycles 64-376): a read landing after the
  park silently tracks the write instead of the line.
- **The GLUE's position against the video counter is not a constant
  of a machine.** On one Mega STE the bottom-border test moved eight
  cycles between two boots with identical tables: the GLUE and the
  MMU come up in one of several relative phases (Hatari's "wakeup
  states"), the counter is the MMU's, and the emulator sits at a
  further offset from any real machine. Hatari cannot show it - its
  states shift the counter and the GLUE together - which is why the
  suggestion to look there was wrongly dismissed for two days on
  emulator evidence. overscan.c now finds the test at open time by
  trying the pulse upward until the ISR reports the border open; any
  future cycle-level placement against a GLUE event wants the same
  search, not a measured constant, and an emulator result that a
  hardware suggestion contradicts is a reason to test on hardware,
  not to discard the suggestion.
- **A Shifter plane-phase slip outlives the program.** On a real
  Mega STE the desktop came back with its planes rotated after a
  probe had run a sync pulse ending in the next line; a resolution
  change to medium did not clear it. A moment of hi-res in the
  blanking does (verified: same run, desktop normal), so every final
  border close and the terminate path reseed the Shifter that way.
  Any new sync-rate or resolution trick wants the same guard on its
  way out.
- **A success check that accepts "the next event" can pass on the
  wrong line, and then the miss counter lies.** The bottom ISR's
  post-check waited for any Display Enable end within a bound; when
  the pulse fell a line early (the emulator's Mega STE), line 262's
  own end satisfied it, the border stayed closed and misses read
  zero - a game port's frame capture found 37% of frames cut short
  that no counter had noticed. Timer B's count identifies a line
  (armed at a known value, one less per line end), so waits for a
  particular line's event compare the value, never "changed". And
  verify a border with the picture, not the counter: Hatari's
  `--trace video_border_v` says per frame whether a border was
  removed, and an AVI capture with `--avirecord` gives per-frame
  extents that need no eyes. The best surface for that capture is
  a static screen held for seconds - a splash, a title, any frame a
  program can be made to hold - with no logic, disk or input behind
  it: every frame should then be identical, and any variation is
  the bug. That is how a port found the 37% in the first place.
- **Isolate resident firmware before sharpening a line-locked ISR.**
  With the placement right, the real Mega STE still flickered the
  bottom border a few times a second and a probe's opens jittered
  between runs of identical settings; both vanished with the
  cartridge's network firmware unloaded (its resident code holds
  interrupts off long enough to make Timer B late). A hardware
  jitter that is not in the emulator is an environment question
  first: reproduce with nothing resident before touching the ISR.
- **Hatari's GEMDOS drive maps names to 8.3.** A scratch binary
  named `BLITCOST2.TOS` beside `BLITCOST.TOS` silently runs
  `BLITCOST.TOS`; four runs of "the fix" measured the old binary.
  Keep test program stems to eight characters.
- **Possible future improvement: the blit policy's per-operation
  cost.** With a border open, BLiTTER throughput is 1.14-1.28x the
  no-border time for full-width operations (mostly the split and
  wait around each window, one to three per mode) but 1.35-1.47x
  for 64x32 blits, because the placement decision costs 500-1400
  cycles against a blit of about 1000. The no-divide fast path
  only covers the beam in the picture; the blanking (a quarter to a
  third of the frame) still goes through the general estimate. A
  second fast path there, or a per-frame "safe until" stamp that
  skips the estimate altogether while far from any window, would
  take most of that off sprite-heavy scenes. Measured in Hatari
  only - parked 2026-09-03 with the numbers above, worth revisiting
  when a port with a border open shows it.
- **A threshold is calibrated against the code it chooses between,
  so it goes stale when either side gets faster.** The BLiTTER was
  taken for any unmasked copy of 32 cells or more, a number fitted
  when short rows went through `memcpy`. Inlining those rows made
  the CPU path up to twice as fast for tile-sized blits and left the
  library picking the BLiTTER for a 32x16 copy that the CPU now did
  in half the time. Re-measure the decision whenever either path
  changes - `tests/hatari/blitcost.c` times the library's own choice
  against the CPU path forced, so a stale threshold reads as
  "SLOWER" instead of hiding.
- **Keep 32-bit multiplies out of the decision, not just the loop.**
  The replacement rule `h * (ROW + CELL * ng) > SETUP` costs two
  `__mulsi3` calls written in plain ints, and the compound condition
  only evaluates them once the BLiTTER is allowed - so it charged 8%
  of a tile blit to blits it then declined to accelerate. It is
  `stdl_row_off` and the 16-bit forms for decisions too.
- **An optimisation behind a silent predicate needs a way to see
  whether it fired.** The short-row copy above is guarded by a
  long-alignment test, and `STDL_CreateSurface` did not guarantee
  it: mintlib's malloc is word aligned and what it returns depends
  on allocation history, so the fast path ran on one machine's
  surfaces and never on another's. Nothing could see it - same
  pixels, same everything, only the clock differs - and two of us
  measured correctly and drew opposite conclusions because neither
  probe said which path it took. The allocator guarantees the
  alignment now (`pix_adj` remembers what was added, so free still
  hands malloc back its own pointer), `tests/host/test_footprint.c`
  asserts it across sizes and allocation orders, and
  `tests/hatari/blitcost.c` prints the alignment before the timings.
  Any new fast path with a run-time condition wants the same three:
  guarantee what you can, assert it, and print it.
- **`memcpy` for a tile row is nearly all prologue.** The aligned
  fast path called it once per row, which for a 16x16 tile is eight
  bytes a call: measured about 650 cycles at 16MHz to move what two
  `move.l` do in thirty. Rows up to `BLIT_INLINE_MAX` are copied
  inline now. A long move needs the pointers long-aligned, which
  `STDL_CreateSurfaceFrom` does not promise (word only), so the test
  is made once per blit - a stride is a whole number of groups, so
  what holds for the first row holds for all of them.
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
- **The library is for a plain 8MHz ST; everything above it is
  progressive enhancement.** 68000, no blitter, no DMA audio, 512K
  to 1M shared between code and heap - that is the machine, and a
  new primitive works there first. The blitter, DMA sample
  playback, STE hardware scrolling and the Mega STE's 16MHz are
  used when present and detected at run time, never assumed:
  `STDL_UseBlitter`, `STDL_HasHwScroll` and the audio calls exist
  so a port branches at init rather than shipping two builds. An
  API that genuinely needs an STE (hardware scrolling) says so in
  its header and fails cleanly elsewhere. Performance work is
  measured on `--machine st` first - the harness defaults to `ste`,
  which is still an 8MHz CPU - because a 16MHz figure hides the
  regression that matters, and "playable on a Mega STE" is not a
  result.

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
- **Xpad is a submodule at `lib/xpad`**, not a vendored copy, and the
  Makefile compiles `$(XPAD)/xpad.c` straight out of it. It used to be
  a copy in `src/`; that stopped being safe once the ABI started
  moving, because a stale `xpad.h` still compiles and gives you a
  struct layout nobody agrees with. Update it deliberately, with
  `git submodule update --remote lib/xpad`, and read the diff. Never
  edit anything under `lib/`: change it upstream and bump.
  It is the consumer half only. Upstream keeps the provider helpers in
  a separate `xpad_provider.c` precisely so a library like this does
  not carry them, there being no section garbage collection on
  m68k-atari-mint to drop them for us. Only the test fixture in
  `tests/hatari/` needs that file, and it now takes it from the
  submodule too rather than keeping its own copy.
  Two consequences worth knowing when a consumer reports something odd
  after a bump. A game Makefile that globs `$(STDL)/src/*.c` to decide
  whether to rebuild `libstdl.a` no longer sees xpad, so a submodule
  bump goes unnoticed and links an archive built against the previous
  ABI; it needs `$(STDL)/lib/xpad/src/*.c` in the glob too. And objects
  now build into `obj/`, mirroring each source's path, so nothing lands
  inside the submodule: anything looking for `src/*.o` wants
  `obj/src/*.o`, and orphaned objects from before the move are not
  removed by `make clean`, which is now `rm -rf obj`.
- Cooperative model: no interrupts except the VBL sound tick
  (ym.c), the public `STDL_AddVBL` callbacks (vbl.c) and the IKBD
  handler (event.c) - plus, while a border is open, overscan.c's
  timer ISRs and its register-free prefixes on the VBL and Timer C
  vectors (the latter only lowers the CPU mask to 5 inside TOS's
  handler). Services (audio refill, compat timers, cursor) run from
  the pump and the native delays.

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
