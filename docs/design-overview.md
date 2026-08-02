# STDL — Planar Display Library for Atari ST

A deliberately incomplete, planar-native subset of SDL 1.2, intended to make
SDL 1.x game ports to the Atari ST / STE / Mega STE mostly mechanical.

**Status:** design sketch. Extracted from FreeNukum ST, to be proven against a
new port (Koules) before retrofitting existing ones.

---

## 1. Design principles

1. **Planar is the native format, not an export target.** There is no chunky
   backbuffer and no c2p step anywhere in the pipeline. Assets are converted to
   ST interleaved planar offline.
2. **Spans, rects and blits are the primary verbs.** `STDL_PutPixel` exists for
   completeness and is documented as slow.
3. **Incomplete on purpose.** Anything that cannot be done cheaply in planar is
   absent rather than emulated. The manual lists what is missing and why.
4. **Source compatibility, not ABI compatibility.** A compatibility header maps
   common `SDL_*` calls onto STDL so ported code reads close to the original.
5. **Mega STE first, ST/STE correct.** Blitter and 16MHz paths are optional
   accelerations behind runtime detection, never a correctness requirement.
6. **C with an opaque-handle object model.** One module per file, consistent
   `STDL_Module_Verb()` naming, ASM only where profiling justifies it.

---

## 2. Non-goals

Documented explicitly so porters know when to stop trying:

| Omitted | Reason |
|---|---|
| Arbitrary bpp surfaces | v1 is 4bpp low res only; 1bpp and 2bpp deferred |
| Per-pixel alpha / blending | No sane planar implementation |
| Runtime scaling / rotation | Pre-render or redesign |
| `SDL_LockSurface` semantics | Surfaces are always directly addressable |
| Threads, timers-as-threads | Cooperative single-thread model |
| Arbitrary palette depth | ST 512-colour / STE 4096-colour only |
| TrueType / SDL_ttf | Bitmap fonts converted offline |

---

## 3. The surface format contract

This is the real deliverable — freeze it first, because it is what makes
third-party tooling and asset converters possible.

### 3.1 Screen layout

**v1 targets low resolution only.** The other two modes are specified here so
the format contract stays stable when they arrive (see 3.1.1).

* Low resolution: 320x200, 4 planes, 16 colours, 160 bytes/line. **(v1)**
* High resolution: 640x400, 1 plane, monochrome, 80 bytes/line (SM124).
* Medium resolution: 640x200, 2 planes, 4 colours, 160 bytes/line.
* Pixels are grouped in 16s; a group is `nplanes` consecutive words.
* Pixel `x` occupies bit `15 - (x & 15)`, MSB first, in each plane word.
* Group address: `base + y * stride + (x >> 4) * nplanes * 2`.

All three modes are 32000 bytes, so full-screen fill and blit costs are
identical across them. High resolution is the degenerate — and therefore
fastest — case: one word per group, no plane loop, and a single blitter setup
per rect instead of four.

#### 3.1.1 Why the other modes are deferred

The addressing formula is already parameterised on `nplanes`, so both modes
cost nothing in the *format contract* and generic C paths would handle them
unchanged. Neither is free in the parts that matter:

* The hot blit and fill loops are plane-count specialised (4-plane `movem`
  block moves). Each additional plane count needs its own hand-written inner
  loops and test coverage.
* Blitter setup, dirty-rect maths and pre-shift tables gain a case each.
* Medium resolution additionally needs its own `stdlconv` palette-reduction
  and aspect path — 640x200 pixels are 1:2, so low-res art cannot be reused.

High resolution is the stronger candidate of the two and the cheaper to add:
one plane means the simplest code path in the library, and it unlocks the 2x2
mono emulation in 3.4. Medium resolution is the weakest — non-square pixels,
four colours, and almost no games ever shipped in it. Add high res once one
game is shipping in low res; revisit medium only if a port demands it.

### 3.2 Sprite storage

Per 16-pixel group, stored in draw order:

```
[ mask ][ plane0 ][ plane1 ][ plane2 ][ plane3 ]
```

`mask` is 1 where the destination should be preserved, allowing:

```c
*dst = (*dst & mask) | *src;
```

Pre-shifted sprites store 16 complete variants, one per `x & 15`, each padded
to one extra group horizontally.

### 3.3 On-disk asset container

A simple chunked format (`STDL` magic, version, chunk directory) holding
tilesets, sprite banks, palettes and bitmap fonts, so a converted game ships one
data file rather than a directory of raw dumps.

