/* VICKY -- the K4510 video chip.
 *
 * Register block at IO_VICKY ($D000), 256 bytes, byte-addressed. Every
 * pointer is a 28-bit physical address into main RAM; there is no video
 * memory. Rendering is per scanline into an 8-bit indexed framebuffer the
 * frontend supplies; the frontend applies vicky_palette_rgb().
 *
 *   $00  CTRL      bit0 display enable; the rest pick the mode.  The glass is
 *                  always 640x480 and the raster lines are always 0..479 --
 *                  a smaller mode is drawn into it, doubled, and centred.
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

#define VICKY_WIDTH   640
#define VICKY_HEIGHT  480
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
uint8_t  vicky_read(uint8_t reg);
void     vicky_write(uint8_t reg, uint8_t v);
void     vicky_render(uint8_t *fb, int pitch);        /* one full frame (tests) */
/* Scanline-granular interface for the frontend: run the CPU between lines. */
void     vicky_begin_frame(uint8_t *fb, int pitch);
void     vicky_line(int y);                           /* render line y, run SHEILA, raise IRQs */
void     vicky_end_frame(void);                       /* vblank */
void     vicky_repaint(uint8_t *fb, int pitch);       /* redraw from RAM, guest state untouched (the frozen menu) */
/* JIM's shaped cursor: the text32 cell whose attribute byte is at attr_addr is
 * drawn with an underline (style 1, the bottom two rows reversed) or a bar
 * (style 2, the left two columns reversed) while on.  A block cursor is the
 * reverse bit in the cell itself, as always, and never comes through here. */
void     vicky_cursor(uint32_t attr_addr, int style, int on);
int      vicky_irq(void);                             /* nonzero if IRQSTAT & IRQMASK */
uint32_t vicky_palette_rgb(int index);                /* 0x00RRGGBB */
uint32_t vicky_palette_gen(void);                     /* changes whenever any palette entry may have */

#endif
