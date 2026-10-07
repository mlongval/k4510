/* VICKY -- the K4510 video chip.
 *
 * Register block at IO_VICKY ($D000), 256 bytes, byte-addressed. Every
 * pointer is a 28-bit physical address into main RAM; there is no video
 * memory. Rendering is per scanline into an 8-bit indexed framebuffer the
 * frontend supplies; the frontend applies vicky_palette_rgb().
 *
 *   $00  CTRL      bit0 display enable; the rest pick the mode.  Without bit5
 *                  the glass is 640x480 and the raster lines 0..479 -- a
 *                  smaller mode is drawn into it, doubled, and centred.
 *                  bit5 (hd-modes branch, 2026-09-14): the HD family, drawn at
 *                  its own size, the raster lines 0..h-1:
 *                     1|32        1440x1080    1|32|2|4     720x540
 *                     1|32|2|4|16 360x270  (a 1080-line panel shows them 1x, 2x, 4x)
 *                  bit1 columns halved (320), bit2 lines halved (240),
 *                  bit3 a 200-line field (40 blank lines top and bottom),
 *                  bit4 columns quartered (160; with bit1).
 *                     1     640x480      1|4    640x240
 *                     1|2   320x240      1|2|8  320x200
 *                     1|2|8|16  160x200  1|4|8  640x200
 *   $01  BGCOL     background palette index (where nothing is drawn)
 *   $02  RASTER    read: current line (low 8); write: raster-compare low
 *   $03            read: line high bits;         write: compare high
 *   $04  IRQSTAT   bit0 vblank, bit1 raster==compare, bit2 SHEILA IRQ op,
 *                  bit3 sprite collision. Write 1s to acknowledge.
 *   $05  IRQMASK   same bits; IRQ line = IRQSTAT & IRQMASK
 *   $06  PALIDX    palette index for the write port
 *   $07  PALR      $08 PALG   $09 PALB  -- writing B commits the entry and
 *                  increments PALIDX
 *   $0A-$0D SPRTAB 28-bit pointer to the sprite attribute table (128 x 16 B)
 *   $0E     SPRCTL bit0 sprites enable
 *   $0F            reserved
 *   $60-$63 SHEILA 28-bit pointer to SHEILA's list
 *   $64     SHEILACTL bit0 enable; the list restarts every frame at line 0
 *   $70-$73 BLTSRC  28-bit     $74-$77 BLTDST 28-bit
 *   $78,79  BLTW    width px   $7A,7B  BLTH   height px
 *   $7C,7D  BLTSS   source stride (bytes)   $7E,7F BLTDS dest stride
 *   $80     BLTOP   0 copy, 1 keyed copy (src 0 skipped), 2 fill (value =
 *                   BLTSRC byte 0), 3 AND, 4 OR, 5 XOR,
 *                   6 LINE: from (LX0,LY0) to (LX1,LY1), colour = BLTSRC
 *                   byte 0, into the BLTDST surface of stride BLTDS, clipped
 *                   to 0..BLTW-1 x 0..BLTH-1
 *                   7 TRIANGLE: filled (LX0,LY0)-(LX1,LY1)-(LX2,LY2), same
 *                   colour, surface and clip as LINE
 *   $81     BLTFLG  bit0 H-flip, bit1 V-flip
 *   $82     BLTCMD  write anything: go. Instant. Reads 0.
 *   $84,85  LX0   $86,87 LY0   $88,89 LX1   $8A,8B LY1   $8C,8D LX2   $8E,8F LY2  (signed 16-bit)
 *   Blits are 8 bpp (one byte per pixel) in this version.
 *   $90-$9F COLSS  read: sprite-sprite collision bits, one bit per sprite
 *                  (sprite n hit another sprite this frame). All 16 cleared on read of $90.
 *   $A0-$AF COLSL  read: sprite-layer collision bits (sprite n over a
 *                  non-transparent layer pixel). All 16 cleared on read of $A0.
 *   $C0-$DF THE GLASS (2026-10-07, docs/design-video-foundations.md).  The
 *                  host finds the panel and tells VICKY (vicky_set_panel); she
 *                  works out the canvas -- the panel, or its largest 4:3 -- and
 *                  the integer display resolutions (IDRs) it divides into.
 *     $C0,C1 PANELW  R  the panel's pixels         $C2,C3 PANELH R
 *     $C4,C5 CANVW   R  the canvas                 $C6,C7 CANVH  R
 *     $C8  IDRN      R  how many IDRs it offers, largest first
 *     $C9  IDRIX     RW which one $CA-$CF describe (0 = the largest)
 *     $CA  IDRS      R  its scale: panel pixels a machine pixel, each way
 *     $CB  IDRF      R  bit0 HD text possible there
 *     $CC,CD IDRW    R  its width                  $CE,CF IDRH   R
 *     $D0  GLASSCTL  RW what CTRL bit5's glass is.  bits0-1: 0 CTRL's own
 *                    bits (1, 2, 4 -- the canvas /1, /2, /4: MODE 5-7); 1 the
 *                    IDR at scale IDRSEL; 2 software, SWW x SWH.  bits4-5, how
 *                    software shows: 0 the largest whole scale, 1 fit
 *                    (sharp-bilinear: the whole multiple, then smoothing);
 *                    2, stretched to 4:3, is reserved and fits for now.
 *                    bit6 reserved (scanlines).
 *     $D1  IDRSEL    RW the scale wanted: one not offered becomes the next
 *                    larger scale that is, else the largest offered
 *     $D2,D3 SWW     RW a software resolution, 160-1920 wide  } clamped to the
 *     $D4,D5 SWH     RW                        100-1200 high  } limits and panel
 *     $D6,D7 GLASSW  R  the glass CTRL and the above give now  $D8,D9 GLASSH R
 *                    (GLASSCTL 1 or 2 implies CTRL bit5, here and for TXT)
 *     $DA  SCALE     R  its whole scale on the panel (0: not an IDR)
 *     $DB  TXTCELL   W  a text cell, as LCTRL's field: 0 8x8, 1 8x16,
 *                    2 16x16, 3 16x32 (text32)
 *     $DC  TXTCOLS   R  whole cells of it on that glass (255 at most)
 *     $DD  TXTROWS   R
 *     $DE  TXTVPAD   R  spare lines above them: half the spare, rounded down
 *     $DF  TXTHPAD   R  spare pixel columns to their left, likewise
 *                  A text32 layer paints the spare pixels round a whole grid in
 *                  the nearest cell's background; K/OS scrolls layer 0 by
 *                  -TXTHPAD, -TXTVPAD to centre its console.
 *   $B0-$B7 LAYOUT  the status bands and the console between them.  VICKY is
 *                  the one owner of where they are (2026-10-01, Doc: "A then
 *                  B"; docs/notes/status-bars.md).  Writes say what is wanted,
 *                  reads of $B5-$B7 say what is in force:
 *     $B0  BANDTOP  a program's top band, rows (used while BANDCTL bit1)
 *     $B1  BANDBOT  a program's bottom band, rows
 *     $B2  BANDCTL  bit0 the user's bands are on -- the F12 switch; the host
 *                   sets it and a guest write cannot change it
 *                   bit1 the bands are the PROGRAM's: its heights, and K/OS
 *                   draws nothing in them.  It works with bit0 off.  Set it,
 *                   then call VIDEO ($FF92); a program MUST clear it and call
 *                   VIDEO again before it exits (BANDS.PRG is the example).
 *     $B3  TCOLS    the text grid, columns } written by whoever sets the
 *     $B4  TROWS    the text grid, rows    } mode (K/OS's VIDEO); 0 = none
 *     $B5  CONOY    read: the top band in force = the console's first row
 *     $B6  CONROWS  read: the console's rows
 *     $B7  CONBOT   read: the bottom band in force
 *     $B8-$BB BANDMAP  28-bit: the bands' OWN text32 cells (option B,
 *                   2026-10-01): the top band's rows, then the bottom's,
 *                   TCOLS cells a row.  While layer 0 shows CONMAP, its band
 *                   rows are drawn from here and not from the map, so nothing
 *                   that writes the console's map can reach a band.  0 = off.
 *     $BC-$BF CONMAP   28-bit: the console's text32 map, as K/OS declares it.
 *                   A program that points layer 0 at a map of its own gets
 *                   its own rows, bands or no bands.  Writing $BF latches the
 *                   layout the bands are drawn to: K/OS writes it after it
 *                   has laid the console out, so a band switched on in F12
 *                   covers nothing until K/OS has moved the console for it.
 *                  The rules: bands only on a grid of 40x30 or more; the
 *                  user's are one row each; a program's that would leave the
 *                  console under VICKY_BAND_MIN_ROWS, or take more than
 *                  VICKY_BAND_MAX_ROWS between them, fall back to one each.
 *                  JIM's window ($DA05-$DA08) is clamped inside the console.
 *
 *   Sprite attribute entry, 16 bytes, in main RAM:
 *   +0,1 X (signed 16)   +2,3 Y (signed 16)   +4..7 DATA 28-bit pointer
 *   +8   CTRL  bit0 enable, bit1 8 bpp (else 4), bit2 H-flip, bit3 V-flip,
 *              bits4-5 Z: drawn after layer Z (0..3)
 *   +9   SIZE  bits0-1 width 8/16/32/64, bits2-3 height 8/16/32/64
 *   +10  PALOFS (4 bpp: index = PALOFS<<4 | pixel)
 *   +11..15 reserved
 *   Pixel 0 is transparent. No per-line limit. 128 sprites.
 *
 * SHEILA -- the display-list coprocessor (Doc named it, 2026-08-22; the
 * Amiga's copper is the ancestor). 4-byte instructions in main RAM, executed at the start of
 * each scanline until a WAIT blocks. Register writes take effect for the
 * line about to be drawn.
 *   00 END                       stop until next frame
 *   01 WAIT lo hi                wait for line >= (hi<<8|lo)
 *   02 MOVE reg val              write val to VICKY register reg
 *   03 SKIP lo hi                skip next instruction if line >= value
 *   04 JUMP a0 a1 a2             continue at 24-bit address
 *   05 IRQ                       set IRQSTAT bit2
 *   At most 256 instructions per line are executed (runaway guard).
 *
 *   Layers 0..3 at $10 + n*$10, 16 bytes each:
 *   +0   LCTRL     bit0 enable, bits1-2 mode (0 bitmap, 1 tile, 2 text8,
 *                  3 text32), bits3-4 bpp (0=1, 1=2, 2=4, 3=8),
 *                  bits5-6 cell size (tile: 8/16/32/64 px square;
 *                  text: 0 = 8x8, 1 = 8x16)
 *   +1   LPALOFS   palette offset for <8 bpp: index = (value << depth) | pixel
 *   +2,3 SCROLLX   16-bit, pixels
 *   +4,5 SCROLLY   16-bit, pixels
 *   +6,7 STRIDE    bitmap: bytes per row. tile/text: map entries per row.
 *   +8..+B DATA    28-bit pointer: pixels (bitmap) or glyph/tile set
 *   +C..+F MAP     28-bit pointer: the map
 *
 * Map formats:
 *   tile    2 bytes/cell: bits 0-9 tile index, 10 H-flip, 11 V-flip,
 *           12-15 palette offset (used for <8 bpp). Tile pixel data at
 *           DATA + index * (size*size*bpp/8), rows MSB-first packed.
 *   text8   1 byte/cell: glyph index. 1 bpp 8xH glyphs at DATA + g*H.
 *           Colours from LPALOFS: index = (LPALOFS<<1) | pixel.
 *   text32  4 bytes/cell: glyph lo, glyph hi (16-bit index), fg, bg --
 *           byte-wide palette indices per cell. bit7 of glyph hi = reverse.
 *
 * Layer 0 is bottom. Pixel index 0 is transparent in every layer; BGCOL is
 * the ground (text32 bg is never transparent). Changed 2026-08-22 from
 * "opaque in the lowest layer" so SHEILA backgrounds show under text.
 */