### 3.4 Mono emulation of low resolution (later phase)

Depends on high resolution support, so post-v1. 640x400 is exactly 2x2 of
320x200, so a low-res game can run unmodified on a mono monitor by
substituting pre-dithered assets and doubling coordinates.

* **Build-time, not runtime.** Dither expansion happens in `stdlconv`, which
  emits a mono variant of each asset bank. There is no per-frame conversion
  pass — that would be c2p by another name and breaks principle 1.
* **Colour to pattern.** Each of the 16 indices maps to a fixed 2x2 pattern.
  A 2x2 cell has exactly 16 distinct arrangements (1 + 4 + 6 + 4 + 1), so the
  mapping can be bijective, but only 5 of those are distinct *luminances*.
  Order indices by perceived brightness and use arrangement as a tie-break
  within a level, so same-brightness elements stay distinguishable by texture.
* **Alignment gets cheaper.** A low-res `x` maps to `2x` in high res, so only
  even bit offsets occur — 8 pre-shift variants instead of 16.
* **Cost is neutral.** Both modes are 32000 bytes; a sprite covering the same
  visual area occupies the same number of bytes in either.
* **Invisible to the game.** `STDL_SetVideoMode(320, 200, 4, STDL_MONO_AUTO)`
  succeeds on an SM124 by selecting high resolution internally, scaling
  coordinates and loading the mono bank. Ported source is unchanged.

Cost is the 4x asset RAM for the mono bank (only one bank is loaded at a
time) and the loss of colour as an information channel — fine for tile- and
sprite-based games, poor for anything using colour to encode state.

---

## 4. Core types

```c
typedef struct STDL_Surface  STDL_Surface;   /* planar pixel buffer      */
typedef struct STDL_Sprite   STDL_Sprite;    /* masked, optionally pre-shifted */
typedef struct STDL_Tileset  STDL_Tileset;   /* fixed-size, 16px-aligned */
typedef struct STDL_Font     STDL_Font;      /* bitmap font              */
typedef struct STDL_Palette  STDL_Palette;   /* 4 or 16 hardware entries */

typedef struct { int16_t x, y; }                   STDL_Point;
typedef struct { int16_t x, y; uint16_t w, h; }    STDL_Rect;
```

### STDL_Surface (public fields)

| Field | Meaning |
|---|---|
| `pixels` | Base address of planar data |
| `w`, `h` | Dimensions in pixels |
| `stride` | Bytes per scanline |
| `planes` | 4 (low) or 2 (medium) |
| `flags` | `STDL_SCREEN`, `STDL_HWSURFACE`, `STDL_DOUBLEBUF` |
| `clip` | Current clip rectangle |

---

## 5. Module overview

### 5.1 `STDL_Video` — init, mode, page flipping

```c
int            STDL_Init(uint32_t flags);
void           STDL_Quit(void);
STDL_Surface  *STDL_SetVideoMode(int w, int h, int bpp, uint32_t flags);
STDL_Surface  *STDL_GetVideoSurface(void);
void           STDL_Flip(void);              /* VBL-synced page flip     */
void           STDL_UpdateRects(int n, STDL_Rect *rects);
void           STDL_WaitVBL(void);
```

Handles resolution save/restore, screen memory alignment (256-byte on ST,
word on STE), and double/triple buffer allocation. Post-v1, `STDL_MONO_AUTO`
lets a low-res request on an SM124 transparently become high resolution with
2x coordinate scaling (see 3.4).

### 5.2 `STDL_Draw` — primitives

```c
void STDL_FillRect(STDL_Surface *dst, const STDL_Rect *r, uint8_t col);
void STDL_HLine(STDL_Surface *dst, int x1, int x2, int y, uint8_t col);
void STDL_VLine(STDL_Surface *dst, int x, int y1, int y2, uint8_t col);
void STDL_Line(STDL_Surface *dst, int x1, int y1, int x2, int y2, uint8_t col);
void STDL_Circle(STDL_Surface *dst, int cx, int cy, int r, uint8_t col);
void STDL_FillCircle(STDL_Surface *dst, int cx, int cy, int r, uint8_t col);
void STDL_PutPixel(STDL_Surface *dst, int x, int y, uint8_t col);  /* slow */
void STDL_SetClipRect(STDL_Surface *dst, const STDL_Rect *r);
```

