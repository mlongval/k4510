/* VICKY step 1: palette port, 8 bpp bitmap layer, text layer, transparency, scroll. */
#include <stdio.h>
#include <string.h>
#include "../core/xemu/emutools_basicdefs.h"
#include "../core/xemu/cpu65.h"
#include "../core/mem.h"
#include "../core/io.h"
#include "../core/vicky.h"
#include <stdlib.h>
static uint8_t fb[640 * 480];                 /* the classic glass: this test draws no HD mode */
/* K4510_SHOT=dir: write each stage's framebuffer as a PPM there */
static void shot(const char *name)
{
    const char *dir = getenv("K4510_SHOT"); if (!dir) return;
    char path[512]; snprintf(path, sizeof path, "%s/vickytest-%s.ppm", dir, name);
    FILE *f = fopen(path, "wb"); if (!f) return;
    fprintf(f, "P6 640 480 255\n");
    for (int i = 0; i < 640 * 480; i++) { uint32_t c = vicky_palette_rgb(fb[i]); fputc(c >> 16, f); fputc(c >> 8, f); fputc(c, f); }
    fclose(f);
}

static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("  FAIL: " __VA_ARGS__); printf("\n"); } } while (0)
static void W(int r, uint8_t v) { io_write(IO_VICKY + r, v); }
static void W32(int r, uint32_t v) { for (int i = 0; i < 4; i++) W(r + i, (v >> (8 * i)) & 0xFF); }
static void W16(int r, uint16_t v) { W(r, v & 0xFF); W(r + 1, v >> 8); }