#ifndef K4510_VICKY_H
#define K4510_VICKY_H
#include <stdint.h>

#define VICKY_WIDTH   1920       /* the largest glass the frame buffer holds (an IDR, a software resolution, */
#define VICKY_HEIGHT  1200       /* or HD text drawn at twice one); the classic modes use 640x480 of it */
#define VICKY_LAYERS  4

/* register offsets */
#define VR_CTRL     0x00
#define VR_BGCOL    0x01
#define VR_RASTER   0x02
#define VR_PALIDX   0x06
#define VR_PALR     0x07
#define VR_PALG     0x08
#define VR_PALB     0x09
#define VR_SPRTAB   0x0A
#define VR_SPRCTL   0x0E
#define VR_COLSS    0x90
#define VR_COLSL    0xA0
#define VR_SHEILA   0x60
#define VR_SHEILACTL   0x64
#define VR_IRQSTAT  0x04
#define VR_IRQMASK  0x05
#define VI_VBLANK   1
#define VI_RASTER   2
#define VI_SHEILA   4
#define VI_COLL     8
#define VR_BLTSRC   0x70
#define VR_BLTDST   0x74
#define VR_BLTW     0x78
#define VR_BLTH     0x7A
#define VR_BLTSS    0x7C
#define VR_BLTDS    0x7E
#define VR_BLTOP    0x80
#define VR_BLTFLG   0x81
#define VR_BLTCMD   0x82
#define VR_LX0      0x84
#define VR_LY0      0x86
#define VR_LX1      0x88
#define VR_LY1      0x8A
#define VR_LX2      0x8C
#define VR_LY2      0x8E
#define VICKY_SPRITES 128
#define VR_LAYER(n) (0x10 + (n) * 0x10)
#define VR_BANDTOP  0xB0
#define VR_BANDBOT  0xB1
#define VR_BANDCTL  0xB2
#define VR_TCOLS    0xB3
#define VR_TROWS    0xB4
#define VR_CONOY    0xB5
#define VR_CONROWS  0xB6
#define VR_CONBOT   0xB7
#define VR_BANDMAP  0xB8
#define VR_CONMAP   0xBC
#define VR_PANELW   0xC0
#define VR_PANELH   0xC2
#define VR_CANVW    0xC4
#define VR_CANVH    0xC6
#define VR_IDRN     0xC8
#define VR_IDRIX    0xC9
#define VR_IDRS     0xCA
#define VR_IDRF     0xCB
#define VR_IDRW     0xCC
#define VR_IDRH     0xCE
#define VR_GLASSCTL 0xD0
#define VR_IDRSEL   0xD1
#define VR_SWW      0xD2
#define VR_SWH      0xD4
#define VR_GLASSW   0xD6
#define VR_GLASSH   0xD8
#define VR_SCALE    0xDA
#define VR_TXTCELL  0xDB
#define VR_TXTCOLS  0xDC
#define VR_TXTROWS  0xDD
#define VR_TXTVPAD  0xDE
#define VR_TXTHPAD  0xDF
#define VG_IDR      1         /* GLASSCTL bits0-1 */
#define VG_SOFT     2
#define VB_USER     0x01      /* BANDCTL: the F12 switch (host only) */
#define VB_PROGRAM  0x02      /* BANDCTL: a program has the bands */
#define VICKY_BAND_MIN_ROWS 10
#define VICKY_BAND_MAX_ROWS 10    /* both bands together: Doc's 2026-09-01 limit, and what BANDMAP's 12 KB holds at 180 columns */
#define VL_CTRL     0
#define VL_PALOFS   1
#define VL_SCROLLX  2
#define VL_SCROLLY  4
#define VL_STRIDE   6
#define VL_DATA     8
#define VL_MAP      12

