/* K4510: TRACKER -- write music for the OPL2, a pattern at a time.
 *
 *   TRACKER            a new song (pattern 00 holds a short one to start from)
 *   TRACKER NAME.TRK   that song
 *
 * Nine channels, one for each of the OPL2's voices; sixteen patterns of 64
 * rows; a song is an order of patterns.  A cell holds a note and one of
 * sixteen instruments -- two-operator patches, changed on the F6 page.
 *
 *   the keyboard is a piano:  Z S X D C V G B H N J M ,  one octave
 *                             Q 2 W 3 E R 5 T 6 Y 7 U I  the one above
 *   1 note off   . or Del clear   0-9 A-F in the instrument column
 *   arrows move   Tab next channel   PgUp PgDn 16 rows   [ ] pattern
 *   F1 F2 octave   F3 F4 instrument   F9 F10 speed (frames a row)
 *   Space play the pattern / stop   F5 play the song   F6 the instruments
 *   (F7 is the machine's menu and F8 its pause, so TRACKER leaves them be)
 *   Ins add this pattern to the order   Ctrl-X take the last one off
 *   Ctrl-S save   Ctrl-O open   Ctrl-E export for OPLPLAY   Esc leave
 *
 * Ctrl-E plays the song through once into a K4OP stream -- the format
 * tools/vgm2opl.py writes and OPLPLAY plays, looping -- so a song made here
 * plays wherever OPLPLAY does.  A song is saved as .TRK (K4TR): its speed,
 * the order, the instruments and all sixteen patterns.
 */
#include "k4510.h"

#define TERM     0xDA00u
#define SCREEN   0x00030000UL
#define FS       0xD300u
#define OPL_ADDR 0xD480u
#define OPL_DATA 0xD481u
#define OPL_ID   0xD482u
#define PATS     0x0DD00000UL                /* the patterns (far memory nothing else uses) */
#define EXPBUF   0x0DE00000UL                /* an export being made */
#define EXPMAX   0x000F0000UL
#define LOADBUF  0x0DF00000UL
#define NPAT  16
#define NROW  64
#define NCH   9
#define PATSZ (NROW * NCH * 2)
#define HDRSZ 376
#define OFF   255                            /* a cell's note: let go */
#define SHELL_RC (*(volatile uint8_t *)0x03FF)

void __fastcall__ rom_chrout(unsigned char c);
static unsigned char rom_args(void) { return ((unsigned char (*)(void))0xFF95)(); }

enum { BLACK, WHITE, RED, CYAN, PURPLE, GREEN, BLUE, YELLOW, ORANGE, BROWN, LRED, DGREY, GREY, LGREEN, LBLUE, LGREY };

#define KY(k)   (0x100u | (k))
#define K_UP    KY(0x80)
#define K_DOWN  KY(0x81)
#define K_LEFT  KY(0x82)
#define K_RIGHT KY(0x83)
#define K_HOME  KY(0x84)
#define K_END   KY(0x85)
#define K_PGUP  KY(0x86)
#define K_PGDN  KY(0x87)
#define K_INS   KY(0x88)
#define K_DEL   KY(0x89)
#define K_F(n)  KY(0x8F + (n))
static uint16_t getkey(void)
{
    uint8_t st = REG(KBDST), k;
    if (!(st & 0x80)) return 0;
    k = REG(KBD);
    return (st & 0x20) ? KY(k) : k;
}

static void fs_w32(uint8_t r, uint32_t v)
{
    REG(FS + r) = (uint8_t) v; REG(FS + r + 1) = (uint8_t)(v >> 8);
    REG(FS + r + 2) = (uint8_t)(v >> 16); REG(FS + r + 3) = (uint8_t)(v >> 24);
}
static uint32_t fs_r32(uint8_t r)
{
    return (uint32_t) REG(FS + r) | ((uint32_t) REG(FS + r + 1) << 8) | ((uint32_t) REG(FS + r + 2) << 16) | ((uint32_t) REG(FS + r + 3) << 24);
}
static uint8_t fs_cmd(uint8_t c) { REG(FS) = c; return REG(FS + 1); }

