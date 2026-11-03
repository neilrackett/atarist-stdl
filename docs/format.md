# STDL format contract

This file is the normative description of every byte layout STDL
touches. Freeze it before anything else: it is what makes
third-party tooling and asset converters possible.

Everything is big-endian (68000 native).

## 1. Screen and surface layout

v1 targets ST low resolution only. The other modes are specified so
the contract stays stable when they arrive.

| Mode | Pixels | Planes | Colours | Bytes/line |
|---|---|---|---|---|
| Low (v1) | 320x200 | 4 | 16 | 160 |
| High (post-v1) | 640x400 | 1 | mono | 80 |
| Medium (unplanned) | 640x200 | 2 | 4 | 160 |

* Pixels are grouped in 16s; a **group** is `planes` consecutive
  words (8 bytes in low resolution).
* Pixel `x` occupies bit `15 - (x & 15)`, MSB first, in each plane
  word of its group.
* Group address: `pixels + y * stride + (x >> 4) * planes * 2`.
* A pixel's colour index is built LSB-first across planes: bit `p`
  of the index comes from plane word `p`.

`STDL_Surface.stride` is `((w + 15) >> 4) * planes * 2`; widths are
padded up to a multiple of 16 pixels internally while `w` keeps the
requested value. Padding pixels exist in memory but are never drawn.

### Plane budget

The layout above never changes: a group is always `planes`
consecutive words and `stride` is always computed from four planes
in low resolution. What can change is how many of those planes STDL
maintains.

`STDL_SetPlaneBudget(N)` declares that no colour index `>= 2^N` will
be drawn again. It adds one invariant to the contract:

* **planes `N..3` of every surface, and of every screen page, are
  zero.**

Given that invariant, a primitive may skip any write whose value
would be zero, which is every write to a plane at or above the
budget. Skipping is a licence, never an obligation: a path that can
write a whole group faster than it can write part of one (a `memset`
clear) is still free to do so, because the bytes it puts in the high
planes are the zeros already there.

Consequences that are part of the contract, not implementation
detail:

* Colour indices are effectively masked to the low `N` bits. Drawing
  colour 9 at budget 2 draws colour 1 - the same on every path,
  CPU or BLiTTER.
* Every public entry point that writes pixels honours the budget,
  including the raw group writers (`STDL_PutGroup`,
  `STDL_PutGroup8`) and the wholesale loaders (BMP, Degas, bank
  surfaces), so the invariant cannot be broken from outside.
* Lowering the budget re-zeroes planes `N..3` of the screen pages.
  It does **not** walk surfaces the program allocated - those start
  zeroed and stay compliant while the promise holds. Data poked
  straight into a surface's `pixels` by the program is the program's
  problem.
* All 16 palette entries remain settable and are programmed to the
  hardware unchanged; only the first `2^N` can appear on screen.
* `N` is 1..4 and defaults to 4, which is the layout above with
  nothing skipped.

### Guard bytes

Surfaces allocated by `STDL_CreateSurface` carry 8 slack bytes
before and after the pixel block (and mask block). The unaligned
blit path may **read** (never write) up to one group before or
after the source rectangle; the slack keeps those reads inside the
allocation. The screen itself has no guard - stray reads next to
screen RAM are harmless on the ST and the values are masked off.

## 2. Transparency masks

A surface's colour-key mask (built by `STDL_SetColourKey`) is one
bit per pixel, one word per group, same bit order as the planes:

* mask bit **set** = pixel is transparent = destination preserved
* `maskstride = (stride / planes) / ... ` - i.e. `groups * 2` bytes
  per line

The mask is a snapshot of the pixels at the time the key is set.
Modify the pixels and you must call `STDL_SetColourKey` again.

Blits and draw primitives onto a masked surface *maintain* the
destination's mask (drawn pixels clear their bits; masked blits AND
the source mask in) - they do not *consult* it as protection. The
exceptions are the two flags that take a destination mask as a
foreground/priority plane: `STDL_I8_UNDER` (`STDL_BlitIndexed8`) and
`STDL_BLIT_UNDER` (`STDL_BlitSurfaceEx`) treat set destination-mask
bits as protected pixels - the "sprites pass behind marked
foreground" idiom - while `STDL_I8_MARK` / `STDL_BLIT_MARK` set bits
under drawn pixels instead of clearing them. The two flag sets share
their bit values, so one set of flags serves a scene composed from
both indexed and planar sources.
A key of 16 or more is not a pixel value: it enables masked blits
from whatever mask the surface already carries (built by
`STDL_CreateMask`, `STDL_PutGroup`/`STDL_PutGroup8` or transparent
fills) and never scans or overwrites it.

## 3. Sprite storage

Per 16-pixel group, stored in draw order:

```
[ mask ][ plane0 ][ plane1 ][ plane2 ][ plane3 ]
```

`mask` follows the same convention (set = preserve destination) and
plane bits are zero wherever the mask is set, so the inner loop is:

```c
*dst = (*dst & mask) | *src;
```

Rows are stored top to bottom, groups left to right, frames
consecutive. Pre-shifted sprites store 16 complete variants, one
per `x & 15`, each padded to one extra group horizontally; variant
`v` is variant 0 shifted right `v` pixels with masks 1-filled and
planes 0-filled at the edges. Variants are stored variant-major:

```
variant -> frame -> row -> group -> 5 words
```

Sprite data the library allocates (`STDL_SpriteFromSurface`,
`STDL_LoadSprite`) is followed by one group - 10 bytes - of slack
that is not part of the layout: a skewed BLiTTER pass reads one
source word beyond the span it copies, and on a sprite's last row
that word lies past the data. The slack is read, never used.

