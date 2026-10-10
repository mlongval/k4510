/* gfx.h -- the drawing words gfx.c needs, each machine's own way, so the
 * suite is one source built twice (as bench.h does for the others).
 *
 * Both machines draw on a 320x240 bitmap of 8 bits a pixel, with a 40x30
 * text layer above it and 16x16 sprites of 8 bits a pixel above that.  Each
 * word is what a C programmer would reach for first on that machine:
 *
 *                 Commander X16 (VERA)            K4510 (VICKY)
 *   g_rect        KERNAL GRAPH_draw_rect, filled  the blitter, FILL
 *   g_line        KERNAL GRAPH_draw_line          the blitter, LINE
 *   g_pixel       VERA's address, then DATA0      far_poke (STA [zp],Z)
 *   g_image       KERNAL GRAPH_draw_image         the blitter, COPY
 *   g_scroll      KERNAL GRAPH_move_rect          DMA copy (memmove)
 *   g_text_row    VERA's data port, 2 bytes/cell  the data ports, 2 bytes/cell
 *   g_sprite      VERA's data port, 4 bytes       far_poke16 x 2
 *   g_palette     VERA's data port, 2 bytes/entry PALIDX, then R G B
 *
 * The X16's GRAPH routines are the KERNAL's 65C02 code drawing through
 * VERA's data port; VERA has no blitter.  The K4510's blitter and DMA finish
 * in the write that starts them (core/vicky.h: "Instant"), so there the time
 * is the CPU's, setting the registers.  README.md says what that means.
 *
 * Text over a picture: VERA's text layer has a colour per cell, and its
 * background colour 0 lets the bitmap through.  VICKY's text32 does the same
 * with LCTRL bit7 (2026-10-10), and the K4510's data ports are VERA's DATA0:
 * port 0 steps along the cells' characters, port 1 along their colours, so
 * both machines write two bytes a cell, one store each.  (Until then the
 * K4510 used text8 and a far_poke a cell, and lost TEXT clock for clock.)
 */
#ifndef GFX_H
#define GFX_H
#include <stdint.h>

#define GW 320
#define GH 240
static uint32_t row_at[GH];                 /* where each row of the bitmap starts */
static uint8_t pal_saved[240 * 3];          /* entries 16-255, as found: g_palette writes them back */

#ifdef __CX16__
/* ---- the Commander X16 ------------------------------------------------ */
#include <cx16.h>
#define R(n) (*(volatile uint16_t *)(0x02 + 2 * (n)))      /* the KERNAL's r0-r15 */
#define SPR_DATA 0x13000UL                  /* what X16 BASIC's SPRITES.BAS uses: free in mode $80 */
#define SPR_ATTR 0x1FC00UL
#define PAL_VRAM 0x1FA00UL
static uint8_t g_c, g_mode_was;
static uint32_t bitmap, textmap;
static uint16_t text_stride;
#define LETTER(i) ((char) (1 + (i) % 26))  /* screen codes: A is 1 */
#define SCODE(c) ((char) ((c) >= 0x41 && (c) <= 0x5A ? (c) - 0x40 : (c)))   /* cc65's PETSCII to a screen code */

/* The bank byte is taken from memory: cc65 2.19 -O compiled (uint8_t) (a >> 16)
 * | inc to inc alone (stz sreg before the ora), so everything above 64 KB of
 * VRAM landed in the bitmap */
#define BYTE2(a) (((const uint8_t *) &(a))[2])
static void vaddr(uint32_t a, uint8_t inc) { VERA.address = (uint16_t) a; VERA.address_hi = BYTE2(a) | inc; }
/* screen_mode ($FF5F): cc65 2.19's videomode() predates r49's modes, and its
 * $80 left the screen in text */
static void mode_set(uint8_t m) { g_c = m; __asm__("lda %v", g_c); __asm__("clc"); __asm__("jsr $FF5F"); }
static uint8_t mode_get(void) { __asm__("sec"); __asm__("jsr $FF5F"); __asm__("sta %v", g_c); return g_c; }
static void colours(uint8_t c) { g_c = c; __asm__("lda %v", g_c); __asm__("tax"); __asm__("ldy #0"); __asm__("jsr $FF29"); }

