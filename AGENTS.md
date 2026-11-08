# Working on STDL

STDL is a planar-native subset of SDL 1.2 for the Atari ST. This
file is for agents/contributors changing the **library itself**.

For *porting games with* STDL, read
[`.claude/skills/stdl/SKILL.md`](.claude/skills/stdl/SKILL.md)
instead - it is plain markdown usable by any agent or human (the
format contract, API costs, rewrite patterns, tooling and test
loop). Claude Code additionally auto-discovers it as the `/stdl`
skill. Porting an SDL2 program to a TT or a Falcon with the SDL2
add-on has its own, [`.claude/skills/stdl-sdl2/SKILL.md`](.claude/skills/stdl-sdl2/SKILL.md)
(`/stdl-sdl2`).

New to the code? [`docs/internals.md`](docs/internals.md) is the map:
what a program links, how optional modules reach the core, the run from
`STDL_Init` to exit, and which interrupts the library owns. The notes
below are the lessons; that is where they sit.

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
  The BLiTTER paths run there too: `blitter.c` drives a register
  struct on the host and `tests/host/blitmodel.c` executes each
  operation, word by word, against real pointers - so a skewed pass
  reading past a surface's guard or a sprite's slack is an ASan
  failure, not a silent fetch. `test_blitter` sets
  `stdl.mach.has_blitter` and `stdl_blit_force` and compares every
  BLiTTER blit, fill and sprite with the CPU byte for byte. The model
  is written from the chip's documented behaviour and asserts on
  what STDL does not use; extend it, never copy Hatari's (GPL).
- **On-target probes for the pixel paths.** `tests/hatari/pixchk.c`
  runs the host pixel suite on the machine - the only check of the
  asm on a plain ST, where BLITCHK has nothing to compare against.
  Its default build samples three cases of each test, about 23s a
  pass, for real hardware; `-DPIXCHK_FULL` runs every case, 18
  minutes a pass on a plain ST (1,070s in Hatari, November 2026, 115s
  of it the long whole-group rows), for the emulator.
  **Time a test on the target before handing it to someone with
  hardware.** The full suite was three hours a pass on a plain ST,
  and nobody knew until a tester was left staring at a silent screen,
  because it had only ever run natively or under fast-forward, and
  the time was the suite's own checking, not the library. A
  `STDL_GetPixel` call and a 32-bit multiply per pixel compared,
  `STDL_PutPixel` with `rnd() % n` per pixel filled, and a random
  generator built on a 32-bit multiply cost the 68000 over a thousand
  cycles a pixel. Plane-word fills, a table decode and a
  shift-register generator took the full pass to 16 minutes; the
  sampling took the hardware build to seconds.
  The emulator has a ceiling too: `run.sh`'s `waitfor` gives up
  after two minutes of host time and the log simply stops, which
  reads exactly like a hang. PGCHK, given a second suite, outgrew it
  by seconds and was chased as a crash in new asm until a trace
  showed it still running; filling its test surfaces a plane word at
  a time rather than through `STDL_PutPixel` put it back at 84
  seconds. And that was the emulator: on a real Mega STE at 8MHz the
  same build took 28 minutes, the reference's exact arithmetic doing
  the work, so PGCHK now samples by default and `-DPGCHK_FULL` is the
  emulator's. Time a CHK runner after adding cases to it, on the
  machine as well as under fast-forward.
  `tests/hatari/sprcost.c` times sprites, restores, blits and fills
  (with the fixed cost of a call taken apart), twice per run so each
  run carries its own noise floor, and `tests/hatari/sccmp.py`
  compares two logs by geometric mean. `tests/hatari/blitcost.c`
  sweeps the BLiTTER's choice against the CPU forced across five
  kinds of blit - refit its thresholds from it whenever either path
  changes. `tests/hatari/sprfit.c` does the same for sprites, the
  BLiTTER forced against the CPU across shapes, art and alignment.
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
  not, which is deliberate. Tag `main`'s tip: the workflow's token may
  not move `latest` across a change to `.github/workflows` (v1.23.1's
  release was refused so, its tag behind a workflow fix on `main`). If
  it happens, run the release by hand from `main` (workflow_dispatch),
  which builds and moves `latest` to `main`'s tip.
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
- **The size ceiling is a decision, not a law.** `PIXEL_MAX` went
  26000 -> 33000 on 2026-09-15 to buy `STDL_FLAG_DISPATCH`: the blit
  row loops are specialised on the two composition flags as well as
  the plane count, which costs 6548 bytes and is worth 19% on
  ordinary masked blits, 18% on UNDER, 13% on MARK and 7% on both,
  measured on a plain ST over 400 masked 64x16 blits with the mask
  reset between runs. The 1M fit that set the old ceiling was a
  target of its own rather than a hard requirement, and the port
  that achieved it stays pinned to the version that did. Raising it
  again wants the same shape of argument: a measurement of what the
  bytes buy, and a view on which machines still have to fit.
- **Size is a test result too.** `stcmd make` runs a `sizecheck`
  target that fails the build if the pixel-path objects outgrow
  `PIXEL_MAX` in the Makefile, or the responsive screens' own objects
  (`responsive.o`, `planes8.o`, `planes8ex.o`, `planes8tile.o`,
  `planes8row.o`, `asset8.o`, `tall4.o`: a program asking for the colour
  links 26.3K of them) outgrow
  `RESP_MAX`. The archive links whole objects into
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
  The same holds for *which* operations the chip takes. Handing
  shifted keyed blits and sprites to the BLiTTER passed BLITCHK,
  PIXCHK and every port - all double-buffered - and made TSPRITE and
  TPALETTE, single-buffered, flash and tear on a real Mega STE: a
  keyed blit is XOR, AND and XOR over the whole rectangle a plane at
  a time, a sprite AND then OR, and on the displayed page the beam
  shows the state between passes. The CPU finishes a group before
  the next. `stdl_drawn_visibly` keeps those onto a single-buffered
  screen on the CPU. Hatari shows it too: record an AVI
  (`--avirecord`) of the scene and look at the frames with the
  fewest lit pixels.
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
- **Masked sections store; they do not compute.** `STDL_SetVoice`
  used to work out its step inside `stdl_int_off()`, and that
  division is a `__udivsi3` call, about a scanline at 8MHz. A
  module player calls it from the voice tick, 2ms into the frame,
  which is where the top border's Timer A lands with a line and a
  quarter of grace, so the STE lost the border every few seconds -
  with music on only, and never on a Mega STE, whose tick finishes
  before line 32. Found by breaking on the miss path in Hatari's
  debugger and reading the interrupted PC off the ISR's stack: every
  miss had interrupted the `move.w d3,sr` at the end of SetVoice.
  Any call a tick or ISR might make does its arithmetic first and
  masks only the stores. `tests/hatari/ovvoice.c` sweeps retriggers
  across the window.
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
- **"Once per process" is an assumption, and a new API can falsify
  it.** The bottom border measures its own instruction timings on
  the machine at the first open and caches them, under a comment
  saying the CPU speed does not change under a running program -
  true until `STDL_UseMegaSteSpeedup` shipped, after which a Mega
  STE calibrated at 16MHz and dropped to 8 did not open the bottom
  border at all. The cache records the mode it was taken at now and
  re-measures when that changes; a border left open across the
  change still runs against the table it was given, which the
  header and docs say. The audit is the lesson: a new call that
  changes machine state has to be checked against every "measured
  once" in the library, not only against the code it touches.

  The second lesson is about the emulator, and it was learned by
  getting it wrong out loud. **Hatari does change CPU speed when
  $FFFF8E21 is written**, as long as nothing has pinned the clock
  with `--cpuclock`: under `--machine megaste` a fixed loop measures
  303 ticks at mode 1 or 3 against 551 at mode 0, with mode 2 at 548
  - the clock alone buys almost nothing, the cache is the whole
  speedup, which matches real hardware and the BLITCHK figures in
  `stdl_video.h`. `tests/hatari/spdprob.c` is that measurement.
  So this bug was reproducible in the emulator from the day it was
  written and nobody ran the case: the example's speed key was
  tested on `st` and `ste`, where the call does nothing by
  definition. `OVAUTO.CFG`'s `speed=` line is the regression test
  (open at the boot speed, close, switch, reopen: `open=0/200`
  before the fix, `200/200` after) and a position that never opened
  now exits non-zero instead of only saying so in the file. Before
  concluding that a hardware behaviour is outside Hatari, measure
  it - the claim is cheap to check and expensive to get wrong.
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
- **Disk I/O and an open border do not mix, and no emulator will
  tell you.** `OVAUTO.CFG`'s `loaddisk=1` opens, reads and closes a
  file every frame while the bottom border runs. On a real Mega STE
  that is `open=5/300` with 295 misses from a hard drive
  (ACSI2STM GemDrive) and `3/300` with 297 from a floppy - the
  border is gone either way, and the two media are within noise of
  each other. So this is not one driver's fault: any synchronous
  transfer holds interrupts off long enough, and TOS's own floppy
  path is as bad as anything attached. The same binary and config
  in Hatari reports `300/300` and zero misses, because the emulator
  models none of it. A factor of sixty between the two instruments
  on the same question.
  So a port that opens a border must keep the disk out of the
  frame: load assets before opening, or close the border around a
  read. That belongs in the porting advice, not just here. Note the
  miss counter *does* see this one - 295 of them - which makes it a
  different signature from a border that flickers while misses read
  clean, and the two should not be assumed to share a cause.
- **Anything the hardware reads has to be in ST RAM, and malloc
  will not do it.** The Shifter and the sound DMA cannot see
  alt-RAM, and a program linked ALTALLOC - the toolchain default -
  gets alt-RAM back from `malloc` on any machine that has some. A
  screen page there displays whatever sits at the aliased ST RAM
  address: garbage, with no error. Reported from an accelerated STF
  and an STE, both with alt-RAM, and pinned down by the same binary
  running clean with the border closed, where the page comes from
  `Physbase()` and is ST RAM by construction.
  `stdl_stram_alloc` (src/stram.c) is the allocator: `Mxalloc` mode
  0, `Malloc` below TOS 1.04 where no alt-RAM exists to get wrong,
  and the returned block *checked* against PHYSTOP rather than
  trusted - the mode argument is a request and the cost of it being
  ignored is a screen of garbage. It returns NULL rather than
  falling back, and every caller fails the open with a message,
  because the tester who hit this had no way to tell what was
  wrong.
  Four allocations needed it: both overscan pages, the double-buffer
  page and both DMA sound rings. When adding anything a video base
  or a DMA pointer will aim at, it needs this too. A caller's own
  buffer cannot be fixed from here, so `STDL_PlaySample` checks and
  refuses instead - `stdl_is_stram` is two compares against the same
  ceiling.
  Do not reach for clearing the ALTALLOC flag. A 594KB port dies at
  its first allocation on a 1MB machine without alt-RAM, which is
  exactly why testers enable it.
- **Timer B has one owner, and its teardown is on the terminate
  path.** The raster service (src/raster.c) and the borders both run
  on Timer B. Before it existed, a program that wanted rasters wrote
  its own handler. Nothing stopped that coexisting with an open
  border, and an abnormal exit left the Timer B and Timer C vectors
  pointing into freed memory. `stdl_timerb_owner` in video.c now
  names the holder and each refuses the other. The raster teardown
  is a vectors-and-registers-only function on `stdl_shutdown_rasters`,
  called from release_hardware: RASTCHK aborted with rasters open,
  and the desktop came back in its own colours. The Timer C prefix
  earns its place measurably. RASTCHK holds a fixed table, and
  tests/hatari/rstill.py counts the held frames whose stripes moved:
  2 of 144 with the prefix, 16 of 144 with RASTCHK built
  -DRASTCHK_NOPREFIX (Hatari, plain ST, program idle in STDL_Delay).
  A hold that only spun showed far more without it during
  development. Colour 0 has its own handlers: one register saved,
  and the write about 20 cycles sooner. Colour 0 is also the border,
  so its write must land before the next line's left border, and the
  general handlers missed that by a few dozen cycles now and then. Any new user of Timer B, or of
  an MFP vector, wants the same three things: an owner, a teardown
  on the terminate path, and a frame-by-frame capture of a still
  scene.
- **A mouse nobody reads still costs a border.** Moving a mouse with
  the bottom border open flickers it on a real Mega STE and the
  missed-window count climbs while the movement lasts; disabling
  IKBD reporting with `STDL_EnableMouse(0)` stops both. Three ACIA
  interrupts per movement, about 1.3ms apart: channel 6 cannot
  preempt the border timers at 13 and 8, but an interrupt in
  service delays them, and the flick lands on an exact cycle. Same
  family as the disk finding - a delay, not a preemption.
  Two things measured rather than assumed on the way, both of which
  could have gone the other way. The IKBD steals joystick 1's fire
  bit into the right mouse button while the mouse reports, and
  event.c folds it back; silencing the mouse hands fire back to the
  joystick packet rather than leaving it stolen, so a joystick-only
  port does not lose its fire button - which would have been a far
  worse bug than the flicker. And the traffic does not reappear as
  joystick 0 packets once the mouse is silenced, which was the
  obvious way for this fix to change the problem's shape rather
  than remove it and would have pointed at joystick interrogation
  mode instead. Both were checked on hardware because IKBD firmware
  behaviour is not something an emulator settles.
- **Isolate resident firmware before sharpening a line-locked ISR.**
  With the placement right, the real Mega STE still flickered the
  bottom border a few times a second and a probe's opens jittered
  between runs of identical settings; both vanished with the
  cartridge's network firmware unloaded (its resident code holds
  interrupts off long enough to make Timer B late). A hardware
  jitter that is not in the emulator is an environment question
  first: reproduce with nothing resident before touching the ISR.