## 4. Tileset storage

Like sprites but per-tile, with the mask word optional
(`masked` flag in the header). Tiles are the aligned fast path by
definition: `STDL_BlitTile` rounds x down to a group boundary.

## 5. Bitmap fonts

1bpp glyph rows, MSB-first (ST/X-bitmap order after reversal),
glyph-major: all rows of glyph n, then glyph n+1. `bytes_per_row =
(cw + 7) / 8`. Cell width is limited to 16 pixels.

## 6. Asset bank container (`stdlconv bank`)

```
offset  size  field
0       4     magic "STDL"
4       2     version = 1
6       2     nchunks
8       12*n  directory entries
...           payloads (byte-packed, no alignment guarantee)
```

Directory entry: `u16 type, u16 id, u32 offset, u32 length`.
Offsets are from the start of the file.

Chunk types and payloads:

| Type | Name | Payload |
|---|---|---|
| 1 | palette | `u16 n`, then n x `u8 r, g, b, 0` (RGB888) |
| 2 | surface | `u16 w, u16 h, u8 haskey, u8 key`, planar rows |
| 3 | sprite | `u16 w, h, nframes, u8 nvariants, u8 planes, u16 groups, u32 framesize`, data words |
| 4 | tileset | `u16 tw, th, ntiles, groups, u8 masked, u8 planes, u32 tilesize`, data words |
| 5 | font | `u16 cw, ch, u8 first, last, u16 bytes_per_row`, glyph bits |
| 6 | surface8 | `u16 w, u16 h, u8 form, u8 key`, rows (below) |
| 7 | sprite8 | `u16 framew`, then a surface8 payload of the frames side by side |
| 8 | palette8 | as palette, n up to 256 |

`framesize` / `tilesize` are in words and describe one frame of one
variant / one tile including masks.

### 6.1 A TT's or a Falcon's 256 colours

Types 6-8 are an 8-plane screen's own (`stdl_responsive.h`), read only
by a program that asks for `STDL_RESPONSIVE_BANKS` with the colour, on
a TT or a Falcon. Its `STDL_LoadSurface`, `STDL_LoadSprite` and
`STDL_LoadPalette` look for an id's 8-plane version first and take the
4-plane chunk of the same id where the bank has none; an ST reads only
types 1-5, so the others cost it disk space and nothing else. A
surface8's `form` byte says which of two forms an id's art takes:

* **0, its own; 1, its own keyed on `key`:** 8-plane rows - a group is
  eight words, plane 0 first, 16 bytes - and a palette of up to 256
  colours, the art drawn for the machine. The key is a colour of 256;
  it cannot be 16, which an 8-plane surface keeps as
  `STDL_TRANSPARENT`. The ST's chunk of the id is art of its own, in
  its own 16 colours.
* **2, over the ST's:** planes 4-7 only, as a 4-plane image is stored,
  over the 4-plane chunk of the same id, which gives planes 0-3 and the
  transparency (a surface's key, a sprite's masks); `key` is unused. A
  pixel's low nibble is its ST colour and its high nibble a shade of
  it: 16 families of 16, colour = shade << 4 | ST colour, the palette a
  palette8 whose entries 0-15 are the ST's. The ST's art is carried
  once. A sprite's 4-plane chunk must then have one variant, frames of
  a whole number of groups, and `framew` and the frames' count those
  of the strip.

One type for both forms, so one lookup finds either, and a bank cannot
hold two 8-plane versions of an id.

`stdlconv bank` writes either from one piece of art of up to 256
colours: `depth=8` the chunk of its own and `map=PAL` the ST's (every
colour put on the nearest of PAL's 16, the key keeping one of its own),
or `depth=hi` the planes over the ST's and `low` the ST's (the low
nibbles, and the palette's first 16).

## 7. Music streams (STM)

`stdlconv midi` (General MIDI) and `stdlconv opm` (YM2151 VGM logs)
render music to a YM2149 register stream replayed at 50Hz by
`STDL_Music`:

```
"STM1"  u16 tick_hz  u16 nframes  u16 loop_frame  u16 reserved
frames: u16 change mask (bit r = YM register r, 0..13), then one
        byte per set bit, ascending register order
```

Rules:

* Registers 14/15 (the I/O ports TOS uses for floppy select) must
  never appear in the mask; the loader rejects streams that touch
  them.
* Register 7 (mixer) is stored with bits 6-7 clear; the player ORs
  in the live port-direction bits at write time.
* Register 13 (envelope shape) appears in a frame only when an
  envelope retrigger is intended, since writing it restarts the
  envelope.
* Volume registers written without bit 4 (fixed-volume channels)
  are scaled by `STDL_VolumeMusic` on the fly.
* A frame holds only the registers that change, so frame 0 should
  set all fourteen (`stdlconv` always does). `STDL_SeekMusic`
  replays from silence: every register 0 and register 7 at 0x3F.
* `stdlconv` also sets registers 0-12 at the loop frame, whether
  they changed or not, so a player that jumps there from the last
  frame lands on exactly the loop's state.

## 8. Palettes and hardware colour words

Logical palettes are RGB888. Hardware words are composed in STE
rotated-nibble format: for a 4-bit channel value `v`, the register
nibble is `(v >> 1) | ((v & 1) << 3)`. A plain ST reads bits 2-0 of
each nibble - the top three bits of the channel - so a single word
programs both register layouts correctly. `STDL_HWColour(r, g, b)`
performs this transformation from RGB888, and `STDL_HW444(0xRGB)`
from a constant with four bits a channel, at compile time - a raw
0xRGB written to a register is not the colour it reads as.
