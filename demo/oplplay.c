/* K4510: OPLPLAY -- the OPL2 playing tunes, with the nine voices on screen.
 *
 * The Pi is an OPL2 machine now (2026-09-01), so this is the demo that shows
 * what that buys: nine FM voices sounding at once, and nine sprites moving to
 * them.  Each voice owns one orb; a note-on kicks its orb upward and lights
 * it, and the orb falls back under gravity while the envelope decays.  So the
 * movement is not decoration bolted beside the music -- it is the note-ons,
 * drawn.
 *
 * THE MUSIC IS ORIGINAL.  Three short pieces written for this program, so
 * they carry the project's licence like the rest of the repository and there
 * is no third-party music to vendor, credit or explain.  They are patterns of
 * sixteenth notes per voice, which is a chiptune tracker's idea of a score
 * and fits in a few hundred bytes.
 *
 * The chip is wired the AdLib's way -- $D480 address, $D481 data -- so every
 * register here is the one in an OPL2 programming guide, and the patches are
 * ordinary two-operator AdLib patches.
 *
 * IF /OPL EXISTS, IT PLAYS THAT INSTEAD.  tools/vgm2opl.py turns VGM/VGZ OPL2
 * logs into .OPL files -- the machine's YM3812 runs at 3579545 Hz, the AdLib
 * crystal every VGM assumes, so they play at the right pitch untouched.  That
 * library is nobody's to redistribute, so it is not in this repository and
 * /OPL is in .gitignore; the three built-in tunes are what ships, and what
 * plays when the directory is absent.
 *
 *   SPACE  next tune      1-9  mute a voice      Q or Escape  back to the shell
 */
#include "k4510.h"
#include "far.h"

#define OPL_ADDR 0xD480u
#define OPL_DATA 0xD481u
#define OPL_ID   0xD482u

#define NCH      9
#define ORB      0x120000UL       /* 16x16, 4 bpp = 128 bytes */
#define SPRTAB_A 0x121000UL
#define SPRTAB_B 0x122000UL
#define TEXTMAP  0x123000UL
#define LISTBUF  0x130000UL       /* the /OPL directory: MAXTUNES names of 32 */
#define TUNEBUF  0x200000UL       /* the loaded .OPL, whole; they run 10-60 KB */
#define MAXTUNES 2048             /* the archive this was written against holds 826 */

/* channel n's modulator slot; the carrier is three on */
static const uint8_t opslot[NCH] = { 0, 1, 2, 8, 9, 10, 16, 17, 18 };
/* F-numbers for one octave: fnum = f * 2^16 / 49716 */
static const uint16_t fnum[12] = { 345, 365, 387, 410, 434, 460, 488, 517, 547, 580, 614, 651 };
#define N(o, s) ((o) * 12 + (s))
enum { C, Cs, D, Ds, E, F, Fs, G, Gs, A, As, B };

static void opl(uint8_t reg, uint8_t val) { REG(OPL_ADDR) = reg; REG(OPL_DATA) = val; }
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

/* ---- the three tunes ---------------------------------------------------- *
 * One byte per sixteenth per voice: 0 is a rest, anything else is a note.
 * Written for this machine; not derived from anything. */
#define R 0
/* Each tune carries its own length: the waltz is in three and does not fit
 * the march's thirty-two.  32 for 4/4, 48 for 3/4 (eight bars of six). */

/* 1. "IRON MARCH" -- D minor, four on the floor, a stubborn bass */
static const uint8_t m_bass[] = {
    N(2,D),R,R,R, N(2,D),R,R,R, N(2,A),R,R,R, N(2,A),R,R,R,
    N(2,As),R,R,R, N(2,As),R,R,R, N(2,F),R,N(2,G),R, N(2,A),R,R,R };
static const uint8_t m_ch1[] = {
    N(4,D),R,R,R, R,R,R,R, N(4,E),R,R,R, R,R,R,R,
    N(4,F),R,R,R, R,R,R,R, N(4,C),R,R,R, N(4,E),R,R,R };
static const uint8_t m_ch2[] = {
    N(4,F),R,R,R, R,R,R,R, N(4,A),R,R,R, R,R,R,R,
    N(4,As),R,R,R, R,R,R,R, N(4,A),R,R,R, N(4,A),R,R,R };
