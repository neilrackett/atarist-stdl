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

`framesize` / `tilesize` are in words and describe one frame of one
variant / one tile including masks.

## 7. Music streams (STM)

`stdlconv midi` renders music to a YM2149 register stream replayed
at 50Hz by `STDL_Music`:

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

## 8. Palettes and hardware colour words

Logical palettes are RGB888. Hardware words are composed in STE
rotated-nibble format: for a 4-bit channel value `v`, the register
nibble is `(v >> 1) | ((v & 1) << 3)`. A plain ST reads bits 2-0 of
each nibble - the top three bits of the channel - so a single word
programs both register layouts correctly. `STDL_HWColour(r, g, b)`
performs this transformation from RGB888.
