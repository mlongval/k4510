/* K4510: JIM in PETSCII -- the other way an 8-bit machine talked to its screen.
 *
 * PETSCII is not a protocol.  It is a set of control codes and a character
 * set, so JIM implements it as a second dispatch beside the ANSI one: the
 * colours land in the same fg/bg, reverse in the same attribute, and the
 * printing goes through the same code.  One renderer, two languages.
 *
 * The mode is FLAGS bit 2 at $DA0E.  This program writes straight to JIM's
 * data register rather than through CHROUT, because the ROM console speaks
 * ANSI and folds a few bytes on the way past ($0D, $08, $0C); PETSCII wants
 * them raw.  It puts the terminal back into ANSI before it returns -- leave
 * it in PETSCII and the shell would come back to a screen it cannot drive.
 *
 * A NOTE ON GLYPHS.  The machine's font is always ASCII/CP437-ordered -- a
 * chargen is permuted on the way in -- so PETSCII codes are mapped onto that,
 * not turned into screen codes.  Letters, digits and punctuation are exact.
 * The line-drawing half is exact too, because the chargen loader lifts those
 * glyphs into their CP437 positions.  The rest of PETSCII's graphics (the
 * diagonals, quarter-blocks, card suits) have no glyph at any code in this
 * font and come out as spaces rather than as some other character that
 * happens to live there.  Giving PETSCII its full set means loading a chargen
 * a second time in screen-code order and switching to it with the mode;
 * docs/TODO.md has it.
 *
 * (Its companion, ANSIDEMO.PRG, the same screen in ANSI, went 2026-10-07.)
 */
#include "k4510.h"

void __fastcall__ rom_chrout(unsigned char c);
unsigned char rom_getin(void);

#define TERM      0xDA00u
#define T_DATA    REG(TERM + 0x00)
#define T_FLAGS   REG(TERM + 0x0E)

/* PETSCII control codes */
#define P_WHITE   0x05
#define P_RETURN  0x0D
#define P_LOWER   0x0E
#define P_RED     0x1C
#define P_GREEN   0x1E
#define P_BLUE    0x1F
#define P_HOME    0x13
#define P_RVSON   0x12
#define P_RVSOFF  0x92
#define P_CLR     0x93
#define P_DOWN    0x11
#define P_RIGHT   0x1D
#define P_UPPER   0x8E
#define P_ORANGE  0x81
#define P_BLACK   0x90
#define P_YELLOW  0x9E
#define P_CYAN    0x9F
#define P_PURPLE  0x9C

/* the sixteen, in the C64's own palette order */
static const unsigned char cols[16] = {
    P_BLACK, P_WHITE, P_RED, P_CYAN, P_PURPLE, P_GREEN, P_BLUE, P_YELLOW,
    P_ORANGE, 0x95 /*brown*/, 0x96 /*lt red*/, 0x97 /*dk grey*/,
    0x98 /*grey*/, 0x99 /*lt green*/, 0x9A /*lt blue*/, 0x9B /*lt grey*/
};

static void put(unsigned char c) { T_DATA = c; }
static void print(const char *s) { while (*s) put(*s++); }
static void nl(void) { put(P_RETURN); }

static void wait_key(void)
{
    print(P_RETURN ? "" : "");
    nl();
    put(0x9B); print("  PRESS A KEY");
    while (!rom_getin()) K_WAIT();
}

int main(void)
{
    unsigned char i;

    T_FLAGS |= 4;                      /* PETSCII on */
    put(P_CLR);

    put(P_WHITE); put(P_RVSON);
    print("  JIM IN PETSCII  ");
    put(P_RVSOFF); nl(); nl();

    put(0x9B);
    print("THE SAME TERMINAL THAT SPEAKS ANSI,"); nl();
    print("SPEAKING THE OTHER LANGUAGE."); nl(); nl();

    /* ---- the sixteen colours ---- */
    put(P_WHITE); print("THE SIXTEEN COLOUR CODES:"); nl(); nl();
    print("  ");
    for (i = 0; i < 16; i++) { put(cols[i]); put(P_RVSON); print("  "); put(P_RVSOFF); }
    nl(); nl();

    /* ---- reverse, the CBM way ---- */
    put(P_WHITE); print("REVERSE VIDEO IS A CONTROL CODE, NOT AN"); nl();
    print("ESCAPE SEQUENCE:  ");
    put(P_YELLOW); put(P_RVSON); print(" RVS ON "); put(P_RVSOFF);
    put(P_WHITE); print("  AND OFF."); nl(); nl();

    /* ---- cursor codes ---- */
    put(P_WHITE); print("CURSOR CODES MOVE WITHOUT PRINTING:"); nl(); nl();
    print("  ");
    put(P_GREEN);
    for (i = 0; i < 10; i++) { print("*"); put(P_RIGHT); }
    nl(); nl();

    /* ---- the graphics half ---- */
    put(P_WHITE); print("THE LINE-DRAWING SET, WHICH THIS FONT"); nl();
    print("REALLY HAS:"); nl(); nl();
    put(P_CYAN);
    print("  "); put(0xB0); for (i = 0; i < 18; i++) put(0xC0); put(0xAE); nl();
    print("  "); put(0xDD); print(" PETSCII IN CP437 "); put(0xDD); nl();
    print("  "); put(0xAB); for (i = 0; i < 18; i++) put(0xC0); put(0xB3); nl();
    print("  "); put(0xDD); print("THE REST ARE BLANK"); put(0xDD); nl();
    print("  "); put(0xAD); for (i = 0; i < 18; i++) put(0xC0); put(0xBD); nl(); nl();

    put(0x9B);
    put(P_LOWER); print("$0E GIVES THE LOWER-CASE SET,"); nl();
    put(P_UPPER); print("$8E THE UPPER-CASE AND GRAPHICS ONE."); nl();
    print("(BOTH LINES ARE UPPER CASE IN THE SOURCE.)"); nl();

    wait_key();

    T_FLAGS &= (unsigned char)~4;      /* ANSI again, or the shell comes back to a screen it cannot drive */
    rom_chrout(12);
    return 0;
}
