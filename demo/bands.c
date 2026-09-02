/* K4510: BANDS -- a program taking the status bands for itself.
 *
 * The bands are K/OS's furniture: a top band with the clock and a bottom one
 * with the CPU clock, framing the console.  Their heights are the user's, set
 * in F7 -> Terminal.  This is the other half: a PROGRAM asking for them, for
 * as long as it runs, and giving them back on the way out.
 *
 * The whole protocol, and it is three registers:
 *
 *   $DA0F  BANDTOP   rows you want in the top band
 *   $DA16  BANDBOT   rows you want in the bottom band
 *   $DA0E  FLAGS     bit 3: the bands are yours
 *
 * Write the two heights, set bit 3, and call VIDEO ($FF92).  K/OS re-lays the
 * console around the heights you asked for, publishes the new window to JIM
 * ($DA05-$DA08, which is how VI and RANGER know where they may draw), and
 * then leaves the band rows completely alone: no clock, no MHz, and a CLS
 * from inside your program clears the console without touching them.
 *
 * To hand them back: clear bit 3 and call VIDEO again.  That is all -- VIDEO
 * redraws K/OS's own bands when they are not claimed.
 *
 * YOU MUST HAND THEM BACK.  It is the same discipline PETSCII mode has (FLAGS
 * bit 2, see PETSCII.PRG): leave the bit set when you exit and the shell comes
 * back to furniture nobody is maintaining -- a clock that has stopped and a
 * CPU reading that no longer follows the F7 menu.  Nothing enforces it but
 * the program, which is why test/jimtest.sh checks that this one does.
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
#define T_FLAGS  REG(TERM + 0x0E)
#define T_BTOP   REG(TERM + 0x0F)
#define T_PCOLS  REG(TERM + 0x0D)
#define T_BBOT   REG(TERM + 0x16)
#define CLAIM    0x08
#define SCREEN   0x030000UL

#define BAND_FG  0x01          /* white on grey, as K/OS draws its own */
#define BAND_BG  0x0C
#define MINE_FG  0x01          /* the bottom band, in this program's colours */
#define MINE_BG  0x02

static uint8_t pcols;

static void cell(uint8_t x, uint8_t y, uint8_t ch, uint8_t f, uint8_t b)
{
    uint32_t a = SCREEN + ((uint32_t)y * pcols + x) * 4;
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
    T_BTOP = 2; T_BBOT = 1;
    T_FLAGS |= CLAIM;
    rom_video();               /* K/OS re-lays the console and leaves the rows to us */

    /* The rows are ours and they still hold whatever K/OS last drew there, so
     * the first job is to clear them.  Claiming does not blank them: the ROM
     * has stopped touching those rows, which is the point, and that includes
     * not wiping them for us. */
    row(0, BAND_FG, BAND_BG);
    row(1, BAND_FG, BAND_BG);
    say(1, 0, "BANDS.PRG  --  these two rows belong to this program", BAND_FG, BAND_BG);
    say(1, 1, "K/OS is not drawing here: no clock, no MHz", BAND_FG, BAND_BG);

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
    T_FLAGS &= (unsigned char)~CLAIM;
    rom_video();
    rom_chrout(12);                          /* CLS, so the console starts clean under K/OS's bands */
    print("BANDS: handed back.  The clock and the MHz are K/OS's again.\r\n");
    return 0;
}