- **Ideas for a future stabilisation pass: how AGT opens its borders.**
  Atari Game Tools (bitbucket.org/d_m_l/agtools) has an STE-only,
  top-and-bottom display service (200, 240, 256 and 272 lines;
  `agtsys/m68k/ste/dispcom.s`, `disp272.s`). Read, not measured, and
  AGT has no licence, so techniques only, never code. Three things it
  does that STDL does differently, worth trying if the borders need
  steadying again:
  - It pauses any running BLiTTER operation at the top of its VBL
    handler, before arming the border timers, rather than placing
    each operation against the beam.
  - It does not trust when the top-border timer fires - a comment
    says blits delay Timer A - and instead syncs to the scanline with
    `move #$2100,sr` / `stop #$2100`, sleeping until the next HBL and
    counting HBLs from there. STDL polls the video counter instead.
  - The top-border timer is Timer A or Timer D, chosen at build time.
  It also has a Timer B display list ("Nickel": per-line viewport
  switches, palette bursts, register loads and callbacks), which is
  its split-screen system. It has nothing for the plain ST - the STF
  modes are only commented out - and no left or right borders.
- **Hatari's GEMDOS drive maps names to 8.3.** A scratch binary
  named `BLITCOST2.TOS` beside `BLITCOST.TOS` silently runs
  `BLITCOST.TOS`; four runs of "the fix" measured the old binary.
  Keep test program stems to eight characters.
- **A Falcon is STE-class, not an STE.** One port's ST build, run on
  an emulated Falcon (TOS 4.04, VGA), was silent at 6258Hz, refused
  to start from a 640x480 16-colour desktop and left a 320x480
  256-colour one in the wrong mode and colours. Each was the library
  trusting an STE answer:
  - 6258Hz: Hatari, which models the crossbar, mutes the DAC there.
  - `Getrez()` under a VIDEL mode names some other machine's mode
    (7 under 320x480 in 256 colours, 2 under 640x480 in 16), so it
    can neither say mono nor put the mode back.
  - `VgetRGB` read all black under a desktop whose colours the VDI
    had set; the registers have them.
  `tests/hatari/falcchk.c` shows all three, in its printout, a sound
  recording and a screenshot of the desktop after; linked against
  the release before the fix, it shows each one fail. To get those desktops in
  Hatari, pass `--tos-res ttlow` or `ttmed` with `--machine falcon`:
  autostart writes its own NEWDESK.INF, which asks for ST Low
  whatever the NVRAM says. That is TOS 4.04: EmuTOS 1.4 keeps the
  NVRAM's mode, which with Hatari's own NVRAM is 640x480 in 16
  colours on VGA (mode word 0x001a) and 0x012a on RGB, where TOS 4.04
  had ST Low (0x0192, 0x00a2); `--tos-res stlow` gives both ST Low.
  And Hatari writes its NVRAM back on every
  exit, so point `HOME` elsewhere for a run that changes it.
- **A TT is STE-class too, and neither machine's display rate is the
  sync register's.** `tests/hatari/ttchk.c` (TTCHK) measures what the
  TT and Falcon handling rests on, and was run against the release
  before it (v1.18.1) on Hatari's ST, STE, Mega STE, TT (TOS 3.06,
  `LATE="--mmu off"`, desktops `--tos-res` stlow, ttlow, ttmed, sthigh,
  and `--monitor mono` with tthigh) and Falcon (VGA, RGB):
  - Hatari's TT runs ST Low at about 60, where the library assumed 50:
    a silent STM stream played 60 frames a second against the STE's
    50, VBLCHK failed its boot rate, and every VBL-paced sequencer was
    20% fast. So the rate is measured on a TT or a Falcon (frames
    against the 200Hz timer, snapped to 50 or 60 within 2) at
    `STDL_Init` and after each mode set, and `STDL_SetRefresh` there
    only measures: on a Falcon it had reported 60 with the display at
    50. A Falcon on VGA had its desktop at 60 and ST Low at 50.6.
  - Timer B counted 200 events a frame on every machine, but Hatari
    times a TT's and a Falcon's video as an STE's, so that settles
    nothing; rasters are refused there until someone counts on the
    machines.
  - `$FF8240` lands in TT register 0-15 of `$FF8400` (bank 0), so an
    ST Low palette is right on a TT already; but a TT Low desktop uses
    all 256 registers. Writing the desktop's EgetShift word back after
    the Setscreen, tried as well, left a TT Medium desktop shown in
    another mode: Setscreen alone puts the mode back.
  - The MFP's monitor bit (GPIP 7) is the TT's mono test: a TT shows ST
    High on its colour monitor (refused for nothing before), and with
    a mono monitor and a TT High desktop the old library switched to
    ST Low and Hatari reset in a loop.
  - CACR reads 0x3111 on both (data cache on); a Falcon's BLiTTER is
    not used, and BLITCHK there was faster without it anyway (215ms
    against 275).
  - A Falcon program that leaves the crossbar elsewhere silenced the
    old library completely (FALCCHK built with `-DFALCCHK_SCRAMBLE`):
    the first DMA start routes it, and the exit puts it back.
  Each machine's run of TTCHK is about a minute; the questions Hatari
  cannot answer (a real TT's rate, Timer B, where ST Low sits on a TT
  monitor) are what to hand a tester with the hardware.
- **256 colours on a TT or a Falcon are eight interleaved planes: not
  two 4-plane halves, and not chunky.** Before `STDL_RESPONSIVE_COLOUR`
  was built, three layouts were tried on one port's game, 600 frames of a
  busy level from a fixed seed in Hatari (TT `--mmu off`, Falcon on
  VGA), ms a frame:

  | | TT drawing | TT present | Falcon drawing | Falcon present |
  |---|---|---|---|---|
  | two halves | 7.3 | 5.9 | 14.8 | 8.3 |
  | eight planes | 8.6 | 4.1 | 17.1 | 0.0 |
  | chunky, present only | | 16.0 | | 27.9 |

  - Two halves (a surface's low and high colour nibbles, each a
    4-plane surface drawn by the planar routines) reach every planar
    call at once, but Flip has to interleave them into the display on
    every machine, and a TT carries 128K more ST-RAM. Drawing only the
    low halves, a bound on any one-bank fast path, still left them
    behind on a Falcon (17.9).
  - Eight planes are the display's own layout: the screen is the page,
    a Falcon presents nothing, a TT copies each changed row to the line
    under it. Its drawing was behind only in its C sprite routine (3.2ms
    on a TT against the halves' 2.2, the same bytes moved), which
    tuning recovers; the halves' interleave cannot be tuned away.
  - The chunky row converts the same changed boxes with the 68030 c2p
    from one port's 030 build: its present alone costs more than either
    planar frame, before any drawing.
  The two planar models drew identical pages, every frame, on both
  machines. The halves model is kept as `tests/host/p8ref.c`, the
  reference `test_responsive` holds the 8-plane routines to: it is the planar
  routines run twice and shares no code with `src/planes8.c`.
  Tuned afterwards (the sprite routine's clip and addresses taken out
  of its rows, the points' mask test out of their loop), the 8 planes
  drew the same game in 11.1ms a frame on a TT and 14.1 on a Falcon,
  against the halves' 13.2 and 23.1: sprites 1.76ms against 2.20 on a
  TT. A restore's whole groups copied by memcpy, a call a row, were 12%
  slower than the loop: a game's restores are a sprite or two wide.
- **What 8 planes cost an ST.** Every pixel call tests `planes` at its
  entry: 604 bytes over the pixel path (PIXEL_MAX was raised for it,
  with the reason in the Makefile), 135-178 bytes a program, and 0.3%
  of SPRCOST's and PRIMCOST's geometric means on a plain ST (a
  clipped-out blit 5%, PutPixel and DrawChar 2.9%) against a layout
  floor of 0.03%. Putting the tests where the ST already branches off
  (a clipped-out exit) would have needed 8-plane surfaces to show
  misleading public fields - an empty clip, no pixels - and was left.
  A program that asks for the colour links about 25K more (planes8.o
  18K, responsive.o 4.5K, planes8tile.o 1.5K, planes8ex.o 1.2K, on
  2026-11-03): with libcmini one port's game keeps 107K free on a 512K
  ST (106,874 bytes after its art, 108,770 before the tiles and the
  third page), where with mintlib a 512K machine's game wanted a build
  of its own for a TT and a Falcon, as one port's 030 build is.
- **Console text lands in a responsive page's margins.** A TT shows a
  200-row screen centred in 240, and what a program printed after the
  mode set was drawn by TOS into the rows above, which nothing redraws,
  on one page only: it flickered there. A hash of the whole page
  differed between two builds for that alone: a line the program
  printed named the build. The mode set now moves the console's cursor
  past the margin, into the picture, where the program's drawing covers
  it as an ST's does (a program's own cursor escapes can still put text
  in the margin); hash the screen's rows anyway (`STDL_PresentStats.shown`,
  `stdl_bench.h`).
- **A tall screen in 16 colours needs no hooks** (`STDL_RESPONSIVE_TALL`,
  `src/tall4.c`): an ST opens its bottom border (240 of 245 lines, the
  surface capped by `stdl_ovsc_cap`), a Falcon's 4-bit VIDEL modes are
  the ST's plane layout so the surface is the page, and a TT's Flip
  compares the 4-plane page with what TT Low shows and copies what
  changed to both lines, the upper planes left at zero - so a program's
  own writes reach the screen too. `tests/hatari/tallchk.c` (TALLCHK,
  TALLCOL with the colour) draws a card to the very edge on each.
- **The display's own height is the tall screen without the ST's
  border** (`STDL_RESPONSIVE_HEIGHT`): 240 rows on a TT and a Falcon
  (on RGB in overscan, as the tall screen), an ST's 200 with no border
  and no error, so each machine shows all of its own pixels. The two
  options share one model; the border hook is an object of its own
  (`src/tallst.c`), passed by `STDL_UseResponsive` for TALL alone, so a
  program asking for the height links none of the border code: TALLHGT
  is 8,698 bytes smaller than TALLCHK stripped, as is the colour's
  pair. TALL wins where a program asks for both.
- **The 16-colour tall screen costs a TT 6.3-7.5ms a frame.** ST Low
  costs a TT nothing, but TT Low is 8 planes, so without the colour
  every Flip compares the whole 4-plane page with the display (20
  groups by 240 rows, two longs each) and copies what changed. One
  port's bench (Hatari, a TT with no TT-RAM, 600 frames of play): the
  drawing took 3.8ms a frame, as on ST Low, and the present 6.3 to
  7.5ms in builds of the same tall4.c, for 175 groups copied - the
  compare's 75K of ST-RAM reads are the cost, not the copy, and where
  the pages fall moves it. The colour at 200 rows presented in 4.1ms,
  the box drawn (1,837 groups). So on a TT the tall 16-colour screen
  costs more than the colour; recommend it there only with that cost.
- **The console tells a TT's Flip when it has written.** TOS writes
  its text to page 0 (its Logbase since the mode set) in TT Low's own
  layout: all eight planes, both lines of each row, the bands too. The
  tall 16-colour screen's compare reads planes 0-3 of the even lines,
  and the 8-plane screen copies the box drawn to the odd lines, so text
  printed after the mode set showed through the picture for good (that
  bench's start-up lines, dark over the play, and its hashes moving
  with what was printed). Comparing all eight planes on both lines
  would have tripled the compare's reads, and a clear at the mode set
  comes before the text. So responsive.c chains a hook onto TOS's
  xconout vectors (CON: and the raw console, $57E), which every
  character to the screen goes through: it sets a flag and goes on to
  TOS's routine, and the Flip that next shows page 0 clears its bands
  and presents it whole. Nothing a frame where nothing was printed, and
  once after printing (CONCHK on a TT in Hatari) 13.6ms against 7.7 on
  the tall 16-colour screen, and 10.4 against 8.8 with the colour,
  whose box was the whole screen already: its bands. The
  vectors go back in release_hardware (`stdl_shutdown_console`), and a
  second mode set does not chain the hook onto itself. A Falcon's
  console writes the program's own page, as an ST's does, so it has no
  hook. `tests/hatari/conchk.c` (CONCHK, CONCOL with the colour) prints
  in the middle of play and times the present.
- **A bank's 256-colour art is an option of its own**
  (`STDL_RESPONSIVE_BANKS`, `src/asset8.c`, `docs/format.md` 6.1).
  Reached through the 8-plane table it was in every program asking for
  the colour, banks or none (2.3K then; one port's game asks for the
  colour and loads no bank); as an option the inline
  `STDL_UseResponsive` links it only where it is named. It was five
  chunk types, an id's own art and the planes over the ST's for
  surfaces and sprites each; it is three (`docs/format.md` 6), a
  surface8's form byte saying which, so one lookup finds either and a
  bank cannot hold two 8-plane versions of an id: asset8.o went from
  2.6K to 1.7K, and with tilesets it is 2.1K built for size (`-Os`,
  2.5K at -O2: it runs once an asset). A sprite's frames and a
  tileset's tiles over the ST's are one walk (`a8_cells`), cells cut
  across the art and then down. That inline is `always_inline`: gcc 4.6 left
  a copy out of line in that game, which calls it twice from one large
  function, with `what` a variable there, and so linked every option -
  the ST's border code and this, 11.4K of its bench - since the height
  went in. `asset.c` asks it through one out-of-line call
  for its three loaders, 116 bytes in every program with a bank (three
  checks inline cost 244), and hands it the bank reader, as planes8 is
  handed the polygon rasteriser, so it links no reader. A payload is a
  malloc of its own and every header an even length, so its plane words
  are aligned where they lie and are used there; an id's own art goes
  straight into the surface (`stdl_be_words`, one memcpy on the
  machine). On the host no loader's plane words had been tested, and
  they were never swapped for its byte order; `stdl_be_words` swaps
  them there, the Degas loaders' too.
  `test_responsive`'s bank test writes its own bank; `test_bank8conv`
  runs stdlconv on art Pillow makes (skipped without it), and BANKCHK
  is the same on the machine.
