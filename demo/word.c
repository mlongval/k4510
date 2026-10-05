/* K4510: WORD [-s] [name] -- a reader for Microsoft Word's .docx files, in
 * MS-DOS EDIT's visual language (demo/dosui.h).
 *
 * Doc, 2026-10-02: "can you write a 'WORD' version, using the same visual
 * language, that can at least READ MS-WORD files.  SAVING would be a
 * perhaps more complicated question."  So this reads; it does not write
 * .docx (File > Save As Text writes what it shows as plain text).
 *
 * A .docx is a zip of XML.  The machine's file device already mounts a zip
 * as a folder and inflates on the way out (MOUNT, core/zip.c), so WORD
 * mounts the file at /MNT/WORDDOC, LOADs word/document.xml (and styles.xml,
 * numbering.xml) into far memory, unmounts, and parses the XML itself, on
 * the 45GS10: paragraphs, runs, bold / italic / underline, headings and the
 * title (by their style's name), bulleted and numbered lists, tables (a row
 * to a line, cells between bars), tabs, line and page breaks, links, and
 * UTF-8 down to the code page.  What it does not try: pictures, text boxes,
 * footnotes, headers and footers, fonts and sizes.  A Word 97-2003 .doc is a
 * different file altogether (an OLE compound file) and is refused by name.
 * A .TXT opens too, a line to a paragraph.
 *
 * The page is laid out to the window: each paragraph wrapped at a word,
 * centred or right-aligned when Word had it so, a blank line between
 * paragraphs.  Emphasis is colour, the only thing a text cell has: bold
 * white, italic cyan, both yellow, underlined green, headings yellow and
 * white.
 *
 * Far memory, $09000000-$0BFFFFFF (inside EDIT's reservation; the two never
 * run together): the XML at $09000000, the parsed document at $0A000000,
 * the line table at $0B000000, the Open dialog's list at $0BF00000. */
#include "k4510.h"

void __fastcall__ rom_chrout(unsigned char c);
unsigned char rom_getin(void);
static unsigned char rom_args(void) { return (unsigned char)(((unsigned (*)(void))0xFF95)() & 0xFF); }
static unsigned char rom_load(void) { return (unsigned char)(((unsigned (*)(void))0xFF89)() & 0xFF); }
static unsigned char rom_save(void) { return (unsigned char)(((unsigned (*)(void))0xFF8C)() & 0xFF); }
static void rom_video(void) { ((void (*)(void))0xFF92)(); }
static void zp16(uint8_t a, uint16_t v) { REG(a) = v; REG(a + 1) = v >> 8; }
static void zp32(uint8_t a, uint32_t v) { REG(a)=v; REG(a+1)=v>>8; REG(a+2)=v>>16; REG(a+3)=v>>24; }
static uint32_t zpr32(uint8_t a) { return (uint32_t)REG(a) | ((uint32_t)REG(a+1)<<8) | ((uint32_t)REG(a+2)<<16) | ((uint32_t)REG(a+3)<<24); }

#include "dosui.h"

#define XMLBUF  0x09000000UL            /* the XML being read */
#define DOC     0x0A000000UL            /* the document: paragraphs, each a header and (char, attr) pairs */
#define LINES   0x0B000000UL            /* the layout: 8 bytes a screen line */
#define FLAT    0x0B800000UL            /* Save As Text's file */
#define MNT     "/MNT/WORDDOC"
#define PMAX    1500u                   /* characters a paragraph keeps */

enum { C_OPEN = 1, C_SAVETXT, C_EXIT, C_FIND, C_NEXT, C_TOP, C_END, C_DOS, C_SYS, C_TABW, C_HELP, C_ABOUT };
/* a paragraph's kind */
enum { P_TEXT, P_H1, P_H2, P_H3, P_TITLE, P_LIST, P_ROW, P_PAGE };
/* a character's attributes */
#define A_B 1
#define A_I 2
#define A_U 4
#define A_LINK 8

static char name[NAMEMAX], fbuf[NAMEMAX], sbuf[NAMEMAX];
static const char *note = "";
static char nbuf[80]; static uint8_t nbn;
static uint8_t wtabw = 4;                              /* Options > Tab Width: a Tab in the document is this many spaces (never a tab character) */
static void nb_reset(void) { nbn = 0; nbuf[0] = 0; }
static void nb_s(const char *s) { while (*s && nbn < sizeof nbuf - 1) nbuf[nbn++] = *s++; nbuf[nbn] = 0; }
static void nb_n(unsigned long v) { char b[10]; uint8_t k = 0; do { b[k++] = (char)('0' + v % 10); v /= 10; } while (v); while (k && nbn < sizeof nbuf - 1) nbuf[nbn++] = b[--k]; nbuf[nbn] = 0; }
static const char *base_of(const char *p) { const char *b = p; for (; *p; p++) if (*p == '/') b = p + 1; return b; }
static uint8_t up(uint8_t c) { return (uint8_t)((c >= 'a' && c <= 'z') ? c - 32 : c); }

/* ---- reading the XML -------------------------------------------------------
 * A byte at a time out of far memory, through a 256-byte window. */
static uint32_t xp, xend;
static uint8_t xb[256], xi, xn;
static int xget(void)
{
    if (xi >= xn) {
        unsigned n;
        if (xp >= xend) return -1;
        n = (xend - xp) > 255 ? 255 : (unsigned)(xend - xp);   /* 255: xn and xi are bytes */
        far_get(xp, xb, n); xp += n; xn = (uint8_t)n; xi = 0;
    }
    return xb[xi++];
}
static uint32_t load_part(const char *path, uint32_t at)   /* a file into far memory; its length, 0 if none */
{
    zp16(0xF0, (uint16_t)path); zp32(0xF2, at);
    if (rom_load()) return 0;
    return zpr32(0xF6);
}