/* ---- the instruments: mod mul lvl a/d s/r wave, car mul lvl a/d s/r wave, connection ---- */
static const uint8_t ins_default[16][11] = {
    { 0x01, 0x1A, 0xF2, 0x53, 0x00,  0x01, 0x00, 0xF3, 0x53, 0x00,  0x0A },   /* PIANO */
    { 0x21, 0x2C, 0x83, 0x35, 0x01,  0x21, 0x10, 0x74, 0x35, 0x00,  0x08 },   /* ORGAN */
    { 0x31, 0x1E, 0xF6, 0x27, 0x02,  0x11, 0x00, 0xF4, 0x37, 0x00,  0x06 },   /* BELL */
    { 0x11, 0x28, 0xF8, 0x88, 0x00,  0x01, 0x00, 0xF8, 0x69, 0x00,  0x04 },   /* PLUCK */
    { 0x61, 0x30, 0x51, 0x14, 0x01,  0x21, 0x18, 0x41, 0x24, 0x00,  0x0C },   /* STRINGS */
    { 0x21, 0x1C, 0xF4, 0x26, 0x01,  0x21, 0x06, 0xF3, 0x36, 0x00,  0x08 },   /* LEAD */
    { 0x01, 0x1A, 0xF2, 0x53, 0x00,  0x01, 0x04, 0xF3, 0x53, 0x00,  0x0A },   /* BASS */
    { 0x0E, 0x00, 0xF6, 0xF6, 0x03,  0x0E, 0x00, 0xF8, 0xF7, 0x03,  0x0E },   /* PERC */
    { 0x61, 0x27, 0x55, 0x17, 0x00,  0x21, 0x04, 0x75, 0x07, 0x00,  0x0E },   /* FLUTE */
    { 0x21, 0x1A, 0x72, 0x15, 0x00,  0x21, 0x00, 0x72, 0x16, 0x00,  0x0C },   /* BRASS */
    { 0x01, 0x00, 0xF8, 0xF8, 0x00,  0x00, 0x00, 0xF7, 0xF7, 0x00,  0x00 },   /* KICK */
    { 0x0F, 0x00, 0xF8, 0xF8, 0x03,  0x0F, 0x00, 0xF8, 0xF8, 0x03,  0x0E },   /* SNARE */
    { 0x0F, 0x00, 0xF9, 0xF9, 0x03,  0x0E, 0x00, 0xF9, 0xF9, 0x02,  0x0E },   /* HIHAT */
    { 0x05, 0x14, 0xF8, 0xB8, 0x00,  0x01, 0x00, 0xF6, 0x96, 0x00,  0x08 },   /* MARIMBA */
    { 0x62, 0x28, 0x31, 0x13, 0x02,  0x21, 0x0C, 0x31, 0x14, 0x00,  0x0E },   /* PAD */
    { 0x21, 0x10, 0xF1, 0x21, 0x02,  0x21, 0x08, 0xF1, 0x21, 0x02,  0x0E } }; /* SQUARE */
static const char *const iname[16] = { "PIANO", "ORGAN", "BELL", "PLUCK", "STRINGS", "LEAD", "BASS", "PERC",
                                       "FLUTE", "BRASS", "KICK", "SNARE", "HIHAT", "MARIMBA", "PAD", "SQUARE" };
static uint8_t ins[16][11];

static uint8_t order[64], olen, speed = 6, octave = 4, curins, pat;
static uint8_t pc[PATSZ];                    /* the pattern being edited, near; written through to far memory */
static uint8_t crow, cch, cfield, top, first_ch, vis_ch, nvis, view_ins, irow, ifield, inib;
static uint8_t cols, rows, ox, oy, stride, fg0, bg0, flags_was, modified;
static char fname[64];
static char msg[80];
static const char hexd[] = "0123456789ABCDEF";
static const char *const nname[12] = { "C-", "C#", "D-", "D#", "E-", "F-", "F#", "G-", "G#", "A-", "A#", "B-" };

static void load_pat(void) { dma_copy(PATS + (uint32_t) pat * PATSZ, (uint32_t)(uint16_t) pc, PATSZ); }
static void set_cell(uint8_t r, uint8_t c, uint8_t n, uint8_t i)
{
    uint16_t o = ((uint16_t) r * NCH + c) * 2;
    pc[o] = n; pc[o + 1] = i;
    dma_copy((uint32_t)(uint16_t)(pc + o), PATS + (uint32_t) pat * PATSZ + o, 2);
    modified = 1;
}