#define VL_MODE_BITMAP 0
#define VL_MODE_TILE   1
#define VL_MODE_TEXT   2    /* text8  */
#define VL_MODE_TEXT32 3

void     vicky_reset(void);
/* The panel (the host's display, native pixels) and the canvas base (0 4:3,
 * 1 the whole panel): the IDR list follows.  Until told, 1920x1080 and 4:3 --
 * 1440x1080, 720x540, 480x360, 360x270: the HD family as it was. */
void     vicky_set_panel(int pw, int ph, int base);
void     vicky_set_cap(long pixels);                  /* the most pixels VICKY draws a frame (the host's limit) */
long     vicky_cap(void);
typedef struct { int w, h, scale, hd; } vicky_idr;    /* hd: HD text possible there */
int      vicky_idr_count(void);
const vicky_idr *vicky_idr_at(int i);                 /* largest first; NULL past the end */
int      vicky_idr_of_scale(int scale);               /* the list index a wanted scale becomes (IDRSEL's rule); -1 none */
int      vicky_glass_scale(void);                     /* this frame's glass on the panel: its scale, 0 if not an IDR */
int      vicky_glass_ctl(void);                       /* GLASSCTL as written (the frontend: software presentation) */
int      vicky_panel_w(void);
int      vicky_panel_h(void);
int      vicky_panel_base(void);
uint8_t  vicky_read(uint8_t reg);
void     vicky_write(uint8_t reg, uint8_t v);
void     vicky_render(uint8_t *fb, int pitch);        /* one full frame (tests) */
/* Scanline-granular interface for the frontend: run the CPU between lines. */
void     vicky_begin_frame(uint8_t *fb, int pitch);
void     vicky_line(int y);                           /* render line y, run SHEILA, raise IRQs */
void     vicky_end_frame(void);                       /* vblank */
void     vicky_commit(void);
/* Idle frames (2026-10-06): nothing VICKY could show has been written since
 * the last frame was drawn, so the frame is not drawn again -- fb keeps it.
 * Everything that writes what VICKY reads says so: the CPU's writes (but its
 * own low pages and K/OS's workspace), every I/O write but the IRQ
 * acknowledge and WAIT, the devices that load into memory, JIM, the host's
 * loaders.  SHEILA and JIM's pictures draw every
 * frame. */
