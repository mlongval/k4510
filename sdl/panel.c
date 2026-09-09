#include "panel.h"
#include <stdio.h>
#include <string.h>
#include "../core/xemu/emutools_basicdefs.h"
#include "../core/xemu/cpu65.h"
#include "../core/io.h"
#include "../core/vicky.h"
#include "../core/ui/settings.h"
#include "panel_ops.h"

static uint32_t *buf; static int bw, bh, bpitch, gscale; static const uint8_t *gfont;
#define C_BG    0xFF101820u
#define C_TEXT  0xFFD8D8C0u
#define C_DIM   0xFF7A8090u
#define C_HEAD  0xFFF0C040u
#define C_PC    0xFF80E0A0u

static void put(int row, int col, const char *s, uint32_t colour)
{
    int cell = 8 * gscale;
    for (; *s; s++, col++) {
        const uint8_t *gl = gfont + (uint8_t)*s * 8;
        int x0 = col * cell, y0 = row * cell;
        if (x0 + cell > bw || y0 + cell > bh) return;
        for (int y = 0; y < 8; y++) {
            for (int x = 0; x < 8; x++) {
                if (!(gl[y] & (0x80 >> x))) continue;
                for (int dy = 0; dy < gscale; dy++)
                    for (int dx = 0; dx < gscale; dx++)
                        buf[(y0 + y * gscale + dy) * bpitch + x0 + x * gscale + dx] = colour;
            } }
    }
}
static void rule(int row, int col, const char *title, int cols)
{
    char line[96]; int n = 0;
    line[n++] = ' ';
    for (const char *t = title; *t && n < 90; t++) line[n++] = *t;
    line[n++] = ' ';
    while (n < cols && n < 94) line[n++] = (char)0xC4;         /* CP437's horizontal bar */
    line[n] = 0;
    put(row, col, line, C_HEAD);
}
/* the CPU's view, but never through the I/O page: reading $D100 pops the
 * keyboard, and the panel must not be a side effect */
static int peek(uint16_t a, uint8_t *v)
{
    if (a >= 0xD000 && a < 0xE000) return 0;
    *v = cpu65_read_callback(a); return 1;
}
static int disasm(uint16_t pc, char *out, int outmax)
{
    uint8_t op, b1 = 0, b2 = 0; char arg[24] = "";
    if (!peek(pc, &op)) { snprintf(out, outmax, "%04X  (I/O page)", pc); return 1; }
    int mn = op_tab[op].mn, md = op_tab[op].mode, len = 1;
    if (mn == 255) { snprintf(out, outmax, "%04X  %02X        ???", pc, op); return 1; }
    switch (md) {
    case M_IMP: break;
    case M_ACC: snprintf(arg, sizeof arg, "A"); break;
    case M_IMM: case M_ZP: case M_ZPX: case M_ZPY: case M_IZP: case M_INX: case M_INY: case M_ISY: case M_REL: len = 2; break;
    default: len = 3; break;
    }
    if (len >= 2) peek(pc + 1, &b1);
    if (len == 3) peek(pc + 2, &b2);
    uint16_t w = b1 | (b2 << 8);
    switch (md) {
    case M_IMM:  snprintf(arg, sizeof arg, "#$%02X", b1); break;
    case M_WIMM: snprintf(arg, sizeof arg, "#$%04X", w); break;
    case M_ZP:   snprintf(arg, sizeof arg, "$%02X", b1); break;
    case M_ZPX:  snprintf(arg, sizeof arg, "$%02X,X", b1); break;
    case M_ZPY:  snprintf(arg, sizeof arg, "$%02X,Y", b1); break;
    case M_ABS:  snprintf(arg, sizeof arg, "$%04X", w); break;
    case M_ABX:  snprintf(arg, sizeof arg, "$%04X,X", w); break;
    case M_ABY:  snprintf(arg, sizeof arg, "$%04X,Y", w); break;
    case M_IND:  snprintf(arg, sizeof arg, "($%04X)", w); break;
    case M_IAX:  snprintf(arg, sizeof arg, "($%04X,X)", w); break;
    case M_INX:  snprintf(arg, sizeof arg, "($%02X,X)", b1); break;
    case M_INY:  snprintf(arg, sizeof arg, "($%02X),Y", b1); break;
    case M_IZP:  snprintf(arg, sizeof arg, "($%02X),Z", b1); break;
    case M_ISY:  snprintf(arg, sizeof arg, "($%02X,SP),Y", b1); break;
    case M_REL:  snprintf(arg, sizeof arg, "$%04X", (uint16_t)(pc + 2 + (int8_t)b1)); break;
    case M_REL16: snprintf(arg, sizeof arg, "$%04X", (uint16_t)(pc + 2 + (int16_t)w)); break;
    default: break;
    }
    char bytes[12];
    if (len == 1) snprintf(bytes, sizeof bytes, "%02X      ", op);
    else if (len == 2) snprintf(bytes, sizeof bytes, "%02X %02X   ", op, b1);
    else snprintf(bytes, sizeof bytes, "%02X %02X %02X", op, b1, b2);
    snprintf(out, outmax, "%04X  %s  %s %s", pc, bytes, op_mnem[mn], arg);
    return len;
}

