# More colours and lines on a TT or a Falcon

An ST game runs on a TT or a Falcon as it is, in ST Low (porting.md,
[ST-specific gotchas](porting.md#st-specific-gotchas), says where the
library covers their differences). What they add - 256 colours, 240
lines, and 640x480 in 16 colours - a game asks for by name with `STDL_UseResponsive`
(`stdl_responsive.h`), once, before the first `STDL_SetVideoMode`.
Nothing changes for a game that does not ask, and one binary serves
every machine:

```c
STDL_UseResponsive(STDL_RESPONSIVE_COLOUR | STDL_RESPONSIVE_HEIGHT);
screen = STDL_SetVideoMode(320, 240, 8, STDL_DOUBLEBUF);
deep = screen->format->BitsPerPixel == 8;   /* 256 colours, or 16 */
rows = screen->h;                           /* 240, or 200 */
```

| Option                   | A TT or a Falcon               | An ST, an STE or a Mega STE                    |
| ------------------------ | ------------------------------ | ---------------------------------------------- |
| `STDL_RESPONSIVE_COLOUR` | 256 colours, 8 planes          | 16 colours, as without it                      |
| `STDL_RESPONSIVE_HEIGHT` | 240 rows                       | 200 rows, no border, no error                  |
| `STDL_RESPONSIVE_TALL`   | 240 rows                       | 240 rows, the bottom border open ([overscan.md](overscan.md)) |
| `STDL_RESPONSIVE_BANKS`  | each bank id's 256-colour art  | the ST's art, as without it                    |
| `STDL_RESPONSIVE_WIDE`   | 640x480 in 16 colours          | 320 pixels, as the call's other options give, the error set |

`examples/respdemo.c` is the pattern, and `examples/widedemo.c` for the
wide screen.

* **Branch on the screen, not the machine.** `BitsPerPixel` and `h`
  say what was given: an ST answers 4 and 200, or 240 with
  `STDL_RESPONSIVE_TALL`, its bottom border open.
* **Colours.** Every call takes 0-255 on an 8-plane screen. Keep 0-15
  the ST's palette and put the extra shades above them: text, the HUD
  and anything drawn from 4-plane data (a surface made before the
  screen, a loader's image, `STDL_PutGroup`) keep 0-15, so they look
  the same on both. One port put its ramps of 32 shades at 64-255.
* **Surfaces** created while an 8-plane screen is up have 8 planes,
  and blit, sprite and tile onto each other as usual; a tileset cut
  from one has 8 planes, and 4-plane tiles draw onto one in colours
  0-15. What they refuse, each setting the error and drawing nothing:
  a blit, a sprite or a tile from 8 planes onto 4, patterned polygons
  and the cursor.
  `STDL_TRANSPARENT` punches holes in a masked one as on 4 planes; the
  blit flags (UNDER, MARK) keep a priority plane in its mask; and
  `STDL_CreateSurfaceFrom8Planes` lays one over a program's own
  8-plane block.
* **Pixels a program writes itself**: tell an 8-plane screen with
  `STDL_UpdateRects` (`SDL_UpdateRect`); the tall 16-colour screen
  needs nothing, as a TT compares its whole page. The layout is the
  display's (`stdl_responsive.h`): 16 bytes a group, the next page's
  `pixels` after each Flip; on a TT a stride of two display lines, so
  a program converting its own picture can write both lines of a row
  and report nothing, where one box around scattered reports would
  copy everything between them.
* **Colours with a picture**: with `STDL_StagePalette(1)`, the colours
  set just before `STDL_Flip` are shown with its page, triple-buffered
  too; unstaged, an 8-plane screen's are written when they are set.
* **What it costs.** The colour's code is about 25K of program: on a
  512K ST link libcmini (one port kept 107K free with it). A TT copies
  what changed to its doubled lines at every Flip, 4.1ms a frame for
  one port with the colour; its tall 16-colour screen compares the
  whole page instead, 6.3-7.5ms, more than the colour. A Falcon's
  screen is the display itself, with nothing to copy. A responsive
  screen is always double-buffered, and `STDL_TRIPLEBUF` gives it a
  third page as on an ST, another of the display's: 153.6K of ST RAM
  on a TT, 64-92K on a Falcon in 256 colours. The times are Hatari's:
  a real TT takes 2.3 times as long as Hatari's TT over the same
  compiled code, and 3.0 to 3.9 times over copies into ST-RAM, which
  is what a present is; a real Falcon has not been timed, so take a
  frame budget from a machine.
* **Art.** With `STDL_RESPONSIVE_BANKS` as well, a bank holds each
  id's 256-colour version beside the ST's, and the same
  `STDL_LoadSurface`, `STDL_LoadSprite`, `STDL_LoadTileset` and
  `STDL_LoadPalette` calls give whichever the screen can show: art of
  its own (`stdlconv bank`'s `depth=8`, and `map=PAL` for the ST's
  chunk from the same image), or planes 4-7 over the ST's art
  (`depth=hi`, with `low` for the ST's), which keeps one set of colour
  numbers for both machines (`docs/format.md` section 6.1). An ST
  reads only its own chunks.
* **Console text** printed after the mode set lands in the picture, as
  on an ST; on a TT the next Flip shows the screen as the program
  drew it.

## 640x480 in 16 colours: `STDL_RESPONSIVE_WIDE`

A TT's TT Medium and a Falcon's 640x480 in 16 colours, for a game made
for a TT or a Falcon, or a port of a PC game drawn at 640x480 in 16
colours (a Windows 3.x or VGA-era DOS game), which a 320-pixel screen
would have to halve. A call asking for more than 320 pixels gets it:

```c
STDL_UseResponsive(STDL_RESPONSIVE_WIDE);
screen = STDL_SetVideoMode(640, 480, 4, STDL_TRIPLEBUF);
if (screen->w < 640) {
    /* an ST: 320x200, and STDL_GetError() says why */
}
```

| Machine | The screen |
| --- | --- |
| TT | TT Medium, the TT's own 640x480 in 16 colours |
| Falcon on VGA | its own 640x480 in 16 colours |
| Falcon on RGB or a TV | 640x400 interlaced in the desktop's PAL or NTSC, the VIDEL's window opened 40 lines at each end to 480, as its overscan modes have it; 640x400 if a mode leaves no room |
| Falcon on mono | refused, as every screen is |
| ST, STE, Mega STE | 320 pixels: 320x200, or 320x240 with `STDL_RESPONSIVE_TALL` also asked, the error set |

* **Branch on `w`, and on `h`.** An ST has no screen of 640 pixels in
  16 colours (ST Medium has 4 colours and 200 rows), so there is no
  fallback that keeps the layout: a game that cannot lay itself out in
  320 pixels stops there. A Falcon's RGB says 400 if it could not open
  the window.
* **The screen is an ordinary 4-plane surface over the display page
  itself**: 320 bytes a row, 16-pixel groups of four plane words as on
  an ST. Every call works on it, so do a program's own writes, and
  Flip only swaps the page; there is nothing to copy and nothing to
  report. Surfaces made while it is up have 4 planes, as on an ST.
* **The colours** are 0-15, set as an ST's are (`STDL_SetColour`,
  palettes, fades, `STDL_StagePalette`), into the TT's registers (the
  bank TT Medium shows) or the Falcon's.
* **With the other options.** It wins for a call asking more than 320
  pixels; a call asking 320 gets the colour or the tall screen as
  before, so one program can ask for all of them and choose by the
  call. The cursor is refused on it, as on every responsive screen,
  and there are no borders, rasters or hardware scrolling.
* **What it costs.** A page is 153.6K of ST RAM on every machine and
  monitor, so double-buffered 307K and triple-buffered 461K. 640x480 is
  4.8 times an ST's pixels and a TT has no BLiTTER (a Falcon's is not
  used), so a frame that redraws the whole screen costs more than a
  frame has: draw what changed. A background surface the screen's size
  with `STDL_DirtyRestore` a page, as WIDEDEMO does, kept 24 balls of
  24x24 at 60 frames a second on Hatari's TT and Falcon (ST RAM only);
  TT-RAM, where a TT has it, is faster for the CPU than ST RAM for
  everything but the pages. The code is 0.5K (`wide4.o`), linked only
  where it is asked for.
* **A Falcon's RGB picture is interlaced**: each line is shown 25 times
  a second in PAL, so one-pixel lines flicker on a CRT, and a frame
  flipped mid-picture shows the old and new frames on alternate lines
  for a field. A flicker fixer or an upscaler removes the flicker;
  software cannot. Hatari shows the whole picture but neither the
  flicker nor what a real monitor or TV crops, and it has not been seen
  on a real Falcon.

An SDL2 program for a TT or a Falcon only has an add-on of its own on
this screen: [sdl2.md](sdl2.md).