/* A tag: its name (without the w: prefix's colon kept), its attributes as
 * text (cut short: a <w:document> carries kilobytes of namespaces), and
 * whether it closes, opens, or does both. */
static char tn[24], ta[100];
static uint8_t tclose, tself;
static int read_tag(void)                             /* after '<': fills tn/ta; -1 at the end */
{
    int c; uint8_t n = 0, a = 0, last = 0;
    tclose = tself = 0;
    c = xget();
    if (c == '/') { tclose = 1; c = xget(); }
    if (c == '?' || c == '!') {                       /* a declaration or a comment: to its end */
        while ((c = xget()) >= 0 && c != '>') ;
        tn[0] = 0; return c;
    }
    while (c >= 0 && c != '>' && c != ' ' && c != '/' && c != '\t' && c != '\r' && c != '\n') { if (n < sizeof tn - 1) tn[n++] = (char)c; c = xget(); }
    tn[n] = 0;
    while (c >= 0 && c != '>') { if (a < sizeof ta - 1) ta[a++] = (char)c; last = (uint8_t)c; c = xget(); }
    ta[a] = 0;
    if (last == '/') tself = 1;
    return c;
}
static uint8_t is(const char *s) { const char *t = tn; while (*s && *t == *s) { s++; t++; } return (uint8_t)(!*s && !*t); }
static const char *attr(const char *k)                /* the value of k="..." in ta, or 0 */
{
    const char *p = ta, *q; uint8_t n = slen(k);
    for (; *p; p++) {
        for (q = k; *q && p[q - k] == *q; q++) ;
        if (!*q && p[n] == '=' && p[n + 1] == '"') return p + n + 2;
    }
    return 0;
}
static uint8_t aval(const char *k, const char *v)     /* attribute k's value begins with v */
{
    const char *p = attr(k);
    if (!p) return 0;
    while (*v && up((uint8_t)*p) == up((uint8_t)*v)) { p++; v++; }
    return (uint8_t)!*v;
}
static unsigned anum(const char *k) { const char *p = attr(k); unsigned v = 0; if (p) while (*p >= '0' && *p <= '9') v = v * 10 + (unsigned)(*p++ - '0'); return v; }

/* ---- UTF-8 to the code page ------------------------------------------------
 * The font is code page 437: Latin-1's accented lower case is mostly there,
 * its capitals mostly not (they lose the accent); quotes and dashes become
 * their ASCII selves. */
static const uint8_t lat1[96] = {
    ' ', 0xAD, 0x9B, 0x9C, '?', 0x9D, '|', 0x15, '"', 'c', 0xA6, 0xAE, 0xAA, '-', 'R', '-',
    0xF8, 0xF1, 0xFD, '3', '\'', 0xE6, 0x14, 0xFA, ',', '1', 0xA7, 0xAF, 0xAC, 0xAB, '3', 0xA8,
    'A', 'A', 'A', 'A', 0x8E, 0x8F, 0x92, 0x80, 'E', 0x90, 'E', 'E', 'I', 'I', 'I', 'I',
    'D', 0xA5, 'O', 'O', 'O', 'O', 0x99, 'x', 'O', 'U', 'U', 'U', 0x9A, 'Y', 'P', 0xE1,
    0x85, 0xA0, 0x83, 'a', 0x84, 0x86, 0x91, 0x87, 0x8A, 0x82, 0x88, 0x89, 0x8D, 0xA1, 0x8C, 0x8B,
    'd', 0xA4, 0x95, 0xA2, 0x93, 'o', 0x94, 0xF6, 'o', 0x97, 0xA3, 0x96, 0x81, 'y', 'p', 0x98 };
static uint8_t cp_of(unsigned u)
{
    if (u < 0x80) return (uint8_t)u;
    if (u >= 0xA0 && u <= 0xFF) return lat1[u - 0xA0];
    switch (u) {
    case 0x2018: case 0x2019: case 0x201A: case 0x2032: return '\'';
    case 0x201C: case 0x201D: case 0x201E: case 0x2033: return '"';
    case 0x2010: case 0x2011: case 0x2012: case 0x2013: case 0x2014: case 0x2212: return '-';
    case 0x2022: case 0x25CF: return 0x07;
    case 0x2026: return 0xFA;                         /* an ellipsis: one dot is what fits */
    case 0x2192: return 0x1A; case 0x2190: return 0x1B;
    case 0x0153: return 'o'; case 0x0152: return 'O';
    case 0x20AC: return 'E';
    }
    return '?';
}

/* ---- the document, as it is built ---------------------------------------------
 * A paragraph in far memory: kind, alignment (0 left, 1 centre, 2 right),
 * list level, list number (0 = a bullet), then its length and the pairs. */
static uint32_t dp, dend;                             /* where the next paragraph goes; the end */
static unsigned npara;
#pragma bss-name (push, "BIGBSS")                    /* DMA-only buffers, after the image (demo/word.cfg) */
static uint8_t pb[2 * PMAX];                          /* the paragraph being built: (char, attr) pairs */
#pragma bss-name (pop)
static unsigned pn;
static uint8_t pkind, pjc, plvl; static unsigned pnum;
static uint8_t attrs;
static void emit(uint8_t c) { if (pn < PMAX) { pb[pn * 2] = c; pb[pn * 2 + 1] = attrs; pn++; } }
static void emit_tab(void) { uint8_t k; for (k = wtabw; k; k--) emit(' '); }
static void para_end(void)
{
    uint8_t h[7];
    h[0] = pkind; h[1] = pjc; h[2] = plvl; h[3] = (uint8_t)pnum; h[4] = (uint8_t)(pnum >> 8);
    h[5] = (uint8_t)pn; h[6] = (uint8_t)(pn >> 8);
    far_put(h, dp, 7); dp += 7;
    if (pn) { far_put(pb, dp, pn * 2); dp += (uint32_t)pn * 2; }
    npara++;
    pn = 0; pkind = P_TEXT; pjc = 0; plvl = 0; pnum = 0;
}