extern int vicky_dirty, vicky_low;     /* vicky_low: something VICKY shows is in physical $0000-$FFFF (a CPU write there counts) */
#define VICKY_TOUCH() (vicky_dirty = 1)                          /* the picture, whole, now: the end of a synchronized update (core/term.c) */
/* HD text (2026-10-06, Doc: "can we cheat?").  At 720x540 the panel shows each
 * machine pixel as 2x2; with an HD font given here, VICKY draws the frame at
 * 1440x1080 instead: every layer and sprite doubled, as before, except text32
 * cells of 8x16 whose glyph in RAM is the stock one (STOCK, 256 x 16 rows) --
 * those come from HD, 256 glyphs of 16x32, two bytes a row, MSB first (8x8 cells
 * likewise from HD16, 16x16).  A
 * glyph a program changed is drawn as it is, doubled.  NULL turns it off.  The
 * machine sees nothing of this: its glass stays 720x540. */
void     vicky_hd_font(const uint8_t *hd, const uint8_t *stock, const uint8_t *hd16, const uint8_t *stock8);
                                                      /* and the same for 8x8 cells: HD16 256 x 16x16, STOCK8 256 x 8 */
int      vicky_cell_w(int layer);                     /* a text layer's cell: 8 or 16 wide (text32's field 2 and 3), */
int      vicky_cell_h(int layer);                     /* 8, 16 or 32 tall */
int      vicky_out_scale(void);                       /* 2 while this frame is drawn HD, else 1 */
int      vicky_out_w(void);                           /* the frame buffer's picture: the glass x the scale */
int      vicky_out_h(void);
int      vicky_glass_w(void);                         /* this frame's glass, latched at its start: 640x480, */
int      vicky_glass_h(void);                         /* or an HD mode's own size; the frame has vicky_glass_h() lines */
void     vicky_repaint(uint8_t *fb, int pitch);       /* redraw from RAM, guest state untouched (the frozen menu) */
/* JIM's shaped cursor: the text32 cell whose attribute byte is at attr_addr is
 * drawn with an underline (style 1, the bottom two rows reversed) or a bar
 * (style 2, the left two columns reversed) while on.  A block cursor is the
 * reverse bit in the cell itself, as always, and never comes through here. */