- **A mode set from a responsive screen to an ST's took nothing down**:
  a TT or a Falcon stayed in its own mode under the ST's pages, and
  `stdl_resp_screen` stayed set, so surfaces made after it had 8 planes.
  `resp_enter`, which video.c calls at the start of every mode set,
  takes a responsive screen already up down first, and where the new
  one is an ST's, sets ST Low on its page. BANKCHK found it.
- **The blit flags on 8 planes keep out of the plain blit's way.**
  UNDER and MARK (for a layer whose mask is a priority plane, as one
  port's is) go to `p8_blit_ex`, a CPU path of its own, a group at a
  time from 4 planes or 8. Inlined, gcc grew `p8_blit` by half and
  moved every slot of its frame; out of line (`noinline`) `p8_blit`
  keeps its loops' instructions and frame, a call at its top. Called
  after `p8_blit`'s own clip instead, it reshaped `p8_blit` again (its
  frame 4 bytes smaller, its blocks reordered), so it clips for
  itself. Its rows are inline, the source's group index stepped by one
  a group and a source group past an edge read from a zero group. At
  -O2 it was 1.7K, so it is an object of its own built with `-Os`
  (`planes8ex.c`, 1.2K). gcc 4.6's `optimize("Os")` on the function did
  as well inside planes8.c, but one such attribute anywhere in a file
  changes the code of every function in it, wherever it stands and
  whether or not the declaration carries it: the sprite blit, its
  source untouched, merged two tails and took a branch more a row.
  Compare every function's code with the last commit's after touching
  gcc's options.
  On a Falcon one port's bench (256 colours, Hatari) moves with code
  placement more than with any of this: its restores took 6.85 to
  7.40ms a frame over five builds, 6.85 and 7.13 with `p8_blit` the same
  and `planes8ex.o` left out or linked in (`-Wl,-u`). The hand-off at
  `p8_blit`'s top (7.35) and a table entry choosing the path instead
  (7.40) could not be told apart, so the hand-off stayed; against the
  last commit the frames are 0.7% slower there, 0.8% on a TT, the same
  on an STE.
  `STDL_CreateSurfaceFrom8Planes`, a surface over a program's own
  8-plane block, is 250 bytes in planes8.o; it is a call of its own so
  that `STDL_CreateSurfaceFrom`, in every program that borrows a block,
  stays the 4-plane call it was. Its name says planes because a
  trailing 8 elsewhere in the library means 8 bits (STDL_PutGroup8's
  bytes, STDL_SurfaceFromIndexed8's byte a pixel), on 4 planes.
- **Tiles on 8 planes are an object of their own** (`planes8tile.c`),
  reached through the 8-plane table as the sprites are, so planes8.o's
  code stays as it was (its table three entries longer). An 8-plane
  tile's group is a surface's four longs, a masked one's with a mask
  long in front as a sprite's; a 4-plane tile draws onto 8 planes in
  colours 0-15, a plain one as a masked one with nothing transparent.
  Built with `-Os`: 1,524 bytes where -O2 made 1,808, the merges the
  same instructions, the count a subtract and compare where -O2 had a
  dbf. `tileset.c` hands a tile over where either side has 8 planes,
  as `sprite.c` does a sprite, which costs an ST's tile two
  instructions: PRIMCOST's 16x16 tile 380.8us against 383.3-385.7 on a
  plain ST (0.7%), the masked one inside the bench's noise. Every
  mutation tried was caught only once a frame drew a tile alone: the
  random run draws over the whole screen every frame, so its box
  could not see a tile's box a group short, and a sheet keyed on
  colour 0 has no bits for the planes to keep under the mask.
- **A responsive screen's third page** (`STDL_TRIPLEBUF`) is one more
  of the display's, its box its own; Flip waits as an ST's
  triple-buffered flip does, for the last request to have been taken,
  and the tall 16-colour screen gives it a 4-plane copy of its own.
  Without the memory for the display's page, or for that copy, the
  screen has two pages, as an ST's. `stdl_host_stram_left` makes the
  host's ST RAM run out on cue for the test. TRIPRESP
  (`tests/hatari/tripchk.c` built with `-DTRIPRESP`) times it, its
  frames 1.5 and 2.4 of the VBLs it measures: 40 of the first took
  59-60 VBLs against 80 double-buffered on a TT and on a Falcon on VGA
  and RGB (Hatari, cycle-exact; all three at 60Hz there), 40 of the
  second 95-96 against 120, and the video counter was never in the page
  handed back.
- **Staged colours on an 8-plane screen go with their page.**
  `STDL_StagePalette` promised it, and an 8-plane screen wrote them at
  once, so colours set before Flip showed on the page before for the
  rest of its frame.
  - They are converted where they are set and written by staging's
    flush, which the voices' VBL calls before it mixes, as their slot
    can come first in TOS's queue.
  - Flip holds them out of any blank before its page is taken. A TT's
    present ran through one (the whole page after the console wrote,
    the best part of a frame), and a triple-buffered flip's wait spans
    one when frames are quicker than the display.
  - STG8CHK (`tests/hatari/stg8chk.c`) logs at every blank the page
    taken and colour 1's register, with four voices mixing. Every blank
    agreed on a TT and on a Falcon on VGA and RGB (NTSC at 60Hz, PAL at
    50), double-buffered, triple at 1.5 VBLs and triple at none. The
    library before wrote every staged colour at once and was wrong in
    59 of 61 blanks at none, as was Flip without its hold.
  - On a Falcon the log ran at VFC 16-36 half-lines against a picture
    from VDB 63-127, so the colours are in before it. A TT's video
    counter reads 0 in Hatari: its timing is a question for hardware.
- **A Falcon's RGB mode takes the desktop's PAL or NTSC.** It always
  asked for PAL. In Hatari TOS 4.04 still gave an NTSC desktop its own
  mode (0x0003, 60Hz), which is why RGB ran at 60 there, and a PAL one
  gets 0x0023 at 50.
- **Bound a group by the surface's width, not its stride.** A Falcon's
  overscan line is 384 pixels with the 320 centred, so the screen's
  stride runs past the picture: STDL_PutGroup on an 8-plane screen,
  bounded by the stride, wrote into the margin and the next line.
- **The host suite runs on the machine too** (`RESPCHK.TOS`, built from
  `tests/host/test_responsive.c` as its main says): the same calls
  against the reference on the TT's or the Falcon's own screen, page
  hashes where the host keeps pages. Each part says its time (`RC:`
  lines; `TEST_TIMES=1` on the host). It took 152s of a TT's time, three
  minutes in Hatari (and `test_rows8`, the block blits, 11s more since
  November 2026: its first version took 31), and longer than `run.sh`'s
  two-minute `waitfor`:
  chain several, or run.sh stops the emulator (see the SIGTERM note
  below) before the end. It had grown to 90 minutes in Hatari:
  - the page hash was FNV's, a 32-bit multiply a byte, which on a
    68000 is a `__mulsi3` call: 153.6K of them a page, every page of
    every frame. It is h * 33 + each long now, a shift and an add;
  - the reference drew a polygon's coverage a pixel at a time through
    GetPixel and PutPixel on both halves, four calls a pixel over
    polygons the size of the screen. It works a group at a time on the
    halves' planes now, as the 4-plane PutPixel writes each pixel. A
    `compare()` went from 211s to 29s;
  - `test_mixed` read the whole screen back after each of 120 blits. It
    reads the box and a group round it, the whole screen every eighth
    and at the end: 25s.

  Each was broken on purpose to see the checks still fail, and two
  polygon mutations the reference had never caught were (a colour
  leaving the mask, a hole keeping its colour): the random run now
  draws a polygon onto its masked surface every frame, last.

  On a Falcon it failed one check, under TOS 4.04 and EmuTOS alike,
  on VGA and RGB (November 2026): `test_tiles` asked for the box a
  TT's Flip doubles, and a Falcon keeps none (`p8_screen_init` clears
  `stdl_rs.track`, as the page drawn into is the one shown), so the
  box was still the empty one (`the tile's box: 1,0 to 0,199`);
  `SET_MACHINE` does nothing on the machine. The box checks
  (`test_tiles`, `test_rows8`) now go by `stdl_rs.falcon`: a TT must
  keep the box, a Falcon must keep none - not by the track itself,
  which would pass a TT that lost it. RESPCHK passes on both.
- **Hatari ignores `run.sh`'s SIGTERM while it waits to confirm a
  quit.** It was first seen with `--machine tt` and `falcon` and taken
  for theirs: `run.sh` waited out its caller's `timeout`, and 61
  emulators had piled up over one session. What holds it is Hatari's
  own default, which asks before it quits: a configuration of its own
  (a `HOME` or `--configfile` without the key) asks, and under
  `SDL_VIDEODRIVER=dummy` no window is there to answer. A TERM went
  unanswered on st, ste, tt and falcon alike, with EmuTOS and with TOS
  3.06 (Hatari 2.6.1, November 2026); with `--confirm-quit false` 164
  of 164 TT and Falcon runs ended on it. Runs with a window were not
  tried again. `run.sh` passes that itself
  now, and sends -9 ten seconds after a TERM that did not end the
  emulator, saying so on stderr: another of Hatari's questions holds
  it as well, such as the one `--ttram` without `--addr24 off` asks
  before TOS starts (no program started then, under either TOS).
- **EmuTOS's 512K image stands in for TOS 3.06 and 4.04** (EmuTOS 1.4,
  `etos512uk.img`; its 256K image has no TT or Falcon video, NVRAM or
  second MFP). Under Hatari 2.6.1, cycle-exact, 4MB, a TT `--mmu off`
  and a Falcon on VGA and RGB (PAL, and NTSC from the NVRAM), each of
  TTCHK, FALCCHK (and `-DFALCCHK_SCRAMBLE`), RESPCHK, TALLCHK, TALLCOL,
  TALLHGT, STG8CHK, CONCHK, CONCOL, SDL2RCHK, SDL2RCH2, SDL2AUD and
  SDL2DEMO was run under both, on the desktops TTCHK and FALCCHK were
  run from before, 168 runs in pairs (November 2026):
  - every check said the same: the cookies, STDL's machine info, the
    modes' words (0x0113 on VGA, 0x0023 RGB PAL, 0x0003 NTSC), the
    rates (60 on a TT; a Falcon's ST Low 50 on VGA and RGB PAL and 60
    NTSC, its 8-bit screen 60 on VGA), STG8CHK's 0 of 61-124 blanks
    in another page's colour, FALCCHK's triangle (97 crossings a
    second in both recordings) and both samples played, the crossbar
    put back, SDL2RCHK and SDL2RCH2 with no failures, SDL2AUD's specs
    all ok (its drain rates a half's refill apart, 182 or 184 calls at
    22050Hz, 0.1-0.7% off, either way round); RESPCHK passed on
    a TT under both and failed the same one check on a Falcon under
    both, a check of the test's since fixed (its entry above);
  - the programs' own pictures were the same pixel for pixel (TTCHK's
    cards, FALCCHK's bars, both pages of each tall card), and the
    desktop after each came back in the mode and colours it had after
    a program that leaves the screen alone, under both;
  - EmuTOS's own differences: the Falcon desktop (in FALCCHK's entry);
    Getrez() 2 under 320x480 in 256 colours where TOS 4.04 says 7;
    console text in an 8-bit mode drawn magenta, where TOS 3.06 and
    4.04 draw it white; the largest free ST-RAM block (3679K against
    TOS 3.06's 3670K, on a Falcon with an ST Low desktop 3798K against
    3762K, and hundreds of K apart with other desktop modes); STG8CHK's
    log a half-line or two later in the blank (VFC 17-37 against
    16-35, VDB 63 or more); and its boot takes another time (a
    Falcon's TALLCHK ended 11s after the start against 16s, a TT's
    checks a few seconds later than under TOS 3.06), so a screenshot
    taken a fixed time after the start catches another moment (shoot
    in a burst, or after a marker);
  - timings on a TT moved by up to 10% either way between the two
    (RESPCHK's parts, in whole seconds, 30-34s against 28-31s, EmuTOS
    the slower; CONCHK's present 6.35ms against 6.90, EmuTOS the
    quicker; CONCOL's 8.77ms both), on a Falcon hardly (RESPCHK's ten
    timed parts on VGA and RGB the same whole seconds, 49-56s, but one
    a second apart); compare timings under one TOS.
  The runs are `tests/hatari/out/e_*` (EmuTOS) beside `r_*` (TOS).