static const uint8_t m_mel[] = {
    N(5,D),R,N(5,E),R, N(5,F),R,N(5,E),R, N(5,D),R,R,R, N(5,A),R,R,R,
    N(5,As),R,N(5,A),R, N(5,G),R,N(5,F),R, N(5,E),R,N(5,D),R, N(5,D),R,R,R };
static const uint8_t m_arp[] = {
    N(5,D),N(5,F),N(5,A),N(5,F), N(5,D),N(5,F),N(5,A),N(5,F),
    N(5,E),N(5,A),N(5,Cs),N(5,A), N(5,E),N(5,A),N(5,Cs),N(5,A),
    N(5,F),N(5,As),N(5,D),N(5,As), N(5,F),N(5,As),N(5,D),N(5,As),
    N(5,E),N(5,A),N(5,Cs),N(5,A), N(5,E),N(5,A),N(5,Cs),N(5,A) };
static const uint8_t m_perc[] = {
    N(3,C),R,R,N(3,C), R,R,N(3,C),R, N(3,C),R,R,N(3,C), R,R,N(3,C),R,
    N(3,C),R,R,N(3,C), R,R,N(3,C),R, N(3,C),R,N(3,C),R, N(3,C),N(3,C),N(3,C),R };

/* 2. "GLASS WALTZ" -- G major, three beats to the bar, eight bars of six */
static const uint8_t w_bass[] = {
    N(2,G),R,R,R,R,R,  N(2,D),R,R,R,R,R,  N(2,C),R,R,R,R,R,  N(2,D),R,R,R,R,R,
    N(2,G),R,R,R,R,R,  N(2,B),R,R,R,R,R,  N(2,C),R,R,R,R,R,  N(2,D),R,R,R,R,R };
static const uint8_t w_ch1[] = {
    R,R,N(4,B),R,N(4,B),R,  R,R,N(4,A),R,N(4,A),R,  R,R,N(4,G),R,N(4,G),R,  R,R,N(4,A),R,N(4,A),R,
    R,R,N(4,B),R,N(4,B),R,  R,R,N(4,G),R,N(4,G),R,  R,R,N(4,A),R,N(4,A),R,  R,R,N(4,B),R,N(4,B),R };
static const uint8_t w_ch2[] = {
    R,R,N(4,D),R,N(4,D),R,  R,R,N(4,Fs),R,N(4,Fs),R,  R,R,N(4,E),R,N(4,E),R,  R,R,N(4,Fs),R,N(4,Fs),R,
    R,R,N(4,D),R,N(4,D),R,  R,R,N(4,D),R,N(4,D),R,  R,R,N(4,E),R,N(4,E),R,  R,R,N(4,Fs),R,N(4,Fs),R };
static const uint8_t w_mel[] = {
    N(5,D),R,R,R,R,R,  N(5,Fs),R,R,R,N(5,E),R,  N(5,G),R,R,R,R,R,  N(5,Fs),R,R,R,N(5,E),R,
    N(5,D),R,R,R,R,R,  N(5,B),R,R,R,N(5,A),R,  N(5,G),R,R,R,N(5,A),R,  N(5,B),R,R,R,R,R };
static const uint8_t w_arp[] = {
    N(5,G),R,N(5,B),R,N(5,D),R,  N(5,Fs),R,N(5,A),R,N(5,D),R,  N(5,E),R,N(5,G),R,N(5,C),R,
    N(5,Fs),R,N(5,A),R,N(5,D),R,  N(5,G),R,N(5,B),R,N(5,D),R,  N(5,G),R,N(5,B),R,N(5,D),R,
    N(5,E),R,N(5,G),R,N(5,C),R,   N(5,Fs),R,N(5,A),R,N(5,D),R };
static const uint8_t w_perc[] = {
    N(3,G),R,R,R,R,R,  N(3,G),R,R,R,R,R,  N(3,G),R,R,R,R,R,  N(3,G),R,R,R,R,R,
    N(3,G),R,R,R,R,R,  N(3,G),R,R,R,R,R,  N(3,G),R,R,R,R,R,  N(3,G),R,R,R,R,R };

/* 3. "RUNNER" -- fast, A minor, everything moving at once */
static const uint8_t r_bass[] = {
    N(2,A),R,N(2,A),R, N(2,A),R,N(2,A),R, N(2,F),R,N(2,F),R, N(2,F),R,N(2,F),R,
    N(2,C),R,N(2,C),R, N(2,C),R,N(2,C),R, N(2,G),R,N(2,G),R, N(2,G),R,N(2,G),R };