All fills route through a precomputed `fill[colour][plane]` word table; all
shapes decompose to masked spans.

### 5.3 `STDL_Blit` — surface and sprite copying

```c
void STDL_BlitSurface(STDL_Surface *src, const STDL_Rect *srcrect,
                      STDL_Surface *dst, STDL_Rect *dstrect);
void STDL_BlitSprite(STDL_Sprite *spr, int frame, STDL_Surface *dst,
                     int x, int y);
void STDL_BlitTile(STDL_Tileset *ts, int index, STDL_Surface *dst,
                   int x, int y);
```

Dispatch strategy, in order of preference:

1. Aligned, unmasked — `movem` block copy.
2. Aligned, masked — per-group mask/or.
3. Unaligned, pre-shifted variant available — select variant, use (1) or (2).
4. Unaligned, no variant — runtime shift chain (documented as the slow path).
5. Large aligned rect on STE/Mega STE — blitter, one pass per plane.

### 5.4 `STDL_Dirty` — background restore

The module Koules will force into existence; neither FreeNukum nor Sopwith
needed it.

```c
void STDL_DirtyInit(STDL_Surface *background, int max_rects);
void STDL_DirtyPush(const STDL_Rect *r);      /* mark for restore   */
void STDL_DirtyRestore(void);                 /* repaint from background */
void STDL_DirtyReset(void);
```

Alternative save-under strategy under the same interface, chosen at init.

### 5.5 `STDL_Palette`

```c
void STDL_SetPalette(STDL_Surface *s, const STDL_Palette *pal);
void STDL_SetColour(int index, uint16_t stColour);
void STDL_FadeTo(const STDL_Palette *target, int frames);
```

Handles ST 3-bit vs STE 4-bit register layout transparently, plus optional
VBL-driven fades. In high resolution the module degenerates to inverse-video
control, and fades become no-ops.

### 5.6 `STDL_Event` — input

```c
int  STDL_PollEvent(STDL_Event *e);
int  STDL_WaitEvent(STDL_Event *e);
const uint8_t *STDL_GetKeyState(int *numkeys);
```

IKBD-driven keyboard (scancode → SDL-like keysym table), mouse and joystick.
Event union deliberately mirrors SDL 1.2 shape to ease porting.

### 5.7 `STDL_Time`

```c
uint32_t STDL_GetTicks(void);      /* 200Hz system timer  */
void     STDL_Delay(uint32_t ms);
void     STDL_FrameLimit(int fps); /* VBL-aligned pacing  */
```

### 5.8 `STDL_Audio` (optional, later)

YM2149 via a tracker replay hook on ST; DMA sample playback on STE/Mega STE.
Deliberately last — most ports can ship silent first.

### 5.9 `STDL_Asset`

```c
STDL_Sprite  *STDL_LoadSprite(const char *bank, int id, uint32_t flags);
STDL_Tileset *STDL_LoadTileset(const char *bank, int id);
STDL_Font    *STDL_LoadFont(const char *bank, int id);
```

`flags` includes `STDL_PRESHIFT` — 16x RAM for zero-cost unaligned blits.

---

## 6. Tooling: `stdlconv`

Host-side converter (Python or C, run under `stcmd`-adjacent tooling):

* PNG / PCX / EGA planar / SDL surface dumps → STDL asset bank.
* Palette quantisation and remapping to ST/STE hardware colours.
* Mask generation from colour key or alpha channel.
* Optional pre-shift expansion.
* Optional mono bank generation (2x2 ordered dither, 2x expansion) — post-v1.
* Tileset slicing and sprite-sheet indexing.

Arguably more valuable than the runtime — it is the part every port otherwise
reinvents.

---

## 7. SDL 1.2 compatibility shim

`stdl_compat.h` maps the common surface so ported source stays recognisable:

| SDL 1.2 | STDL |
|---|---|
| `SDL_Init` / `SDL_Quit` | `STDL_Init` / `STDL_Quit` |
| `SDL_SetVideoMode` | `STDL_SetVideoMode` |
| `SDL_BlitSurface` | `STDL_BlitSurface` |
| `SDL_FillRect` | `STDL_FillRect` (colour index, not mapped RGB) |
| `SDL_Flip` | `STDL_Flip` |
| `SDL_MapRGB` | `STDL_MapRGB` — nearest palette index |
| `SDL_LockSurface` | no-op |
| `SDL_DisplayFormat` | no-op (already planar) |

