/* K4510: BANNER -- clear the screen and draw the machine's banner (was LOGO until 2026-09-11; LOGO is the language now).
 *
 * The same picture the ROM shows at power-on and the BANNER command reprints,
 * but as a program you can edit: the bars, their colours and the text are the
 * four tables below.  It writes text32 cells straight into the screen rather
 * than printing, because printing cannot set a cell's background, and the
 * bars are backgrounds.
 *
 * The console's geometry comes from JIM, so this follows whatever MODE the
 * machine is in -- including the one-cell margin.
 */
#include "k4510.h"

#define TERM   0xDA00u
#define SCREEN 0x00030000UL

void __fastcall__ rom_chrout(unsigned char c);
static void rom_video(void) { ((void (*)(void))0xFF92)(); }

static uint8_t cols, rows, ox, oy, stride;

static void cell(uint8_t r, uint8_t c, uint8_t glyph, uint8_t fg, uint8_t bg)
{
    uint32_t a = SCREEN + ((uint32_t)(r + oy) * stride + c + ox) * 4;
    static uint8_t q[4];
    if (c >= cols || r >= rows) return;
    q[0] = glyph; q[1] = 0; q[2] = fg; q[3] = bg;
    dma_copy((uint32_t)(uint16_t)q, a, 4);
}
static void text(uint8_t r, uint8_t c, const char *s, uint8_t fg, uint8_t bg)
{
    while (*s) cell(r, c++, (uint8_t) *s++, fg, bg);
}

void main(void)
{
    static const uint8_t width[5]  = { 16, 12, 8, 12, 16 };   /* 4:3:2:3:4 */
    static const uint8_t colour[5] = { 2, 8, 7, 5, 14 };      /* red, orange, yellow, green, light blue */
    static const char *const say[5] = {
        "K4510 -- A FANTASY 8/16-bit COMPUTER",
        "",
        "CPU: 45GS10",
        "RAM: 256 000 000 bytes",
        "CHIPS: 1-4 reSID, VICKY, SHEILA, FRED, JIM" };
    uint8_t r, i, bg0;

    cols = REG(TERM + 5); rows = REG(TERM + 6);
    ox = REG(TERM + 7);   oy = REG(TERM + 8);
    stride = REG(TERM + 0x0D);
    if (!cols)   cols = 80;
    if (!rows)   rows = 30;
    if (!stride) stride = 80;
    bg0 = REG(TERM + 0x15);                      /* the console's background */

    rom_chrout(12);                              /* CLS, and the cursor comes home */
    for (r = 0; r < 5; r++) {
        for (i = 0; i < width[r]; i++) cell((uint8_t)(r + 1), (uint8_t)(i + 2), ' ', colour[r], colour[r]);
        text((uint8_t)(r + 1), 20, say[r], r ? 1 : 7, bg0);
    }
    for (r = 0; r < 7; r++) rom_chrout('\n');    /* leave the prompt below the picture */
    rom_video();                                 /* hand the screen back as the ROM likes it */
}