static const uint8_t r_ch1[] = {
    N(4,C),R,R,R, N(4,C),R,R,R, N(4,C),R,R,R, N(4,C),R,R,R,
    N(4,C),R,R,R, N(4,C),R,R,R, N(4,B),R,R,R, N(4,B),R,R,R };
static const uint8_t r_ch2[] = {
    N(4,E),R,R,R, N(4,E),R,R,R, N(4,A),R,R,R, N(4,A),R,R,R,
    N(4,G),R,R,R, N(4,G),R,R,R, N(4,D),R,R,R, N(4,D),R,R,R };
static const uint8_t r_mel[] = {
    N(5,A),N(5,B),N(5,C),N(5,E), N(5,A),R,N(5,G),R, N(5,F),N(5,G),N(5,A),N(5,C), N(5,F),R,N(5,E),R,
    N(5,E),N(5,F),N(5,G),N(5,B), N(5,E),R,N(5,D),R, N(5,D),N(5,E),N(5,F),N(5,A), N(5,G),R,R,R };
static const uint8_t r_arp[] = {
    N(4,A),N(5,C),N(5,E),N(5,C), N(4,A),N(5,C),N(5,E),N(5,C),
    N(4,F),N(4,A),N(5,C),N(4,A), N(4,F),N(4,A),N(5,C),N(4,A),
    N(4,C),N(4,E),N(4,G),N(4,E), N(4,C),N(4,E),N(4,G),N(4,E),
    N(4,G),N(4,B),N(5,D),N(4,B), N(4,G),N(4,B),N(5,D),N(4,B) };
static const uint8_t r_perc[] = {
    N(3,C),R,N(3,C),N(3,C), N(3,C),R,N(3,C),R, N(3,C),R,N(3,C),N(3,C), N(3,C),R,N(3,C),R,
    N(3,C),R,N(3,C),N(3,C), N(3,C),R,N(3,C),R, N(3,C),N(3,C),N(3,C),N(3,C), N(3,C),N(3,C),N(3,C),N(3,C) };

/* voice 0 bass, 1-2 chord, 3 melody, 4 arpeggio, 5-7 pad (chord, held), 8 percussion */
typedef struct {
    const char *name;
    uint8_t frames;                       /* frames per step: the tempo */
    uint8_t len;                          /* steps in the pattern */
    const uint8_t *trk[6];                /* bass, ch1, ch2, melody, arp, perc */
} song_t;
static const song_t songs[3] = {
    { "IRON MARCH   D minor, 4/4",  7, 32, { m_bass, m_ch1, m_ch2, m_mel, m_arp, m_perc } },
    { "GLASS WALTZ  G major, 3/4",  9, 48, { w_bass, w_ch1, w_ch2, w_mel, w_arp, w_perc } },
    { "RUNNER       A minor, fast", 4, 32, { r_bass, r_ch1, r_ch2, r_mel, r_arp, r_perc } },
};
/* which channel each track drives; the pad voices 5-7 follow the chord tracks */
static const uint8_t trk_ch[6] = { 0, 1, 2, 3, 4, 8 };

static uint8_t muted[NCH];
static int16_t oy[NCH], energy[NCH];      /* 12.4 fixed: the orbs, and how far each is lifted */
static uint8_t lit[NCH];
static uint8_t cur_tab;

/* ---- keeping real time ---------------------------------------------------
 * SYS+$36 is a free-running millisecond counter read from the host's own
 * clock.  The frame counter is no good for music: it counts frames the
 * machine RENDERED, so when the host runs long the tune slows with it and
 * speeds back up when it catches up -- which is exactly what the Pi did.
 *
 * Sixteen bits of it is plenty.  It wraps every 65.5 seconds and only the
 * difference between two reads is ever used, so the wrap costs nothing as
 * long as the reads are closer together than that -- and they are one frame
 * apart.  A gap larger than a quarter second is a load or a menu, not lost
 * time, and is clamped rather than made up: catching up two hundred steps at
 * once would be a burst of noise, not music. */
