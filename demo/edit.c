/* K4510: EDIT name -- a small full-screen editor, the nano of this machine.
 *
 * The text is one flat buffer at $0800, below the program itself, so the file
 * is edited in place and SAVE writes it straight back out. Keys arrive raw
 * from the ROM (GETIN gives K4510 codes, so an arrow is one byte and there is
 * no escape sequence to unpick), and the screen goes out through JIM, the
 * VT100 at $DA00, so drawing is ANSI: position, clip, erase to end of line.
 * That division is the whole trick -- raw in, ANSI out.
 *
 *   arrows Home End PgUp PgDn   move        Enter        split the line
 *   Backspace Delete           rub out      printable    insert
 *   Ctrl-O / Ctrl-S            save         Ctrl-X       leave
 */
#include "k4510.h"

#define TERM   0xDA00u
#define BUF    ((char *)0x0800)
#define BUFMAX 0x5700u                      /* $0800-$5EFF: everything under the program */
#define NAMEMAX 64

void __fastcall__ rom_chrout(unsigned char c);
unsigned char rom_getin(void);
static unsigned char rom_args(void) { return ((unsigned char (*)(void))0xFF95)(); }
static unsigned char rom_load(void) { return ((unsigned char (*)(void))0xFF89)(); }
static unsigned char rom_save(void) { return ((unsigned char (*)(void))0xFF8C)(); }
static void rom_video(void) { ((void (*)(void))0xFF92)(); }
static void zp16(uint8_t a, uint16_t v) { REG(a) = v; REG(a + 1) = v >> 8; }
static void zp32(uint8_t a, uint32_t v) { REG(a) = v; REG(a+1) = v>>8; REG(a+2) = v>>16; REG(a+3) = v>>24; }
static uint32_t zpr32(uint8_t a) { return (uint32_t)REG(a) | ((uint32_t)REG(a+1)<<8) | ((uint32_t)REG(a+2)<<16) | ((uint32_t)REG(a+3)<<24); }

static char name[NAMEMAX];
static unsigned len, cur, top;              /* bytes used; cursor; offset of the top screen line */
static uint8_t cols, rows, dirty, running = 1;
static const char *msg = "";
static uint8_t quitp;            /* Ctrl-X seen once on a modified file */
/* Only what changed.  Redrawing every row on every keypress left the raster
 * permanently somewhere inside a half-written screen, which shows as a fast
 * flicker of wrong cells: a text32 cell is four bytes and the beam does not
 * wait for all four.  Typing now rewrites the cursor's line and the status;
 * a full pass is asked for when the line structure or the view moves. */
static uint8_t full = 1, lasthoff = 0xFF, lastcrow = 0xFF;
static unsigned lasttop = 0xFFFF;

/* ---- the screen, through JIM ------------------------------------------- */
static uint8_t clip, sx;         /* while clip is set, drop anything past the right edge:
                                    on the last row a wrap scrolls the whole screen */
static void put(char c) { if (clip) { if (sx >= cols) return; sx++; } REG(TERM) = (uint8_t)c; }
static void say(const char *s) { while (*s) put(*s++); }
static void num(unsigned v) { char b[6]; uint8_t i = 0; if (!v) { put('0'); return; } while (v) { b[i++] = (char)('0' + v % 10); v /= 10; } while (i) put(b[--i]); }
static void at(uint8_t r, uint8_t c) { put(27); put('['); num((unsigned)r + 1); put(';'); num((unsigned)c + 1); put('H'); }
static void eeol(void) { put(27); put('['); put('K'); }
static void sgr(const char *s) { put(27); put('['); say(s); put('m'); }

/* ---- lines in a flat buffer -------------------------------------------- */
static unsigned bol(unsigned o) { while (o && BUF[o - 1] != '\n') o--; return o; }
static unsigned eol(unsigned o) { while (o < len && BUF[o] != '\n') o++; return o; }
static unsigned nextl(unsigned o) { o = eol(o); return o < len ? o + 1 : o; }
static unsigned prevl(unsigned o) { o = bol(o); return o ? bol(o - 1) : 0; }