/* styles.xml: which style ids are headings and the title */
#define NSTY 40
static char sid[NSTY][16]; static uint8_t skind[NSTY], nsty;
static char cursid[16];
static void styles_tag(void)
{
    const char *v; uint8_t i;
    if (is("w:style") && !tclose) {
        v = attr("w:styleId"); cursid[0] = 0;
        if (v) { for (i = 0; v[i] && v[i] != '"' && i < 15; i++) cursid[i] = v[i]; cursid[i] = 0; }
        return;
    }
    if (is("w:name") && cursid[0] && nsty < NSTY) {
        uint8_t k = 0;
        if (aval("w:val", "heading 1")) k = P_H1;
        else if (aval("w:val", "heading 2")) k = P_H2;
        else if (aval("w:val", "heading ")) k = P_H3;
        else if (aval("w:val", "title")) k = P_TITLE;
        if (k) { for (i = 0; cursid[i]; i++) sid[nsty][i] = cursid[i]; sid[nsty][i] = 0; skind[nsty++] = k; }
        cursid[0] = 0;
    }
}
static uint8_t style_kind(void)                       /* the w:val of a pStyle, as a paragraph kind */
{
    const char *v = attr("w:val"); uint8_t i, j;
    if (!v) return P_TEXT;
    for (i = 0; i < nsty; i++) {
        for (j = 0; sid[i][j] && v[j] == sid[i][j]; j++) ;
        if (!sid[i][j] && v[j] == '"') return skind[i];
    }
    if (aval("w:val", "Heading1")) return P_H1;       /* no styles.xml: the English ids */
    if (aval("w:val", "Heading2")) return P_H2;
    if (aval("w:val", "Heading")) return P_H3;
    if (aval("w:val", "Title")) return P_TITLE;
    return P_TEXT;
}

/* numbering.xml: which lists are bullets and which count.  The ids are any
 * numbers (pandoc's are 1000 and up), so they are looked up, not indexed. */
#define NNUM 48
static unsigned num_id[NNUM], num_abs[NNUM], abs_id[NNUM];
static uint8_t abs_mask[NNUM], n_num, n_abs;          /* abs_mask bit l: level l counts (1. 2. 3.), not a bullet */
static unsigned cnt[NNUM][4];
static uint8_t cur_a, cur_lvl;
static uint8_t abs_at(unsigned id) { uint8_t i; for (i = 0; i < n_abs; i++) if (abs_id[i] == id) return i; return 0xFF; }
static uint8_t num_at(unsigned id) { uint8_t i; for (i = 0; i < n_num; i++) if (num_id[i] == id) return i; return 0xFF; }
static void numbering_tag(void)
{
    if (tclose) return;
    if (is("w:abstractNum")) { cur_a = 0xFF; if (n_abs < NNUM) { cur_a = n_abs; abs_id[n_abs] = anum("w:abstractNumId"); abs_mask[n_abs++] = 0; } return; }
    if (is("w:lvl")) { cur_lvl = (uint8_t)anum("w:ilvl"); return; }
    if (is("w:numFmt") && cur_a != 0xFF && cur_lvl < 4) { if (!aval("w:val", "bullet") && !aval("w:val", "none")) abs_mask[cur_a] |= (uint8_t)(1 << cur_lvl); return; }
    if (is("w:num") && n_num < NNUM) { num_id[n_num] = anum("w:numId"); num_abs[n_num++] = 0xFFFF; return; }
    if (is("w:abstractNumId") && n_num && attr("w:val")) num_abs[n_num - 1] = anum("w:val");
}

