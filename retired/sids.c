/* K4510 demo: two SIDs. SID 0 plays the melody (pulse, voice 1) and a
 * fast arpeggio (voice 2, the classic C64 trick); SID 1 plays the bass
 * (sawtooth, voice 1) and drums (noise, voice 2). A 16-bar loop at
 * 120 bpm, sequenced once per frame. On screen: which note each voice
 * holds and a level bar per voice. Any key exits. */
#include "k4510.h"

#define SID0 0xD400u
#define SID1 0xD420u
#define TEXTMAP 0x123000UL

/* SID frequency words at a 1 MHz SID clock: f * 16777216 / 1000000, octave 3 (C3 = 130.81 Hz) */
static const uint16_t oct3[12] = { 2195, 2325, 2463, 2610, 2765, 2930, 3104, 3288, 3484, 3691, 3910, 4143 };
static const char *const names[12] = { "C ", "C#", "D ", "D#", "E ", "F ", "F#", "G ", "G#", "A ", "A#", "B " };

/* note numbers: 12 * octave + semitone (C=0); 0 = rest */
#define N(o, s) ((o) * 12 + (s))
#define C 0
#define Cs 1
#define D 2
#define Ds 3
#define E 4
#define F 5
#define Fs 6
#define G 7
#define Gs 8
#define A 9
#define As 10
#define B 11

/* melody, one note per eighth (15 frames), 16 bars x 8 = 128 entries; A minor */
static const uint8_t melody[128] = {
    N(5,A),0,N(5,C),N(5,E),N(6,A),0,N(5,G),N(5,E),   N(5,F),0,N(5,A),N(5,C),N(6,F),0,N(5,E),N(5,C),
    N(5,C),0,N(5,E),N(5,G),N(6,C),0,N(5,B),N(5,G),   N(5,G),0,N(5,B),N(5,D),N(6,G),N(6,F),N(6,E),N(6,D),
    N(6,A),0,N(6,G),0,N(6,E),0,N(6,C),0,             N(6,F),0,N(6,E),0,N(6,C),0,N(5,A),0,
    N(6,C),N(5,B),N(6,C),N(6,D),N(6,E),0,N(6,D),N(6,C), N(5,B),0,N(5,G),0,N(5,B),N(6,C),N(6,D),N(5,B),
    N(5,A),0,N(5,C),N(5,E),N(6,A),0,N(5,G),N(5,E),   N(5,F),0,N(5,A),N(5,C),N(6,F),0,N(5,E),N(5,C),
    N(5,C),0,N(5,E),N(5,G),N(6,C),0,N(5,B),N(5,G),   N(5,G),0,N(5,B),N(5,D),N(6,G),N(6,F),N(6,E),N(6,D),
    N(6,E),0,N(6,E),N(6,D),N(6,C),0,N(5,B),N(5,A),   N(5,G),0,N(5,A),N(5,B),N(6,C),0,N(6,D),N(6,E),
    N(6,A),0,0,0,N(6,G),0,N(6,E),0,                  N(6,A),0,0,0,0,0,0,0 };
/* chord roots per bar (for the arpeggio and the bass): root, third offset, fifth */
static const uint8_t bars[16] = { N(3,A), N(3,F), N(3,C), N(3,G), N(3,A), N(3,F), N(3,C), N(3,G),
                                  N(3,A), N(3,F), N(3,C), N(3,G), N(3,A), N(3,G), N(3,F), N(3,E) };
static const uint8_t minor[16] = { 1,0,0,0, 1,0,0,0, 1,0,0,0, 1,0,0,1 };   /* minor chords get a flat third */

static void sid_freq(uint16_t base, uint8_t voice, uint8_t note)
{
    uint16_t f = oct3[note % 12]; uint8_t o = note / 12;
    if (o > 3) f <<= (o - 3); else if (o < 3) f >>= (3 - o);
    REG(base + voice * 7 + 0) = (uint8_t)f; REG(base + voice * 7 + 1) = (uint8_t)(f >> 8);
}

static void show_note(uint8_t row, uint8_t note, uint8_t level)
{
    uint32_t p = TEXTMAP + (uint32_t)row * 80 + 36; uint8_t i;
    if (note) { far_poke(p, names[note % 12][0]); far_poke(p + 1, names[note % 12][1]); far_poke(p + 2, '0' + note / 12); }
    else { far_poke(p, '-'); far_poke(p + 1, '-'); far_poke(p + 2, ' '); }
    for (i = 0; i < 40; i++) far_poke(p + 6 + i, i < level ? '#' : '.');
}