static void openup(unsigned at_, unsigned n)      /* n bytes of room at at_ */
{
    unsigned i = len;
    while (i > at_) { i--; BUF[i + n] = BUF[i]; }
    len += n;
}
static void closeup(unsigned at_, unsigned n)
{
    unsigned i = at_;
    while (i + n < len) { BUF[i] = BUF[i + n]; i++; }
    len -= n;
}

/* ---- drawing ------------------------------------------------------------ */
static unsigned line_no(void)
{
    unsigned o = 0, n = 1;
    while (o < cur) { if (BUF[o] == '\n') n++; o++; }
    return n;
}
static void draw(void)
{
    unsigned o = top, e;
    uint8_t r, c, hoff = 0, crow = 0, ccol;
    /* where the cursor sits, and how far the line must slide to show it */
    { unsigned s = bol(cur), t = top;
      ccol = (uint8_t)(cur - s);
      while (t < s) { t = nextl(t); crow++; } }
    if (ccol >= cols) hoff = (uint8_t)(ccol - cols + 1);
    if (top != lasttop || hoff != lasthoff) full = 1;
    lasttop = top; lasthoff = hoff; lastcrow = crow;
    if (!full) {                                       /* just the cursor's line */
        unsigned s2 = bol(cur), e2 = eol(cur), w2 = e2 - s2;
        at(crow, 0);
        for (c = 0; (unsigned)(c + hoff) < w2 && c < cols; c++) put(BUF[s2 + c + hoff]);
        eeol();
        goto status;
    }
    full = 0;
    for (r = 0; r < rows - 1; r++) {
        at(r, 0);
        if (o <= len) {
            e = eol(o);
            { unsigned w = e - o;                          /* a line may be longer than a byte */
              for (c = 0; (unsigned)(c + hoff) < w && c < cols; c++) put(BUF[o + c + hoff]); }
            o = e < len ? e + 1 : len + 1;         /* len+1 marks "past the last line" */
        }
        eeol();
    }
status:
    at((uint8_t)(rows - 1), 0);
    sgr("7");
    clip = 1; sx = 0;                                /* text only: the escapes are not columns */
    say(" "); say(name[0] ? name : "(no name)");
    if (dirty) say(" *");
    say("  line "); num(line_no()); say(" col "); num((unsigned)ccol + 1);
    if (*msg) { say("   "); say(msg); }              /* the message earns the room over the hints */
    else say("   ^O save  ^X exit");
    clip = 0;
    eeol();                                          /* fill to the edge and no further: printing cols
                                                        spaces here overran the last line and scrolled
                                                        the whole screen up by one */
    sgr("0");
    at(crow, (uint8_t)(ccol - hoff));
}

/* ---- files -------------------------------------------------------------- */
static uint8_t too_big;
static uint8_t file_fits(void)             /* FS_LOAD ignores LEN: a file bigger than BUF landed on this program */
{
    REG(0xD304) = (uint8_t)(uint16_t)name; REG(0xD305) = (uint8_t)((uint16_t)name >> 8); REG(0xD306) = 0; REG(0xD307) = 0;
    REG(0xD300) = 8;                       /* STAT */
    if (REG(0xD301)) return 1;             /* absent: a new file */
    return REG(0xD312) == 0 && REG(0xD313) == 0 && ((uint16_t)REG(0xD310) | (uint16_t)REG(0xD311) << 8) <= BUFMAX;
}
static void load_file(void)
{
    uint8_t st;
    if (!file_fits()) { too_big = 1; len = 0; return; }
    zp16(0xF0, (uint16_t)name); zp32(0xF2, (uint32_t)(uint16_t)BUF);
    st = rom_load();
    len = st ? 0 : (unsigned)zpr32(0xF6);
    if (len > BUFMAX) len = BUFMAX;
}
static void save_file(void)
{
    uint8_t st;
    zp16(0xF0, (uint16_t)name); zp32(0xF2, (uint32_t)(uint16_t)BUF); zp32(0xF6, (uint32_t)len);
    st = rom_save();
    if (st) msg = "NOT saved";
    else { msg = "saved"; dirty = 0; }
}

