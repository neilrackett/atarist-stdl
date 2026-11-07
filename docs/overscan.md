# Borders and rasters on an ST

An ST shows 200 lines of picture in a frame of 313. Opening the
borders shows more of them, and rasters change colours down the
picture. Both run on the MFP's Timer B, so they never run together:
each refuses to open while the other holds it. Both are put back on any
exit, abnormal ones included, which a port's own Timer B handler would
not be. A TT and a Falcon have no GLUE borders and refuse both; their
240 lines come from the responsive screen
([responsive.md](responsive.md)).

`examples/overscan.c` is the pattern for the borders, including the
toggle for A/B comparison, and `examples/rasters.c` for rasters.

## A world taller than 200 lines

A game designed for 224 or 240 lines has three ways onto the ST's
200: drop lines (artefacts), crop (hides play area), or open the
top border with `STDL_OpenTopBorder()` - 228 native lines on any
50Hz machine for one timer interrupt per frame; a 60Hz base screen
is switched to 50Hz while a border is open and restored on close.
It fails cleanly (returns 0) where the hardware has no GLUE borders
(TT/Falcon), or while rasters are open, so keep one of the other two
mappings as the fallback and choose at init. Or ask for the tall
screen ([responsive.md](responsive.md)): `STDL_RESPONSIVE_TALL` is
320x240 on every machine, an ST's bottom border opened for 240 of its
245 lines and a TT's and a Falcon's own 240, and
`STDL_RESPONSIVE_HEIGHT` the 240 only where the display has them.

## The borders

`STDL_OpenBottomBorder()` gains 45 lines at the other end instead,
seamlessly: content goes at rows 200..244 with nothing to align
around. Opening both borders combines them automatically (273 rows,
edge to edge); closing one falls back to the other alone.

Each border costs an interrupt and two to three lines of polling per
frame (under 1% of an 8MHz frame), and the bottom, the first time it
opens, one to two frames with interrupts masked to calibrate its delay
loops against the machine's own scanlines and its own execution of
the flick's instructions - the 200Hz tick loses a few counts, once -
and then about a third of a second of the bottom border flickering
while it finds where the GLUE's border test sits against the video
counter (it moves from boot to boot, so it is searched for rather than
assumed). The flick is a short 60Hz pulse across the GLUE's border
test, restored inside line 262: on real hardware a restore that lands
in line 263 leaves the Shifter's plane phase a word out for the rest
of the frame (every colour wrong, every shape in place), which no
emulator shows.

While any border is open TOS's 200Hz Timer C vector carries a prefix
that lowers the CPU mask inside the handler so the border timers can
interrupt it; it runs for over two scanlines at a time, and without
that the ISRs would have to fire lines early and spin. Palette writes
land in the blanking via a VBL callback.

### The BLiTTER with a border open

While any border is open, every BLiTTER operation STDL starts is
placed from where the beam is - the video counter says during the
picture, a Timer B stopwatch through the blanking - so that it ends a
few lines before the next timing window; one that would not is split
around the window, the part before it runs, the policy waits the few
lines the window lasts (the ISR runs meanwhile) and the rest follows.
Blits stay in hog mode, so the CPU never waits on bus slices and an
ISR is never held off by a blit. Measured on an emulated STE with
back-to-back full-screen fills: 14% below the no-border throughput
with the bottom border open, 18% with the top, 28% with both (it was
2.1x slower in shared mode); 64x32 blits pay 35-47% for the
per-operation decision.

A program driving the BLiTTER on its own must use non-hog mode or keep
its hog blits short and clear of frame lines 30-36, 259-265 and 310-1.

### Misses and page updates

The border is lost in about one frame in 1600 beyond the frame in
which it opens; `STDL_OverscanMisses()` says whether anything else
stalls the CPU. Read it before closing: every open restarts the count,
and so does closing one border of two.

Full-page screen updates should start at the VBL (`STDL_WaitVBL`) so
the copy stays ahead of the beam - the display starts 29 lines earlier
with the top border open, and an unsynced full-page copy tears
intermittently.

## What disturbs an open border

None of these stops a port doing any of it - they are measured costs,
so you can decide what the border is worth in your frame. All are
invisible in Hatari, which is the awkward part: a port can only find
them on hardware.

* *Disk access.* One file opened, read and closed per frame, on a
  real Mega STE: the bottom border appeared in 5 frames of 300
  from a hard drive and 3 of 300 from a floppy. Both media, so
  changing storage is not a way out - any synchronous transfer
  holds interrupts off long enough. Loading assets before opening
  the border, or closing it around a read, avoids it; streaming
  from disk during play with a border open does not work.
