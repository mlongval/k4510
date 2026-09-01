/* K4510: the OPL2 -- nine FM voices at $D480.
 *
 * The same Pachelbel progression the SID demos play (SIDS, SID6, SID12), so
 * the two chips can be put side by side.  Since 2026-09-01 the OPL2 is what
 * the machine sounds through and the SIDs are off, so this is the one of the
 * four that plays as things stand; hearing the others means putting
 * `audio.chip = reSID` in k4510.cfg, there being no menu row for it any more.
 * The OPL2 and the SIDs are mutually exclusive on this machine, which is why
 * this is its own program rather than another voice in the orchestra.
 *
 * The chip is wired the AdLib's way -- $D480 is the address port, $D481 the
 * data port -- so the register numbers below are the ones in any OPL2 or
 * AdLib programming guide, and an instrument patch lifted from one will work
 * here unchanged.
 *
 *   1-9   toggle a voice        Q or Escape   back to the shell
 */
#include "k4510.h"
#include "far.h"

#define OPL_ADDR 0xD480u
#define OPL_DATA 0xD481u
#define OPL_ID   0xD482u
#define TEXTMAP  0x123000UL
#define NCH 9

/* Operator slot offsets: channel n's modulator, and its carrier three on. */
static const uint8_t opslot[NCH] = { 0, 1, 2, 8, 9, 10, 16, 17, 18 };

/* F-numbers for block 4, i.e. one octave: fnum = f * 2^16 / 49716. */
static const uint16_t fnum[12] = { 345, 365, 387, 410, 434, 460, 488, 517, 547, 580, 614, 651 };
static const char *const names[12] = { "C ", "C#", "D ", "D#", "E ", "F ", "F#", "G ", "G#", "A ", "A#", "B " };
#define N(o, s) ((o) * 12 + (s))
enum { C, Cs, D, Ds, E, F, Fs, G, Gs, A, As, B };

static const uint8_t roots[8] = { N(3,D), N(3,A), N(3,B), N(3,Fs), N(3,G), N(3,D), N(3,G), N(3,A) };
static const uint8_t minor[8] = { 0, 0, 1, 1, 0, 0, 0, 0 };
static const uint8_t melody[64] = {
    N(5,Fs),0,N(5,E),0, N(5,D),0,N(5,Cs),0,   N(5,B),0,N(5,A),0, N(5,B),0,N(5,Cs),0,
    N(5,D),0,N(5,Cs),0, N(5,B),0,N(5,A),0,    N(4,G),0,N(5,Fs),0, N(4,G),0,N(4,A),0,
    N(5,D),N(5,Fs),N(5,A),N(5,G), N(5,Fs),N(5,D),N(5,Fs),N(5,E),  N(5,D),N(4,B),N(5,D),N(5,A), N(4,G),N(4,B),N(4,A),N(4,G),
    N(4,Fs),N(4,D),N(4,E),N(5,Cs), N(5,D),N(5,Fs),N(5,A),N(5,A),  N(4,B),N(5,G),N(5,A),N(5,Fs), N(5,A),N(5,Cs),N(5,D),0 };

static uint8_t cur[NCH], level[NCH], muted[NCH];

static void rom_video(void) { ((void (*)(void))0xFF92)(); }   /* the ROM's VIDEO call: the text screen back as it likes it */
static void opl(uint8_t reg, uint8_t val) { REG(OPL_ADDR) = reg; REG(OPL_DATA) = val; }

/* An instrument: the two operators' multiplier, level, envelope and wave.
 * mod/car pairs, straight out of an AdLib patch table. */