/* ---- keys --------------------------------------------------------------- */
static void scroll_into_view(void)
{
    unsigned s = bol(cur), t = top, n = 0;
    if (s < top) { top = s; return; }
    while (t < s) { t = nextl(t); n++; if (n > 400) break; }
    while (n > (unsigned)(rows - 2)) { top = nextl(top); n--; }
}
static void col_keep(uint8_t want)
{
    unsigned s = bol(cur), e = eol(cur);
    cur = s + want > e ? e : s + want;
}

void main(void)
{
    uint8_t k, n = rom_args(); const char *a = *(const char **)0xF0;
    uint8_t i = 0, want;
    while (n && *a == ' ') { a++; n--; }
    while (i < n && i < NAMEMAX - 1 && a[i] != ' ') { name[i] = a[i]; i++; }
    name[i] = 0;
    cols = REG(TERM + 5); rows = REG(TERM + 6);
    if (!cols) cols = 80;
    if (!rows) rows = 30;
    load_file();
    if (too_big) { say("edit: file bigger than the buffer (22 KB); not a file, or use VI\n"); return; }
    REG(TERM + 4) = 2;                                /* JIM: clear and home */
    REG(TERM + 0x0E) = 1;                             /* its cursor */
    while (running) {
        draw();
        do { k = rom_getin(); } while (!k);
        if (k != 0x18) quitp = 0;                     /* any other key takes back the intent to quit */
        msg = "";
        /* $80-$FF is either a KEY_* code or an accented letter; KBDST bit 6
         * says which the byte just read was.  A letter goes in like any other. */
        if (k >= 0x80 && !(REG(KBDST) & 0x40)) { if (len < BUFMAX) { openup(cur, 1); BUF[cur++] = (char)k; dirty = 1; } scroll_into_view(); continue; }
        switch (k) {
        case 0x82: if (cur) cur--; break;                          /* left  */
        case 0x83: if (cur < len) cur++; break;                    /* right */
        case 0x80: want = (uint8_t)(cur - bol(cur)); if (bol(cur)) { cur = prevl(cur); col_keep(want); } break;
        case 0x81: want = (uint8_t)(cur - bol(cur)); if (eol(cur) < len) { cur = nextl(cur); col_keep(want); } break;
        case 0x84: cur = bol(cur); break;                          /* home */
        case 0x85: cur = eol(cur); break;                          /* end  */
        case 0x86: for (i = 0; i < rows - 2; i++) cur = prevl(cur); break;
        case 0x87: for (i = 0; i < rows - 2; i++) if (eol(cur) < len) cur = nextl(cur); break;
        case 0x08: if (cur) { if (BUF[cur - 1] == '\n') full = 1; cur--; closeup(cur, 1); dirty = 1; } break;
        case 0x89: if (cur < len) { if (BUF[cur] == '\n') full = 1; closeup(cur, 1); dirty = 1; } break;   /* delete */
        case 0x0F: case 0x13: save_file(); break;                  /* Ctrl-O, Ctrl-S */
        case 0x18:                                                 /* Ctrl-X */
            if (dirty && !quitp) { quitp = 1; msg = "MODIFIED -- ^X again to discard, ^O to save"; }
            else running = 0;
            break;
        case 0x0D: if (len < BUFMAX) { openup(cur, 1); BUF[cur++] = '\n'; dirty = 1; full = 1; } break;
        default:
            if (k >= 0x20 && k < 0x7F && len < BUFMAX) { openup(cur, 1); BUF[cur++] = (char)k; dirty = 1; }
            break;
        }
        scroll_into_view();
    }
    REG(TERM + 0x0E) = 0;
    REG(TERM + 4) = 2;                                /* leave a clean screen, not our status bar */
    rom_video();
}
