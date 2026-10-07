#include <stdio.h>
#include <string.h>
#include "io_int.h"
#include "opl2.h"
#include "state.h"
/* ---- the sound sequencer ($D5E0-$D5E3) ---------------------------------
 * The BBC Micro's four queued sound channels, in K4510 silicon. Write CH
 * ($D5E0: low nibble = channel, bit 4 = flush that channel's queue first,
 * bit 7 = silence everything now), AMP (signed, 0 to -15; an envelope
 * number > 0 plays at a fixed loudness), PITCH (quarter semitones, 53 =
 * middle C) and DUR ($D5E3: 20ths of a second, 255 = hold forever); the
 * DUR write queues the note. The channels are OPL2 voices 0-3: channel 0
 * is the Beeb's noise channel, a heavily fed-back FM patch here (the OPL2
 * has no noise generator outside rhythm mode); 1-3 are a plain two-operator
 * tone.  Each channel holds a playing note plus 63 queued ones -- much
 * deeper than the Beeb's four, because the Beeb blocked BASIC when the
 * queue filled and a one-way Tube cannot; a whole tune fits instead.  A
 * note arriving on a full queue is dropped.
 *
 * Until 2026-09-05 the channels were SID voices; the SIDs are gone.  The
 * patch is written on every note because a program is free to zero the
 * chip (OPL2.PRG did, on its way out) and the sequencer must still sound
 * after it. */
#define SEQ_DEPTH 64
typedef struct { uint16_t freq; uint8_t amp, dur; } seq_note;   /* freq: OPL2 F-number | block << 10 */
static seq_note seq_q[4][SEQ_DEPTH];
static uint8_t  seq_head[4], seq_len[4], seq_reg[3];
static int      seq_left[4];             /* frames left of the playing note; 0 idle, -1 forever */

