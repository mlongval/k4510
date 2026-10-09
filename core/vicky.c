#include "vicky.h"
#include "mem.h"
#include "io.h"
#include "term.h"
#include "jimgfx.h"
#include "idr.h"
#include <string.h>
#include <stdlib.h>

static uint8_t  reg[256];
static uint32_t pal[256];
static uint32_t pal_gen;                     /* bumped on every palette change: the frontend rebuilds its tables on a new value */
static int      cur_line;
static uint8_t  col_ss[16], col_sl[16];     /* collision accumulators for the frame in progress */
static uint8_t *frame_fb; static int frame_pitch;
static uint32_t sh_pc; static int sh_wait;   /* -1 = not waiting, -2 = ended */
static uint16_t raster_cmp;
static uint8_t  owner[VICKY_WIDTH];          /* per-pixel: 0 = layers only, else sprite n+1 */
static uint8_t  layer_hit[VICKY_WIDTH];      /* per-pixel: a layer drew a non-zero index here */
static uint8_t  lowres_tmp[VICKY_WIDTH];     /* CTRL bit1: 320x240 rendered here, then doubled */
#define OLD_W 640                            /* the classic glass: MODE 0-4 are drawn into 640x480 */
#define OLD_H 480
static inline uint16_t rd16(const uint8_t *p);

/* ---- the panel, the canvas and the integer display resolutions ---------------
 * (vicky.h $C0-$DF; docs/design-video-foundations.md).  The host says what the
 * panel is; the list of IDRs is worked out here, once, and every glass CTRL bit5
 * asks for is one of them -- or a software resolution, inside the same limits. */
static int  panel_w = 1920, panel_h = 1080, panel_base = IDR_BASE_43, canv_w = 1440, canv_h = 1080;
static long cap_px = 1920L * 1080L;
static vicky_idr idrs[IDR_MAX]; static int n_idr = -1;
static void idr_build(void)
{
    idr_limits lim = idr_default_limits; idr_t t[IDR_MAX];
    lim.max_pixels = cap_px; lim.max_w = VICKY_WIDTH; lim.max_h = VICKY_HEIGHT;
    idr_canvas(panel_w, panel_h, panel_base, &canv_w, &canv_h);
    n_idr = idr_list(panel_w, panel_h, panel_base, &lim, t, IDR_MAX);
    for (int i = 0; i < n_idr; i++) { idrs[i].w = t[i].w; idrs[i].h = t[i].h; idrs[i].scale = t[i].scale; idrs[i].hd = idr_hd_text(&t[i], &lim); }
    vicky_dirty = 1;
}
static void idr_ready(void) { if (n_idr < 0) idr_build(); }
void vicky_set_panel(int pw, int ph, int base)
{
    if (pw == panel_w && ph == panel_h && base == panel_base && n_idr >= 0) return;
    panel_w = pw; panel_h = ph; panel_base = base; idr_build();
}
void vicky_set_cap(long px) { if (px < 320L * 200L) px = 320L * 200L; if (px != cap_px || n_idr < 0) { cap_px = px; idr_build(); } }
long vicky_cap(void) { return cap_px; }
int  vicky_idr_count(void) { idr_ready(); return n_idr; }
const vicky_idr *vicky_idr_at(int i) { idr_ready(); return i >= 0 && i < n_idr ? &idrs[i] : NULL; }
int  vicky_panel_w(void) { return panel_w; }
int  vicky_panel_h(void) { return panel_h; }
int  vicky_panel_base(void) { return panel_base; }
int  vicky_glass_ctl(void) { return reg[VR_GLASSCTL]; }
/* A wanted scale as an IDR: that scale if it is offered; else the next larger
 * one that is (a smaller picture rather than none: /1 on a 4K panel is /2);
 * else the largest scale offered (/4 under 320x200 becomes the smallest IDR). */
int vicky_idr_of_scale(int s)
{
    int best = -1;
    idr_ready();
    if (n_idr <= 0) return -1;
    for (int i = 0; i < n_idr; i++) if (idrs[i].scale == s) return i;
    for (int i = 0; i < n_idr; i++) if (idrs[i].scale > s && (best < 0 || idrs[i].scale < idrs[best].scale)) best = i;
    return best >= 0 ? best : n_idr - 1;
}
/* The glass a CTRL byte asks for, with GLASSCTL, IDRSEL and SWW/SWH as they
 * stand.  Without bit5 the classic 640x480; with it an IDR or a software size. */
static void glass_of(uint8_t c, int *w, int *h, int *hd, int *scale)
{
    int gc = reg[VR_GLASSCTL] & 3;
    *scale = 0;
    if (!(c & 0x20)) { *w = OLD_W; *h = OLD_H; *hd = 0; return; }
    *hd = 1;
    if (gc == VG_SOFT) {                              /* a program's own size, inside the limits and the panel */
        int sw = rd16(&reg[VR_SWW]), sh = rd16(&reg[VR_SWH]);
        int mw = panel_w < VICKY_WIDTH ? panel_w : VICKY_WIDTH, mh = panel_h < VICKY_HEIGHT ? panel_h : VICKY_HEIGHT;
        if (sw < 160) sw = 160;
        if (sh < 100) sh = 100;
        if (sw > mw) sw = mw;
        if (sh > mh) sh = mh;
        if ((long) sw * sh > cap_px) sh = (int)(cap_px / sw);
        *w = sw; *h = sh; return;
    }
    { int want = gc == VG_IDR ? reg[VR_IDRSEL] : (c & 16) ? 4 : (c & 6) ? 2 : 1, i = vicky_idr_of_scale(want);
      if (i < 0) { *w = OLD_W; *h = OLD_H; return; }   /* a panel too small for any: the classic glass */
      *w = idrs[i].w; *h = idrs[i].h; *scale = idrs[i].scale; }
}
static int      glass_w = OLD_W, glass_h = OLD_H, glass_hd, glass_scale;   /* this frame's, latched at its start */
/* Which lines of the frame are a status band's (2026-10-07: the frame colour,
 * io.h io_frame): set as the lines are drawn, read by the frontend. */
static uint8_t band_out[VICKY_HEIGHT], line_band;
const uint8_t *vicky_band_lines(void) { return band_out; }
static void glass_latch(void) { glass_of(reg[VR_CTRL], &glass_w, &glass_h, &glass_hd, &glass_scale); }
int vicky_glass_w(void) { return glass_w; }
int vicky_glass_h(void) { return glass_h; }
int vicky_glass_scale(void) { return glass_scale; }
/* The text grid TXTCELL's cells make on the glass the registers ask for now
 * (GLASSCTL alone implies bit5: K/OS asks before it writes CTRL). */
