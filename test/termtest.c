/* JIM, the terminal ($DA00): sequences in, cells out. */
#include <stdio.h>
#include <string.h>
#include "../core/xemu/emutools_basicdefs.h"
#include "../core/xemu/cpu65.h"
#include "../core/mem.h"
#include "../core/io.h"
#include "../core/term.h"
static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("  FAIL: " __VA_ARGS__); printf("\n"); } } while (0)
#define R(r) io_read(IO_TERM + (r))
#define W(r, v) io_write(IO_TERM + (r), (uint8_t)(v))
static void send(const char *s) { while (*s) W(0, *s++); }
static uint8_t *cell(int x, int y) { return k4510_ram + 0x030000 + ((y + 1) * 80 + x + 1) * 4; }   /* the ROM's MODE 1 1 window: origin (1,1), stride 80 */
static void row(char *out, int y) { for (int x = 0; x < 79; x++) out[x] = (char) cell(x, y)[0]; out[79] = 0; for (int x = 78; x >= 0 && out[x] == ' '; x--) out[x] = 0; }
static void drain(char *out) { int i = 0; while (R(1) & 0x80) out[i++] = (char) R(2); out[i] = 0; }
static int apalb_test(int ansi) { static const int b[8] = { 11, 10, 13, 7, 14, 4, 3, 1 }; return b[ansi]; }   /* term.c's bright set */
int main(void)
{
    char r[80], rep[32];
    mem_init(); io_reset();
    W(5, 79); W(6, 29); W(7, 1); W(8, 1); W(13, 80); W(0x14, 7); W(0x15, 6); W(4, 2);
    send("Hello\r\nworld");
    row(r, 0); CHECK(!strcmp(r, "Hello"), "line 1 '%s'", r);
    row(r, 1); CHECK(!strcmp(r, "world"), "line 2 '%s'", r);
    CHECK(R(9) == 5 && R(10) == 1, "cursor %d,%d", R(9), R(10));
    printf("1. plain text, CR LF: ok\n");
    send("\033[5;10HX\033[2A\033[3DY"); CHECK(cell(9, 4)[0] == 'X' && cell(7, 2)[0] == 'Y', "CUP/CUU/CUB");
    send("\033[31;44mR\033[0m"); CHECK(cell(8, 2)[0] == 'R' && cell(8, 2)[2] == 2 && cell(8, 2)[3] == 6, "SGR red on blue: fg %d bg %d", cell(8, 2)[2], cell(8, 2)[3]);
    send("\033[1;32mG\033[0m"); CHECK(cell(9, 2)[2] == 13, "bold green -> light green (%d)", cell(9, 2)[2]);
    send("\033[7mV\033[27m"); CHECK(cell(10, 2)[2] == 6 && cell(10, 2)[3] == 7, "reverse swaps");
    send("\033[6n"); drain(rep); CHECK(!strcmp(rep, "\033[3;12R"), "CPR '%s'", rep + 1);
    printf("2. cursor moves, SGR, CPR: ok\n");
    W(4, 2); send("\033[2;5r\033[2;1Ha\r\nb\r\nc\r\nd\r\ne\r\nf");         /* region rows 2-5: f pushes a out */
    row(r, 0); CHECK(r[0] == 0, "row 1 untouched by the region scroll ('%s')", r);
    row(r, 1); CHECK(!strcmp(r, "c"), "region top now c ('%s')", r);
    row(r, 4); CHECK(!strcmp(r, "f"), "region bottom f ('%s')", r);
    row(r, 5); CHECK(r[0] == 0, "row 6 outside the region ('%s')", r);
    send("\033[r\033[3;1H\033[2L"); row(r, 3); CHECK(r[0] == 0, "IL blanked ('%s')", r); row(r, 4); CHECK(!strcmp(r, "d"), "IL pushed d down ('%s')", r);
    send("\033[3;1H\033[2M"); row(r, 2); CHECK(!strcmp(r, "d"), "DL ('%s')", r);
    printf("3. DECSTBM, IL, DL: ok\n");
    W(4, 2); send("abcdef\033[3G\033[2@XY\033[2P");
    row(r, 0); CHECK(!strcmp(r, "abXYef"), "ICH/DCH ('%s')", r);
    send("\033[2;1H\033(0lqqk\033(B|"); CHECK(cell(0, 1)[0] == 0xDA && cell(1, 1)[0] == 0xC4 && cell(3, 1)[0] == 0xBF && cell(4, 1)[0] == '|', "DEC line drawing -> CP437");
    send("\016x\017x"); CHECK(cell(5, 1)[0] == 'x' && cell(6, 1)[0] == 'x', "SO/SI with G1 still ASCII (%02X)", cell(5, 1)[0]);
    send("\033)0\016x\017"); CHECK(cell(7, 1)[0] == 0xB3, "G1 = line drawing");
    W(4, 2); send("\033[1;79H1\033[?7l23\033[?7h4"); CHECK(cell(78, 0)[0] == '3' && cell(0, 1)[0] == '4', "DECAWM off then on");
    W(4, 2); { char big[200]; memset(big, 'z', 100); big[100] = 0; send(big); } CHECK(cell(78, 0)[0] == 'z' && cell(20, 1)[0] == 'z' && cell(21, 1)[0] == ' ', "wrap at 79");
    printf("4. ICH/DCH, line drawing, wrap: ok\n");
    W(3, KEY_UP); W(3, KEY_F1); W(3, KEY_PGDN); W(3, 'a'); W(3, KEY_DEL); drain(rep);
    CHECK(!strcmp(rep, "\033[A\033OP\033[6~a\177"), "keys");
    send("\033[?1h"); W(3, KEY_LEFT); drain(rep); CHECK(!strcmp(rep, "\033OD") && (R(0x0E) & 2), "DECCKM");
    send("\033[c"); drain(rep); CHECK(!strncmp(rep, "\033[?62", 5), "DA");
    W(9, 3); W(10, 3); CHECK(!(R(1) & 1), "dirty cleared by CX/CY"); send("q"); CHECK(R(1) & 1, "dirty set by DATA");
    W(0x0E, 1); CHECK(cell(4, 3)[1] & 0x80, "cursor drawn (reverse bit)"); W(0x0E, 0); CHECK(!(cell(4, 3)[1] & 0x80), "cursor undrawn");
    send("\033]0;title\007T\033P junk \033\\U"); CHECK(cell(4, 3)[0] == 'T' && cell(5, 3)[0] == 'U', "OSC and DCS swallowed");
    send("\xC4\xB3"); CHECK(cell(6, 3)[0] == 0xC4 && cell(7, 3)[0] == 0xB3, "CP437 bytes pass as glyphs");
    W(4, 2); send("\033[2J\033[H\033#8"); CHECK(cell(0, 0)[0] == 'E' && cell(78, 28)[0] == 'E', "DECALN");
    printf("5. keys, DA, dirty bit, cursor, OSC, CP437, DECALN: ok\n");
    /* A control character inside a CSI is executed at once and the CSI goes on
     * (The Penalty Box sends ESC [ ! BS BS BS CR mid-banner; JIM used to spin on it). */
    W(4, 2); send("abcdef\033[!\b\b\b\r");                  /* cursor 6 -> 3 -> 0, the CSI still open */
    send("mZ");                                              /* m ends it (ESC[!m: nothing), Z lands where CR left the cursor */
    row(r, 0); CHECK(!strcmp(r, "Zbcdef"), "BS and CR inside a CSI act, the CSI still ends on its final byte ('%s')", r);
    W(4, 2); send("abc\033[2\bJx");                          /* BS moves 3 -> 2, then ED 2 erases, cursor kept */
    row(r, 0); CHECK(!strcmp(r, "  x"), "a control among the parameters does not lose the sequence ('%s')", r);
    printf("6. controls inside a CSI: ok\n");

    /* 7. A cell rewritten under the drawn cursor must not poison the cursor.
     * The block cursor inverts the cell's reverse bit in place.  The ROM's
     * line editor writes cells directly when it believes the cursor is
     * hidden -- and a program's CursorOn had just shown it again -- so the
     * inversion parity went off by one and every later move left a
     * reverse-video space behind (PMANDEL on the Dell, 2026-09-11). */
    W(4, 2); W(0x0E, 1); W(9, 3); W(10, 2);                     /* clear; cursor shown; put it at (3,2) */
    CHECK(cell(3, 2)[1] & 0x80, "the shown cursor inverts the reverse bit of its cell");
    cell(3, 2)[0] = ' '; cell(3, 2)[1] = 0; cell(3, 2)[2] = 7; cell(3, 2)[3] = 6;   /* the ROM blanks that cell directly */
    send("abc\r\ndef\r\nghi\r\n"); W(9, 0); W(10, 5); send("jkl");   /* the cursor visits a dozen cells */
    W(0x0E, 0);                                                  /* hide it, so the only reverse bit left would be a stray */
    { int strays = 0; for (int y = 0; y < 8; y++) for (int x = 0; x < 40; x++) if ((cell(x, y)[1] & 0x80) && cell(x, y)[0] == ' ') strays++;
      CHECK(strays == 0, "no reverse-video space left behind after a cell was rewritten under the cursor (%d)", strays); }
    CHECK(!(cell(3, 2)[1] & 0x80), "the rewritten cell keeps what was written, not the cursor's inversion");
    W(0x0E, 1); CHECK(cell(3, 5)[1] & 0x80, "and the cursor still draws afterwards");
    printf("7. a cell rewritten under the cursor: ok\n");

    /* 8. UTF-8 (ESC % G): a Linux host's box lines and bullets as CP437, a BBS's
     * CP437 art untouched, wide and zero-width characters keeping the columns,
     * and off again by ESC % @ and by the machine's reset (Doc, 2026-09-12). */
    W(0x0E, 0); W(4, 1); W(4, 2);
    send("\033%G\xE2\x94\x80\xE2\x94\x82\xE2\x95\xAD\xE2\x97\x8F\xC3\xA9\xE2\x86\x92\xE2\x94\x81");   /* ─ │ ╭ ● é → ━ */
    CHECK(cell(0, 0)[0] == 0xC4 && cell(1, 0)[0] == 0xB3 && cell(2, 0)[0] == 0xDA && cell(3, 0)[0] == 0x07
          && cell(4, 0)[0] == 0x82 && cell(5, 0)[0] == 0x1A && cell(6, 0)[0] == 0xC4,
          "UTF-8 -> CP437: %02X %02X %02X %02X %02X %02X %02X", cell(0, 0)[0], cell(1, 0)[0], cell(2, 0)[0], cell(3, 0)[0], cell(4, 0)[0], cell(5, 0)[0], cell(6, 0)[0]);
    CHECK(R(9) == 7, "seven characters, seven cells (cursor %d)", R(9));
    send("\r\n\xDB\xDB \xB1\xC4" "A\xFF");                     /* not UTF-8: a lead before a lead, a lone B1, a lead before 'A', FF */
    CHECK(cell(0, 1)[0] == 0xDB && cell(1, 1)[0] == 0xDB && cell(2, 1)[0] == ' ' && cell(3, 1)[0] == 0xB1
          && cell(4, 1)[0] == 0xC4 && cell(5, 1)[0] == 'A' && cell(6, 1)[0] == 0xFF,
          "bytes that cannot be UTF-8 draw as CP437: %02X %02X %02X %02X %02X %02X %02X", cell(0, 1)[0], cell(1, 1)[0], cell(2, 1)[0], cell(3, 1)[0], cell(4, 1)[0], cell(5, 1)[0], cell(6, 1)[0]);
    W(9, 20); send("\xC4\xB3");                                 /* the known limit: a line + a bar IS valid UTF-8 (U+0133) -- why TELNET keeps a BBS in CP437 */
    CHECK(cell(20, 1)[0] == '?' && R(9) == 21, "C4 B3 decodes as one character, not a line and a bar (%02X, cursor %d)", cell(20, 1)[0], R(9));
    send("\r\n\xE4\xB8\xADx\xCC\x81y\xF0\x9F\x98\x80z");      /* 中 (wide) x + combining acute, y, 😀 (wide), z */
    row(r, 2); CHECK(!strcmp(r, "? xy? z"), "wide = '?' + space, combining = nothing ('%s')", r);
    W(9, 10); send("<\xEE\x82\xA0\xE2\x8F\xB5\xF3\xB0\x80\x80>");   /* U+E0A0 (a Nerd Font icon), U+23F5, U+F0000: blanks */
    CHECK(cell(10, 2)[0] == '<' && cell(11, 2)[0] == ' ' && cell(12, 2)[0] == ' ' && cell(13, 2)[0] == ' ' && cell(14, 2)[0] == '>',
          "icons draw as blanks, one cell each (%02X %02X %02X)", cell(11, 2)[0], cell(12, 2)[0], cell(13, 2)[0]);
    send("\r\n\xE2\x94\033[1mA");                               /* ESC mid-sequence: the half-read bytes spill, the ESC still acts */
    CHECK(cell(0, 3)[0] == 0xE2 && cell(1, 3)[0] == 0x94 && cell(2, 3)[0] == 'A', "ESC mid-sequence (%02X %02X %c)", cell(0, 3)[0], cell(1, 3)[0], cell(2, 3)[0]);
    send("\033[0m\033%@\r\n\xE2\x94\x80");                      /* off: three CP437 glyphs */
    CHECK(cell(0, 4)[0] == 0xE2 && cell(1, 4)[0] == 0x94 && cell(2, 4)[0] == 0x80, "ESC %% @ turns it off");
    send("\033%G"); W(4, 1); W(4, 2); send("\xE2\x94\x80");      /* CTRL 1 leaves it: the ROM resets JIM AFTER a ! session switched it on */
    CHECK(cell(0, 0)[0] == 0xC4, "CTRL 1 leaves UTF-8 as it was (%02X)", cell(0, 0)[0]);
    send("\033%@");
    term_host_session(1); W(4, 2); send("\xE2\x94\x80"); CHECK(cell(0, 0)[0] == 0xC4, "term_set_utf8 (the ! shell's switch)");
    term_host_session(0);
    printf("8. UTF-8: ok\n");

    /* 9. A host session turns LNM off and gives it back.  The ROM sets LNM
     * (LF returns the column); tmux moves down with a bare LF and expects the
     * column kept -- left on, it drew "Go ahead" at column 0 and left stray
     * characters behind (the Dell, 2026-09-12). */
    W(4, 2); send("\033[20h");                                  /* as the ROM's video_init */
    term_host_session(1);                                       /* the ROM's order for `!`: REG(TUBE+3) starts the session... */
    W(4, 1);                                                    /* ...THEN tube_term resets JIM -- which once turned UTF-8 off again */
    send("\033[4;1H\xE2\x94\x80");
    CHECK(cell(0, 3)[0] == 0xC4, "UTF-8 survives the ROM's reset after the session starts (%02X)", cell(0, 3)[0]);
    send("\033[5;3HX\nY");                                      /* X at (2,4); LF: down, the column kept */
    CHECK(cell(2, 4)[0] == 'X' && cell(3, 5)[0] == 'Y', "in a session a bare LF keeps the column (Y at col %d)", cell(3, 5)[0] == 'Y' ? 3 : -1);
    term_host_session(0);
    send("\nZ");                                                /* LNM back: LF returns the column */
    CHECK(cell(0, 6)[0] == 'Z', "after the session LNM is back (Z %s)", cell(0, 6)[0] == 'Z' ? "at col 0" : "misplaced");
    send("\xE2\x94\x80"); CHECK(cell(1, 6)[0] == 0xE2, "and UTF-8 is off again");
    printf("9. a host session: LNM off, then back: ok\n");

    /* 10. Colours from a Unix host.  Claude Code's inline code is ESC[34m, and
     * ANSI blue is the machine's blue background: blue on blue, invisible (the
     * Dell, 2026-09-12).  In a UTF-8 session it takes the bright blue; a BBS
     * (CP437) keeps its exact colours.  256-colour and truecolour map to the
     * nearest of the 16, and DA2 gets its own answer. */
    W(4, 1); W(4, 2);                                           /* bg = DEFBG 6 (blue) */
    term_host_session(1);
    send("\033[34mA\033[m");
    CHECK(cell(0, 0)[2] == 14 && cell(0, 0)[3] == 6, "blue on blue in a Unix session draws light blue (fg %d bg %d)", cell(0, 0)[2], cell(0, 0)[3]);
    send("\033[38;5;196mR\033[38;2;0;205;0mG\033[38;5;244mY\033[m");   /* 256-colour red, truecolour green, a 256 grey */
    CHECK(cell(1, 0)[2] == 10 && cell(2, 0)[2] == 5, "256-colour red -> bright red, truecolour green -> green (%d %d)", cell(1, 0)[2], cell(2, 0)[2]);
    CHECK(cell(3, 0)[2] == apalb_test(0), "a 256-colour grey -> dark grey (%d)", cell(3, 0)[2]);
    send("\033[38;2;0;0;0;1mB\033[m");                          /* truecolour black then bold: the 1 is SGR, not a colour */
    CHECK(cell(4, 0)[0] == 'B', "truecolour consumes its three values");
    term_host_session(0);
    send("\033[34mb\033[m");                                    /* CP437 (a BBS): exact, even blue on blue */
    CHECK(cell(5, 0)[2] == 6, "outside a Unix session blue on blue stays exact (%d)", cell(5, 0)[2]);
    send("\033[>c"); drain(rep); CHECK(!strcmp(rep, "\033[>1;10;0c"), "DA2 answer '%s'", rep + 1);
    send("\033[c");  drain(rep); CHECK(!strncmp(rep, "\033[?62", 5), "DA1 still answers DA1");
    printf("10. host colours, 256/truecolour, DA2: ok\n");

    /* 11. The other way: a CP437 byte the machine typed, as UTF-8 for a Unix host
     * (the ! session's accented letters reached ubuntu-s1 as raw $82, 2026-09-12). */
    { char u[4]; int n;
      n = term_cp437_utf8(0x82, u); CHECK(n == 2 && (uint8_t)u[0] == 0xC3 && (uint8_t)u[1] == 0xA9, "CP437 82 -> U+00E9 e-acute (%d bytes %02X %02X)", n, (uint8_t)u[0], (uint8_t)u[1]);
      n = term_cp437_utf8('a', u);  CHECK(n == 1 && u[0] == 'a', "ASCII passes as one byte");
      n = term_cp437_utf8(0xC4, u); CHECK(n == 3 && (uint8_t)u[0] == 0xE2 && (uint8_t)u[1] == 0x94 && (uint8_t)u[2] == 0x80, "CP437 C4 -> U+2500 (%d bytes)", n); }
    printf("11. CP437 -> UTF-8 for the host: ok\n");
    printf(fails ? "\n%d FAILED\n" : "\nALL OK\n", fails); return fails != 0;
}