#define MSCLK 0xD536u
static uint16_t ms_last;
static uint16_t ms_acc;                   /* thousandths of a step still owed */
static uint16_t ms_now(void) { return (uint16_t)(REG(MSCLK) | ((uint16_t)REG(MSCLK + 1) << 8)); }
static uint8_t steps_due(uint8_t per_step_frames)
{
    uint16_t now = ms_now(), dt = (uint16_t)(now - ms_last);
    uint8_t n = 0;
    ms_last = now;
    if (dt > 250) dt = 250;               /* a stall, not elapsed music */
    ms_acc = (uint16_t)(ms_acc + dt * 60);        /* 60ths of a second, x1000 */
    while (ms_acc >= (uint16_t)(1000u * per_step_frames) && n < 8) {
        ms_acc = (uint16_t)(ms_acc - 1000u * per_step_frames);
        n++;
    }
    if (n == 8) ms_acc = 0;               /* fell far behind: take the loss */
    return n;
}

/* ---- the .OPL library, when there is one -------------------------------- */
static uint16_t ntunes;                   /* 0 = play the built-in tunes */
static char fsname[36];
static uint32_t tp, tend;                 /* the walk: cursor and end */
static uint32_t tloop;                    /* loop point, 0 = one-shot */
static uint16_t twait;                    /* frames still owed to a 02 nn */
static char ttitle[32];

static uint8_t fs_cmd(uint8_t c) { REG(0xD300) = c; return REG(0xD301); }
static void fs_setname(const char *n) { far_w32(0xD304, (uint16_t)n); }

static void list_tunes(void)
{
    ntunes = 0;
    fs_setname("/OPL"); if (fs_cmd(11)) return;          /* no /OPL: built-ins it is */
    if (fs_cmd(6)) return;
    for (;;) {
        uint8_t i, len; char *e;
        far_w32(0xD308, (uint16_t)fsname);
        if (fs_cmd(7)) break;
        if (far_r32(0xD310) == 0xFFFFFFFFUL) continue;   /* a directory */
        len = 0; while (fsname[len]) len++;
        if (len < 5) continue; e = fsname + len - 4;
        if (e[0] != '.' || (e[1] | 0x20) != 'o' || (e[2] | 0x20) != 'p' || (e[3] | 0x20) != 'l') continue;
        for (i = 0; i < 31; i++) far_poke(LISTBUF + (uint32_t)ntunes * 32 + i, (uint8_t)(i < len ? fsname[i] : 0));
        far_poke(LISTBUF + (uint32_t)ntunes * 32 + 31, 0);
        if (++ntunes == MAXTUNES) break;
    }
}

/* Load tune n and stand the walker at the first command.  A bad header is not
 * fatal: the caller steps to the next file rather than playing noise. */
static uint8_t load_tune(uint16_t n)
{
    uint32_t size; uint8_t i;
    for (i = 0; i < 31; i++) fsname[i] = (char)far_peek(LISTBUF + (uint32_t)n * 32 + i);
    fsname[31] = 0;
    fs_setname(fsname); far_w32(0xD308, TUNEBUF);
    if (fs_cmd(9)) return 1;
    size = far_r32(0xD30C);
    if (size < 45) return 1;
    if (far_peek(TUNEBUF) != 'K' || far_peek(TUNEBUF + 1) != '4' ||
        far_peek(TUNEBUF + 2) != 'O' || far_peek(TUNEBUF + 3) != 'P') return 1;
    tloop = far_r32(TUNEBUF + 8);
    if (tloop) tloop += TUNEBUF + 44;
    for (i = 0; i < 31; i++) ttitle[i] = (char)far_peek(TUNEBUF + 12 + i);
    ttitle[31] = 0;
    if (!ttitle[0]) { for (i = 0; i < 31; i++) ttitle[i] = fsname[i]; ttitle[31] = 0; }
    tp = TUNEBUF + 44; tend = TUNEBUF + size; twait = 0;
    return 0;
}

/* One frame of the stream: every write up to the next wait.  Key-ons are
 * lifted out as they go past so the orbs still move -- $B0+n bit 5 is the
 * key, and the block and F-number high bits in the same byte are pitch
 * enough to throw the orb by. */