static void txt_grid(int *cols, int *rows, int *vpad, int *hpad)
{
    int w, h, hd, sc, csz = reg[VR_TXTCELL] & 3, cw = csz >= 2 ? 16 : 8, ch = csz == 0 ? 8 : csz == 3 ? 32 : 16;
    glass_of((uint8_t)(reg[VR_CTRL] | ((reg[VR_GLASSCTL] & 3) ? 0x20 : 0)), &w, &h, &hd, &sc);
    *cols = w / cw; *rows = h / ch;
    if (*cols > 255) *cols = 255;
    if (*rows > 255) *rows = 255;
    *vpad = (h - *rows * ch) / 2; *hpad = (w - *cols * cw) / 2;
}

/* ---- HD text (vicky.h) ---------------------------------------------------- */
static const uint8_t *hd_font, *hd_stock, *hd_font16, *hd_stock8, *hd_font48, *hd_font24;
static int hd_on;                                /* this frame: 0, or 2 / 3 -- the glass drawn at twice / three times, text from the HD font */
void vicky_hd_font(const uint8_t *hd, const uint8_t *stock, const uint8_t *hd16, const uint8_t *stock8)
{
    if (hd != hd_font || stock != hd_stock || hd16 != hd_font16 || stock8 != hd_stock8) vicky_dirty = 1;
    hd_font = hd; hd_stock = stock; hd_font16 = hd16; hd_stock8 = stock8;
}
void vicky_hd_font3(const uint8_t *hd48, const uint8_t *hd24)
{
    if (hd48 != hd_font48 || hd24 != hd_font24) vicky_dirty = 1;
    hd_font48 = hd48; hd_font24 = hd24;
}
int vicky_cell_w(int n) { uint8_t c = reg[VR_LAYER(n & 3) + VL_CTRL]; return ((c >> 1) & 3) == VL_MODE_TEXT32 && (c & 0x40) ? 16 : 8; }
int vicky_cell_h(int n) { uint8_t c = reg[VR_LAYER(n & 3) + VL_CTRL]; int z = (c >> 5) & 3;
                          return z == 3 && ((c >> 1) & 3) == VL_MODE_TEXT32 ? 32 : z ? 16 : 8; }
int vicky_out_scale(void) { return hd_on ? hd_on : 1; }
int vicky_out_w(void) { return glass_w * vicky_out_scale(); }
int vicky_out_h(void) { return glass_h * vicky_out_scale(); }
/* What the text32 layer left at each pixel of the line, for the HD pass:
 * hd_src 1 where a text32 cell's pixel is on top (any later layer or sprite
 * clears it), and then which glyph, its row, column, colours and cursor. */
static uint8_t  hd_src[VICKY_WIDTH], hd_gx[VICKY_WIDTH], hd_gy[VICKY_WIDTH], hd_fg[VICKY_WIDTH], hd_bg[VICKY_WIDTH], hd_cur[VICKY_WIDTH];
static uint16_t hd_g[VICKY_WIDTH];
static uint32_t hd_data[VICKY_WIDTH];
static uint8_t  hd_line[VICKY_WIDTH];            /* the machine's line, before it is doubled */
static uint8_t  hd_ok[2][256]; static uint32_t hd_ok_data[2] = { 0xFFFFFFFFu, 0xFFFFFFFFu };   /* [0] 8x16, [1] 8x8: the glyphs at hd_ok_data that are stock, worked out once a frame */
static uint8_t  spr_list[4][VICKY_SPRITES];  /* this line's sprites, by Z, in table order (sprites_gather) */
static int      spr_n[4];

static const uint32_t c64_palette[16] = {   /* VIC-II colours as the first 16 entries (spec §2) */
    0x000000, 0xFFFFFF, 0x880000, 0xAAFFEE, 0xCC44CC, 0x00CC55, 0x0000AA, 0xEEEE77,
    0xDD8855, 0x664400, 0xFF7777, 0x333333, 0x777777, 0xAAFF66, 0x0088FF, 0xBBBBBB,
};

void vicky_reset(void)
{
    uint8_t user = reg[VR_BANDCTL] & VB_USER;   /* the host's switch is the host's: a reset keeps it */
    memset(reg, 0, sizeof reg);
    reg[VR_BANDCTL] = user;
    for (int i = 0; i < 256; i++) pal[i] = (i < 16) ? c64_palette[i] : (uint32_t)(i * 0x010101);
    pal_gen++; vicky_dirty = 1;
    cur_line = 0;
    sh_wait = -2; raster_cmp = 0xFFFF;
}

static void blit(void);
uint32_t vicky_palette_rgb(int i) { return pal[i & 0xFF]; }
uint32_t vicky_palette_gen(void) { return pal_gen; }

void vicky_layout(uint8_t *oy, uint8_t *rows, uint8_t *bot)
{
    uint8_t tc = reg[VR_TCOLS], tr = reg[VR_TROWS], ctl = reg[VR_BANDCTL];
    int t = 0, b = 0;
    if (tc >= 40 && tr >= 30 && (ctl & (VB_USER | VB_PROGRAM))) {
        if (ctl & VB_PROGRAM) { t = reg[VR_BANDTOP]; b = reg[VR_BANDBOT]; }
        else                  { t = 1; b = 1; }                  /* the user's: one row each (Doc, 2026-09-14) */
        if (t + b > tr - VICKY_BAND_MIN_ROWS || t + b > VICKY_BAND_MAX_ROWS) { t = 1; b = 1; }
    }
    *oy = (uint8_t) t; *bot = (uint8_t) b; *rows = (uint8_t)(tr - t - b);
}
void vicky_set_user_bands(int on) { reg[VR_BANDCTL] = (uint8_t)((reg[VR_BANDCTL] & ~VB_USER) | (on ? VB_USER : 0)); }