static void g_rect(uint16_t x, uint8_t y, uint16_t w, uint8_t h, uint8_t c)
{
    colours(c); R(0) = x; R(1) = y; R(2) = w; R(3) = h; R(4) = 0;
    __asm__("sec"); __asm__("jsr $FF2F");
}
static void g_line(uint16_t x0, uint8_t y0, uint16_t x1, uint8_t y1, uint8_t c)
{
    colours(c); R(0) = x0; R(1) = y0; R(2) = x1; R(3) = y1;
    __asm__("jsr $FF2C");
}
static void g_pixel(uint16_t x, uint8_t y, uint8_t c)
{
    uint32_t a = row_at[y] + x;
    VERA.address = (uint16_t) a; VERA.address_hi = BYTE2(a); VERA.data0 = c;
}
static void g_image(uint16_t x, uint8_t y, const uint8_t *p, uint8_t w, uint8_t h)
{
    R(0) = x; R(1) = y; R(2) = (uint16_t) p; R(3) = w; R(4) = h;
    __asm__("jsr $FF38");
}
static void g_scroll(void)                  /* everything up one line */
{
    R(0) = 0; R(1) = 1; R(2) = 0; R(3) = 0; R(4) = GW; R(5) = GH - 1;
    __asm__("jsr $FF32");
}
static void g_text_row(uint8_t r, const char *s, uint8_t c)
{
    uint8_t i;
    vaddr(textmap + (uint32_t) r * text_stride, 0x10);
    for (i = 0; i < 40; i++) { VERA.data0 = s[i]; VERA.data0 = c; }
}
static void g_sprite(uint8_t n, uint16_t x, uint16_t y)
{
    vaddr(SPR_ATTR + 2 + (uint16_t) n * 8, 0x10);
    VERA.data0 = (uint8_t) x; VERA.data0 = x >> 8; VERA.data0 = (uint8_t) y; VERA.data0 = y >> 8;
}
static void g_palette(void)                 /* entries 16-255: 12 bits each, GB then R */
{
    uint8_t i; const uint8_t *p = pal_saved;
    vaddr(PAL_VRAM + 32, 0x10);
    for (i = 0; i < 240; i++) { VERA.data0 = p[0]; VERA.data0 = p[1]; p += 3; }
}
static void g_init(const uint8_t *shape)
{
    uint16_t i; uint8_t n;
    g_mode_was = mode_get();
    mode_set(0x80);                         /* 320x240, 256 colours, the text over it */
    R(0) = 0; __asm__("jsr $FF20");         /* GRAPH_init: the default framebuffer driver */
    __asm__("lda #2"); __asm__("jsr $FF62"); /* screen_set_charset: PETSCII upper case, where A is screen code 1 */
    VERA.control = 0;
    bitmap = (uint32_t) (VERA.layer0.tilebase & 0xFC) << 9;
    textmap = (uint32_t) VERA.layer1.mapbase << 9;
    text_stride = 2 * (32 << ((VERA.layer1.config >> 4) & 3));
    for (i = 0; i < GH; i++) row_at[i] = bitmap + (uint32_t) i * GW;
    vaddr(PAL_VRAM + 32, 0x10);
    for (i = 0; i < 240; i++) { pal_saved[i * 3] = VERA.data0; pal_saved[i * 3 + 1] = VERA.data0; }
    vaddr(SPR_DATA, 0x10);
    for (i = 0; i < 256; i++) VERA.data0 = shape[i];
    vaddr(SPR_ATTR, 0x10);                  /* 32 sprites: 8 bpp, in front, 16x16, off the screen */
    for (n = 0; n < 32; n++) {
        VERA.data0 = (uint8_t) (SPR_DATA >> 5); VERA.data0 = 0x80 | (uint8_t) (SPR_DATA >> 13);
        VERA.data0 = 0; VERA.data0 = 2; VERA.data0 = 0; VERA.data0 = 2;     /* x, y = 512 */
        VERA.data0 = 0x0C; VERA.data0 = 0x50;
    }
    VERA.display.video |= 0x40;
}
static void g_record(void)                  /* a few frames of the picture into run.py's .gif */
{
    uint16_t t;
    *(volatile uint8_t *) 0x9FB5 = 2;
    t = ticks(); while ((uint16_t) (ticks() - t) < 20) ;
    *(volatile uint8_t *) 0x9FB5 = 0;
}
static void g_done(void)
{
    uint8_t n;
    vaddr(SPR_ATTR + 6, 0x40);              /* every eighth byte: the sprites' Z, 0 = off */
    for (n = 0; n < 32; n++) VERA.data0 = 0;
    VERA.display.video &= ~0x40;
    mode_set(g_mode_was);
}

