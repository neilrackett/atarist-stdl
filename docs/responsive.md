# More colours and lines on a TT or a Falcon

An ST game runs on a TT or a Falcon as it is, in ST Low (porting.md,
[ST-specific gotchas](porting.md#st-specific-gotchas), says where the
library covers their differences). What they add - 256 colours, and 240
lines - a game asks for by name with `STDL_UseResponsive`
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

`examples/respdemo.c` is the pattern.

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
  a real TT took 1.5 to 2.3 times as long as Hatari's TT over the
  same compiled code, and a real Falcon has not been timed, so take a
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

An SDL2 program for a TT or a Falcon only has an add-on of its own on
this screen: [sdl2.md](sdl2.md).