uint8_t vicky_read(uint8_t r)
{
    switch (r) {
    case VR_CONOY: case VR_CONROWS: case VR_CONBOT:
        { uint8_t oy, rows, bot; vicky_layout(&oy, &rows, &bot); return r == VR_CONOY ? oy : r == VR_CONROWS ? rows : bot; }
    case VR_RASTER:     return cur_line & 0xFF;
    case VR_PANELW: case VR_PANELW + 1: return (uint8_t)(panel_w >> (8 * (r - VR_PANELW)));
    case VR_PANELH: case VR_PANELH + 1: return (uint8_t)(panel_h >> (8 * (r - VR_PANELH)));
    case VR_CANVW: case VR_CANVW + 1: idr_ready(); return (uint8_t)(canv_w >> (8 * (r - VR_CANVW)));
    case VR_CANVH: case VR_CANVH + 1: idr_ready(); return (uint8_t)(canv_h >> (8 * (r - VR_CANVH)));
    case VR_IDRN: return (uint8_t) vicky_idr_count();
    case VR_IDRS: case VR_IDRF: case VR_IDRW: case VR_IDRW + 1: case VR_IDRH: case VR_IDRH + 1:
        { const vicky_idr *d = vicky_idr_at(reg[VR_IDRIX]); if (!d) return 0;
          return r == VR_IDRS ? (uint8_t) d->scale : r == VR_IDRF ? (uint8_t)((d->hd ? 1 : 0) | (d->hd == 3 ? 2 : 0))
               : r < VR_IDRH ? (uint8_t)(d->w >> (8 * (r - VR_IDRW))) : (uint8_t)(d->h >> (8 * (r - VR_IDRH))); }
    case VR_GLASSW: case VR_GLASSW + 1: case VR_GLASSH: case VR_GLASSH + 1: case VR_SCALE:
        { int w, h, hd, sc; glass_of((uint8_t)(reg[VR_CTRL] | ((reg[VR_GLASSCTL] & 3) ? 0x20 : 0)), &w, &h, &hd, &sc);   /* GLASSCTL implies bit5, as for TXT */
          return r == VR_SCALE ? (uint8_t) sc : r < VR_GLASSH ? (uint8_t)(w >> (8 * (r - VR_GLASSW))) : (uint8_t)(h >> (8 * (r - VR_GLASSH))); }
    case VR_TXTCOLS: case VR_TXTROWS: case VR_TXTVPAD: case VR_TXTHPAD:
        { int c, rw, vp, hp; txt_grid(&c, &rw, &vp, &hp);
          return (uint8_t)(r == VR_TXTCOLS ? c : r == VR_TXTROWS ? rw : r == VR_TXTVPAD ? vp : hp); }
    case VR_RASTER + 1: return cur_line >> 8;
    default:
        if (r >= VR_COLSS && r < VR_COLSS + 16) { uint8_t v = reg[r]; if (r == VR_COLSS) memset(&reg[VR_COLSS], 0, 16); return v; }
        if (r >= VR_COLSL && r < VR_COLSL + 16) { uint8_t v = reg[r]; if (r == VR_COLSL) memset(&reg[VR_COLSL], 0, 16); return v; }
        if (r == VR_PALR) return (uint8_t)(pal[reg[VR_PALIDX]] >> 16);      /* the palette reads back: entry PALIDX */
        if (r == VR_PALG) return (uint8_t)(pal[reg[VR_PALIDX]] >> 8);
        if (r == VR_PALB) return (uint8_t)pal[reg[VR_PALIDX]];
        return reg[r];
    }
}

void vicky_write(uint8_t r, uint8_t v)
{
    if (r == VR_IRQSTAT) { reg[r] &= ~v; return; }          /* ack */
    if (r == VR_BLTCMD)  { blit(); return; }
    if (r == VR_RASTER)     { raster_cmp = (raster_cmp & 0xFF00) | v; return; }
    if (r == VR_RASTER + 1) { raster_cmp = (raster_cmp & 0x00FF) | (v << 8); return; }
    if (r >= VR_CONOY && r <= VR_CONBOT) return;           /* computed: read-only */
    if (r >= VR_PANELW && r <= VR_TXTHPAD && r != VR_IDRIX && r != VR_GLASSCTL && r != VR_IDRSEL
        && !(r >= VR_SWW && r < VR_SWW + 4) && r != VR_TXTCELL) return;   /* the glass's: computed, read-only */
    if (r == VR_BANDCTL) { reg[r] = (uint8_t)((reg[r] & VB_USER) | (v & VB_PROGRAM)); return; }   /* bit0 is the host's */
    reg[r] = v;
    if (r == VR_CONMAP + 3) vicky_layout(&reg[VR_CONOY], &reg[VR_CONROWS], &reg[VR_CONBOT]);   /* latch the layout the bands are drawn to */
    if (r == VR_PALB) {
        uint8_t i = reg[VR_PALIDX];
        pal[i] = ((uint32_t)reg[VR_PALR] << 16) | ((uint32_t)reg[VR_PALG] << 8) | v;
        reg[VR_PALIDX] = i + 1;
        pal_gen++; vicky_dirty = 1;
    }
}

static inline uint32_t rd32(const uint8_t *p) { return (p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24)) & K4510_PHYS_MASK; }
static inline uint16_t rd16(const uint8_t *p) { return p[0] | (p[1] << 8); }
static inline uint8_t  ram(uint32_t a) { return k4510_ram[a & K4510_PHYS_MASK]; }
#define ram_ptr(a) k4510_ram[(a) & K4510_PHYS_MASK]

/* Option B: which row of BANDMAP a map row cy of layer 0 is drawn from, or
 * -1 for a console row (or when the bands are not drawn from BANDMAP). */
static int band_row(int cy, uint32_t map)
{
    uint32_t bm = rd32(&reg[VR_BANDMAP]);
    if (!bm || map != rd32(&reg[VR_CONMAP])) return -1;
    /* the layout LATCHED when CONMAP was written -- the one K/OS laid the
     * console out to -- not the live one: a band switched on in F12 must not
     * cover a running program's rows before K/OS has moved its console */
    int oy = reg[VR_CONOY], rows = reg[VR_CONROWS], bot = reg[VR_CONBOT];
    if (cy < oy) return cy;
    if (cy >= oy + rows && cy < oy + rows + bot) return cy - rows;
    return -1;
}
/* The second screen (core/term.c): while alt_map is set, layer 0's console
 * rows come from it instead of the console's map; the band rows still come
 * from BANDMAP.  A program's own map (not CONMAP) is shown as it is. */
static uint32_t alt_map;
void vicky_screen_map(uint32_t map) { alt_map = map & K4510_PHYS_MASK; vicky_dirty = 1; }
int vicky_bands(uint8_t *oy, uint8_t *rows, uint8_t *bot, uint8_t *cols, int *claimed)
{
    const uint8_t *L = &reg[VR_LAYER(0)];
    if (!rd32(&reg[VR_BANDMAP])) return 0;
    if (((L[VL_CTRL] >> 1) & 3) != VL_MODE_TEXT32 || rd32(&L[VL_MAP]) != rd32(&reg[VR_CONMAP])) return 0;   /* a program's own screen: no bands drawn into it */
    *oy = reg[VR_CONOY]; *rows = reg[VR_CONROWS]; *bot = reg[VR_CONBOT];   /* latched with CONMAP: what is drawn */
    *cols = reg[VR_TCOLS];
    *claimed = (reg[VR_BANDCTL] & VB_PROGRAM) != 0;
    return 1;
}
uint32_t vicky_text_cell(int col, int row)
{
    const uint8_t *L = &reg[VR_LAYER(0)];
    uint32_t map = rd32(&L[VL_MAP]); uint16_t stride = rd16(&L[VL_STRIDE]);
    int br = (((L[VL_CTRL] >> 1) & 3) == VL_MODE_TEXT32) ? band_row(row, map) : -1;
    if (br >= 0) return (rd32(&reg[VR_BANDMAP]) + ((uint32_t) br * stride + (uint32_t) col) * 4) & K4510_PHYS_MASK;
    if (alt_map && map == rd32(&reg[VR_CONMAP])) map = alt_map;
    return (map + ((uint32_t) row * stride + (uint32_t) col) * 4) & K4510_PHYS_MASK;
}