void panel_render(uint32_t *px, int pitch_px, int w, int h, int g, const uint8_t *font, const panel_info *info)
{
    buf = px; bw = w; bh = h; bpitch = pitch_px; gscale = g > 0 ? g : 1; gfont = font;
    int cols = w / (8 * gscale), rows = h / (8 * gscale), r = 0; char t[96];
    for (int y = 0; y < h; y++) { for (int x = 0; x < w; x++) px[y * pitch_px + x] = C_BG; }
    if (cols < 20 || rows < 10) return;
    if (cols > 94) cols = 94;

    snprintf(t, sizeof t, "K4510  %s", info->host); put(r, 1, t, C_HEAD);
    snprintf(t, sizeof t, "%5.1f fps", info->fps); put(r++, cols - 10, t, C_DIM);
    snprintf(t, sizeof t, "CPU %.1f MHz   frame %u", info->cpu_hz / 1e6, io_read(0xD50D) | (io_read(0xD50E) << 8)); put(r++, 1, t, C_DIM);
    r++;

    rule(r++, 0, "CPU", cols);
    { uint8_t pf = cpu65_get_pf(); char fl[9]; const char *names = "NVEBDIZC";
      for (int b = 0; b < 8; b++) { fl[b] = (pf & (0x80 >> b)) ? names[b] : '.'; }
      fl[8] = 0;
      snprintf(t, sizeof t, "PC $%04X  A $%02X  X $%02X  Y $%02X  Z $%02X", cpu65.pc, cpu65.a, cpu65.x, cpu65.y, cpu65.z); put(r++, 1, t, C_TEXT);
      snprintf(t, sizeof t, "SP $%04X  B $%02X  P %s", cpu65.s | cpu65.sphi, cpu65.bphi, fl); put(r++, 1, t, C_TEXT); }
    { uint16_t a = cpu65.pc;
      for (int i = 0; i < 8 && r < rows - 1; i++) { int n = disasm(a, t, sizeof t); put(r++, 1, t, i ? C_TEXT : C_PC); a += n; } }
    r++;

    rule(r++, 0, "VICKY", cols);
    { uint8_t c = vicky_read(VR_CTRL); const char *name = "?";
      switch (c & 0x1F) { case 1: name = "640x480"; break; case 5: name = "640x240"; break; case 3: name = "320x240"; break;
                          case 11: name = "320x200"; break; case 27: name = "160x200"; break; case 13: name = "640x200"; break; case 0: name = "off"; break; }
      snprintf(t, sizeof t, "ctrl $%02X  %s  raster %3u", c, name, vicky_read(0x0A)); put(r++, 1, t, C_TEXT); }
    r++;

    rule(r++, 0, "BANKS  $D600", cols);
    for (int i = 0; i < 8 && r < rows - 1; i++) {
        uint32_t v = io_read(0xD600 + i * 4) | (io_read(0xD601 + i * 4) << 8) | ((uint32_t)io_read(0xD602 + i * 4) << 16);
        uint8_t on = io_read(0xD603 + i * 4);
        snprintf(t, sizeof t, "%d $%04X  %s $%06X", i, i * 0x2000, on ? "->" : "  ", v); put(r++, 1, t, on ? C_TEXT : C_DIM);
    }
    r++;

    if (r < rows - 2) {
        rule(r++, 0, "AUDIO", cols);
        snprintf(t, sizeof t, "gaps %u   volume %d", io_audio_gaps, settings_get(SET_AUDIO_VOLUME)); put(r++, 1, t, C_TEXT);
    }
}
