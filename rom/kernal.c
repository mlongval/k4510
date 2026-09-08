/* K4510 system ROM, Stage 3. cc65, 65C02 subset of the 45GS10.
 *
 * A colour text terminal on VICKY text32, a keyboard driver, the host
 * filesystem, and a shell that keeps Wozmon's syntax and adds files,
 * 28-bit memory access through DMA, INFO, and a handful of utilities.
 * 24 KB ROM at $A000-$FFFF with the I/O hole at $D000 (rom/k4510.cfg).
 */
#include <stdint.h>
#include <string.h>

/* ---- hardware (mirrors core/io.h and core/vicky.h) -------------------- */
#define REG(a) (*(volatile uint8_t *)(a))
#define VICKY  0xD000u
#define KBD    0xD100u
#define KBDST  0xD101u
#define KEY_LEFT 0x82u                  /* the KEY_* codes readline edits with (core/io.h has the full set) */
#define KEY_RIGHT 0x83u
#define KEY_HOME 0x84u
#define KEY_END  0x85u
#define KEY_DEL  0x89u
#define DMA    0xD200u
#define FS     0xD300u
#define FM     0xD480u          /* the OPL2: $D480 address port, $D481 data */
#define SYS    0xD500u
#define SYSOPT_STATUS 0x08           /* $D521 bit 3: the host's status-bar mode is switched on */
#define SYS_BANDTOP   0x2D           /* $D52D/$D52E: rows in each band, the F7 Terminal menu's.  $D521 is
                                      * full -- all eight bits -- so these are their own bytes. */
#define SYS_BANDBOT   0x2E
#define SYS_CLOCKFMT  0x2F           /* bit0 24-hour; bits1-2 the date order (0 D.M.Y, 1 ISO, 2 M/D/Y) */
#define BAND_MIN_ROWS 10             /* the console never shrinks below this, whatever is asked for */
#define T_BANDTOP     0x0F           /* JIM: a PROGRAM's band heights, and FLAGS bit 3 to claim them */
#define T_BANDBOT     0x16
#define T_CLAIMED     0x08
#define BANK   0xD600u
#define TUBE   0xD800u
#define TERM   0xDA00u                /* JIM, the terminal: a VT100/ANSI in hardware (core/term.h) */

#define SCREEN   0x030000UL           /* text32 cells, 80x60 x 4 bytes, in far memory: the CPU's 64 KB is for programs */
#define FONT     0x010000UL           /* placed by the loader */
#define USER     0x0800u              /* free RAM for programs: $0800-$9FFF (38 KB); .prg files say where they load */
#define USER_END 0xA000u
#define MAXCOLS 80
#define MAXROWS 60
#pragma code-name ("CODE2")
/* Resident code defaults to CODE2 (the $E000 half). Cold commands live in
 * the sideways window $A000-$BFFF: bank 0 is the base image (SWCODE0),
 * banks 1+ are appended 8 KB images called through sw_call(). */
static uint8_t COLS, ROWS, vmode, margin;            /* MODE 0: 80x60 (640x480)  1: 80x30 (640x240)  2: 40x30 (320x240); video_init sets them */
static uint8_t PCOLS, PROWS;                         /* physical text cells; with margin = 1 the terminal uses (PCOLS-1)x(PROWS-1) from (1,1) */
uint8_t OY;                                          /* status mode: top-band height (the console origin).
                                                      * Exported since 2026-09-02: the IRQ reads it, because
                                                      * the clock lives in the TOP band and a bottom height
                                                      * of zero must not stop it ticking. */
uint8_t bband;                                       /* bottom-band height.  NOT the "bands are on" flag any
                                                      * more -- the heights are independent, so either may be
                                                      * zero with the other set; ask bands_on() instead. */
#define OX margin
#define ROM_VERSION "stage 4"

#define C_BG   0x06   /* VIC-II blue     */
#define C_FG   0x07   /* yellow          */
#define C_HI   0x01   /* white           */
#define C_ERR  0x0A   /* light red       */
#define C_DIM  0x0C   /* grey            */

/* ---- terminal ---------------------------------------------------------- */
static uint8_t cx, cy, fg = C_FG, bg = C_BG;
static uint8_t mode_note;                  /* an F7 mode/status change was performed: the shell repaints (LOGO) at its next prompt */
static const char *args_tail;                /* the command tail, for the ARGS system call */
static char args_none;
extern volatile uint8_t ticks, cursor_vis;       /* crt0.s */
static uint8_t prog_running;                     /* set around call_prog: no console cursor under a program (RANGER's stray block) */
extern uint32_t cursor_far;                      /* crt0.s: far address of the cell attribute under the cursor */
uint16_t speed_loop(void);                       /* crt0.s */
void __fastcall__ far_poke(unsigned long a, unsigned char v);   /* crt0.s: 45GS10 flat store */
unsigned char __fastcall__ far_peek(unsigned long a);           /* crt0.s: 45GS10 flat load, ~10 cycles */
void __fastcall__ call_prog(unsigned addr);                     /* crt0.s: JSR with the ROM zero page saved around it */

static void w32(uint16_t r, uint32_t v) { REG(r) = v; REG(r + 1) = v >> 8; REG(r + 2) = v >> 16; REG(r + 3) = v >> 24; }
static void w16(uint16_t r, uint16_t v) { REG(r) = v; REG(r + 1) = v >> 8; }
static uint32_t r32(uint16_t r) { return (uint32_t)REG(r) | ((uint32_t)REG(r + 1) << 8) | ((uint32_t)REG(r + 2) << 16) | ((uint32_t)REG(r + 3) << 24); }
static uint16_t r16(uint16_t r) { return (uint16_t)REG(r) | ((uint16_t)REG(r + 1) << 8); }

static uint32_t cell(uint8_t x, uint8_t y) { return SCREEN + ((uint16_t)(y + OY) * PCOLS + x + OX) * 4; }
#define ROWTPL   0x03F000UL           /* far: one blank text row in the current colours */
static uint8_t tpl_fg, tpl_bg, tpl_cols, cellbuf[4];
#pragma code-name (push, "CODE")   /* resident either way: ROM1C is where the room is */
static void blank_row(uint8_t y)        /* y is a PHYSICAL row: margins included */
{
    if (tpl_fg != fg || tpl_bg != bg || tpl_cols != PCOLS) {     /* (re)build the template: one cell, copied across */
        uint8_t i;
        cellbuf[0] = ' '; cellbuf[1] = 0; cellbuf[2] = fg; cellbuf[3] = bg;
        w32(DMA + 0, (uint16_t)cellbuf); w32(DMA + 8, 4);
        for (i = 0; i < PCOLS; i++) { w32(DMA + 4, ROWTPL + (uint16_t)i * 4); REG(DMA + 12) = 1; }
        tpl_fg = fg; tpl_bg = bg; tpl_cols = PCOLS;
    }
    /* a whole physical row (the margin column stays blank because every row is blanked whole) */
    w32(DMA + 0, ROWTPL); w32(DMA + 4, SCREEN + (uint32_t)y * PCOLS * 4); w32(DMA + 8, PCOLS * 4); REG(DMA + 12) = 1;
}

#pragma code-name (pop)

static void draw_cursor(uint8_t on)
{
    uint32_t c = cell(cx, cy) + 1;
    cursor_vis = 0;
    far_poke(c, on ? 0x80 : 0x00);           /* reverse bit = cursor; IRQ blinks it */
    cursor_far = c;
    cursor_vis = on;
}

/* ---- the status bands -------------------------------------------------- *
 * Two static bars frame the console when status mode is on.  The console is
 * a scroll region between them (scroll() only ever moves OY..OY+ROWS-1), so
 * the bands sit still while text scrolls -- a VT100 DECSTBM done in the
 * machine's own layout.  Phase 1 draws a title bar and a status bar; the
 * blank spacer rows are where the widgets go later. */
#define BAND_FG  C_HI                 /* white on grey: a status bar, ancient or modern */
#define BAND_BG  0x0C
static void draw_clock(void);         /* the top-right widget.  It lived in ROM2 while ROM1C was full;
                                       * the 2026-09-01/02 savings gave ROM1C the room, and it belongs
                                       * beside bar_str, which is what it draws through. */
/* Are the bands up?  Not "is either height nonzero": with the bands off, OY
 * carries the one-cell margin instead, so OY alone would say yes to a margin.
 * The host's own switch is the only honest answer, and it costs no state --
 * which matters, BSSR being 447 of 448 bytes used. */
static uint8_t claimed(void) { return (uint8_t)((REG(TERM + 0x0E) & T_CLAIMED) && PCOLS == 80); }
/* A program that has claimed the bands gets them whether or not the user's F7
 * switch is on -- that is the point of claiming: a program wants the furniture
 * for its own, and asking the user to enable it first would be absurd. */
static uint8_t bands_on(void) { return (uint8_t)(claimed() || ((REG(SYS + 0x21) & SYSOPT_STATUS) && PCOLS == 80)); }
#pragma code-name (push, "CODE")      /* the band drawing lives in ROM1C, where the room is */
static void put_at(uint8_t px, uint8_t py, uint8_t ch, uint8_t f, uint8_t b)
{
    uint32_t a = SCREEN + ((uint32_t)py * PCOLS + px) * 4;
    far_poke(a, ch); far_poke(a + 1, 0); far_poke(a + 2, f); far_poke(a + 3, b);
}
static void bar_str(uint8_t px, uint8_t py, const char *s)
{
    while (*s) put_at(px++, py, (uint8_t)*s++, BAND_FG, BAND_BG);
}
static void bar_num(uint8_t rx, uint8_t py, uint16_t v)     /* right-anchored at column rx */
{
    do { put_at(rx--, py, (uint8_t)('0' + v % 10), BAND_FG, BAND_BG); v /= 10; } while (v);
}
static uint8_t day_col(void)
{
    uint8_t k = (uint8_t)((REG(SYS + SYS_CLOCKFMT) >> 1) & 3);
    return (uint8_t)(PCOLS - (k == 1 ? 2 : k == 2 ? 7 : 10));
}
static void draw_bands(void)
{
    uint8_t i, ofg = fg, obg = bg, last = PROWS - 1;
    uint16_t mhz = (uint16_t)(((uint32_t)r16(SYS) | ((uint32_t)REG(SYS + 0x26) << 16)) / 1000);
    fg = BAND_FG; bg = BAND_BG;                                  /* the bars, each only if it has a band */
    if (OY) blank_row(0);
    if (bband) blank_row(last);
    fg = ofg; bg = obg;                                          /* the spacers, in the console's colours */
    for (i = 1; i < OY; i++) blank_row(i);
    for (i = OY + ROWS; i < last; i++) blank_row(i);
    /* Dropped 2026-09-02, both of them (Doc): "K4510  K/OS" top left and
     * "status mode" bottom left.  A status bar should carry what is otherwise
     * invisible and what changes without being asked; those two told you what
     * you already knew, never changed, and held the best real estate on the
     * screen between them.  What is left earns its place: the clock, and the
     * CPU clock -- which the governor steps DOWN on its own when frames run
     * late, and is the one widget that shows the machine acting behind your
     * back. */
    if (OY) { (void)REG(SYS + 4); draw_clock(); }
    if (bband) { bar_str(PCOLS - 3, last, "MHz"); bar_num(PCOLS - 5, last, mhz); }
}
#pragma code-name (pop)

/* Two digits, then four.  Written as helpers because cc65 inlines a division
 * at every site and the spelled-out version cost ROM2 more than it has. */
static char *dig2(char *d, uint8_t v) { *d++ = (char)('0' + v / 10); *d++ = (char)('0' + v % 10); return d; }
static char *dig4(char *d, uint16_t v) { d = dig2(d, (uint8_t)(v / 100)); return dig2(d, (uint8_t)(v % 100)); }
static void draw_clock(void)
{
    /* Three date orders over one set of fields, rather than three spelled-out
     * layouts.  ord[] says which field goes where; the separator comes with it. */
    static const char sep[3] = { '.', '-', '/' };
    static const uint8_t ord[3][3] = { { 0, 1, 2 }, { 2, 1, 0 }, { 1, 0, 2 } };
    char b[20], *d = b;
    uint8_t f = REG(SYS + SYS_CLOCKFMT), j, w, h;
    uint8_t hh = REG(SYS + 7);
    uint8_t k = (uint8_t)((f >> 1) & 3);
    if (k > 2) k = 0;
    h = hh;
    if (!(f & 1)) { h = hh % 12; if (!h) h = 12; }        /* 12-hour: 0 and 12 both read 12 */
    d = dig2(d, h); *d++ = ':';                           /* the hour is PADDED, never narrowed: */
    d = dig2(d, REG(SYS + 6)); *d++ = ' ';                /* fixed width keeps the IRQ a digit poker */
    if (!(f & 1)) { *d++ = (hh >= 12) ? 'P' : 'A'; *d++ = 'M'; *d++ = ' '; }
    for (j = 0; j < 3; j++) {
        w = ord[k][j];
        if (w == 2) d = dig4(d, r16(SYS + 0x0A));
        else        d = dig2(d, REG(SYS + (w ? 9 : 8)));   /* 8 = day, 9 = month */
        if (j < 2) *d++ = sep[k];
    }
    *d = 0;
    /* Right-anchored, so the date always ends in the last column and always
     * occupies the last ten: 16 cells at 24-hour, 19 with the AM/PM. */
    bar_str((uint8_t)(PCOLS - (uint8_t)(d - b)), 0, b);
}

/* Which column holds the first digit of the day, for the once-a-day repaint
 * in k_getin.  The date is the last ten cells whatever the format; only the
 * day's place inside it moves. */
/* The clock widget: HH:MM DD.MM.YYYY at the top-right.  This lays down the
 * whole string once (separators and the year included); the machine's IRQ
 * (crt0.s) then repaints the eight digits every minute, so it ticks even
 * inside a program that never calls the console.  The caller latches the RTC
 * (a read of SYS+4) first.  In ROM2, called from ROM1C's draw_bands. */

static void cls(void)
{
    uint8_t i;
    if (bands_on()) {                                  /* status mode: clear the console window, keep the bands */
        for (i = OY; i < OY + ROWS; i++) blank_row(i);
        if (!claimed()) draw_bands();                  /* claimed: the rows are the program's, and a CLS
                                                        * from inside it must not wipe what it drew */
    } else {
        for (i = 0; i < PROWS; i++) blank_row(i);       /* every physical row, the margins with them */
    }
    cx = cy = 0;
    REG(TERM + 9) = 0; REG(TERM + 10) = 0;      /* the cursor is JIM's: moving it means telling it */
}

static uint16_t band_mhz = 0xFFFF;           /* what the status band's clock last showed */
static uint8_t paging, paged_out;            /* newline() pages while paging is set; paged_out is
                                              * the reader having said q -- the caller checks it,
                                              * since newline cannot abort anyone itself */
static uint8_t page_break(void);