static uint32_t cur_at; static int cur_style, cur_on;      /* JIM's shaped cursor */
void vicky_cursor(uint32_t attr_addr, int style, int on) { if (cur_at != attr_addr || cur_style != style || cur_on != (on && style)) vicky_dirty = 1;
                                                           cur_at = attr_addr; cur_style = style; cur_on = on && style; }

/* Render the first w pixels of one scanline of one layer into line[]; index 0
 * is transparent.  w is 640, or 320/160 in the half- and quarter-width modes,
 * where nothing past w is ever shown (review 2026-09-05, 8: those modes used
 * to render all 640 and throw most away).  Cell and pixel sizes are powers of
 * two, so positions are shifts and masks, not a divide per pixel. */
static void layer_line(int n, int y, uint8_t *line, int w)
{
    const uint8_t *L = &reg[VR_LAYER(n)];
    int mode  = (L[VL_CTRL] >> 1) & 3;
    int depth = (L[VL_CTRL] >> 3) & 3;          /* 0..3 -> 1,2,4,8 bpp */
    int csz   = (L[VL_CTRL] >> 5) & 3;          /* cell size field */
    int bpp   = 1 << depth;
    int ppb   = 8 >> depth;                     /* pixels per byte */
    uint8_t  palofs = L[VL_PALOFS];
    uint32_t data   = rd32(&L[VL_DATA]);
    uint32_t map    = rd32(&L[VL_MAP]);
    uint16_t stride = rd16(&L[VL_STRIDE]);
    int sy  = y + rd16(&L[VL_SCROLLY]);
    int sx0 = rd16(&L[VL_SCROLLX]);
    uint8_t mask = (uint8_t)((1 << bpp) - 1);

    if (mode == VL_MODE_BITMAP) {
        uint8_t base = (uint8_t)(palofs << bpp);
        uint32_t row = data + (uint32_t)sy * stride;
        if (bpp == 8) {
            for (int x = 0; x < w; x++) { uint8_t pix = ram(row + (uint32_t)(sx0 + x)); if (pix) { line[x] = pix; layer_hit[x] = 1; hd_src[x] = 0; } }
            return;
        }
        /* 1/2/4 bpp: fetch each byte once and walk its pixels */
        uint32_t a = row + (uint32_t)(sx0 >> (3 - depth));
        int sub = sx0 & (ppb - 1);
        uint8_t b = ram(a);
        for (int x = 0; x < w; x++) {
            uint8_t pix = (b >> (8 - bpp - sub * bpp)) & mask;
            if (pix) { line[x] = (uint8_t)(base | pix); layer_hit[x] = 1; hd_src[x] = 0; }
            if (++sub == ppb) { sub = 0; b = ram(++a); }
        }
        return;
    }
    if (mode == VL_MODE_TILE) {
        int lg = 3 + csz, size = 1 << lg;                  /* 8,16,32,64 */
        int tbytes = size * size * bpp / 8;
        int rowbytes = size * bpp / 8;
        int cy = sy >> lg, ty = sy & (size - 1);
        for (int x = 0; x < w; ) {
            int sx = x + sx0;
            int cx = sx >> lg, tx0 = sx & (size - 1);
            uint32_t e = map + ((uint32_t)cy * stride + cx) * 2;
            uint16_t ent = ram(e) | (ram(e + 1) << 8);
            int idx = ent & 0x3FF, hf = ent & 0x400, vf = ent & 0x800;
            uint8_t base = (uint8_t)((ent >> 12) << bpp);
            int ry = vf ? (size - 1 - ty) : ty;
            uint32_t trow = data + (uint32_t)idx * tbytes + (uint32_t)ry * rowbytes;
            for (int tx = tx0; tx < size && x < w; tx++, x++) {
                int px = hf ? (size - 1 - tx) : tx;
                uint8_t b = ram(trow + (uint32_t)((px << depth) >> 3));
                int shift = (bpp == 8) ? 0 : (8 - bpp - (px & (ppb - 1)) * bpp);
                uint8_t pix = (b >> shift) & mask;
                if (pix) { line[x] = (bpp == 8) ? pix : (uint8_t)(base | pix); layer_hit[x] = 1; hd_src[x] = 0; }
            }
        }
        return;
    }
    /* text modes: 1-bpp glyphs, 8 px wide, H = 8 or 16 rows.  text32 also has
     * cells 16 wide (2026-10-06, for 1440x1080): the cell field's 2 is 16x16 and
     * 3 is 16x32, a glyph row two bytes, MSB first.  TEXT keeps 8 wide. */
    int H = csz == 3 && mode == VL_MODE_TEXT32 ? 32 : csz ? 16 : 8;
    int CW = (csz & 2) && mode == VL_MODE_TEXT32 ? 16 : 8;
    sy = y + (int16_t) rd16(&L[VL_SCROLLY]);          /* signed for text: the ROM scrolls the HD console DOWN by half its spare lines */
    /* The HD spare lines, above and below the whole rows: a text32 layer
     * paints them with the nearest row's backgrounds and no glyphs, so with
     * the bands up they are the bands' grey and without them the console's
     * colour.  Not BGCOL: that is what a program's transparent bitmap shows,
     * and making it grey turned SPLIT's picture grey (the Dell, 2026-09-14). */
    int pad = 0;
    if (sy < 0) { if (mode != VL_MODE_TEXT32 || !glass_hd) return; sy = 0; pad = 1; }
    int cy = sy / H, gy = sy % H;
    if (glass_hd && sy >= glass_h / H * H) { if (mode != VL_MODE_TEXT32) return; cy = glass_h / H - 1; pad = 1; }
    if (mode == VL_MODE_TEXT) {
        uint8_t base = (uint8_t)(palofs << 1);
        for (int x = 0; x < w; ) {
            int sx = x + sx0, cx = sx >> 3, gx0 = sx & 7;
            uint8_t cell = ram(map + (uint32_t)cy * stride + cx);
            uint8_t row  = ram(data + (uint32_t)cell * H + gy);
            for (int gx = gx0; gx < 8 && x < w; gx++, x++) {
                uint8_t pix = (row >> (7 - gx)) & 1;
                if (pix) { line[x] = (uint8_t)(base | pix); layer_hit[x] = 1; hd_src[x] = 0; }
            }
        }
        return;
    }
    /* text32 -- layer 0's band rows from BANDMAP when K/OS has the bands there */
    uint32_t rowbase = map + (uint32_t)cy * stride * 4;
    if (n == 0) { int br = band_row(cy, map); if (br >= 0) { rowbase = rd32(&reg[VR_BANDMAP]) + (uint32_t)br * stride * 4; line_band = 1; }
                  else if (alt_map && map == rd32(&reg[VR_CONMAP])) rowbase = alt_map + (uint32_t)cy * stride * 4; }   /* the second screen */
    /* ...and the spare COLUMNS too (2026-10-07: an IDR need not be a whole
     * number of cells wide -- 1066 is not): in an HD glass the scroll is signed
     * and the pixels left of the grid, and right of its last column, take the
     * nearest cell's background */
    int sxt = glass_hd ? (int16_t) rd16(&L[VL_SCROLLX]) : sx0;
    for (int x = 0; x < w; ) {
        int sx = x + sxt, cx = sx / CW, gx0 = sx % CW;
        if (sx < 0 || (glass_hd && stride && cx >= stride)) {
            uint32_t pe = rowbase + (uint32_t)(sx < 0 ? 0 : stride - 1) * 4;
            uint8_t pc = (ram(pe + 1) & 0x80) ? ram(pe + 2) : ram(pe + 3);
            int n = sx < 0 ? -sx : w - x;
            for (; n > 0 && x < w; n--, x++) { line[x] = pc; layer_hit[x] = 1; if (hd_on) hd_src[x] = 0; }
            continue;
        }
        uint32_t e = rowbase + (uint32_t)cx * 4;
        uint16_t g = ram(e) | ((ram(e + 1) & 0x7F) << 8);
        int rev = ram(e + 1) & 0x80;
        uint8_t fg = ram(e + 2), bg = ram(e + 3);
        if (rev) { uint8_t t = fg; fg = bg; bg = t; }
        unsigned row;                                     /* a padding line: the background only */
        if (pad) row = 0;
        else if (CW == 8) row = ram(data + (uint32_t)g * H + gy);
        else { uint32_t a = data + ((uint32_t)g * H + gy) * 2; row = (unsigned) ram(a) << 8 | ram(a + 1); }
        /* the shaped cursor (vicky_cursor): an underline reverses this cell's
         * bottom eighth (two rows of 16), a bar its left quarter (two columns of 8) */
        int cur = !pad && cur_on && e + 1 == cur_at;
        if (cur && cur_style == 1 && gy < H - (H >= 16 ? H / 8 : 2)) cur = 0;
        for (int gx = gx0; gx < CW && x < w; gx++, x++) {
            int sw = cur && (cur_style == 1 || gx < CW / 4);
            line[x] = (((row >> (CW - 1 - gx)) & 1) != 0) != (sw != 0) ? fg : bg;
            layer_hit[x] = 1;              /* text32 cells are opaque: every pixel is "a layer drew here" */
            if (hd_on) { hd_src[x] = (uint8_t)(!pad && CW == 8 ? (H == 16 ? 1 : 2) : 0); hd_g[x] = g; hd_gx[x] = (uint8_t) gx; hd_gy[x] = (uint8_t) gy;
                         hd_fg[x] = fg; hd_bg[x] = bg; hd_data[x] = data; hd_cur[x] = (uint8_t)(cur ? 1 + cur_style : 0); }
        }
    }
}