static void patch(uint8_t ch, uint8_t mmul, uint8_t mlvl, uint8_t mad, uint8_t msr, uint8_t mwave,
                  uint8_t cmul, uint8_t clvl, uint8_t cad, uint8_t csr, uint8_t cwave, uint8_t conn)
{
    uint8_t m = opslot[ch], c = (uint8_t)(m + 3);
    opl((uint8_t)(0x20 + m), mmul); opl((uint8_t)(0x40 + m), mlvl);
    opl((uint8_t)(0x60 + m), mad);  opl((uint8_t)(0x80 + m), msr);  opl((uint8_t)(0xE0 + m), mwave);
    opl((uint8_t)(0x20 + c), cmul); opl((uint8_t)(0x40 + c), clvl);
    opl((uint8_t)(0x60 + c), cad);  opl((uint8_t)(0x80 + c), csr);  opl((uint8_t)(0xE0 + c), cwave);
    opl((uint8_t)(0xC0 + ch), conn);
}
static void keyoff(uint8_t ch) { opl((uint8_t)(0xB0 + ch), 0); }
static void play(uint8_t ch, uint8_t note, uint8_t lvl)
{
    uint16_t f = fnum[note % 12];
    uint8_t block = note / 12;
    if (block > 7) block = 7;
    cur[ch] = note;
    if (muted[ch]) return;
    keyoff(ch);                                   /* retrigger: the envelope has to be let go first */
    opl((uint8_t)(0xA0 + ch), (uint8_t)f);
    opl((uint8_t)(0xB0 + ch), (uint8_t)(0x20 | (block << 2) | ((f >> 8) & 3)));
    level[ch] = lvl;
}

static uint8_t keys(void)
{
    uint8_t k = key_get();
    if (!k) return 0;
    if (k == 'q' || k == 'Q' || k == 0x1B) return 1;
    if (k >= '1' && k <= '9') { uint8_t v = (uint8_t)(k - '1');
        muted[v] ^= 1; if (muted[v]) { keyoff(v); level[v] = 0; } }
    return 0;
}

static const char *const vname[NCH] = {
    "voice 1  bass        organ",
    "voice 2  chord 3rd   soft reed",
    "voice 3  chord 5th   soft reed",
    "voice 4  melody      two-operator bell",
    "voice 5  arpeggio    short pluck",
    "voice 6  pad root    slow strings",
    "voice 7  pad 3rd     slow strings",
    "voice 8  pad 5th     slow strings",
    "voice 9  percussion  fast decay" };

static void show(uint8_t v)
{
    uint32_t p = TEXTMAP + (uint32_t)(6 + v * 2) * 80 + 50; uint8_t i, n = cur[v];
    if (n) { far_poke(p, names[n % 12][0]); far_poke(p + 1, names[n % 12][1]); far_poke(p + 2, (uint8_t)('0' + n / 12)); }
    else  { far_poke(p, '-'); far_poke(p + 1, '-'); far_poke(p + 2, ' '); }
    if (muted[v]) { far_poke(p + 6, 'o'); far_poke(p + 7, 'f'); far_poke(p + 8, 'f'); for (i = 3; i < 23; i++) far_poke(p + 6 + i, ' '); }
    else for (i = 0; i < 23; i++) far_poke(p + 6 + i, i < level[v] ? 0xDB : '.');
}

