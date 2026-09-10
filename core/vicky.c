#include "vicky.h"
#include "mem.h"
#include "io.h"
#include <string.h>
#include <stdlib.h>

static uint8_t  reg[256];
static uint32_t pal[256];
static int      cur_line;
static uint8_t  col_ss[16], col_sl[16];     /* collision accumulators for the frame in progress */
static uint8_t *frame_fb; static int frame_pitch;
static uint32_t sh_pc; static int sh_wait;   /* -1 = not waiting, -2 = ended */
static uint16_t raster_cmp;
static uint8_t  owner[VICKY_WIDTH];          /* per-pixel: 0 = layers only, else sprite n+1 */
static uint8_t  layer_hit[VICKY_WIDTH];
static uint8_t  lowres_tmp[VICKY_WIDTH];     /* CTRL bit1: 320x240 rendered here, then doubled */      /* per-pixel: a layer drew a non-zero index here */

static const uint32_t c64_palette[16] = {   /* VIC-II colours as the first 16 entries (spec §2) */
    0x000000, 0xFFFFFF, 0x880000, 0xAAFFEE, 0xCC44CC, 0x00CC55, 0x0000AA, 0xEEEE77,
    0xDD8855, 0x664400, 0xFF7777, 0x333333, 0x777777, 0xAAFF66, 0x0088FF, 0xBBBBBB,
};

void vicky_reset(void)
{
    memset(reg, 0, sizeof reg);
    for (int i = 0; i < 256; i++) pal[i] = (i < 16) ? c64_palette[i] : (uint32_t)(i * 0x010101);
    cur_line = 0;
    sh_wait = -2; raster_cmp = 0xFFFF;
}

static void blit(void);
uint32_t vicky_palette_rgb(int i) { return pal[i & 0xFF]; }

uint8_t vicky_read(uint8_t r)
{
    switch (r) {
    case VR_RASTER:     return cur_line & 0xFF;
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
    reg[r] = v;
    if (r == VR_PALB) {
        uint8_t i = reg[VR_PALIDX];
        pal[i] = ((uint32_t)reg[VR_PALR] << 16) | ((uint32_t)reg[VR_PALG] << 8) | v;
        reg[VR_PALIDX] = i + 1;
    }
}

static inline uint32_t rd32(const uint8_t *p) { return (p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24)) & K4510_PHYS_MASK; }
static inline uint16_t rd16(const uint8_t *p) { return p[0] | (p[1] << 8); }
static inline uint8_t  ram(uint32_t a) { return k4510_ram[a & K4510_PHYS_MASK]; }
#define ram_ptr(a) k4510_ram[(a) & K4510_PHYS_MASK]

/* Render one scanline of one layer into line[], honouring transparency.
 * opaque: this is the lowest enabled layer, so index 0 is drawn too. */
static uint32_t cur_at; static int cur_style, cur_on;      /* JIM's shaped cursor */
void vicky_cursor(uint32_t attr_addr, int style, int on) { cur_at = attr_addr; cur_style = style; cur_on = on && style; }