/* The console is JIM's (D-11).  Every byte the machine prints goes to the
 * terminal at $DA00, which owns the wrap, the scroll, the tab stops and the
 * cursor arithmetic -- one renderer on the screen instead of the two that used
 * to share it.  cx/cy are read back rather than tracked: they are still the
 * console's own, because JIM draws in the console's window (video_init hands it
 * COLS/ROWS/OX/OY/STRIDE), and the full-screen UI still writes cells directly.
 *
 * Three bytes keep their old meaning rather than JIM's, because every caller in
 * this ROM already means the old one:
 *   $08  destructive: JIM's backspace only steps left, ours rubs out
 *   $0C  clear: cls() keeps the status bands, JIM's own clear would not
 *   $0D  a full newline, as CHROUT has always promised.  LNM (set in video_init)
 *        makes JIM's \n return the column, and CR is folded onto it -- EhBASIC's
 *        glue, BBC BASIC and CP/M all send a bare CR and mean "next line", and
 *        JIM's own CR is a carriage return only.  Folding it here keeps every
 *        existing program working; not folding it overprints their output.
 * JIM's fg/bg are pushed only when they have actually changed, which is far
 * cheaper than two stores per character. */
static uint8_t jim_fg = 0xFF, jim_bg = 0xFF;
void __fastcall__ k_chrout(uint8_t ch)
{
    uint8_t oy;
    draw_cursor(0);
    if (ch == 12) { cls(); return; }
    if (fg != jim_fg) { REG(TERM + 11) = fg; jim_fg = fg; }
    if (bg != jim_bg) { REG(TERM + 12) = bg; jim_bg = bg; }
    if (ch == 8) { REG(TERM) = 8; REG(TERM) = ' '; REG(TERM) = 8; cx = REG(TERM + 9); return; }
    if (ch == 13) ch = 10;
    oy = REG(TERM + 10);
    REG(TERM) = ch;
    cx = REG(TERM + 9); cy = REG(TERM + 10);
    if (paging && (ch == '\n' || cy != oy) && page_break()) paged_out = 1;
}

static void newline(void) { k_chrout('\n'); }

static void puts_(const char *s) { while (*s) k_chrout(*s++); }
static void puthex(uint8_t v) { static const char h[] = "0123456789ABCDEF"; k_chrout(h[v >> 4]); k_chrout(h[v & 15]); }
static void puthex16(uint16_t v) { puthex(v >> 8); puthex(v); }
static void puthex28(uint32_t v) { puthex(v >> 24); puthex(v >> 16); puthex(v >> 8); puthex(v); }
static void putdec(uint32_t v) { char b[11]; uint8_t i = 10; b[i] = 0; do { b[--i] = '0' + v % 10; v /= 10; } while (v); puts_(&b[i]); }
#pragma code-name (push, "SWCODE1")   /* its only caller is INFO, in sideways bank 1 */
static void putdec2(uint8_t v) { k_chrout('0' + v / 10); k_chrout('0' + v % 10); }
#pragma code-name (pop)
/* Pad to a column -- but never past the right margin.  k_chrout wraps cx back
 * to 0 there, so a target at or beyond COLS is a target cx can never reach,
 * and the loop never ends.  That is what hung MODE 4 (19 columns) on the
 * first DIR, alias listing or banner: pad(col+20), pad(col+12), pad(20). */
static void pad(uint8_t col)
{
    if (col >= COLS) { if (!COLS) return; col = (uint8_t)(COLS - 1); }
    while (cx < col) k_chrout(' ');
}
static void label(const char *s) { uint8_t o = fg; fg = C_HI; puts_(s); fg = o; pad(8); }
static void onoff(uint8_t v) { puts_(v ? "on" : "off"); }

/* ---- keyboard ---------------------------------------------------------- */
/* CAPSLOCK: when on, letters read from the keyboard come up uppercase, so
 * the language keywords (BBC BASIC, EhBASIC) need no Shift. Digits and
 * symbols are untouched -- a caps lock, not a shift lock. The flag lives
 * here and every key the ROM reads passes through caps(). */
static uint8_t capslock;
static uint8_t caps(uint8_t k)
{
    if (!capslock) return k;
    if (k >= 'a' && k <= 'z') return (uint8_t)(k - 32);
    if (k >= 'A' && k <= 'Z') return (uint8_t)(k + 32);   /* shifted: a caps lock gives the OTHER case */
    return k;
}
/* The host's F7 menu asks for a video mode through $D521 bits 5-7 (always
 * the wanted mode + 1; bit 4 says a change is asked for).  The ROM has to be the one to do it: the console's
 * PCOLS/PROWS/stride are the ROM's, and writing VICKY's CTRL alone would
 * leave the text laid out for the old mode.  Resident on purpose -- banked
 * commands read keys too, and sw_call does not nest.
 * No "is it already that mode?" test: the host holds the request only until
 * it sees VICKY's CTRL change, and doing it twice is doing it once.  That
 * keeps the whole thing ~20 bytes, which is what the resident ROM has. */
static void video_init(void);
static void cls(void);
#pragma code-name (push, "CODE2")   /* resident; ROM1C is full, ROM2 has the room */
static void mode_do(void)
{
    uint8_t r = REG(SYS + 0x21);
    REG(SYS + 0x21) = 0;                   /* acknowledge: the host drops the request the moment it is
                                            * performed.  Without this it stands for frames and every
                                            * key poll performs it again, and each cls() wipes whatever
                                            * the machine printed in between. */
    vmode  = (uint8_t)((r >> 5) - 1);           /* bits 5-7 carry mode+1: 0 is "nothing published" */
    margin = (r & SYSOPT_STATUS) ? 0 : (uint8_t)((r >> 1) & 1);   /* the status bands frame the screen; no margin with them */
    video_init(); cls();
    mode_note = 1;
}
#pragma code-name (pop)

/* GETIN shows the cursor while a program waits for a key (BASIC reads this way) */
static void draw_cursor(uint8_t on);
uint8_t k_getin(void)
{
    if (REG(SYS + 0x21) & 0x10) { mode_do(); return 13; }   /* rare: the F7 menu asked for a mode; the CR
                                                              * unsticks readline so the shell repaints now */
    if (REG(KBDST) & 0x80) { if (cursor_vis) draw_cursor(0); return caps(REG(KBD)); }
    /* Not while JIM is showing its own: a program that draws through the
     * terminal (VI, EDIT, anything under CP/M) polls this for keys, and the
     * console's cursor would be a second one -- blinking to a different
     * clock, parked on whatever cell the shell last left it on, reversing
     * whatever the program has since drawn there. */
    /* The band's clock follows the menu.  draw_bands() only runs from cls(), so
     * a clock changed in F7 used to leave yesterday's number sitting in the bar
     * until something cleared the screen (Doc, 2026-09-01).  This is the poll
     * every program already goes through, so it is where the number is kept
     * honest -- and it costs a compare per key poll, only while the bands are up.
     * The MHz sits in the BOTTOM band, so this asks for that one specifically:
     * with a bottom height of zero there is nowhere to put it. */
    if (bband && !claimed()) {
        uint16_t m = (uint16_t)(((uint32_t)r16(SYS) | ((uint32_t)REG(SYS + 0x26) << 16)) / 1000);
        if (m != band_mhz) { band_mhz = m; bar_num(PCOLS - 5, (uint8_t)(PROWS - 1), m); }
    }
    /* And the date, once a day.  The IRQ keeps HH:MM right -- that is the part
     * that has to tick inside a program which never polls -- but it is
     * deliberately not taught the three date orders, because a format-aware
     * painter in the interrupt is a lot of assembler guarding a thing that
     * changes at midnight.  So the day is checked HERE, against what is
     * actually on the glass rather than against a remembered value: BSSR has
     * one byte left in it, and this needs none.  It also self-heals if
     * anything else scribbles on the clock. */
    if (OY && !claimed()) {
        uint8_t c = day_col();
        (void)REG(SYS + 4);                                   /* latch the RTC */
        if (far_peek(SCREEN + (uint32_t)c * 4) != (uint8_t)('0' + REG(SYS + 8) / 10)) draw_clock();
    }
    if (REG(TERM + 0x0E) || prog_running) { if (cursor_vis) draw_cursor(0); }   /* a program owns the screen: no console cursor under it */
    else if (!cursor_vis) draw_cursor(1);
    return 0;
}

uint8_t k_chrin(void)
{
    uint8_t k;
    while (!(k = k_getin())) ;
    return k;
}

#pragma code-name (push, "SWCODE3")   /* the line editor: only ever run at a prompt, never under a program, so a bank of its own (bank 1 filled up when it learned to edit, 2026-09-08) */
#pragma rodata-name (push, "SWRODATA3")
/* The line editor.  The cursor can be anywhere in the line: Left/Right move
 * it, Home/End jump, Backspace takes the character before it, Delete the one
 * under it, Esc clears the line, Enter takes it.  Typed characters go in at
 * the cursor.  Up/Down do nothing yet (no history: the ROM has no RAM for
 * one).  Until 2026-09-08 none of this existed and an arrow key printed the
 * accented letter that shares its code -- KBDST bit 6 is what tells an $82
 * that is Left from an $82 that is é. */
static void rl_left(void)                 /* one cell back, up a row if the line wrapped: JIM's own $08 stops at column 0 */
{
    if (REG(TERM + 9) == 0) { REG(TERM + 10) = (uint8_t)(REG(TERM + 10) - 1); REG(TERM + 9) = (uint8_t)(REG(TERM + 5) - 1); }
    else REG(TERM) = 8;
    cx = REG(TERM + 9); cy = REG(TERM + 10);
}
static void rl_tail(const char *buf, uint8_t p, uint8_t n, uint8_t pad)   /* repaint from p to the end, pad blanks, cursor back to p */
{
    uint8_t i;
    for (i = p; i < n; i++) k_chrout((uint8_t)buf[i]);
    for (i = 0; i < pad; i++) k_chrout(' ');
    for (i = (uint8_t)(n - p + pad); i; i--) rl_left();
}
static uint8_t readline(char *buf, uint8_t max)
{
    uint8_t n = 0, p = 0, k, i, key;
    for (;;) {
        draw_cursor(1);
        k = k_chrin();
        key = (uint8_t)(k >= 0x80 && (REG(KBDST) & 0x40));   /* a KEY_* code, not a character that shares its byte */
        if (k == 13) { draw_cursor(0); for (i = p; i < n; i++) k_chrout((uint8_t)buf[i]); buf[n] = 0; newline(); return n; }
        if (k == 8) {                                                   /* backspace: the character before the cursor */
            if (!p) continue;
            p--; n--; rl_left();
            for (i = p; i < n; i++) buf[i] = buf[i + 1];
            rl_tail(buf, p, n, 1); continue;
        }
        if (k == 27) { while (p) { p--; rl_left(); } rl_tail(buf, 0, 0, n); n = 0; continue; }   /* clear the line */
        if (key) {
            switch (k) {
            case KEY_LEFT:  if (p) { p--; rl_left(); } break;
            case KEY_RIGHT: if (p < n) { k_chrout((uint8_t)buf[p]); p++; } break;
            case KEY_HOME:  while (p) { p--; rl_left(); } break;
            case KEY_END:   while (p < n) { k_chrout((uint8_t)buf[p]); p++; } break;
            case KEY_DEL:   if (p < n) { n--; for (i = p; i < n; i++) buf[i] = buf[i + 1]; rl_tail(buf, p, n, 1); } break;
            default: break;                                             /* Up, Down, Insert, the F-keys: nothing, and no glyph */
            }
            continue;
        }
        /* $80-$FF are the font's code page 437 half: accented letters a host
         * dead key composes and the frontend hands over as one byte.  $7F is
         * the only printable code excluded. */
        if (k >= 0x20 && k != 0x7F && n < max - 1) {
            for (i = n; i > p; i--) buf[i] = buf[i - 1];
            buf[p] = (char)k; n++;
            k_chrout(k); p++;
            if (p < n) rl_tail(buf, p, n, 0);
        }
    }
}
static void readline_sw(const char *buf) { readline((char *)buf, 96); }   /* 96 = sizeof line, declared below */
#pragma code-name (pop)
#pragma rodata-name (pop)

/* ---- filesystem -------------------------------------------------------- */
static uint8_t fs_cmd(uint8_t cmd) { REG(FS) = cmd; return REG(FS + 1); }
static void fs_name(const char *name) { w32(FS + 4, (uint16_t)name); }

/* jump-table entry points (crt0.s saves and restores the ROM's zero page around
 * each, so a program may own $00-$FF) use $F0.. as the parameter block */
#define P_NAME  (*(volatile uint16_t *)0xF0)
#define P_ADDR  (*(volatile uint32_t *)0xF2)
#define P_LEN   (*(volatile uint32_t *)0xF6)
uint8_t k_load(void) { uint8_t st; w32(FS + 4, P_NAME); w32(FS + 8, P_ADDR); st = fs_cmd(9); P_LEN = r32(FS + 12); return st; }
uint8_t k_save(void) { w32(FS + 4, P_NAME); w32(FS + 8, P_ADDR); w32(FS + 12, P_LEN); return fs_cmd(10); }

/* ---- 28-bit memory through DMA ------------------------------------------ */
static uint8_t dmabuf[16];
static void dma_copy(uint32_t src, uint32_t dst, uint32_t len) { w32(DMA, src); w32(DMA + 4, dst); w32(DMA + 8, len); REG(DMA + 12) = 1; }
static void dma_fill(uint8_t v, uint32_t dst, uint32_t len) { REG(DMA) = v; w32(DMA + 4, dst); w32(DMA + 8, len); REG(DMA + 12) = 2; }
static uint8_t peek(uint32_t a)
{
    if (a < 0x10000UL) return *(uint8_t *)(uint16_t)a;              /* CPU view: I/O and ROM as the CPU sees them */
    dma_copy(a, (uint16_t)dmabuf, 1); return dmabuf[0];
}
static void poke(uint32_t a, uint8_t v)
{
    if (a < 0x10000UL) { *(uint8_t *)(uint16_t)a = v; return; }
    dmabuf[0] = v; dma_copy((uint16_t)dmabuf, a, 1);
}

/* ---- shell ------------------------------------------------------------- */
static char line[96];
static uint32_t xam;          /* last opened address */
static uint8_t mode;          /* 0 xam, 1 store, 2 block */
#define NAMEMAX 96    /* a name or a URL (the Meatloaf rule): as long as the shell line itself */
static char last_name[NAMEMAX]; static uint32_t last_addr, last_len; static uint16_t last_run; static uint8_t last_segs, last_bmask;   /* last LOAD; bmask = blocks claimed by K4SG segments */