int main(void)
{
    printf("VICKY step 1\n");
    CHECK(mem_init() == 0, "mem_init");
    mem_reset();

    /* palette port: write entry 5 = #123456, index auto-increments */
    W(VR_PALIDX, 5); W(VR_PALR, 0x12); W(VR_PALG, 0x34); W(VR_PALB, 0x56);
    CHECK(vicky_palette_rgb(5) == 0x123456, "palette write");
    CHECK(io_read(IO_VICKY + VR_PALIDX) == 6, "palette index auto-increment");
    CHECK(vicky_palette_rgb(1) == 0xFFFFFF, "default palette entry 1 = white");

    /* layer 0: 8 bpp bitmap at phys $200000, stride 640, filled with index 7 except a hole of 0 */
    uint32_t bmp = 0x200000;
    for (int y = 0; y < VICKY_HEIGHT; y++) for (int x = 0; x < VICKY_WIDTH; x++)
        mem_poke(bmp + y * 640 + x, (x >= 100 && x < 200 && y >= 100 && y < 200) ? 0 : 7);
    W(VR_BGCOL, 2);
    W(VR_CTRL, 1);
    W32(VR_LAYER(0) + VL_DATA, bmp); W16(VR_LAYER(0) + VL_STRIDE, 640);
    W(VR_LAYER(0) + VL_CTRL, 1 | (VL_MODE_BITMAP << 1) | (3 << 3));   /* enable, bitmap, 8 bpp */
    vicky_render(fb, 640);
    shot("1-bitmap");
    printf("1. bitmap: px(10,10)=%d hole(150,150)=%d (index 0 transparent -> BGCOL)\n", fb[10*640+10], fb[150*640+150]);
    CHECK(fb[10 * 640 + 10] == 7, "bitmap pixel");
    CHECK(fb[150 * 640 + 150] == 2, "index 0 transparent: BGCOL shows");

    /* layer 1: text on top, glyph 'X' at cell (0,0) with a 1bpp font where 'X' = all ones; palofs 3 -> indices 6/7... */
    uint32_t font = 0x10000, map = 0x800;
    for (int i = 0; i < 256 * 8; i++) mem_poke(font + i, 0);
    for (int r = 0; r < 8; r++) mem_poke(font + 'X' * 8 + r, 0xFF);
    for (int i = 0; i < 80 * 60; i++) mem_poke(map + i, ' ');
    mem_poke(map + 0, 'X');
    W32(VR_LAYER(1) + VL_DATA, font); W32(VR_LAYER(1) + VL_MAP, map); W16(VR_LAYER(1) + VL_STRIDE, 80);
    W(VR_LAYER(1) + VL_PALOFS, 9);                     /* 1 bpp: index = (9<<1)|pix = 18/19 */
    W(VR_LAYER(1) + VL_CTRL, 1 | (VL_MODE_TEXT << 1));
    vicky_render(fb, 640);
    shot("2-text-over-bitmap");
    printf("2. text over bitmap: (0,0)=%d  (20,0)=%d (space: transparent, bitmap shows)\n", fb[0], fb[20]);
    CHECK(fb[0] == 19, "text pixel with palette offset");
    CHECK(fb[20] == 7,  "transparent text cell shows bitmap");

    /* scroll text layer by 8 px: the X moves left off-screen, cell (1,0) now at x=0 */
    mem_poke(map + 1, 'X');
    W16(VR_LAYER(1) + VL_SCROLLX, 8);
    vicky_render(fb, 640);
    CHECK(fb[0] == 19 && fb[8] == 7, "scroll X by one cell");

    /* display off -> all background */
    W(VR_CTRL, 0); vicky_render(fb, 640);
    CHECK(fb[0] == 2 && fb[150 * 640 + 150] == 2, "display off = background colour");

    /* raster register reads back end-of-frame */
    CHECK(io_read(IO_VICKY + VR_RASTER) == (480 & 0xFF), "raster low after frame");

    /* ---- tiles: 16x16 at 4 bpp, map entry with H-flip and palette offset ---- */
    mem_reset(); W(VR_CTRL, 1); W(VR_BGCOL, 0);
    uint32_t tiles = 0x300000, tmap = 0x310000;
    /* tile 1: left half index 1, right half index 2 (4 bpp: 8 bytes per row, 16 rows) */
    for (int r = 0; r < 16; r++) for (int b = 0; b < 8; b++) mem_poke(tiles + 128 + r * 8 + b, b < 4 ? 0x11 : 0x22);
    for (int i = 0; i < 40 * 30 * 2; i++) mem_poke(tmap + i, 0);
    mem_poke(tmap + 0, 1); mem_poke(tmap + 1, 0x00);          /* cell 0: tile 1, no flip, palofs 0 */
    mem_poke(tmap + 2, 1); mem_poke(tmap + 3, 0x04 | 0x30);   /* cell 1: tile 1, H-flip, palofs 3 */
    W32(VR_LAYER(0) + VL_DATA, tiles); W32(VR_LAYER(0) + VL_MAP, tmap); W16(VR_LAYER(0) + VL_STRIDE, 40);
    W(VR_LAYER(0) + VL_CTRL, 1 | (VL_MODE_TILE << 1) | (2 << 3) | (1 << 5));   /* tile, 4 bpp, 16 px */
    vicky_render(fb, 640);
    shot("3-tiles");
    printf("3. tiles 16x16 4bpp: cell0 (0,5)=%d (12,5)=%d | cell1 flipped+palofs3 (16,5)=%d (28,5)=%d\n",
           fb[5*640+0], fb[5*640+12], fb[5*640+16], fb[5*640+28]);
    CHECK(fb[5*640+0] == 1 && fb[5*640+12] == 2, "tile halves");
    CHECK(fb[5*640+16] == (3<<4|2) && fb[5*640+28] == (3<<4|1), "H-flip and palette offset");

    /* ---- text32: per-cell fg/bg, reverse bit, 8x16 glyphs ---- */
    uint32_t f16 = 0x320000, m32 = 0x330000;
    for (int i = 0; i < 256 * 16; i++) mem_poke(f16 + i, 0);
    for (int r = 0; r < 16; r++) mem_poke(f16 + 'A' * 16 + r, 0xF0);   /* left half set */
    uint8_t cell0[4] = { 'A', 0x00, 9, 4 }, cell1[4] = { 'A', 0x80, 9, 4 };
    mem_load(m32, cell0, 4); mem_load(m32 + 4, cell1, 4);
    W32(VR_LAYER(1) + VL_DATA, f16); W32(VR_LAYER(1) + VL_MAP, m32); W16(VR_LAYER(1) + VL_STRIDE, 80);
    W(VR_LAYER(1) + VL_CTRL, 1 | (VL_MODE_TEXT32 << 1) | (1 << 5));   /* text32, 8x16 */
    vicky_render(fb, 640);
    shot("4-text32");
    printf("4. text32 8x16: cell0 (1,12)=%d (6,12)=%d | reversed cell1 (9,12)=%d (14,12)=%d\n",
           fb[12*640+1], fb[12*640+6], fb[12*640+9], fb[12*640+14]);
    CHECK(fb[12*640+1] == 9 && fb[12*640+6] == 4, "text32 fg/bg");
    CHECK(fb[12*640+9] == 4 && fb[12*640+14] == 9, "text32 reverse");
    CHECK(fb[15*640+1] == 9 && fb[16*640+1] != 9, "8x16 glyph height");

    /* ---- sprites ---- */
    mem_reset(); W(VR_CTRL, 1); W(VR_BGCOL, 0);
    uint32_t sdat = 0x400000, stab = 0x410000;
    for (int i = 0; i < 16 * 16; i++) mem_poke(sdat + i, 0x33);            /* 16x16 8bpp, index 3 */
    for (int i = 0; i < 8 * 8 / 2; i++) mem_poke(sdat + 0x100 + i, 0x55);  /* 8x8 4bpp, index 5 */
    uint8_t s0[16] = { 100, 0, 50, 0, 0x00, 0x00, 0x40, 0x00, 0x03, 0x05, 0, 0,0,0,0,0 };  /* 8bpp 16x16 at (100,50), Z=0 */
    uint8_t s1[16] = { 108, 0, 58, 0, 0x00, 0x01, 0x40, 0x00, 0x01, 0x00, 2, 0,0,0,0,0 };  /* 4bpp 8x8 at (108,58) palofs 2, Z=0 */
    uint8_t s2[16] = { 0xF8, 0xFF, 10, 0, 0x00, 0x01, 0x40, 0x00, 0x01, 0x00, 1, 0,0,0,0,0 }; /* 8x8 at x=-8: fully off-screen left */
    for (int i = 0; i < 128 * 16; i++) mem_poke(stab + i, 0);
    mem_load(stab, s0, 16); mem_load(stab + 16, s1, 16); mem_load(stab + 32, s2, 16);
    W32(VR_SPRTAB, stab); W(VR_SPRCTL, 1);
    /* a layer under them: 8bpp bitmap all index 1 at y>=60 only */
    for (int y = 0; y < 480; y++) for (int x = 0; x < 640; x++) mem_poke(bmp + y * 640 + x, y >= 60 ? 1 : 0);
    W32(VR_LAYER(0) + VL_DATA, bmp); W16(VR_LAYER(0) + VL_STRIDE, 640);
    W(VR_LAYER(0) + VL_CTRL, 1 | (3 << 3));
    vicky_render(fb, 640);
    shot("5-sprites");
    printf("5. sprites: s0(100,50)=%d  s1 over s0 (110,60)=%d  outside=%d  colSS=$%02X colSL=$%02X\n",
           fb[50*640+100], fb[60*640+110], fb[40*640+100], io_read(IO_VICKY+VR_COLSS), io_read(IO_VICKY+VR_COLSL));
    CHECK(fb[50*640+100] == 0x33, "sprite 0 pixel");
    CHECK(fb[60*640+110] == (2<<4|5), "sprite 1 (4bpp, palofs) drawn over sprite 0 -- later sprite wins");
    CHECK(fb[40*640+100] == 0, "nothing above sprite");
    /* collisions: s0<->s1 overlap -> bits 0,1; s0 and s1 over layer (y>=60) -> bits 0,1; s2 off-screen: none */
    mem_reset(); /* reset clears read-cleared regs; re-render to read fresh */
    W(VR_CTRL,1); W32(VR_SPRTAB, stab); W(VR_SPRCTL,1); W32(VR_LAYER(0)+VL_DATA,bmp); W16(VR_LAYER(0)+VL_STRIDE,640); W(VR_LAYER(0)+VL_CTRL, 1|(3<<3));
    vicky_render(fb, 640);
    uint8_t ss = io_read(IO_VICKY + VR_COLSS), sl = io_read(IO_VICKY + VR_COLSL);
    CHECK(ss == 0x03, "sprite-sprite collision bits (%02X)", ss);
    CHECK(sl == 0x03, "sprite-layer collision bits (%02X)", sl);
    CHECK(io_read(IO_VICKY + VR_COLSS) == 0, "collision cleared on read");

    /* ---- SHEILA: change BGCOL at lines 100 and 200, IRQ at 300; raster compare at 50 ---- */
    mem_reset(); W(VR_CTRL, 1); W(VR_BGCOL, 1);
    uint32_t cop = 0x500000;
    uint8_t prog[] = {
        0x01, 100, 0,  0,       /* WAIT 100   */
        0x02, VR_BGCOL, 2, 0,   /* MOVE BGCOL,2 */
        0x01, 200, 0,  0,       /* WAIT 200   */
        0x02, VR_BGCOL, 3, 0,
        0x01, 44, 1,   0,       /* WAIT 300   */
        0x05, 0, 0, 0,          /* IRQ        */
        0x00, 0, 0, 0,          /* END        */
    };
    mem_load(cop, prog, sizeof prog);
    W32(VR_SHEILA, cop); W(VR_SHEILACTL, 1);
    W(VR_RASTER, 50); W(VR_RASTER + 1, 0);
    W(VR_IRQMASK, VI_RASTER | VI_SHEILA);
    vicky_begin_frame(fb, 640);
    int irq_at_raster = -1, irq_at_sheila = -1;
    for (int y = 0; y < 480; y++) {
        vicky_line(y);
        if (irq_at_raster < 0 && (io_read(IO_VICKY + VR_IRQSTAT) & VI_RASTER)) irq_at_raster = y;
        if (irq_at_sheila < 0 && (io_read(IO_VICKY + VR_IRQSTAT) & VI_SHEILA)) irq_at_sheila = y;
    }
    vicky_end_frame();
    shot("6-sheila");
    printf("6. SHEILA: bg@50=%d bg@150=%d bg@250=%d  rasterIRQ@%d sheilaIRQ@%d  irq=%d\n",
           fb[50*640], fb[150*640], fb[250*640], irq_at_raster, irq_at_sheila, vicky_irq());
    CHECK(fb[50*640] == 1 && fb[150*640] == 2 && fb[250*640] == 3, "SHEILA gradient");
    CHECK(irq_at_raster == 50, "raster compare IRQ");
    CHECK(irq_at_sheila == 300, "SHEILA IRQ");
    CHECK(vicky_irq() && !(vicky_irq() & VI_VBLANK), "IRQ line respects mask (vblank masked)");
    W(VR_IRQSTAT, VI_RASTER | VI_SHEILA);
    CHECK(vicky_irq() == 0, "ack clears");
    vicky_render(fb, 640);
    /* frame 2: BGCOL persists at 3 from frame 1 until the list sets 2 at line 100 -> proves restart */
    CHECK(fb[50*640] == 3 && fb[150*640] == 2, "SHEILA restarts each frame");

    /* ---- blitter: 4x2 sprite-ish block, keyed copy with H-flip onto a filled area ---- */
    mem_reset();
    uint32_t bs = 0x600000, bd = 0x610000;
    uint8_t blk[8] = { 1, 2, 0, 4,   5, 0, 7, 8 };          /* 4 wide, 2 high, stride 4 */
    mem_load(bs, blk, 8);
    W32(VR_BLTSRC, 9); W32(VR_BLTDST, bd); W16(VR_BLTW, 6); W16(VR_BLTH, 3); W16(VR_BLTDS, 6);
    W(VR_BLTOP, 2); W(VR_BLTFLG, 0); W(VR_BLTCMD, 1);      /* fill 6x3 with 9 */
    CHECK(mem_peek(bd) == 9 && mem_peek(bd + 17) == 9 && mem_peek(bd + 18) == 0, "fill 6x3");
    W32(VR_BLTSRC, bs); W32(VR_BLTDST, bd + 1); W16(VR_BLTW, 4); W16(VR_BLTH, 2); W16(VR_BLTSS, 4); W16(VR_BLTDS, 6);
    W(VR_BLTOP, 1); W(VR_BLTFLG, 1); W(VR_BLTCMD, 1);      /* keyed, H-flipped, at (1,0) */
    printf("7. blit keyed+hflip: row0 = %d %d %d %d %d %d | row1 = %d %d %d %d %d %d\n",
        mem_peek(bd),mem_peek(bd+1),mem_peek(bd+2),mem_peek(bd+3),mem_peek(bd+4),mem_peek(bd+5),
        mem_peek(bd+6),mem_peek(bd+7),mem_peek(bd+8),mem_peek(bd+9),mem_peek(bd+10),mem_peek(bd+11));
    CHECK(mem_peek(bd+1) == 4 && mem_peek(bd+2) == 9 && mem_peek(bd+3) == 2 && mem_peek(bd+4) == 1, "keyed H-flip row 0");
    CHECK(mem_peek(bd+7) == 8 && mem_peek(bd+8) == 7 && mem_peek(bd+9) == 9 && mem_peek(bd+10) == 5, "keyed H-flip row 1");
    W(VR_BLTOP, 5); W(VR_BLTFLG, 0); W(VR_BLTCMD, 1);      /* XOR same block unflipped at (1,0) */
    /* line op: 8x8 surface at bd+64, diagonal (0,0)-(7,7) colour 3, and one that leaves the clip box */
    { uint32_t ls = bd + 64; int i, diag = 1, off = 0, clipped = 1;
      for (i = 0; i < 64; i++) mem_poke(ls + i, 0);
      W32(VR_BLTSRC, 3); W32(VR_BLTDST, ls); W16(VR_BLTW, 8); W16(VR_BLTH, 8); W16(VR_BLTDS, 8);
      W16(VR_LX0, 0); W16(VR_LY0, 0); W16(VR_LX1, 7); W16(VR_LY1, 7); W(VR_BLTOP, 6); W(VR_BLTCMD, 1);
      for (i = 0; i < 8; i++) { if (mem_peek(ls + i * 8 + i) != 3) diag = 0; if (mem_peek(ls + i * 8 + (7 - i)) != 0 && i != 3 && i != 4) off = 1; }
      W16(VR_LX0, 4); W16(VR_LY0, 4); W16(VR_LX1, 40); W16(VR_LY1, 4); W(VR_BLTCMD, 1);     /* runs off the right edge */
      for (i = 64; i < 72; i++) if (mem_peek(ls + i) != 0) clipped = 0;
      CHECK(diag && !off, "blitter LINE draws the diagonal");
      CHECK(mem_peek(ls + 4 * 8 + 7) == 3 && clipped, "blitter LINE clips to BLTW x BLTH");
      /* triangle (0,0) (7,0) (0,7): upper-left half of the 8x8 filled with 5 */
      for (i = 0; i < 64; i++) mem_poke(ls + i, 0);
      W32(VR_BLTSRC, 5); W16(VR_LX0, 0); W16(VR_LY0, 0); W16(VR_LX1, 7); W16(VR_LY1, 0); W16(VR_LX2, 0); W16(VR_LY2, 7); W(VR_BLTOP, 7); W(VR_BLTCMD, 1);
      { int in = 1, out = 1; for (i = 0; i < 8; i++) { if (mem_peek(ls + i * 8 + 0) != 5) in = 0; if (mem_peek(ls + i * 8 + 7) != (i == 0 ? 5 : 0)) out = 0; }
        CHECK(in && out && mem_peek(ls + 7 * 8 + 1) == 0, "blitter TRIANGLE fills the half square"); }
    }
    CHECK(mem_peek(bd+1) == (4 ^ 1) && mem_peek(bd+2) == (9 ^ 2), "XOR");

    /* ---- the smaller modes: a 200-line field, and 160 columns ---- */
    mem_reset();
    { uint32_t bm = 0x700000;
      for (int i = 0; i < 320 * 200; i++) mem_poke(bm + i, 7);        /* 320x200, 8 bpp, every pixel 7 */
      W(VR_BGCOL, 3);
      W(VR_LAYER(0) + VL_CTRL, 1 | (VL_MODE_BITMAP << 1) | (3 << 3)); /* enable, bitmap, 8 bpp */
      W(VR_LAYER(0) + VL_PALOFS, 0);
      W16(VR_LAYER(0) + VL_SCROLLX, 0); W16(VR_LAYER(0) + VL_SCROLLY, 0);
      W16(VR_LAYER(0) + VL_STRIDE, 320); W32(VR_LAYER(0) + VL_DATA, bm);
      for (int n = 1; n < VICKY_LAYERS; n++) W(VR_LAYER(n) + VL_CTRL, 0);
      W(VR_SPRCTL, 0); W(VR_SHEILACTL, 0);

      W(VR_CTRL, 1 | 2 | 8);                                          /* 320x200 */
      vicky_render(fb, 640);
      printf("8. 320x200: line 39=%d line 40=%d line 439=%d line 440=%d  px(638,240)=%d\n",
             fb[39*640], fb[40*640], fb[439*640], fb[440*640], fb[240*640+638]);
      CHECK(fb[39*640] == 3 && fb[440*640] == 3, "the 40 lines above and below the field are BGCOL");
      CHECK(fb[40*640] == 7 && fb[439*640] == 7, "the 200-line field fills lines 40..439");
      CHECK(fb[240*640+638] == 7 && fb[240*640+639] == 7, "320 columns doubled reach the right edge");

      W(VR_CTRL, 1 | 2 | 8 | 16);                                     /* 160x200: four screen pixels each */
      for (int i = 0; i < 320 * 200; i++) mem_poke(bm + i, (i % 320) < 1 ? 5 : 7);   /* column 0 of the source = 5 */
      vicky_render(fb, 640);
      printf("9. 160x200: px(0,100)=%d px(3,100)=%d px(4,100)=%d line 39=%d\n",
             fb[100*640], fb[100*640+3], fb[100*640+4], fb[39*640]);
      CHECK(fb[100*640] == 5 && fb[100*640+3] == 5 && fb[100*640+4] == 7, "one pixel of the machine is four on the glass");
      CHECK(fb[39*640] == 3 && fb[440*640] == 3, "the field is still 200 lines");
    }

    printf(fails ? "\n%d FAILED\n" : "\nALL OK\n", fails);
    return fails != 0;
}