static void layer_line(int n, int y, uint8_t *line, int opaque)
{
    const uint8_t *L = &reg[VR_LAYER(n)];
    int mode  = (L[VL_CTRL] >> 1) & 3;
    int depth = (L[VL_CTRL] >> 3) & 3;          /* 0..3 -> 1,2,4,8 bpp */
    int csz   = (L[VL_CTRL] >> 5) & 3;          /* cell size field */
    int bpp   = 1 << depth;
    uint8_t  palofs = L[VL_PALOFS];
    uint32_t data   = rd32(&L[VL_DATA]);
    uint32_t map    = rd32(&L[VL_MAP]);
    uint16_t stride = rd16(&L[VL_STRIDE]);
    int sy  = y + rd16(&L[VL_SCROLLY]);
    int sx0 = rd16(&L[VL_SCROLLX]);
    uint8_t mask = (uint8_t)((1 << bpp) - 1);

    if (mode == VL_MODE_BITMAP) {
        int ppb = 8 / bpp;
        uint8_t base = (uint8_t)(palofs << bpp);
        uint32_t row = data + (uint32_t)sy * stride;
        for (int x = 0; x < VICKY_WIDTH; x++) {
            int sx = x + sx0;
            uint8_t b = ram(row + sx / ppb);
            int shift = (bpp == 8) ? 0 : (8 - bpp - (sx % ppb) * bpp);
            uint8_t pix = (b >> shift) & mask;
            if (pix || opaque) { line[x] = (bpp == 8) ? pix : (uint8_t)(base | pix); if (pix) layer_hit[x] = 1; }
        }
        return;
    }
    if (mode == VL_MODE_TILE) {
        int size = 8 << csz;                               /* 8,16,32,64 */
        int tbytes = size * size * bpp / 8;
        int rowbytes = size * bpp / 8;
        int cy = sy / size, ty = sy % size;
        for (int x = 0; x < VICKY_WIDTH; ) {
            int sx = x + sx0;
            int cx = sx / size, tx0 = sx % size;
            uint32_t e = map + ((uint32_t)cy * stride + cx) * 2;
            uint16_t ent = ram(e) | (ram(e + 1) << 8);
            int idx = ent & 0x3FF, hf = ent & 0x400, vf = ent & 0x800;
            uint8_t base = (uint8_t)((ent >> 12) << bpp);
            int ry = vf ? (size - 1 - ty) : ty;
            uint32_t trow = data + (uint32_t)idx * tbytes + (uint32_t)ry * rowbytes;
            for (int tx = tx0; tx < size && x < VICKY_WIDTH; tx++, x++) {
                int px = hf ? (size - 1 - tx) : tx;
                uint8_t b = ram(trow + px * bpp / 8);
                int shift = (bpp == 8) ? 0 : (8 - bpp - (px % (8 / bpp)) * bpp);
                uint8_t pix = (b >> shift) & mask;
                if (pix || opaque) { line[x] = (bpp == 8) ? pix : (uint8_t)(base | pix); if (pix) layer_hit[x] = 1; }
            }
        }
        return;
    }
    /* text modes: 1-bpp glyphs, 8 px wide, H = 8 or 16 rows */
    int H = csz ? 16 : 8;
    int cy = sy / H, gy = sy % H;
    if (mode == VL_MODE_TEXT) {
        uint8_t base = (uint8_t)(palofs << 1);
        for (int x = 0; x < VICKY_WIDTH; ) {
            int sx = x + sx0, cx = sx >> 3, gx0 = sx & 7;
            uint8_t cell = ram(map + (uint32_t)cy * stride + cx);
            uint8_t row  = ram(data + (uint32_t)cell * H + gy);
            for (int gx = gx0; gx < 8 && x < VICKY_WIDTH; gx++, x++) {
                uint8_t pix = (row >> (7 - gx)) & 1;
                if (pix || opaque) { line[x] = (uint8_t)(base | pix); if (pix) layer_hit[x] = 1; }
            }
        }
        return;
    }
    /* text32 */
    for (int x = 0; x < VICKY_WIDTH; ) {
        int sx = x + sx0, cx = sx >> 3, gx0 = sx & 7;
        uint32_t e = map + ((uint32_t)cy * stride + cx) * 4;
        uint16_t g = ram(e) | ((ram(e + 1) & 0x7F) << 8);
        int rev = ram(e + 1) & 0x80;
        uint8_t fg = ram(e + 2), bg = ram(e + 3);
        if (rev) { uint8_t t = fg; fg = bg; bg = t; }
        uint8_t row = ram(data + (uint32_t)g * H + gy);
        /* the shaped cursor (vicky_cursor): an underline reverses this cell's
         * bottom two rows, a bar its left two columns */
        int cur = cur_on && e + 1 == cur_at;
        if (cur && cur_style == 1 && gy < H - 2) cur = 0;
        for (int gx = gx0; gx < 8 && x < VICKY_WIDTH; gx++, x++) {
            int sw = cur && (cur_style == 1 || gx < 2);
            line[x] = (((row >> (7 - gx)) & 1) != 0) != (sw != 0) ? fg : bg;
        }
        layer_hit[x] = 1;
    }
}