void main(void)
{
    uint8_t frame = 0, step = 0, bar = 0, arp = 0, drum = 0;
    uint8_t mel_note = 0, mel_level = 0, arp_level = 0, bass_level = 0, drum_level = 0;
    REG(V_CTRL) = 0;
    pal(1, 255, 255, 255); pal(2, 255, 200, 60); pal(3, 120, 200, 255);
    REG(V_BGCOL) = 0;
    dma_fill(' ', TEXTMAP, 80 * 60);
    text8_layer(0, TEXTMAP, 80, 0);
    text8_print(TEXTMAP, 80, 1, 1,  "K4510  two SIDs (reSID 6581 x 2 of 4), 1 MHz, mixed to one channel");
    text8_print(TEXTMAP, 80, 1, 4,  "SID 0  voice 1  melody   pulse");
    text8_print(TEXTMAP, 80, 1, 6,  "SID 0  voice 2  arpeggio pulse");
    text8_print(TEXTMAP, 80, 1, 8,  "SID 1  voice 1  bass     sawtooth");
    text8_print(TEXTMAP, 80, 1, 10, "SID 1  voice 2  drums    noise");
    text8_print(TEXTMAP, 80, 1, 14, "16 bars, A minor, 120 bpm.  INFO -s in the shell shows the gates.");
    text8_print(TEXTMAP, 80, 1, 58, "any key returns to the shell");
    REG(V_CTRL) = 1;

    /* instruments */
    REG(SID0 + 0x18) = 15; REG(SID1 + 0x18) = 15;                      /* volume */
    REG(SID0 + 5) = 0x09; REG(SID0 + 6) = 0x59; REG(SID0 + 2) = 0x00; REG(SID0 + 3) = 0x08;   /* melody: A0 D9, S5 R9, pulse width 1/8 */
    REG(SID0 + 7 + 5) = 0x00; REG(SID0 + 7 + 6) = 0x38; REG(SID0 + 7 + 2) = 0x00; REG(SID0 + 7 + 3) = 0x04;   /* arp: short, thin pulse */
    REG(SID1 + 5) = 0x00; REG(SID1 + 6) = 0xA8;                        /* bass: S10 R8 */
    REG(SID1 + 7 + 5) = 0x00; REG(SID1 + 7 + 6) = 0x06;                /* drum: instant, short */
    REG(SID1 + 0x17) = 0x01; REG(SID1 + 0x16) = 0x40; REG(SID1 + 0x18) = 0x1F;   /* bass through the low-pass filter */

    while (!key_hit()) {
        /* one eighth note = 15 frames */
        if (frame == 0) {
            uint8_t n = melody[step];
            if (n) { mel_note = n; sid_freq(SID0, 0, n); REG(SID0 + 4) = 0x40; REG(SID0 + 4) = 0x41; mel_level = 40; }
            else   { REG(SID0 + 4) = 0x40; }
            if ((step & 7) == 0) {                                    /* new bar: bass root */
                bar = step >> 3;
                sid_freq(SID1, 0, bars[bar] - 12); REG(SID1 + 4) = 0x20; REG(SID1 + 4) = 0x21; bass_level = 40;
            }
            if ((step & 3) == 2) { sid_freq(SID1, 0, bars[bar] - 5); REG(SID1 + 4) = 0x21; bass_level = 30; }   /* fifth below on the off-beat */
            if ((step & 1) == 0) { drum = (step & 2) ? 1 : 2; }         /* kick on 1 and 3, snare on 2 and 4 */
        }
        if (frame == 0 && drum) {
            sid_freq(SID1, 1, drum == 2 ? N(2,C) : N(6,C)); REG(SID1 + 7 + 4) = 0x80; REG(SID1 + 7 + 4) = 0x81; drum_level = drum == 2 ? 40 : 28; drum = 0;
        }
        /* arpeggio: root, third, fifth, octave every 2 frames on SID 0 voice 2 */
        if ((frame & 1) == 0) {
            uint8_t root = bars[bar] + 12, third = root + (minor[bar] ? 3 : 4), fifth = root + 7;
            uint8_t an = (arp == 0) ? root : (arp == 1) ? third : (arp == 2) ? fifth : root + 12;
            sid_freq(SID0, 1, an); REG(SID0 + 7 + 4) = 0x40; REG(SID0 + 7 + 4) = 0x41; arp = (arp + 1) & 3; arp_level = 22 + arp * 4;
        }
        if (frame == 8) REG(SID1 + 7 + 4) = 0x80;                      /* drum gate off */
        wait_vblank();
        if (mel_level) mel_level--; if (bass_level) bass_level--; if (drum_level > 1) drum_level -= 2; else drum_level = 0;
        show_note(4, mel_note, mel_level); show_note(6, (uint8_t)(bars[bar] + 12), arp_level);
        show_note(8, (uint8_t)(bars[bar] - 12), bass_level); show_note(10, 0, drum_level);
        text8_print(TEXTMAP, 80, 1, 16, "bar"); put_num(TEXTMAP, 80, 5, 16, (uint8_t)(bar + 1)); text8_print(TEXTMAP, 80, 9, 16, "of 16");
        if (++frame == 15) { frame = 0; if (++step == 128) step = 0; }
    }
    REG(SID0 + 0x18) = 0; REG(SID1 + 0x18) = 0;
    REG(SID0 + 4) = 0; REG(SID0 + 11) = 0; REG(SID1 + 4) = 0; REG(SID1 + 11) = 0;
}