static const uint8_t seq_slot[4] = { 0, 1, 2, 8 };   /* the modulator slot of OPL2 voices 0-3; the carrier is 3 on */
static void seq_off(int ch)
{
    opl2_write_reg((uint8_t)(0xB0 + ch), 0);                      /* key off */
    seq_left[ch] = 0;
}
static void seq_start(int ch, const seq_note *n)
{
    uint8_t m = seq_slot[ch], c = (uint8_t)(m + 3), noise = ch == 0;
    uint8_t tl = (uint8_t)((15 - (n->amp > 15 ? 15 : n->amp)) * 3);   /* carrier level: 0 loudest, 63 silent */
    opl2_write_reg(0x01, 0x20);                                   /* waveform select on */
    opl2_write_reg((uint8_t)(0x20 + m), noise ? 0x0F : 0x21);     /* modulator: mult 15 for noise, else 1 + sustain */
    opl2_write_reg((uint8_t)(0x40 + m), noise ? 0x00 : 0x18);
    opl2_write_reg((uint8_t)(0x60 + m), 0xF0);                    /* attack instant, no decay */
    opl2_write_reg((uint8_t)(0x80 + m), 0x0F);                    /* sustain full, release fast */
    opl2_write_reg((uint8_t)(0xE0 + m), noise ? 0x00 : 0x01);
    opl2_write_reg((uint8_t)(0x20 + c), 0x21);                    /* carrier: mult 1, sustain */
    opl2_write_reg((uint8_t)(0x40 + c), tl);
    opl2_write_reg((uint8_t)(0x60 + c), 0xF0);
    opl2_write_reg((uint8_t)(0x80 + c), 0x0F);
    opl2_write_reg((uint8_t)(0xE0 + c), 0x00);
    opl2_write_reg((uint8_t)(0xC0 + ch), noise ? 0x0E : 0x00);   /* noise: feedback 7, FM */
    opl2_write_reg((uint8_t)(0xB0 + ch), 0);                      /* key off first: a retrigger needs the envelope let go */
    if (n->amp) {
        opl2_write_reg((uint8_t)(0xA0 + ch), (uint8_t)(n->freq & 0xFF));
        opl2_write_reg((uint8_t)(0xB0 + ch), (uint8_t)(0x20 | ((n->freq >> 8) & 0x1F)));
    }
    seq_left[ch] = (n->dur == 255) ? -1 : (n->dur ? n->dur * 3 : 1);   /* 20ths at 60 fps */
}
static void seq_next(int ch)
{
    if (seq_len[ch]) { seq_start(ch, &seq_q[ch][seq_head[ch]]); seq_head[ch] = (seq_head[ch] + 1) % SEQ_DEPTH; seq_len[ch]--; }
    else seq_off(ch);
}
void seq_tick(void)
{
    for (int ch = 0; ch < 4; ch++)
        if (seq_left[ch] > 0 && --seq_left[ch] == 0) seq_next(ch);
}
void seq_write(uint8_t r, uint8_t v)
{
    if (r < 3) {
        if (r == 0 && (v & 0x80)) for (int c = 0; c < 4; c++) { seq_len[c] = 0; seq_off(c); }
        seq_reg[r] = v;
        return;
    }
    {
        static const double semiq[48] = {    /* 2^(i/48): a quarter-semitone ladder */
            1.000000000, 1.014545335, 1.029302237, 1.044273782, 1.059463094, 1.074873340,
            1.090507733, 1.106369533, 1.122462048, 1.138788635, 1.155352697, 1.172157689,
            1.189207115, 1.206504531, 1.224053543, 1.241857812, 1.259921050, 1.278247024,
            1.296839555, 1.315702520, 1.334839854, 1.354255547, 1.373953647, 1.393938263,
            1.414213562, 1.434783772, 1.455653183, 1.476826146, 1.498307077, 1.520100455,
            1.542210825, 1.564642798, 1.587401052, 1.610490332, 1.633915453, 1.657681301,
            1.681792831, 1.706255071, 1.731073122, 1.756252160, 1.781797436, 1.807714277,
            1.834008086, 1.860684348, 1.887748625, 1.915206561, 1.943063882, 1.971326397 };
        int ch = seq_reg[0] & 3, q = (int)seq_reg[2] - 5;         /* pitch 5 = C3, 130.81 Hz */
        signed char a = (signed char)seq_reg[1];
        double hz, f; int block;
        seq_note n;
        n.amp = a < 0 ? (uint8_t)(-a > 15 ? 15 : -a) : (a > 0 ? 13 : 0);
        hz = 130.8127827;
        if (q < 0) hz = hz * semiq[q + 48] / 2.0;
        else       hz = hz * semiq[q % 48] * (double)(1 << (q / 48));
        /* the OPL2's F-number: hz * 2^(20-block) / 49716, in the lowest block that holds it */
        for (block = 0; block < 7; block++) if (hz * (double)(1 << (20 - block)) / 49716.0 < 1024.0) break;
        f = hz * (double)(1 << (20 - block)) / 49716.0;
        n.freq = (uint16_t)((f > 1023.0 ? 1023 : (int)f) | (block << 10));
        n.dur = v;
        if (seq_reg[0] & 0x10) { seq_len[ch] = 0; seq_left[ch] = 0; }          /* flush: this note now */
        if (seq_left[ch] == 0) seq_start(ch, &n);
        else if (seq_len[ch] < SEQ_DEPTH) { seq_q[ch][(seq_head[ch] + seq_len[ch]) % SEQ_DEPTH] = n; seq_len[ch]++; }
    }
}
void seq_reset(void)
{
    memset(seq_q, 0, sizeof seq_q); memset(seq_head, 0, sizeof seq_head); memset(seq_len, 0, sizeof seq_len);
    memset(seq_reg, 0, sizeof seq_reg); memset(seq_left, 0, sizeof seq_left);
}
void seq_state_save(FILE *f)
{
    state_put(f, "SEQQ", seq_q, sizeof seq_q);
    state_put(f, "SEQH", seq_head, sizeof seq_head);
    state_put(f, "SEQL", seq_len, sizeof seq_len);
    state_put(f, "SEQR", seq_reg, sizeof seq_reg);
    state_put(f, "SEQF", seq_left, sizeof seq_left);
}
int seq_state_load(FILE *f)
{
    if (state_get(f, "SEQQ", seq_q, sizeof seq_q) || state_get(f, "SEQH", seq_head, sizeof seq_head)
        || state_get(f, "SEQL", seq_len, sizeof seq_len) || state_get(f, "SEQR", seq_reg, sizeof seq_reg) || state_get(f, "SEQF", seq_left, sizeof seq_left)) return -2;
    for (int c = 0; c < 4; c++) { seq_head[c] %= SEQ_DEPTH; if (seq_len[c] > SEQ_DEPTH) seq_len[c] = SEQ_DEPTH; }
    return 0;
}
