# Design, 2026-10-07: video foundations

*The panel, the canvas and the integer display resolutions.*

*Written as a proposal; Doc answered its twelve questions on 2026-10-07
(`docs/notes/decisions-2026-10-07.md`) and it was built the same day on
the branch `video-foundations` -- `docs/BUILD-LOG.md` has what, and where
it differs from the text below: the cells are 16 wide on any glass over
1279 as well as at scale 1; K/OS takes 132 columns at most; the pixel cap
is measured per host (`tools/k4510-vidcap`); the menu labels a choice by
its grid ("720x540 90x33"); stretch-to-4:3 is reserved.*

Doc's ask, in his words as the session put it:

- At boot, find the display panel's native resolution.
- Find whether the user set boot to a 4:3 canvas or the whole panel.
- For 4:3, the largest 4:3 rectangle in the panel is the canvas.
- From the canvas, the *integer display resolutions* (IDRs): canvas/1,
  /2, /4 ... -- 1920x1080 gives 1440x1080, 720x540, 360x270; 1920x1200
  gives 1600x1200, 800x600, 400x300. Very high-definition panels need
  numbers: some IDRs will be too big to draw at 60 frames a second.
- K/OS and JIM always run in an IDR.
- Text: in every IDR, 8x8 and 8x16 cells, plus the HD 16x16 and 16x32,
  so each IDR has low- and high-resolution text at normal and double
  line counts.
- Software resolutions that are *not* integer divisions of the panel:
  let the game's developer choose one, within limits, and handle it;
  the smoothing and the shaders are there if they want them.

---

## 1. What exists today

### 1.1 The panel is never asked

Nothing in the frontend asks SDL what the display is. The window is
created at 1280x960 (`sdl/main.c:1245`, `K4510_WINDOW=WxH` overrides
it), then made `SDL_WINDOW_FULLSCREEN_DESKTOP` on the first frame
(`sdl/main.c:2056`), and since 2026-10-06 full screen is forced on
whatever k4510.cfg says (`core/ui/settings.c:248`). After that the
only size the frontend knows is `SDL_GetRendererOutputSize`, read every
frame for Placement and the side areas (`sdl/main.c:2244`). It is
never compared with the machine's resolutions.

The resolution list is a constant. VICKY's HD family is 1440x1080
divided by 1, 2 or 4, chosen by CTRL bits 5, 1-2 and 4
(`core/vicky.c:23-28`); `VICKY_WIDTH` and `VICKY_HEIGHT` are 1440 and
1080 (`core/vicky.h:137`). F12's Resolution row offers exactly those
three (`core/ui/settings.c:13`, `settings_first` at `:125`). The
2026-10-06 log calls them "the whole dividers of the panel's 4:3"; they
are the whole dividers of *a 1080-line panel's* 4:3, on every panel.

What that does on other panels, checked under Xvfb with
`K4510_GLASS` (the whole renderer read back), MODE 5:

- **1366x768: the picture is cropped.** 1440x1080 cannot be shown at
  1x, `SDL_RenderSetIntegerScale` will not go below 1, and the glass is
  centred and clipped: the banner's first two lines and the prompt's
  first characters are off the screen. MODE 6 (720x540) shows at 1x in
  a 1366x768 frame, MODE 7 (360x270) at 2x. Nothing fills the panel.
- **1920x1200: a 240/60-pixel border.** 1440x1080 at 1x; the panel's
  own 4:3, 1600x1200, is not on offer.

### 1.2 The modes, the cells and the grids

From the ROM's tables (`rom/kernal.c:2012-2016`, `video_init` at
`:2018`) and the frontend's (`sdl/main.c:1782`,
`core/ui/settings.c:13-15`):

| MODE | Glass | CTRL | Cells | Grid | F12 name |
|---|---|---|---|---|---|
| 0 | 640x480 | 1 | 8x16 | 80x30 | 640x480 |
| 0 60 | 640x480 | 1 | 8x8 | 80x60 | 640x480x60 |
| 1 | 640x240 (lines doubled) | 1\|4 | 8x8 | 80x30 | 640x240 |
| 2 | 320x240 (doubled) | 1\|2 | 8x8 | 40x30 | 320x240 |
| 3 | 320x200 (doubled, 40-line bars) | 1\|2\|8 | 8x8 | 40x25 | 320x200 (live only) |
| 4 | 160x200 | 1\|2\|8\|16 | 8x8 | 20x25 | 160x200 (live only) |
| 5 | 1440x1080 | 1\|32 | 16x32 | 90x33 | 1440x1080 16x32 |
| 5 67 | 1440x1080 | 1\|32 | 16x16 | 90x67 | 1440x1080 16x16 |
| 6 | 720x540 | 1\|32\|6 | 8x16 (HD: 16x32 glyphs) | 90x33 | 720x540 16x32 |
| 6 67 | 720x540 | 1\|32\|6 | 8x8 (HD: 16x16 glyphs) | 90x67 | 720x540 16x16 |
| 7 | 360x270 | 1\|32\|6\|16 | 8x8 | 45x33 | 360x270 |

MODE 0-4 are drawn into a 640x480 glass, doubled where the mode is
smaller (`core/vicky.c:522-586`). The HD spare lines (1080 is not a
multiple of 32, 540 not of 16, 270 not of 8) are split above and below
the text by scrolling layer 0 down (`vpad_of`, `rom/kernal.c:2015`), and
text32 paints them with the nearest row's background
(`core/vicky.c:244-250`). There is no horizontal padding: every glass
today is a whole number of cells wide.

HD text (`core/vicky.c:477`, `hd_line_draw` at `:488`) is 720x540 only:
VICKY composes each line at 720 as always, then writes it twice at 1440,
doubling every pixel except where a stock 8x16 (or 8x8) glyph is on
top, which comes from the F12 font's 16x32 (or 16x16) instead. The
glass the machine sees stays 720x540. The F12 Font row picks the face;
"unscii" turns HD off.