/* Once per line: which enabled sprites cover line y, bucketed by Z in table
 * order.  The four sprites_line calls then walk only those, instead of each
 * reading all 128 entries (review 2026-09-05, 7).  Per line, not per frame,
 * because SHEILA or a raster IRQ may rewrite the table mid-frame. */
static void sprites_gather(int y)
{
    spr_n[0] = spr_n[1] = spr_n[2] = spr_n[3] = 0;
    if (!(reg[VR_SPRCTL] & 1)) return;
    uint32_t tab = rd32(&reg[VR_SPRTAB]);
    for (int n = 0; n < VICKY_SPRITES; n++) {
        uint32_t e = tab + (uint32_t)n * 16;
        uint8_t ctrl = ram(e + 8);
        if (!(ctrl & 1)) continue;
        int16_t syp = (int16_t)(ram(e + 2) | (ram(e + 3) << 8));
        int h = 8 << ((ram(e + 9) >> 2) & 3);
        int ry = y - syp;
        if (ry < 0 || ry >= h) continue;
        int z = (ctrl >> 4) & 3;
        spr_list[z][spr_n[z]++] = (uint8_t)n;
    }
}

/* Draw this line's sprites with Z == z (sprites_gather ran first), clipped to w. */
static void sprites_line(int z, int y, uint8_t *line, int w)
{
    uint32_t tab = rd32(&reg[VR_SPRTAB]);
    for (int k = 0; k < spr_n[z]; k++) {
        int n = spr_list[z][k];
        uint32_t e = tab + (uint32_t)n * 16;
        uint8_t ctrl = ram(e + 8);
        int16_t sxp = (int16_t)(ram(e) | (ram(e + 1) << 8));
        int16_t syp = (int16_t)(ram(e + 2) | (ram(e + 3) << 8));
        uint8_t size = ram(e + 9);
        int sw = 8 << (size & 3), h = 8 << ((size >> 2) & 3);
        int ry = y - syp;
        int bpp = (ctrl & 2) ? 8 : 4;
        int rowbytes = sw * bpp / 8;
        /* a byte at a time: rd32 on a raw pointer masked only the START, so a
         * sprite table in the last bytes of RAM read past the mapping (review
         * 2026-09-17).  ram() masks each address, as everything else here does. */
        uint32_t data = ((uint32_t) ram(e + 4) | (uint32_t) ram(e + 5) << 8 | (uint32_t) ram(e + 6) << 16
                         | (uint32_t) ram(e + 7) << 24) & K4510_PHYS_MASK;
        if (ctrl & 8) ry = h - 1 - ry;                  /* V-flip */
        uint32_t row = data + (uint32_t)ry * rowbytes;
        uint8_t base = (uint8_t)(ram(e + 10) << 4);
        for (int px = 0; px < sw; px++) {
            int x = sxp + px;
            if (x < 0 || x >= w) continue;
            int sp = (ctrl & 4) ? (sw - 1 - px) : px;   /* H-flip */
            uint8_t b = ram(row + sp * bpp / 8);
            uint8_t pix = (bpp == 8) ? b : ((sp & 1) ? (b & 0x0F) : (b >> 4));
            if (!pix) continue;
            if (owner[x]) { int o = owner[x] - 1; col_ss[o >> 3] |= 1 << (o & 7); col_ss[n >> 3] |= 1 << (n & 7); }
            else owner[x] = (uint8_t)(n + 1);
            if (layer_hit[x]) col_sl[n >> 3] |= 1 << (n & 7);
            line[x] = (bpp == 8) ? pix : (uint8_t)(base | pix);
            hd_src[x] = 0;
        }
    }
}

