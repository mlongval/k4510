#include "panel.h"
#include <stdio.h>
#include <string.h>
#include "../core/xemu/emutools_basicdefs.h"
#include "../core/xemu/cpu65.h"
#include "../core/io.h"
#include "../core/vicky.h"
#include "../core/ui/settings.h"
#include "panel_ops.h"

static uint32_t *buf; static int bw, bh, bpitch, gscale, grows; static const uint8_t *gfont;
#define C_BG    0xFF101820u
#define C_TEXT  0xFFD8D8C0u
#define C_DIM   0xFF7A8090u
#define C_HEAD  0xFFF0C040u
#define C_PC    0xFF80E0A0u

static void put(int row, int col, const char *s, uint32_t colour)
{
    int cell = 8 * gscale, cellh = grows * gscale;
    for (; *s; s++, col++) {
        const uint8_t *gl = gfont + (uint8_t)*s * grows;
        int x0 = col * cell, y0 = row * cellh;
        if (x0 + cell > bw || y0 + cellh > bh) return;
        for (int y = 0; y < grows; y++) {
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
    if (!peek(pc, &op)) { snprintf(out, outmax, "%04X (I/O page)", pc); return 1; }
    int mn = op_tab[op].mn, md = op_tab[op].mode, len = 1;
    if (mn == 255) { snprintf(out, outmax, "%04X %02X     ???", pc, op); return 1; }
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
    char bytes[8];
    if (len == 1) snprintf(bytes, sizeof bytes, "%02X", op);
    else if (len == 2) snprintf(bytes, sizeof bytes, "%02X%02X", op, b1);
    else snprintf(bytes, sizeof bytes, "%02X%02X%02X", op, b1, b2);
    snprintf(out, outmax, "%04X %-6s %s %s", pc, bytes, op_mnem[mn], arg);
    return len;
}
int panel_disasm(uint16_t pc, char *out, int outmax) { return disasm(pc, out, outmax); }

int panel_scale(int w, int h, int rows)
{
    int g = w / (8 * PANEL_COLS), gh = h / (rows * 30);
    if (gh < g) g = gh;
    return g < 1 ? 1 : g > 4 ? 4 : g;
}

void panel_render(uint32_t *px, int pitch_px, int w, int h, int g, const uint8_t *font, int frows, const panel_info *info)
{
    buf = px; bw = w; bh = h; bpitch = pitch_px; gscale = g > 0 ? g : 1; gfont = font; grows = frows;
    int cols = w / (8 * gscale), rows = h / (grows * gscale), r = 0; char t[96];
    for (int y = 0; y < h; y++) { for (int x = 0; x < w; x++) px[y * pitch_px + x] = C_BG; }
    if (cols < 20 || rows < 10) return;
    if (cols > PANEL_COLS) cols = PANEL_COLS;
    /* What fits: everything but the disassembly is a fixed number of rows,
     * and the disassembly takes what is left (3 to 16 instructions).  On a
     * short window the audio line goes first, then the idle banks. */
    int active_banks = 0;
    for (int i = 0; i < 8; i++) if (io_read(0xD603 + i * 4)) active_banks++;
    int need = 4 + 1 + 4 + 1 + 1 + 1 + 3 + 1 + 9 + 1 + 2;          /* head, CPU, NEXT's rule, VICKY, BANKS, AUDIO, with blanks */
    if (info->paused) need += 10;                                    /* the rule, six keys, two status lines, a blank */
    int show_audio = 1, all_banks = 1;
    if (rows - need < 3) { show_audio = 0; need -= 3; }
    if (rows - need < 3) { all_banks = 0; need -= 8 - active_banks; }
    int nnext = rows - need; if (nnext < 3) nnext = 3; if (nnext > 16) nnext = 16;

    snprintf(t, sizeof t, "K4510 %s", info->host); put(r++, 1, t, C_HEAD);
    snprintf(t, sizeof t, "%5.1f fps  %.1f MHz", info->fps, info->cpu_hz / 1e6); put(r++, 1, t, C_DIM);
    snprintf(t, sizeof t, "frame %u", io_read(0xD50D) | (io_read(0xD50E) << 8) | (io_read(0xD50F) << 16)); put(r++, 1, t, C_DIM);
    if (info->paused) { snprintf(t, sizeof t, "PAUSED  at line %d", info->line); put(r, 1, t, C_HEAD); }
    r++;
    r++;

    if (info->paused) {
        rule(r++, 0, "DEBUG", cols);
        put(r++, 1, "F8     run", C_TEXT);
        put(r++, 1, "Space  one instruction", C_TEXT);
        put(r++, 1, "L      one scanline", C_TEXT);
        put(r++, 1, "F      one frame", C_TEXT);
        put(r++, 1, "D      dump to dumps/", C_TEXT);
        put(r++, 1, "T      trace on/off", C_TEXT);
        if (info->trace_on) snprintf(t, sizeof t, "trace  ON, %u lines", info->trace_lines);
        else if (info->trace_lines) snprintf(t, sizeof t, "trace  off, %u lines", info->trace_lines);
        else snprintf(t, sizeof t, "trace  off");
        put(r++, 1, t, info->trace_on ? C_PC : C_DIM);
        if (info->dump_n > 0) snprintf(t, sizeof t, "dump   %03d written", info->dump_n); else snprintf(t, sizeof t, "dump   none yet");
        put(r++, 1, t, C_DIM);
        r++;
    }

    rule(r++, 0, "CPU", cols);
    { uint8_t pf = cpu65_get_pf(); char fl[9]; const char *names = "NVEBDIZC";
      for (int b = 0; b < 8; b++) { fl[b] = (pf & (0x80 >> b)) ? names[b] : '.'; }
      fl[8] = 0;
      snprintf(t, sizeof t, "PC $%04X   P %s", cpu65.pc, fl); put(r++, 1, t, C_TEXT);
      snprintf(t, sizeof t, "A $%02X X $%02X Y $%02X Z $%02X", cpu65.a, cpu65.x, cpu65.y, cpu65.z); put(r++, 1, t, C_TEXT);
      snprintf(t, sizeof t, "SP $%04X   B $%02X", cpu65.s | cpu65.sphi, cpu65.bphi); put(r++, 1, t, C_TEXT); }
    r++;

    rule(r++, 0, "NEXT", cols);
    { uint16_t a = cpu65.pc;
      for (int i = 0; i < nnext && r < rows; i++) { int n = disasm(a, t, sizeof t); put(r++, 0, t, i ? C_TEXT : C_PC); a += n; } }
    r++;

    rule(r++, 0, "VICKY", cols);
    { uint8_t c = vicky_read(VR_CTRL); const char *name = "?";
      switch (c & 0x1F) { case 1: name = "640x480"; break; case 5: name = "640x240"; break; case 3: name = "320x240"; break;
                          case 11: name = "320x200"; break; case 27: name = "160x200"; break; case 13: name = "640x200"; break; case 0: name = "off"; break; }
      snprintf(t, sizeof t, "%-8s  ctrl $%02X", name, c); put(r++, 1, t, C_TEXT);
      snprintf(t, sizeof t, "raster %3u", vicky_read(VR_RASTER) | vicky_read(VR_RASTER + 1) << 8); put(r++, 1, t, C_TEXT); }
    r++;

    rule(r++, 0, all_banks ? "BANKS $D600" : "BANKS $D600 (active)", cols);
    for (int i = 0; i < 8 && r < rows; i++) {
        uint32_t v = io_read(0xD600 + i * 4) | (io_read(0xD601 + i * 4) << 8) | ((uint32_t)io_read(0xD602 + i * 4) << 16);
        uint8_t on = io_read(0xD603 + i * 4);
        if (!on && !all_banks) continue;
        snprintf(t, sizeof t, "%d $%04X %s $%06X", i, i * 0x2000, on ? "->" : "  ", v); put(r++, 1, t, on ? C_TEXT : C_DIM);
    }
    if (!all_banks && !active_banks && r < rows) put(r++, 1, "none", C_DIM);
    r++;

    if (show_audio && r < rows - 1) {
        rule(r++, 0, "AUDIO", cols);
        snprintf(t, sizeof t, "gaps %u  volume %d", io_audio_gaps, settings_get(SET_AUDIO_VOLUME)); put(r++, 1, t, C_TEXT);
    }
}