/* document.xml */
static uint8_t in_t, in_ppr, in_rpr, in_tbl, cell_n, pcell, in_link;
static unsigned numid;
static void ent(void)                                 /* after '&': an entity, emitted */
{
    char e[8]; uint8_t n = 0; int c; unsigned v = 0;
    while ((c = xget()) >= 0 && c != ';' && n < 7) e[n++] = (char)c;
    e[n] = 0;
    if (e[0] == '#') { uint8_t i = 1, hex = (uint8_t)(e[1] == 'x');
        for (i = (uint8_t)(1 + hex); e[i]; i++) v = hex ? v * 16 + (unsigned)(e[i] <= '9' ? e[i] - '0' : (e[i] | 0x20) - 'a' + 10) : v * 10 + (unsigned)(e[i] - '0');
        emit(cp_of(v)); return; }
    emit(e[0] == 'a' && e[1] == 'm' ? '&' : e[0] == 'l' ? '<' : e[0] == 'g' ? '>' : e[0] == 'q' ? '"' : '\'');
}
static void doc_tag(void)
{
    if (is("w:p")) {
        if (tclose) { if (!in_tbl) para_end(); in_ppr = 0; }
        else if (in_tbl) { if (pcell++) emit(' '); }
        return;
    }
    if (is("w:pPr")) { in_ppr = (uint8_t)!tclose && !tself; return; }
    if (is("w:rPr")) { in_rpr = (uint8_t)!tclose && !tself; return; }
    if (is("w:r")) { if (!tclose) attrs = in_link ? A_LINK : 0; return; }
    if (is("w:hyperlink")) { in_link = (uint8_t)!tclose && !tself; return; }
    if (is("w:t")) { in_t = (uint8_t)(!tclose && !tself); return; }
    if (tclose) {
        if (is("w:tbl")) in_tbl = 0;
        else if (is("w:tr")) { para_end(); }
        return;
    }
    if (in_ppr && !in_rpr) {
        if (is("w:pStyle")) { uint8_t k = style_kind(); if (k) pkind = k; }
        else if (is("w:jc")) pjc = (uint8_t)(aval("w:val", "center") ? 1 : aval("w:val", "right") || aval("w:val", "end") ? 2 : 0);
        else if (is("w:ilvl")) plvl = (uint8_t)anum("w:val");
        else if (is("w:numId")) {
            numid = anum("w:val");
            if (numid && pkind == P_TEXT) {
                uint8_t l = plvl > 3 ? 3 : plvl, n = num_at(numid), a = n != 0xFF ? abs_at(num_abs[n]) : 0xFF, j;
                pkind = P_LIST;
                if (a != 0xFF && (abs_mask[a] >> l) & 1) {
                    pnum = ++cnt[n][l];
                    for (j = (uint8_t)(l + 1); j < 4; j++) cnt[n][j] = 0;   /* a deeper list starts again */
                }
            }
        }
        return;
    }
    if (in_rpr) {
        uint8_t off = (uint8_t)(aval("w:val", "0") || aval("w:val", "false") || aval("w:val", "none"));
        if (is("w:b")) { if (off) attrs &= (uint8_t)~A_B; else attrs |= A_B; }
        else if (is("w:i")) { if (off) attrs &= (uint8_t)~A_I; else attrs |= A_I; }
        else if (is("w:u")) { if (off) attrs &= (uint8_t)~A_U; else attrs |= A_U; }
        return;
    }
    if (is("w:tab")) emit_tab();
    else if (is("w:br")) {
        if (aval("w:type", "page")) { if (!in_tbl) { para_end(); pkind = P_PAGE; para_end(); } }
        else emit(0x0A);                              /* a line break inside the paragraph */
    }
    else if (is("w:tbl")) in_tbl = 1;
    else if (is("w:tr")) { pkind = P_ROW; cell_n = 0; }
    else if (is("w:tc")) { if (cell_n++) { emit(' '); emit(0xB3); emit(' '); } pcell = 0; }
}
/* the XML at XMLBUF, len bytes, through one of the three tag handlers */
static void parse(uint32_t len, void (*tag)(void), uint8_t text)
{
    int c; unsigned u;
    xp = XMLBUF; xend = XMLBUF + len; xi = xn = 0;
    while ((c = xget()) >= 0) {
        if (c == '<') { if (read_tag() < 0) break; if (tn[0]) tag(); continue; }
        if (!text || !in_t) continue;
        if (c == '&') { ent(); continue; }
        if (c < 0x80) { emit((uint8_t)c); continue; }
        if ((c & 0xE0) == 0xC0) { u = (unsigned)(c & 0x1F) << 6; c = xget(); u |= (unsigned)(c & 0x3F); }
        else if ((c & 0xF0) == 0xE0) { u = (unsigned)(c & 0x0F) << 12; c = xget(); u |= (unsigned)(c & 0x3F) << 6; c = xget(); u |= (unsigned)(c & 0x3F); }
        else { xget(); xget(); xget(); u = '?'; }    /* beyond the BMP: an emoji, as a ? */
        emit(cp_of(u));
    }
}

/* ---- opening -------------------------------------------------------------- */
static uint8_t fs_mount(const char *zip, const char *at)
{
    w32(0xD304, (uint32_t)(uint16_t)zip); w32(0xD308, (uint32_t)(uint16_t)at);
    return fs_do(19);
}
static void fs_umount(const char *at) { w32(0xD304, (uint32_t)(uint16_t)at); fs_do(20); fs_do(14); }   /* and the folder MOUNT made for it */
static uint8_t ext_is(const char *n, const char *e)
{
    const char *d = 0;
    for (; *n; n++) if (*n == '.') d = n;
    if (!d) return 0;
    for (d++; *d && *e && up((uint8_t)*d) == *e; d++, e++) ;
    return (uint8_t)(!*d && !*e);
}
static void reset_doc(void)
{
    uint8_t i, j;
    dp = DOC; npara = 0; pn = 0; pkind = P_TEXT; pjc = 0; plvl = 0; pnum = 0; attrs = 0;
    nsty = 0; in_t = in_ppr = in_rpr = in_tbl = in_link = 0;
    n_num = n_abs = 0;
    for (i = 0; i < NNUM; i++) for (j = 0; j < 4; j++) cnt[i][j] = 0;
}
static uint8_t open_txt(void)                         /* a text file: a line to a paragraph */
{
    uint32_t len = load_part(name, XMLBUF); int c;
    if (!len) return 0;
    xp = XMLBUF; xend = XMLBUF + len; xi = xn = 0;
    while ((c = xget()) >= 0) {
        if (c == '\n') para_end();
        else if (c == '\t') emit_tab();
        else if (c != '\r') emit((uint8_t)c);
    }
    if (pn) para_end();
    return 1;
}
static uint8_t open_docx(void)
{
    static const char doc[] = MNT "/word/document.xml", sty[] = MNT "/word/styles.xml", num[] = MNT "/word/numbering.xml";
    uint32_t len; uint8_t st;
    load_part(name, XMLBUF);                          /* the first bytes: a .doc of Word 97-2003 is no zip */
    if (far_peek(XMLBUF) == 0xD0 && far_peek(XMLBUF + 1) == 0xCF) { note = "A Word 97-2003 .DOC -- WORD reads .DOCX (Word 2007 and later)"; return 0; }
    st = fs_mount(name, MNT);
    if (st) { note = "Not a .DOCX WORD can open (the zip would not mount)"; return 0; }
    if ((len = load_part(sty, XMLBUF))) parse(len, styles_tag, 0);
    if ((len = load_part(num, XMLBUF))) parse(len, numbering_tag, 0);
    len = load_part(doc, XMLBUF);
    fs_umount(MNT);
    if (!len) { note = "No word/document.xml inside: not a Word document"; return 0; }
    parse(len, doc_tag, 1);
    if (pn) para_end();
    return 1;
}