/* ---- the chip, or an export being written ------------------------------- */
static const uint8_t opslot[NCH] = { 0, 1, 2, 8, 9, 10, 16, 17, 18 };
static const uint16_t fnum[12] = { 345, 365, 387, 410, 434, 460, 488, 517, 547, 580, 614, 651 };
static uint8_t opl_ok, exporting, eover, chins[NCH];
static uint32_t ep;
static void ebyte(uint8_t b) { if (ep < EXPBUF + EXPMAX) far_poke(ep++, b); else eover = 1; }
static void out(uint8_t reg, uint8_t val)
{
    if (exporting) { ebyte(1); ebyte(reg); ebyte(val); }
    else if (opl_ok) { REG(OPL_ADDR) = reg; REG(OPL_DATA) = val; }
}
static void load_patch(uint8_t ch, uint8_t i)
{
    const uint8_t *p = ins[i];
    uint8_t m = opslot[ch], c = (uint8_t)(m + 3);
    out((uint8_t)(0x20 + m), p[0]); out((uint8_t)(0x40 + m), p[1]); out((uint8_t)(0x60 + m), p[2]);
    out((uint8_t)(0x80 + m), p[3]); out((uint8_t)(0xE0 + m), p[4]);
    out((uint8_t)(0x20 + c), p[5]); out((uint8_t)(0x40 + c), p[6]); out((uint8_t)(0x60 + c), p[7]);
    out((uint8_t)(0x80 + c), p[8]); out((uint8_t)(0xE0 + c), p[9]);
    out((uint8_t)(0xC0 + ch), p[10]);
}
static void note_off(uint8_t ch) { out((uint8_t)(0xB0 + ch), 0); }
static void note_on(uint8_t ch, uint8_t n, uint8_t i)
{
    uint8_t s = (uint8_t)(n - 1), block = (uint8_t)(s / 12);
    uint16_t f = fnum[s % 12];
    if (block > 7) block = 7;
    if (chins[ch] != i) { load_patch(ch, i); chins[ch] = i; }
    out((uint8_t)(0xB0 + ch), 0);
    out((uint8_t)(0xA0 + ch), (uint8_t) f);
    out((uint8_t)(0xB0 + ch), (uint8_t)(0x20 | (block << 2) | ((f >> 8) & 3)));
}
static void chip_reset(void)
{
    uint8_t v;
    if (!exporting) for (v = 0; v < 0xF6; v++) out(v, 0);
    else for (v = 0; v < NCH; v++) note_off(v);
    out(0x01, 0x20); out(0x08, 0x00); out(0xBD, 0x00);
    for (v = 0; v < NCH; v++) chins[v] = 0xFF;
}

/* ---- playing ------------------------------------------------------------ */
static uint8_t playing, psong, ppos, prow, ptick, ppat, rowbuf[NCH * 2];
static void play_row(void)
{
    uint8_t c, n;
    dma_copy(PATS + (uint32_t) ppat * PATSZ + (uint16_t) prow * NCH * 2, (uint32_t)(uint16_t) rowbuf, NCH * 2);
    for (c = 0; c < NCH; c++) {
        n = rowbuf[c * 2];
        if (n == OFF) note_off(c); else if (n) note_on(c, n, (uint8_t)(rowbuf[c * 2 + 1] & 15));
    }
}
static uint8_t advance(void)                 /* the next row; nonzero when the song comes round to its start */
{
    if (++prow < NROW) return 0;
    prow = 0;
    if (!psong) return 0;
    if (++ppos >= olen) { ppos = 0; ppat = order[0]; return 1; }
    ppat = order[ppos];
    return 0;
}
static void play_start(uint8_t song)
{
    if (song && !olen) song = 0;
    chip_reset();
    psong = song; ppos = 0; ppat = song ? order[0] : pat; prow = 0; ptick = 0; playing = 1;
}
static void play_stop(void) { uint8_t c; playing = 0; for (c = 0; c < NCH; c++) note_off(c); }
static uint8_t play_tick(void)               /* once a frame; nonzero when a row was played */
{
    uint8_t r = 0;
    if (!playing) return 0;
    if (ptick == 0) { play_row(); r = 1; }
    if (++ptick >= speed) { ptick = 0; advance(); }
    return r;
}

/* ---- the screen --------------------------------------------------------- */
static uint8_t rb[80 * 4], rn;
static void rb_put(uint8_t glyph, uint8_t fg, uint8_t bg)
{
    uint8_t *q = rb + rn * 4;
    if (rn >= 80 || rn >= cols) return;
    q[0] = glyph; q[1] = 0; q[2] = fg; q[3] = bg; rn++;
}
static void rb_str(const char *s, uint8_t fg, uint8_t bg) { while (*s) rb_put((uint8_t) *s++, fg, bg); }
static void rb_hex2(uint8_t v, uint8_t fg, uint8_t bg) { rb_put((uint8_t) hexd[v >> 4], fg, bg); rb_put((uint8_t) hexd[v & 15], fg, bg); }
static void rb_out(uint8_t r)
{
    while (rn < cols && rn < 80) rb_put(' ', fg0, bg0);
    if (r < rows) dma_copy((uint32_t)(uint16_t) rb, SCREEN + ((uint32_t)(r + oy) * stride + ox) * 4, (uint32_t) rn * 4);
    rn = 0;
}
static void rb_out_bar(uint8_t r)            /* a row in the bar colours */
{
    while (rn < cols && rn < 80) rb_put(' ', bg0, fg0);
    rb_out(r);
}