- **The Mega STE's cache decides the bottom border's flick, word by
  word.** Drawing with a border open flickered the bottom border at
  16MHz with the cache on, never at 8MHz or with it off, and worst
  through the BLiTTER. This file used to blame the blit policy and
  recommend `STDL_UseBlitter(0)`; the cause was the cache, and it is
  fixed in the ISR (2026-10-06).
  The facts that settle it are in Hatari's source, from Christian
  Zietz's reading of the schematics: the cache is 8192 lines of
  *one word each*, direct-mapped on address bits 1-13, it is emptied
  *entirely* whenever another master takes the bus - every BLiTTER
  operation - and every word the CPU reads or writes replaces the
  line it maps to. A miss costs a 16MHz CPU up to a bus slot. So by
  the time the bottom ISR runs, the frame's drawing has decided which
  words of the flick are cold, and its warm-up pass, which entered
  the path at the table read with slot 15's entries, left the poll's
  branch target, the nops slot 15 skips and both table words of the
  slot actually caught to be fetched cold inside a window of a few
  cycles.
  `tests/hatari/ovcache.c` proved it on the machine by toggling one
  thing at a time in one binary, 150 frames each, 16MHz with the
  cache: idle 146 open; clear-and-set the cache bit once a frame -
  no BLiTTER, no drawing at all - 30; one BLiTTER operation of one
  word 25; a 16-row fill 43; a 24K CPU copy 53. With the cache off,
  150 in every case. The ISR now reads all 32 table words in at its
  head on a cached CPU, runs the whole path from the poll's entry
  with a table of zeros (no turns, every nop), keeps its code, tables
  and variables in one block so no two can share a line, and returns
  from the flick through a register so no stack write lands between
  the warm pass and the 50Hz write. Same binary, same boot, every
  case above: 150 of 150 (149 with the copy). The demo's overscan
  part lost the bottom border in 37% of frames on that machine
  before.
  Two lessons. The pre-touch tried on 2026-09-17 read four words 16
  bytes apart, on the assumption of 16-byte lines; with one-word
  lines it warmed 4 of the 32 words, and "the same either way" was
  measuring nothing. An A/B is only a control if the toggled thing
  can change the state, and that includes the granularity of the
  hardware it acts on - look it up before designing the toggle. And
  the earlier experiments here (policy off made the whole screen
  flicker, pause off made the bottom flicker constantly) were all
  run on top of a flick that was already failing; changing how the
  BLiTTER was driven moved the shape of the failure, never its
  cause. When every lever moves the symptom and none removes it,
  look for something all of them share - here, that every BLiTTER
  operation flushes the cache.
  Hatari cannot show any of it: at 16MHz its video counter is not
  cycle-correct, so the border takes the Timer B path there, which
  reads no table. Any future change to the bottom ISR wants
  OVCACHE on the machine.
  A caution about reading hardware reports, which cost a wrong
  entry in this file. "The top half of the top border is wrong" was
  read here as half the screen and written up as a fill running
  with the wrong extent, which would have been a far more serious
  bug. A photograph settled it: the band was about sixteen rows of
  273, exactly the fill the probe asks for, landing in the top
  border because the probe draws at y=0 and the top border is the
  28 rows above the old picture. Get the picture before theorising
  from the words, and put a probe's own drawing somewhere it cannot
  be mistaken for the thing being measured.
- **Tested and dead: memcpy's `movem.l` as a border disturber.** A
  port's game flickers its bottom border in proportion to how much
  it draws - solid with the top border alone, flickering in play,
  worst in cutscenes, and stable while an inventory screen redraws
  45 icons a frame. That is a dose-response against copy volume,
  and it survives 8MHz, so it is neither the clock, the cache nor
  the BLiTTER, all excluded by hardware tests.
  The mechanism that fitted was interrupt latency: mintlib's
  `memcpy` moves eleven registers per `movem.l`
  (`d1-d7/a2-a3/a5-fp`), which is 12 + 8*11 = about 100 cycles
  during which a 68000 cannot take an interrupt, against a flick
  pulse of 12-28 cycles that has to land on an exact one - and
  `blit.c` calls `memcpy` for every long row, so the library does
  this to itself. It is a good story and it is wrong. `OVAUTO.CFG`
  gained `loadcopy=N` and `blitter=0` to test it; eight full-width
  CPU copies a frame on `--machine st` gives 300/300 with zero
  misses. The ISR polls for its line rather than relying on exact
  entry, which is presumably what absorbs the jitter.
  Recorded so nobody spends a day on the same reasoning. At 16MHz
  with the cache on, drawing volume did move the border, through the
  cache (see the Mega STE cache entry above: a 24K CPU copy a frame
  opened it 53 times in 150, fixed since). What that does not
  explain is the same port's report at 8MHz, where the cache cannot
  be on; if it recurs on a build with the cache fix, it is something
  else.
- **The unblocking flip is `STDL_TRIPLEBUF`.** `STDL_Flip` waits
  for the vertical blank, so a port whose frame takes 34-40ms is
  quantised to 40ms - 25fps on a 50Hz display - and pays half a VBL
  a frame on average. A third page lets the next frame start at once
  (2026-10-26); the flip then waits only for the previous request to
  have been taken, before making its own, so a program faster than
  the display is held to it with every frame shown, rather than
  drawing frames a later request replaces. `tests/hatari/tripchk.c`
  watches the video counter for the page handed back being on the
  screen and times 0-, 1.5- and 2.25-VBL frames against double
  buffering; drop the wait and it fails both ways. With a border
  open, overscan.c's own two pages flip as before.
- **Parked, measured: an asm middle block for wide unshifted
  sprites.** Carrying the previous source group's five words in the
  high halves of d0-d4 (move.w (a0)+ into the low half, a copy
  shifted by r, swap) fetches each source word once instead of
  twice, and took 64x32 unshifted sprites at odd phases 12-19%
  faster on a plain ST. It cost 1696 bytes of pixel-path text and
  bought nothing below two groups between the edge columns - at one,
  the call and the column split made 32x32 up to 8% slower, so it
  had to be gated to wide sprites only, and inlined instead it made
  phase-0 sprites 4-6% slower through register pressure alone. Wide
  unshifted sprites are the case the docs already steer to PRESHIFT
  or, on a BLiTTER machine, to the chip. The code is on the local
  branch `exp/sprite-wide-asm`; revisit if a port without the RAM
  for PRESHIFT draws wide sprites on a plain ST and the size budget
  has room.
- **Measured and rejected: "endmask" sprites on the BLiTTER.** One
  copy per sprite row, YCOUNT walking the planes and the row's mask
  in EM1-EM3, halves the chip's bus traffic against the two passes a
  plane `STDL_BlitSprite` makes - and costs more, because the CPU
  programs the chip once a row instead of once a pass. In its best
  case (masks precomputed, no clipping) the extra cost per row was
  38.7us against 24.6 on an STE for a 16px sprite, 42.6 against 37.1
  at 32px, and 31 against 12 on a Mega STE; the real thing adds a
  mask shift per row. Its total looked 40% faster only because the
  probe had no entry cost to pay. The engine it comes from makes it
  pay with generated code and the chip's registers held in address
  registers, a subsystem rather than a change. The probe is on the
  local branch `exp/ems`. Compare techniques by the slope - the same
  width at two heights - not by one total.
- **Possible future improvement: what a declined BLiTTER costs.** On
  a machine with a BLiTTER, every blit the size rules give the CPU
  still pays for asking: `stdl_blitter_wants` is out of line, and the
  call with its five arguments measured about 80us on an STE - a
  16x16 aligned restore 14% slower with the BLiTTER allowed than
  refused, a 16x1 blit 21%. A plain ST never asks. Tried and
  reverted (2026-09-29): an inline no-multiply test in front of the
  call for the shapes that are certainly the CPU's (same phase, h <=
  16, whole groups up to 128 wide, or keyed up to 33). It won exactly
  those two (0.91, 0.89) and cost everything else in
  `STDL_BlitSurfaceEx` 1-3% on both machines through register
  allocation - the entry 3.1% on a plain ST - and 96 bytes past the
  size budget. The answer probably has to come from outside that
  function (a caller that knows its shapes, or a decision made once
  per surface pair), not from another test inside it.
- **Possible future improvement: BLiTTER fills a plane at a time.**
  A fill the chip does in several passes (a colour other than 0 or
  15, or partial edge groups) calls `stdl_blitter_run` once a plane,
  saving and restoring eight registers each time. Making those passes
  inline (`stdl_blit_pass`, out of line as a group) measured 15-21%
  faster for such fills on an STE (2026-09-29) - and cost the
  single-operation clears in 0 and 15, and the small fills the CPU
  keeps, 0.6-1.4%, through fill_rows' register allocation, with its
  entry code unchanged; moving the whole BLiTTER fill out of line
  instead cost those clears 3-9%. Reverted for the regression.
  Worth another arrangement if a port's frame shows multi-plane
  BLiTTER fills.
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
- **Three in a row have had the same shape: right in the source,
  absent on the machine, invisible to every check but a print.** An
  optimisation behind a run-time predicate that never fired; a test
  that could not fail; and a guarantee the compiler deleted. Each
  looked correct in review, each passed the suite, and each was
  found only when something printed what the machine actually did.
  The rule that falls out: for anything that is conditional at run
  time, guarantee what you can, assert it where the assert can fail,
  and print it from the target in a tool people already run -
  `dist/BLITCHK.TOS` prints the row alignment for this reason.

  The family keeps growing, and a port working against this library
  has hit the same shape independently: a counter incremented but
  never printed, whose number was quoted for days; and a review
  agent that costed a function at 3-10ms a frame from its 987 call
  sites in the source, where counting at run time found 11-21 calls
  a frame and about 3.5ms. A grep measures the source, not the
  frame, and an agent's confident number derived from structure
  reads exactly like a measured one - so a review of this library's
  hot paths says which of the two it is holding. The sharpest statement of the compiler case is
  theirs: **when you are checking an assumption the compiler is also
  allowed to make, the check has to come from outside the
  compiler** - which is why the malloc alignment above needed a
  disassembly and a print from the target, and why the C assert
  that looked like it covered it could not.
- **A performance fix is not confirmed until it survives a control
  that changes one thing.** Twice this week a fix "worked" because
  it reshuffled the heap rather than because of its mechanism. The
  control that settles it keeps the suspected mechanism and removes
  only what you believe is incidental: allocate the spare word but
  do not align it, add the instrumentation but not the change. A
  crude perturbation - a kilobyte of padding, an instrumented build
  - moves the symptom without telling you which half moved it, and
  reads as noise when it is a signal you failed to aim. Both of the
  wrong diagnoses here passed a crude perturbation and died to an
  aimed one.
- **A compiler can delete a guarantee about malloc.** gcc knows
  malloc returns memory aligned to MALLOC_ABI_ALIGNMENT (4 here), so
  it folded `STDL_CreateSurface`'s long-alignment adjustment to a
  constant zero and dropped the arithmetic: the function compiled to
  a literal `clrb` with no and-with-3 in it. mintlib's malloc does
  not honour that at run time, so the optimiser had assumed away the
  exact case the code existed for. `launder()` in surface.c hides
  the pointer's provenance behind an empty asm. Check this class by
  disassembling, never by reading the source - and note that a C
  assertion on `pixels & 3` right after the call folds to true by
  the same reasoning.
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
  guarantee what you can, assert it, and print it. (This predicate
  has since gone - see the next note - but the lesson stands.)

  Be careful where the assert lives. A host test asserting that
  `STDL_CreateSurface` returns long-aligned rows passes whether or
  not the code does anything, because host malloc is generously
  aligned and mintlib's is not - the guarantee is real work only on
  target, so that is where it has to be checked, and
  `tests/hatari/blitcost.c` prints it. What the host *can* test is
  the other side: `tests/host/test_blit8.c` hands
  `STDL_CreateSurfaceFrom` a deliberately word-aligned block and
  compares the result against an aligned one, which fails if a
  copy mishandles a word-aligned block. A test that cannot fail is worse
  than no test, because it is counted.
- **`memcpy` for a tile row is nearly all prologue.** The aligned
  fast path called it once per row, which for a 16x16 tile is eight
  bytes a call: measured about 650 cycles at 16MHz to move what two
  `move.l` do in thirty. Rows up to `BLIT_INLINE_MAX` are copied
  inline now, as longs at any even address: a 68000 faults only on
  an odd one. There was a long-alignment test here once, sending
  word-aligned borrowed surfaces to a word loop, and it cost more
  than it saved. A change elsewhere in the function made gcc compile
  that loop to indexed moves with a stack compare a word, and a port
  whose tiles are carved from one bank at word offsets found every
  16x16 tile blit a quarter slower on a plain ST than the release
  before. Dropping the test made its frames 18% faster than that
  release. A fallback that exists only for speed is a second path
  whose code nobody measures; when the machine does not need it,
  delete it.
  Code that shares plane memory as longs uses `stdl_wlong`
  (stdl_internal.h), which is `uint32_t` on the target and tells the
  host's alignment sanitizer the 68000's rule.
  `memset` is the same story at a larger size: mintlib's costs about
  70us a call on a plain ST before it stores anything, so the fill
  path that cleared each row of a 0 or 15 fill with one lost to a
  plain store loop at every screen width (a 16x16 clear 1347us
  against 745, 320x32 4687 against 4375). Only a contiguous block,
  or rows of 32 groups and more, still take it.
- **After touching blit/fill paths**: run `dist/BLITCHK.TOS`, about
  two minutes on a Mega STE (longer than `run.sh`'s two-minute
  `waitfor`: chain two), a dot a second while it works - it
  randomises fills/blits and compares the CPU and BLiTTER paths
  byte-for-byte on target. Both paths must stay identical;
  `STDL_UseBlitter(0)` forces CPU. It runs every plane budget twice,
  once with `stdl_blit_force` sending every eligible operation to the
  BLiTTER whatever its size (it links the counted build of the pixel
  objects for that), and fails a forced pass the BLiTTER never took
  part in - a comparison of the CPU with itself passes whatever the
  BLiTTER code does.
- **The BLiTTER never gets NFSR.** On the real chip NFSR does not
  behave as commonly described, and with one-word lines it gives
  wrong results. Unaligned blits and sprites use SKEW alone for a
  right shift and SKEW with FXSR for a left one, so a line reads at
  most one source word past its span; surfaces carry guard bytes and
  sprite data one group of slack (`SPR_SLACK`) for that word. With
  FXSR a line fetches one word more than it writes, and the source's
  y increment and the border policy's split path both count it.