/* ---- the layout ------------------------------------------------------------
 * Each paragraph wrapped to the window, a word at a time.  A screen line is
 * 8 bytes in LINES: the paragraph's header (4), the first character (2),
 * how many (1), and flags (1: bit0 its first line, bit7 a blank). */
static unsigned nlines, top;
static uint8_t th, tw, margin = 2;
static uint8_t lb[8];
static void line_put(unsigned n, uint32_t h, unsigned s, uint8_t c, uint8_t f)
{
    lb[0] = (uint8_t)h; lb[1] = (uint8_t)(h >> 8); lb[2] = (uint8_t)(h >> 16); lb[3] = (uint8_t)(h >> 24);
    lb[4] = (uint8_t)s; lb[5] = (uint8_t)(s >> 8); lb[6] = c; lb[7] = f;
    far_put(lb, LINES + ((uint32_t)n << 3), 8);
}
static uint8_t ph[7];                                 /* a paragraph's header, read back */
static uint8_t prefix_w(void)                         /* the bullet's or the number's width, with its space */
{
    unsigned v = (unsigned)ph[3] | ((unsigned)ph[4] << 8); uint8_t n = 2;
    if (ph[0] != P_LIST) return 0;
    if (!v) return 2;
    while (v >= 10) { v /= 10; n++; }
    return (uint8_t)(n + 1);
}
static uint8_t indent_of(void) { return (uint8_t)(ph[0] == P_LIST ? 2 + ph[2] * 3 : 0); }
static void layout(void)
{
    uint32_t h = DOC; unsigned p, len, s, e, b; uint8_t w, prev = 0xFF, lead;
    nlines = 0;
    for (p = 0; p < npara && nlines < 60000u; p++) {
        far_get(h, ph, 7);
        len = (unsigned)ph[5] | ((unsigned)ph[6] << 8);
        if (len) far_get(h + 7, pb, len * 2);
        lead = (uint8_t)(indent_of() + prefix_w());
        w = (uint8_t)(tw - 2 * margin - lead);
        if (w < 10) w = 10;
        /* A blank line between paragraphs -- not inside a list, not between a
         * table's rows, and not around an empty paragraph: a document that
         * spaces itself with empty paragraphs (a letter, usually) would come
         * out double-spaced.  An empty paragraph is one blank line itself. */
        if (!len && ph[0] != P_PAGE) { line_put(nlines++, h, 0, 0, 0x80); prev = 0xFE; h += 7; continue; }
        if (nlines && prev != 0xFE && !(ph[0] == P_LIST && prev == P_LIST) && !(ph[0] == P_ROW && prev == P_ROW)) line_put(nlines++, h, 0, 0, 0x80);
        prev = ph[0];
        if (ph[0] == P_PAGE) { line_put(nlines++, h, 0, 0, 1); h += 7; continue; }
        s = 0;
        while (s < len) {
            e = s;
            while (e < len && e - s < w && pb[e * 2] != 0x0A) e++;
            if (e < len && pb[e * 2] != 0x0A && e - s >= w) {   /* too long: back to the last space */
                for (b = e; b > s && pb[b * 2] != ' '; b--) ;
                if (b > s) e = b;
            }
            line_put(nlines++, h, s, (uint8_t)(e - s), (uint8_t)(s == 0 ? 1 : 0));
            s = e;
            if (s < len && (pb[s * 2] == ' ' || pb[s * 2] == 0x0A)) s++;   /* the space (or the break) the line ended at */
        }
        h += 7 + (uint32_t)len * 2;
    }
}

