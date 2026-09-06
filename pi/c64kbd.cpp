//
// A real C64 keyboard on the Pi's GPIO, wired the BMC64-PCB way
// (BMC64 "GPIO Config 2"; Doc's box). Rows are driven low one at a time,
// columns read with pull-ups; RESTORE is a plain switch to ground.
//
//   row r (C64 keyboard connector pins 20..13 = PA0..7, with PA0/PA7 swapped
//   on the PCB): GPIO 5, 20, 19, 16, 13, 6, 12, 26
//   col c (pins 12..5 = PB0..7, with PB3/PB7 swapped): GPIO 8, 25, 24, 18, 23, 27, 17, 22
//   RESTORE (pin 3): GPIO 4
//
// The matrix is the standard one (row = CIA port A line, col = port B line).
// Keys become the ASCII / KEY_* codes the K4510's $D100 keyboard device
// expects: shift picks the upper symbol, C= acts as Alt, CTRL as Ctrl,
// RUN/STOP is Escape, the cursor keys shift into up/left, F-keys shift
// into the even ones. Held keys repeat after 0.5 s, 25/s.
//
#include <circle/gpiopin.h>
#include <circle/timer.h>
#include <cstdint>
extern "C" {
#include "../core/io.h"
}

static const unsigned ROW_GPIO[8] = { 5, 20, 19, 16, 13, 6, 12, 26 };
static const unsigned COL_GPIO[8] = { 8, 25, 24, 18, 23, 27, 17, 22 };
static const unsigned RESTORE_GPIO = 4;

static CGPIOPin *rows[8], *cols[8], *restore_pin;
static uint8_t state[8], prev[8];           // bit c set = key (r,c) down
static int held_r = -1, held_c = -1; static unsigned held_frames;

// key codes, unshifted and shifted, per matrix position. 0 = modifier / none.
#define LSH 0xF1
#define RSH 0xF2
#define CBM 0xF3
#define CTL 0xF4
static const uint8_t keymap[8][8][2] = {
    { {KEY_BS, KEY_DEL}, {KEY_ENTER, KEY_ENTER}, {KEY_RIGHT, KEY_LEFT}, {KEY_F1+6, KEY_F1+7}, {KEY_F1, KEY_F1+1}, {KEY_F1+2, KEY_F1+3}, {KEY_F1+4, KEY_F1+5}, {KEY_DOWN, KEY_UP} },
    { {'3','#'}, {'w','W'}, {'a','A'}, {'4','$'}, {'z','Z'}, {'s','S'}, {'e','E'}, {LSH,LSH} },
    { {'5','%'}, {'r','R'}, {'d','D'}, {'6','&'}, {'c','C'}, {'f','F'}, {'t','T'}, {'x','X'} },
    { {'7','\''}, {'y','Y'}, {'g','G'}, {'8','('}, {'b','B'}, {'h','H'}, {'u','U'}, {'v','V'} },
    { {'9',')'}, {'i','I'}, {'j','J'}, {'0','0'}, {'m','M'}, {'k','K'}, {'o','O'}, {'n','N'} },
    { {'+','+'}, {'p','P'}, {'l','L'}, {'-','-'}, {'.','>'}, {':','['}, {'@','@'}, {',','<'} },
    { {'\\','|'}, {'*','*'}, {';',']'}, {KEY_HOME, KEY_END}, {RSH,RSH}, {'=','='}, {'^','~'}, {'/','?'} },
    { {'1','!'}, {'`','_'}, {CTL,CTL}, {'2','"'}, {' ',' '}, {CBM,CBM}, {'q','Q'}, {KEY_ESC, KEY_ESC} },
};

extern "C" void c64kbd_init(void)
{
    for (int i = 0; i < 8; i++) {
        rows[i] = new CGPIOPin(ROW_GPIO[i], GPIOModeInputPullUp);
        cols[i] = new CGPIOPin(COL_GPIO[i], GPIOModeInputPullUp);
    }
    restore_pin = new CGPIOPin(RESTORE_GPIO, GPIOModeInputPullUp);
}

static void emit(int r, int c, int shift, int ctrl, int cbm)
{
    uint8_t k = keymap[r][c][shift ? 1 : 0];
    if (k == 0 || k == LSH || k == RSH || k == CBM || k == CTL) return;
    if (ctrl && k >= 'a' && k <= 'z') k = (uint8_t)(k - 'a' + 1);
    else if (ctrl && k >= 'A' && k <= 'Z') k = (uint8_t)(k - 'A' + 1);
    (void)cbm;
    kbd_push(k);
}

extern "C" void c64kbd_poll(void)
{
    if (!rows[0]) return;
    for (int r = 0; r < 8; r++) {
        rows[r]->SetMode(GPIOModeOutput); rows[r]->Write(LOW);
        CTimer::SimpleusDelay(10);
        uint8_t v = 0;
        for (int c = 0; c < 8; c++) if (cols[c]->Read() == LOW) v |= (uint8_t)(1 << c);
        rows[r]->SetMode(GPIOModeInputPullUp);
        state[r] = v;
    }
    int shift = (state[1] & 0x80) || (state[6] & 0x10);
    int cbm   = (state[7] & 0x20) != 0;
    int ctrl  = (state[7] & 0x04) != 0;
    kbd_modifiers((uint8_t)shift, (uint8_t)ctrl, (uint8_t)cbm);
    {   /* $D104, the keys held: CRSR LR is row 0 col 2 (shifted = left), CRSR UD row 0 col 7
         * (shifted = up), space row 7 col 4, Z row 1 col 4, X row 2 col 7 (the keymap above) */
        uint8_t held = 0;
        if (state[0] & 0x04) held |= shift ? HELD_LEFT : HELD_RIGHT;
        if (state[0] & 0x80) held |= shift ? HELD_UP : HELD_DOWN;
        if (state[7] & 0x10) held |= HELD_FIRE;
        if (state[1] & 0x10) held |= HELD_A;
        if (state[2] & 0x80) held |= HELD_B;
        kbd_held(held);
    }
    for (int r = 0; r < 8; r++) {
        uint8_t pressed = (uint8_t)(state[r] & ~prev[r]);
        for (int c = 0; c < 8; c++) if (pressed & (1 << c)) { emit(r, c, shift, ctrl, cbm); held_r = r; held_c = c; held_frames = 0; }
        prev[r] = state[r];
    }
    if (held_r >= 0) {
        if (!(state[held_r] & (1 << held_c))) held_r = -1;
        else if (++held_frames >= 30 && ((held_frames - 30) % 2) == 0) emit(held_r, held_c, shift, ctrl, cbm);   /* 0.5 s, then 30/s */
    }
    static int restore_prev = HIGH;
    int rs = restore_pin->Read();
    if (rs == LOW && restore_prev == HIGH) kbd_push(KEY_ESC);      /* RESTORE: an escape too */
    restore_prev = rs;
}