### 1.3 Where 4:3 versus the whole panel is decided

Nowhere, as a choice. Every glass is 4:3 (MODE 1, 3 and 4 have
non-square pixels inside a 4:3 glass). The space beside a 4:3 picture
on a wide panel is the border colour, the sidebars (built out since
2026-10-06) or the side panel (Apple IIe, registers), and Placement
(centre, left, right; `core/ui/settings.c:65`) moves the picture.

### 1.4 Scaling, smoothing, shaders

- **Integer**, the only mode reachable: SDL's logical size is the glass
  and `SDL_RenderSetIntegerScale` floors the scale
  (`sdl/main.c:2073-2080`, `:2264`); Placement left or right does the
  same arithmetic by hand in device pixels (`:2246-2258`). Nearest
  filtering everywhere.
- **Fit (sharp-bilinear)** is still in the code (`sdl/main.c:2438-2470`):
  hard pixels to the whole multiple below, then linear filtering for
  the last fraction. It is *unreachable*: the Scaling row left F12 on
  2026-10-06 and `settings_load` forces `SMOOTH_INTEGER`
  (`core/ui/settings.c:248`).
- **Scanlines are gone**, removed 2026-09-14 at Doc's word ("a nice idea
  that has limited only nostalgic use", `BUILD-LOG.md`). There are no
  shaders of any kind: SDL's 2D renderer, one texture, nearest or linear.

So Doc's "the existing smoothing algorithm and scanline shaders are
there" is half true: the smoothing is in the source, dormant; the
scanlines would have to be written again (see 4.4).

### 1.5 What is sized for 1440x1080 and will have to change

Every one of these is a constant or a byte today, and an IDR larger
than 1440 wide or 1080 tall, or a text grid larger than today's, runs
into it:

- VICKY's line buffers, `owner`, `layer_hit`, `lowres_tmp` and the
  `hd_*` arrays: `VICKY_WIDTH` (`core/vicky.c:17-19`, `:49-52`).
- The frontend's frame buffer `fb[VICKY_WIDTH * VICKY_HEIGHT]`
  (`sdl/main.c:709`), the streaming texture made at that size
  (`:1271`), the still-frame test that compares and copies all of `fb`
  whatever the glass (`:2103`).
- JIM's picture plane: 1440x1080 bytes at `$0F000000`
  (`core/jimgfx.h:35`).
- The text grid's registers: VICKY's TCOLS/TROWS (`$D0B3/$D0B4`) and
  JIM's COLS/ROWS/STRIDE (`$DA05-$DA0D`, `core/term.c:17`) are bytes, so
  255 columns at most.
- The console's map: `SCREEN` at `$030000`, the bands' `BANDMAP` at
  `$03C000` (`rom/kernal.c:54`, `:107`): 48 KB, **12,288 text32 cells**.
  That is why 1440x1080 in 8x8 (180x135, 24,300 cells) is not a MODE.
  BANDMAP's 12 KB holds the ten band rows at up to 307 columns
  (`core/vicky.h:191`).
- The ROM's per-MODE tables (`rom/kernal.c:2012-2016`), `modename`
  (`:899`), the error text in `cmd_mode` (`:907`), `$022F`'s three mode
  bits, the frontend's `ctrl_of` (`sdl/main.c:1782`) and the settings'
  `vmode_names`/`vmode_number` (`core/ui/settings.c:13-15`).
- Programs that know the sizes: LOGO (`demo/logo.c`), the Pascal graph
  unit (asks for MODE 0 in an HD mode), `test/capture`, `test/headless`.

---

## 2. The IDR algorithm

`core/idr.c` is this section in 40 lines; `test/idrtest` checks it.

### 2.1 The panel

The panel's native size is the renderer's **output size once the window
is full screen** -- `SDL_GetRendererOutputSize`, the same call the frame
loop already makes. Not `SDL_GetDesktopDisplayMode`: under a desktop
with fractional scaling that reports logical pixels, and the K4510x on
KMSDRM has no desktop at all. The frontend reads it once the window is
full screen, before the machine's first frame, and again when the
output size changes (a monitor plugged in). `K4510_PANEL=WxH` stands in
for it, for the tests and for screenshots of panels nobody here owns,
in the style of `K4510_WINDOW`.

In a window (a desktop session that is not full screen; not reachable
today, but the code keeps it) the "panel" is the window's size.

### 2.2 The canvas

```
4:3 base     panel wider than 4:3:  ch = ph,  cw = floor(ph * 4 / 3)
             otherwise:             cw = pw,  ch = floor(pw * 3 / 4)
full base    cw = pw, ch = ph
```