/* ---- the screen --------------------------------------------------------------- */
static uint8_t col_of(uint8_t a, uint8_t kind)        /* a character's colour, by its attributes and its paragraph */
{
    if (sysc) {
        uint8_t d = kf[K_TEXT], b = kb[K_TEXT];
        if (kind == P_H1 || kind == P_TITLE || (a & A_B)) return (uint8_t)(b != 1 && d != 1 ? 1 : d);
        if (a & (A_I | A_U | A_LINK)) return kf[K_MHOT] != b ? kf[K_MHOT] : d;
        return d;
    }
    if (kind == P_H1 || kind == P_TITLE) return 7;   /* yellow */
    if (kind == P_H2) return 1;                       /* white */
    if (kind == P_H3) return 3;                       /* cyan */
    if (a & A_LINK) return 14;                        /* light blue */
    if ((a & (A_B | A_I)) == (A_B | A_I)) return 7;
    if (a & A_B) return 1;
    if (a & A_I) return 3;
    if (a & A_U) return 13;                           /* light green */
    return 15;
}
static unsigned fline = 0xFFFF; static uint8_t fcol, flen;   /* a match found, lit */
#pragma bss-name (push, "BIGBSS")
static uint8_t cb[2 * 184];
#pragma bss-name (pop)
static void text_row(uint8_t r)
{
    unsigned l = top + r, s, v; uint32_t h; uint8_t c, n, x, i, f, lead, bg = kb[K_TEXT], fg;
    cel(0, 0xB3, K_FRAME);
    for (x = 1; x < cols - 1; x++) cel(x, ' ', K_TEXT);
    if (l < nlines) {
        far_get(LINES + ((uint32_t)l << 3), lb, 8);
        h = (uint32_t)lb[0] | ((uint32_t)lb[1] << 8) | ((uint32_t)lb[2] << 16) | ((uint32_t)lb[3] << 24);
        s = (unsigned)lb[4] | ((unsigned)lb[5] << 8); n = lb[6]; f = lb[7];
        if (!(f & 0x80)) {
            far_get(h, ph, 7);
            if (ph[0] == P_PAGE) {
                for (x = 1; x < cols - 1; x++) celc(x, 0xC4, 12, bg);
                for (i = 0; " page break "[i]; i++) celc((uint8_t)(cols / 2 - 6 + i), (uint8_t)" page break "[i], 12, bg);
            } else {
                if (n) far_get(h + 7 + (uint32_t)s * 2, cb, (unsigned)n * 2);
                lead = (uint8_t)(indent_of() + prefix_w());
                x = (uint8_t)(1 + margin + lead);
                if (ph[1] == 1 || ph[0] == P_TITLE) x = (uint8_t)(1 + (tw - n) / 2);
                else if (ph[1] == 2) x = (uint8_t)(1 + tw - margin - n);
                if ((f & 1) && ph[0] == P_LIST) {     /* the bullet or the number, on its first line */
                    uint8_t px = (uint8_t)(1 + margin + indent_of());
                    v = (unsigned)ph[3] | ((unsigned)ph[4] << 8);
                    if (!v) celc(px, 0x07, kf[K_TEXT], bg);
                    else { char d[6]; uint8_t k = 0; do { d[k++] = (char)('0' + v % 10); v /= 10; } while (v); while (k) celc(px++, (uint8_t)d[--k], kf[K_TEXT], bg); celc(px, '.', kf[K_TEXT], bg); }
                }
                for (i = 0; i < n && x < cols - 1; i++, x++) {
                    c = cb[i * 2]; if (c == 0x0A) c = ' ';
                    fg = col_of(cb[i * 2 + 1], ph[0]);
                    if (l == fline && i >= fcol && i < fcol + flen) celc(x, c, kf[K_SEL], kb[K_SEL]);
                    else celc(x, c, fg, bg);
                }
            }
        }
    }
    flush((uint8_t)(2 + r), (uint8_t)(cols - 1));
}
static void band_file(void)
{
    const char *b = base_of(name); uint8_t i;
    REG(0xD544) = 0;
    for (i = 0; b[i]; i++) REG(0xD544) = (uint8_t)b[i];
}
static unsigned long words;
static void count_words(void)
{
    uint32_t h = DOC; unsigned p, len, i; uint8_t inw;
    words = 0;
    for (p = 0; p < npara; p++) {
        far_get(h, ph, 7); len = (unsigned)ph[5] | ((unsigned)ph[6] << 8);
        if (len) far_get(h + 7, pb, len * 2);
        for (inw = 0, i = 0; i < len; i++) { uint8_t c = pb[i * 2]; if (c == ' ' || c == 0x0A || c == 0xB3) inw = 0; else if (!inw) { inw = 1; words++; } }
        h += 7 + (uint32_t)len * 2;
    }
}
static void draw(void)
{
    uint8_t r; char p[40];
    if (top + th > nlines) top = nlines > th ? nlines - th : 0;
    if (full) { band_file(); menubar(-1); frame_top(1, name[0] ? base_of(name) : (const char *)"(no document)", 1); }
    for (r = 0; r < th; r++) text_row(r);
    vbar_at((uint8_t)(cols - 1), 2, (uint8_t)(rows - 3), thumb(top, nlines > th ? nlines - th + 1 : 1, vtrack(2, (uint8_t)(rows - 3))));
    { uint8_t x; cel(0, 0xC0, K_FRAME); for (x = 1; x < cols - 1; x++) cel(x, 0xC4, K_FRAME); cel((uint8_t)(cols - 1), 0xD9, K_FRAME); flush((uint8_t)(rows - 2), cols); }
    nb_reset(); nb_s("Line "); nb_n(nlines ? top + 1 : 0); nb_s(" of "); nb_n(nlines); nb_s("   "); nb_n(words); nb_s(" words");
    for (r = 0; nbuf[r]; r++) p[r] = nbuf[r];
    p[r] = 0;
    status_line(*note ? note : (const char *)"WORD  <F1=Help>  <F10 or Alt=Menus>", p);
    full = 0;
}