/* Draw every enabled sprite with Z == z that covers line y. */
static void sprites_line(int z, int y, uint8_t *line)
{
    if (!(reg[VR_SPRCTL] & 1)) return;
    uint32_t tab = rd32(&reg[VR_SPRTAB]);
    for (int n = 0; n < VICKY_SPRITES; n++) {
        uint32_t e = tab + (uint32_t)n * 16;
        uint8_t ctrl = ram(e + 8);
        if (!(ctrl & 1) || ((ctrl >> 4) & 3) != z) continue;
        int16_t sxp = (int16_t)(ram(e) | (ram(e + 1) << 8));
        int16_t syp = (int16_t)(ram(e + 2) | (ram(e + 3) << 8));
        uint8_t size = ram(e + 9);
        int w = 8 << (size & 3), h = 8 << ((size >> 2) & 3);
        int ry = y - syp;
        if (ry < 0 || ry >= h) continue;
        int bpp = (ctrl & 2) ? 8 : 4;
        int rowbytes = w * bpp / 8;
        uint32_t data = rd32(&ram_ptr(e + 4));
        if (ctrl & 8) ry = h - 1 - ry;                  /* V-flip */
        uint32_t row = data + (uint32_t)ry * rowbytes;
        uint8_t base = (uint8_t)(ram(e + 10) << 4);
        for (int px = 0; px < w; px++) {
            int x = sxp + px;
            if (x < 0 || x >= VICKY_WIDTH) continue;
            int sp = (ctrl & 4) ? (w - 1 - px) : px;    /* H-flip */
            uint8_t b = ram(row + sp * bpp / 8);
            uint8_t pix = (bpp == 8) ? b : ((sp & 1) ? (b & 0x0F) : (b >> 4));
            if (!pix) continue;
            if (owner[x]) { int o = owner[x] - 1; col_ss[o >> 3] |= 1 << (o & 7); col_ss[n >> 3] |= 1 << (n & 7); }
            else owner[x] = (uint8_t)(n + 1);
            if (layer_hit[x]) col_sl[n >> 3] |= 1 << (n & 7);
            line[x] = (bpp == 8) ? pix : (uint8_t)(base | pix);
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
            if (xl <= xr) memset(&ram_ptr(dst + (uint32_t)y * ds + xl), fill, xr - xl + 1);
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

void vicky_begin_frame(uint8_t *fb, int pitch)
{
    frame_fb = fb; frame_pitch = pitch;
    memset(col_ss, 0, 16); memset(col_sl, 0, 16);
    sh_pc = rd32(&reg[VR_SHEILA]); sh_wait = (reg[VR_SHEILACTL] & 1) ? -1 : -2;
}

void vicky_line(int y)
{
    cur_line = y;
    sheila_run(y);
    if (y == raster_cmp) reg[VR_IRQSTAT] |= VI_RASTER;
    uint8_t *line = frame_fb + y * frame_pitch;
    uint8_t ctrl = reg[VR_CTRL];
    /* bit1: columns halved (320); bit2: lines halved (240); bit3: a 200-line
     * field, 40 blank lines above and below it; bit4: columns quartered (160). */
    int top = (ctrl & 8) ? (VICKY_HEIGHT - 400) / 2 : 0;
    if (y < top || y >= VICKY_HEIGHT - top) { memset(line, reg[VR_BGCOL], VICKY_WIDTH); return; }
    int yy = y - top;
    if (ctrl & 6) {
        int half = ctrl & 2;
        if (yy & 1) { memcpy(line, line - frame_pitch, VICKY_WIDTH); return; }
        uint8_t *dst = half ? lowres_tmp : line;
        memset(dst, reg[VR_BGCOL], VICKY_WIDTH);
        if (ctrl & 1) {
            memset(owner, 0, VICKY_WIDTH); memset(layer_hit, 0, VICKY_WIDTH);
            for (int n = 0; n < VICKY_LAYERS; n++) {
                if (reg[VR_LAYER(n) + VL_CTRL] & 1) layer_line(n, yy >> 1, dst, 0);
                sprites_line(n, yy >> 1, dst);
            }
        }
        if (half) { int q = (ctrl & 16) ? 4 : 2;             /* screen pixels per pixel of the machine */
                    for (int x = 0; x < VICKY_WIDTH / q; x++)
                        for (int i = 0; i < q; i++) line[x * q + i] = lowres_tmp[x]; }
        return;
    }
    memset(line, reg[VR_BGCOL], VICKY_WIDTH);
    if (!(ctrl & 1)) return;
    memset(owner, 0, VICKY_WIDTH); memset(layer_hit, 0, VICKY_WIDTH);
    for (int n = 0; n < VICKY_LAYERS; n++) {
        if (reg[VR_LAYER(n) + VL_CTRL] & 1) layer_line(n, yy, line, 0);
        sprites_line(n, yy, line);
    }
}

/* Draw the picture again from RAM without moving the machine on.
 * The F7 menu freezes the machine, so nothing redraws when a setting changes
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
    vicky_begin_frame(fb, pitch);
    for (int y = 0; y < VICKY_HEIGHT; y++) vicky_line(y);
    memcpy(reg, sreg, sizeof reg); memcpy(col_ss, sss, 16); memcpy(col_sl, ssl, 16);
    frame_fb = sfb; frame_pitch = spitch; cur_line = sline; sh_wait = swait; sh_pc = spc; raster_cmp = scmp;
}

void vicky_end_frame(void)
{
    int any = 0;
    for (int i = 0; i < 16; i++) { reg[VR_COLSS + i] |= col_ss[i]; reg[VR_COLSL + i] |= col_sl[i]; any |= col_ss[i] | col_sl[i]; }
    if (any) reg[VR_IRQSTAT] |= VI_COLL;
    reg[VR_IRQSTAT] |= VI_VBLANK;
    cur_line = VICKY_HEIGHT;   /* vblank */
    io_frame_tick();
}

void vicky_render(uint8_t *fb, int pitch)
{
    vicky_begin_frame(fb, pitch);
    for (int y = 0; y < VICKY_HEIGHT; y++) vicky_line(y);
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
    return 0;
}
