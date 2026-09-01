/* sidorch.h -- a small SID orchestra for the K4510 demos.
 * NCHIPS (2 or 4) SIDs at $D400 + 32*n, three voices each, sequenced once
 * per frame at 120 bpm (an eighth = 15 frames). Pachelbel's progression in
 * D: D A Bm F#m G D G A, 8 bars, looped. Voices (chip.voice):
 *   0.0 bass       sawtooth, low-pass            0.1 chord 3rd  pulse
 *   0.2 chord 5th  pulse                         1.0 melody     triangle
 *   1.1 arpeggio   thin pulse, 16ths             1.2 drums      noise
 *   2.0-2.2 pad    the chord an octave up, triangle, slow attack   (NCHIPS 4)
 *   3.0 echo       the melody two eighths late, pulse
 *   3.1 bass 8va   sawtooth, off-beats           3.2 hi-hat     noise, 8ths
 * On screen: a row per voice with the note it holds and a level bar; the
 * key in front of a row toggles that voice. Q or Escape returns to the shell. */
#include "k4510.h"
#include "far.h"

#define SIDB(n) (0xD400u + 32 * (n))
#define TEXTMAP 0x123000UL
#define NV (NCHIPS * 3)

static const uint16_t oct3[12] = { 2195, 2325, 2463, 2610, 2765, 2930, 3104, 3288, 3484, 3691, 3910, 4143 };
static const char *const names[12] = { "C ", "C#", "D ", "D#", "E ", "F ", "F#", "G ", "G#", "A ", "A#", "B " };
#define N(o, s) ((o) * 12 + (s))
enum { C, Cs, D, Ds, E, F, Fs, G, Gs, A, As, B };

/* chords: root (octave 3) and minor flag */
static const uint8_t roots[8] = { N(3,D), N(3,A), N(3,B), N(3,Fs), N(3,G), N(3,D), N(3,G), N(3,A) };
static const uint8_t minor[8] = { 0, 0, 1, 1, 0, 0, 0, 0 };
/* the canon's line, one note per eighth, 8 bars; 0 = hold */
static const uint8_t melody[64] = {
    N(5,Fs),0,N(5,E),0, N(5,D),0,N(5,Cs),0,   N(5,B),0,N(5,A),0, N(5,B),0,N(5,Cs),0,
    N(5,D),0,N(5,Cs),0, N(5,B),0,N(5,A),0,    N(4,G),0,N(5,Fs),0, N(4,G),0,N(4,A),0,
    N(5,D),N(5,Fs),N(5,A),N(5,G), N(5,Fs),N(5,D),N(5,Fs),N(5,E),  N(5,D),N(4,B),N(5,D),N(5,A), N(4,G),N(4,B),N(4,A),N(4,G),
    N(4,Fs),N(4,D),N(4,E),N(5,Cs), N(5,D),N(5,Fs),N(5,A),N(5,A),  N(4,B),N(5,G),N(5,A),N(5,Fs), N(5,A),N(5,Cs),N(5,D),0 };

static uint8_t cur[NV], level[NV], muted[NV];
static const uint8_t wave_of[12] = { 0x20, 0x40, 0x40, 0x10, 0x40, 0x80, 0x10, 0x10, 0x10, 0x40, 0x20, 0x80 };

static void sid_freq(uint8_t chip, uint8_t voice, uint8_t note)
{
    uint16_t f = oct3[note % 12], base = SIDB(chip) + voice * 7; uint8_t o = note / 12;
    if (o > 3) f <<= (o - 3); else if (o < 3) f >>= (3 - o);
    REG(base) = (uint8_t)f; REG(base + 1) = (uint8_t)(f >> 8);
}
/* play: gate off then on, with the waveform bits; remember it for the display */
static void play(uint8_t chip, uint8_t voice, uint8_t note, uint8_t wave, uint8_t lvl)
{
    uint16_t ctl = SIDB(chip) + voice * 7 + 4;
    if (muted[chip * 3 + voice]) { cur[chip * 3 + voice] = note; return; }     /* a muted voice still shows its note */
    sid_freq(chip, voice, note); REG(ctl) = wave; REG(ctl) = wave | 1;
    cur[chip * 3 + voice] = note; level[chip * 3 + voice] = lvl;
}
/* keys 1-9, 0, A, B toggle voices 1-12; returns 1 on Q or Escape */
static uint8_t keys(void)
{
    uint8_t k = key_get(), v = 255;
    if (!k) return 0;
    if (k == 'q' || k == 'Q' || k == 0x1B) return 1;
    if (k >= '1' && k <= '9') v = k - '1'; else if (k == '0') v = 9; else if ((k | 0x20) == 'a') v = 10; else if ((k | 0x20) == 'b') v = 11;
    if (v < NV) {
        muted[v] ^= 1;
        if (muted[v]) { REG(SIDB(v / 3) + (v % 3) * 7 + 4) = wave_of[v]; level[v] = 0; }   /* gate off now */
    }
    return 0;
}
static void release(uint8_t chip, uint8_t voice, uint8_t wave) { REG(SIDB(chip) + voice * 7 + 4) = wave; }
static void adsr(uint8_t chip, uint8_t voice, uint8_t ad, uint8_t sr, uint16_t pw)
{
    uint16_t b = SIDB(chip) + voice * 7;
    REG(b + 5) = ad; REG(b + 6) = sr; REG(b + 2) = (uint8_t)pw; REG(b + 3) = (uint8_t)(pw >> 8);
}