#else
/* ---- the K4510 -------------------------------------------------------- */
#define BLT      0xD070u
#define BITMAP   0x00200000UL               /* where BREAKOUT and K4510 BASIC's GRAPHICS draw */
#define TEXTMAP  0x00220000UL               /* text32: char, -, colour, background 0 (see-through) */
#define SPR_TAB  0x00230000UL               /* 128 x 16 bytes */
#define SPR_DATA 0x00231000UL
#define LETTER(i) ((char) ('A' + (i) % 26))
#define SCODE(c) ((char) ((c) >= 'a' && (c) <= 'z' ? (c) - 32 : (c)))      /* capitals, as the X16 shows them */
static void rom_video(void) { ((void (*)(void)) 0xFF92)(); }
static uint8_t was_ctrl, was_bg, was_spr, was_l[3], was_tab[4];

static void g_rect(uint16_t x, uint8_t y, uint16_t w, uint8_t h, uint8_t c)
{
    REG(BLT) = c; REG(BLT + 1) = 0; REG(BLT + 2) = 0; REG(BLT + 3) = 0;
    w32(BLT + 4, BITMAP + row_at[y] + x); w16(BLT + 8, w); w16(BLT + 10, h); w16(BLT + 14, GW);
    REG(BLT + 0x10) = 2; REG(BLT + 0x12) = 1;
}
static void g_line(uint16_t x0, uint8_t y0, uint16_t x1, uint8_t y1, uint8_t c)
{
    REG(BLT) = c; REG(BLT + 1) = 0; REG(BLT + 2) = 0; REG(BLT + 3) = 0;
    w32(BLT + 4, BITMAP); w16(BLT + 8, GW); w16(BLT + 10, GH); w16(BLT + 14, GW);
    w16(BLT + 0x14, x0); w16(BLT + 0x16, y0); w16(BLT + 0x18, x1); w16(BLT + 0x1A, y1);
    REG(BLT + 0x10) = 6; REG(BLT + 0x12) = 1;
}
static void g_pixel(uint16_t x, uint8_t y, uint8_t c)
{
    far_poke(BITMAP + row_at[y] + x, c);
}
static void g_image(uint16_t x, uint8_t y, const uint8_t *p, uint8_t w, uint8_t h)
{
    w32(BLT, (uint16_t) p);                 /* a program's own memory is physical memory */
    w32(BLT + 4, BITMAP + row_at[y] + x); w16(BLT + 8, w); w16(BLT + 10, h); w16(BLT + 12, w); w16(BLT + 14, GW);
    REG(BLT + 0x10) = 0; REG(BLT + 0x12) = 1;
}
static void g_scroll(void)
{
    dma_copy(BITMAP + GW, BITMAP, (uint32_t) GW * (GH - 1));
}
static void g_text_row(uint8_t r, const char *s, uint8_t c)
{
    uint8_t i; uint32_t p = TEXTMAP + (uint32_t) r * 160;
    port_at(PORT0, p, 4); port_at(PORT1, p + 2, 4);   /* every cell's char, every cell's colour */
    for (i = 0; i < 40; i++) { PORT_DATA(PORT0) = s[i]; PORT_DATA(PORT1) = c; }
}
static void g_sprite(uint8_t n, uint16_t x, uint16_t y)
{
    uint32_t e = SPR_TAB + (uint16_t) n * 16;   /* (a data port is slower here: 6 stores to set it for 4 to send) */
    far_poke16(e, x); far_poke16(e + 2, y);
}
static void g_palette(void)
{
    uint8_t i; const uint8_t *p = pal_saved;
    REG(V_PALIDX) = 16;                     /* B commits the entry and moves PALIDX on */
    for (i = 0; i < 240; i++) { REG(V_PALR) = p[0]; REG(V_PALG) = p[1]; REG(V_PALB) = p[2]; p += 3; }
}
static void g_init(const uint8_t *shape)
{
    uint16_t i; uint8_t n;
    was_ctrl = REG(V_CTRL); was_bg = REG(V_BGCOL); was_spr = REG(V_SPRCTL);
    for (n = 0; n < 3; n++) was_l[n] = REG(V_LAYER(n));
    for (n = 0; n < 4; n++) was_tab[n] = REG(V_SPRTAB + n);
    for (i = 0; i < GH; i++) row_at[i] = (uint32_t) i * GW;
    for (i = 0; i < 240; i++) {             /* the palette reads back (core/vicky.c) */
        REG(V_PALIDX) = (uint8_t) (16 + i);
        pal_saved[i * 3] = REG(V_PALR); pal_saved[i * 3 + 1] = REG(V_PALG); pal_saved[i * 3 + 2] = REG(V_PALB);
    }
    dma_fill(0, BITMAP, (uint32_t) GW * GH);
    dma_fill(0, TEXTMAP, 40 * 30 * 4);
    dma_fill(0, SPR_TAB, 128 * 16);
    for (i = 0; i < 256; i++) far_poke(SPR_DATA + i, shape[i]);
    for (n = 0; n < 32; n++) {              /* on, 8 bpp, over every layer; 16x16; off the screen */
        uint32_t e = SPR_TAB + (uint16_t) n * 16;
        far_poke16(e, 512); far_poke16(e + 2, 512);
        far_poke16(e + 4, (uint16_t) SPR_DATA); far_poke16(e + 6, (uint16_t) (SPR_DATA >> 16));
        far_poke(e + 8, 1 | 2 | (3 << 4)); far_poke(e + 9, 1 | (1 << 2));
    }
    REG(V_LAYER(0)) = 0;                    /* the console off; ours are layers 1 and 2 */
    for (n = 1; n <= 5; n++) REG(V_LAYER(1) + n) = 0;
    w16(V_LAYER(1) + 6, GW); w32(V_LAYER(1) + 8, BITMAP);
    REG(V_LAYER(1)) = 1 | (3 << 3);         /* bitmap, 8 bpp */
    for (n = 1; n <= 5; n++) REG(V_LAYER(2) + n) = 0;
    w16(V_LAYER(2) + 6, 40); w32(V_LAYER(2) + 8, FONT8); w32(V_LAYER(2) + 12, TEXTMAP);
    REG(V_LAYER(2)) = 0x80 | 1 | (3 << 1);  /* text32, 8x8 cells, see-through */
    w32(V_SPRTAB, SPR_TAB); REG(V_SPRCTL) = 1;
    REG(BLT + 0x11) = 0;
    REG(V_BGCOL) = 0;
    REG(V_CTRL) = 1 | 2 | 4;                /* 320x240 */
}
static void g_record(void)                  /* two seconds on show: run.py's screenshot falls in them */
{
    uint8_t i;
    for (i = 0; i < 120; i++) wait_vblank();
}
static void g_done(void)
{
    uint8_t n;
    REG(V_SPRCTL) = was_spr;
    for (n = 0; n < 4; n++) REG(V_SPRTAB + n) = was_tab[n];
    for (n = 0; n < 3; n++) REG(V_LAYER(n)) = was_l[n];
    REG(V_BGCOL) = was_bg; REG(V_CTRL) = was_ctrl;
    rom_video();
}
#endif
#endif