/* ---- SHEILA, the display-list coprocessor ------------------------------------------------------------ */
static void sheila_run(int y)
{
    if (!(reg[VR_SHEILACTL] & 1) || sh_wait == -2) return;
    if (sh_wait >= 0) { if (y < sh_wait) return; sh_wait = -1; }
    for (int guard = 0; guard < 256; guard++) {
        uint8_t op = ram(sh_pc), a0 = ram(sh_pc + 1), a1 = ram(sh_pc + 2), a2 = ram(sh_pc + 3);
        sh_pc += 4;
        switch (op) {
        case 0x00: sh_wait = -2; return;
        case 0x01: { int l = a0 | (a1 << 8); if (y < l) { sh_wait = l; return; } break; }
        case 0x02: vicky_write(a0, a1); break;
        case 0x03: if (y >= (a0 | (a1 << 8))) sh_pc += 4; break;
        case 0x04: sh_pc = a0 | (a1 << 8) | ((uint32_t)a2 << 16); break;
        case 0x05: reg[VR_IRQSTAT] |= VI_SHEILA; break;
        default:   sh_wait = -2; return;       /* bad opcode ends the list */
        }
    }
}

/* ---- blitter ------------------------------------------------------------- */
static void blit(void)
{
    uint32_t src = rd32(&reg[VR_BLTSRC]), dst = rd32(&reg[VR_BLTDST]);
    int w = rd16(&reg[VR_BLTW]), h = rd16(&reg[VR_BLTH]);
    if ((uint64_t)w * (uint64_t)h > K4510_PHYS_SIZE) return;   /* nothing legitimate blits more than RAM */
    int ss = rd16(&reg[VR_BLTSS]), ds = rd16(&reg[VR_BLTDS]);
    int op = reg[VR_BLTOP], hf = reg[VR_BLTFLG] & 1, vf = reg[VR_BLTFLG] & 2;
    uint8_t fill = reg[VR_BLTSRC];
    if (op == 6) {                                     /* LINE, Bresenham, clipped to w x h */
        int x0 = (int16_t)rd16(&reg[VR_LX0]), y0 = (int16_t)rd16(&reg[VR_LY0]);
        int x1 = (int16_t)rd16(&reg[VR_LX1]), y1 = (int16_t)rd16(&reg[VR_LY1]);
        int dx = abs(x1 - x0), dy = abs(y1 - y0), sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1, err = dx - dy;
        for (;;) {
            if (x0 >= 0 && x0 < w && y0 >= 0 && y0 < h) ram_ptr(dst + (uint32_t)y0 * ds + x0) = fill;
            if (x0 == x1 && y0 == y1) break;
            int e2 = err * 2;
            if (e2 > -dy) { err -= dy; x0 += sx; }
            if (e2 <  dx) { err += dx; y0 += sy; }
        }
        return;
    }
    if (op == 7) {                                     /* TRIANGLE: scanline fill, clipped to w x h */
        int px[3] = { (int16_t)rd16(&reg[VR_LX0]), (int16_t)rd16(&reg[VR_LX1]), (int16_t)rd16(&reg[VR_LX2]) };
        int py[3] = { (int16_t)rd16(&reg[VR_LY0]), (int16_t)rd16(&reg[VR_LY1]), (int16_t)rd16(&reg[VR_LY2]) };
        int ymin = py[0], ymax = py[0];
        for (int i = 1; i < 3; i++) { if (py[i] < ymin) ymin = py[i]; if (py[i] > ymax) ymax = py[i]; }
        if (ymin < 0) ymin = 0;
        if (ymax >= h) ymax = h - 1;
        for (int y = ymin; y <= ymax; y++) {
            int xl = 0x7FFFFFFF, xr = -0x7FFFFFFF;
            for (int i = 0; i < 3; i++) {                  /* intersect the scanline with each edge */
                int j = (i + 1) % 3, ya = py[i], yb = py[j], xa = px[i], xb = px[j];
                if (ya == yb) { if (y == ya) { if (xa < xl) xl = xa; if (xb < xl) xl = xb; if (xa > xr) xr = xa; if (xb > xr) xr = xb; } continue; }
                if (y < (ya < yb ? ya : yb) || y > (ya < yb ? yb : ya)) continue;
                int x = xa + (int)((long)(xb - xa) * (y - ya) / (yb - ya));
                if (x < xl) xl = x;
                if (x > xr) xr = x;
            }
            if (xl > xr) continue;
            if (xl < 0) xl = 0;
            if (xr >= w) xr = w - 1;
            if (xl <= xr) {                      /* ram_ptr masks the start only; the span must not leave RAM */
                uint32_t a = (dst + (uint32_t)y * ds + (uint32_t)xl) & K4510_PHYS_MASK, n = (uint32_t)(xr - xl + 1);
                if (a + n <= K4510_PHYS_SIZE) memset(k4510_ram + a, fill, n);
                else for (uint32_t i = 0; i < n; i++) ram_ptr(a + i) = fill;
            }
        }
        return;
    }
    for (int y = 0; y < h; y++) {
        int sy = vf ? (h - 1 - y) : y;
        uint32_t srow = src + (uint32_t)sy * ss, drow = dst + (uint32_t)y * ds;
        for (int x = 0; x < w; x++) {
            int sx = hf ? (w - 1 - x) : x;
            uint8_t *d = &ram_ptr(drow + x);
            uint8_t  s = (op == 2) ? fill : ram(srow + sx);
            switch (op) {
            case 0: case 2: *d = s; break;
            case 1: if (s) *d = s; break;
            case 3: *d &= s; break;
            case 4: *d |= s; break;
            case 5: *d ^= s; break;
            default: return;
            }
        }
    }
}

int vicky_irq(void) { return reg[VR_IRQSTAT] & reg[VR_IRQMASK]; }

int vicky_dirty = 1, vicky_low = 1;
static int frame_skip;                               /* this frame is the last one: the lines are not drawn again */
/* Does VICKY read anything in the CPU's own 64 KB (physical $0000-$FFFF)?  A
 * program's stack and variables are written there all the time, and they are
 * the picture only if a layer, the sprite table or a sprite's data is there
 * too (core/mem.c asks, through vicky_low). */
