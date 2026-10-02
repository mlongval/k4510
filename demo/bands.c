/* K4510: BANDS -- a program taking the status bands for itself.
 *
 * The bands are K/OS's furniture: a top band with the clock and a bottom one
 * with the battery, framing the console, switched on in F12 -> Terminal.
 * This is the other half: a PROGRAM asking for them, for as long as it runs,
 * and giving them back on the way out.
 *
 * Since option B (2026-10-01) the band rows are not part of the console's map:
 * they are BANDMAP's ($D0B8), and VICKY draws them from there.  cell() below
 * shows the arithmetic.
 *
 * The whole protocol is three of VICKY's registers -- she owns the layout
 * (core/vicky.h, $D0B0; since 2026-10-01, before which these were JIM's
 * $DA0F, $DA16 and FLAGS bit 3, which still answer as doors onto hers):
 *
 *   $D0B0  BANDTOP   rows you want in the top band
 *   $D0B1  BANDBOT   rows you want in the bottom band
 *   $D0B2  BANDCTL   bit 1: the bands are yours
 *
 * Write the two heights, set bit 1, and call VIDEO ($FF92).  K/OS re-lays the
 * console around what VICKY grants -- she keeps the console ten rows at least
 * -- publishes the new window to JIM ($DA05-$DA08, which is how VI and RANGER
 * know where they may draw, and which JIM will not let move over a band), and
 * then leaves the band rows completely alone: no clock, no battery, and a CLS
 * from inside your program clears the console without touching them.
 *
 * To hand them back: clear bit 1 and call VIDEO again.  That is all -- VIDEO
 * redraws K/OS's own bands when they are not claimed.
 *
 * YOU MUST HAND THEM BACK.  It is the same discipline PETSCII mode has (JIM
 * FLAGS bit 2, see PETSCII.PRG): leave the bit set when you exit and the shell
 * comes back to furniture nobody is maintaining -- a clock that has stopped.
 * Nothing enforces it but the program, which is why test/jimtest.sh checks
 * that this one does.
 *
 * The demo asks for 2 rows on top and 1 at the bottom, draws its own things in
 * them, and scrolls text through the console underneath so you can see that
 * the bands sit still while it does.
 */
#include "k4510.h"

void __fastcall__ rom_chrout(unsigned char c);
unsigned char rom_getin(void);
static void rom_video(void) { ((void (*)(void))0xFF92)(); }

#define TERM     0xDA00u
#define T_ROWS   REG(TERM + 0x06)
#define T_OY     REG(TERM + 0x08)
#define T_PCOLS  REG(TERM + 0x0D)
#define VICKY    0xD000u
#define V_BTOP   REG(VICKY + 0xB0)
#define V_BBOT   REG(VICKY + 0xB1)
#define V_BANDCTL REG(VICKY + 0xB2)
#define CLAIM    0x02
#define SCREEN   0x030000UL

#define BAND_FG  0x01          /* white on grey, as K/OS draws its own */
#define BAND_BG  0x0C
#define MINE_FG  0x01          /* the bottom band, in this program's colours */
#define MINE_BG  0x02

static uint8_t pcols;

/* The bands have memory of their own (VICKY $D0B8, BANDMAP: the top band's
 * rows, then the bottom's), and VICKY draws them from there, not from the
 * console's map -- which is why nothing a program writes to the console can
 * reach them.  So a physical row is either a band row in BANDMAP or a console
 * row in SCREEN. */
static uint32_t bandmap(void)
{
    return (uint32_t)REG(VICKY + 0xB8) | ((uint32_t)REG(VICKY + 0xB9) << 8) | ((uint32_t)REG(VICKY + 0xBA) << 16) | ((uint32_t)REG(VICKY + 0xBB) << 24);
}
static void cell(uint8_t x, uint8_t y, uint8_t ch, uint8_t f, uint8_t b)
{
    uint8_t oy = T_OY, rows = T_ROWS;
    uint32_t a = y < oy         ? bandmap() + ((uint32_t)y * pcols + x) * 4
               : y >= oy + rows ? bandmap() + ((uint32_t)(y - rows) * pcols + x) * 4
               :                  SCREEN + ((uint32_t)y * pcols + x) * 4;
    far_poke(a, ch); far_poke(a + 1, 0); far_poke(a + 2, f); far_poke(a + 3, b);
}
static void row(uint8_t y, uint8_t f, uint8_t b)
{
    uint8_t x;
    for (x = 0; x < pcols; x++) cell(x, y, ' ', f, b);
}
static void say(uint8_t x, uint8_t y, const char *s, uint8_t f, uint8_t b)
{
    while (*s) cell(x++, y, (uint8_t)*s++, f, b);
}
static void print(const char *s) { while (*s) rom_chrout((unsigned char)*s++); }
static void num(uint8_t x, uint8_t y, uint16_t v, uint8_t f, uint8_t b)
{
    cell(x, y, (uint8_t)('0' + v / 100), f, b);
    cell(x + 1, y, (uint8_t)('0' + (v / 10) % 10), f, b);
    cell(x + 2, y, (uint8_t)('0' + v % 10), f, b);
}

int main(void)
{
    uint8_t last, i, pos = 0, dir = 1;
    uint16_t n = 0;

    pcols = T_PCOLS;
    print("BANDS -- the status bands, taken by a program.\r\n\r\n");
    print("Asking for 2 rows on top and 1 at the bottom, then\r\n");
    print("drawing in them.  Press a key to hand them back.\r\n\r\n");

    /* ---- claim: two heights, one bit, one call ---- */
    V_BTOP = 2; V_BBOT = 1;
    V_BANDCTL |= CLAIM;
    rom_video();               /* K/OS re-lays the console and leaves the rows to us */

    /* The rows are ours and they still hold whatever K/OS last drew there, so
     * the first job is to clear them.  Claiming does not blank them: the ROM
     * has stopped touching those rows, which is the point, and that includes
     * not wiping them for us. */
    row(0, BAND_FG, BAND_BG);
    row(1, BAND_FG, BAND_BG);
    say(1, 0, "BANDS.PRG  --  these two rows belong to this program", BAND_FG, BAND_BG);
    say(1, 1, "K/OS is not drawing here: no clock, no battery", BAND_FG, BAND_BG);

    last = (uint8_t)(T_OY + T_ROWS);       /* the first row below the console: our bottom band */
    row(last, MINE_FG, MINE_BG);
    say(1, last, "frames", MINE_FG, MINE_BG);

    /* ---- run ---- */
    while (!rom_getin()) {
        wait_vblank();
        n++;
        num(8, last, n % 1000, MINE_FG, MINE_BG);
        /* a marker walking the bottom band, so it is obvious the row is live */
        cell((uint8_t)(20 + pos), last, ' ', MINE_FG, MINE_BG);
        pos = (uint8_t)(pos + dir);
        if (pos == 0 || pos > 30) dir = (uint8_t)-dir;
        cell((uint8_t)(20 + pos), last, '*', MINE_FG, MINE_BG);
        if ((n & 15) == 0) {                /* scroll the console: the bands must not move with it */
            print("console text scrolls between the bands ... ");
            for (i = 0; i < 3; i++) rom_chrout((unsigned char)('0' + (n >> 4) % 10));
            print("\r\n");
        }
    }

    /* ---- hand them back: one bit, one call ---- */
    V_BANDCTL &= (unsigned char)~CLAIM;
    rom_video();
    rom_chrout(12);                          /* CLS, so the console starts clean under K/OS's bands */
    print("BANDS: handed back.  The clock and the battery are K/OS's again.\r\n");
    return 0;
}