static uint8_t ishex(char c) { return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f'); }
static uint8_t hexval(char c) { return c <= '9' ? c - '0' : (c | 0x20) - 'a' + 10; }
static uint32_t parsehex(const char **p, uint8_t *digits)
{
    uint32_t v = 0; *digits = 0;
    while (ishex(**p)) { v = (v << 4) | hexval(**p); (*p)++; (*digits)++; }
    return v;
}
static void skipsp(const char **p) { while (**p == ' ') (*p)++; }
static uint8_t getname(const char **p, char *name)
{
    uint8_t i = 0;
    skipsp(p);
    while (**p && **p != ' ' && i < NAMEMAX - 1) name[i++] = *(*p)++;
    name[i] = 0; skipsp(p);
    return i;
}
/* case-insensitive command match; on success *p points past the word */
static uint8_t is_cmd(const char **p, const char *cmd)
{
    const char *s = *p;
    while (*cmd) { if (((*s) | 0x20) != ((*cmd) | 0x20)) return 0; s++; cmd++; }
    if (*s && *s != ' ') return 0;
    *p = s; skipsp(p);
    return 1;
}

static void dump(uint32_t from, uint32_t to)
{
    uint8_t n = 0;
    for (; from <= to; from++) {
        if (n == 0) { puthex28(from); puts_(": "); }
        puthex(peek(from)); k_chrout(' ');
        if (++n == 16) { n = 0; newline(); }
        if (from == 0x0FFFFFFFUL) break;
    }
    if (n) newline();
}

/* $03FF is the shell's result byte: error() sets it, the SHELL system call
 * ($FF8F) clears it before the line and returns it after, so a program (RX)
 * learns whether the command it sent failed.  Page $03 belongs to programs. */
#define SHELL_RC (*(volatile uint8_t *)0x03FF)
static void error(const char *m) { uint8_t o = fg; fg = C_ERR; puts_(m); newline(); fg = o; SHELL_RC = 1; }
static void put_cwd(void);

#pragma code-name (push, "SWCODE0")
#pragma rodata-name (push, "SWRODATA0")
static char upc(char c) { return (c >= 'a' && c <= 'z') ? (char)(c - 32) : c; }
/* DIR *.PAS: * any run, ? any one, case-insensitive like the rest of the shell.
 * The usual backtracking matcher, small enough to not be worth a table. */
static uint8_t wild(const char *pat, const char *s)
{
    const char *star = 0, *ss = 0;
    for (;;) {
        if (!*s) { while (*pat == '*') pat++; return !*pat; }
        if (*pat == '?' || (*pat && upc(*pat) == upc(*s))) { pat++; s++; continue; }
        if (*pat == '*') { star = pat++; ss = s; continue; }
        if (star) { pat = star + 1; s = ++ss; continue; }
        return 0;
    }
}

static void cmd_dir(const char *p)
{
    char name[64], pat[NAMEMAX], back[64]; uint16_t count = 0; uint32_t total = 0, sz;   /* the device writes entries of at most 64 */
    uint8_t first = 6, haspat, went = 0;                 /* DIR A: dotfiles too */
    if ((*p | 0x20) == 'a' && (!p[1] || p[1] == ' ')) { first = 18; p++; }
    haspat = getname(&p, pat);                           /* a pattern, or a directory to look in */
    if (haspat) {                                        /* no * or ? in it: it names a directory, so go and look */
        const char *w = pat; went = 1;
        while (*w) { if (*w == '*' || *w == '?') { went = 0; break; } w++; }
        if (went) {
            haspat = 0;
            w32(FS + 8, (uint16_t)back); fs_cmd(15);      /* remember where we are */
            fs_name(pat);
            if (fs_cmd(11)) { error("dir: no such directory"); return; }
        }
    }
    if (fs_cmd(first)) { error("dir: no device"); return; }
    { uint8_t o = fg; fg = C_HI; puts_("directory of "); put_cwd(); fg = o; newline(); }
    for (;;) {
        uint8_t col = cx;
        w32(FS + 8, (uint16_t)name);
        if (fs_cmd(7)) break;
        if (haspat && !wild(pat, name)) continue;
        sz = r32(FS + 16);
        if (sz == 0xFFFFFFFFUL) { uint8_t o = fg; fg = C_HI; puts_(name); fg = o; pad(col + 20); puts_("<DIR>"); }
        else { puts_(name); pad(col + 20); putdec(sz); count++; total += sz; }
        if (COLS >= 78 && col == 0) pad(COLS / 2); else newline();
    }
    if (cx) newline();
    putdec(count); puts_(" file(s), "); putdec(total); puts_(" bytes"); newline();
    if (went) { fs_name(back); fs_cmd(11); }             /* and back where we started */
}

static void cmd_cd(const char *p)
{
    char name[NAMEMAX];
    if (!getname(&p, name)) strcpy(name, "/");
    fs_name(name);
    if (fs_cmd(11)) { error("cd: no such directory"); return; }
}
#pragma code-name (push, "SWCODE1")   /* cold: bank 1 (ROM2 was full, 2026-09-07) */
#pragma rodata-name (push, "SWRODATA1")
static void cmd_mkdir(const char *p)
{
    char name[NAMEMAX];
    if (!getname(&p, name)) { error("mkdir: name?"); return; }
    fs_name(name);
    if (fs_cmd(12)) { error("mkdir: failed"); return; }
}
#pragma code-name (pop)
#pragma rodata-name (pop)
/* RM moves a file to /.TRASH; RM -f is the one that really removes it.
 *
 * The safe default is the machine's, not Unix's, and deliberately so: this
 * filesystem is a directory on somebody's laptop, so keeping a deleted file
 * until you say otherwise costs nothing, and not keeping it costs whatever
 * was in it.  -f is there because a default you cannot get out of is not a
 * default, it is a restriction -- a script clearing its temporary files wants
 * the real thing, and so does anyone reclaiming space.
 *
 * The trash is /.TRASH, shared with DELETE.prg and RANGER's DD, under the
 * same rule: a name already in there gets ~1, ~2 rather than being
 * overwritten.  That has to be asked with STAT beforehand -- the device's
 * RENAME takes the host's semantics and overwrites in silence, which is how
 * RANGER's first trash lost a file (docs/BUILD-LOG.md, 2026-08-29). */
static void cmd_rm(const char *p)
{
    char name[NAMEMAX], dst[NAMEMAX + 8];
    uint8_t force = 0, n, l;
    if (is_cmd(&p, "-f")) force = 1;
    if (!getname(&p, name)) { error("rm: name?"); return; }
    fs_name(name);
    if (fs_cmd(8)) { error("rm: not found"); return; }
    if (r32(FS + 0x10) == 0xFFFFFFFFUL) { error("rm: not a file"); return; }
    if (force) { fs_name(name); if (fs_cmd(13)) error("rm: failed"); return; }
    /* The name MUST be in RAM: the device reads it from physical memory at
     * that address, and a string literal lives in the ROM image, which is not
     * the RAM underneath it.  Every other fs_name() in this file passes a
     * buffer for exactly this reason. */
    strcpy(dst, "/.TRASH"); fs_name(dst); fs_cmd(12);  /* already there is fine */
    for (n = 0; n < 100; n++) {
        strcpy(dst, "/.TRASH/"); strcat(dst, name);
        if (n) {                                       /* taken: name~1, name~2, ... */
            l = (uint8_t)strlen(dst);
            dst[l++] = '~';
            if (n >= 10) dst[l++] = (char)('0' + n / 10);
            dst[l++] = (char)('0' + n % 10);
            dst[l] = 0;
        }
        fs_name(dst);
        if (fs_cmd(8)) break;                          /* STAT failed: the name is free */
    }
    fs_name(name); w32(FS + 8, (uint16_t)dst);
    if (fs_cmd(16)) { error("rm: could not move it to the trash"); return; }
}
#pragma code-name (push, "SWCODE1")   /* cold: bank 1 (ROM2 was full, 2026-09-07) */
#pragma rodata-name (push, "SWRODATA1")
static void cmd_rmdir(const char *p)
{
    char name[NAMEMAX]; uint8_t st;
    if (!getname(&p, name)) { error("rmdir: name?"); return; }
    fs_name(name);
    st = fs_cmd(14);
    if (st == 1) { error("rmdir: not found"); return; }
    if (st) { error("rmdir: not a directory, or not empty"); return; }
}
#pragma code-name (pop)
#pragma rodata-name (pop)
#pragma code-name (pop)
#pragma rodata-name (pop)
static void put_cwd(void) { char cwd[64]; w32(FS + 8, (uint16_t)cwd); fs_cmd(15); puts_(cwd); }

static uint8_t is_prg(const char *name)
{
    uint8_t n = strlen(name);
    return n > 4 && name[n - 4] == '.' && (name[n - 3] | 0x20) == 'p' && (name[n - 2] | 0x20) == 'r' && (name[n - 1] | 0x20) == 'g';
}

/* returns 0 on success. A .prg carries a 4-byte header: load address, run address.
 * A segmented program (K-03) starts with "K4SG" instead:
 *   4 nseg  5 flags  6-7 entry (CPU address)
 *   then nseg x 12 (nseg <= 8): phys[4] len[4] block (bank it there, $FF = no) pad[3]
 *   then the segments' bytes, in order. Each lands at its physical address
 *   anywhere in 256 MB; a block number also sets that bank register. */
#pragma code-name (push, "SWCODE0")   /* bank 0: 1.2K, and cold -- the resident half has no room for it.
                                      * Its rodata stays resident: ROM1C is mapped whatever the window holds. */
static uint8_t do_load(const char *name, uint32_t addr, uint8_t has_addr)
{
    uint8_t hdr[4];
    last_run = 0; last_segs = 0; last_bmask = 0;
    fs_name(name);
    if (is_prg(name)) {
        if (fs_cmd(1)) return 1;
        w32(FS + 8, (uint16_t)hdr); w32(FS + 12, 4);
        if (fs_cmd(3) || r32(FS + 12) != 4) { fs_cmd(5); return 2; }
        if (hdr[0] == 'K' && hdr[1] == '4' && hdr[2] == 'S' && hdr[3] == 'G') {
            static uint8_t tab[8 * 12];          /* the table comes first, then the segments' bytes */
            uint8_t i, n; uint32_t phys, len, total = 0;
            w32(FS + 8, (uint16_t)hdr); w32(FS + 12, 4);
            if (fs_cmd(3) || r32(FS + 12) != 4) { fs_cmd(5); return 2; }
            n = hdr[0]; last_run = hdr[2] | ((uint16_t)hdr[3] << 8);
            if (n == 0 || n > 8) { fs_cmd(5); return 2; }
            w32(FS + 8, (uint16_t)tab); w32(FS + 12, (uint32_t)n * 12);
            if (fs_cmd(3) || r32(FS + 12) != (uint32_t)n * 12) { fs_cmd(5); return 2; }
            for (i = 0; i < n; i++) {
                uint8_t *e = tab + i * 12;
                phys = r32((uint16_t)e) & 0x0FFFFFFFUL; len = r32((uint16_t)e + 4);
                if (len) {
                    w32(FS + 8, phys); w32(FS + 12, len);
                    if (fs_cmd(3) || r32(FS + 12) != len) { fs_cmd(5); return 2; }
                }
                if (e[8] < 5) w32(BANK + 4 * e[8], phys);           /* low blocks: engage at load */
                else if (e[8] < 8) {                                 /* blocks 5-7: base only -- engaging here would
                                                                        pull the ROM out from under this loader;
                                                                        the launch trampoline engages them */
                    REG(BANK + 4 * e[8])     = (uint8_t)phys;
                    REG(BANK + 4 * e[8] + 1) = (uint8_t)(phys >> 8);
                    REG(BANK + 4 * e[8] + 2) = (uint8_t)(phys >> 16);
                    last_bmask |= 1 << (e[8] & 7);
                }
                if (i == 0) addr = phys;
                total += len;
            }
            fs_cmd(5);
            last_len = total; last_segs = n;
        } else {
            if (!has_addr) addr = hdr[0] | ((uint16_t)hdr[1] << 8);
            last_run = hdr[2] | ((uint16_t)hdr[3] << 8);
            w32(FS + 8, addr); w32(FS + 12, 0x100000UL);
            if (fs_cmd(3)) { fs_cmd(5); return 2; }
            last_len = r32(FS + 12); fs_cmd(5);
        }
    } else {
        w32(FS + 8, addr);
        if (fs_cmd(9)) return 1;
        last_len = r32(FS + 12);
    }
    last_addr = addr; strcpy(last_name, name); xam = addr;
    return 0;
}
#pragma code-name (pop)

#pragma code-name (push, "SWCODE0")
#pragma rodata-name (push, "SWRODATA0")
static void cmd_load(const char *p)
{
    char name[NAMEMAX]; uint8_t d, st, has = 0; uint32_t addr = USER;
    if (!getname(&p, name)) { error("load: name?"); return; }
    if (*p) { addr = parsehex(&p, &d); has = 1; }
    st = do_load(name, addr, has);
    if (st == 1) { error("load: not found"); return; }
    if (st) { error("load: bad file"); return; }
    puts_("loaded "); putdec(last_len); puts_(" bytes at "); puthex28(last_addr);
    if (last_segs) { puts_(" in "); putdec(last_segs); puts_(" segments"); }
    if (last_run) { puts_(", run address "); puthex16(last_run); }
    newline();
}

#pragma code-name (push, "SWCODE1")   /* bank 1: cold, called through sw_call() */
#pragma rodata-name (push, "SWRODATA1")
static void cmd_save(const char *p)
{
    char name[NAMEMAX]; uint8_t d; uint32_t from, to;
    if (!getname(&p, name)) { error("save: name from.to"); return; }
    from = parsehex(&p, &d); if (!d || *p != '.') { error("save: name from.to"); return; }
    p++; to = parsehex(&p, &d); if (!d || to < from) { error("save: name from.to"); return; }
    fs_name(name); w32(FS + 8, from); w32(FS + 12, to - from + 1);
    if (fs_cmd(10)) { error("save: failed"); return; }
    puts_("saved "); putdec(to - from + 1); puts_(" bytes"); newline();
}
#pragma code-name (pop)
#pragma rodata-name (pop)

static uint8_t exec_busy;                    /* a script is running: nobody to press a key */
static uint8_t typed;                        /* lines since the last "-- more --" */
/* One screenful at a time.  Never while a script is running the command: there
 * is nobody to press the key, and the wait would hang STARTUP.BAT at power-on.
 * Returns 1 if the reader asked to stop. */
/* RESIDENT on purpose: newline() calls this on every line, and newline() is
 * resident.  Left in the sideways window it was a cross-bank call -- with bank
 * 1 engaged (INFO runs there) the window holds bank 1, not this, so newline
 * jumped into whatever was at that address.  sw_call's rules, learned again. */
#pragma code-name (push, "CODE2")
static uint8_t page_break(void)
{
    static uint8_t inside;                   /* its own printing goes through newline() too */
    uint8_t k, ofg;
    if (inside) return 0;
    if (exec_busy || ++typed < (uint8_t)(ROWS - 1)) return 0;
    inside = 1;
    typed = 0;
    ofg = fg; fg = C_DIM; puts_("-- more --"); fg = ofg;
    do { k = k_getin(); } while (!k);
    cx = 0; REG(TERM + 9) = 0; blank_row((uint8_t)(cy + OY));   /* take the prompt back off */
    inside = 0;
    return (uint8_t)(k == 27 || k == 'q' || k == 'Q');
}
#pragma code-name (pop)
#pragma code-name (push, "SWCODE1")   /* bank 1: cold, called through sw_call() */
#pragma rodata-name (push, "SWRODATA1")
static void cmd_type(const char *p)
{
    char name[NAMEMAX]; uint32_t n; uint16_t i;
    typed = 0;
    if (!getname(&p, name)) { error("type: name?"); return; }
    fs_name(name);
    if (fs_cmd(1)) { error("type: not found"); return; }
    for (;;) {
        w32(FS + 8, (uint16_t)line); w32(FS + 12, sizeof line);
        if (fs_cmd(3)) break;
        n = r32(FS + 12); if (!n) break;
        for (i = 0; i < n; i++) {
            k_chrout(line[i]);
            /* A screen at a time, so HELP does not scroll past.  Never when a
             * script is running it: there is nobody to press the key, and the
             * wait would hang STARTUP.BAT. */
            if (line[i] == '\n' && page_break()) { fs_cmd(5); return; }
        }
    }
    fs_cmd(5);
    typed = 0;
    if (cx) newline();
}
#pragma code-name (pop)
#pragma rodata-name (pop)

typedef void (*fn_t)(void);
#pragma code-name (pop)
#pragma rodata-name (pop)
static void video_init(void);
#pragma code-name (push, "CODE2")
/* Launching a program (stage 3 of the memory plan): the program owns
 * $0800-$CFFF and $E000-$FEFF by default. A RAM trampoline at $02D8
 * engages banks 5-7 onto the RAM under the ROM (skipping blocks a K4SG
 * load already claimed), calls the program, then turns every bank off.
 * It must live in always-visible RAM: once block 7 engages, the ROM
 * half that built it is gone until the next system call. */
#define TRAMP 0x02D8u
static void sw_call(uint8_t bank, void (*fn)(const char *), const char *p);
static void pal_snap(const char *p); static void pal_restore(const char *p); static void pal_reset16(const char *p);
static void run_at(uint16_t a)
{
    static const uint8_t tpl[] = {
        0xA9, 0x00,                                  /*      lda #0             */
        0xEA, 0xEA, 0xEA,                            /*      three slots: sta $D617 / $D61B / $D61F */
        0xEA, 0xEA, 0xEA,                            /*      (engage a block) or nop nop nop        */
        0xEA, 0xEA, 0xEA,
        0x20, 0x00, 0x00,                            /*      jsr program        */
        0xA2, 28, 0xA9, 0xFF,                        /*      ldx #28  lda #$FF  */
        0x9D, 0x03, 0xD6,                            /* @:   sta $D603,x        */
        0xCA, 0xCA, 0xCA, 0xCA,                      /*      dex x4             */
        0x10, 0xF7,                                  /*      bpl @              */
        0x60 };                                      /*      rts                */
    uint8_t *t = (uint8_t *)TRAMP, b, i;
    /* snapshot the video controls: a program that drew gets the text mode
     * put back and a clean screen; one that only printed keeps its output
     * on screen (so SAY, and disk commands like it, behave like commands) */
    /* Layer 0 is the console itself, so it is in the list: a program that
     * borrows it for a bitmap (CHESS at 640x480) and switches it off on the
     * way out left the mode unchanged and the console dark (2026-09-07). */
    uint8_t v0 = REG(VICKY + 0), bgc = REG(VICKY + 1), l0 = REG(VICKY + 0x10), l1 = REG(VICKY + 0x20), l2 = REG(VICKY + 0x30), l3 = REG(VICKY + 0x40), sc = REG(VICKY + 0x0E);
    for (i = 0; i < sizeof tpl; i++) t[i] = tpl[i];
    for (b = 5; b <= 7; b++) {
        uint8_t *slot = t + 2 + 3 * (b - 5);
        if (!(last_bmask & (1 << b))) {              /* base: the RAM under the ROM. Bytes 0-2 only -- */
            uint32_t base = (uint32_t)b << 13;       /* a byte-3 write ENGAGES, and engaging block 7  */
            REG(BANK + 4 * b)     = (uint8_t)base;   /* here would vaporise the ROM this code runs in */
            REG(BANK + 4 * b + 1) = (uint8_t)(base >> 8);
            REG(BANK + 4 * b + 2) = (uint8_t)(base >> 16);
        }                                            /* claimed blocks keep the base the loader set */
        slot[0] = 0x8D;                              /* sta $D603+4b: engage (byte 3 = 0, from RAM) */
        slot[1] = (uint8_t)(0x03 + 4 * b);
        slot[2] = 0xD6;
    }
    t[12] = (uint8_t)a; t[13] = (uint8_t)(a >> 8);
    draw_cursor(0);
    REG(TERM + 9) = cx; REG(TERM + 10) = cy; REG(TERM + 11) = fg; REG(TERM + 12) = bg;   /* JIM starts where the console is */
    sw_call(2, pal_snap, 0);                     /* the shell's palette, to come back to */
    { uint8_t cl = capslock; capslock = 0;       /* a program wants the keys as they were typed:
                                                 * with caps lock on, VI's :q arrives as :Q and
                                                 * there is no way out of the editor.  The shell
                                                 * gets its caps lock back when the program ends. */
      prog_running = 1; call_prog(TRAMP); prog_running = 0;
      capslock = cl; }
    if (REG(TERM + 1) & 1) { cx = REG(TERM + 9); cy = REG(TERM + 10); REG(TERM + 0x0E) = 0; }   /* and the console follows a program that used it */
    if (v0 != REG(VICKY + 0) || bgc != REG(VICKY + 1) || l0 != REG(VICKY + 0x10) || l1 != REG(VICKY + 0x20) || l2 != REG(VICKY + 0x30) ||
        l3 != REG(VICKY + 0x40) || sc != REG(VICKY + 0x0E)) {
        video_init();
        cls();
    }
    sw_call(2, pal_restore, 0);                  /* whatever the program did to the colours, undone */
}

/* EXEC name: run a file of shell lines (and /STARTUP.BAT at power-on). The file
 * is loaded whole into far memory first, so its own commands may use the
 * filesystem; one level only, lines up to 95 chars. */
#define EXECBUF 0x0FE00000UL

#pragma code-name (pop)
#pragma code-name (push, "SWCODE0")
#pragma rodata-name (push, "SWRODATA0")
static void cmd_run(const char *p)
{
    uint8_t d; uint32_t a; const char *q = p;
    while (ishex(*q)) q++;
    if (*p && *q && *q != ' ') {                  /* not a hex number: RUN name.prg */
        char name[NAMEMAX]; uint8_t st;
        getname(&p, name);
        st = do_load(name, USER, 0);
        if (st == 1 && !is_prg(name) && strlen(name) < NAMEMAX - 5) { strcat(name, ".prg"); st = do_load(name, USER, 0); }   /* RUN ehbasic -> ehbasic.prg */
        if (st == 1) { error("run: not found"); return; }
        if (st) { error("run: bad file"); return; }
        args_tail = p;
        if (!last_run) { error("run: not a program"); return; }
        run_at(last_run); return;
    }
    a = last_run ? last_run : (last_addr ? last_addr : xam);
    if (*p) a = parsehex(&p, &d);
    if (a >= 0x10000UL) { error("run: 16-bit address"); return; }
    run_at((uint16_t)a);
}

#pragma code-name (push, "SWCODE1")   /* bank 1: cold, called through sw_call() */
#pragma rodata-name (push, "SWRODATA1")
static void cmd_fill(const char *p)
{
    uint8_t d; uint32_t from, to, v;
    from = parsehex(&p, &d); if (!d || *p != '.') { error("fill: from.to value"); return; }
    p++; to = parsehex(&p, &d); if (!d || to < from) { error("fill: from.to value"); return; }
    skipsp(&p); v = parsehex(&p, &d); if (!d) { error("fill: from.to value"); return; }
    dma_fill((uint8_t)v, from, to - from + 1);
    putdec(to - from + 1); puts_(" bytes filled"); newline();
}

static void cmd_copy(const char *p)
{
    uint8_t d; uint32_t from, to, dst;
    from = parsehex(&p, &d); if (!d || *p != '.') { error("copy: from.to dest"); return; }
    p++; to = parsehex(&p, &d); if (!d || to < from) { error("copy: from.to dest"); return; }
    skipsp(&p); dst = parsehex(&p, &d); if (!d) { error("copy: from.to dest"); return; }
    dma_copy(from, dst, to - from + 1);
    putdec(to - from + 1); puts_(" bytes copied to "); puthex28(dst); newline();
}

static void video_init(void);
static const char *modename(uint8_t m)
{
    return m == 0 ? "640x480" : m == 1 ? "640x240" : m == 2 ? "320x240" : m == 3 ? "320x200" : "160x200";
}
static void cmd_mode(const char *p)
{
    uint8_t d; uint32_t m;
    if (!*p) { puts_("MODE "); putdec(vmode); puts_(": "); putdec(COLS); k_chrout('x'); putdec(ROWS); puts_(" text, ");
               puts_(modename(vmode)); puts_(" pixels, margin "); putdec(margin); puts_("   (MODE 0-2 [0|1])"); newline(); return; }
    /* 0-2 only. 3 (320x200) and 4 (160x200) leave too few columns to read a
     * way back out, and someone meeting the machine for the first time should
     * not be able to type themselves into a screen they cannot use. VICKY
     * still has them: a program that wants one writes the CTRL bits itself. */
    m = parsehex(&p, &d); if (!d || m > 2) { error("mode: 0 = 80x60 (640x480), 1 = 80x30 (640x240), 2 = 40x30 (320x240)  [0|1: margin]"); return; }
    vmode = (uint8_t)m; skipsp(&p);
    if (*p) { m = parsehex(&p, &d); if (!d || m > 1) { error("mode: second value 0 = full screen, 1 = one-cell margin"); return; } margin = (uint8_t)m; }
    video_init(); cls();
}

static void cmd_color(const char *p)
{
    uint8_t d; uint32_t f, b = bg;
    f = parsehex(&p, &d); if (!d) { error("color: fg [bg]  (palette indices, hex)"); return; }
    skipsp(&p); if (*p) b = parsehex(&p, &d);
    fg = (uint8_t)f; bg = (uint8_t)b; REG(VICKY + 1) = bg;
    cls();
}
#pragma code-name (pop)
#pragma rodata-name (pop)

/* ---- INFO ----------------------------------------------------------------- */
#pragma code-name (pop)
#pragma rodata-name (pop)
#pragma code-name (push, "SWCODE1")   /* sideways bank 1: INFO and TIME, called through sw_call() */
#pragma rodata-name (push, "SWRODATA1")
/* Flat char arrays, NOT const char*[]: a pointer array in a sideways bank
 * stores addresses the banking does not fix up, so puts_() reads the string
 * from whatever bank is mapped when it runs -- garbage. INFO showed it on
 * the layer-0 mode name for a long time. A char[] indexed in place is read
 * from this bank directly and has no such pointer to go stale. */
static const char daynames[] = "SunMonTueWedThuFriSat";   /* flat 3-char slots, indexed in place */

static void info_version(void)
{
    uint8_t i;
    label("SYSTEM"); puts_("K/OS " ROM_VERSION " (the K4510 operating system)");
    newline(); pad(8);
    puts_("build ");
    for (i = 0; i < 16 && REG(SYS + 0x10 + i); i++) k_chrout(REG(SYS + 0x10 + i));
    /* $D522: what is beneath the machine, so an issue says which one it came from */
    puts_(REG(SYS + 0x22) ? ", K4510x" : ", K4510 on a desktop");
    newline();
}

static void info_cpu(void)
{
    uint16_t it; uint32_t mhz100;
    /* three bytes: the ladder reaches 202500 kHz, past what SYS+0/1 hold */
    label("CPU"); puts_("45GS10: 4510 + Q register + 32-bit flat + 28-bit MAP, ");
    putdec((uint32_t)r16(SYS) | ((uint32_t)REG(SYS + 0x26) << 16)); puts_(" kHz"); newline();
    it = speed_loop();                                  /* 18 cycles per iteration, one frame */
    mhz100 = (uint32_t)it * 18UL * 60UL / 10000UL;
    pad(8); puts_("measured "); putdec(mhz100 / 100); k_chrout('.'); putdec2(mhz100 % 100); puts_(" MHz  (");
    putdec(it); puts_(" loop iterations per frame, 18 cycles each)"); newline();
}

static void info_mem(void)
{
    uint16_t rombase = (uint16_t)REG(SYS + 0x20) << 8;
    label("MEMORY"); putdec(r16(SYS + 2)); puts_(" MB physical, 28-bit, MAP + DMA + flat addressing"); newline();
    pad(8); puts_("CPU view: zp $0000-$00FF  stack $0100-$01FF  ROM data $0200-$02FF, $0440-$07FF"); newline();
    pad(8); puts_("user $0800-$9FFF ("); putdec((USER_END - USER) / 1024); puts_(" KB); ROM out: $0800-$CFFF + $E000-$FEFF (62 KB)"); newline();
    pad(8); puts_("(banks 5-7 -> $A000-$FEFF; I/O and the $FF00 page always stay)"); newline();
    pad(8); puts_("text screen at $030000 (far)"); newline();
    pad(8); puts_("I/O $D000-$DFFF  ROM $"); puthex16(rombase); puts_("-$FFFF ("); putdec((0x10000UL - rombase) / 1024); puts_(" KB)"); newline();
    pad(8); puts_("font at $0010000, MAP window convention $2000-$BFFF"); newline();
    { uint8_t b, any = 0;
      for (b = 0; b < 8; b++) if (REG(BANK + 4 * b + 3) != 0xFF) {
          if (!any) { pad(8); puts_("banks:"); any = 1; }
          puts_(" "); putdec(b); puts_("=$"); puthex28(r32(BANK + 4 * b)); }
      if (any) newline(); }
    if (last_len) { pad(8); puts_("last load: "); puts_(last_name); puts_(", "); putdec(last_len); puts_(" bytes at $"); puthex28(last_addr); if (last_run) { puts_(", run $"); puthex16(last_run); } newline(); }
}

static void info_video(void)
{
    uint8_t ctrl = REG(VICKY), n, L, lc, cnt = 0; uint32_t t; uint8_t i;
    label("VIDEO"); puts_("VICKY "); puts_((ctrl & 2) ? "320x240" : (ctrl & 4) ? "640x240" : "640x480"); puts_(" (MODE "); putdec(vmode); puts_(")"); puts_(", display ");
    onoff(ctrl & 1); puts_(", bg colour $"); puthex(REG(VICKY + 1)); puts_(", raster "); putdec(r16(VICKY + 2) & 0x1FF);
    puts_(", irq mask $"); puthex(REG(VICKY + 5)); newline();
    for (n = 0; n < 4; n++) {
        L = 0x10 + n * 0x10; lc = REG(VICKY + L);
        pad(8); puts_("layer "); k_chrout('0' + n); puts_(": ");
        if (!(lc & 1)) { puts_("off"); newline(); continue; }
        switch ((lc >> 1) & 3) { case 0: puts_("bitmap"); break; case 1: puts_("tile"); break;
                                 case 2: puts_("text8"); break; default: puts_("text32"); break; }
        k_chrout(' '); putdec(1 << ((lc >> 3) & 3)); puts_(" bpp");
        if (((lc >> 1) & 3) == 1) { puts_(", "); putdec(8 << ((lc >> 5) & 3)); puts_("px tiles"); }
        if (((lc >> 1) & 3) >= 2) { puts_(", 8x"); putdec((lc & 0x20) ? 16 : 8); puts_(" cells"); }
        puts_(", stride "); putdec(r16(VICKY + L + 6)); puts_(", scroll "); putdec(r16(VICKY + L + 2)); k_chrout(','); putdec(r16(VICKY + L + 4)); newline();
        pad(17); puts_("data $"); puthex28(r32(VICKY + L + 8)); puts_("  map $"); puthex28(r32(VICKY + L + 12)); newline();
    }
    t = r32(VICKY + 0x0A);
    pad(8); puts_("sprites "); onoff(REG(VICKY + 0x0E) & 1);
    if (REG(VICKY + 0x0E) & 1) {
        for (i = 0; i < 128; i++) if (peek(t + (uint32_t)i * 16 + 8) & 1) cnt++;
        puts_(", table $"); puthex28(t); puts_(", "); putdec(cnt); puts_(" of 128 enabled");
    }
    newline();
    pad(8); puts_("SHEILA "); onoff(REG(VICKY + 0x64) & 1); puts_(", list $"); puthex28(r32(VICKY + 0x60)); newline();
}

static void info_sound(void)
{
    uint8_t c, on = 0;
    label("SOUND"); puts_("OPL2 (YM3812) at $D480, nine FM voices"); newline();
    for (c = 0; c < 9; c++) { REG(FM) = (uint8_t)(0xB0 + c); if (REG(FM + 1) & 0x20) on++; }   /* key-on bits, from the data readback */
    pad(8); puts_("voices keyed on: "); putdec(on); puts_(" of 9; sequencer at $D5E0, four channels"); newline();
}

static void info_files(void)
{
    char name[NAMEMAX]; uint16_t count = 0; uint32_t total = 0;
    label("FILES"); puts_("host filesystem at $D300 (the emulator's fs/)");
    if (fs_cmd(6)) { puts_(": no device"); newline(); return; }
    for (;;) { w32(FS + 8, (uint16_t)name); if (fs_cmd(7)) break; total += r32(FS + 16); count++; }
    puts_(": "); putdec(count); puts_(" files, "); putdec(total); puts_(" bytes"); newline();
}

static void info_time(void)
{
    uint32_t f; uint32_t s;
    { volatile uint8_t d = REG(SYS + 4); (void)d; }      /* latch the host clock */
    label("TIME"); putdec(r16(SYS + 0x0A)); k_chrout('-'); putdec2(REG(SYS + 9)); k_chrout('-'); putdec2(REG(SYS + 8));
    k_chrout(' '); putdec2(REG(SYS + 7)); k_chrout(':'); putdec2(REG(SYS + 6)); k_chrout(':'); putdec2(REG(SYS + 5));
    { uint8_t dd = (REG(SYS + 12) % 7) * 3; k_chrout(' '); k_chrout(daynames[dd]); k_chrout(daynames[dd + 1]); k_chrout(daynames[dd + 2]); }
    f = (uint32_t)REG(SYS + 0x0D) | ((uint32_t)REG(SYS + 0x0E) << 8) | ((uint32_t)REG(SYS + 0x0F) << 16);
    s = f / 60;
    puts_(", up "); putdec(s / 3600); k_chrout(':'); putdec2((s / 60) % 60); k_chrout(':'); putdec2(s % 60);
    puts_(" ("); putdec(f); puts_(" frames)"); newline();
}

static void cmd_info(const char *p)
{
    uint8_t flags = 0;
    while (*p) {
        if (*p == '-') { p++; continue; }
        switch (*p | 0x20) {
        case 'v': flags |= 1; break;   case 'c': flags |= 2; break;   case 'm': flags |= 4; break;
        case 'g': flags |= 8; break;   case 's': flags |= 16; break;  case 'f': flags |= 32; break;
        case 't': flags |= 64; break;  case 'a': flags |= 127; break;
        case ' ': break;
        default: error("info [-v -c -m -g -s -f -t]  version cpu memory graphics sound files time"); return;
        }
        p++;
    }
    if (!flags) flags = 127;
    /* INFO is 28 lines: it fits a 30-row screen and does not fit MODE 3's 25.
     * Page it, unless a script is reading it, in which case page_break stands
     * down by itself. */
    paging = 1; paged_out = 0; typed = 0;
    if (paged_out) { paging = 0; return; }
    if (flags & 1)  info_version();
    if (paged_out) { paging = 0; return; }
    if (flags & 2)  info_cpu();
    if (paged_out) { paging = 0; return; }
    if (flags & 4)  info_mem();
    if (paged_out) { paging = 0; return; }
    if (flags & 8)  info_video();
    if (paged_out) { paging = 0; return; }
    if (flags & 16) info_sound();
    if (paged_out) { paging = 0; return; }
    if (flags & 32) info_files();
    if (paged_out) { paging = 0; return; }
    if (flags & 64) info_time();
    paging = 0;
}

static void cmd_time(const char *p) { (void)p; info_time(); }
#pragma code-name (pop)
#pragma rodata-name (pop)
/* run a command living in a sideways bank: engage it in the $A000-$BFFF
 * window (bank register 5), call, restore the ROM view. Syscalls inside
 * still work (the stub banks 5-7 off and back around every call); the
 * one rule is that sideways code must not call bank 0's commands. */
static void sw_call(uint8_t bank, void (*fn)(const char *), const char *p)
{
    /* The base bytes are the PROGRAM's when this is reached through a
     * system call: the stub banks 5-7 off around the call and re-engages
     * them on return with whatever base is in the register.  Leaving the
     * sideways bank's base there mapped the program's $A000-$BFFF onto the
     * bank's RAM -- RANGER's file preview read a buffer the device had
     * filled at the real address and saw only what the bank held (2026-09-07). */
    uint8_t b0 = REG(BANK + 20), b1 = REG(BANK + 21), b2 = REG(BANK + 22);
    w32(BANK + 20, 0x0FF00000UL + ((uint32_t)(bank - 1) << 13));
    fn(p);
    REG(BANK + 20) = b0; REG(BANK + 21) = b1; REG(BANK + 22) = b2;   /* the base back, bytes 0-2 only: no engage */
    REG(BANK + 23) = 0x80;                    /* off: the ROM view returns (the stub re-engages for a program) */
}

uint8_t k_shell(const char *p);
static void shell_line(const char *p);
static void banner(void);                 /* the logo: sideways window, not resident */
static void cmd_mon(const char *p);
static void cmd_bbcbasic(uint8_t prog);
static void cmd_bang(const char *p);
/* DUMP [note]: the emulator writes dumps/dump-NNN.txt with the machine state,
 * the screen, the PC history and the shell log; the note goes into the log */
#pragma code-name (push, "SWCODE0")
#pragma rodata-name (push, "SWRODATA0")
#pragma code-name (push, "SWCODE1")   /* bank 1: cold, called through sw_call() */
#pragma rodata-name (push, "SWRODATA1")
static void cmd_dump(const char *p)
{
    uint8_t n;
    if (is_cmd(&p, "ON"))  { REG(SYS + 0xF2) = 1; puts_("auto dump on: every 15 s of run time (dumps/, 100 files rotating)"); newline(); return; }
    if (is_cmd(&p, "OFF")) { REG(SYS + 0xF2) = 0; puts_("auto dump off"); newline(); return; }
    REG(SYS + 0xF1) = '#'; while (*p) REG(SYS + 0xF1) = *p++; REG(SYS + 0xF1) = '\n';
    REG(SYS + 0xF0) = 1;
    n = REG(SYS + 0xF0);
    if (n) { puts_("dump "); putdec(n); puts_(" written (dumps/dump-"); if (n < 100) k_chrout('0'); if (n < 10) k_chrout('0'); putdec(n); puts_(".txt)"); newline(); }
    else error("dump: failed");
}
#pragma code-name (pop)
#pragma rodata-name (pop)

/* CPM [command]: RunCPM reads AUTOEXEC.TXT at boot and runs its first line,
 * so a command given here is written there, CP/M is started, and the file is
 * taken away again afterwards -- otherwise it would hijack every later boot.
 * One line is all the CCP takes, but it auto-submits a .SUB when a .COM is
 * not found, so CPM GO runs GO.SUB and that may be as long as you like
 * (ending in EXIT, if you want quitting the program to leave CP/M too). */
static void cmd_cpm(const char *p)
{
    char nm[18], txt[64]; uint8_t n = 0, wrote = 0;
    /* Clear anything a previous session left behind before starting. Stop the
     * machine while CP/M is up -- or kill the emulator -- and the cleanup at
     * the bottom never runs, so the old AUTOEXEC.TXT would quietly hijack
     * every later boot. An abandoned $$$.SUB does the same, being CP/M's
     * half-finished submit. Neither is ever wanted on a fresh start. */
    strcpy(nm, "/CPM/AUTOEXEC.TXT"); fs_name(nm); fs_cmd(13);
    strcpy(txt, "/CPM/A/0/$$$.SUB"); fs_name(txt); fs_cmd(13);
    if (*p) {
        while (p[n] && n < sizeof txt - 3) { txt[n] = p[n]; n++; }
        txt[n++] = '\r'; txt[n++] = '\n'; txt[n] = 0;
        strcpy(nm, "/CPM/AUTOEXEC.TXT");
        fs_name(nm); w32(FS + 8, (uint16_t)txt); w32(FS + 12, (uint32_t)n);
        if (!fs_cmd(10)) wrote = 1; else error("cpm: cannot write /CPM/AUTOEXEC.TXT");
    }
    cmd_bbcbasic(3);
    if (wrote) { fs_name(nm); fs_cmd(13); }
}

/* ---- SWAP: run a command and give this machine back ------------------------
 * The caller's whole CPU view -- zero page, both stacks, the ROM's own
 * workspace, the program, and the RAM under the ROM and the I/O page -- is
 * physical $0000-$FFFF, so one DMA puts all of it in far memory and one puts
 * it back. The text screen goes too ($030000), or the caller would return to
 * the callee's output.
 *
 * Nothing needs saving by hand. The bank registers are already on the stack,
 * pushed by the system-call stub, and the stack is inside what is saved. The
 * C frame is balanced across the call, so the CPU's own stack pointer is the
 * same on both sides and comes back by arithmetic rather than by memory. The
 * DMA is complete before the next instruction runs, so the restore lands
 * under our feet and the code carries on out of ROM, which never moved.
 *
 * What is NOT saved is far memory: a BASIC's K4SG segments live out there and
 * are simply not touched, which is why EhBASIC survives. The callee must be a
 * plain program that does not claim far segments of its own, and must not use
 * MAP. One level deep. */
#define SWAPRAM 0x0FD00000UL
#define SWAPSCR 0x0FD10000UL
static uint8_t swapping;
/* SWAP command            run it over this program, then put this one back
 * SWAP -k command         ...and leave on the screen whatever it drew
 *
 * The screen is part of what SWAP restores, because a file manager wants its
 * display back when the editor closes.  A script wants the opposite: a
 * command it ran should look the way it would at the prompt.  -k is that. */
static void cmd_swap(const char *p)
{
    uint8_t keep = 0;
    if (p[0] == '-' && (p[1] | 0x20) == 'k' && (p[2] == ' ' || !p[2])) { p += 2; skipsp(&p); keep = 1; }
    if (!*p) { error("swap: swap command"); return; }
    if (swapping) { error("swap: no nesting"); return; }
    /* The save must fire from HERE, not from inside dma_copy: the image
     * includes the zero page, and the zero page holds cc65's stack pointer.
     * Taken inside a call it records a pointer 12 bytes lower than this
     * function's own, and the restore below then hands that pointer to
     * everything above -- every enclosing frame read 12 bytes out, which is
     * how a script that swapped came back to a cleared console and how
     * RANGER got its stray cursor (2026-09-07).  The restore is inline for
     * the same reason; the two must match. */
    w32(DMA, 0x00000000UL); w32(DMA + 4, SWAPRAM); w32(DMA + 8, 0x10000UL);
    REG(DMA + 12) = 1;
    if (!keep) dma_copy(SCREEN, SWAPSCR, 80UL * 60 * 4);
    swapping = 1;                                     /* set after the save, so the restore clears it again */
    shell_line(p);
    if (!keep) dma_copy(SWAPSCR, SCREEN, 80UL * 60 * 4);
    else { REG(TERM + 9) = cx; REG(TERM + 10) = cy; }   /* the console kept what the command drew: JIM follows the ROM again */
    /* The restore overwrites the stack, so it must not be triggered from
     * inside a call: the returning JSR would find the SAVED return address
     * under it and jump back to the save, round and round. Set the registers
     * up first, then fire with a bare store, when the only frame standing is
     * this one -- which the saved image matches byte for byte. */
    w32(DMA, SWAPRAM); w32(DMA + 4, 0x00000000UL); w32(DMA + 8, 0x10000UL);
    REG(DMA + 12) = 1;
}

/* RENAME old new / CP old new: two names, the second passed via the ADDR reg
 *
 * The device takes the HOST's semantics, which is to say it overwrites the
 * destination in silence -- the same trap that cost RANGER's first trash a
 * file (docs/BUILD-LOG.md, 2026-08-29).  Down here it is worse, because this
 * is what a person types: RENAME onto an existing name, or CP over one, and
 * the old contents are gone with nothing said.  So ask first, with STAT, and
 * refuse.  -f overwrites, spelled the way RM spells it, because a default
 * you cannot get out of is a restriction rather than a default. */
static void cmd_two(uint8_t cmdno, const char *p)
{
    char a[NAMEMAX], b[NAMEMAX];
    uint8_t force = 0;
    if (is_cmd(&p, "-f")) force = 1;
    if (!getname(&p, a) || !getname(&p, b)) { error("old new?  (-f overwrites)"); return; }
    if (!force) { fs_name(b); if (!fs_cmd(8)) { error("exists: -f overwrites"); return; } }
    fs_name(a); w32(FS + 8, (uint16_t)b);
    if (fs_cmd(cmdno)) error("failed");
}

/* XD name (HEX works too): hex + ASCII, 16 bytes a row; Esc stops it */
#pragma code-name (push, "SWCODE1")   /* bank 1: cold, called through sw_call() */
#pragma rodata-name (push, "SWRODATA1")
static void cmd_xd(const char *p)
{
    char name[NAMEMAX]; uint32_t off = 0, n; uint8_t i, buf[16];
    if (!getname(&p, name)) { error("xd: name?"); return; }
    fs_name(name);
    if (fs_cmd(1)) { error("xd: not found"); return; }
    for (;;) {
        w32(FS + 8, (uint16_t)buf); w32(FS + 12, 16);
        if (fs_cmd(3)) break;
        n = r32(FS + 12); if (!n) break;
        puthex(off >> 16); puthex16((uint16_t)off); puts_(": ");
        for (i = 0; i < 16; i++) { if (i < n) { puthex(buf[i]); k_chrout(' '); } else puts_("   "); }
        k_chrout(' ');
        for (i = 0; i < n; i++) k_chrout(buf[i] >= 0x20 && buf[i] < 0x7F ? buf[i] : '.');
        newline();
        off += n;
        if (n < 16 || k_getin() == 27) break;
    }
    fs_cmd(5);
}

/* HUSH: stop every noise the machine can be making -- flush the sequencer,
 * key off all nine OPL2 voices ($B0-$B8, bit 5) and drop their levels. */
static void cmd_hush(const char *p)
{
    uint8_t c, r; (void)p;
    REG(SYS + 0xE0) = 0x80;
    for (c = 0; c < 9; c++) {                       /* key off, then silence both operators */
        REG(FM) = (uint8_t)(0xB0 + c); REG(FM + 1) = 0;
    }
    for (r = 0x40; r <= 0x55; r++) {                /* total level: $3F is quietest */
        REG(FM) = r; REG(FM + 1) = 0x3F;
    }
    puts_("hushed"); newline();
}
#pragma code-name (pop)
#pragma rodata-name (pop)

#pragma code-name (pop)
#pragma rodata-name (pop)
static void cmd_exec(const char *p)
{
    char name[NAMEMAX]; static uint32_t len, off; uint32_t L; uint8_t i;
    if (exec_busy) { error("exec: no nesting"); return; }
    if (!getname(&p, name)) { error("exec: name?"); return; }
    fs_name(name); w32(FS + 8, EXECBUF);
    if (fs_cmd(9)) { error("exec: not found"); return; }
    len = r32(FS + 12);
    exec_busy = 1;
    for (off = 0; off < len; ) {
        L = len - off; if (L > sizeof line - 1) L = sizeof line - 1;
        dma_copy(EXECBUF + off, (uint16_t)line, L);
        for (i = 0; i < L; i++) if (line[i] == '\n' || line[i] == '\r') break;
        line[i] = 0;
        off += (uint32_t)i + 1;
        if (line[0]) shell_line(line);
    }
    exec_busy = 0;
}

/* the ARGS system call ($FF95): what followed the program name on the
 * command line. $F0/$F1 = pointer (RAM, valid until the next shell line),
 * A = length; empty when there was nothing. */
uint8_t k_args(void)
{
    uint8_t n = 0;
    const char *t = args_tail ? args_tail : (const char *)&args_none;
    P_NAME = (uint16_t)t;
    while (t[n]) n++;
    return n;
}

#pragma code-name (push, "SWCODE1")   /* bank 1: cold, called through sw_call() */
#pragma rodata-name (push, "SWRODATA1")
static void cmd_caps(const char *p)
{
    skipsp(&p);
    if (is_cmd(&p, "ON"))       capslock = 1;
    else if (is_cmd(&p, "OFF")) capslock = 0;
    else if (!*p)               capslock = !capslock;
    else { error("caps: ON, OFF, or nothing to toggle"); return; }
    puts_("caps lock "); puts_(capslock ? "on" : "off"); newline();
}
/* CLG: clear the bitmap, whoever put it there -- EhBASIC's GRAPHICS, the
 * Tube ULA's MODE, a program of your own. Layer 1 is the bitmap layer, so
 * its own registers say where the pixels are and how wide a row is; the
 * height comes from the chip's mode, doubled lines meaning half as many. */
static void cmd_clg(const char *p)
{
    uint32_t addr, len; (void)p;
    if (!(REG(VICKY + 0x20) & 1)) { error("clg: no bitmap on screen"); return; }
    addr = r32(VICKY + 0x28) & 0x0FFFFFFFUL;
    len = (uint32_t) r16(VICKY + 0x26) * ((REG(VICKY + 0) & 6) ? 240UL : 480UL);
    if (len) dma_fill(0, addr, len);
}
#pragma code-name (pop)
#pragma rodata-name (pop)


/* ---- command aliases -------------------------------------------------------
 * An alias is a name and the line it stands for. The table lives in sideways
 * bank 2, which is not in the ROM image at all: banks 1..15 are plain RAM at
 * $0FF00000, and a bank engaged in the $A000 window is writable -- only the
 * unmapped ROM view is read-only. So the machine gets 8 KB of alias space
 * without spending a byte of ROM or of BSS, and the bank doubles as the
 * scratch on which the expanded line is built.
 *
 * Records are name\0 expansion\0 and end at a zero where a name would start;
 * a redefined or removed name is tombstoned with a 1 in its first byte.
 *
 * The engine and its table live in the SAME bank (2), which is why none of
 * this needs to map anything: sw_call(2, ...) puts both in the window at once.
 * It used to be resident code that mapped the table under itself, and that
 * cost ROM1C about 1.4 KB for something only the shell ever calls.
 * Bank 2 is filled with zeroes in the ROM image, so an untouched table reads
 * as empty and needs no initialising.  The engine does its own case folding
 * and calls only resident helpers (getname, puts_, error): while a bank is
 * engaged, bank 0's commands are not in the window, and sw_call does not nest.
 * Everything it reads from the caller -- the shell line, the C stack -- is
 * resident too, and the expanded line is copied into `line' before returning. */
#pragma code-name (push, "SWCODE2")
#pragma rodata-name (push, "SWRODATA2")

/* ---- PALETTE -------------------------------------------------------------
 * VICKY has 256 entries of 24-bit RGB, one palette shared by everything: the
 * console's colours are only entries 0-15 by convention, and a program may
 * have all 256 (text32 cells already carry byte-wide fg and bg).  COLOR picks
 * indices; PALETTE says what those indices look like.
 *
 *   PALETTE                list entries 0-15
 *   PALETTE n rr gg bb     set one entry (hex, n may be 0-FF)
 *   PALETTE LOAD name      apply a .PAL from /SYSTEM/PALETTES
 *   PALETTE SAVE name      write entries 0-15 out as a .PAL
 *   PALETTE RESET          back to the VIC-II sixteen
 *
 * A .PAL is text, so it can be written with VI on the machine: lines of
 * "index rr gg bb" in hex, # to end of line is a comment, and a file only
 * changes the entries it names -- an amber terminal is two lines, not sixteen.
 *
 * This lives in bank 2 with its own table because bank 1 has 964 bytes left
 * and bank 2 has 6762; it calls only resident helpers (fs_cmd, fs_name,
 * parsehex, puts_, error), as everything in this bank must. */
#define PALBUF   0x0FE10000UL            /* a .PAL, loaded whole; clear of EXECBUF */
static const uint8_t pal_vic2[16][3] = {
    {0,0,0},{255,255,255},{136,0,0},{170,255,238},{204,68,204},{0,204,85},{0,0,170},{238,238,119},
    {221,136,85},{102,68,0},{255,119,119},{51,51,51},{119,119,119},{170,255,102},{0,136,255},{187,187,187} };

static void pal_put(uint8_t i, uint8_t r, uint8_t g, uint8_t b)
{
    REG(VICKY + 6) = i; REG(VICKY + 7) = r; REG(VICKY + 8) = g; REG(VICKY + 9) = b;
}
static uint8_t pal_get(uint8_t i, uint8_t c)     /* c: 0 R, 1 G, 2 B */
{
    REG(VICKY + 6) = i; return REG(VICKY + 7 + c);
}
/* The shell's palette, kept across a program: run_at snapshots all 256 entries
 * to far RAM before a program runs and puts them back after -- so a game's
 * colours end with the game, and a PALETTE LOAD made on purpose survives it.
 * Only entries that differ are written back. */
#define PALSNAP 0x0FDFF000UL
static void pal_snap(const char *p)
{
    uint16_t i; uint32_t a = PALSNAP; (void) p;
    for (i = 0; i < 256; i++) { REG(VICKY + 6) = (uint8_t)i; far_poke(a++, REG(VICKY + 7)); far_poke(a++, REG(VICKY + 8)); far_poke(a++, REG(VICKY + 9)); }
}
static void pal_restore(const char *p)
{
    uint16_t i; uint32_t a = PALSNAP; (void) p;
    for (i = 0; i < 256; i++, a += 3) {
        uint8_t r = far_peek(a), g = far_peek(a + 1), b = far_peek(a + 2);
        REG(VICKY + 6) = (uint8_t)i;
        if (REG(VICKY + 7) != r || REG(VICKY + 8) != g || REG(VICKY + 9) != b) pal_put((uint8_t)i, r, g, b);
    }
}
static void pal_reset16(const char *p)           /* the VIC-II sixteen: PALETTE RESET, and every reset */
{
    uint8_t i; (void) p;
    for (i = 0; i < 16; i++) pal_put(i, pal_vic2[i][0], pal_vic2[i][1], pal_vic2[i][2]);
}
static char pal_dig(uint8_t v) { v &= 15; return (char)(v < 10 ? '0' + v : 'A' + v - 10); }
static void pal_hex2(uint8_t v) { k_chrout(pal_dig((uint8_t)(v >> 4))); k_chrout(pal_dig(v)); }

static uint8_t pal_word(const char **p, const char *w)   /* case-folded word match */
{
    const char *q = *p;
    while (*w) { char c = *q; if (c >= 'a' && c <= 'z') c = (char)(c - 32);
                 if (c != *w) return 0; q++; w++; }
    if (*q && *q != ' ') return 0;
    while (*q == ' ') q++;
    *p = q; return 1;
}
/* /SYSTEM/PALETTES/NAME.PAL unless the name already has a path or a dot */
static void pal_path(const char *name, char *out)
{
    const char *q = name; uint8_t dot = 0, slash = 0, i = 0;
    while (*q) { if (*q == '.') dot = 1; if (*q == '/') slash = 1; q++; }
    if (!slash) { const char *d = "/SYSTEM/PALETTES/"; while (*d) out[i++] = *d++; }
    q = name; while (*q) out[i++] = *q++;
    if (!dot) { const char *e = ".PAL"; while (*e) out[i++] = *e++; }
    out[i] = 0;
}

static void pal_load(const char *name)
{
    char path[NAMEMAX], lb[64];
    uint32_t len, off = 0; uint8_t n = 0;
    pal_path(name, path);
    fs_name(path); w32(FS + 8, PALBUF);
    if (fs_cmd(9)) { error("palette: no such file"); return; }
    len = r32(FS + 0x0C);
    while (off < len) {
        uint8_t i = 0; const char *q; uint8_t d; uint32_t idx, r, g, b;
        while (off < len && i < 63) { char c = (char)peek(PALBUF + off++);
                                      if (c == '\n' || c == '\r') break;
                                      lb[i++] = c; }
        lb[i] = 0;
        q = lb; while (*q == ' ' || *q == 9) q++;
        if (!*q || *q == '#') continue;
        /* A palette may choose the console colours that suit it.  Without this
         * a ramp is a trap: load fifteen levels of amber and the shell's
         * default COLOR 7 6 is level seven on level six, which cannot be read
         * -- and cannot be read well enough to type the fix. */
        if (pal_word(&q, "COLOR") || pal_word(&q, "COLOUR")) {
            uint32_t f = parsehex(&q, &d); if (!d) continue;
            while (*q == ' ') q++; b = parsehex(&q, &d);
            fg = (uint8_t)f; if (d) { bg = (uint8_t)b; REG(VICKY + 1) = bg; }
            cls();
            continue;
        }
        idx = parsehex(&q, &d); if (!d) continue;
        while (*q == ' ') q++; r = parsehex(&q, &d); if (!d) continue;
        while (*q == ' ') q++; g = parsehex(&q, &d); if (!d) continue;
        while (*q == ' ') q++; b = parsehex(&q, &d); if (!d) continue;
        pal_put((uint8_t)idx, (uint8_t)r, (uint8_t)g, (uint8_t)b);
        n++;
    }
    puts_("palette: "); {
        char nb[4]; uint8_t j = 0, v = n;
        if (v >= 100) { nb[j++] = (char)('0' + v / 100); v = (uint8_t)(v % 100); }
        if (n >= 10)  { nb[j++] = (char)('0' + v / 10); }
        nb[j++] = (char)('0' + v % 10); nb[j] = 0; puts_(nb);
    }
    puts_(" entries from "); puts_(path); newline();
}

static void pal_save(const char *name)
{
    char path[NAMEMAX]; uint32_t o = 0; uint8_t i, c;
    const char *hdr = "# K4510 palette -- index rr gg bb, hex\n";
    pal_path(name, path);
    while (*hdr) far_poke(PALBUF + o++, (uint8_t)*hdr++);
    for (i = 0; i < 16; i++) {
        far_poke(PALBUF + o++, (uint8_t)pal_dig(i));
        far_poke(PALBUF + o++, ' ');
        for (c = 0; c < 3; c++) { uint8_t v = pal_get(i, c);
            far_poke(PALBUF + o++, (uint8_t)pal_dig((uint8_t)(v >> 4)));
            far_poke(PALBUF + o++, (uint8_t)pal_dig(v));
            far_poke(PALBUF + o++, ' '); }
        far_poke(PALBUF + o++, '\n');
    }
    fs_name(path); w32(FS + 8, PALBUF); w32(FS + 0x0C, o);
    if (fs_cmd(10)) { error("palette: cannot write"); return; }
    puts_("palette: written to "); puts_(path); newline();
}

static void cmd_palette(const char *p)
{
    uint8_t d; uint32_t idx, r, g, b; uint8_t i;
    while (*p == ' ') p++;
    if (!*p) {                                     /* list the console's sixteen */
        for (i = 0; i < 16; i++) {
            k_chrout(pal_dig(i)); k_chrout(' ');
            pal_hex2(pal_get(i, 0)); pal_hex2(pal_get(i, 1)); pal_hex2(pal_get(i, 2));
            k_chrout(' '); k_chrout(' ');
            if ((i & 3) == 3) newline();
        }
        return;
    }
    if (pal_word(&p, "RESET")) { pal_reset16(0); return; }
    if (pal_word(&p, "LOAD"))  { char nm[NAMEMAX]; if (!getname(&p, nm)) { error("palette: load name?"); return; } pal_load(nm); return; }
    if (pal_word(&p, "SAVE"))  { char nm[NAMEMAX]; if (!getname(&p, nm)) { error("palette: save name?"); return; } pal_save(nm); return; }
    idx = parsehex(&p, &d); if (!d) { error("palette: [n rr gg bb | LOAD f | SAVE f | RESET]"); return; }
    while (*p == ' ') p++; r = parsehex(&p, &d); if (!d) { error("palette: n rr gg bb"); return; }
    while (*p == ' ') p++; g = parsehex(&p, &d); if (!d) { error("palette: n rr gg bb"); return; }
    while (*p == ' ') p++; b = parsehex(&p, &d); if (!d) { error("palette: n rr gg bb"); return; }
    pal_put((uint8_t)idx, (uint8_t)r, (uint8_t)g, (uint8_t)b);
}
#pragma code-name (pop)
#pragma rodata-name (pop)

#pragma code-name (push, "SWCODE2")
#pragma rodata-name (push, "SWRODATA2")
#define ALIAS_BANK  2
#define ALIAS_TAB   ((char *) 0xB400u)      /* the table sits above the engine, in the same bank.
                                             * $B400 is the SW2/SW2T split in rom/k4510.cfg: the
                                             * linker refuses code past it, so this cannot be
                                             * silently overrun again. */
#define ALIAS_SCRAP ((char *) 0xBF00u)      /* the last page: where the expanded line is built */
#define ALIAS_LIMIT ((char *) 0xBEF0u)      /* the table may grow to here */
#define ALIAS_SEND  ((char *) 0xBFF0u)
/* One byte at the bottom of SW2T so the linker emits the area (zero filled)
 * and the alias table's RAM is really there.  Nothing reads it. */
#pragma rodata-name (push, "SW2TAB")
static const uint8_t alias_tab_floor = 0;
#pragma rodata-name (pop)
static uint8_t alias_depth;
static uint8_t alias_hit;                 /* BSS, so resident: alias_expand cannot return one */
static char afold(char c) { return (c >= 'a' && c <= 'z') ? (char)(c - 32) : c; }
static uint8_t asame(const char *a, const char *b)
{
    while (*a && *b) { if (afold(*a) != afold(*b)) return 0; a++; b++; }
    return !*a && !*b;
}
#define ANEXT(q) do { while (*(q)) (q)++; (q)++; } while (0)
static char *alias_end(void) { char *q = ALIAS_TAB; while (*q) { ANEXT(q); ANEXT(q); } return q; }

/* the shell has run out of other ideas: is the first word an alias? if so the
 * expanded line replaces `line' and the caller runs it again */
static void alias_expand(const char *p0)          /* sw_call takes void(*)(const char*): the answer
                                                   * comes back in alias_hit, which is resident */
{
    char want[24]; const char *args = p0; char *q, *n, *e, *w; uint8_t i;
    alias_hit = 0;
    if (!getname(&args, want)) return;
    skipsp(&args);
    for (q = ALIAS_TAB; *q; ) {
        n = q; ANEXT(q); e = q; ANEXT(q);
        if (*n != 1 && asame(n, want)) {
            w = ALIAS_SCRAP;
            while (*e && w < ALIAS_SEND) *w++ = *e++;
            if (*args) { *w++ = ' '; while (*args && w < ALIAS_SEND) *w++ = *args++; }
            *w = 0;
            for (i = 0; i < sizeof line - 1 && ALIAS_SCRAP[i]; i++) line[i] = ALIAS_SCRAP[i];
            line[i] = 0;                                  /* args came out of line: it is free to overwrite now */
            alias_hit = 1;
            return;
        }
    }
    alias_hit = 0;
}

static void alias_kill(const char *want)                  /* tombstone every definition of a name */
{
    char *q, *n;
    for (q = ALIAS_TAB; *q; ) { n = q; ANEXT(q); ANEXT(q); if (*n != 1 && asame(n, want)) *n = 1; }
}

static void cmd_alias(const char *p)
{
    char want[24]; char *q, *n, *e, *w; const char *s; uint8_t col;
    if (!getname(&p, want)) {                             /* ALIAS on its own: list them */
        uint8_t any = 0;
        for (q = ALIAS_TAB; *q; ) {
            n = q; ANEXT(q); e = q; ANEXT(q);
            if (*n == 1) continue;
            any = 1; col = cx; puts_(n); pad(col + 12); puts_(e); newline();
        }
        if (!any) puts_("no aliases"), newline();
        return;
    }
    skipsp(&p);
    alias_kill(want);                                     /* ALIAS name, with nothing after it, just removes */
    if (!*p) return;
    w = alias_end();
    if (w + strlen(want) + strlen(p) + 3 >= ALIAS_LIMIT) { error("alias: full"); return; }
    for (s = want; *s; ) *w++ = *s++;
    *w++ = 0;
    while (*p) *w++ = *p++;
    *w++ = 0; *w = 0;
}

#pragma code-name (pop)
#pragma rodata-name (pop)


#pragma code-name (push, "SWCODE0")
#pragma rodata-name (push, "SWRODATA0")
/* An unknown word with a .COM on CP/M's A: is a CP/M program: run it there.
 * A .prg has no signature to test -- its header is a load address and a run
 * address, and a .COM's first bytes are Z80 code that would read as a
 * perfectly plausible one -- so the extension is what tells them apart, and
 * that is enough. A one-shot .SUB carries the command and an EXIT after it,
 * so the round trip ends back at this prompt instead of at A0>. */
static uint8_t try_com(const char *nm, const char *args)
{
    char path[40]; uint8_t i = 0, n = 0;
    if (!(REG(SYS + 0x21) & 1)) return 0;                 /* the F7 menu decides: off by default, so a
                                                             mistyped D does not start a Z80 program */
    if (strlen(nm) > 8) return 0;                         /* CP/M names are 8.3 */
    strcpy(path, "/CPM/A/0/"); strcat(path, nm); strcat(path, ".COM");
    fs_name(path);
    if (fs_cmd(8)) return 0;                              /* no such .COM: not ours */
    while (nm[n] && i < sizeof line - 12) line[i++] = nm[n++];
    if (*args) { line[i++] = ' '; while (*args && i < sizeof line - 10) line[i++] = *args++; }
    line[i++] = '\r'; line[i++] = '\n';
    line[i++] = 'E'; line[i++] = 'X'; line[i++] = 'I'; line[i++] = 'T';
    line[i++] = '\r'; line[i++] = '\n'; line[i++] = 26;   /* ^Z: CP/M's end of file */
    strcpy(path, "/CPM/A/0/K-RUN.SUB");
    fs_name(path); w32(FS + 8, (uint16_t)line); w32(FS + 12, (uint32_t)i);
    if (fs_cmd(10)) return 0;
    strcpy(path, "K-RUN");
    cmd_cpm(path);
    strcpy(path, "/CPM/A/0/K-RUN.SUB"); fs_name(path); fs_cmd(13);
    return 1;
}
#pragma code-name (pop)
#pragma rodata-name (pop)

#pragma code-name (push, "SWCODE1")
#pragma rodata-name (push, "SWRODATA1")
/* The REXX rule's second half: HELLO WORLD with no HELLO.prg but a HELLO.RX
 * runs "RX HELLO WORLD" -- the interpreter is a program like any other.
 * Same contract as alias_expand: the line is rewritten and alias_hit says so. */
static void try_rx(const char *p0)
{
    char nm[NAMEMAX]; const char *q = p0; char *w; uint8_t i;
    if (!getname(&q, nm) || strlen(nm) > NAMEMAX - 4) return;
    strcat(nm, ".RX"); fs_name(nm);
    if (fs_cmd(8)) return;
    w = ALIAS_SCRAP; *w++ = 'R'; *w++ = 'X'; *w++ = ' ';
    while (*p0 && w < ALIAS_SEND) *w++ = *p0++;
    *w = 0;
    for (i = 0; i < sizeof line - 1 && ALIAS_SCRAP[i]; i++) line[i] = ALIAS_SCRAP[i];
    line[i] = 0; alias_hit = 1;
}
#pragma code-name (pop)
#pragma rodata-name (pop)
static void shell_line(const char *p)
{
    uint8_t d; uint32_t v; const char *p0;
    skipsp(&p);
    if (!*p || *p == '#') return;                        /* blank, or a comment: EXEC scripts want them */
    p0 = p;
    { const char *q = p; while (*q) REG(SYS + 0xF1) = *q++; REG(SYS + 0xF1) = '\n'; }   /* the shell log, for DUMP */
    if (*p == '!') { p++; skipsp(&p); cmd_bang(p); return; }   /* !ls -l  the host's shell, where there is one */
    if (is_cmd(&p, "DIR") || is_cmd(&p, "LS")) { cmd_dir(p); return; }
    if (is_cmd(&p, "CD") || is_cmd(&p, "CHDIR")) { cmd_cd(p); return; }
    if (is_cmd(&p, "MKDIR")) { sw_call(1, cmd_mkdir, p); return; }
    if (is_cmd(&p, "RM") || is_cmd(&p, "ERASE") || is_cmd(&p, "DEL")) { cmd_rm(p); return; }
    if (is_cmd(&p, "RMDIR")) { sw_call(1, cmd_rmdir, p); return; }
    if (is_cmd(&p, "LOAD"))  { cmd_load(p); return; }
    if (is_cmd(&p, "SAVE"))  { sw_call(1, cmd_save, p); return; }
    if (is_cmd(&p, "TYPE"))  { sw_call(1, cmd_type, p); return; }
    if (is_cmd(&p, "XD") || is_cmd(&p, "HEX")) { sw_call(1, cmd_xd, p); return; }
    if (is_cmd(&p, "RENAME") || is_cmd(&p, "REN") || is_cmd(&p, "MV")) { cmd_two(16, p); return; }
    if (is_cmd(&p, "CP"))    { cmd_two(17, p); return; }
    if (is_cmd(&p, "EXEC"))  { cmd_exec(p); return; }
    if (is_cmd(&p, "HUSH"))  { sw_call(1, cmd_hush, p); return; }
    if (is_cmd(&p, "RUN"))   { cmd_run(p); return; }
    if (is_cmd(&p, "FILL"))  { sw_call(1, cmd_fill, p); return; }
    if (is_cmd(&p, "COPY"))  { sw_call(1, cmd_copy, p); return; }
    if (is_cmd(&p, "INFO"))  { sw_call(1, cmd_info, p); return; }
    if (is_cmd(&p, "TIME"))  { sw_call(1, cmd_time, p); return; }
    if (is_cmd(&p, "COLOR") || is_cmd(&p, "COLOUR")) { sw_call(1, cmd_color, p); return; }
    if (is_cmd(&p, "PALETTE")) { sw_call(2, cmd_palette, p); return; }
    if (is_cmd(&p, "MODE"))  { sw_call(1, cmd_mode, p); return; }
    if (is_cmd(&p, "ECHO"))  { puts_(p); newline(); return; }
    if (is_cmd(&p, "CLS"))   { cls(); return; }
    if (is_cmd(&p, "LOGO"))  { banner(); return; }
    if (is_cmd(&p, "SWAP"))    { cmd_swap(p); return; }
    if (is_cmd(&p, "ALIAS"))   { sw_call(ALIAS_BANK, cmd_alias, p); return; }
    if (is_cmd(&p, "CLG"))   { sw_call(1, cmd_clg, p); return; }
    if (is_cmd(&p, "CAPSLOCK") || is_cmd(&p, "CAPS")) { sw_call(1, cmd_caps, p); return; }
    if (is_cmd(&p, "RESET")) { ((fn_t)(*(uint16_t *)0xFFFC))(); return; }
    if (is_cmd(&p, "HELP"))  { sw_call(1, cmd_type, "/.HELP"); return; }   /* the help text lives on disk, dot-hidden */
    if (is_cmd(&p, "DUMP"))  { sw_call(1, cmd_dump, p); return; }
    if (is_cmd(&p, "MON") || is_cmd(&p, "WOZ")) { cmd_mon(p); return; }
    if (is_cmd(&p, "BBCBASIC") || is_cmd(&p, "BBC")) { cmd_bbcbasic(1); return; }
    if (is_cmd(&p, "CPM"))   { cmd_cpm(p); return; }
    /* an unknown word: if it names a program, run it (OPLPLAY = RUN oplplay.prg) */
    { char name[NAMEMAX]; const char *q = p0;                 /* REXX-style: an unknown word is a program on disk */
      if (getname(&q, name)) {
          uint8_t st = do_load(name, USER, 0);
          if (st == 1 && !is_prg(name) && strlen(name) < NAMEMAX - 5) { strcat(name, ".prg"); st = do_load(name, USER, 0); }
          if (!st && last_run) { args_tail = q; run_at(last_run); args_tail = 0; return; }
      } }
    { char nm[NAMEMAX]; const char *q = p0;               /* a CP/M program is a real program too */
      if (getname(&q, nm) && try_com(nm, q)) return; }
    if (alias_depth < 4) sw_call(ALIAS_BANK, alias_expand, p0);   /* last of all, so an alias never shadows a real command */
    if (alias_depth < 4 && !alias_hit) sw_call(1, try_rx, p0);     /* and a script comes after a program of the same name */
    if (alias_depth < 4 && alias_hit) {
        alias_depth++; shell_line(line); alias_depth--; return;
    }
    error("? (HELP lists the commands; MON is the monitor)");
}

/* the Wozmon grammar: addr  addr.addr  addr:b b b  addrR */
#pragma code-name (push, "SWCODE0")
#pragma rodata-name (push, "SWRODATA0")
static void mon_line(const char *p)
{
    uint8_t d; uint32_t v;
    mode = 0;
    for (;;) {
        skipsp(&p);
        if (!*p) return;
        if (*p == ':') { mode = 1; p++; continue; }
        if (*p == '.') { mode = 2; p++; continue; }
        if (*p == 'R' || *p == 'r') { if (xam < 0x10000UL) run_at((uint16_t)xam); else error("run: 16-bit address"); return; }
        v = parsehex(&p, &d);
        if (!d) { error("?"); return; }
        if (mode == 1) { poke(xam++, (uint8_t)v); continue; }
        if (mode == 2) { dump(xam, v); xam = v + 1; mode = 0; continue; }
        xam = v; dump(v, v);
    }
}

/* MON: the machine monitor, Wozmon's grammar at a * prompt; X leaves (back to
 * the shell, or to BASIC when entered with @MON). Shell commands work too. */
static void cmd_mon(const char *p)
{

    if (*p) { mon_line(p); return; }                     /* MON E000.E00F : one line, no prompt */
    { uint8_t o = fg; fg = C_DIM; puts_("monitor: addr  addr.addr  addr:b b b  addrR  (28-bit hex)   X leaves"); newline(); fg = o; }
    for (;;) {
        const char *q;
        puts_("*");
        sw_call(3, readline_sw, line);   /* the shell line buffer: its previous contents were consumed above */
        q = line; skipsp(&q);
        if (!*q) continue;
        if (is_cmd(&q, "X") || is_cmd(&q, "EXIT") || is_cmd(&q, "Q")) return;
        if (ishex(*q) || *q == ':' || *q == '.') mon_line(q); else shell_line(q);
    }
}

#pragma code-name (pop)
#pragma rodata-name (pop)
/* VICKY CTRL for each MODE: halve columns (2), halve lines (4), 200-line
 * field (8), quarter columns (16).  See core/vicky.h. */
static const uint8_t ctrlmode[5] = { 0, 4, 2, 2 | 8, 2 | 8 | 16 };
#pragma code-name (push, "CODE2")
static void video_init(void)
{
    uint8_t i;
    PCOLS = vmode == 4 ? 20 : vmode >= 2 ? 40 : 80; PROWS = vmode == 0 ? 60 : vmode >= 3 ? 25 : 30;
    /* status mode: two static bands frame the console (the 80-column modes only).
     * The band heights scale with the screen: 640x240 -> 2 top + 3 bottom (25 rows);
     * 640x480 -> 4 + 6 (50 rows).  bband != 0 is the flag the rest of the ROM reads. */
    /* The bands are the F7 Terminal menu's now, and independent (Doc,
     * 2026-09-02): a top height and a bottom height, either of which may be
     * zero.  They used to be PROWS/15 and PROWS/10 -- 4+6 at 640x480 and 2+3
     * at 640x240 -- which spent a sixth of the screen on furniture holding
     * four strings, two of which were a nameplate.  The default is 1+2 in
     * both modes.  Clamped so the console always keeps BAND_MIN_ROWS: a
     * program may ask for anything, and gets the clamp rather than the ask. */
    if (bands_on()) {
        uint8_t t, b;
        if (claimed()) { t = REG(TERM + T_BANDTOP); b = REG(TERM + T_BANDBOT); }   /* the program's */
        else           { t = REG(SYS + SYS_BANDTOP); b = REG(SYS + SYS_BANDBOT); } /* the user's */
        if (t + b > PROWS - BAND_MIN_ROWS) { t = 1; b = 2; }
        OY = t; bband = b;
    } else                                                { OY = margin;    bband = 0; }
    COLS = PCOLS - (bband ? 0 : margin); ROWS = PROWS - OY - bband;
    REG(VICKY + 0) = 0;
    REG(VICKY + 1) = C_BG;
    /* The palette is deliberately NOT reloaded here.  VICKY comes up with the
     * VIC-II sixteen already in entries 0-15 (core/vicky.c: vicky_reset), byte
     * for byte the same table this used to write, so the write was doing
     * nothing at power-on -- and undoing PALETTE's work at every mode change,
     * every VIDEO call and every BBC BASIC text mode.  The palette belongs to
     * VICKY and to whoever last set it; PALETTE RESET and the reset chord are
     * the ways back.  See docs/notes/design-ideas.md. */
    /* layer 0: text32, 8x8, map SCREEN, glyphs FONT, 80 cells/row */
    w16(VICKY + 0x16, PCOLS);
    w32(VICKY + 0x1C, SCREEN);
    w32(VICKY + 0x18, FONT);
    w16(VICKY + 0x12, 0); w16(VICKY + 0x14, 0);
    REG(VICKY + 0x11) = 0;
    REG(VICKY + 0x10) = 0x01 | (3 << 1);      /* enable | text32 */
    for (i = 1; i < 4; i++) REG(VICKY + 0x10 + i * 0x10) = 0;
    REG(VICKY + 0x0E) = 0; REG(VICKY + 0x64) = 0;
    REG(VICKY + 5) = 1;                        /* IRQ on vblank */
    REG(VICKY + 0) = (uint8_t)(1 | ctrlmode[vmode]);
    /* JIM, the terminal, draws in the same window */
    REG(TERM + 5) = COLS; REG(TERM + 6) = ROWS; REG(TERM + 7) = OX; REG(TERM + 8) = OY; REG(TERM + 0x0D) = PCOLS;
    REG(TERM + 0x14) = C_FG; REG(TERM + 0x15) = C_BG;
    REG(TERM) = 27; REG(TERM) = '['; REG(TERM) = '2'; REG(TERM) = '0'; REG(TERM) = 'h';  /* LNM: \n returns the column */
    REG(TERM + 9) = cx; REG(TERM + 10) = cy;                                             /* and JIM starts where the console is */
    jim_fg = jim_bg = 0xFF;                                                              /* colours re-pushed on the next character */
    /* The bands are part of laying the screen out, so VIDEO ($FF92) draws
     * them -- which makes handing them back one step for a program: clear
     * FLAGS bit 3, call VIDEO, done.  Claimed, it draws nothing: the rows are
     * the program's and it is about to fill them itself. */
    if (bands_on() && !claimed()) draw_bands();
}

#pragma code-name (pop)
/* the VIDEO system call ($FF92): the text screen's mode and palette back, screen contents kept */
void k_video(void) { video_init(); }
/* ---- the Tube's chips ---------------------------------------------------
 * BBC BASIC's graphics VDU stream and SOUND statements travel down the
 * Tube as ESC]K4G;... / ESC]K4S;... strings; the Tube ULA (core/io.c)
 * executes them itself, on the VICKY blitter and the sound sequencer at
 * $D5E0, before they ever reach this console -- the job the BBC Micro's
 * I/O processor did for its co-processors. Only MODE (K4G;22) is passed
 * through as well, because the console must switch its own text geometry
 * under the ULA's 640x480 bitmap. */
static uint8_t bgon, oldvm, oldmg;
#pragma code-name (push, "SWCODE0")
#pragma rodata-name (push, "SWRODATA0")
static void bbg_mode22(uint8_t n)
{
    if (n == 3 || n == 6 || n == 7) {                    /* a text mode: the console mode returns */
        if (bgon) { bgon = 0; vmode = oldvm; margin = oldmg; video_init(); cls(); }
        return;
    }
    if (!bgon) { oldvm = vmode; oldmg = margin; bgon = 1; }
    vmode = 0; margin = 0; video_init(); cls();          /* 640x480, 80x60 text under the bitmap */
    REG(VICKY + 0x20) = 0x19;                            /* video_init turned the ULA's bitmap layer off; back on */
}
/* BBCBASIC / CPM: the console connected to the Tube co-processor, which
 * runs Richard Russell's BBC BASIC (console edition) or RunCPM, each with
 * its own flat 256 MB (PAGE and HIMEM are the co-processor's, far beyond
 * this CPU's 64 KB view). Bytes coming up go to JIM, the terminal, which
 * is a VT100 with ANSI colour: CP/M programs are set up for VT100 or ANSI
 * and BBC BASIC's console edition speaks it natively. Keys go through JIM
 * too, which turns the cursor and function keys into their VT sequences.
 * The only bytes read here are the OSC strings the Tube ULA forwards:
 * K4510; (a star command for the shell) and K4G;22 (MODE, so the console
 * can change its geometry under the ULA's bitmap). *QUIT / EXIT (or the
 * co-processor dying) returns here. */
static void tube_keys(void) { while (REG(TERM + 1) & 0x80) REG(TUBE + 2) = REG(TERM + 2); }
static void tube_term(void)
{
    draw_cursor(0);
    REG(TERM + 4) = 1;                                   /* JIM: modes and attributes to defaults, home */
    REG(TERM + 9) = cx; REG(TERM + 10) = cy;
    REG(TERM + 0x0E) = 1;                                /* its cursor shown */
}
static void cmd_bbcbasic(uint8_t prog)
{
    uint8_t c, esc = 0, oi = 0, ofg = fg, obg = bg;
    REG(TUBE + 3) = prog;
    { uint8_t tries = 60; while (tries-- && !(REG(TUBE) & 1)) { uint8_t f = REG(SYS + 0x0D); while (REG(SYS + 0x0D) == f) ; } }
    if (!(REG(TUBE) & 1)) { error("no Tube (desktop host only)"); return; }
    if (prog != 4) { puts_(prog == 3 ? "CP/M 2.2 on the Z80 second processor. EXIT returns to the shell."
                                     : "BBC BASIC on the Tube co-processor. *QUIT returns to the shell."); newline(); }
    tube_term();
    for (;;) {
        uint8_t st = REG(TUBE);
        if (!(st & 0x81)) break;                         /* the co-processor ended (*QUIT) and its last bytes are shown */
        if (st & 0x80) {
            c = REG(TUBE + 1);
            if (esc == 2) {                              /* an OSC string: ESC ] ... BEL (into the shell line buffer) */
                if (c == 7) {
                    esc = 0; line[oi] = 0;
                    if (oi > 6 && !memcmp(line, "K4510;", 6)) {  /* a star command, handed over */
                        cx = REG(TERM + 9); cy = REG(TERM + 10); REG(TERM + 0x0E) = 0;
                        newline(); shell_line(line + 6);
                        REG(TERM + 9) = cx; REG(TERM + 10) = cy; REG(TERM + 0x0E) = 1;
                    } else if (oi > 7 && !memcmp(line, "K4G;22,", 7)) {     /* MODE, forwarded by the ULA */
                        uint8_t m22 = 0; const char *q = line + 7;
                        while (*q >= '0' && *q <= '9') m22 = m22 * 10 + (uint8_t)(*q++ - '0');
                        bbg_mode22(m22);
                        tube_term();
                    }
                    oi = 0;
                } else if (oi < sizeof line - 1) line[oi++] = (char)c;
                continue;
            }
            if (esc == 1) {                              /* the byte after ESC: ']' opens an OSC, anything else is JIM's */
                esc = 0;
                if (c == ']') { esc = 2; oi = 0; continue; }
                REG(TERM) = 0x1B;
            } else if (c == 0x1B) { esc = 1; continue; }
            REG(TERM) = c;
            tube_keys();                                 /* JIM's answers (cursor position reports) go up */
            continue;                                    /* drain output before reading keys */
        }
        if (REG(KBDST) & 0x80) {
            uint8_t k = caps(REG(KBD));
            /* CP/M's software is from 1984 and reads the WordStar diamond --
             * ^E ^X ^S ^D -- not the VT100 sequences JIM would make of the
             * arrow keys, so in WordStar and Turbo Pascal the arrows did
             * nothing at all. Under CP/M they go down as the diamond,
             * straight past JIM. BBC BASIC wants the VT sequences and is
             * left alone. */
            if (prog == 3 && k >= 0x80 && k <= 0x83 && (REG(KBDST) & 0x40)) {   /* the arrows, not the letters that share their codes */
                static const uint8_t ws[4] = { 0x05, 0x18, 0x13, 0x04 };   /* up down left right */
                REG(TUBE + 2) = ws[k - 0x80];
            } else { REG(TERM + 3) = k; tube_keys(); }
        }
    }
    REG(TUBE + 3) = 2;                                   /* the ULA silences the sequencer and drops the bitmap */
    REG(TERM + 0x0E) = 0;
    if (bgon) { bgon = 0; vmode = oldvm; margin = oldmg; video_init(); cls(); }
    else { cx = REG(TERM + 9); cy = REG(TERM + 10); }
    fg = ofg; bg = obg;
    if (prog == 4) { if (cx) newline(); return; }        /* a host command: back to the prompt, no ceremony */
    newline(); puts_("the Tube co-processor has left."); newline();
}
/* `!cmd` / `!`: the host's shell on the Tube (program 4), in the machine's own
 * colours -- the same loop as BBC BASIC, a different child.  Only where the
 * device says the shell is fitted (K4510x); elsewhere it says so. */
static void cmd_bang(const char *p)
{
    if (!(REG(TUBE) & 4)) { error("no host shell on this machine"); return; }
    w32(TUBE + 4, (uint16_t)p);
    REG(TUBE + 8) = ROWS; REG(TUBE + 9) = COLS;           /* the console window: bands and margin already out */
    cmd_bbcbasic(4);
}

/* the SHELL system call ($FF8F): run one command line from a program (EhBASIC's @) */
#pragma code-name (pop)
#pragma rodata-name (pop)
uint8_t k_shell(const char *p) { SHELL_RC = 0; shell_line(p); if (cx) newline(); return SHELL_RC; }

/* box-drawing glyphs of the CP437 font */
#define B_H 0xC4
#define B_V 0xB3
#define B_TL 0xDA
#define B_TR 0xBF
#define B_BL 0xC0
#define B_BR 0xD9
#define B_LT 0xC3
#define B_RT 0xB4
#pragma code-name (push, "SWCODE0")
#pragma rodata-name (push, "SWRODATA0")

/* the logo: an hourglass of colour blocks, left-aligned, five rows; the
 * machine's name, speed and memory on its right. Everything else is INFO. */
/* The machine's face.  Five colour bars tapering to a point on the right --
 * the taper is a glyph in the bar's own colour rather than a block, which is
 * what makes the edge look cut rather than stepped -- and the machine's
 * description beside them.  LOGO reprints it; the shell calls it at boot.
 * Lives in the sideways window (SWCODE0), not the resident ROM. */
static void banner(void)
{
    static const uint8_t width[5]  = { 16, 12, 8, 12, 16 };   /* 4,3,2,3,4 -- Doc's proportions */
    static const uint8_t colour[5] = { 2, 8, 7, 5, 14 };      /* red, orange, yellow, green, light blue */
    uint8_t r, i, obg = bg, ofg = fg;
    cls();
    newline();
    for (r = 0; r < 5; r++) {
        k_chrout(' '); k_chrout(' ');
        bg = colour[r]; fg = colour[r];
        for (i = 0; i < width[r]; i++) k_chrout(' ');
        bg = obg;
        pad(20);
        switch (r) {
        case 0: fg = C_HI;  if (REG(SYS + 0x22)) puts_("BMC-");   /* on the card it IS the appliance; see docs/NAMING.md */
                    puts_("K4510 -- A FANTASY 8/16-bit COMPUTER"); break;
        /* No clock here: the machine has no one speed any more.  The clock in
         * force is INFO's business, and it says it in kHz. */
        case 2: fg = C_FG; puts_("CPU: 45GS10"); break;
        case 3: fg = C_FG;  puts_("RAM: 256 000 000 bytes"); break;
        case 4: fg = C_FG;  puts_("CHIPS: OPL2, VICKY, SHEILA, FRED, JIM"); break;
        }
        fg = ofg;
        newline();
    }
    /* The "clock not measured -- run SETUP" line used to live here.  It is out
     * (Doc, 2026-09-01): the boot probe that would have cleared it is itself
     * disabled (sdl/main.c: "the boot probe: kept, not run"), so the banner was
     * nagging every boot about a job the machine no longer offers to do.  SETUP
     * still exists for anyone who wants the number; it is just not advertised
     * on a machine that is running perfectly well without it.
     */
    newline();
}

#pragma code-name (pop)
#pragma rodata-name (pop)
int main(void)
{
    /* The host publishes the saved video mode in $D521 bits 5-7 (mode+1;
     * 0 = a host that does not) from power-on, so the machine boots straight
     * into it -- there is no late mode request to perform, and nothing to
     * wipe the banner with.  MODE 1 0 (640x240, the full 80x30) if nothing
     * is published. */
    margin = (uint8_t)((REG(SYS + 0x21) >> 1) & 1);   /* the host never sets bit 1 with the status bar on */
    vmode = (uint8_t)(REG(SYS + 0x21) >> 5);
    if (vmode) vmode--; else vmode = 1;
    video_init();
    sw_call(2, pal_reset16, 0);                   /* a warm RESET after a game used to keep the game's palette */
    fg = C_FG;
    banner();
    /* /STARTUP.BAT.  No grace window and no "hold a key to skip" any more:
     * F7 -> Shell -> Run STARTUP.BAT turns it off and stays off, and
     * --no-startup.bat skips one run, so a half-second wait at every power-on
     * was buying a third way in that nobody needed. */
    if (!(REG(SYS + 0x21) & 4)) {
        strcpy(line, "/STARTUP.BAT"); fs_name(line);   /* absolute: a power cycle used to come back in the old directory */   /* (the fs device reads names from RAM) */
        if (!fs_cmd(8)) cmd_exec(line);
    }
    for (;;) {
        if (mode_note) { mode_note = 0; banner(); }   /* back from an F7 mode or status-bar change */
        put_cwd(); puts_("] ");
        sw_call(3, readline_sw, line);
        shell_line(line);
    }
    return 0;
}