void     vicky_cursor(uint32_t attr_addr, int style, int on);
int      vicky_irq(void);                             /* nonzero if IRQSTAT & IRQMASK */
/* The layout in force ($B5-$B7): top band, console rows, bottom band.  rows is
 * 0 while no text grid has been declared ($B4 = 0). */
void     vicky_layout(uint8_t *oy, uint8_t *rows, uint8_t *bot);
void     vicky_set_user_bands(int on);                /* the host: the F12 switch, BANDCTL bit0 */
/* The address of the text32 cell shown at (col, row) of layer 0's grid: the
 * console's map, or BANDMAP for a band row.  For the host's screen readers
 * (the tests, the dumps, the frontend's band overlay), which must see what the
 * glass shows. */
uint32_t vicky_text_cell(int col, int row);
uint32_t vicky_palette_rgb(int index);                /* 0x00RRGGBB */
/* JIM's (core/term.c): the second screen's map shown in the console's place
 * (0: the console's own), and the bands as they are drawn -- the layout
 * latched with CONMAP, TCOLS, whether a program has them.  0 if BANDMAP is off. */
void     vicky_screen_map(uint32_t map);
int      vicky_bands(uint8_t *oy, uint8_t *rows, uint8_t *bot, uint8_t *cols, int *claimed);
const uint8_t *vicky_band_lines(void);              /* per line of the frame: 1 if a status band drew it */
uint32_t vicky_palette_gen(void);                     /* changes whenever any palette entry may have */

#endif