static void draw_header(void)
{
    uint8_t i;
    rb_str(" TRACKER  ", bg0, fg0);
    for (i = 0; fname[i] && i < 20; i++) rb_put((uint8_t) fname[i], bg0, fg0);
    rb_str("  pat ", bg0, fg0); rb_hex2(pat, bg0, fg0);
    rb_str("  oct ", bg0, fg0); rb_put((uint8_t)('0' + octave), bg0, fg0);
    rb_str("  ins ", bg0, fg0); rb_put((uint8_t) hexd[curins], bg0, fg0); rb_put(' ', bg0, fg0); rb_str(iname[curins], bg0, fg0);
    rb_str("  speed ", bg0, fg0); rb_put((uint8_t)('0' + speed / 10), bg0, fg0); rb_put((uint8_t)('0' + speed % 10), bg0, fg0);
    if (modified) rb_str(" *", RED, fg0);
    rb_out_bar(0);
}
static void draw_order(void)
{
    uint8_t i;
    rb_str(" order:", GREY, bg0);
    for (i = 0; i < olen && rn + 3 < cols; i++) {
        rb_put(' ', fg0, bg0);
        rb_hex2(order[i], (playing && psong && i == ppos) ? YELLOW : (order[i] == pat ? WHITE : fg0), bg0);
    }
    if (!olen) rb_str(" (empty: Ins adds the pattern)", GREY, bg0);
    rb_out(1);
}
static void draw_chanhead(void)
{
    uint8_t c;
    rb_str("   ", fg0, bg0);
    for (c = first_ch; c < NCH && c < first_ch + vis_ch; c++) {
        rb_str("CH", GREY, bg0); rb_put((uint8_t)('1' + c), c == cch ? WHITE : GREY, bg0); rb_str("    ", GREY, bg0);   /* 7 wide, as a cell */
    }
    rb_out(2);
}
static void draw_pattern(void)
{
    uint8_t v, r, c, n, i, f, g, cur, prow_here;
    uint16_t o;
    prow_here = playing && ppat == pat ? prow : 0xFF;
    if (playing && ptick) prow_here = playing && ppat == pat ? (uint8_t)(prow ? prow - 1 : NROW - 1) : 0xFF;
    for (v = 0; v < nvis; v++) {
        r = (uint8_t)(top + v);
        if (r >= NROW) { rb_out((uint8_t)(3 + v)); continue; }
        g = r == prow_here ? DGREY : bg0;
        rb_hex2(r, (r & 3) ? GREY : YELLOW, g); rb_put(' ', fg0, g);
        for (c = first_ch; c < NCH && c < first_ch + vis_ch; c++) {
            o = ((uint16_t) r * NCH + c) * 2; n = pc[o]; i = pc[o + 1];
            cur = r == crow && c == cch;
            f = cur && cfield == 0;
            if (!n)          rb_str("---", f ? BLACK : DGREY, f ? YELLOW : g);
            else if (n == OFF) rb_str("===", f ? BLACK : LRED, f ? YELLOW : g);
            else { rb_str(nname[(n - 1) % 12], f ? BLACK : WHITE, f ? YELLOW : g); rb_put((uint8_t)('0' + (n - 1) / 12), f ? BLACK : WHITE, f ? YELLOW : g); }
            rb_put(' ', fg0, g);
            f = cur && cfield == 1;
            rb_put((uint8_t)(n && n != OFF ? hexd[i & 15] : '.'), f ? BLACK : LGREEN, f ? YELLOW : g);
            rb_put(' ', fg0, g); rb_put(' ', fg0, g);
        }
        rb_out((uint8_t)(3 + v));
    }
}
static uint8_t field_col(uint8_t f) { return (uint8_t)(12 + f * 3 + (f >= 5) + (f >= 10)); }
static void draw_instruments(void)
{
    uint8_t v, i, f, cur;
    rb_str(" instrument  modulator: mul lvl a/d s/r wav  carrier: mul lvl a/d s/r wav  con", GREY, bg0);
    rb_out(2);
    for (v = 0; v < nvis; v++) {
        if (v >= 16) { rb_out((uint8_t)(3 + v)); continue; }
        i = v;
        rb_put((uint8_t) hexd[i], i == irow ? WHITE : GREY, bg0); rb_put(' ', fg0, bg0);
        rb_str(iname[i], i == curins ? YELLOW : fg0, bg0);
        while (rn < 12) rb_put(' ', fg0, bg0);
        for (f = 0; f < 11; f++) {
            while (rn < field_col(f)) rb_put(' ', fg0, bg0);
            cur = i == irow && f == ifield;
            rb_put((uint8_t) hexd[ins[i][f] >> 4], cur ? BLACK : fg0, cur && !inib ? YELLOW : (cur ? fg0 : bg0));
            rb_put((uint8_t) hexd[ins[i][f] & 15], cur ? BLACK : fg0, cur && inib ? YELLOW : (cur ? fg0 : bg0));
        }
        rb_out((uint8_t)(3 + v));
    }
}
static void draw_footer(void)
{
    if (msg[0]) rb_str(msg, bg0, fg0);
    else if (view_ins) rb_str(" 0-9 A-F edit  arrows move  Space hear it  Enter use it  F6/Esc back", bg0, fg0);
    else if (cols >= 78) rb_str(" SPC play  F5 song  F1/2 oct  F3/4 ins  F6 instr  ^S save  ^O open  ^E export", bg0, fg0);
    else rb_str(" SPC play F5 song F6 ins ^S save ^E exp", bg0, fg0);
    rb_out_bar((uint8_t)(rows - 1));
}
static void redraw(void)
{
    draw_header(); draw_order();
    if (view_ins) draw_instruments(); else { draw_chanhead(); draw_pattern(); }
    draw_footer();
}
static void keep_visible(void)
{
    if (crow < top) top = crow;
    if (crow >= top + nvis) top = (uint8_t)(crow - nvis + 1);
    if (cch < first_ch) first_ch = cch;
    if (cch >= first_ch + vis_ch) first_ch = (uint8_t)(cch - vis_ch + 1);
}