static uint8_t step_tune(void)
{
    uint8_t c, r, d;
    if (twait) { twait--; return 0; }
    for (;;) {
        if (tp >= tend) { if (tloop) { tp = tloop; continue; } return 1; }
        c = far_peek(tp);
        if (c == 0x01) {
            r = far_peek(tp + 1); d = far_peek(tp + 2); tp += 3;
            if (r >= 0xB0 && r <= 0xB8) {
                uint8_t ch = (uint8_t)(r - 0xB0);
                if (muted[ch]) continue;                 /* a muted voice is not written at all */
                if (d & 0x20) {
                    uint8_t pitch = (uint8_t)(((d >> 2) & 7) * 8 + ((d & 3) << 1));
                    energy[ch] = (int16_t)((30 + (int16_t)pitch * 4) << 4);
                    lit[ch] = 15;
                }
            }
            opl(r, d);
        } else if (c == 0x02) {
            twait = far_peek(tp + 1); tp += 2;
            if (twait) twait--;
            return 0;
        } else if (c == 0x00) {
            if (tloop) { tp = tloop; continue; }
            return 1;
        } else tp++;                                     /* not ours: step over it */
    }
}


static void keyoff(uint8_t ch) { opl((uint8_t)(0xB0 + ch), 0); }
static void note_on(uint8_t ch, uint8_t note)
{
    uint16_t f = fnum[note % 12];
    uint8_t block = note / 12;
    if (block > 7) block = 7;
    if (muted[ch]) return;
    keyoff(ch);
    opl((uint8_t)(0xA0 + ch), (uint8_t)f);
    opl((uint8_t)(0xB0 + ch), (uint8_t)(0x20 | (block << 2) | ((f >> 8) & 3)));
    /* Not ballistic.  A note sets the orb's height outright and it sinks back,
     * the way a peak meter falls -- which is the only model that survives both
     * ends of the tempo range.  Adding velocity ratchets (a second note before
     * the orb lands adds to the climb, and busy voices end up parked against
     * the ceiling); resetting the position truncates (on the fast tune a note
     * arrives every four frames and the orb never leaves the floor).
     *
     * The height is the note's own pitch, so the picture carries the tune's
     * shape and not just its rhythm: the bass barely lifts, the melody's top
     * notes clear most of the screen. */
    energy[ch] = (int16_t)((40 + ((int16_t)note - 24) * 6) << 4);
    lit[ch] = 15;
}

static void make_orb(void)
{
    uint32_t d = ORB; uint8_t x, y;
    for (y = 0; y < 16; y++) for (x = 0; x < 16; x += 2) {
        uint8_t v[2], k;
        for (k = 0; k < 2; k++) {
            int16_t dx = (int16_t)(x + k) - 8, dy = (int16_t)y - 8;
            uint16_t r2 = (uint16_t)(dx * dx + dy * dy);
            if (r2 > 7 * 7) v[k] = 0;
            else { int16_t hx = dx + 3, hy = dy + 3;
                   uint16_t h = (uint16_t)(hx * hx + hy * hy);
                   v[k] = (uint8_t)(15 - (h >> 3)); if (v[k] < 3) v[k] = 3; if (v[k] > 15) v[k] = 15; }
        }
        far_poke(d++, (uint8_t)((v[0] << 4) | v[1]));
    }
}
static void make_palette(void)
{
    static const uint8_t hue[NCH][3] = {
        {255,80,80},{255,160,60},{255,240,80},{140,255,80},{60,230,140},
        {60,230,230},{80,150,255},{160,90,255},{255,90,200} };
    uint8_t b, s;
    for (b = 0; b < NCH; b++) for (s = 0; s < 16; s++)
        pal((uint8_t)((b + 1) * 16 + s),
            (uint8_t)(((uint16_t)hue[b][0] * s) / 15),
            (uint8_t)(((uint16_t)hue[b][1] * s) / 15),
            (uint8_t)(((uint16_t)hue[b][2] * s) / 15));
    pal(1, 255, 255, 255);
}
static void init_table(uint32_t tab)
{
    uint8_t i;
    dma_fill(0, tab, 2048);
    for (i = 0; i < NCH; i++, tab += 16) {
        far_poke16(tab + 4, (uint16_t)ORB); far_poke16(tab + 6, (uint16_t)(ORB >> 16));
        far_poke(tab + 8, 1);                 /* enable, 4 bpp */
        far_poke(tab + 9, (uint8_t)(1 | (1 << 2)));   /* 16 x 16 */
        far_poke(tab + 10, (uint8_t)(i + 1)); /* palette bank: one colour per voice */
    }
}
static void write_table(uint32_t tab)
{
    uint8_t i;
    for (i = 0; i < NCH; i++, tab += 16) {
        far_poke16(tab, (uint16_t)(40 + i * 64));
        far_poke16(tab + 2, (uint16_t)(oy[i] >> 4));
    }
}

