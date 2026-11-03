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
  Its default build samples three cases of each test, about 20s a
  pass, for real hardware; `-DPIXCHK_FULL` runs every case, 16
  minutes a pass on a plain ST, for the emulator.
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
  `asset8.o`, `tall4.o`: a program asking for the colour links 25.2K of
  them) outgrow
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
  whatever the NVRAM says. And Hatari writes its NVRAM back on every
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
  minutes in Hatari, and longer than `run.sh`'s two-minute `waitfor`:
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
- **Hatari ignores `run.sh`'s SIGTERM on this host** (seen with
  `--machine tt` and `falcon`): `run.sh` then waits out its caller's
  `timeout`, and the emulator outlives it - 61 of them had piled up
  over one session. Wait for the program's own "done" line and end
  that emulator with `kill -9`, matched by its `--cmd-fifo` path.
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
- **After touching blit/fill paths**: run `dist/BLITCHK.TOS` - it
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
  set-up, not register pressure from the loops. Measure every change
  to that function on `st`, all 85 SPRCOST cases, not the one you
  meant to change.
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
- Commits: no Co-Authored-By trailers.