static uint8_t ask(const char *q, char *buf, uint8_t max)
{
    uint8_t n = (uint8_t) strlen(buf);
    uint16_t k;
    for (;;) {
        rb_str(q, bg0, fg0); rb_str(buf, bg0, fg0); rb_put('_', YELLOW, fg0);
        rb_out_bar((uint8_t)(rows - 1));
        while ((k = getkey()) == 0) ;
        if (k == 13) return n != 0;
        if (k == 0x1B) return 0;
        if ((k == 8 || k == 0x7F || k == 0x14 || k == K_DEL) && n) buf[--n] = 0;
        else if (k >= 0x20 && k < 0x7F && n < max) { buf[n++] = (char) k; buf[n] = 0; }
    }
}

/* ---- files -------------------------------------------------------------- */
static uint8_t hdr[HDRSZ];
static void save_trk(void)
{
    uint8_t i, e;
    memset(hdr, 0, sizeof hdr);
    hdr[0] = 'K'; hdr[1] = '4'; hdr[2] = 'T'; hdr[3] = 'R'; hdr[4] = 1; hdr[5] = speed; hdr[6] = olen; hdr[7] = NPAT;
    memcpy(hdr + 8, order, 64);
    memcpy(hdr + 72, ins, 16 * 11);
    for (i = 0; i < 16; i++) strncpy((char *) hdr + 248 + i * 8, iname[i], 8);
    fs_w32(4, (uint16_t) fname);
    if (fs_cmd(2)) { strcpy(msg, " could not write that name"); return; }
    fs_w32(8, (uint16_t) hdr); fs_w32(12, HDRSZ); e = fs_cmd(4);
    fs_w32(8, PATS); fs_w32(12, (uint32_t) NPAT * PATSZ); e |= fs_cmd(4);
    fs_cmd(5);
    if (e) strcpy(msg, " the disk would not take it all");
    else { modified = 0; strcpy(msg, " saved"); }
}
static void demo_song(void);
static uint8_t load_trk(void)                /* 0 loaded, 1 absent, 2 not a song */
{
    uint8_t st;
    fs_w32(4, (uint16_t) fname); fs_w32(8, LOADBUF); fs_w32(12, 0x10000UL);
    st = fs_cmd(9);
    if (st == 1) return 1;
    if (st) return 2;
    dma_copy(LOADBUF, (uint32_t)(uint16_t) hdr, HDRSZ);
    if (hdr[0] != 'K' || hdr[1] != '4' || hdr[2] != 'T' || hdr[3] != 'R') return 2;
    speed = hdr[5] ? hdr[5] : 6; olen = hdr[6] > 64 ? 64 : hdr[6];
    memcpy(order, hdr + 8, 64);
    memcpy(ins, hdr + 72, 16 * 11);
    dma_copy(LOADBUF + HDRSZ, PATS, (uint32_t) NPAT * PATSZ);
    pat = 0; load_pat(); modified = 0;
    return 0;
}
static void export_opl(void)
{
    static char en[64];
    static const char *const title = "made in TRACKER";
    uint8_t i, n, done = 0;
    uint32_t loop;
    char *dot;
    if (!olen) { strcpy(msg, " the order is empty: Ins adds this pattern to it"); return; }
    strcpy(en, "/APPS/OPLPLAY/TUNES/");
    for (i = 0, n = (uint8_t) strlen(en); fname[i] && n < 50; i++) if (fname[i] == '/') n = (uint8_t) strlen("/APPS/OPLPLAY/TUNES/"); else en[n++] = fname[i];
    en[n] = 0;
    dot = strrchr(en, '.'); if (dot) *dot = 0;
    strcat(en, ".OPL");
    if (!ask(" export as: ", en, 60)) return;
    play_stop();
    exporting = 1; eover = 0; ep = EXPBUF;
    ebyte('K'); ebyte('4'); ebyte('O'); ebyte('P'); ebyte(1); ebyte(1); ebyte(60); ebyte(0);
    for (i = 0; i < 4; i++) ebyte(0);
    for (i = 0; i < 32; i++) ebyte((uint8_t)(i < 15 ? title[i] : 0));
    chip_reset();
    loop = ep - (EXPBUF + 44);
    psong = 1; ppos = 0; ppat = order[0]; prow = 0;
    while (!done && !eover) { play_row(); ebyte(2); ebyte(speed); done = advance(); }
    for (i = 0; i < NCH; i++) note_off(i);
    ebyte(0);
    exporting = 0;
    for (i = 0; i < 4; i++) far_poke(EXPBUF + 8 + i, (uint8_t)(loop >> (i * 8)));
    if (eover) { strcpy(msg, " the song is too long to export (1 MB)"); return; }
    fs_w32(4, (uint16_t) en); fs_w32(8, EXPBUF); fs_w32(12, ep - EXPBUF);
    if (fs_cmd(10)) strcpy(msg, " could not write the export");
    else { strcpy(msg, " exported: OPLPLAY plays it"); }
    chip_reset();
}