* *A mouse nobody reads.* Every movement is a three-byte packet, one
  ACIA interrupt per byte about 1.3ms apart, parsed whether or not
  anybody asked. The ACIA is MFP channel 6, below the border timers
  at 13 and 8, so it cannot preempt them - but an interrupt already
  in service delays them, and a flick has to land on an exact cycle.
  Measured on a Mega STE with the bottom border open: moving the
  mouse continuously flickers the border and the missed-window count
  climbs. `STDL_EnableMouse(0)` stops the IKBD reporting it, and the
  same movement produces neither; the traffic does not reappear as
  joystick 0 packets. Joystick traffic is unaffected, the library
  puts reporting back when it releases the keyboard including down
  the terminate path, and `STDL_GetMouseState` simply stops changing
  rather than failing. `examples/overscan.c` has it on M.
* *Drawing on a 16MHz Mega STE with its cache on - fixed in
  v1.15.1.* Before that release, drawing every frame lost the
  bottom border on that machine, worst through the BLiTTER (a
  demo's overscan part lost it in 36% of frames), and the advice
  here was `STDL_UseBlitter(0)`. The cause was the Mega STE's
  cache: every BLiTTER operation empties it and the CPU's own
  memory traffic replaces it word by word, and the border's
  interrupt ran its timing-critical path partly cold. It now
  warms all of that path itself, and the same demo loses 1 frame
  in 1500. No workaround is needed; keep the BLiTTER.
* *Drawing volume, unexplained at 8MHz.* One port sees its bottom
  border flicker in proportion to how much it draws - solid with
  the top border alone, flickering in play, worst in cutscenes,
  steady while an inventory screen redraws 45 icons a frame. At
  16MHz with the cache on, that was at least partly the cache
  problem above. The same port reported it at 8MHz and with
  `STDL_UseBlitter(0)` too, where the cache cannot be the reason,
  and that part is not understood. A small fixed drawing region
  appears to be safe where a large varying one is not.
* *Resident firmware.* A cartridge's network driver flickered the
  border a few times a second on this machine and stopped
  entirely when unloaded, its resident code holding interrupts
  off long enough to make the timer late. Worth ruling out before
  blaming your own frame.
* *A speed change.* Change a Mega STE's speed
  (`STDL_UseMegaSteSpeedup`) with any border closed: the bottom
  border's timing is measured at open time and re-measured on the
  next open after a speed change, and a border left open across the
  change goes on running against the wrong clock's numbers.

## Rasters

Rasters - one or two colour registers changed every few lines down
the picture: copper bars, skies, a gradient behind text - are
`STDL_OpenRasters(lines, reg1, reg2)`: fill `STDL_RasterTable()` with
hardware words a frame ahead, `STDL_ShowRasters(keep)` with each flip,
`STDL_CloseRasters()` when done. They carry the same Timer C prefix as
the borders, which keeps them steady while TOS's 200Hz tick runs.

## A program that runs the machine itself

A program that runs the machine itself - its own interrupts, its own
page flips and colours, no C library - can have the borders without
the rest of the library: it compiles `src/overscan_host.c` alone with
`-Iinclude` and links that object instead of `libstdl.a` (never
both). It needs only libgcc's arithmetic, and STDL's build fails if
that ever changes, so a release that gives overscan.c a new hook
cannot break such a program's build. `STDL_OverscanHostInit()` takes
the page the borders open on (ST RAM, 256-byte aligned,
`STDL_OVERSCAN_PAGE_BYTES`), the screen to put back, the `_MCH`
cookie and whether the program blits with a border open; then the
usual open and close calls. The program flips its own pages and says
so from its VBL, twice a flip: `STDL_OverscanPageNext(page)` in the VBL
that writes the page to the video base, `STDL_OverscanPageShown()` in
the VBL after - the border interrupts compare the video counter with
the page on screen and the one coming, so a flip they were not told
about costs a frame's border and counts as a miss. Its VBL must count
`_frclock` ($466), as TOS's does; a Mega STE's speed is read from the
register at each open. `STDL_OverscanRelease()` puts the vectors,
timers and sync rate back touching nothing else, for an exception
handler. Keep row 0 of every page at colour 0: with the top border
open it is fetched, a word out of step, on the line above the
picture. `examples/ovhost.c` takes the machine that way and flips
three pages at 50Hz with both borders open: no misses over 300 frames
in Hatari on an ST, an STE and a Mega STE, and about 400 when either
call is left out. The object is 8.1K of code against the library
build's 7.4K.