void main(void)
{
    uint8_t frame = 0, step = 0, bar = 0, v, lfc, fitted;
    REG(V_CTRL) = 0;
    pal(1, 255, 255, 255); pal(2, 255, 200, 60); pal(3, 120, 200, 255);
    REG(V_BGCOL) = 0;
    dma_fill(' ', TEXTMAP, 80 * 60);
    text8_layer(0, TEXTMAP, 80, 0);
    text8_print(TEXTMAP, 80, 1, 1, "K4510  the OPL2 -- nine FM voices");
    fitted = REG(OPL_ID) == 0x02;
    if (!fitted) {
        text8_print(TEXTMAP, 80, 1, 3, "No OPL2 answers at $D480 on this machine.");
        text8_print(TEXTMAP, 80, 1, 5, "Press a key to return to the shell.");
        REG(V_CTRL) = 1;
        while (!key_get()) ;
        rom_video();
        return;
    }
    text8_print(TEXTMAP, 80, 1, 3, "Pachelbel in D, 8 bars at 120 bpm -- the same progression SIDS and SID12 play.");
    text8_print(TEXTMAP, 80, 1, 4, "This chip has the machine; the SIDs are off (audio.chip in k4510.cfg).");
    for (v = 0; v < NCH; v++) {
        far_poke(TEXTMAP + (uint32_t)(6 + v * 2) * 80 + 1, (uint8_t)('1' + v));
        text8_print(TEXTMAP, 80, 3, 6 + v * 2, vname[v]);
    }
    text8_print(TEXTMAP, 80, 1, 6 + NCH * 2 + 2, "keys 1-9 toggle a voice;  Q or Escape returns to the shell");
    REG(V_CTRL) = 1;

    for (v = 0; v < 0xF6; v++) opl(v, 0);            /* a clean chip */
    opl(0x01, 0x20);                                 /* waveform select enabled: the OPL2's four shapes */
    opl(0x08, 0x00);                                 /* no CSM, no keyboard split */
    opl(0xBD, 0x00);                                 /* melodic mode: nine voices, no rhythm section */

    /*      ch   modulator: mul lvl a/d s/r wave      carrier: mul lvl a/d s/r wave   conn */
    patch(0, 0x01, 0x1A, 0xF2, 0x53, 0x00,   0x01, 0x00, 0xF3, 0x53, 0x00, 0x0A);   /* bass */
    patch(1, 0x21, 0x2C, 0x83, 0x35, 0x01,   0x21, 0x10, 0x74, 0x35, 0x00, 0x08);   /* chord 3rd */
    patch(2, 0x21, 0x2C, 0x83, 0x35, 0x01,   0x21, 0x10, 0x74, 0x35, 0x00, 0x08);   /* chord 5th */
    patch(3, 0x31, 0x1E, 0xF6, 0x27, 0x02,   0x11, 0x00, 0xF4, 0x37, 0x00, 0x06);   /* melody */
    patch(4, 0x11, 0x28, 0xF8, 0x88, 0x00,   0x01, 0x00, 0xF8, 0x69, 0x00, 0x04);   /* arpeggio */
    patch(5, 0x61, 0x30, 0x51, 0x14, 0x01,   0x21, 0x18, 0x41, 0x24, 0x00, 0x0C);   /* pad root */
    patch(6, 0x61, 0x30, 0x51, 0x14, 0x01,   0x21, 0x18, 0x41, 0x24, 0x00, 0x0C);   /* pad 3rd */
    patch(7, 0x61, 0x30, 0x51, 0x14, 0x01,   0x21, 0x18, 0x41, 0x24, 0x00, 0x0C);   /* pad 5th */
    patch(8, 0x0E, 0x00, 0xF6, 0xF6, 0x03,   0x0E, 0x00, 0xF8, 0xF7, 0x03, 0x0E);   /* percussion */

    lfc = REG(SYS + 0x0D);
    while (!keys()) {
        uint8_t fc = REG(SYS + 0x0D), d;
        if (fc == lfc) continue;
        d = (uint8_t)(fc - lfc); lfc = fc;
        while (d--) {
            if (frame == 0) {                              /* a new eighth */
                uint8_t n = melody[step];
                if ((step & 7) == 0) {
                    uint8_t root = roots[bar = (uint8_t)(step >> 3)];
                    uint8_t third = (uint8_t)(root + (minor[bar] ? 3 : 4)), fifth = (uint8_t)(root + 7);
                    play(0, (uint8_t)(root - 12), 36);
                    play(1, third, 26); play(2, fifth, 26);
                    play(5, (uint8_t)(root + 12), 18); play(6, (uint8_t)(third + 12), 18); play(7, (uint8_t)(fifth + 12), 18);
                }
                if ((step & 3) == 2) play(0, (uint8_t)(roots[bar] - 12 + 7), 28);
                if (n) play(3, n, 36);
                play(4, (uint8_t)(roots[bar] + 12 + ((step & 3) == 1 ? 4 : (step & 3) == 3 ? 7 : 0)), 22);
                if ((step & 1) == 0) play(8, (step & 2) ? N(5,C) : N(2,C), (step & 2) ? 22 : 32);
                step = (uint8_t)((step + 1) & 63);
            }
            if (++frame == 15) frame = 0;                  /* an eighth at 120 bpm */
            for (v = 0; v < NCH; v++) if (level[v]) level[v]--;
        }
        for (v = 0; v < NCH; v++) show(v);
    }
    for (v = 0; v < NCH; v++) keyoff(v);
    for (v = 0; v < 0xF6; v++) opl(v, 0);                  /* leave the chip quiet for the next program */
    rom_video();
}