/* ---- the song to start from: Korobeiniki over bass and drums ----------- */
#define N(o, s) ((o) * 12 + (s) + 1)
enum { C, Cs, D, Ds, E, F, Fs, G, Gs, A, As, B };
static const uint8_t tune[] = {
    N(5,E),2, N(4,B),1, N(5,C),1, N(5,D),2, N(5,C),1, N(4,B),1,  N(4,A),2, N(4,A),1, N(5,C),1, N(5,E),2, N(5,D),1, N(5,C),1,
    N(4,B),3, N(5,C),1, N(5,D),2, N(5,E),2,  N(5,C),2, N(4,A),2, N(4,A),2, OFF,2,
    N(5,D),3, N(5,F),1, N(5,A),2, N(5,G),1, N(5,F),1,  N(5,E),3, N(5,C),1, N(5,E),2, N(5,D),1, N(5,C),1,
    N(4,B),2, N(4,B),1, N(5,C),1, N(5,D),2, N(5,E),2,  N(5,C),2, N(4,A),2, N(4,A),2, OFF,2, 0, 0 };
static const uint8_t broot[8] = { N(2,E), N(2,A), N(2,E), N(2,A), N(2,D), N(2,C), N(2,E), N(2,A) };
static void demo_song(void)
{
    uint8_t r = 0, i;
    pat = 0; memset(pc, 0, PATSZ);
    for (i = 0; tune[i + 1] && r < NROW; i += 2) { pc[(uint16_t) r * NCH * 2] = tune[i]; pc[(uint16_t) r * NCH * 2 + 1] = 5; r = (uint8_t)(r + tune[i + 1]); }
    for (r = 0; r < NROW; r++) {
        pc[((uint16_t) r * NCH + 1) * 2] = (uint8_t)(broot[r >> 3] + (r & 1 ? 12 : 0)); pc[((uint16_t) r * NCH + 1) * 2 + 1] = 6;
        if ((r & 7) == 0) { pc[((uint16_t) r * NCH + 2) * 2] = N(2,C); pc[((uint16_t) r * NCH + 2) * 2 + 1] = 10; }
        if ((r & 7) == 4) { pc[((uint16_t) r * NCH + 3) * 2] = N(4,C); pc[((uint16_t) r * NCH + 3) * 2 + 1] = 11; }
        if ((r & 1) == 0) { pc[((uint16_t) r * NCH + 4) * 2] = N(6,C); pc[((uint16_t) r * NCH + 4) * 2 + 1] = 12; }
    }
    dma_copy((uint32_t)(uint16_t) pc, PATS, PATSZ);
    order[0] = 0; olen = 1; speed = 8;
}