static void setup_voices(void)
{
    opl(0x01, 0x20);                                   /* waveform select enabled */
    opl(0x08, 0x00); opl(0xBD, 0x00);                  /* no CSM, no rhythm mode */
    /* bass */            patch(0, 0x21, 0x18, 0xF2, 0x74, 0, 0x21, 0x00, 0xF4, 0x76, 0, 0x0E);
    /* chord 1, 2 */      patch(1, 0x31, 0x1C, 0x53, 0x64, 1, 0x31, 0x06, 0x63, 0x55, 0, 0x08);
                          patch(2, 0x31, 0x1C, 0x53, 0x64, 1, 0x31, 0x06, 0x63, 0x55, 0, 0x08);
    /* melody: a bell */  patch(3, 0x01, 0x20, 0xF6, 0x36, 0, 0x11, 0x00, 0xF5, 0x37, 0, 0x0A);
    /* arpeggio pluck */  patch(4, 0x11, 0x24, 0xF8, 0xA6, 2, 0x11, 0x00, 0xF9, 0xB6, 0, 0x0C);
    /* pad 5,6,7 */       patch(5, 0x61, 0x2A, 0x24, 0x35, 0, 0x61, 0x10, 0x34, 0x46, 0, 0x08);
                          patch(6, 0x61, 0x2A, 0x24, 0x35, 0, 0x61, 0x10, 0x34, 0x46, 0, 0x08);
                          patch(7, 0x61, 0x2A, 0x24, 0x35, 0, 0x61, 0x10, 0x34, 0x46, 0, 0x08);
    /* percussion */      patch(8, 0x0F, 0x00, 0xF8, 0xF8, 3, 0x0F, 0x08, 0xF8, 0xF7, 3, 0x0E);
}

static void caption(uint8_t s)
{
    uint8_t i;
    dma_fill(' ', TEXTMAP, 80 * 60);
    text8_print(TEXTMAP, 80, 2, 1, "K4510   OPLPLAY -- the OPL2, nine voices, nine orbs");
    if (ntunes) { char b[40]; uint8_t i;
                  for (i = 0; i < 31 && ttitle[i]; i++) b[i] = ttitle[i];
                  b[i] = 0;
                  text8_print(TEXTMAP, 80, 2, 3, b); }
    else text8_print(TEXTMAP, 80, 2, 3, (char *)songs[s].name);
    text8_print(TEXTMAP, 80, 2, 56, "SPACE next tune    1-9 mute a voice    Q returns to the shell");
    if (ntunes) { char b[48]; uint16_t v = ntunes; uint8_t i = 0, j;
                  char d[6]; j = 0; do { d[j++] = (char)('0' + v % 10); v /= 10; } while (v);
                  while (j) b[i++] = d[--j];
                  b[i++] = ' '; b[i++] = 't'; b[i++] = 'u'; b[i++] = 'n'; b[i++] = 'e'; b[i++] = 's';
                  b[i++] = ' '; b[i++] = 'i'; b[i++] = 'n'; b[i++] = ' '; b[i++] = '/'; b[i++] = 'O';
                  b[i++] = 'P'; b[i++] = 'L'; b[i] = 0;
                  text8_print(TEXTMAP, 80, 55, 3, b); }
    text8_print(TEXTMAP, 80, 2, 57, "each orb is one FM voice; a note-on throws it up");
    /* The program cannot tell silence from sound, so say where the volume is.
     * (Until 2026-09-05 this pointed at a Sound chip row; the OPL2 is the only
     * chip now and that row is gone.) */
    text8_print(TEXTMAP, 80, 2, 5, "silent?  F7 - Audio - Volume, then the host's own mixer");
    for (i = 0; i < NCH; i++) {
        char b[2]; b[0] = (char)('1' + i); b[1] = 0;
        text8_print(TEXTMAP, 80, (uint8_t)(5 + i * 8), 54, b);
    }
}