/* ---- the commands ---------------------------------------------------------- */
static void open_name(const char *nm)
{
    uint8_t i, ok;
    for (i = 0; nm[i] && i < NAMEMAX - 1; i++) name[i] = nm[i];
    name[i] = 0;
    reset_doc(); top = 0; fline = 0xFFFF; note = "";
    if (!name[0]) ok = 0;
    else if (ext_is(name, "TXT") || ext_is(name, "BAS") || ext_is(name, "C") || ext_is(name, "PAS")) ok = open_txt();
    else ok = open_docx();
    if (!ok) { if (!*note) note = "Could not open it"; reset_doc(); name[0] = 0; }
    layout(); count_words(); full = 1;
}
static void save_text(void)                           /* what is shown, as plain text: a paragraph to a line */
{
    uint32_t h = DOC, o = FLAT; unsigned p, len, i, v; uint8_t c, nl = '\n';
    fbuf[0] = 0;
    { const char *b = base_of(name); for (i = 0; b[i] && b[i] != '.' && i < NAMEMAX - 5; i++) fbuf[i] = b[i]; fbuf[i++] = '.'; fbuf[i++] = 'T'; fbuf[i++] = 'X'; fbuf[i++] = 'T'; fbuf[i] = 0; }
    if (!form1("Save As Text", "File Name:", fbuf, NAMEMAX, "OK") || !fbuf[0]) return;
    for (p = 0; p < npara; p++) {
        far_get(h, ph, 7); len = (unsigned)ph[5] | ((unsigned)ph[6] << 8);
        if (len) far_get(h + 7, pb, len * 2);
        if (ph[0] == P_LIST) {
            for (i = 0; i < ph[2] * 2u; i++) { c = ' '; far_put(&c, o++, 1); }
            v = (unsigned)ph[3] | ((unsigned)ph[4] << 8);
            if (!v) { far_put("* ", o, 2); o += 2; }
            else { char d[8]; uint8_t k = 0; do { d[k++] = (char)('0' + v % 10); v /= 10; } while (v); while (k) { c = (uint8_t)d[--k]; far_put(&c, o++, 1); } far_put(". ", o, 2); o += 2; }
        }
        for (i = 0; i < len; i++) { c = pb[i * 2]; if (c == 0xB3) c = '|'; else if (c == 0x0A) c = '\n'; far_put(&c, o++, 1); }
        far_put(&nl, o++, 1);
        h += 7 + (uint32_t)len * 2;
    }
    zp16(0xF0, (uint16_t)fbuf); zp32(0xF2, FLAT); zp32(0xF6, o - FLAT);
    note = rom_save() ? "Not saved" : "Saved as text";
}
static void find_next(void)                           /* from the line after the top, a screen line at a time, any case */
{
    unsigned l, s; uint32_t h; uint8_t n, i, j, pl = slen(sbuf);
    if (!pl) return;
    for (l = (fline == 0xFFFF ? top : fline + 1); l < nlines; l++) {
        far_get(LINES + ((uint32_t)l << 3), lb, 8);
        if (lb[7] & 0x80) continue;
        n = lb[6]; if (n < pl) continue;
        h = (uint32_t)lb[0] | ((uint32_t)lb[1] << 8) | ((uint32_t)lb[2] << 16) | ((uint32_t)lb[3] << 24);
        s = (unsigned)lb[4] | ((unsigned)lb[5] << 8);
        far_get(h + 7 + (uint32_t)s * 2, cb, (unsigned)n * 2);
        for (i = 0; i + pl <= n; i++) {
            for (j = 0; j < pl && up(cb[(i + j) * 2]) == up((uint8_t)sbuf[j]); j++) ;
            if (j == pl) { fline = l; fcol = i; flen = pl; if (l < top || l >= top + th) top = l > 2 ? l - 2 : 0; return; }
        }
    }
    fline = 0xFFFF; note = "Match not found";
}
static const char *const helptext[] = {
    "Up, Down        a line             PgUp, PgDn    a screen",
    "Ctrl+Home, End  the beginning, the end of the document",
    "Ctrl+F, F3      find (any case), find again",
    "Ctrl+O          open a .DOCX or a .TXT",
    "Ctrl+S          save what is shown as plain text",
    "Ctrl+Q, Esc     leave",
    "F10, Alt+letter the menus;  the mouse: the menus, the scroll bar, the wheel",
    "",
    "Bold is white, italic cyan, both yellow, underlined green, a link blue.",
    "WORD -s FILE starts in the console's own colours (View).",
    0 };
static const char *const abouttext[] = { "WORD -- reads Microsoft Word's .DOCX", "", "In MS-DOS EDIT's manner.  It reads; it does not write .DOCX:", "File > Save As Text writes what it shows.", 0 };
/* View > Tab Width: a Tab in a document is shown as this many spaces -- spaces,
 * never a tab character (Doc, 2026-10-05).  WORD reads, so there is no Tab key
 * to press; the width is how far a Word tab is drawn.  The file is read again
 * so the change shows. */
static void tabw_dlg(void)
{
    char b[4]; uint8_t o = wtabw, n = 0, i;
    if (o > 9) { b[0] = '1'; b[1] = (char)('0' + o - 10); b[2] = 0; } else { b[0] = (char)('0' + o); b[1] = 0; }
    if (!form1("Tab Width", "Spaces per Tab (1-16):", b, 3, "OK")) return;
    for (i = 0; b[i] >= '0' && b[i] <= '9'; i++) n = (uint8_t)(n * 10 + (b[i] - '0'));
    if (!i || b[i] || n < 1 || n > 16) { note = "Tab width is 1 to 16 spaces"; return; }
    wtabw = n;
    if (name[0]) { strcpy(fbuf, name); open_name(fbuf); }
}
static void run_cmd(uint8_t c)
{
    switch (c) {
    case C_OPEN:    if (open_dialog(fbuf)) open_name(fbuf); full = 1; break;
    case C_SAVETXT: if (npara) save_text(); break;
    case C_EXIT:    running = 0; break;
    case C_FIND:    if (form1("Find", "Find What:", sbuf, NAMEMAX, "OK") && sbuf[0]) { fline = 0xFFFF; find_next(); } break;
    case C_NEXT:    find_next(); break;
    case C_TOP:     top = 0; break;
    case C_END:     top = nlines > th ? nlines - th : 0; break;
    case C_DOS:     scheme(0); full = 1; break;
    case C_SYS:     scheme(1); full = 1; break;
    case C_TABW:    tabw_dlg(); break;
    case C_HELP:    text_box("WORD -- the keys", helptext); break;
    case C_ABOUT:   text_box("About", abouttext); break;
    }
}
static const char *const mtitle[] = { "File", "Search", "View", "Help" };
static const struct item m_file[]   = { { "Open...", 0, C_OPEN, "Ctrl+O" }, { "Save As Text...", 8, C_SAVETXT, "Ctrl+S" },
                                        { "", 0, C_SEP, "" }, { "Exit", 1, C_EXIT, "Ctrl+Q" }, { 0, 0, 0, 0 } };