The 4:3 rule floors the side it computes, so it is never wider or
taller than the panel. 1280x800 gives 1066x800, 0.03% off 4:3, which no
eye can see (the pixels are the panel's, and square); forcing exact 4:3
(1064x798) would throw away panel lines for nothing. A portrait panel
works the same way: 1024x1280 gives 1024x768.

### 2.3 The IDRs

```
IDR(n) = floor(cw / n) x floor(ch / n),   n = 1, 2, 3, ...
shown at n x n panel pixels per machine pixel
```

- **Rounding.** Floors, always. What a floor drops -- fewer than n
  panel pixels on each axis -- joins the border, split as evenly as
  it goes, as the letterbox is today. 1366x768: canvas 1024x768, IDRs
  1024x768, 512x384, 341x256 (341 x 3 = 1023: one column of border).
  3840x2160: canvas 2880x2160, IDRs 2880x2160, 1440x1080, 960x720,
  720x540 ...
- **Which n.** Every whole n, not only powers of two (question 1). /3
  is where the good ones are: 480x360 on a 1080 panel, 640x480 exactly
  on a 1440 panel, 960x720 on 4K.
- **The limits** cut the list at both ends: an IDR is offered if it is
  at least 320x200 (40x25 in 8x8 cells: question 2) and VICKY draws no
  more than 2,073,600 pixels for it (1920x1080's count: section 3),
  and no wider than 1920 or taller than 1200 (the line buffers, when
  they grow). An IDR over the cap is skipped and the next scale tried,
  so a 4K panel's list starts at /2.
- **Order.** Largest first. The list is what F12's Resolution row
  offers and what K/OS's MODE names (5.3).
- **Odd sizes** (1066x800, 533x400, 341x256, 683x384) are kept as they
  are: a game wants every pixel, and the text grid floors anyway. VICKY
  then needs horizontal padding for text32 -- the vertical kind it has
  (`core/vicky.c:244-250`) turned sideways -- and the ROM an `hpad`
  beside `vpad` (question 7).

### 2.4 Text in an IDR

The grid for cells of `a x b` *machine* pixels in an IDR of `w x h` is
`floor(w / a) x floor(h / b)`, the spare pixels split round it.

Doc's four choices map onto two grids per IDR:

| Choice | Glyph | Grid | Drawn |
|---|---|---|---|
| low-res, normal lines | 8x16 | w/8 x h/16 | at the IDR |
| low-res, double lines | 8x8 | w/8 x h/8 | at the IDR |
| HD, normal lines | 16x32 in an 8x16 cell | w/8 x h/16 | VICKY at 2x the IDR |
| HD, double lines | 16x16 in an 8x8 cell | w/8 x h/8 | VICKY at 2x the IDR |

HD text is today's 720x540 trick generalised: the frame drawn at twice
the IDR, glyphs at the doubled size, everything else doubled. It needs
an **even scale** (the glyph's pixel is n/2 panel pixels, whole) and a
doubled frame inside the cap. So it exists at /2, /4, /6 ... and not at
/1 or /3. At /1 there is nothing finer than the IDR to draw on; there
the 16-wide cells are ordinary machine cells, as MODE 5 is today
(16x32: w/16 x h/32; 16x16: w/16 x h/16). The tables below give all
four cell sizes in machine pixels; read the 16x16 and 16x32 columns as
"MODE 5's cells", and the HD column as "the 8x8 and 8x16 grids may be
drawn with HD glyphs".

One equivalence worth knowing: HD 16x32 at IDR /2 and native 16x32 at
IDR /1 put the same glyphs on the same panel pixels in the same grid
(720x540 HD and 1440x1080 MODE 5 are both 90x33 of 16x32). They differ
in the graphics beside the text, and in cost (3.1).

### 2.5 The tables

Generated by `test/idrtest -t`; every IDR is listed, the ones over the
cap ~~struck~~ (not offered). Grids marked `*` exceed the console map's
12,288 cells and `!` the 255-column limit (1.5): not offered for K/OS
until those move (question 8). The HD column is "yes" where HD text is
possible, struck where the scale is even but the doubled frame is over
the cap.

**4:3 base**

| Panel | Canvas | IDR | x | 8x8 | 8x16 | 16x16 | 16x32 | HD |
|---|---|---|---|---|---|---|---|---|
| 1024x768 | 1024x768 | 1024x768 | 1 | 128x96 | 128x48 | 64x48 | 64x24 |  |
| | | 512x384 | 2 | 64x48 | 64x24 | 32x24 | 32x12 | yes |
| | | 341x256 | 3 | 42x32 | 42x16 | 21x16 | 21x8 |  |
| 1280x800 | 1066x800 | 1066x800 | 1 | 133x100* | 133x50 | 66x50 | 66x25 |  |
| | | 533x400 | 2 | 66x50 | 66x25 | 33x25 | 33x12 | yes |
| | | 355x266 | 3 | 44x33 | 44x16 | 22x16 | 22x8 |  |
| 1366x768 | 1024x768 | 1024x768 | 1 | 128x96 | 128x48 | 64x48 | 64x24 |  |
| | | 512x384 | 2 | 64x48 | 64x24 | 32x24 | 32x12 | yes |
| | | 341x256 | 3 | 42x32 | 42x16 | 21x16 | 21x8 |  |
| 1600x900 | 1200x900 | 1200x900 | 1 | 150x112* | 150x56 | 75x56 | 75x28 |  |
| | | 600x450 | 2 | 75x56 | 75x28 | 37x28 | 37x14 | yes |
| | | 400x300 | 3 | 50x37 | 50x18 | 25x18 | 25x9 |  |
| 1920x1080 | 1440x1080 | 1440x1080 | 1 | 180x135* | 180x67 | 90x67 | 90x33 |  |
| | | 720x540 | 2 | 90x67 | 90x33 | 45x33 | 45x16 | yes |
| | | 480x360 | 3 | 60x45 | 60x22 | 30x22 | 30x11 |  |
| | | 360x270 | 4 | 45x33 | 45x16 | 22x16 | 22x8 | yes |
| 1920x1200 | 1600x1200 | 1600x1200 | 1 | 200x150* | 200x75* | 100x75 | 100x37 |  |
| | | 800x600 | 2 | 100x75 | 100x37 | 50x37 | 50x18 | yes |
| | | 533x400 | 3 | 66x50 | 66x25 | 33x25 | 33x12 |  |
| | | 400x300 | 4 | 50x37 | 50x18 | 25x18 | 25x9 | yes |
| | | 320x240 | 5 | 40x30 | 40x15 | 20x15 | 20x7 |  |
| 2560x1440 | 1920x1440 | ~~1920x1440~~ | 1 | 240x180* | 240x90* | 120x90 | 120x45 |  |
| | | 960x720 | 2 | 120x90 | 120x45 | 60x45 | 60x22 | ~~yes~~ |
| | | 640x480 | 3 | 80x60 | 80x30 | 40x30 | 40x15 |  |
| | | 480x360 | 4 | 60x45 | 60x22 | 30x22 | 30x11 | yes |
| | | 384x288 | 5 | 48x36 | 48x18 | 24x18 | 24x9 |  |
| | | 320x240 | 6 | 40x30 | 40x15 | 20x15 | 20x7 | yes |
| 2560x1600 | 2133x1600 | ~~2133x1600~~ | 1 | 266x200! | 266x100! | 133x100* | 133x50 |  |
| | | 1066x800 | 2 | 133x100* | 133x50 | 66x50 | 66x25 | ~~yes~~ |
| | | 711x533 | 3 | 88x66 | 88x33 | 44x33 | 44x16 |  |
| | | 533x400 | 4 | 66x50 | 66x25 | 33x25 | 33x12 | yes |
| | | 426x320 | 5 | 53x40 | 53x20 | 26x20 | 26x10 |  |
| | | 355x266 | 6 | 44x33 | 44x16 | 22x16 | 22x8 | yes |
| 2880x1800 | 2400x1800 | ~~2400x1800~~ | 1 | 300x225! | 300x112! | 150x112* | 150x56 |  |
| | | 1200x900 | 2 | 150x112* | 150x56 | 75x56 | 75x28 | ~~yes~~ |
| | | 800x600 | 3 | 100x75 | 100x37 | 50x37 | 50x18 |  |
| | | 600x450 | 4 | 75x56 | 75x28 | 37x28 | 37x14 | yes |
| | | 480x360 | 5 | 60x45 | 60x22 | 30x22 | 30x11 |  |
| | | 400x300 | 6 | 50x37 | 50x18 | 25x18 | 25x9 | yes |
| | | 342x257 | 7 | 42x32 | 42x16 | 21x16 | 21x8 |  |
| 3840x2160 | 2880x2160 | ~~2880x2160~~ | 1 | 360x270! | 360x135! | 180x135* | 180x67 |  |
| | | 1440x1080 | 2 | 180x135* | 180x67 | 90x67 | 90x33 | ~~yes~~ |
| | | 960x720 | 3 | 120x90 | 120x45 | 60x45 | 60x22 |  |
| | | 720x540 | 4 | 90x67 | 90x33 | 45x33 | 45x16 | yes |
| | | 576x432 | 5 | 72x54 | 72x27 | 36x27 | 36x13 |  |
| | | 480x360 | 6 | 60x45 | 60x22 | 30x22 | 30x11 | yes |
| | | 411x308 | 7 | 51x38 | 51x19 | 25x19 | 25x9 |  |
| | | 360x270 | 8 | 45x33 | 45x16 | 22x16 | 22x8 | yes |
| | | 320x240 | 9 | 40x30 | 40x15 | 20x15 | 20x7 |  |

**Full-panel base**

| Panel | Canvas | IDR | x | 8x8 | 8x16 | 16x16 | 16x32 | HD |
|---|---|---|---|---|---|---|---|---|
| 1024x768 | 1024x768 | 1024x768 | 1 | 128x96 | 128x48 | 64x48 | 64x24 |  |
| | | 512x384 | 2 | 64x48 | 64x24 | 32x24 | 32x12 | yes |
| | | 341x256 | 3 | 42x32 | 42x16 | 21x16 | 21x8 |  |
| 1280x800 | 1280x800 | 1280x800 | 1 | 160x100* | 160x50 | 80x50 | 80x25 |  |
| | | 640x400 | 2 | 80x50 | 80x25 | 40x25 | 40x12 | yes |
| | | 426x266 | 3 | 53x33 | 53x16 | 26x16 | 26x8 |  |
| | | 320x200 | 4 | 40x25 | 40x12 | 20x12 | 20x6 | yes |
| 1366x768 | 1366x768 | 1366x768 | 1 | 170x96* | 170x48 | 85x48 | 85x24 |  |
| | | 683x384 | 2 | 85x48 | 85x24 | 42x24 | 42x12 | yes |
| | | 455x256 | 3 | 56x32 | 56x16 | 28x16 | 28x8 |  |
| 1600x900 | 1600x900 | 1600x900 | 1 | 200x112* | 200x56 | 100x56 | 100x28 |  |
| | | 800x450 | 2 | 100x56 | 100x28 | 50x28 | 50x14 | yes |
| | | 533x300 | 3 | 66x37 | 66x18 | 33x18 | 33x9 |  |
| | | 400x225 | 4 | 50x28 | 50x14 | 25x14 | 25x7 | yes |
| 1920x1080 | 1920x1080 | 1920x1080 | 1 | 240x135* | 240x67* | 120x67 | 120x33 |  |
| | | 960x540 | 2 | 120x67 | 120x33 | 60x33 | 60x16 | yes |
| | | 640x360 | 3 | 80x45 | 80x22 | 40x22 | 40x11 |  |
| | | 480x270 | 4 | 60x33 | 60x16 | 30x16 | 30x8 | yes |
| | | 384x216 | 5 | 48x27 | 48x13 | 24x13 | 24x6 |  |
| 1920x1200 | 1920x1200 | ~~1920x1200~~ | 1 | 240x150* | 240x75* | 120x75 | 120x37 |  |
| | | 960x600 | 2 | 120x75 | 120x37 | 60x37 | 60x18 | ~~yes~~ |
| | | 640x400 | 3 | 80x50 | 80x25 | 40x25 | 40x12 |  |
| | | 480x300 | 4 | 60x37 | 60x18 | 30x18 | 30x9 | yes |
| | | 384x240 | 5 | 48x30 | 48x15 | 24x15 | 24x7 |  |
| | | 320x200 | 6 | 40x25 | 40x12 | 20x12 | 20x6 | yes |
| 2560x1440 | 2560x1440 | ~~2560x1440~~ | 1 | 320x180! | 320x90! | 160x90* | 160x45 |  |
| | | 1280x720 | 2 | 160x90* | 160x45 | 80x45 | 80x22 | ~~yes~~ |
| | | 853x480 | 3 | 106x60 | 106x30 | 53x30 | 53x15 |  |
| | | 640x360 | 4 | 80x45 | 80x22 | 40x22 | 40x11 | yes |
| | | 512x288 | 5 | 64x36 | 64x18 | 32x18 | 32x9 |  |
| | | 426x240 | 6 | 53x30 | 53x15 | 26x15 | 26x7 | yes |
| | | 365x205 | 7 | 45x25 | 45x12 | 22x12 | 22x6 |  |
| 2560x1600 | 2560x1600 | ~~2560x1600~~ | 1 | 320x200! | 320x100! | 160x100* | 160x50 |  |
| | | 1280x800 | 2 | 160x100* | 160x50 | 80x50 | 80x25 | ~~yes~~ |
| | | 853x533 | 3 | 106x66 | 106x33 | 53x33 | 53x16 |  |
| | | 640x400 | 4 | 80x50 | 80x25 | 40x25 | 40x12 | yes |
| | | 512x320 | 5 | 64x40 | 64x20 | 32x20 | 32x10 |  |
| | | 426x266 | 6 | 53x33 | 53x16 | 26x16 | 26x8 | yes |
| | | 365x228 | 7 | 45x28 | 45x14 | 22x14 | 22x7 |  |
| | | 320x200 | 8 | 40x25 | 40x12 | 20x12 | 20x6 | yes |
| 2880x1800 | 2880x1800 | ~~2880x1800~~ | 1 | 360x225! | 360x112! | 180x112* | 180x56 |  |
| | | 1440x900 | 2 | 180x112* | 180x56 | 90x56 | 90x28 | ~~yes~~ |
| | | 960x600 | 3 | 120x75 | 120x37 | 60x37 | 60x18 |  |
| | | 720x450 | 4 | 90x56 | 90x28 | 45x28 | 45x14 | yes |
| | | 576x360 | 5 | 72x45 | 72x22 | 36x22 | 36x11 |  |
| | | 480x300 | 6 | 60x37 | 60x18 | 30x18 | 30x9 | yes |
| | | 411x257 | 7 | 51x32 | 51x16 | 25x16 | 25x8 |  |
| | | 360x225 | 8 | 45x28 | 45x14 | 22x14 | 22x7 | yes |
| | | 320x200 | 9 | 40x25 | 40x12 | 20x12 | 20x6 |  |
| 3840x2160 | 3840x2160 | ~~3840x2160~~ | 1 | 480x270! | 480x135! | 240x135* | 240x67* |  |
| | | 1920x1080 | 2 | 240x135* | 240x67* | 120x67 | 120x33 | ~~yes~~ |
| | | 1280x720 | 3 | 160x90* | 160x45 | 80x45 | 80x22 |  |
| | | 960x540 | 4 | 120x67 | 120x33 | 60x33 | 60x16 | yes |
| | | 768x432 | 5 | 96x54 | 96x27 | 48x27 | 48x13 |  |
| | | 640x360 | 6 | 80x45 | 80x22 | 40x22 | 40x11 | yes |
| | | 548x308 | 7 | 68x38 | 68x19 | 34x19 | 34x9 |  |
| | | 480x270 | 8 | 60x33 | 60x16 | 30x16 | 30x8 | yes |
| | | 426x240 | 9 | 53x30 | 53x15 | 26x15 | 26x7 |  |
| | | 384x216 | 10 | 48x27 | 48x13 | 24x13 | 24x6 | yes |

---

## 3. The performance budget

### 3.1 Measured

`test/vidbench` sets VICKY's registers by hand (no CPU, no ROM), fills
the screen with the worst text there is -- every cell a random glyph in
random colours -- or a full-screen 8 bpp bitmap, or both plus 64
sprites of 32x32 at 8 bpp, and draws every frame whole (an idle or
still frame costs nothing since 2026-10-06). Then the frontend's
per-pixel work on a changed frame as `sdl/main.c` does it: the copy and
compare of `fb`, and the palette lookup into ARGB. The GPU's share
(texture upload, scaling, present) is not measured: it is the host's,
and it is small beside these on any GPU (5.9 MB a frame at 1440x1080 is
356 MB/s).

Host: ubuntu-s1, Intel i7-6700 (Skylake, 3.4 GHz, 4.0 turbo), `-O2`,
pinned to one core (`taskset -c 5`), 300 frames a case. ubuntu-s1 is a
shared server: three pinned runs agreed within 4%, and two unpinned
runs earlier in the day, with less else running, were 10-25% faster on
the large glasses (1440x1080 busy 5.2-5.3 ms). The figures below are
the slower, pinned set. Times in ms a frame; `ns/px` is VICKY's time
over the pixels it wrote; `cmp+cp` the still-frame copy and compare of
`fb` (all 1440x1080 of it, whatever the glass: 1.5);  `pal` the
palette lookup:

```
glass             picture               out px vicky ms     ns/px cmp+cp ms   pal ms   tex MB
640x480           text                  307200     0.98      3.19      0.08     0.13     1.17
640x480           bitmap                307200     0.38      1.23      0.08     0.13     1.17
640x480           bitmap+text+64spr     307200     1.67      5.43      0.08     0.13     1.17
360x270           text                   97200     0.37      3.79      0.09     0.05     0.37
360x270           bitmap                 97200     0.12      1.21      0.08     0.04     0.37
360x270           bitmap+text+64spr      97200     0.61      6.29      0.08     0.04     0.37
720x540           text                  388800     1.28      3.28      0.09     0.17     1.48
720x540           bitmap                388800     0.49      1.25      0.09     0.18     1.48
720x540           bitmap+text+64spr     388800     2.09      5.37      0.09     0.18     1.48
720x540 HD text   text                 1555200     4.78      3.08      0.11     0.70     5.93
720x540 HD text   bitmap               1555200     1.12      0.72      0.10     0.66     5.93
720x540 HD text   bitmap+text+64spr    1555200     5.28      3.39      0.11     0.68     5.93
1440x1080         text                 1555200     4.23      2.72      0.11     0.68     5.93
1440x1080         bitmap               1555200     1.90      1.22      0.12     0.70     5.93
1440x1080         bitmap+text+64spr    1555200     6.44      4.14      0.11     0.69     5.93

frontend passes alone (8-bit fb -> ARGB, compare+copy), ms a frame:
  1440x1080   1555200 px   cmp+cp   0.10   palette   0.69   texture   5.9 MB
  1600x1200   1920000 px   cmp+cp   0.14   palette   0.84   texture   7.3 MB
  1920x1080   2073600 px   cmp+cp   0.16   palette   0.93   texture   7.9 MB
  1920x1440   2764800 px   cmp+cp   0.28   palette   1.37   texture  10.5 MB
  2160x1620   3499200 px   cmp+cp   0.40   palette   1.71   texture  13.3 MB
  2400x1800   4320000 px   cmp+cp   0.71   palette   2.19   texture  16.5 MB
  2880x2160   6220800 px   cmp+cp   1.04   palette   3.18   texture  23.7 MB
  3840x2160   8294400 px   cmp+cp   1.45   palette   4.17   texture  31.6 MB
```

And the CPU, from `test/bench` with EhBASIC in `10 X=X+1:GOTO 10`, on
the same host, pinned: **4.05 ms a frame at 40.5 MHz, 8.37 ms at 81
MHz** (VICKY's 640x480 share of that run, 0.9 ms, is in the table
above, not added twice).

What the numbers say:

- **VICKY's cost goes with the pixels it draws**: about 1.2 ns a pixel
  for a bitmap, 2.7-3.3 for text, 4.1-6.3 for the busy picture (the
  small glasses pay more a pixel: per-line overheads). The IDR sets the
  cost, not the panel.
- **HD text costs about what native 16-wide cells cost**: 4.8 ms for
  720x540 HD against 4.2 ms for 1440x1080 in 16x32 cells -- the same
  glyphs on the same panel pixels (2.4). (In the faster unpinned runs
  the gap was wider, 5.1 against 3.4; the per-pixel stock-glyph test is
  the suspect. Question 11.)
- **Graphics at an IDR, doubled, are cheap**: the 720x540 bitmap drawn
  at 1440x1080 by the HD path costs 1.1 ms, against 1.9 for a 1440x1080
  bitmap. So the busy picture is *cheaper* in 720x540 HD (5.3 ms) than
  at 1440x1080 (6.4 ms).
- The frontend's two passes are about 0.5 ns a pixel together.

### 3.2 Extrapolated to bigger IDRs

The frontend's passes were measured at each size (the second table
above). VICKY cannot draw past 1440x1080 yet, so its share is the busy
picture's measured 1440x1080 rate, 4.14 ns a pixel, times the pixels --
a fair extrapolation, since the cost is per pixel and the large glasses
pay the least overhead a pixel. The worst case throughout: every pixel
of a bitmap, a text layer and 64 sprites redrawn every frame, with the
CPU flat out at 40.5 MHz.

| IDR | Pixels | VICKY busy | Frontend | Video | + CPU 40.5 MHz | of 16.7 ms |
|---|---|---|---|---|---|---|
| 1440x1080 | 1.56 M | 6.4 | 0.8 | 7.2 | 11.3 | 68% |
| 1600x1200 | 1.92 M | 8.0 | 1.0 | 8.9 | 13.0 | 78% |
| 1920x1080 | 2.07 M | 8.6 | 1.1 | 9.7 | 13.7 | 82% |
| 1920x1440 | 2.76 M | 11.4 | 1.6 | 13.1 | 17.1 | 103% |
| 2160x1620 | 3.50 M | 14.5 | 2.1 | 16.6 | 20.7 | 124% |
| 2400x1800 | 4.32 M | 17.9 | 2.9 | 20.8 | 24.8 | 149% |
| 2880x2160 | 6.22 M | 25.8 | 4.2 | 30.0 | 34.0 | 204% |
| 3840x2160 | 8.29 M | 34.3 | 5.6 | 40.0 | 44.0 | 264% |

(ms a frame. A text screen redrawn whole is about two thirds of the busy
figure, and an idle or still frame is nothing.)

Memory is not the constraint: at the cap `fb` is 2 MB (twice, with the
still-frame copy) and the texture 7.9 MB; in the machine a full-screen
8 bpp bitmap is 2 MB of 256. The CPU writing pixels one by one is a
constraint for programs -- not measured here, but at a handful of
cycles a byte a 45GS02 at 40.5 MHz writes a few MB a second, so no IDR
much over 640x480 is redrawn by the CPU every frame -- but that is what
the blitter (instant) and sprites are for, and it is the program's
choice of IDR, not a reason to cap the panel.

### 3.3 The cap

**Proposed: VICKY draws at most 2,073,600 pixels a frame** -- a 1080p
panel's count -- and at most 1920x1200 on either axis (the line buffers
grow to 1920). On this host that is 9.7 ms of video for the worst
picture, 13.7 ms with the CPU flat out: 82% of a frame. 1920x1440 is
the first size that does not fit at all.

That is a tight cap, not a comfortable one, and it was measured on a
2016 desktop core, not on the K4510x's laptops. There is a sign the
Dell is a good deal slower: `docs/CPU-CLOCK-POLICY.md` has a 2016 i7
fitting about 125 MHz in a frame, and the Dell's measured clock is
40.5-60 MHz. Two things still make the cap reasonable: the worst case
is rare (every layer redrawn every frame; K/OS at the prompt draws
nothing, and a text screen scrolling costs two thirds of it), and the
clock governor (`core/governor.h`) already steps the CPU's clock down
when a frame runs long. The safe alternative is
today's 1440x1080 count (1,555,200), which the Dell already draws in
MODE 5 and HD text; it costs 1920x1200 its 1600x1200 and the full base
its 1920x1080. `test/vidbench` on the Dell decides (question 3).

The cap applies to **what VICKY draws**, so an HD-text IDR counts four
times its pixels (2.4): 720x540 HD is 1.56 M, 960x540 HD 2.07 M (at the
cap), 1440x1080 HD 6.2 M (out).

What the cap leaves on each panel, top of the list:

| Panel | 4:3 top IDR | Full top IDR |
|---|---|---|
| 1366x768 | 1024x768 /1 | 1366x768 /1 |
| 1920x1080 | 1440x1080 /1 | 1920x1080 /1 |
| 1920x1200 | 1600x1200 /1 | 960x600 /2 (1920x1200 is 2.3 M) |
| 2560x1440 | 960x720 /2 (1920x1440 is 2.8 M) | 1280x720 /2 |
| 2560x1600 | 1066x800 /2 | 1280x800 /2 |
| 3840x2160 | 1440x1080 /2 | 1920x1080 /2 |

### 3.4 A 4K panel

On 3840x2160 with the 4:3 base, the IDRs offered are **1440x1080 /2,
960x720 /3, 720x540 /4, 576x432 /5, 480x360 /6, 411x308 /7, 360x270 /8,
320x240 /9**. The canvas itself, 2880x2160, would take 30 ms of video a
frame on the busy picture here, twice the frame, and is not offered.
HD text is offered at /4 (720x540, drawn 1440x1080) and below; not at
/2, where it would draw 2880x2160. So a 4K panel shows exactly what a
1080 panel shows, each machine pixel twice as many panel pixels
across, plus 960x720 and 576x432. The refresh rate is not the problem: the GPU scales a
1440x1080 texture to 2880x2160 for nothing, and a 4K panel at 60 Hz is
fed the same 60 frames.

---

## 4. Software resolutions

### 4.1 The recommendation: yes, as Doc leans

A program may ask for any resolution inside the limits; the machine
shows it as well as the panel allows and the program lives with the
result. It is the honest version of what MODE 0-4 already are: on a
1080 panel, 640x480 is not an integer division of anything, and it is
shown at 2x in a 1280x960 box with a border today.

K/OS and JIM never run in a software resolution. A program that sets
one gets the IDR back when it exits through VIDEO (`$FF92`), as a
program that changes CTRL does today.

### 4.2 Limits

- **Width 160-1920, height 100-1200, and the same pixel cap as the
  IDRs** (VICKY's cost does not care whether a size divides the panel).
- **No larger than the panel**: nothing is shown below 1x, so on a
  1366x768 panel a 1440x1080 request is clamped (SWW/SWH read back what
  was granted).
- **Any aspect.** A 16:9 game on a 4:3 canvas gets bars; the program
  chose.
- **Pixel aspect: square, unless the program asks for stretch** (4.3).

### 4.3 Presentation

The program picks one, in the register sketch's `GLASSCTL`:

1. **Integer** (the default): the largest whole scale that fits the
   canvas, centred, the border colour round it -- exactly how MODE 0
   shows on 1080 lines today.
2. **Fit**: fill the canvas on the tighter axis by sharp-bilinear --
   whole multiple first, then smoothing for the last fraction. The
   code exists (`sdl/main.c:2438-2470`); it needs to be reachable per
   program rather than per user.
3. **Stretch to 4:3**: the glass fills the 4:3 canvas whatever its
   shape, for non-square-pixel modes in the 320x200 tradition. Sharp-
   bilinear, as Fit.

The F12 menu is drawn over all three in whole pixels, as it is now.

### 4.4 Shaders

There are none to offer today. If a game wants scanlines, they are best
done in the frontend as a per-program option drawn at the panel's
resolution (one dark line every n panel lines, n the scale), not in
VICKY and not for K/OS. Recommendation: not now; the register sketch
keeps a bit for it so a program can ask once it exists (question 10).
**Built 2026-10-08** that way: GLASSCTL bit6, MODE -c (Doc: "Please build the
scanline options").

### 4.5 The register sketch

VICKY `$D0C0-$D0DF`, free today. Reads are what is in force; writes take
effect at the next frame (latched where `glass_latch` is now).

```
$C0,C1  PANELW   R  the panel's pixels (the window's, in a window)
$C2,C3  PANELH   R
$C4,C5  CANVW    R  the canvas: the panel, or its largest 4:3
$C6,C7  CANVH    R
$C8     IDRN     R  how many IDRs this panel offers (1..24)
$C9     IDRIX    RW which IDR $CA-$CF describe (0 = the largest)
$CA     IDRS     R  its scale: panel pixels a machine pixel
$CB     IDRF     R  bit0 HD text possible; bit1 8x8 grid fits K/OS's
                    limits; bit2 8x16 likewise
$CC,CD  IDRW     R  its width
$CE,CF  IDRH     R  its height
$D0     GLASSCTL RW bits0-1 the glass: 0 CTRL's modes (as today),
                    1 the IDR IDRSEL names, 2 software (SWW x SWH)
                    bits4-5 software presentation: 0 integer, 1 fit,
                    2 stretch to 4:3;  bit6 scanlines (reserved)
$D1     IDRSEL   RW the IDR shown when GLASSCTL says 1
$D2,D3  SWW      RW a software resolution's width  } clamped to the
$D4,D5  SWH      RW and height                      } limits; read back
$D6,D7  GLASSW   R  the glass in force (vicky_glass_w)
$D8,D9  GLASSH   R
$DA     SCALE    R  its whole scale on the panel, 0 when fitted
```

CTRL bit5 keeps working: with GLASSCTL 0, CTRL's HD bits mean IDR scale
1, 2 or 4 of *this* panel's canvas (what MODE 5, 6, 7 mean in 5.3), so
every program written since 2026-09-14 runs unchanged. A program that
wants an exact size reads the list; one that wants "the biggest thing
that fits" writes IDRSEL 0.

---

## 5. Boot, MODE and the config

### 5.1 The boot sequence

1. The frontend reads k4510.cfg, creates the window and makes it full
   screen (as now).
2. It reads the output size: the panel (2.1); or `K4510_PANEL`; or, if
   `video.display` is set to a size, that.
3. It takes the canvas from `video.base` and the IDR list from
   `core/idr.c`, publishes them in `$D0C0-$D0CF`, and sizes `fb`, the
   texture and VICKY's buffers for the largest IDR offered.
4. It resolves `video.scale` to an IDR: that scale if offered; else the
   nearest larger scale offered (a smaller picture rather than none:
   /1 on a 4K panel becomes /2); else the largest scale offered (/4 on
   1366x768, under 320x200, becomes /3). It publishes it with the
   cells (`video.cells`) through `$D521`/`$D53C`, as the mode is
   published now.
5. The ROM's `video_init` reads the IDR's size and lays out the text:
   grid, `vpad` and `hpad`, bands. MODE's tables stop being tables.
6. When the output size changes later, steps 2-5 run again and K/OS
   gets a mode request, as F12 sends today.

### 5.2 Config keys

In k4510.cfg's style (`section.name`, enum names in the file):

| Key | Values | Default | Note |
|---|---|---|---|
| `video.base` | `4:3`, `full` | `4:3` | F12 > Video > Canvas. Live. |
| `video.scale` | `1` .. `24` | `4` | Today's default, 360x270 on a 1080 panel. The IDR by its scale, not its size: a K4510x stick moves between laptops, and "720x540" means nothing on a 1366x768 panel while "/2" does. |
| `video.cells` | `8x16`, `8x8` | `8x16` | The grid; `16x32`/`16x16` at scale 1 (MODE 5's cells). |
| `video.hdfont` | as now | as now | Unchanged: "unscii" is HD off; any other face is HD text wherever the IDR allows it. |
| `video.display` | `auto`, `WxH` | `auto` | Override the panel (a projector that lies in its EDID). |

`video.mode` is read once for migration and not written again:
"1440x1080 16x32" becomes scale 1, cells 16x32; "1440x1080 16x16"
scale 1, 16x16; "720x540 16x32" scale 2, 8x16; "720x540 16x16" scale 2,
8x8; "360x270" scale 4, 8x8; the 640x480 family scale 2, its cells
(question 9). `video.panel` is not reused: it is the old side panel's
key, still read for its migration (`core/ui/settings.c:233-238`).

### 5.3 MODE

- `MODE` alone lists this panel's IDRs with their grids, and says which
  is in force.
- `MODE WxH [rows]` -- `MODE 720x540`, `MODE 480x360 45` -- picks an
  offered IDR by size; anything else is an error that lists the offered
  sizes.
- `MODE 5`, `6`, `7` stay, meaning canvas /1, /2, /4 on any panel (on a
  1080 panel exactly what they mean today); `MODE 5 67`, `MODE 6 67` as
  now. `$022F` keeps its three bits for them.
- `MODE 0-2` stay as the 640x480 family, now software resolutions
  (4.1): the Pascal graph unit and LOGO keep working.

---

## 6. Open questions for Doc

1. **Every whole scale, or powers of two only?** /3 gives 480x360 on a
   1080 panel, 640x480 exactly on a 1440 panel, 960x720 on 4K.
   *Recommendation: every whole scale. MODE 5/6/7 keep meaning /1, /2,
   /4, so nothing written so far moves.*

2. **The smallest IDR K/OS runs in.** 320x200 is 40x25 in 8x8 cells;
   it drops 1366x768's 256x192 (32x24).
   *Recommendation: 320x200. A game can still ask for 256x192 as a
   software resolution.*

3. **The cap: 2,073,600 pixels (a 1080p frame), 1920x1200 at most.**
   *Recommendation: yes, after `make test/vidbench &&
   ./test/vidbench` has been run on the Dell (about 30 s). If its
   1440x1080 busy line is over 8 ms (ubuntu-s1: 6.4), fall back to
   today's 1440x1080 count, 1,555,200.*

4. **A 4K panel tops out at 1440x1080 /2, and HD text only from 720x540
   /4 down.** Lifting that needs HD text drawn by the GPU (a second,
   text-only texture at panel resolution) rather than by VICKY.
   *Recommendation: accept the limit; do the GPU text only if a 4K
   panel becomes a K4510's panel.*

5. **The default base: 4:3 or full?**
   *Recommendation: 4:3. The handbook's pictures, the bands, every
   program so far and the 4:3 identity of the machine assume it; full
   is one F12 row away.*

6. **Save the IDR by scale, not size.**
   *Recommendation: yes (5.2): the stick moves between panels.*

7. **Odd-sized IDRs (1066x800, 533x400, 341x256, 683x384).** Keep the
   exact size and teach VICKY's text32 horizontal padding, or round the
   width down to whole cells?
   *Recommendation: exact size plus horizontal padding. Rounding would
   make the graphics lose pixels to suit the text.*

8. **Grids over today's limits** (more than 255 columns, or more than
   12,288 cells: every 8x8 grid at 1066 wide and up, 8x16 at 1600x1200).
   *Recommendation: not offered to K/OS for now, as 1440x1080 8x8 is
   not offered today. Moving `SCREEN`, `BANDMAP` and widening TCOLS and
   JIM's geometry to 16 bits is a separate piece of work, worth doing
   only if Doc wants 200-column consoles.*

9. **MODE 0-2 (the 640x480 family) and a saved `video.mode = 640x480`.**
   *Recommendation: they stay as software resolutions for programs; a
   config that booted into one boots into scale 2 with the same cells,
   because K/OS always runs in an IDR.*

10. **Scanlines for software resolutions.**
    *Recommendation: not now. If wanted, in the frontend at panel
    resolution and asked for by the program (`GLASSCTL` bit 6), never
    for K/OS.*

11. **HD text's implementation.** Drawing HD text per pixel (today's
    path) cost 13% more than the same glyphs as native 16-wide cells in
    the pinned runs, and 50% more in the faster unpinned ones (3.1).
    *Recommendation: when IDRs are built, move the stock-glyph test
    from per pixel to per cell, and measure both on the Dell; not a
    reason to change the design.*

12. **Changing the base or the scale live.**
    *Recommendation: live, as Resolution is today: the frontend
    resizes, K/OS gets a mode request and repaints with a banner.*