static void show(uint8_t v)
{
    uint32_t p = TEXTMAP + (uint32_t)(6 + v * 2) * 80 + 32; uint8_t i, n = cur[v];
    if (n) { far_poke(p, names[n % 12][0]); far_poke(p + 1, names[n % 12][1]); far_poke(p + 2, '0' + n / 12); }
    else { far_poke(p, '-'); far_poke(p + 1, '-'); far_poke(p + 2, ' '); }
    if (muted[v]) { far_poke(p + 6, 'o'); far_poke(p + 7, 'f'); far_poke(p + 8, 'f'); for (i = 3; i < 40; i++) far_poke(p + 6 + i, ' '); }
    else for (i = 0; i < 40; i++) far_poke(p + 6 + i, i < level[v] ? 0xDB : '.');
}

static void oclk(uint8_t y, uint16_t sec)
{
    uint32_t p = TEXTMAP + (uint32_t)y * 80 + 66; uint8_t m = (uint8_t)(sec / 60), c = (uint8_t)(sec % 60);
    far_poke(p, '0' + m / 10); far_poke(p + 1, '0' + m % 10); far_poke(p + 2, ':');
    far_poke(p + 3, '0' + c / 10); far_poke(p + 4, '0' + c % 10);
}
static const char *const vname[12] = {
    "SID 0.1  bass      sawtooth", "SID 0.2  chord 3rd pulse", "SID 0.3  chord 5th pulse",
    "SID 1.1  melody    triangle", "SID 1.2  arpeggio  pulse", "SID 1.3  drums     noise",
    "SID 2.1  pad root  triangle", "SID 2.2  pad 3rd   triangle", "SID 2.3  pad 5th   triangle",
    "SID 3.1  echo      pulse", "SID 3.2  bass 8va  sawtooth", "SID 3.3  hi-hat    noise" };