static int reads_low(void)
{
    for (int n = 0; n < VICKY_LAYERS; n++) {
        const uint8_t *L = &reg[VR_LAYER(n)];
        if ((L[VL_CTRL] & 1) && (rd32(L + VL_DATA) < 0x10000u || rd32(L + VL_MAP) < 0x10000u)) return 1;
    }
    if (reg[VR_SPRCTL] & 1) {
        uint32_t tab = rd32(&reg[VR_SPRTAB]);
        if (tab < 0x10000u) return 1;
        for (int n = 0; n < VICKY_SPRITES; n++) {
            uint32_t e = tab + (uint32_t)n * 16;
            if (!(ram(e + 8) & 1)) continue;
            { int16_t sx = (int16_t)(ram(e) | (ram(e + 1) << 8)), sy = (int16_t)(ram(e + 2) | (ram(e + 3) << 8));
              if (sx <= -64 || sy <= -64 || sx >= glass_w || sy >= glass_h) continue; }   /* off the glass: not seen */
            if ((((uint32_t) ram(e + 4) | (uint32_t) ram(e + 5) << 8 | (uint32_t) ram(e + 6) << 16 | (uint32_t) ram(e + 7) << 24) & K4510_PHYS_MASK) < 0x10000u) return 1;
        }
    }
    return 0;
}
static int repainting;                               /* vicky_repaint's own begin_frame: not a new frame */
void vicky_begin_frame(uint8_t *fb, int pitch)
{
    /* The second screen's bytes are read here, before this frame's lines and
     * before frame_skip is decided: bytes that came during the last frame are
     * on the glass at the end of this one.  They were read at vblank, after
     * the picture was made, and waited a frame more (the JIM review,
     * 2026-10-09: 16.7 ms on every echo). */
    if (!repainting) io_frame_start();
    if (fb != frame_fb || pitch != frame_pitch) vicky_dirty = 1;   /* another buffer: it has not got the last picture */
    frame_fb = fb; frame_pitch = pitch;
    frame_skip = !vicky_dirty && !(reg[VR_SHEILACTL] & 1) && !jimgfx_active();
    vicky_dirty = 0; vicky_low = reads_low();
    glass_latch();
    { int i = glass_scale ? vicky_idr_of_scale(glass_scale) : -1;     /* an IDR at an even scale, the doubled frame in the limits */
      int on = !(hd_stock && hd_stock8 && glass_hd && i >= 0) ? 0
             : idrs[i].hd == 2 && hd_font && hd_font16 ? 2 : idrs[i].hd == 3 && hd_font48 && hd_font24 ? 3 : 0;
      if (on != hd_on) { hd_on = on; frame_skip = 0; }
      hd_ok_data[0] = hd_ok_data[1] = 0xFFFFFFFFu; }                             /* a program may have changed a glyph since */
    memset(col_ss, 0, 16); memset(col_sl, 0, 16);
    sh_pc = rd32(&reg[VR_SHEILA]); sh_wait = (reg[VR_SHEILACTL] & 1) ? -1 : -2;
}

/* One machine line of an HD frame: composed at the glass as ever (with hd_src
 * kept), then written as K lines of K times the width -- each pixel K x K,
 * unless a text32 cell is on top whose glyph is the stock one: then the HD
 * glyph's K rows and K columns for it.  K is 2 (16-wide glyphs, two bytes a
 * row) or 3 (24-wide, three bytes; 2026-10-07, for 480x360). */
static void hd_line_draw(int y, uint8_t ctrl)
{
    const int w = glass_w, K = hd_on, GW = 8 * K;
    const unsigned full = (1u << GW) - 1, bar = ((1u << (GW / 4)) - 1) << (GW - GW / 4);
    uint8_t *o[3];
    for (int s = 0; s < K; s++) o[s] = frame_fb + (size_t)(K * y + s) * frame_pitch;
    memset(hd_line, reg[VR_BGCOL], (size_t) w); memset(hd_src, 0, (size_t) w);
    if (ctrl & 1) {
        memset(owner, 0, (size_t) w); memset(layer_hit, 0, (size_t) w);
        sprites_gather(y);
        for (int n = 0; n < VICKY_LAYERS; n++) {
            if (reg[VR_LAYER(n) + VL_CTRL] & 1) layer_line(n, y, hd_line, w);
            sprites_line(n, y, hd_line, w);
        }
    }
    for (int x = 0; x < w; x++) {
        uint8_t p = hd_line[x];
        int k = hd_src[x] - 1;                                   /* 0: an 8x16 cell, 1: an 8x8 one */
        if (k < 0 || hd_g[x] > 255) { for (int s = 0; s < K; s++) memset(o[s] + K * x, p, (size_t) K); continue; }
        int H = k ? 8 : 16; const uint8_t *st = k ? hd_stock8 : hd_stock;
        if (hd_data[x] != hd_ok_data[k]) {                       /* which glyphs in RAM are the stock ones: once a frame */
            hd_ok_data[k] = hd_data[x];
            for (int g = 0; g < 256; g++) { hd_ok[k][g] = 1;
                for (int r = 0; r < H; r++) if (ram(hd_ok_data[k] + (uint32_t)(g * H + r)) != st[g * H + r]) { hd_ok[k][g] = 0; break; } }
        }
        if (!hd_ok[k][hd_g[x]]) { for (int s = 0; s < K; s++) memset(o[s] + K * x, p, (size_t) K); continue; }
        const uint8_t *font = K == 3 ? (k ? hd_font24 : hd_font48) : (k ? hd_font16 : hd_font);
        const uint8_t *gr = font + ((size_t) hd_g[x] * (size_t)(K * H) + (size_t) hd_gy[x] * K) * K;   /* the HD glyph's K rows, K bytes each */
        uint8_t fg = hd_fg[x], bg = hd_bg[x];
        unsigned m = hd_cur[x] == 2 ? full : hd_cur[x] ? bar : 0;   /* the shaped cursor at the HD size */
        /* A whole cell with nothing over it -- the common case, a line of text:
         * its K glyph rows once, every pixel of each (2026-10-07: the test
         * below, made once a pixel, cost HD text 13-50% over the same glyphs
         * drawn as 16-wide cells). */
        if (hd_gx[x] == 0 && x + 8 <= w) {
            int whole = 1;
            for (int i = 1; i < 8 && whole; i++)
                whole = hd_src[x + i] == hd_src[x] && hd_g[x + i] == hd_g[x] && hd_gx[x + i] == i && hd_cur[x + i] == hd_cur[x]
                        && hd_fg[x + i] == hd_fg[x] && hd_bg[x + i] == hd_bg[x];
            if (whole) {
                for (int s = 0; s < K; s++) {
                    const uint8_t *r = gr + s * K; unsigned bits = K == 3 ? (unsigned) r[0] << 16 | (unsigned) r[1] << 8 | r[2] : (unsigned) r[0] << 8 | r[1];
                    bits ^= m; uint8_t *d = o[s] + K * x;
                    for (int b = 0; b < GW; b++) d[b] = (bits >> (GW - 1 - b)) & 1 ? fg : bg;
                }
                x += 7; continue;
            }
        }
        for (int s = 0; s < K; s++) {
            const uint8_t *r = gr + s * K; unsigned bits = K == 3 ? (unsigned) r[0] << 16 | (unsigned) r[1] << 8 | r[2] : (unsigned) r[0] << 8 | r[1];
            bits ^= m; uint8_t *d = o[s] + K * x;
            for (int t = 0; t < K; t++) d[t] = (bits >> (GW - 1 - (hd_gx[x] * K + t))) & 1 ? fg : bg;
        }
    }
}

