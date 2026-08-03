# What STDL will not do, and why

STDL is incomplete on purpose. Anything that cannot be done cheaply
in planar is absent rather than emulated, so porters know when to
stop trying and redesign instead.

## Non-goals

| Omitted | Reason |
|---|---|
| Arbitrary bpp surfaces | v1 is 4bpp low res only; 1bpp and 2bpp deferred |
| Per-pixel alpha / blending | no sane planar implementation; `SDL_SetAlpha` returns -1 |
| Runtime scaling / rotation | pre-render or redesign |
| `SDL_LockSurface` semantics | surfaces are always directly addressable |
| Threads, timers-as-threads | cooperative single-thread model; timer callbacks fire inside `SDL_Delay` |
| Arbitrary palette depth | ST 512-colour / STE 4096-colour only |
| TrueType / SDL_ttf | bitmap fonts converted offline |
| Gamma / gamma ramps | `SDL_SetGamma*` return -1 |
| Chunky pixel access | there is no 8bpp buffer; see docs/porting.md |
| YUV overlays, OpenGL, CD-ROM | see section 8.2 of the design doc |

## Practical limits of v1

* `STDL_SetVideoMode` always yields the 320x200x4 screen; requested
  width/height/bpp are accepted for source compatibility and
  ignored. On a monochrome monitor it fails (mono emulation is a
  post-v1 feature, see the design overview section 3.4).
* Colour-key masks are snapshots: re-key after modifying pixels.
* `SDL_SetPalette(SDL_PHYSPAL)` clamps to the 16 hardware entries.
* The whole program runs in supervisor mode between `STDL_Init` and
  `STDL_Quit`/exit; keep that in mind if you spawn GEMDOS
  subprocesses (don't).
* While STDL owns the IKBD, BIOS console *input* is dead
  (`Cconin`, `Bconin`); console output still works.
* Timer callbacks only run inside `SDL_Delay` - a busy main loop
  that never delays never fires timers. Use `STDL_GetTicks`
  directly for frame pacing.
* Audio is STE/Mega STE DMA only and cooperative: the ring refill
  (and the user callback) runs inside `STDL_PumpEvents` /
  `SDL_Delay`. Keep the pump running at least every ~150ms while
  audio plays. Plain ST: `STDL_OpenAudio` fails cleanly - ship
  silent, as the design intends. Playback rates are the four DMA
  rates; other rates are nearest-neighbour resampled (convert
  offline with `stdlconv wav` for a bit-exact path).
  The ring device is not free: its callback, mixing and resampling
  are CPU work in the pump, measured at 36-75% of an 8MHz STE for
  four SDL_mixer channels at 6258Hz. A game with no frame budget
  to spare wants `STDL_PlaySample` instead, which hands the DMA a
  buffer to read once and costs nothing per frame - at the price of
  being monophonic. Only one of the two may own the chip at a time;
  whichever is second fails cleanly.
* Music is the one interrupt-driven part of STDL: the YM replay
  runs from a VBL queue slot (register updates cannot tolerate pump
  jitter). It is three square-wave voices plus noise - MIDI
  converted with `stdlconv midi` keeps the three most recent notes
  (last-note priority) and maps percussion to noise bursts; chords
  beyond three voices are dropped. Streams replay at 50Hz and
  assume a 50Hz display (colour low resolution). There is no MOD /
  OGG / MP3 path: sample-based music has no YM equivalent - author
  natively (SNDH-style) or re-score via MIDI.
* Effects share the three YM voices with music: a playing effect
  owns its voice (music skips it, then gets it back with its state
  restored). The speaker is always voice A; `STDL_PlaySfx` prefers
  voice C. More than three simultaneous effects steal voices.
* The surface origin applies to blits and rect fills only; draw
  primitives (lines, circles, PutPixel) ignore it.
* All draw primitives maintain surface masks (fills, spans,
  `STDL_PutPixel`, and the line/circle shapes built on them), and
  all accept `STDL_TRANSPARENT`.
* The XOR primitives (`STDL_XorRect` and friends) are the one
  exception to `STDL_TRANSPARENT`: only bits 0-3 of the colour are
  used, colour 0 is a no-op, and the pixels they touch are marked
  opaque in a masked surface. They are always CPU paths - including
  the batched `STDL_XorVSpans` / `STDL_XorHSpans`. The BLiTTER is
  deliberately not used for them: the spans worth XORing are one or
  two words wide, an order of magnitude below the measured
  fill/blit break-even, so the register setup alone would cost more
  than the whole span, and staying on the CPU keeps BLITCHK's
  "both paths byte-identical" invariant covering every accelerated
  path.
* The batched span calls (`STDL_HSpans`, `STDL_VSpans`,
  `STDL_XorHSpans`, `STDL_XorVSpans`) take spans in `int16_t`
  coordinates, skip entries with `len <= 0` (a negative length is
  not a reversed span), and read the list only. They are a
  call-overhead optimisation, not a different renderer: for a
  handful of long spans they are worth nothing.
* `STDL_Points` (and `STDL_PointsC`, the colour-per-point form) is
  the same idea one step further down: a span list still charges a
  one-pixel entry for a length clamp, two edge masks and a straddle
  test, which measured about a third of the cost of a particle on a
  plain ST. Use them for particle fields and starfields; they are
  defined to equal `STDL_PutPixel` per entry, so they are a drop-in
  for that loop and nothing more. They do not make per-pixel
  rendering viable - measured on a plain ST, 250 points cost 11ms to
  draw one colour, 13.5ms with a colour each and 9ms to erase, so a
  few hundred particles is still most of an 8MHz frame. The
  unmasked, unclipped, long-aligned case has a dedicated loop that
  merges two bitplanes per long; a masked destination or a clip
  origin away from (0,0) falls back to the general path, which is
  roughly twice as slow per point.
* The software cursor's save-under is a snapshot: hide the cursor
  before drawing beneath it, and prefer sprites for pointers in
  games that redraw every frame. Cursors are at most 32x32 with
  width a multiple of 8; "inverted" cursor pixels render black.
* SDL joystick support is the physical joystick port only: one
  stick, two digital axes, one button. No hats, balls, or analog
  values.
* `STDL_DrawText` fonts are limited to 16-pixel-wide cells.
* Tiles blit at group-aligned x only (`x & ~15`); use sprites for
  free positioning.
* `STDL_SetPlaneBudget(N)` trades colours for speed: promise never
  to draw an index `>= 2^N` and every primitive (including the
  BLiTTER passes) stops maintaining the higher planes, moving `N/4`
  of the memory. It is a whole-program mode, not a per-surface or
  per-region one - set it once, next to `STDL_SetVideoMode`. It is
  global on purpose: a per-surface budget would put an extra load
  in every inner loop and cost more than it saved. Lowering it
  zeroes the high planes of the screen pages; surfaces the program
  already filled with out-of-budget colours are *not* rescanned and
  will render wrong until re-created. Colour indices above the
  budget are truncated, not rejected.
* Large same-phase fills and blits are BLiTTER-accelerated when
  the hardware has one (Mega ST/STE/Mega STE, detected at init);
  correctness never depends on it, `STDL_UseBlitter(0)` forces the
  CPU paths, and BLITCHK.TOS proves both paths byte-identical on
  target. Unaligned blits stay on the CPU shift chain - pre-shifted
  sprites are the designed answer for free positioning.