Anything not in the table is a compile error by design, so the porter finds
gaps at build time rather than at runtime.

---

## 8. Bring-up: the SDL 1.2 test suite

Before any game, port a subset of the `test/` directory from
`libsdl-org/SDL-1.2`. It is a conformance suite for almost exactly the subset
STDL defines, each program isolates one API area, and none of it needs game
assets or a licence decision — the SDL 1.2 library is LGPL-2, but the test
programs are public domain, so they can live in `examples/` permanently.

### 8.1 Port these, in order

| Test | Exercises |
|---|---|
| `testbitmap` | 1bpp blit — simplest possible bring-up |
| `graywin` | `STDL_FillRect`, clipping |
| `testwin` | Surface blit, colour key |
| `testsprite` | Masked sprite blits + `SDL_UpdateRects` dirty-rect flow |
| `testpalette` | `STDL_SetPalette`, palette animation |
| `checkkeys` / `testkeys` | Keysym mapping for the IKBD table |
| `testtimer` | `STDL_GetTicks` against the 200Hz timer |
| `testblitspeed`, `testvidinfo` | Blit throughput benchmarks |

`testsprite` is the important one — it is the closest thing to a reference
implementation of the masked-blit and dirty-rect flow STDL has to get right.

`testblitspeed` and `testvidinfo` are the sleeper value: they give real
numbers on a Mega STE and a plain ST early enough to settle how much of the
blit path can stay in C, before any game has been committed to.

### 8.2 Do not port these

`testalpha`, `testgl`, `testdyngl`, `testoverlay`, `testcdrom`, and the
thread and semaphore tests all target documented non-goals (section 2). A
failing test in this group is the suite disagreeing with STDL's scope, not a
bug. Keep them out of the tree so nobody is tempted.

### 8.3 What the suite will not tell you

Frame budget under real load, dirty-rects vs save-under, and everything about
the asset pipeline. Passing the suite is bring-up, not shipping — a real game
still has to follow.

---

## 9. Suggested porting workflow

1. Convert assets with `stdlconv`; verify visually in Hatari.
2. Drop in `stdl_compat.h`, build, and let link errors enumerate the work.
3. Replace unsupported calls with STDL primitives.
4. Replace per-pixel drawing with spans or pre-rendered sprites.
5. Align sprites to 16px or enable `STDL_PRESHIFT`.
6. Profile on Mega STE; enable blitter paths where rects are large enough.
7. Verify on plain ST at 8MHz — the correctness floor.

---

## 10. Repository layout

```
stdl/
  include/stdl/        public headers, one per module
  src/                 video, draw, blit, dirty, palette, event, time, asset
  src/m68k/            hand-written ASM (blit inner loops, shift chains)
  tools/stdlconv/      asset converter
  examples/            ported SDL 1.2 test programs (public domain) + per-module harnesses
  docs/
    format.md          surface + asset format contract (freeze first)
    porting.md         SDL 1.2 → STDL guide
    limits.md          what STDL will not do, and why
  skills/stdl/         LLM skill: SKILL.md + references
```

---

## 11. Companion skill outline

A `SKILL.md` letting an LLM do the mechanical porting work. Contents:

* The surface format contract, stated precisely enough to generate correct
  blit code without guessing.
* The API reference, with cost annotations (cheap / avoid / slow path).
* The SDL 1.2 → STDL mapping table plus common rewrite patterns
  (per-pixel loop → span fill, `SDL_Rect` clipping → `STDL_SetClipRect`).
* Asset conversion recipes for common source formats.
* Toolkit invocation conventions (`STCMD_NO_TTY=1 stcmd`), Hatari test loop.
* Anti-patterns: introducing c2p, calling `STDL_PutPixel` in a loop, assuming
  arbitrary bpp, assuming unaligned blits are free.

---

## 12. Open questions

* High resolution is the first mode to add after v1 ships; medium resolution
  only if a port demands it (see 3.1.1).
* Is 2x2 mono emulation worth building, or should mono ports be authored
  natively at 640x400? Games that encode state in colour will not survive the
  dither regardless.
* Should the mono bank ship alongside the colour bank, or as a separate build?
* Dirty rects vs save-under as the default — decide with Koules profiling.
* Is the asset container worth it, or are loose converted files fine for v1?
* How much of the blit path can stay C before Mega STE frame budget bites?
* Should `STDL_Sprite` own its pre-shift variants, or should that be a separate
  cache so RAM-tight ports can opt out per sprite?