void main(void)
{
    uint16_t song = 0;
    uint8_t step = 0, tick = 0, i, k, quit = 0;
    uint16_t frame = 0;

    REG(V_CTRL) = 0;
    list_tunes();                                  /* /OPL if it is there, the built-ins if not */
    if (ntunes) { while (ntunes && load_tune(song)) {   /* a bad file: drop it and try the next */
                      uint16_t j; ntunes--;
                      for (j = song; j < ntunes; j++) { uint8_t b2;
                          for (b2 = 0; b2 < 32; b2++)
                              far_poke(LISTBUF + (uint32_t)j * 32 + b2,
                                       far_peek(LISTBUF + (uint32_t)(j + 1) * 32 + b2)); }
                      if (song >= ntunes) song = 0; } }
    make_orb(); make_palette();
    for (i = 0; i < NCH; i++) { oy[i] = (int16_t)(430 << 4); energy[i] = 0; muted[i] = 0; }   /* resting on the floor */
    caption((uint8_t)(ntunes ? 0 : song));
    text8_layer(0, TEXTMAP, 80, 0);
    init_table(SPRTAB_A); init_table(SPRTAB_B);
    write_table(SPRTAB_A); w32(V_SPRTAB, SPRTAB_A); REG(V_SPRCTL) = 1;
    setup_voices();
    ms_last = ms_now(); ms_acc = 0;             /* start the clock here, not at zero */
    REG(V_CTRL) = 1;

    while (!quit) {
        uint32_t back = cur_tab ? SPRTAB_A : SPRTAB_B;

        /* ---- the stream, or the sequencer ---- */
        if (ntunes) {
            uint8_t due = steps_due(1), ended = 0;
            while (due-- && !ended) ended = step_tune();
            if (ended) {                           /* the tune ended and does not loop: next */
                for (i = 0; i < NCH; i++) keyoff(i);
                song = (uint16_t)((song + 1) % ntunes);
                if (load_tune(song)) { for (i = 0; i < NCH; i++) keyoff(i); }
                caption(0); ms_last = ms_now(); ms_acc = 0;
            }
        } else for (tick = steps_due(songs[song].frames); tick; tick--) {
            for (i = 0; i < 6; i++) {
                uint8_t n = songs[song].trk[i][step];
                if (n) note_on(trk_ch[i], n);
            }
            /* the three pad voices take the chord, an octave down, on the bar */
            if (step % (songs[song].len == 48 ? 6 : 8) == 0) {
                uint8_t a = songs[song].trk[0][step], b = songs[song].trk[1][step], c = songs[song].trk[2][step];
                if (a) note_on(5, (uint8_t)(a + 12));
                if (b) note_on(6, b);
                if (c) note_on(7, c);
            }
            if (++step >= songs[song].len) step = 0;
        }

        /* ---- the orbs: thrown by note-ons, pulled back by gravity ---- */
        for (i = 0; i < NCH; i++) {
            energy[i] -= (int16_t)((energy[i] >> 4) + 12);        /* fast at first, then easing down */
            if (energy[i] < 0) energy[i] = 0;
            oy[i] = (int16_t)((430 << 4) - energy[i]);
            if (oy[i] < (int16_t)(40 << 4)) oy[i] = (int16_t)(40 << 4);
            if (lit[i]) lit[i]--;
        }
        write_table(back);
        w32(V_SPRTAB, back); cur_tab ^= 1;

        wait_vblank();                             /* the frame counter at $D50D, as the other demos do */
        frame++;

        k = key_get();
        if (k == 'q' || k == 'Q' || k == 0x1B) quit = 1;
        else if (k == ' ') { for (i = 0; i < NCH; i++) keyoff(i);
                             if (ntunes) { song = (uint16_t)((song + 1) % ntunes);
                                           if (load_tune(song)) song = 0;
                                           caption(0); ms_last = ms_now(); ms_acc = 0; }
                             else { song = (uint16_t)((song + 1) % 3); step = 0; tick = 0; caption((uint8_t)song); } }
        else if (k >= '1' && k <= '9') { uint8_t v = (uint8_t)(k - '1');
                                         muted[v] ^= 1; if (muted[v]) keyoff(v); }
    }

    for (i = 0; i < NCH; i++) keyoff(i);
    REG(V_SPRCTL) = 0;
    ((void (*)(void))0xFF92)();                    /* the ROM's VIDEO call: the text screen back */
}