/* ---- the keys ----------------------------------------------------------- */
static const char low_row[] = "zsxdcvgbhnjm,";
static const char high_row[] = "q2w3er5t6y7ui";
static int8_t hexval(uint8_t c)
{
    if (c >= '0' && c <= '9') return (int8_t)(c - '0');
    if (c >= 'a' && c <= 'f') return (int8_t)(c - 'a' + 10);
    if (c >= 'A' && c <= 'F') return (int8_t)(c - 'A' + 10);
    return -1;
}
static void preview(uint8_t ch, uint8_t n, uint8_t i) { if (!playing) { note_on(ch, n, i); } }
static void step_down(void) { crow = (uint8_t)((crow + 1) & (NROW - 1)); }

static uint8_t key_ins_view(uint16_t k)
{
    int8_t d;
    switch (k) {
    case K_UP:    if (irow) irow--; return 0;
    case K_DOWN:  if (irow < 15) irow++; return 0;
    case K_LEFT:  if (inib) inib = 0; else if (ifield) { ifield--; inib = 1; } return 0;
    case K_RIGHT: if (!inib) inib = 1; else if (ifield < 10) { ifield++; inib = 0; } return 0;
    case ' ':     chins[8] = 0xFF; note_on(8, N(4,C), irow); return 0;
    case 13:      curins = irow; view_ins = 0; return 0;
    case 0x1B: case K_F(6): view_ins = 0; note_off(8); return 0;
    }
    d = k < 0x100 ? hexval((uint8_t) k) : -1;
    if (d >= 0) {
        uint8_t *p = &ins[irow][ifield];
        *p = inib ? (uint8_t)((*p & 0xF0) | d) : (uint8_t)((*p & 0x0F) | (d << 4));
        if (!inib) inib = 1; else if (ifield < 10) { ifield++; inib = 0; }
        modified = 1;
        { uint8_t c; for (c = 0; c < NCH; c++) chins[c] = 0xFF; }   /* every voice takes the new patch at its next note */
    }
    return 0;
}
static uint8_t leave(void)
{
    uint16_t k;
    if (!modified) return 1;
    rb_str(" not saved -- save first?  Y yes  N no  Esc stay", bg0, fg0); rb_out_bar((uint8_t)(rows - 1));
    for (;;) {
        while ((k = getkey()) == 0) ;
        if (k == 'y' || k == 'Y') { if (ask(" save as: ", fname, 60)) save_trk(); return !modified; }
        if (k == 'n' || k == 'N') return 1;
        if (k == 0x1B) return 0;
    }
}
static uint8_t key(uint16_t k)               /* nonzero: leave */
{
    const char *p;
    uint8_t c, n;
    int8_t d;
    msg[0] = 0;
    if (view_ins) return key_ins_view(k);
    switch (k) {
    case K_UP:    crow = (uint8_t)((crow + NROW - 1) & (NROW - 1)); return 0;
    case K_DOWN:  step_down(); return 0;
    case K_LEFT:  if (cfield) cfield = 0; else if (cch) { cch--; cfield = 1; } return 0;
    case K_RIGHT: if (!cfield) cfield = 1; else if (cch < NCH - 1) { cch++; cfield = 0; } return 0;
    case 9:       cch = (uint8_t)((cch + 1) % NCH); cfield = 0; return 0;
    case K_PGUP:  crow = crow >= 16 ? (uint8_t)(crow - 16) : 0; return 0;
    case K_PGDN:  crow = crow + 16 < NROW ? (uint8_t)(crow + 16) : NROW - 1; return 0;
    case K_HOME:  crow = 0; return 0;
    case K_END:   crow = NROW - 1; return 0;
    case ' ':     if (playing) play_stop(); else play_start(0); return 0;
    case K_F(5):  if (playing) play_stop(); else play_start(1); return 0;
    case K_F(1):  if (octave) octave--; return 0;
    case K_F(2):  if (octave < 7) octave++; return 0;
    case K_F(3):  curins = (uint8_t)((curins + 15) & 15); return 0;
    case K_F(4):  curins = (uint8_t)((curins + 1) & 15); return 0;
    case K_F(9):  if (speed > 1) speed--; modified = 1; return 0;
    case K_F(10): if (speed < 31) speed++; modified = 1; return 0;
    case K_F(6):  view_ins = 1; irow = curins; ifield = 0; inib = 0; return 0;
    case '[':     pat = (uint8_t)((pat + NPAT - 1) % NPAT); load_pat(); return 0;
    case ']':     pat = (uint8_t)((pat + 1) % NPAT); load_pat(); return 0;
    case K_INS:   if (olen < 64) { order[olen++] = pat; modified = 1; } return 0;
    case 0x18:    if (olen) { olen--; modified = 1; } return 0;
    case 0x13:    if (ask(" save as: ", fname, 60)) save_trk(); return 0;
    case 0x0F:
        if (ask(" open: ", fname, 60)) {
            play_stop();
            n = load_trk();
            if (n == 1) strcpy(msg, " no such file");
            else if (n == 2) strcpy(msg, " that is not a TRACKER song");
        }
        return 0;
    case 0x05:    export_opl(); return 0;
    case 0x1B:    return leave();
    case '.': case K_DEL: set_cell(crow, cch, 0, 0); step_down(); return 0;
    }
    if (k >= 0x100) return 0;
    if (cfield == 1) {
        d = hexval((uint8_t) k);
        n = pc[((uint16_t) crow * NCH + cch) * 2];
        if (d >= 0 && n && n != OFF) { set_cell(crow, cch, n, (uint8_t) d); step_down(); }
        return 0;
    }
    if (k == '1') { set_cell(crow, cch, OFF, 0); if (!playing) note_off(cch); step_down(); return 0; }
    c = (uint8_t) k; if (c >= 'A' && c <= 'Z') c = (uint8_t)(c + 32);
    if ((p = strchr(low_row, c)) != 0 && c) n = (uint8_t)(octave * 12 + (p - low_row) + 1);
    else if ((p = strchr(high_row, c)) != 0 && c) n = (uint8_t)((octave + 1) * 12 + (p - high_row) + 1);
    else return 0;
    if (n > 96) return 0;
    set_cell(crow, cch, n, curins);
    preview(cch, n, curins);
    step_down();
    return 0;
}