static void orchestra(const char *title)
{
    uint8_t frame = 0, step = 0, bar = 0, arp = 0, c, v, rls, lfc;
    uint16_t tframes = 0, rsecs = 0;   /* tune = frames played / 60; real = host wall seconds */
    REG(V_CTRL) = 0;
    pal(1, 255, 255, 255); pal(2, 255, 200, 60); pal(3, 120, 200, 255);
    REG(V_BGCOL) = 0;
    dma_fill(' ', TEXTMAP, 80 * 60);
    text8_layer(0, TEXTMAP, 80, 0);
    text8_print(TEXTMAP, 80, 1, 1, title);
    text8_print(TEXTMAP, 80, 1, 3, "Pachelbel in D, 8 bars at 120 bpm.  reSID 6581 x NCHIPS at 1 MHz, one channel.");
    for (v = 0; v < NV; v++) { far_poke(TEXTMAP + (uint32_t)(6 + v * 2) * 80 + 1, "1234567890AB"[v]); text8_print(TEXTMAP, 80, 3, 6 + v * 2, vname[v]); }
    text8_print(TEXTMAP, 80, 1, 6 + NV * 2 + 2, "keys 1-9, 0, A, B toggle a voice;  Q or Escape returns to the shell");
    text8_print(TEXTMAP, 80, 61, 1, "tune"); text8_print(TEXTMAP, 80, 61, 2, "real");
    REG(V_CTRL) = 1;

    for (c = 0; c < NCHIPS; c++) { uint8_t r; for (r = 0; r < 25; r++) REG(SIDB(c) + r) = 0; REG(SIDB(c) + 0x18) = 15; }
    adsr(0, 0, 0x00, 0xA8, 0);       REG(SIDB(0) + 0x17) = 0x01; REG(SIDB(0) + 0x16) = 0x50; REG(SIDB(0) + 0x18) = 0x1F;   /* bass: low-pass */
    adsr(0, 1, 0x19, 0x78, 0x0600);  adsr(0, 2, 0x19, 0x78, 0x0A00);                                                     /* chord: soft pulses */
    adsr(1, 0, 0x09, 0x69, 0);       adsr(1, 1, 0x00, 0x28, 0x0300);  adsr(1, 2, 0x00, 0x06, 0);                         /* melody, arp, drums */
#if NCHIPS == 4
    adsr(2, 0, 0x8A, 0x8A, 0); adsr(2, 1, 0x8A, 0x8A, 0); adsr(2, 2, 0x8A, 0x8A, 0); REG(SIDB(2) + 0x18) = 9;             /* pad: slow, quieter */
    adsr(3, 0, 0x09, 0x37, 0x0800);  adsr(3, 1, 0x00, 0x47, 0);      adsr(3, 2, 0x00, 0x03, 0); REG(SIDB(3) + 0x18) = 10;/* echo, bass 8va, hat */
#endif

    { volatile uint8_t xx = REG(SYS + 4); (void)xx; rls = REG(SYS + 5); }   /* real clock start */
    lfc = REG(SYS + 0x0D);
    while (!keys()) {
        uint8_t fc, d;
        do { fc = REG(SYS + 0x0D); } while (fc == lfc);            /* wait for >= one frame */
        d = (uint8_t)(fc - lfc); lfc = fc;                         /* frames elapsed, not one per pass */
        while (d--) {
        if (frame == 0) {                                          /* a new eighth */
            uint8_t n = melody[step];
            if ((step & 7) == 0) {                                 /* a new bar: bass and chord */
                uint8_t root = roots[bar = step >> 3], third = root + (minor[bar] ? 3 : 4), fifth = root + 7;
                play(0, 0, root - 12, 0x20, 40);
                play(0, 1, third, 0x40, 30); play(0, 2, fifth, 0x40, 30);
#if NCHIPS == 4
                play(2, 0, root + 12, 0x10, 20); play(2, 1, third + 12, 0x10, 20); play(2, 2, fifth + 12, 0x10, 20);
#endif
            }
            if ((step & 3) == 2) { play(0, 0, roots[bar] - 12 + 7, 0x20, 30); }      /* bass: the fifth on beats 2 and 4 */
            if (n) play(1, 0, n, 0x10, 40); else if (level[3]) level[3] = 20;
            if ((step & 1) == 0) play(1, 2, (step & 2) ? N(6,C) : N(2,C), 0x80, (step & 2) ? 28 : 40);   /* kick 1,3  snare 2,4 */
#if NCHIPS == 4
            { uint8_t e = melody[(step + 62) & 63]; if (e) play(3, 0, e, 0x40, 24); }                   /* the echo: two eighths late */
            if (step & 1) play(3, 1, roots[bar], 0x20, 24);                                            /* bass an octave up, off-beats */
            play(3, 2, N(7,C), 0x80, (step & 1) ? 14 : 20);                                            /* hi-hat every eighth */
#endif
        }
        if ((frame & 3) == 0) {                                    /* arpeggio: a 16th every ~4 frames */
            uint8_t root = roots[bar] + 12, third = root + (minor[bar] ? 3 : 4);
            uint8_t an = (arp == 0) ? root : (arp == 1) ? third : (arp == 2) ? root + 7 : root + 12;
            play(1, 1, an, 0x40, 18 + arp * 4); arp = (arp + 1) & 3;
        }
        if (frame == 6) { release(1, 2, 0x80); }
#if NCHIPS == 4
        if (frame == 3) release(3, 2, 0x80);
        if (frame == 10) release(3, 0, 0x40);
#endif
        for (v = 0; v < NV; v++) if (level[v]) level[v]--;         /* decay, per music frame */
        tframes++;
        if (++frame == 15) { frame = 0; if (++step == 64) step = 0; }
        }                                                          /* end per-elapsed-frame */
        for (v = 0; v < NV; v++) show(v);                          /* draw once per pass */
        text8_print(TEXTMAP, 80, 1, 6 + NV * 2, "bar"); put_num(TEXTMAP, 80, 5, 6 + NV * 2, (uint8_t)(bar + 1)); text8_print(TEXTMAP, 80, 9, 6 + NV * 2, "of 8");
        { volatile uint8_t xx = REG(SYS + 4); uint8_t sc = REG(SYS + 5); (void)xx; if (sc != rls) { rls = sc; rsecs++; } }
        oclk(1, tframes / 60); oclk(2, rsecs);
    }
    for (c = 0; c < NCHIPS; c++) { uint8_t r; for (r = 0; r < 25; r++) REG(SIDB(c) + r) = 0; }
}