- **The BLiTTER sees ST RAM only.** A surface a program allocated with
  plain malloc on a machine with alt-RAM may not be there, and the
  chip would read and write whatever ST RAM aliases it. `STDL_Init`
  records whether any alt-RAM exists; only then are a surface's
  addresses compared with PHYSTOP before the BLiTTER is chosen, and
  the CPU takes whatever is out of reach. Asking every surface on
  every machine cost BLiTTER blits 3% on an STE. Hatari cannot give
  an STE alt-RAM, so the refusal is untested on the case it guards.
- **Code added inside STDL_BlitSurfaceEx moves its CPU paths.** gcc
  4.6 allocates registers for the whole function at once, and every
  arrangement of the BLiTTER's decision and work tried inside it -
  inline, as a call from the shift chain, folded into one out-of-line
  worker - made some plain-ST CPU blit 1-7% slower, a machine that
  never reaches the new code. What survived: the size rules
  (`stdl_blitter_wants`) and the work (`stdl_blitter_blit`) both out of
  line in blitter.c, behind an inline two-byte test; and the hottest
  small loop (whole-group row copies) as asm with register operands,
  which gcc cannot spill whatever else changes. Moving every row loop
  out of line behind a job struct was also tried and made small
  blits 5-25% slower - the function's fixed cost is its own clip and
  set-up, not register pressure from the loops. Even a type counts:
  the row strides read as `int16_t` on every path, for rows run bottom
  up, made keyed blits 1.2-1.7% slower (November 2026); only the
  unmasked same-phase paths read them signed. Measure every change to
  that function on `st`, all 85 SPRCOST cases, not the one you meant
  to change.