void main(void)
{
    const char *p;
    uint8_t i = 0, q = 0, lf, fc, d, f;
    uint16_t k;

    rom_args();
    p = *(const char **) 0xF0;
    while (*p == ' ') p++;
    if (*p == '"') { q = 1; p++; }
    while (*p && i < 60 && (q ? *p != '"' : *p != ' ')) fname[i++] = *p++;
    fname[i] = 0;

    memcpy(ins, ins_default, sizeof ins);
    dma_fill(0, PATS, (uint32_t) NPAT * PATSZ);
    if (!i) { strcpy(fname, "SONG.TRK"); demo_song(); strcpy(msg, " a new song: Space plays pattern 00, F6 the instruments"); }
    else {
        d = load_trk();
        if (d == 1) { demo_song(); strcpy(msg, " a new song: Space plays pattern 00"); }
        else if (d == 2) { rom_chrout('t'); { const char *e = "racker: that is not a TRACKER song\n"; while (*e) rom_chrout((uint8_t) *e++); } SHELL_RC = 1; return; }
    }
    load_pat();

    opl_ok = REG(OPL_ID) == 0x02;
    chip_reset();
    cols = REG(TERM + 5); rows = REG(TERM + 6);
    ox = REG(TERM + 7);   oy = REG(TERM + 8);
    stride = REG(TERM + 0x0D);
    fg0 = REG(TERM + 0x14); bg0 = REG(TERM + 0x15);
    if (fg0 == bg0) { fg0 = WHITE; bg0 = BLUE; }
    if (!cols) cols = 80;
    if (!rows) rows = 25;
    if (!stride) stride = cols;
    vis_ch = (uint8_t)((cols - 3) / 7); if (vis_ch > NCH) vis_ch = NCH; if (!vis_ch) vis_ch = 1;
    nvis = (uint8_t)(rows - 4);
    flags_was = REG(TERM + 0x0E);
    REG(TERM + 0x0E) = (uint8_t)(flags_was & ~1);
    if (!opl_ok) strcpy(msg, " no OPL2 answers at $D480: the song is silent here");

    redraw();
    lf = REG(SYS + 0x0D);
    for (;;) {
        f = 0;
        fc = REG(SYS + 0x0D);
        if (fc != lf) {
            d = (uint8_t)(fc - lf); lf = fc;
            while (d--) if (play_tick()) f = 1;
        }
        k = getkey();
        if (k) { if (key(k)) break; keep_visible(); f = 1; }
        if (f) redraw();
    }
    play_stop();
    for (i = 0; i < 0xF6 && opl_ok; i++) { REG(OPL_ADDR) = i; REG(OPL_DATA) = 0; }
    REG(TERM + 0x0E) = flags_was;
    rom_chrout(12);
}
