/* K4510: BANNER -- clear the screen and draw the machine's banner (was LOGO until 2026-09-11; LOGO is the language now).
 *
 * The same picture the ROM shows at power-on and the BANNER command reprints,
 * but as a program you can edit: the bars, their colours and the text are the
 * four tables below.  It writes text32 cells straight into the screen rather
 * than printing, because printing cannot set a cell's background, and the
 * bars are backgrounds.
 *
 * The console's geometry comes from JIM, so this follows whatever MODE the
 * machine is in: beside the bars where the screen is wide enough (67 columns),
 * under them and wrapped where it is not -- MODE 2 and 7 (Doc, 2026-09-14).
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

/* A line of text from column c, wrapped at spaces to w columns; the rows it took. */
static uint8_t wrap(uint8_t r, uint8_t c, uint8_t w, const char *s, uint8_t fg, uint8_t bg)
{
    uint8_t used = 0, n, brk, i;
    if (!*s) return 1;
    while (*s) {
        n = 0; brk = 0;
        while (s[n] && n < w) { if (s[n] == ' ') brk = n; n++; }
        if (s[n] && brk) n = brk;                  /* break at the last space that fits */
        for (i = 0; i < n; i++) cell((uint8_t)(r + used), (uint8_t)(c + i), (uint8_t) s[i], fg, bg);
        s += n; while (*s == ' ') s++;
        used++;
    }
    return used;
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
        "CHIPS: MELODY (OPL2), VICKY, SHEILA, FRED, JIM" };
    uint8_t r, i, bg0, bw, used, longest = 0, n;

    cols = REG(TERM + 5); rows = REG(TERM + 6);
    ox = REG(TERM + 7);   oy = REG(TERM + 8);
    stride = REG(TERM + 0x0D);
    if (!cols)   cols = 80;
    if (!rows)   rows = 30;
    if (!stride) stride = 80;
    bg0 = REG(TERM + 0x15);                      /* the console's background */
    for (r = 0; r < 5; r++) { for (n = 0; say[r][n]; n++) ; if (n > longest) longest = n; }

    rom_chrout(12);                              /* CLS, and the cursor comes home */
    for (r = 0; r < 5; r++) {                    /* the bars, narrowed to the screen when it is narrower than they are */
        bw = width[r]; if (2 + bw > cols) bw = (uint8_t)(cols > 3 ? cols - 3 : 1);
        for (i = 0; i < bw; i++) cell((uint8_t)(r + 1), (uint8_t)(i + 2), ' ', colour[r], colour[r]);
    }
    if (20 + longest < cols) {                   /* wide (MODE 0 1 5 6): the text beside the bars */
        for (r = 0; r < 5; r++) text((uint8_t)(r + 1), 20, say[r], r ? 1 : 7, bg0);
        used = 7;
    } else {                                     /* narrow (MODE 2 7, and the game modes): under them, wrapped */
        used = 7;
        for (r = 0; r < 5; r++) if (say[r][0]) used = (uint8_t)(used + wrap(used, 2, (uint8_t)(cols - 3), say[r], r ? 1 : 7, bg0));
        used++;
    }
    for (r = 0; r < used; r++) rom_chrout('\n');   /* leave the prompt below the picture */
    rom_video();                                 /* hand the screen back as the ROM likes it */
}