- **Scrolling without scroll registers is a copy, and the copy is the
  frame** (SCRLCOST, `tests/hatari/scrlcost.c`, November 2026: st, ste,
  megaste, a TT and a Falcon on VGA, cycle-exact, EmuTOS 256K and 512K).
  On a plain ST a 320x200 redraw of 16x16 tiles is 97.9ms and the whole
  page through `STDL_BlitSurface` with a NULL srcrect, one `memcpy`,
  19.3ms. The probe's first run proposed three library speed-ups, and
  all three are built, before and after under the same TOS (ms a frame,
  the CPU's column on an STE and a Mega STE):

  | | plain ST | STE | Mega STE | TT | Falcon |
  |---|---|---|---|---|---|
  | a window out of a wider buffer | 38.0 / 20.7 | 38.0 / 20.7 | 29.3 / 19.0 | 15.7 / 8.2 | 31.4 / 16.5 |
  | the probe's own movem loop | 19.5 | 19.5 | 18.3 | 8.1 | 16.2 |
  | a window across a ring's seams | 53.8 / 26.4 | 53.8 / 26.4 | 38.0 / 22.7 | 22.7 / 8.8 | 45.7 / 17.7 |
  | a window at a sub-group x | 132-164, the shift chain, as was | | | 117.5 / 12.4 | 235.8 / 24.9 |
  | a whole-page 1-pixel ROXL | 37.3 | | | 10.2 | 20.3 |

  - **4 planes: rows of whole groups longer than `BLIT_INLINE_MAX`**
    went through a `memcpy` call a row. They go to `src/rowcopy.c`
    now: ten longs a `movem.l`, eight of them unrolled with the stores
    at fixed offsets and entered by a jump, every row of a strip of up
    to 320 bytes, then the rows' last 0-32 bytes in a pass of their
    own, all in one call from `blit_rows_long`, out of line in blit.c.
    A row costs its units and 48 cycles (the jump, the strides, the
    count sharing d0 with the jump's offset): the 1.2ms between the
    library's 20.7 and the probe's 19.5. A ring's window is two pieces
    a row, each paying that and a tail pass, hence 26.4 and not 20; a
    layout with no seam (the skill's scan-walk) is the plain window.
  - **8 planes, the same phase:** the same copy, from
    `src/planes8row.c` for blits of 64 pixels and more; their edge
    columns, where the destination's x or end is off a group, go as the
    shifted blits' do, a long copied under the mask. Without them those
    went a row at a time: a 304-pixel panel at x = 8 took 14.9ms on a
    TT and 29.9 on a Falcon, 9.3 and 18.6 now.
  - **8 planes, another phase:** the whole groups between the edges in
    `stdl_p8_shift_rows` (planes8row.c): a plane pair is a load, a
    rotate, a swap and a merge, one rotation serving two groups; the
    edges a group column at a time as the row loop draws them, their
    source groups chosen once and walked by the stride (three mulu.w
    a row an edge before: the ring below 5.6% slower on both). A ring
    scrolled a pixel a frame went from 122.0ms (worst frame 130.9) to
    21.2 (41.8) on a TT and from 245.2 (263.1) to 42.4 (83.8) on a
    Falcon; sixteen pre-shifted rings with sprite columns from 24.6
    (26.3) to 13.1 (17.5) and 49.5 (52.3) to 26.2 (35.1).
  - **What they decline.** Passes are right for any rows that do not
    overlap, and for an overlap with the destination at a lower address
    than the source in one pass (whole units, one strip), as a forward
    copy is; `stdl_copy_rows_checked` tests the rows' extents and
    declines the rest, which go a row at a time as before (on 4 planes
    `memmove`, which keeps rightwards in a row right). 8-plane blits
    with an edge column or a shift decline any overlap, as their edges
    go before their middles. Every overlap SCRLCOST checks came out as before: right on
    the 4-plane CPU, and on an STE, a Mega STE, a TT and a Falcon wrong
    in the same four checks by the same pixel counts (all right since:
    "A blit onto its own memory" below).
  - **The cost to everything else.** SPRCOST on a plain ST, all 85
    cases: geometric mean 1.0001 against same-binary floors of 1.0023
    and 1.0018, four cases a single 5ms tick slower (up to 0.8%; two
    of them fills, whose code did not change). Run with ten
    times the measuring window (`MINTICKS` 1200), no case was slower in
    every sample, mean 0.9998 against floors of 1.0002, and aligned
    restores 0.3-0.6% faster. Only `blit.o` and `planes8.o` changed of
    the old objects (`objdump -dr`; `event.o`, `stdl_xpad.o` and
    `time.o` only in the names gcc numbers function-local statics by,
    shifted by the new prototypes). On a TT and a Falcon small
    unaligned 8-plane blits are 1-3% slower (13 16x16 tile blits 18.8
    to 19.3ms, 38.0 to 39.0) and restores 2% faster, inside the layout
    floor: the old library's own objects relinked, forwards and
    reversed, moved that case to 19.0 and 38.7 and others by up to 5%
    (a TT's memmove-and-strip frame 12.6 to 13.2).
    The hand-off from `p8_blit` sits after its clip and reshaped its
    registers; called first and clipping for itself, as planes8ex.c
    is, it measured within 1-3% of that either way on both machines
    and was 620 bytes larger.
  - **Bytes:** `PIXEL_MAX` +532 (rowcopy.o 416, blit.o 116), `RESP_MAX`
    +1,152 (planes8row.o 1,096, planes8.o 56).
  - **The BLiTTER's size rules stand.** BLITCOST on an STE: the CPU's
    long aligned rows 21-41% faster (320x16 5045 to 3295 per 1500), and
    the BLiTTER still wins every long-row shape the sweep has (2915).
  - **Tested:** `test_long_rows` (test_pixel.c) and `test_rows8`
    (test_responsive.c) against models, every unit count, tail and two
    strips, views, masks, clips and the overlaps; nine mutations of the
    4-plane paths and thirteen of the 8-plane ones each failed them.
    On the machine PIXCHK (plain ST, full and sampled) and BLITCHK
    (STE, Mega STE, now 40 long-row copies a pass against the BLiTTER)
    pass, and RESPCHK passes on a TT and on a Falcon (where it then
    still failed only the `test_tiles` box check since fixed). Broken
    on purpose in the asm: the tails' jump a long early failed PIXCHK, and crashed SCRLCOST
    outright (it wrote past its rows); a long late, which writes nothing
    it should not, failed four of SCRLCOST's ring checks; the shift's
    mask a bit short failed RESPCHK and three of SCRLCOST's checks on a
    TT.
  - **A full-screen scroller on a plain ST is a 25Hz game, and now one
    with room in it:** the window copy alone is still a whole 50Hz
    frame (20.7ms), and with eight pre-shifted sprites (7.6) and a new
    column of tiles (4.9) a coarse-scrolling frame is about 33ms (an
    estimate from the parts), inside two frames, where it had been
    about 51, three. Smooth scrolling a pixel a frame with sixteen
    rings is 43.3ms before sprites (worst 45.8), three frames. On a TT
    and a Falcon smooth scrolling no longer needs rings for speed.

  Its checks compare every technique's page with a model of the map
  pixel by pixel, and built with `-DSC_BREAK` (two techniques a row
  short) exactly those nine checks fail.
- **Noted for later, not built: coarse vertical scrolling of a plain
  ST's video base.** Its base moves in 256-byte steps, and lcm(160,
  256) = 1280 bytes is 8 lines, so a page laid out for it can scroll
  vertically in 8-line steps with no copy at all, by letting
  `STDL_SetScrollWindow` accept a plain ST for that one case
  (`STDL_Flip` owns the base today, and the call refuses a plain ST).
  Revisit only if scrolling on a plain ST still needs it after the
  copies above: they made a full-screen coarse scroll fit two frames.
- **A blit onto its own memory goes the way its overlap needs**
  (November 2026; until then it was right only leftwards and upwards,
  and wrong through the BLiTTER and on 8 planes in the other two
  directions, by 29,234 to 57,044 pixels in SCRLCOST's checks).
  `STDL_BlitSurfaceEx`, `p8_blit` and `p8_blit_ex` ask
  `src/overlap.c` once, after the clip, and only where src and dst are
  one surface or one is borrowed (`STDL_PREALLOC`): a compare and a
  flag test in the blit, the rest out of line in an object of its own,
  built with `-Os` (1,224 bytes, 1,500 at `-O2`). Each kind comes in
  through its own small object with its own blit for the pieces,
  `stdl_blit_overlap` (`overlap4.c`, `STDL_BlitSurfaceEx`) and
  `stdl_blit_overlap8` (`overlap8.c`, the 8-plane blit): with
  `STDL_BlitSurfaceEx` called from overlap.c itself, v1.23.0 linked
  blit.o's 4-plane paths (13,788 bytes) into every program drawing on
  8 planes, and one port's TT and Falcon build grew 17K. It compares
  the rows' groups in memory, so two surfaces over one block are one
  surface's rectangles to it:
  - **Destination at the lower address** (left, up, or left within a
    group): the rows go as they always have. A keyed blit there goes to
    the CPU, as the BLiTTER's keyed passes (XOR, AND, XOR over the
    whole rectangle, the last reading what the first wrote) are wrong
    for any overlap. A destination row is taken a group wider than the
    source's, as its phase can reach one more; a one-row keyed move left
    by 17 needs that (a host check of it).
  - **Rows below:** from the bottom. On 4 planes at the same phase with
    no key and no destination mask - a page moved down - through the
    usual paths over headers (`blit_heads`) that start at the last row
    and whose 16-bit strides step back; `stdl_copy_rows_groups`,
    `blit_rows_long`, `blit_aligned_plain` and `blit_rows_edges` read
    them signed, the BLiTTER's line increments are signed anyway, and
    `stdl_copy_rows_checked` passes negative strides where no row
    overlaps its own source. On 8 planes whole groups go to that movem
    copy from the last row where its one pass holds them. Anything else
    goes in bands from the bottom, none taller than the rows between a
    source row's end and its destination.
  - **The same rows, to the right:** whole groups at the same phase with
    no key, flags or destination mask (every plane, on 4 planes) are a
    `memmove` a row, walked by pointer (indexed by a `mulu` a row it took
    47.5ms against 44.8). Anything else is copied whole groups at a time
    into a 1K buffer on the stack (with a group of guard either side, as
    a shifted blit reads beside its rows), as many rows as fit, and
    blitted from there; keyed, or wider than the buffer, a column at a
    time from the right, none wider than the move.

  What it costs (SCRLCOST, Hatari, EmuTOS; every check on all five
  machines passes, where the old library failed six on an STE and six
  on a Mega STE, the new right 1 and right 8 among them):
  - down 1 with its strip: plain ST 23.8ms as before (`blit_whole`'s
    `memmove`), STE 21.4 to 22.3 and Mega STE 11.3 to 12.0 through the
    BLiTTER bottom up (the test and a second call), TT 16.2 to 9.8 and
    Falcon 32.0 to 19.2 (the movem copy, where they had gone a row at a
    time, wrongly);
  - right 16 with its column (304 wide): plain ST 44.4 to 44.8, STE
    21.1 to 44.8 and Mega STE 11.4 to 34.3 (the BLiTTER's wrong pass
    to a `memmove` a row), TT 12.4 to 12.5, Falcon 24.6 to 24.9;
  - right 1 and right 8 in place (304x200 from x = 8): plain ST 161 and
    179ms, STE 77 and 75 with the BLiTTER, Mega STE 56 and 55, TT 45.6
    and 37.7, Falcon 91.3 and 75.4. The CPU's old results were wrong;
    the BLiTTER's were right, by luck, at 17.8ms on an STE: it reads
    each word before writing it when the move stays inside a group, and
    one pass there cost 192 bytes, so it was left out;
  - up 1, which only asks: STE 21.4 to 21.6.

  SPRCOST on a plain ST, all 90 cases (nothing in it overlaps): 0.9984
  against floors of 1.0014 and 1.0023; with `MINTICKS` 1200, 0.9986
  against 1.0002, five cases slower in every sample by 0.1-0.25% (keyed
  16x16 and 32x16 blits, a dirty restore) and seventeen faster (plain
  blits 0.4-0.9%, a clipped-out blit 6%). On a TT and a Falcon small
  shifted 8-plane blits moved inside the layout floor (13 tile blits
  18.6 to 19.0ms on a TT and 37.7 to 38.6 on a Falcon, where relinking
  the old objects alone had given 19.0 and 38.7). Changed objects:
  `blit.o`, `rowcopy.o`, `planes8.o`, `planes8ex.o`, and the new
  `overlap.o` (`objdump -dr`). Tried and dropped on the way:
  - strides signed on every 4-plane path: keyed blits 1.2-1.7% slower in
    SPRCOST, through `BlitSurfaceEx`'s registers, so masked and shifted
    moves down go in bands instead;
  - the 8-plane rows bottom up in `p8_blit`'s own loop: small shifted
    8-plane blits 10% slower on a TT and a Falcon (13 tile blits 18.6
    to 20.5ms, 37.7 to 41.7);
  - columns for everything off whole groups: right 1 took 3.6s on a
    plain ST and 567ms on an STE; the buffer for whole groups too:
    right 16 88.8ms against 44.8 on a plain ST, 79.6 against 44.8 on an
    STE.

  Tested on the host against a blit from a copy (`inplace_pass` in
  test_blitter.c: 14 shapes by 15 moves, with the BLiTTER refused,
  forced, and on two planes of four; `test_inplace8` in
  test_responsive.c: 9 shapes by 14 moves), and
  `stdl_copy_rows_checked` against a snapshot for strides of both
  signs. Broken on purpose, 41 mutations each failed them (27 on 4
  planes, 14 on 8); three more pass, as they change only the path
  taken: the buffer never used (columns instead), the 8-plane movem
  copy handed strides that do not step back (it declines, and bands
  follow), and `blit_aligned_plain`'s strides unsigned (it only hands
  them to `blit_rows_edges`, which reads them signed).
- **Refit a threshold against every shape it decides between.** The
  first BLiTTER refit measured only whole-group copies, handed
  partial-edge restores to the CPU, and made them 2-3x slower on an
  STE: the CPU merges edge groups a group at a time and the BLiTTER
  wins those from 26x8. `blitcost.c` sweeps five kinds for this.
- **Inline asm in a pixel path is paired with its C twin.** blit8.c
  is the pattern: the hand-written 68000 gather sits under
  `#ifdef __m68k__` with the identical loop in C as the `#else` -
  the C build is what tests/host exercises, and the same host tests
  run on the target gate the asm: `tests/hatari/pixchk.c` for
  test_pixel.c, `tests/hatari/i8chk.c` for test_blit8.c,
  `tests/hatari/ixchk.c` for test_indexed.c,
  `tests/hatari/pgchk.c` for test_polygon.c and
  `tests/hatari/ppchk.c` for test_polypat.c. A pixel
  file with asm and no such runner has its asm checked by nobody -
  blit8.c's was, until I8CHK, and a deliberately broken gather now
  fails it with 26 differences. (gcc 4.6 earned the asm: it
  compiled the C loop at ~210 cycles/pixel by spilling accumulators
  and calling __mulsi3 per group.) Remember the 68000's (d8,An,Xn)
  mode takes an 8-BIT displacement only.
- **Audio/music**: record Hatari output (`hatari-shortcut recsound`,
  path from the `szYMCaptureFileName` config key) and verify
  spectrally; PLAYMUS's DEMO.STM is note-exact by construction (its
  melody's held notes carry a vibrato of 1/8 semitone). The
  voice mixer (voice.c) is host-tested end to end through the fake
  TOS queue and DMA counter in tests/host/test_voice.c - steer
  `stdl_host_dma_pos` and call the queue slot like the interrupt
  would. Its inner loop is inline asm with a C twin
  (`voice_run_c`); tests/hatari/voicechk.c runs random mixes
  through both on target, compares them byte for byte and times
  them. Rerun it after touching either. Each of the mixer's
  shortcuts - the asm loops, the byte mix, the short-loop copies -
  has a switch in `stdl_voice_test` (`STDL_VT_*`,
  stdl_internal.h), on the host and the machine: test_voice.c
  compares each shortcut's output with it switched off, and VOICECHK
  times each against its switch. A new shortcut gets a switch and a
  comparison; break it on purpose once to see the comparison fail.
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

## The SDL2 add-on (`addons/sdl2/`)

An SDL2-shaped library for SDL2 programs on a stock TT or Falcon
(`docs/sdl2.md`): byte-per-pixel textures converted into the
responsive screen's eight planes. It is the one chunky part of STDL,
and the rules below keep it from being anything else's.

- **The core never meets it.**
  - `all`, `libstdl.a` and `libstdl-cmini.a` do not build, include or
    name it.
  - `addons/sdl2/sdl2.mk` gives it its own targets (`make sdl2`) and
    static pattern rules with its own flags. The core's
    `$(OBJDIR)/%.o` carries `-Iinclude/compat`, whose SDL 1.2 `SDL.h`
    would be found ahead of the add-on's.
  - A change to the add-on leaves every core object's `objdump -dr`
    as it was. Check by comparing before and after.
- **It reaches STDL through public headers only.** The host's
  `fake_stdl.c` can stand in for STDL because of that, so keep it so.
  If the add-on needs something internal, that is a core change in
  its own right, with the core's rules.
- **No `SDL_` link symbols.** Every public name is a macro onto a
  `stdl_sdl2_` link name, because `src/compat.c` (inside `libstdl.a`)
  defines `SDL_Delay`, `SDL_FillRect` and the joystick calls with SDL
  1.2's signatures. Extensions are `STDL_SDL2_*`, in `SDL_stdl.h`.
- **Built for the 68020 without FPU instructions.** A program links
  against the 68000 libraries, since the 68020 ones assume an FPU and
  a stock Falcon refuses them. What `SDL_Init` runs before the machine
  is known (`init`, `error`, `hints`: `SDL2_CPU000`) is built for the
  68000, so an ST gets an error and not an illegal instruction.
- **`make sdl2` checks each build.**
  - Neither archive has an `SDL_` symbol.
  - The headers compile as gnu99 and as C++98 under g++ 4.6 with
    warnings as errors, and fail when they meet the 1.2 shim.
  - The text stays within `SDL2_MAX`.
  - There is no libgcc arithmetic.
  - Each check was broken on purpose once and failed.
- **The conversion (`c2p*.c`, `c2p_asm.h`).**
  - Each loop, with the code round it that runs once a row (from
    `_cache0` to `_cache1`), must stay within 240 bytes, which fits the
    68030's 256-byte instruction cache whatever its alignment.
    `sdl2check` measures that span with `nm` and refuses more, as the
    assembler cannot measure across forward branches. So nothing
    else goes inside a row: a copy of the group before the loop, which
    the loops had until November 2026, made them 428 bytes, fetched
    again at every row; taking it out made whole frames 12-21% faster
    in Hatari and boxes two groups wide a third to a half.
  - Every box goes through the assembler on the machine, however
    narrow; the C twin is the host's, and the reference the machine
    checks the assembler against.
  - A fixed border index X has an object of its own (`c2px.c`, built
    15 times), which `STDL_SDL2_SetBorderIndex` names inline for a
    constant X. Reading X at run time costs about 3% of a frame.
  - `SDL2CHK` (`tests/hatari/sdl2chk.c`) checks every variant against
    the C twin on the machine and times them.
- **Input keeps SDL2's numbers.** Scancodes, key codes, `KMOD_` bits
  and event types are SDL2's own values, which programs keep in saved
  key maps; `test_events` checks a sample, and that every STDL symbol
  with a scancode has one key's. Joystick and controller state comes
  from STDL's public events and the readers, which take the joystick
  (`STDL_JoystickInputHeld`, `STDL_HasPad`, `STDL_PadRumble`,
  `STDL_NumJoysticks`), nothing deeper, for every joystick STDL has.
- **Audio keeps SDL2's contract on STDL's ring.** The ring asks for
  what a half of it needs; the program's callback gets exactly the
  obtained size, through a buffer, and is never called before the
  first unpause (the ring primes itself at open by calling its own).
  `test_audio`'s stand-in asks as `ring.c` does, so keep the two
  alike; `SDL2AUD` checks the sizes and the rate on the machine.
- **The compositor has a model, not a mirror, as its test.**
  `tests/test_render.c` draws a chunky model a pixel at a time and
  expects each page shown to be it, converted, byte for byte. It has
  none of the compositor's groups, batches or masks, which is why
  eight different breakages of those each failed it. `SDL2RCHK` is the
  same test on the machine.
- **What runs once a file is built -Os** (`SDL2_SIZE` in sdl2.mk, as
  the core's `SIZE_OBJS`). `SDL_LoadBMP` (`bmp.o`) came to 1,036 bytes
  against 1,280 at -O2, where gcc 4.6 also copied the error path out
  at every `goto` to it.
- **`stdlconv bmp8` maps through Pillow only what it cannot look up.**
  Pillow's `quantize(palette=...)` finds the nearest to 6 bits a
  channel, so a colour that is in the palette can land on a neighbour
  (101,100,100 went to the entry before it, 100,100,100). So the
  art's own colours are looked up exactly, and Pillow maps the rest:
  the nearest of 74,931 colours in Python took 14.8s, against 0.2.
  `test_bmp`'s `bmp8conv.py` checks each of bmp8's promises, and each
  was broken on purpose once and failed.
- **A change a porter would meet goes into `docs/sdl2.md` and the
  `stdl-sdl2` skill** (`.claude/skills/stdl-sdl2/SKILL.md`) in the same
  change, as the core's API changes go into the `stdl` skill.

## Shipping a change

- **A public API change is not done until the docs and the skill
  say so.** Adding, changing or deprecating anything in
  `include/stdl/` means updating, in the same change:
  `docs/format.md` if it touches the byte-level contract or the
  mask rules, `docs/porting.md` if a port would meet it (or
  `docs/overscan.md` for the borders and rasters,
  `docs/responsive.md` for a TT's or a Falcon's screen,
  `docs/building.md` for the build and link),
  `.claude/skills/stdl/SKILL.md` (the cost table, the API list, and
  any pattern worth teaching), and `README.md` if it belongs in the
  headline feature list. A header comment alone does not count - nobody
  porting a game reads the headers first. Check it the cheap way
  before committing: grep the new symbol across `docs/ README.md
  .claude/` and see it come back.
- **A change to a released call can go in a minor release when the
  build catches every old use** - a call with the old arguments that no
  longer compiles, or a name that is gone and no longer links - and is
  written up for programs moving up in the `stdl` skill's "Moving to a
  newer STDL" section, newest release first, with what a caller
  changes.
- **New functionality needs an example that exercises it.** The
  examples are how a port author sees an API working before
  committing to it, and they ship as .TOS binaries on the release
  page - so they are the API's real acceptance test on hardware.
  Extend an existing example when the feature belongs to a demo
  that already runs (a new blit flag joins the one drawing
  sprites); add a new one when it does not fit, remembering the
  Makefile rule and the `EXAMPLES` list, or it never gets built or
  shipped, and its row in `examples/README.md`, or nobody finds it. Keep them small and CC0, show the intended usage rather
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
  `stdl_ym_mix_update`). Every register write goes through
  `stdl_ym_write` or `stdl_ym_mix_update`, which mask interrupts
  across the select and the write: TOS's Timer C plays Dosound
  sequences (the bell, an accessory's sound) on the same chip and
  outranks the VBL, so a write split by it lands in the wrong
  register. Effects/music share voices via `stdl_ym_owned`
  ownership - see src/ym.c.
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
- **`STDL_PollEvent` pumps once a VBL** (`STDL_PumpEventsIfDue`,
  November 2026): when the VBL count has moved since the last pump, or
  there has been none, and otherwise it only takes from the queue,
  serving the timers and the audio refill at most once a 200Hz tick
  (time.c's `stdl_idle_services`, the delays' own). `STDL_PumpEvents`
  always pumps, and `STDL_WaitEvent` pumps every time it looks.
  - Before, every call pumped: a drain of k events was k + 1 pumps,
    each reading the ring and every pad again. PUMPCOST
    (`tests/hatari/pumpcost.c`: 250 frames, each 16 events of its own
    and two sticks moved from the script, drained as a game does), in
    Hatari cycle-exact: on an ST 17.3 pumps and 6.43ms a frame to 1
    and 2.76ms, with four stub pads apart 18.87ms to 2.98; on a Falcon
    2.52ms to 0.80, with the four pads 7.16 to 0.88. The SDL2 add-on
    pumped twice an event (each `SDL_PollEvent` pumped, and its drain
    of STDL's queue pumped again for each STDL event): SDL2PUMP
    (`tests/hatari/sdl2pump.c`, the four pads apart, 16 keys a frame)
    on a Falcon 33-35 pumps and 18.0-19.0ms a frame, now 1 and 3.5ms.
    The add-on pumps on the same beat: `SDL_PollEvent` takes from its
    queue unless `STDL_PumpEventsIfDue` pumped, and `SDL_PumpEvents`
    and the waits always pump.
  - Latency, a key from the ACIA's interrupt to its event (the vector
    at $118 wrapped by the probe; 19-20 keys, a stray stick packet
    left out): a loop that polls once a frame, after the VBL, unchanged
    (ST 10.8ms average against 10.8, Falcon 10.6 against 7.1 and 11.1
    against 10.3, inside the sampling); a loop that only polls, from
    0.2-1.3ms to 8.5-12.1 (the next VBL, at most 20ms at 50Hz and 17
    at 60). That is the cost, and only for a program that polls
    between frames.
  - Behaviour in time kept: key repeat runs on its own beat (the next
    one an interval after the last was due, from now again if more
    than an interval behind), as each repeat timed from a pump a VBL
    made the default 30ms 40 at 50Hz; the timers and the refill
    between pumps, so an SDL 1.2 game firing timers from a busy
    polling loop keeps their pace; rumble ends, the cursor and the
    room count (`untaken`, now pumps a VBL apart) were a pump a frame
    for a game draining once a frame already. A VBL that does not
    count (a program's own VBL handler leaving `_frclock` alone, or the
    VBL masked) gets a pump every five 200Hz ticks: just over a 50Hz
    frame, so at 50Hz the VBL comes first. A TT or a Falcon at 60 or
    71Hz pumps at its own rate. The terminate path never pumps.
  - Bytes: event.o 7,120 to 7,204 (the step 46, the pump's two clocks
    and the repeat's beat 28, the wait's own pump 8), time.o 324 to 280
    (the service out of line, which the delays had each inlined), the
    add-on's events.o 2,504 to 2,540; every program 37 bytes larger,
    SDL2DEMO 76.
  - Pads held back for room now come at the next frame's pump rather
    than later in the same drain: "eighty events, none lost" takes four
    frames, a pad a frame, and "pad 0 waits for room" two.
  - `tests/host/test_joystick.c` ("the pump's beat", "key repeat's own
    beat") moves the VBL apart from the 200Hz clock (`STDL_HOST_FRCLOCK`
    for that suite alone), and the add-on's `test_events` ("beat") has a
    VBL count of its own in its fakes. 13 breaks all fail them: a pump
    at every call, the VBL ignored, the 200Hz fallback gone or a tick
    early, nothing served between pumps, the explicit pump or the waits
    gated (the add-on's wait then never returns), repeats timed from
    the pump or sending the lost ones, the add-on pumping STDL at every
    call or never, or leaving an explicit pump to STDL's beat. A flag for
    the first pump went: `_hz_200` counts from boot, so the 200Hz test
    makes the first call due, and its break could not fail.
- **Joystick input has two sources and they merge, never override.**
  A stick on port 1 arrives as IKBD packets; a modern controller
  arrives as an Xpad block found through the cookie jar
  (`src/stdl_xpad.c`). `event.c` keeps `joy_ikbd` and `joy_xpad`
  apart and ORs them, so one releasing cannot clear what the other
  holds, and either alone works. Xpad matters here specifically
  because STDL replaces `ikbdsys`: TOS never dispatches `joyvec`
  while a game runs, so a provider that injects there cannot reach
  us, and one that publishes a block can.
- **Rumble is the one thing STDL writes to a provider**, and every
  pulse needs an end: a provider holds magnitudes until something
  replaces them. `STDL_PadRumble` records where it wrote; the pump
  zeroes it once the time has passed, and `stdl_events_remove()`
  zeroes it on every exit route, the terminate path included (memory
  writes only). A refusal stops what is running before it returns -1,
  because a provider drops `XPAD_CAP_RUMBLE` when its last
  rumble-capable pad leaves, and a pulse already written would
  otherwise never end. `tests/hatari/xpadprobe.c` checks each case
  against `XPADSTUB.PRG`, taking snapshots and printing them
  afterwards: a console print costs about 150ms under Hatari, longer
  than the pulse it would be timing.
- **More than one joystick is asked for, and the mouse port is a mouse
  or a joystick, never both** (`STDL_SetNumJoysticks`, November 2026).
  - 1 is the default and exactly what every program had: joystick 0 (the
    joystick port and pad 0) and the mouse. 2 sends the IKBD $12 and $14,
    and its $FE packets become joystick 1; 3 and 4 add Xpad pads 2 and 3
    where the provider has those slots (`pad_count`, which a published
    block may not change), so 4 with no provider is 2. $14 is there for
    the hardware: a mouse moved on a port silenced by $12 alone sent no
    $FE on a real Mega STE (the mouse entry above), and Hatari reports
    the port after $12 alone, so the emulator cannot say whether it is
    needed. On a real Mega STE (November 2026, IKBDMODE) the mouse-port
    stick worked after $14 alone and after $12 $14, and the mouse
    worked on the desktop afterwards; both bytes are kept, the $12 to
    be certain the mouse is off. A commercial two-player ST game
    that shipped and played on real machines, from a published
    disassembly, installs its own handler and sends $15 (interrogation
    mode) with no $12, then polls both ports once a VBL ($16, answered
    by $FD and both ports' bytes): so a joystick mode command is what
    turns the mouse port into a stick, and the $12 is probably not
    needed for the stick (it is kept: it costs one byte once, and
    turns the mouse off for certain). Polling was not taken: that game's $FD
    replies overran the ACIA's one byte while interrupts were off and
    lost keys, and the IKBD interrupt that delivered them delayed a
    raster split by about 300 cycles; $14 sends a packet only when a
    stick changes. `tests/hatari/ikbdmode.c` (IKBDMODE) is the probe
    for the hardware: a packet counter of its own on the IKBD vector
    and five modes on keys 1-5, each after $08 ($08 alone, $14 alone,
    $12 alone, $12 $14, $15 with $16 a frame), the counts on screen and
    in IKBDMODE.LOG. Hatari, cycle-exact on an ST with both ports from
    `joykeys.cfg`: the mouse-port stick sent nothing after $08 alone,
    and the same 4 $FE packets after $14 alone, $12 alone and both, so
    its answer is the emulator's model, not the chip's. A mouse left in the port should move
    joystick 1 as it moves, its quadrature lines being the stick's.
  - A joystick's sources are fixed: merged (the default), 0 is port 1
    and pad 0, 1 port 0 and pad 1, 2 and 3 pads 2 and 3, each port
    merged with its pad as joystick 0's always was. Joystick 0 so stays
    what it was when a program goes to two, and a player keeps their
    number when another's pad leaves. The design asked for "two
    joysticks, or the first two pads"; with the merge it is both at
    once.
  - Apart (`STDL_UseJoystickMerge(0)`, the Use* convention: 1, 0, -1 to
    ask, the previous returned), the pads follow the ports: 0 port 1, 1
    port 0, 2 to 5 pads 0 to 3, six joysticks at most where four are
    merged. The mouse rule does not change: two or more turn it off, an
    ST's mouse port being a mouse or a joystick. Joystick 0 stays
    event.c's; the setting is its pad record's slot (`stdl_pad0.slot`,
    -1 apart), and going apart lets go of what pad 0 held at once
    (`stdl_pad0_report`, the pump's own report of joystick 0), as
    joysticks 1-5 let go of theirs, so that the releases come before
    the device events; a record without a slot is then never read, and
    `STDL_HasPad(0)` and `STDL_PadRumble(0, ...)` say no pad. Merged
    again, pad 0 joins as at the start. (Until November 2026 the
    releases came at the next pump, and a merge set and unset before
    it had to keep the record to send them.) joysticks.c keeps each
    other joystick's record and re-maps at once on a switch: one going,
    or whose pad changes hands, releases what it held and stops its
    rumble; one coming starts its pad at rest; the count follows what
    was asked (6 is 4 merged and 6 apart). `STDL_UseJoystickMerge` is
    an object of its own (`joymerge.c`, 160 bytes), for a program with
    one joystick that wants the port alone.
  - Device events come from the core (November 2026):
    `STDL_JOYDEVICEREMOVED` and `STDL_JOYDEVICEADDED` (14 and 15, SDL
    1.2's reserved numbers; `e.jdevice.which`), pushed where the
    re-map happens, straight after the releases - joysticks.c for 1-5,
    each one going or whose pad slot changes, joymerge.c for joystick 0
    at every merge change. Before, the SDL2 add-on worked them out at
    its pump by comparing `STDL_NumJoysticks` and the merge setting
    with what it had said, which missed a change undone between two
    pumps (2 to 1 to 2, the merge set twice) although the core had
    re-mapped and sent releases, and held its REMOVED to the pump's end.
    Only for a program that asks, `STDL_UseJoystickDeviceEvents(1)`
    (`joydev.c`, 120 bytes), which also says ADDED for each joystick
    there is then, as SDL2 does at start-up: a program written before
    them would not know them, and the SDL 1.2 shim, whose
    `SDL_PollEvent` is `STDL_PollEvent` itself, could only drop them at
    the cost of a function of its own in front of every SDL 1.2
    program's queue; it never asks. The add-on asks at its first pump,
    and its `say()` and end-of-pump bookkeeping went (events.o 2,540 to
    2,160). They take queue room as any event does; a re-map pushes its
    releases and its device events at once, as it pushed the releases
    before. A controller that goes lets go of all it held before its
    REMOVED, and comes back at rest.
  - The axes and the hat are the core's readers,
    `STDL_GetJoystickAxis(which, axis)` (`STDL_JOYSTICK_AXIS_X`, `_Y`,
    `_RX`, `_RY`, `_LT`, `_RT`) and `STDL_GetJoystickHat(which)`
    (`STDL_HAT_*`), as the events carry them and 0 for a joystick, an
    axis or a pad that is not there (`joyread.c`, 244 bytes, its own
    object). The SDL 1.2 shim read `stdl_joyn` and the pad records for
    them, and the add-on kept copies built from the events: both read
    the core now (compat.o 2,180 to 1,976; the add-on's joystick.o
    drops its axes and hats, 2,048 to 2,240 with the device events'
    translation).
  - Checked by `test_joystick`'s "device events, asked for" (the
    sequences the add-on missed, the order against the releases, none
    unasked) and "axes and hats, read" (every axis against the last
    event that carried it), and the add-on's `test_events`, whose fakes
    push device events as the core does: 22 breaks all fail them, one
    only once "one joystick" read past the count with joysticks.c not
    linked.
  - Every pad is read by one piece of code (stdl_xpad.c), each
    joystick's pad a record (`stdl_pad_t`: the reading, what was last
    reported, its slot, its rumble): joystick 0's is `stdl_pad0`,
    joysticks.c keeps the others'. A reading the same as the last one
    reported (three long compares) is no news, and the pump skips the
    report and the events, joystick 0's included; a record without a
    slot reads at rest once and then not at all. Before, joysticks.c had
    a reading, diff and rumble of its own beside stdl_xpad.c's: one
    path took joysticks.o from 2,644 bytes to 1,256 and left
    stdl_xpad.o its 1,584.
  - Two more sources a program could want, neither read today: a
    parallel-port four-player adapter (its sockets 3 and 4 on D4-D7
    with BUSY and D0-D3 with STROBE, port A bit 5 of the PSG), and
    Jaguar pads on an STE's or a Falcon's enhanced ports ($FF9200 read
    before $FF9202).
  - The readers take the joystick first: `STDL_GetJoystickState(which)`,
    `STDL_JoystickInputHeld(which, input)`, `STDL_HasPad(which)` and
    `STDL_PadRumble(which, low, high, ms)`. The public names say
    Joystick, not Joy (the key emulation's calls and the inputs,
    `STDL_JOYSTICK_*`); the events and their structs keep SDL's
    (`STDL_JOYAXISMOTION`, `STDL_JoyAxisEvent`), which a porter looks
    for. The inputs' count is `STDL_JOYSTICK_INPUTS`, so that it cannot
    be read as a number of joysticks.
  - Only joystick 0 makes keys (`STDL_JoystickKeyEmulation`, the bindings).
  - Every way back gives the mouse back. 1 again sends $08 (and $12 if
    `STDL_EnableMouse(0)` was asked, which only records while there are
    two), and `stdl_events_remove` sends $08 on every exit.
  - The mode is one function, `stdl_ikbd_mode` (event.c), which the
    install, `STDL_EnableMouse` and the re-map each call: two joysticks
    or more, $12 $14; one, $08, and $12 after it with the mouse off.
    That changed three sequences (November 2026), each a repeat the
    IKBD takes at any time: the install sends $13 $11 $08 $16 where it
    took the mouse's mode as TOS left it ($13 $11 $16),
    `STDL_EnableMouse(0)` $08 $12 where it sent $12, and with two
    joysticks `STDL_EnableMouse` re-sends $12 $14 where it sent
    nothing. `STDL_EnableMouse` tells the IKBD only while the library
    has the keyboard, where it had asked `stdl.initialised`: a $12
    sent between STDL_Init and the mode set outlived a program that
    left before taking the keyboard, the only place the mouse is given
    back; the install applies what was asked.
    `tests/hatari/mousebk.c` runs JOYCHK at two joysticks, lets it leave
    by STDL_Quit, exit and abort(), then counts TOS's own mouse and
    joystick packets for a click and the mouse port's stick: 4 mouse and
    0 joystick after each, against 0 and 6 with $12 $14 sent by hand,
    its control. An address error in supervisor mode stops at EmuTOS's
    panic, whose "press any key" never gets one with STDL's handler on
    `ikbdsys`, with one joystick as with two; that route was not seen to
    its end.
  - The right mouse button carries joystick 0's fire while the mouse
    reports, and only then: the fold asks for the mouse on and one
    joystick, and `joy1_fire` (static in event.c) is cleared wherever
    the mouse stops reporting (`stdl_ikbd_mode`). With it off, port 1's
    fire comes in its own packet, and a mouse packet still in flight
    must not move it. Before, only the count was asked and the clearing
    was the re-map's: fire held through `STDL_EnableMouse(0)` stayed
    held for good after its release (`test_joystick`'s "fire, then the
    mouse off" failed before and passes now).
  - Cost in a program that never asks, 424 bytes in all when the
    joysticks came (544 now, below). event.o 332:
    224 for the mouse port's ring item, the pump's two hooks and the
    exit's, the install's $12 and $14 and the count, 84 for the index
    on the three readers there - a test and a branch each, into one
    shared out-of-line call (`joyn_read`) through `stdl_joyn`'s single
    `read` entry - and 24 for the merge setting's byte and
    `STDL_HasPad(0)`'s test of it. stdl_xpad.o 92: 76 for rumble's
    index, a tail call to the hook, and 16 for pad 0 read at rest and
    its rumble refused while apart. The packet handler inlines `ring_push` at each call, and
    a push apiece for $FD and $FE made it 140 bytes longer where one
    shared push makes it 90. compat.o is 592 bytes larger in an SDL 1.2
    port, reading joysticks 1-5's axes and pad slots from `stdl_joyn`.
    The one pad path and the queue's room (below), November 2026: 160
    more in event.o - the pump 52 (the room and its count, pad 0 read
    through its record, and 68 less for the fold's bit shifted into
    place rather than chosen by a branch gcc copied), `stdl_ikbd_mode`
    102 where the install is 68 less, `stdl_joy_event` 84 (one event
    push for every joystick event of both objects), the room 22 -
    stdl_xpad.o the same, mouse.o 40 and compat.o 120 less (472 in
    an SDL 1.2 port). So 544 in a program that never asks (630 since,
    with the pump once a VBL's 37 and the device events' and readers'
    49, in their entries): CHECKKEY, STARTER and SFXDEMO are 120
    bytes larger, TSPRITE and TCURSOR through the SDL 1.2 shim 2
    smaller, TJOY 1,363 and JOYBOXES 1,277 smaller. The device events
    and the readers added 49 to that (event.o 7,204 to 7,260: the push,
    40, and joystick 0's report out of the pump, 18; stdl_xpad.o 8
    less): CHECKKEY, STARTER and SFXDEMO 49 larger, TSPRITE 92 (the
    readers, 244, for the shim's 204), JOYBOXES 105 and TJOY 190,
    SDL2DEMO 98.
  - Where the readers live was measured. In a separate object of their
    own (`joystate.o`, 372 bytes) they cost a program reading joystick
    0 that, and every SDL2 program, whose add-on names them for any
    joystick; in event.o and stdl_xpad.o they cost every program 160,
    and the separate object went. CHECKKEY, TSPRITE and STARTER are 164
    bytes larger for it, SDL2DEMO 220 smaller. Inline header forms that
    a constant 0 folds to joystick 0's own call would have cost a
    joystick-0 program nothing, at the price of four public calls that
    are no longer functions; not done.
  - `joysticks.o` (1,256 bytes, 256 of BSS for six) is linked only by a
    program that asks (link maps: of TJOY, CHECKKEY, TSPRITE, STARTER
    and SDL2DEMO, only TJOY, which asks). The add-on grew 536 bytes for
    four joysticks, then 104 (events.o) for the re-map pairs, then 32
    when its buttons became STDL's own (`STDL_JoystickInputHeld`) and
    its handles BSS (192 bytes of data gone; `sdl2.mk`), and lost 188
    when the device events and the readers moved into the core. Every
    SDL2 program links `joydev.o` and `joyread.o` (364), and no longer
    `joymerge.o`.
  - No pad event is lost to STDL's 32-event queue. A pad is read only
    with room for the most it can say, 20 (six axes, thirteen buttons,
    the hat), and joystick 0's with key emulation on only with the
    queue empty, as its keys come too (up to 17 more, which pass 32
    only if every input is bound and all move in one pump); otherwise
    it is read at a later pump, late rather than lost. Before, four
    pads changing everything in one pump passed it (`test_joystick`'s
    comparison of pad 0 with pad 2 lost two of 34, and pumped a pad at
    a time; now one pump), and "eighty events, none lost" and "pad 0
    waits for room" would have lost 48 and 8. A program that only pumps
    and polls never empties the queue, and its pads would wait for
    good: the room counts only while events are being taken
    (`stdl_event_room`; `untaken` counts pumps that found events
    waiting with none taken since, and from 8 a full queue drops events
    as it always did; "pumped, never taken").
  - Checked: `tests/host/test_joystick.c` runs the real packet parser on
    a fake ACIA, Xpad's own consumer on a fake provider, and the SDL 1.2
    shim, apart as well as merged (six at once, switching both ways with
    releases, joystick 0 apart with one joystick); 34 deliberate breaks
    all failed it, four only once the test was made to see them (an
    in-flight mouse packet that changes a button, three slots, a pad
    held from the start, every pad's rumble at exit), and a reader past
    the count by ASan. The add-on's `test_events` covers the device
    events, the re-map pairs and each joystick's state (12 breaks of
    12, one only once the test set the merge before the first pump). In Hatari,
    `tests/hatari/joychk.c` with `tests/hatari/joykeys.cfg`, which
    drives both ports from fifo letters, and `--trace ikbd_cmds`: both
    ports and the switch back on an ST, a TT and a Falcon, and four pads
    from `XPADSTUB.PRG` built with `-DXPADSTUB_PADS=4` on an STE, each
    one's state, its releases when dropped, and its rumble written and
    ended. Apart, on an ST with the four stub pads: six joysticks, the
    shim naming 2 to 5 "Xpad pad 0" to "3", each pad's rumble in its own
    slot and ended, joystick 0 the port alone, the switch both ways
    releasing what changed hands, and the mouse back at one joystick and
    at exit. JOYCHK prints a console line for every event, about 150ms
    each under Hatari, so a script's keys can run seconds ahead of it: a
    click typed after "1" arrived while it still had six, and an Esc
    behind it ended it before it printed the releases. Give it time
    between keys.
  - The one pad path (November 2026) was checked the same way. 37
    breaks of the core's joystick code all fail `test_joystick`, 6 only
    once the test was made to see them: pad 0 read with no room, and
    with key emulation's room as a pad's ("pad 0 waits for room"); the
    fold with the mouse off (an in-flight right button after
    `STDL_EnableMouse(0)`); `STDL_EnableMouse` before the keyboard is
    the library's (STDL_Init first); a pad changing hands news at once;
    and a joystick event's state byte (`has_button` now checks it). An
    unchanged pad read as news shows only in the reading, which "a pad
    as it was, not reported" calls itself. Two more passed and went: the
    merge resetting pad 0's record as it rejoined, which lost a release
    when it rejoined before a pump ("apart and back between pumps"),
    and the count reset when the pump finds the queue empty, which
    could not fail, as only a take empties the queue and a take resets
    the count.
    The add-on's 14 breaks all fail `test_events`; one found
    `SDL_GameControllerRumble` rumbling joystick 0 for a controller
    opened without its joystick (its index unset), now set where a
    handle is first asked after (`there`). In Hatari, on an ST with
    EmuTOS (cycle-exact, no window), JOYCHK on both ports and with the
    four stub pads apart, six, rumbled and merged again, TJOY with the
    four pads, JOYBOXES on both ports and xpadprobe with one pad each
    printed exactly what the same programs built from the release
    before printed; the IKBD's commands differed only by the install's
    new $08, and gave the mouse back ($08) at one joystick and at
    exit.
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
  handler), and while rasters are open, raster.c's Timer B handlers
  and its own Timer C prefix of the same kind. The two never run
  together: `stdl_timerb_owner`. Services (audio refill, compat
  timers, cursor) run from the pump and the native delays.
- **overscan.c builds two ways.** In the library, and inside
  `src/overscan_host.c`, which includes it and defines what it uses
  of the rest of STDL, for a program that runs the machine itself and
  links neither the library nor a C library (`stdl_overscan.h`, "A
  program that runs the machine itself"). A call overscan.c gains into
  the rest of the library fails the build's `ovhostcheck`: define it
  there as such a program needs it, which is usually doing nothing,
  since the page, the colours and the flips are the program's. The
  two macros at overscan.c's top (`OVSC_CLEAR`, `OVSC_SPEED_KEY`) are
  where the builds differ; the library's object is byte-identical to
  what it was before them, and should stay so. `examples/ovhost.c` is
  the on-target check: it prints the misses over 300 flipped frames,
  0 on an ST, an STE and a Mega STE in Hatari, about 400 with either
  page call left out.

## 68000 / gcc 4.6.4 performance notes

- 32-bit multiply/divide are library calls; keep them out of inner
  loops (strided pointers, `&15` and `>>4` instead of `%`/`/`, and
  for resampling an index and a fraction - an error accumulator's
  `while` can compile to a multiply, as the next note says).
- **A loop that never says "multiply" can still call __mulsi3.** The
  ring device's resampler stepped its source with
  `while (acc >= frames) { acc -= frames; f += frame_bytes; }`, and
  gcc 4.6 folded that into a closed form - how many steps, times the
  frame size - with a `__mulsi3` call on every output frame: 37% of
  an 8MHz STE with a callback that did nothing, 73% at 12517Hz, for
  years before anyone measured the silent case. Step a whole index
  and a fraction instead, with the frame size as a shift
  (`stdl_audio_convert`), and check the disassembly of any new
  per-sample loop for `jsr` - `m68k-atari-mint-objdump -dr obj/src/x.o`
  prints the relocations by name.
- **The build lists every libgcc arithmetic call, and fails on a new
  one.** This trap kept coming back, found by profiling one site at a
  time: the resampler above, the BLiTTER size rule's 8% of a tile
  blit, the music tick scaling a volume with `uint8_t * uint8_t`.
  The rules are now measured, not folklore. gcc 4.6 emits
  `muls.w`/`mulu.w` only when both operands are visibly 16-bit and
  of the same signedness at the point it selects instructions, so
  any of these is a call:
  - mixed widths (`u8*u8`, `s16*int`, `s16*u16`);
  - a value derived from a loop counter, which the induction-variable
    pass (ivopts) rewrites as a fresh 32-bit variable, casts and
    `int16_t` declarations notwithstanding. A port's bisect found
    that `-fno-ivopts`, or the function attribute
    `optimize("no-ivopts")`, restores `muls.w`, but globally it
    raised the library's own count;
  - anything at `-O1`, where the widening pass does not run;
  - a divide or `%` by a non-power-of-two, constants included;
  - a difference of pointers to a struct whose size is not a power
    of two.

  So the cross build runs `libcallcheck`: `tools/libcalls.sh` reads
  every library object's relocations with objdump, counts the calls
  per function, and fails on any that `tests/libcalls.allow` does not
  name. Each line there says why that call is not hot. A new one is
  a decision: write the line with its reason, or use
  `include/stdl/stdl_math.h` (`STDL_MulS16` and friends, the
  instruction in inline asm with C twins, checked on target by
  `tests/hatari/mathchk.c`). The internal `stdl_row_off`, `stdl_mul16`
  and `stdl_divu` predate the public header and stay as they are:
  their `"d"` constraints are what the pixel paths were measured
  with.
- Variable shifts cost 8+2n cycles each, so two of them per word is
  a fixed tax whatever the shift amount. Building the value from a
  32-bit window needs one: `(((uint32_t)hi << 16) | lo) >> n` rather
  than `(hi << r) | (lo >> n)`, because the `<< 16` half compiles to
  a free `swap`. Worth 1ms/frame in the unaligned blit path.
- **In `STDL_BlitSurfaceEx`, take code out rather than add a call.**
  The function is large enough that gcc's register allocation for
  the whole of it - including the clip every blit pays - moves with
  any change. Routing unaligned restores to a new out-of-line routine
  cost every blit's fixed path 1.2-1.9% with the call added beside
  the inlined loops, and 3-5% with the test at the head of the
  same-phase block; moving the flag-free inlined loops out into the
  new function, so the big one lost code instead of gaining a call,
  left it within 0.3%. Measure the entry ("blit clipped out") and a
  16x1 blit on `st` for any edit there.
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
- **A frame time can move because a library grew, on a machine with
  no cache at all.** A port measured a reproducible 16ms step across
  one commit here, on a plain STE, and bisected it cleanly over
  several rebuilds. Every change in that commit was then exonerated
  one at a time, and the counters added to settle it read zero: the
  function had never been called during gameplay. Adding the
  counters also made the step vanish. What was left is that the
  commit changed the size of `blit.o` and therefore the address of
  everything linked after it, and the port's frame sits on a
  vertical-blank boundary where a few hundred bytes decide which
  side it lands on. The Mega STE note below is the cached version of
  the same hazard; this one has no cache to blame.
  There is a cheap control for this, and it wants using before any
  cross-build performance claim about this library: relink the same
  object files in a different order. The code is identical and only
  the addresses move, so whatever the frame time does is the noise
  floor of the comparison you were about to make. A port ran it on
  a plain STE and got per-scene frame times moving -3.29% to +1.86%
  with a third of scenes past 1.2%, which was the size of the win
  it was reporting - but the aggregate over all 39 matched scenes
  moved only -0.29%, because layout noise very nearly cancels
  across scenes. Hence: aggregate over every matched scene, never
  quote a per-scene delta, and state the floor beside the result.
  Two things follow. A frame-time regression attributed to a library
  bump needs "did this code run at all" answered first, and an
  opt-in counter (`-DSTDL_BLIT_STATS` in blit.c) answers it in one
  run where a throughput sweep never will. And a port that sits on a
  frame boundary has no stable ground to measure on, which is an
  argument for giving it a way to ask how much beam time is left
  rather than discovering the answer by missing.
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
- A program that runs to its end by itself - an example, a probe in
  `tests/hatari/`, a host test built for the machine - calls
  `hold_on_exit()` (`examples/hold.h`) first thing in `main`, so that
  on a real machine it says "Press a key." and waits rather than
  dropping back to the desktop with its results: `NULL` in an example,
  its name in a probe, which then also writes everything it printed
  to NAME.LOG beside it (a probe that prints while it has a screen of
  its own loses that text when STDL gives the desktop back: TRIPCHK's
  did, in Hatari). Started with a command line it does neither, and
  run.sh needs nothing: its output reaches the console as it is
  printed, and the script ends the emulator. Not for a program that
  waits for Escape already, nor VBLCHK, whose test is an abnormal exit.
- Commits: no Co-Authored-By trailers.