void vicky_line(int y)
{
    cur_line = y;
    sheila_run(y);
    if (y == raster_cmp) reg[VR_IRQSTAT] |= VI_RASTER;
    if (__builtin_expect(frame_skip || term_hold(), 0)) return;   /* an idle frame, or a synchronized update in progress: the line stays as the last frame drew it */
    uint8_t *line = frame_fb + y * frame_pitch;
    uint8_t ctrl = reg[VR_CTRL];
    line_band = 0;
    /* bit1: columns halved (320); bit2: lines halved (240); bit3: a 200-line
     * field, 40 blank lines above and below it; bit4: columns quartered (160). */
    if (hd_on) {                                     /* the glass drawn at 2x or 3x, text from the HD font */
        if (y >= glass_h) return;
        hd_line_draw(y, ctrl);
        for (int s = 0; s < hd_on; s++) if (hd_on * y + s < VICKY_HEIGHT) band_out[hd_on * y + s] = line_band;
        return;
    }
    if (glass_hd) {                                  /* the HD family: its own size, nothing doubled */
        if (y >= glass_h) return;
        memset(line, reg[VR_BGCOL], (size_t) glass_w);
        band_out[y] = 0;
        if (!(ctrl & 1)) return;
        memset(owner, 0, (size_t) glass_w); memset(layer_hit, 0, (size_t) glass_w);
        sprites_gather(y);
        for (int n = 0; n < VICKY_LAYERS; n++) {
            if (reg[VR_LAYER(n) + VL_CTRL] & 1) layer_line(n, y, line, glass_w);
            sprites_line(n, y, line, glass_w);
        }
        band_out[y] = line_band;
        return;
    }
    int top = (ctrl & 8) ? (OLD_H - 400) / 2 : 0;
    band_out[y] = 0;
    if (y < top || y >= OLD_H - top) { memset(line, reg[VR_BGCOL], OLD_W); return; }
    int yy = y - top;
    if (ctrl & 6) {
        int half = ctrl & 2;
        int q = (ctrl & 16) ? 4 : 2;                           /* screen pixels per pixel of the machine */
        int w = half ? OLD_W / q : OLD_W;
        if (yy & 1) { memcpy(line, line - frame_pitch, OLD_W); band_out[y] = y ? band_out[y - 1] : 0; return; }
        uint8_t *dst = half ? lowres_tmp : line;
        memset(dst, reg[VR_BGCOL], OLD_W);
        if (ctrl & 1) {
            memset(owner, 0, OLD_W); memset(layer_hit, 0, OLD_W);
            sprites_gather(yy >> 1);
            for (int n = 0; n < VICKY_LAYERS; n++) {
                if (reg[VR_LAYER(n) + VL_CTRL] & 1) layer_line(n, yy >> 1, dst, w);
                sprites_line(n, yy >> 1, dst, w);
            }
        }
        if (half) { for (int x = 0; x < w; x++)
                        for (int i = 0; i < q; i++) line[x * q + i] = lowres_tmp[x]; }
        band_out[y] = line_band;
        return;
    }
    memset(line, reg[VR_BGCOL], OLD_W);
    if (!(ctrl & 1)) return;
    memset(owner, 0, OLD_W); memset(layer_hit, 0, OLD_W);
    sprites_gather(yy);
    for (int n = 0; n < VICKY_LAYERS; n++) {
        if (reg[VR_LAYER(n) + VL_CTRL] & 1) layer_line(n, yy, line, OLD_W);
        sprites_line(n, yy, line, OLD_W);
    }    band_out[y] = line_band;
}

/* Draw the picture again from RAM without moving the machine on.
 * The F12 menu freezes the machine, so nothing redraws when a setting changes
 * the way the picture is built -- the chargen at $010000 is the one that
 * matters, and a font you cannot see until you close the menu is no preview.
 * Rasterising has side effects (the raster line, raster and SHEILA IRQ flags,
 * and SHEILA's display list writes registers), so every guest-visible byte is
 * put back afterwards: the freeze stays a freeze and only fb changes. */
void vicky_repaint(uint8_t *fb, int pitch)
{
    uint8_t sreg[sizeof reg]; memcpy(sreg, reg, sizeof reg);
    uint8_t sss[16], ssl[16]; memcpy(sss, col_ss, 16); memcpy(ssl, col_sl, 16);
    uint8_t *sfb = frame_fb; int spitch = frame_pitch, sline = cur_line, swait = sh_wait;
    uint32_t spc = sh_pc; uint16_t scmp = raster_cmp;
    int sskip = frame_skip, sdirty = vicky_dirty;
    repainting = 1; vicky_begin_frame(fb, pitch); repainting = 0;
    frame_skip = 0;                                  /* a repaint draws, whatever */
    for (int y = 0; y < glass_h; y++) vicky_line(y);
    memcpy(reg, sreg, sizeof reg); memcpy(col_ss, sss, 16); memcpy(col_sl, ssl, 16);
    frame_fb = sfb; frame_pitch = spitch; cur_line = sline; sh_wait = swait; sh_pc = spc; raster_cmp = scmp;
    frame_skip = sskip; vicky_dirty = sdirty;
}

/* The end of a synchronized update (ESC [ ? 2026 l, core/term.c): the whole
 * picture drawn now, from RAM as it stands, into this frame's buffer.  The
 * lines the hold skipped would otherwise wait for the next frame -- and a
 * program that holds again at once (the next key is already queued) would
 * never let one be drawn. */
void vicky_commit(void) { if (frame_fb) vicky_repaint(frame_fb, frame_pitch); }

void vicky_end_frame(void)
{
    int any = 0;
    for (int i = 0; i < 16; i++) { reg[VR_COLSS + i] |= col_ss[i]; reg[VR_COLSL + i] |= col_sl[i]; any |= col_ss[i] | col_sl[i]; }
    if (any) reg[VR_IRQSTAT] |= VI_COLL;
    reg[VR_IRQSTAT] |= VI_VBLANK;
    cur_line = glass_h;        /* vblank */
    io_frame_tick();
}

void vicky_render(uint8_t *fb, int pitch)
{
    vicky_begin_frame(fb, pitch);
    for (int y = 0; y < glass_h; y++) vicky_line(y);
    vicky_end_frame();
}

/* ---- save states (core/state.h) ------------------------------------------ */
#include "state.h"
void vicky_state_save(FILE *f)
{
    state_put(f, "VREG", reg, sizeof reg);
    state_put(f, "VPAL", pal, sizeof pal);
    state_put(f, "VRAS", &raster_cmp, sizeof raster_cmp);
    state_put(f, "VSHP", &sh_pc, sizeof sh_pc);
    state_put(f, "VSHW", &sh_wait, sizeof sh_wait);
}
int vicky_state_load(FILE *f)
{
    if (state_get(f, "VREG", reg, sizeof reg) || state_get(f, "VPAL", pal, sizeof pal) || state_get(f, "VRAS", &raster_cmp, sizeof raster_cmp)
        || state_get(f, "VSHP", &sh_pc, sizeof sh_pc) || state_get(f, "VSHW", &sh_wait, sizeof sh_wait)) return -2;
    memset(col_ss, 0, sizeof col_ss); memset(col_sl, 0, sizeof col_sl);
    pal_gen++; vicky_dirty = 1;
    return 0;
}