static const struct item m_search[] = { { "Find...", 0, C_FIND, "Ctrl+F" }, { "Find Next", 5, C_NEXT, "F3" }, { 0, 0, 0, 0 } };
static const struct item m_view[]   = { { "Beginning", 0, C_TOP, "Ctrl+Home" }, { "End", 0, C_END, "Ctrl+End" }, { "", 0, C_SEP, "" },
                                        { "DOS Colours", 0, C_DOS, "" }, { "System Colours", 0, C_SYS, "" }, { "", 0, C_SEP, "" },
                                        { "Tab Width...", 0, C_TABW, "" }, { 0, 0, 0, 0 } };
static const struct item m_help[]   = { { "Keyboard", 0, C_HELP, "F1" }, { "About WORD...", 0, C_ABOUT, "" }, { 0, 0, 0, 0 } };
static const struct item *const menus[] = { m_file, m_search, m_view, m_help };
static uint8_t marked(uint8_t c) { return (uint8_t)((c == C_DOS && !sysc) || (c == C_SYS && sysc)); }

static uint8_t vdrag;
static void scroll(int d)
{
    if (d < 0) top = top > (unsigned)-d ? top + d : 0;
    else { top += (unsigned)d; if (top + th > nlines) top = nlines > th ? nlines - th : 0; }
}
static void vset(uint8_t r)
{
    unsigned n = vtrack(2, (uint8_t)(rows - 3)), y = r < 3 ? 0 : r - 3, span = nlines > th ? nlines - th : 0;
    if (y >= n) y = n - 1;
    top = n > 1 ? (unsigned)((unsigned long)y * span / (n - 1)) : 0;
}
static void do_key(uint8_t k)
{
    uint8_t i, ctrl = (uint8_t)(kmod & 2);
    if (kcode == 2) {
        if (mev == 4) { scroll(-3 * mwheel); return; }   /* + is away from you: up */
        if (mev == 3) { vdrag = 0; return; }
        if (mev == 2) { if (vdrag) vset(mrow); return; }
        if (mrow == 0) { i = title_at(mcol); if (i < ui_nmenu) run_cmd(menu(i)); return; }
        if (mcol == cols - 1 && mrow >= 2 && mrow <= rows - 3) {
            uint8_t t = thumb(top, nlines > th ? nlines - th + 1 : 1, vtrack(2, (uint8_t)(rows - 3)));
            if (mrow == 2) scroll(-1);
            else if (mrow == rows - 3) scroll(1);
            else if (mrow - 3 == t) vdrag = 1;
            else scroll(mrow - 3 < t ? -(int)th : (int)th);
        }
        return;
    }
    if (kcode && k >= KALT && k < KALT + 26) { i = title_of(k); if (i < ui_nmenu) run_cmd(menu(i)); return; }
    if (!kcode) {
        switch (k) {
        case 0x0F: run_cmd(C_OPEN); return;
        case 0x13: run_cmd(C_SAVETXT); return;
        case 0x11: case 0x1B: run_cmd(C_EXIT); return;
        case 0x06: run_cmd(C_FIND); return;
        case ' ':  scroll(th - 1); return;
        }
        return;
    }
    switch (k) {
    case KUP:    scroll(-1); break;
    case KDOWN:  scroll(1); break;
    case KPGUP:  scroll(-(int)(th - 1)); break;
    case KPGDN:  scroll(th - 1); break;
    case KHOME:  if (ctrl) run_cmd(C_TOP); break;
    case KEND:   if (ctrl) run_cmd(C_END); break;
    case KF(1):  run_cmd(C_HELP); break;
    case KF(3):  run_cmd(C_NEXT); break;
    case KF(10): run_cmd(menu(0)); break;
    }
}

void main(void)
{
    uint8_t k, na = rom_args(), j = 0, sys = 0; const char *a = *(const char **)0xF0;
    char nm[NAMEMAX];
    for (;;) {                                        /* WORD [-s] [name] -- the name may have spaces in it */
        while (na && *a == ' ') { a++; na--; }
        if (na >= 2 && a[0] == '-' && (a[1] == 's' || a[1] == 'S') && (na == 2 || a[2] == ' ')) { sys = 1; a += 2; na -= 2; continue; }
        break;
    }
    while (j < na && j < NAMEMAX - 1) { nm[j] = a[j]; j++; }
    while (j && nm[j - 1] == ' ') j--;
    nm[j] = 0;
    ui_init();
    ui_titles = mtitle; ui_menus = menus; ui_nmenu = 4; ui_marked = marked; ui_dirtab = 0x0BF00000UL; ui_name = "WORD ";
    th = (uint8_t)(rows - 4); tw = (uint8_t)(cols - 2);
    scheme(sys);
    REG(TERM + 4) = 1; cursor_show(0);
    ptr_on();
    open_name(nm);
    if (!nm[0]) note = "Ctrl+O opens a Word document (.DOCX)";
    while (running) {
        draw();
        cursor_show(0);
        k = event();
        if (kcode != 2 || mev == 1) note = "";
        do_key(k);
    }
    ptr_off();
    put(27); put('['); put('2'); put(' '); put('q');
    REG(TERM + 0x0E) = 0; REG(TERM + 4) = 1; REG(TERM + 4) = 2;
    rom_video();
}
