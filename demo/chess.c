/* K4510: CHESS -- the KoboChess board, on this machine.
 *
 * The game is KoboChess's (github.com/mlongval/KoboChess): its levels,
 * its buttons -- Game, Undo, Hint, Flip, Options -- its coordinates in the
 * squares.  The look, since 2026-10-09, is the Personality Chooser's (Doc:
 * "The vibe is very 8bit ... redo the Chess game, using the same style"):
 * 320x240 doubled, the grey bands with the time, the blue glass, the
 * banner's bars, unscii, 22 px squares and a Pixel piece set drawn for
 * them.  The panel on the right holds the side to move, the engine, the
 * moves, the captured pieces and the clocks; the menus are the Chooser's
 * list.  The pieces are 4 bpp sprites with a palette bank per side, so
 * Options -> Piece colours can make them classic, wood, red-and-blue or
 * green-and-purple, and Options -> Piece set still has KoboChess's drawn
 * set (Doc's own) and the icons.
 *
 * Three engines.  The built-in one is a small alpha-beta with quiescence,
 * material and piece-square tables, the six KoboChess levels (with the
 * same names and the same "noise" that keeps the weak ones human).  Where
 * the Tube has a UCI engine fitted ($D800 bit 3: Stockfish on a desktop
 * or the K4510 Linux), that is program 5 and it plays at the KoboChess Elo for the
 * level.  Anywhere with a network (the Pi included), a UCI engine served
 * over TCP -- tools/uci-server.sh on any Linux box -- does the same
 * through the N: device; /APPS/CHESS/ENGINE.CFG names it.
 *
 *   arrows / pad / mouse   pick a square, then where it goes
 *   Enter or space         select          Esc      undo the pick, or leave
 *   F1 Game  F2 Undo  F3 Hint  F4 Flip  F5 Options   (or click them in the band)
 *   in a menu: 1-9, or the arrows and Enter
 */
#include "k4510.h"
#include "chess.h"

#define CHESS_PHYS 0x00110000UL
#define SPRD     CHESS_PHYS
#define BMP      0x00200000UL           /* 320x240 8 bpp, layer 0 */
#define SPRTAB   0x00252000UL
#define PGNBUF   ((char *)0x0800)       /* 6 KB below the program: the PGN as it is built */
#define PGNMAX   0x1700u
#define SEQ      0xD5E0u
#define TUBE     0xD800u
#define NET      0xD900u
#define MOUSEX   0xD108u
#define KBDBREAK 0xD103u
#define KEY_ESC 0x1B
#define KEY_ENTER 0x0D
#define KEY_F1 0x90

void __fastcall__ rom_chrout(unsigned char c);
static unsigned char rom_save(void) { return ((unsigned char (*)(void))0xFF8C)(); }
static void zp16(uint8_t a, uint16_t v) { REG(a) = v; REG(a + 1) = v >> 8; }
static void zp32(uint8_t a, uint32_t v) { REG(a)=v; REG(a+1)=v>>8; REG(a+2)=v>>16; REG(a+3)=v>>24; }

/* ---- the board ------------------------------------------------------------ */
#define WHITE 0
#define BLACK 1
enum { EMPTY, PAWN, KNIGHT, BISHOP, ROOK, QUEEN, KING };
#define BLK   8
#define KIND(p)  ((p) & 7)
#define COLOUR(p) ((p) >> 3)
#define SQ(f, r) (((r) << 4) | (f))
#define FILE_OF(s) ((s) & 7)
#define RANK_OF(s) ((s) >> 4)
#define OFFBOARD(s) ((s) & 0x88)
#define MF_CAP 1
#define MF_EP 2
#define MF_CASTLE 4
#define MF_DOUBLE 8
#define MF_PROMO 16
typedef struct { uint8_t from, to, promo, flags; } move_t;
typedef struct { move_t m; uint8_t cap, castle, half; int8_t ep; } hist_t;
#define MAXHIST 300
#define MAXPLY  14
#define MAXMV   96
static uint8_t bd[128];
static uint8_t stm, castle, half;
static int8_t ep;
static uint8_t ksq[2];
/* The game's history is the biggest thing this program keeps, and since the
 * port went in there is no room for it under the I/O page: it lives in the
 * RAM above it, which a program owns as well.  prg0's zerobss does not reach
 * this segment -- nhist (in BSS, zeroed) is what says how much of it is real. */
#pragma bss-name (push, "BSS2")
static hist_t hist[MAXHIST];
static char san[MAXHIST][8];
#pragma bss-name (pop)
static uint16_t nhist;
static move_t ml[MAXPLY][MAXMV]; static uint8_t mn[MAXPLY];
static const int8_t KN_D[8] = { 33, 31, 18, 14, -33, -31, -18, -14 };
static const int8_t KG_D[8] = { 1, -1, 16, -16, 17, 15, -17, -15 };
static const int8_t BI_D[4] = { 17, 15, -17, -15 };
static const int8_t RO_D[4] = { 1, -1, 16, -16 };
static const int16_t VAL[7] = { 0, 100, 320, 330, 500, 900, 0 };

static void set_start(void)
{
    static const uint8_t back[8] = { ROOK, KNIGHT, BISHOP, QUEEN, KING, BISHOP, KNIGHT, ROOK };
    uint8_t f;
    for (f = 0; f < 128; f++) bd[f] = EMPTY;
    for (f = 0; f < 8; f++) { bd[SQ(f, 0)] = back[f]; bd[SQ(f, 1)] = PAWN; bd[SQ(f, 6)] = PAWN | BLK; bd[SQ(f, 7)] = back[f] | BLK; }
    stm = WHITE; castle = 15; ep = -1; half = 0; nhist = 0; ksq[0] = SQ(4, 0); ksq[1] = SQ(4, 7);
}
static uint8_t attacked(uint8_t s, uint8_t by)         /* is square s attacked by side `by`? */
{
    uint8_t i, t, p;
    /* pawns */
    if (by == WHITE) { t = s - 17; if (!OFFBOARD(t) && bd[t] == PAWN) return 1; t = s - 15; if (!OFFBOARD(t) && bd[t] == PAWN) return 1; }
    else { t = s + 17; if (!OFFBOARD(t) && bd[t] == (PAWN | BLK)) return 1; t = s + 15; if (!OFFBOARD(t) && bd[t] == (PAWN | BLK)) return 1; }
    for (i = 0; i < 8; i++) { t = s + KN_D[i]; if (!OFFBOARD(t) && bd[t] == (KNIGHT | (by ? BLK : 0))) return 1; }
    for (i = 0; i < 8; i++) { t = s + KG_D[i]; if (!OFFBOARD(t) && bd[t] == (KING | (by ? BLK : 0))) return 1; }
    for (i = 0; i < 4; i++) {
        t = s;
        for (;;) { t += BI_D[i]; if (OFFBOARD(t)) break; p = bd[t]; if (p) { if (COLOUR(p) == by && (KIND(p) == BISHOP || KIND(p) == QUEEN)) return 1; break; } }
        t = s;
        for (;;) { t += RO_D[i]; if (OFFBOARD(t)) break; p = bd[t]; if (p) { if (COLOUR(p) == by && (KIND(p) == ROOK || KIND(p) == QUEEN)) return 1; break; } }
    }
    return 0;
}
static uint8_t in_check(uint8_t side) { return attacked(ksq[side], side ^ 1); }

static void add(uint8_t ply, uint8_t from, uint8_t to, uint8_t flags)
{
    move_t *m;
    if (mn[ply] >= MAXMV) return;
    if ((flags & MF_PROMO)) {
        uint8_t k;
        for (k = QUEEN; k >= KNIGHT; k--) { if (mn[ply] >= MAXMV) return; m = &ml[ply][mn[ply]++]; m->from = from; m->to = to; m->promo = k; m->flags = flags; }
        return;
    }
    m = &ml[ply][mn[ply]++]; m->from = from; m->to = to; m->promo = 0; m->flags = flags;
}
static void gen(uint8_t ply)                         /* pseudo-legal moves of the side to move */
{
    uint8_t s, p, i, t, mine = stm ? BLK : 0;
    mn[ply] = 0;
    for (s = 0; s < 128; s++) {
        if (OFFBOARD(s)) { s += 7; continue; }
        p = bd[s];
        if (!p || COLOUR(p) != stm) continue;
        switch (KIND(p)) {
        case PAWN: {
            int8_t d = stm ? -16 : 16; uint8_t last = stm ? 1 : 6, start = stm ? 6 : 1;
            uint8_t pr = (RANK_OF(s) == last) ? MF_PROMO : 0;
            t = s + d;
            if (!bd[t]) { add(ply, s, t, pr); if (RANK_OF(s) == start && !bd[t + d]) add(ply, s, t + d, MF_DOUBLE); }
            t = s + d - 1; if (!OFFBOARD(t)) { if (bd[t] && COLOUR(bd[t]) != stm) add(ply, s, t, MF_CAP | pr); else if ((int8_t)t == ep) add(ply, s, t, MF_CAP | MF_EP); }
            t = s + d + 1; if (!OFFBOARD(t)) { if (bd[t] && COLOUR(bd[t]) != stm) add(ply, s, t, MF_CAP | pr); else if ((int8_t)t == ep) add(ply, s, t, MF_CAP | MF_EP); }
            break; }
        case KNIGHT:
            for (i = 0; i < 8; i++) { t = s + KN_D[i]; if (OFFBOARD(t)) continue; if (!bd[t]) add(ply, s, t, 0); else if (COLOUR(bd[t]) != stm) add(ply, s, t, MF_CAP); }
            break;
        case KING:
            for (i = 0; i < 8; i++) { t = s + KG_D[i]; if (OFFBOARD(t)) continue; if (!bd[t]) add(ply, s, t, 0); else if (COLOUR(bd[t]) != stm) add(ply, s, t, MF_CAP); }
            if (stm == WHITE && s == SQ(4, 0)) {
                if ((castle & 1) && !bd[SQ(5, 0)] && !bd[SQ(6, 0)] && bd[SQ(7, 0)] == ROOK && !attacked(SQ(4, 0), BLACK) && !attacked(SQ(5, 0), BLACK) && !attacked(SQ(6, 0), BLACK)) add(ply, s, SQ(6, 0), MF_CASTLE);
                if ((castle & 2) && !bd[SQ(3, 0)] && !bd[SQ(2, 0)] && !bd[SQ(1, 0)] && bd[SQ(0, 0)] == ROOK && !attacked(SQ(4, 0), BLACK) && !attacked(SQ(3, 0), BLACK) && !attacked(SQ(2, 0), BLACK)) add(ply, s, SQ(2, 0), MF_CASTLE);
            } else if (stm == BLACK && s == SQ(4, 7)) {
                if ((castle & 4) && !bd[SQ(5, 7)] && !bd[SQ(6, 7)] && bd[SQ(7, 7)] == (ROOK | BLK) && !attacked(SQ(4, 7), WHITE) && !attacked(SQ(5, 7), WHITE) && !attacked(SQ(6, 7), WHITE)) add(ply, s, SQ(6, 7), MF_CASTLE);
                if ((castle & 8) && !bd[SQ(3, 7)] && !bd[SQ(2, 7)] && !bd[SQ(1, 7)] && bd[SQ(0, 7)] == (ROOK | BLK) && !attacked(SQ(4, 7), WHITE) && !attacked(SQ(3, 7), WHITE) && !attacked(SQ(2, 7), WHITE)) add(ply, s, SQ(2, 7), MF_CASTLE);
            }
            break;
        default: {
            const int8_t *dd = (KIND(p) == ROOK) ? RO_D : BI_D; uint8_t nd = 4, pass;
            for (pass = 0; pass < (KIND(p) == QUEEN ? 2 : 1); pass++, dd = RO_D)
                for (i = 0; i < nd; i++) {
                    t = s;
                    for (;;) { t += dd[i]; if (OFFBOARD(t)) break; if (!bd[t]) { add(ply, s, t, 0); continue; } if (COLOUR(bd[t]) != stm) add(ply, s, t, MF_CAP); break; }
                }
            break; }
        }
    }
    (void) mine;
}
static void make(const move_t *m)
{
    hist_t *h = &hist[nhist++];
    uint8_t p = bd[m->from];
    h->m = *m; h->cap = bd[m->to]; h->castle = castle; h->half = half; h->ep = ep;
    if (m->flags & MF_EP) { uint8_t cs = (uint8_t)(m->to + (stm ? 16 : -16)); h->cap = bd[cs]; bd[cs] = EMPTY; }
    bd[m->to] = m->promo ? (uint8_t)(m->promo | (stm ? BLK : 0)) : p; bd[m->from] = EMPTY;
    if (m->flags & MF_CASTLE) {
        if (m->to == SQ(6, 0)) { bd[SQ(5, 0)] = ROOK; bd[SQ(7, 0)] = EMPTY; }
        else if (m->to == SQ(2, 0)) { bd[SQ(3, 0)] = ROOK; bd[SQ(0, 0)] = EMPTY; }
        else if (m->to == SQ(6, 7)) { bd[SQ(5, 7)] = ROOK | BLK; bd[SQ(7, 7)] = EMPTY; }
        else { bd[SQ(3, 7)] = ROOK | BLK; bd[SQ(0, 7)] = EMPTY; }
    }
    if (KIND(p) == KING) { ksq[stm] = m->to; castle &= stm ? 3 : 12; }
    if (m->from == SQ(7, 0) || m->to == SQ(7, 0)) castle &= ~1; if (m->from == SQ(0, 0) || m->to == SQ(0, 0)) castle &= ~2;
    if (m->from == SQ(7, 7) || m->to == SQ(7, 7)) castle &= ~4; if (m->from == SQ(0, 7) || m->to == SQ(0, 7)) castle &= ~8;
    ep = (m->flags & MF_DOUBLE) ? (int8_t)((m->from + m->to) >> 1) : -1;
    half = (KIND(p) == PAWN || (m->flags & MF_CAP)) ? 0 : (uint8_t)(half + 1);
    stm ^= 1;
}
static void unmake(void)
{
    hist_t *h = &hist[--nhist];
    const move_t *m = &h->m; uint8_t p;
    stm ^= 1;
    p = bd[m->to];
    bd[m->from] = m->promo ? (uint8_t)(PAWN | (stm ? BLK : 0)) : p;
    bd[m->to] = (m->flags & MF_EP) ? EMPTY : h->cap;
    if (m->flags & MF_EP) bd[(uint8_t)(m->to + (stm ? 16 : -16))] = h->cap;
    if (m->flags & MF_CASTLE) {
        if (m->to == SQ(6, 0)) { bd[SQ(7, 0)] = ROOK; bd[SQ(5, 0)] = EMPTY; }
        else if (m->to == SQ(2, 0)) { bd[SQ(0, 0)] = ROOK; bd[SQ(3, 0)] = EMPTY; }
        else if (m->to == SQ(6, 7)) { bd[SQ(7, 7)] = ROOK | BLK; bd[SQ(5, 7)] = EMPTY; }
        else { bd[SQ(0, 7)] = ROOK | BLK; bd[SQ(3, 7)] = EMPTY; }
    }
    if (KIND(bd[m->from]) == KING) ksq[stm] = m->from;
    castle = h->castle; half = h->half; ep = h->ep;
}
static uint8_t legal_after(void) { return !in_check(stm ^ 1); }   /* call after make: was the move legal? */
static uint8_t count_legal(uint8_t ply)              /* legal moves at ply, compacted in place; 0 = mate or stalemate */
{
    uint8_t i, n = 0;
    gen(ply);
    for (i = 0; i < mn[ply]; i++) { make(&ml[ply][i]); if (legal_after()) ml[ply][n++] = ml[ply][i]; unmake(); }
    mn[ply] = n; return n;
}

/* ---- the evaluation: material and piece-square tables, White's view ------ */
static const int8_t PST_P[64] = {
     0,  0,  0,  0,  0,  0,  0,  0,   5, 10, 10,-20,-20, 10, 10,  5,   5, -5,-10,  0,  0,-10, -5,  5,   0,  0,  0, 20, 20,  0,  0,  0,
     5,  5, 10, 25, 25, 10,  5,  5,  10, 10, 20, 30, 30, 20, 10, 10,  50, 50, 50, 50, 50, 50, 50, 50,   0,  0,  0,  0,  0,  0,  0,  0 };
static const int8_t PST_N[64] = {
   -50,-40,-30,-30,-30,-30,-40,-50, -40,-20,  0,  5,  5,  0,-20,-40, -30,  5, 10, 15, 15, 10,  5,-30, -30,  0, 15, 20, 20, 15,  0,-30,
   -30,  5, 15, 20, 20, 15,  5,-30, -30,  0, 10, 15, 15, 10,  0,-30, -40,-20,  0,  0,  0,  0,-20,-40, -50,-40,-30,-30,-30,-30,-40,-50 };
static const int8_t PST_B[64] = {
   -20,-10,-10,-10,-10,-10,-10,-20, -10,  5,  0,  0,  0,  0,  5,-10, -10, 10, 10, 10, 10, 10, 10,-10, -10,  0, 10, 10, 10, 10,  0,-10,
   -10,  5,  5, 10, 10,  5,  5,-10, -10,  0,  5, 10, 10,  5,  0,-10, -10,  0,  0,  0,  0,  0,  0,-10, -20,-10,-10,-10,-10,-10,-10,-20 };
static const int8_t PST_R[64] = {
     0,  0,  0,  5,  5,  0,  0,  0,  -5,  0,  0,  0,  0,  0,  0, -5,  -5,  0,  0,  0,  0,  0,  0, -5,  -5,  0,  0,  0,  0,  0,  0, -5,
    -5,  0,  0,  0,  0,  0,  0, -5,  -5,  0,  0,  0,  0,  0,  0, -5,   5, 10, 10, 10, 10, 10, 10,  5,   0,  0,  0,  0,  0,  0,  0,  0 };
static const int8_t PST_Q[64] = {
   -20,-10,-10, -5, -5,-10,-10,-20, -10,  0,  5,  0,  0,  0,  0,-10, -10,  5,  5,  5,  5,  5,  0,-10,   0,  0,  5,  5,  5,  5,  0, -5,
    -5,  0,  5,  5,  5,  5,  0, -5, -10,  0,  5,  5,  5,  5,  0,-10, -10,  0,  0,  0,  0,  0,  0,-10, -20,-10,-10, -5, -5,-10,-10,-20 };
static const int8_t PST_K[64] = {
    20, 30, 10,  0,  0, 10, 30, 20,  20, 20,  0,  0,  0,  0, 20, 20, -10,-20,-20,-20,-20,-20,-20,-10, -20,-30,-30,-40,-40,-30,-30,-20,
   -30,-40,-40,-50,-50,-40,-40,-30, -30,-40,-40,-50,-50,-40,-40,-30, -30,-40,-40,-50,-50,-40,-40,-30, -30,-40,-40,-50,-50,-40,-40,-30 };
static const int8_t *const PST[7] = { 0, PST_P, PST_N, PST_B, PST_R, PST_Q, PST_K };
static int16_t evaluate(void)                       /* from the side to move's view */
{
    int16_t sc = 0; uint8_t s, p, i64;
    for (s = 0; s < 128; s++) {
        if (OFFBOARD(s)) { s += 7; continue; }
        p = bd[s]; if (!p) continue;
        i64 = (uint8_t)((RANK_OF(s) << 3) | FILE_OF(s));
        if (COLOUR(p) == WHITE) sc += VAL[KIND(p)] + PST[KIND(p)][i64];
        else sc -= VAL[KIND(p)] + PST[KIND(p)][(uint8_t)(i64 ^ 56)];
    }
    return stm == WHITE ? sc : (int16_t)-sc;
}

/* ---- the search ---------------------------------------------------------- */
static uint32_t ms(void) { return (uint32_t)REG(SYS + 0x36) | ((uint32_t)REG(SYS + 0x37) << 8) | ((uint32_t)REG(SYS + 0x38) << 16) | ((uint32_t)REG(SYS + 0x39) << 24); }
static uint32_t deadline; static uint16_t nodes; static uint8_t abort_search;
static uint16_t seed = 0x2A17;
static uint8_t rnd(void) { seed = seed * 25173u + 13849u; return (uint8_t)(seed >> 8); }
static void check_time(void)
{
    if ((++nodes & 255) == 0) {
        if (ms() >= deadline) abort_search = 1;
        if (REG(KBDBREAK)) abort_search = 2;     /* Esc: the person wants out */
    }
}
static void order(uint8_t ply)                       /* captures first, most valuable victim first */
{
    uint8_t i, j, n = mn[ply]; move_t t;
    for (i = 1; i < n; i++) {
        int16_t ki = (ml[ply][i].flags & MF_CAP) ? VAL[KIND(bd[ml[ply][i].to])] + 1000 - VAL[KIND(bd[ml[ply][i].from])] / 10 : 0;
        for (j = i; j > 0; j--) {
            int16_t kj = (ml[ply][j - 1].flags & MF_CAP) ? VAL[KIND(bd[ml[ply][j - 1].to])] + 1000 - VAL[KIND(bd[ml[ply][j - 1].from])] / 10 : 0;
            if (kj >= ki) break;
            t = ml[ply][j]; ml[ply][j] = ml[ply][j - 1]; ml[ply][j - 1] = t;
        }
    }
}
static int16_t quiesce(int16_t alpha, int16_t beta, uint8_t ply)
{
    int16_t stand = evaluate(), sc; uint8_t i;
    check_time(); if (abort_search) return 0;
    if (stand >= beta) return beta;
    if (stand > alpha) alpha = stand;
    if (ply >= MAXPLY - 1) return alpha;
    gen(ply); order(ply);
    for (i = 0; i < mn[ply]; i++) {
        if (!(ml[ply][i].flags & (MF_CAP | MF_PROMO))) continue;
        make(&ml[ply][i]);
        if (!legal_after()) { unmake(); continue; }
        sc = (int16_t)-quiesce((int16_t)-beta, (int16_t)-alpha, (uint8_t)(ply + 1));
        unmake();
        if (abort_search) return 0;
        if (sc >= beta) return beta;
        if (sc > alpha) alpha = sc;
    }
    return alpha;
}
static int16_t search(uint8_t depth, int16_t alpha, int16_t beta, uint8_t ply)
{
    uint8_t i, any = 0; int16_t sc;
    if (depth == 0) return quiesce(alpha, beta, ply);
    check_time(); if (abort_search) return 0;
    gen(ply); order(ply);
    for (i = 0; i < mn[ply]; i++) {
        make(&ml[ply][i]);
        if (!legal_after()) { unmake(); continue; }
        any = 1;
        sc = (int16_t)-search((uint8_t)(depth - 1), (int16_t)-beta, (int16_t)-alpha, (uint8_t)(ply + 1));
        unmake();
        if (abort_search) return 0;
        if (sc >= beta) return beta;
        if (sc > alpha) alpha = sc;
    }
    if (!any) return in_check(stm) ? (int16_t)(-30000 + ply) : 0;
    if (half >= 100) return 0;
    return alpha;
}
typedef struct { const char *name; uint8_t depth; uint16_t time_ms; uint8_t noise; uint16_t elo; uint16_t movetime; } level_t;
static const level_t LEVELS[6] = {
    { "Beginner",     1,   500, 90, 1320,  300 },
    { "Easy",         2,  1000, 55, 1500,  500 },
    { "Casual",       3,  2000, 30, 1800,  800 },
    { "Intermediate", 4,  5000, 12, 2200, 1200 },
    { "Strong",       5, 10000,  0, 2600, 2500 },
    { "Maximum",      6, 25000,  0,    0, 5000 },
};
static uint8_t level = 3;
static uint8_t think(move_t *out)                    /* the built-in engine; 1 with a move, 0 none (mate/stalemate), 2 aborted */
{
    uint8_t d, i, n, best = 0; int16_t sc, bestsc, alpha, beta;
    int16_t noise_v[MAXMV];
    n = count_legal(0);
    if (!n) return 0;
    for (i = 0; i < n; i++) noise_v[i] = LEVELS[level].noise ? (int16_t)(rnd() % (LEVELS[level].noise + 1)) - LEVELS[level].noise / 2 : 0;
    deadline = ms() + LEVELS[level].time_ms; nodes = 0; abort_search = 0;
    for (d = 1; d <= LEVELS[level].depth; d++) {
        uint8_t dbest = 0; bestsc = -32000; alpha = -32000; beta = 32000;
        for (i = 0; i < n; i++) {
            make(&ml[0][i]);
            sc = (int16_t)-search((uint8_t)(d - 1), (int16_t)-beta, (int16_t)-alpha, 1);
            unmake();
            if (abort_search) break;
            sc += noise_v[i];
            if (sc > bestsc) { bestsc = sc; dbest = i; }
            if (sc > alpha) alpha = sc;
        }
        if (abort_search) { if (d == 1) best = dbest; break; }
        best = dbest;
        if (bestsc > 29000 || bestsc < -29000) break;
    }
    *out = ml[0][best];
    return abort_search == 2 ? 2 : 1;
}

/* ---- SAN, and the game's bookkeeping ---------------------------------------- */
static uint8_t game_over, flipped, two_player, human_side, hint_from, hint_to, last_from, last_to;
static const char *result_text = "";
static void san_of(const move_t *m, char *out)      /* before make: the move's SAN, without +/# */
{
    uint8_t p = bd[m->from], k = KIND(p), i, n, amb = 0, samef = 0, samer = 0;
    char *o = out;
    if (m->flags & MF_CASTLE) { const char *c = FILE_OF(m->to) == 6 ? "O-O" : "O-O-O"; while (*c) *o++ = *c++; *o = 0; return; }
    if (k != PAWN) {
        *o++ = "  NBRQK"[k];
        n = count_legal(MAXPLY - 1);
        for (i = 0; i < n; i++) {
            const move_t *x = &ml[MAXPLY - 1][i];
            if (x->to == m->to && x->from != m->from && KIND(bd[x->from]) == k) { amb = 1; if (FILE_OF(x->from) == FILE_OF(m->from)) samef = 1; if (RANK_OF(x->from) == RANK_OF(m->from)) samer = 1; }
        }
        if (amb) { if (!samef) *o++ = (char)('a' + FILE_OF(m->from)); else if (!samer) *o++ = (char)('1' + RANK_OF(m->from)); else { *o++ = (char)('a' + FILE_OF(m->from)); *o++ = (char)('1' + RANK_OF(m->from)); } }
    } else if (m->flags & MF_CAP) *o++ = (char)('a' + FILE_OF(m->from));
    if (m->flags & MF_CAP) *o++ = 'x';
    *o++ = (char)('a' + FILE_OF(m->to)); *o++ = (char)('1' + RANK_OF(m->to));
    if (m->promo) { *o++ = '='; *o++ = "  NBRQ"[m->promo]; }
    *o = 0;
}
static void play(const move_t *m)                    /* make a move on the game, with its SAN and the check mark */
{
    char *s = san[nhist]; uint8_t n;
    san_of(m, s);
    make(m);
    last_from = m->from; last_to = m->to;
    n = count_legal(MAXPLY - 1);
    if (in_check(stm)) { while (*s) s++; *s++ = n ? '+' : '#'; *s = 0; }
    hint_from = hint_to = 0xFF;
}
static uint8_t insufficient(void)
{
    uint8_t s, minors = 0, other = 0;
    for (s = 0; s < 128; s++) { if (OFFBOARD(s)) { s += 7; continue; } if (!bd[s] || KIND(bd[s]) == KING) continue; if (KIND(bd[s]) == KNIGHT || KIND(bd[s]) == BISHOP) minors++; else other++; }
    return !other && minors <= 1;
}
static void judge(void)                              /* after a move: mate, stalemate, the draws */
{
    uint8_t n = count_legal(MAXPLY - 1);
    if (!n) { game_over = 1; result_text = in_check(stm) ? (stm == WHITE ? "Checkmate . Black wins" : "Checkmate . White wins") : "Stalemate . draw"; return; }
    if (half >= 100) { game_over = 1; result_text = "Fifty moves . draw"; return; }
    if (nhist >= MAXHIST - MAXPLY - 2) { game_over = 1; result_text = "Move limit . draw"; return; }   /* hist[] has no other bound; san[] sits right after it */
    if (insufficient()) { game_over = 1; result_text = "Dead position . draw"; }
}

/* ---- the UCI engines: the Tube (program 5) or a TCP server on N: ---------- */
static uint8_t eng_kind;                             /* 0 built-in, 1 Tube, 2 network */
static uint8_t eng_up;
static char eng_name[24] = "built-in";
static unsigned char nbuf[256]; static uint8_t nlen, npos;
static char line[128];
static char eng_url[64];
static unsigned char net(unsigned char cmd) { REG(NET) = cmd; return REG(NET + 1); }
static void eng_send(const char *s)
{
    if (eng_kind == 1) { while (*s) REG(TUBE + 2) = (uint8_t)*s++; }
    else { uint16_t n = 0; const char *q = s; while (*q++) n++; w32(NET + 8, (uint16_t)s); w32(NET + 12, n); net(3); }
}
static int16_t eng_byte(void)                        /* -1 none yet */
{
    if (eng_kind == 1) return (REG(TUBE) & 0x80) ? (int16_t)REG(TUBE + 1) : -1;
    if (npos < nlen) return nbuf[npos++];
    w32(NET + 8, (uint16_t)nbuf); w32(NET + 12, sizeof nbuf - 1); net(2);   /* 255: a 256-byte read came back as 0 and lost a byte */
    nlen = (uint8_t)(REG(NET + 12) | (REG(NET + 13) << 8) ? (REG(NET + 12) | (REG(NET + 13) << 8)) : 0); npos = 0;
    if (REG(NET + 12) == 0 && REG(NET + 13) == 1) nlen = 255;   /* a full buffer read as 256 */
    return npos < nlen ? nbuf[npos++] : -1;
}
static uint8_t eng_line(uint16_t timeout_ms)         /* one line into line[]; 0 on timeout */
{
    uint32_t until = ms() + timeout_ms; uint8_t n = 0; int16_t c;
    for (;;) {
        c = eng_byte();
        if (c < 0) { if (ms() >= until) return 0; wait_vblank(); continue; }
        if (c == '\n') { line[n] = 0; return 1; }
        if (c != '\r' && n < sizeof line - 1) line[n++] = (char)c;
    }
}
static uint8_t starts(const char *a, const char *b) { while (*b) if (*a++ != *b++) return 0; return 1; }
static void eng_stop(void)
{
    if (!eng_up) return;
    eng_send("quit\n");
    if (eng_kind == 1) REG(TUBE + 3) = 2; else net(4);
    eng_up = 0;
}
static uint8_t eng_start(void)                       /* 1 up, 0 could not */
{
    uint8_t tries;
    eng_up = 0; nlen = npos = 0;
    if (eng_kind == 1) {
        if (!(REG(TUBE) & 8)) return 0;
        REG(TUBE + 3) = 5;
        for (tries = 60; tries && !(REG(TUBE) & 1); tries--) wait_vblank();
        if (!(REG(TUBE) & 1)) return 0;
    } else if (eng_kind == 2) {
        if (!eng_url[0]) return 0;
        REG(NET + 2) = 1; w32(NET + 4, (uint16_t)eng_url);
        if (net(1)) return 0;
    } else return 0;
    eng_up = 1;
    eng_send("uci\n");
    for (;;) { if (!eng_line(20000)) { eng_stop(); return 0; } if (starts(line, "id name")) { uint8_t i = 0; const char *q = line + 8; while (*q && i < sizeof eng_name - 1) eng_name[i++] = *q++; eng_name[i] = 0; } if (starts(line, "uciok")) break; }
    eng_send("setoption name Hash value 16\n");
    return 1;
}
static void eng_level(void)
{
    char b[48]; uint16_t e = LEVELS[level].elo; uint8_t i = 0;
    if (!eng_up) return;
    eng_send(e ? "setoption name UCI_LimitStrength value true\n" : "setoption name UCI_LimitStrength value false\n");
    if (e) { const char *p = "setoption name UCI_Elo value "; while (*p) b[i++] = *p++; b[i++] = (char)('0' + e / 1000); b[i++] = (char)('0' + (e / 100) % 10); b[i++] = (char)('0' + (e / 10) % 10); b[i++] = (char)('0' + e % 10); b[i++] = '\n'; b[i] = 0; eng_send(b); }
}
static void send_position(void)
{
    uint16_t i; char b[8];
    eng_send("position startpos");
    if (nhist) eng_send(" moves");
    for (i = 0; i < nhist; i++) {
        const move_t *m = &hist[i].m; uint8_t n = 0;
        b[n++] = ' '; b[n++] = (char)('a' + FILE_OF(m->from)); b[n++] = (char)('1' + RANK_OF(m->from)); b[n++] = (char)('a' + FILE_OF(m->to)); b[n++] = (char)('1' + RANK_OF(m->to));
        if (m->promo) b[n++] = "  nbrq"[m->promo];
        b[n] = 0; eng_send(b);
    }
    eng_send("\n");
}
static uint8_t eng_move(move_t *out)                 /* 1 a move, 0 the engine failed, 2 aborted by Esc */
{
    char b[32]; uint8_t i = 0, n, k; uint16_t t = LEVELS[level].movetime; uint8_t aborted = 0;
    const char *p = "go movetime ";
    send_position();
    eng_send("isready\n");
    for (;;) { if (!eng_line(10000)) return 0; if (starts(line, "readyok")) break; }
    while (*p) b[i++] = *p++;
    b[i++] = (char)('0' + t / 1000); b[i++] = (char)('0' + (t / 100) % 10); b[i++] = (char)('0' + (t / 10) % 10); b[i++] = (char)('0' + t % 10); b[i++] = '\n'; b[i] = 0;
    eng_send(b);
    { uint32_t until = ms() + 60000UL;              /* its own clock: `deadline` belongs to think() and was 0, so the engine was dropped after 30 s of uptime */
    for (;;) {
        if (!eng_line(1000)) { if (!aborted && REG(KBDBREAK)) { eng_send("stop\n"); aborted = 1; } if (ms() > until) return 0; continue; }
        if (starts(line, "bestmove")) break;
    } }
    if (aborted) return 2;
    if (line[9] < 'a' || line[9] > 'h') return 0;
    n = count_legal(0);
    for (k = 0; k < n; k++) {
        const move_t *m = &ml[0][k];
        if (FILE_OF(m->from) == line[9] - 'a' && RANK_OF(m->from) == line[10] - '1' && FILE_OF(m->to) == line[11] - 'a' && RANK_OF(m->to) == line[12] - '1') {
            if (m->promo && "  nbrq"[m->promo] != line[13]) continue;
            *out = *m; return 1;
        }
    }
    return 0;
}
static void load_engine_cfg(void)                    /* /APPS/CHESS/ENGINE.CFG: tcp://host:port on its first line */
{
    static char name[] = "/APPS/CHESS/ENGINE.CFG"; uint8_t i, n;
    w32(0xD304u, (uint16_t)name); w32(0xD308u, (uint16_t)line); w32(0xD30Cu, sizeof line - 1);
    REG(0xD300u) = 9;
    if (REG(0xD301u)) { eng_url[0] = 0; return; }
    n = REG(0xD30Cu);
    for (i = 0; i < n && i < sizeof eng_url - 1 && line[i] != '\n' && line[i] != '\r'; i++) eng_url[i] = line[i];
    eng_url[i] = 0;
}

/* ---- drawing: the bitmap, the text layer, the sprites ---------------------- */
/* ---- the screen: the Personality Chooser's look (Doc, 2026-10-09: "The vibe
 * is very 8bit ... redo the Chess game, using the same style") ---------------
 * 320x240, doubled: a grey band above and below with black text, the blue
 * glass between them, the machine's yellow, white and light blue on it, the
 * banner's five bars, unscii at 8x8, 8x16 and 8x8 doubled -- the font the
 * Chooser draws with.  All of it is drawn into the bitmap: a glyph is built
 * in gbuf and blitted, so text lands on any pixel, as the Chooser's does.
 * The colours are the machine's first sixteen (a .PAL changes them, as it
 * changes the Chooser), never written here. */
#define SW 320
#define BAND 12                                       /* a band: 8-pixel text with 2 above and below */
#define GY BAND                                       /* the glass: lines 12..227 */
#define BBOT (240 - BAND)
#define SQPX 22
#define BX 8
#define BY 32
#define SOFF 5                                        /* a piece's sprite: 5 px up and left of its square */
#define PX 196                                        /* the panel's text, 15 columns */
#define PCOLS 15
#define C_BLACK 0
#define C_WHITE 1
#define C_REDB 2
#define C_GREEN 5
#define C_BLUE 6
#define C_YELLOW 7
#define C_ORANGE 8
#define C_LRED 10
#define C_DGREY 11
#define C_GREY 12
#define C_LBLUE 14
#define C_LGREY 15
#define C_LIGHT C_LGREY                               /* the squares */
#define C_DARK C_GREY
#define F8 0                                          /* text(): the font */
#define F16 1
#define F8X2 2
#define FONT8P  0x00010000UL                          /* unscii-8 and -16, where the frontend puts them */
#define FONT16P 0x00010800UL
#pragma bss-name (push, "BSS2")
static uint8_t f8[96 * 8], f16[96 * 16];              /* ' '..DEL, copied near once */
#pragma bss-name (pop)
static uint8_t gbuf[256];
static void rect(uint16_t x, uint8_t y, uint16_t w, uint8_t h, uint8_t c)
{
    uint32_t p = BMP + (uint32_t)y * SW + x;
    while (h--) { dma_fill(c, p, w); p += SW; }
}
static void blit(uint16_t x, uint8_t y, uint8_t w, uint8_t h)   /* gbuf, w x h, to the screen */
{
    w32(VICKY + 0x70, (uint32_t)(uint16_t)gbuf); w32(VICKY + 0x74, BMP + (uint32_t)y * SW + x);
    w16(VICKY + 0x78, w); w16(VICKY + 0x7A, h); w16(VICKY + 0x7C, w); w16(VICKY + 0x7E, SW);
    REG(VICKY + 0x80) = 0; REG(VICKY + 0x81) = 0; REG(VICKY + 0x82) = 1;
}
static void fonts_near(void)
{
    uint16_t i;
    for (i = 0; i < sizeof f8; i++) f8[i] = far_peek(FONT8P + 256 + i);
    for (i = 0; i < sizeof f16; i++) f16[i] = far_peek(FONT16P + 512 + i);
}
static uint16_t text(uint16_t x, uint8_t y, const char *s, uint8_t fg, uint8_t bg, uint8_t font)   /* the x after it */
{
    uint8_t c, r, b, bits, *g, h = font == F8 ? 8 : 16, w = font == F8X2 ? 16 : 8;
    const uint8_t *src;
    for (; *s; s++, x += w) {
        c = (uint8_t)*s; if (c < 32 || c > 127) c = '?';
        if (font == F16) src = f16 + (c - 32) * 16; else src = f8 + (c - 32) * 8;
        g = gbuf;
        for (r = 0; r < h; r++) {
            bits = font == F8X2 ? src[r >> 1] : src[r];
            if (font == F8X2) { for (b = 0x80; b; b >>= 1) { *g++ = (bits & b) ? fg : bg; *g++ = (bits & b) ? fg : bg; } }
            else for (b = 0x80; b; b >>= 1) *g++ = (bits & b) ? fg : bg;
        }
        blit(x, y, w, h);
    }
    return x;
}
static uint8_t slen(const char *s) { uint8_t n = 0; while (*s++) n++; return n; }
static void text_fit(uint16_t x, uint8_t y, const char *s, uint8_t n, uint8_t fg, uint8_t bg, uint8_t font)   /* at most n characters */
{
    char b[41]; uint8_t i;
    for (i = 0; i < n && i < 40 && s[i]; i++) b[i] = s[i];
    b[i] = 0; text(x, y, b, fg, bg, font);
}
/* the coordinates: a 3x5 font of their own, small in a corner of the square */
static const uint8_t TINY[16][5] = {
    { 2, 6, 2, 2, 7 }, { 6, 1, 2, 4, 7 }, { 6, 1, 2, 1, 6 }, { 5, 5, 7, 1, 1 },   /* 1-4 */
    { 7, 4, 6, 1, 6 }, { 3, 4, 6, 5, 2 }, { 7, 1, 2, 2, 2 }, { 2, 5, 2, 5, 2 },   /* 5-8 */
    { 0, 3, 5, 5, 3 }, { 4, 6, 5, 5, 6 }, { 0, 3, 4, 4, 3 }, { 1, 3, 5, 5, 3 },   /* a-d */
    { 0, 2, 7, 4, 3 }, { 1, 2, 7, 2, 2 }, { 0, 3, 5, 3, 6 }, { 4, 6, 5, 5, 5 },   /* e-h */
};
static void tiny(uint16_t x, uint8_t y, uint8_t i, uint8_t fg, uint8_t bg)
{
    uint8_t r, b, *g = gbuf;
    for (r = 0; r < 5; r++) for (b = 4; b; b >>= 1) *g++ = (TINY[i][r] & b) ? fg : bg;
    blit(x, y, 3, 5);
}
static void band_text(uint8_t y, const char *s)        /* centred in a band, black on grey */
{
    uint8_t n = slen(s); if (n > 39) n = 39;
    rect(0, y, SW, BAND, C_LGREY);
    text_fit((uint16_t)((SW - n * 8) / 2), (uint8_t)(y + 2), s, n, C_BLACK, C_LGREY, F8);
}
static const char KEYS[] = "F1 Game F2 Undo F3 Hint F4 Flip F5 Opts";   /* each "Fn Word " is 64 px: a click's button */
#define KEYS_X 4
static void draw_keys(void) { band_text(BBOT, KEYS); }
static void message(const char *s) { band_text(BBOT, s); }
static uint8_t clock_shown = 0xFF, rtc_read;
static void rtc_latch(void) { rtc_read = REG(SYS + 4); }   /* a store, not a (void) read: cc65 dropped that, and the RTC was never latched */
static void put2(char *b, uint8_t v) { b[0] = (char)('0' + v / 10); b[1] = (char)('0' + v % 10); }
static void draw_top(void)                            /* CHESS on the left, the time on the right, as the Chooser's band */
{
    char b[17]; uint16_t y;
    rtc_latch();
    clock_shown = REG(SYS + 6);
    put2(b, REG(SYS + 7)); b[2] = ':'; put2(b + 3, REG(SYS + 6)); b[5] = ' ';
    put2(b + 6, REG(SYS + 8)); b[8] = '.'; put2(b + 9, REG(SYS + 9)); b[11] = '.';
    y = REG(SYS + 0x0A) | (REG(SYS + 0x0B) << 8);
    b[12] = (char)('0' + (y / 1000) % 10); b[13] = (char)('0' + (y / 100) % 10); b[14] = (char)('0' + (y / 10) % 10); b[15] = (char)('0' + y % 10); b[16] = 0;
    rect(0, 0, SW, BAND, C_LGREY);
    text(8, 2, "K4510 CHESS", C_BLACK, C_LGREY, F8);
    text(SW - 8 - 16 * 8, 2, b, C_BLACK, C_LGREY, F8);
}
static void tick_top(void) { rtc_latch(); if (REG(SYS + 6) != clock_shown) draw_top(); }
static void bars(uint16_t x, uint8_t y, uint8_t h, uint8_t w)   /* the banner's five bars */
{
    static const uint8_t col[5] = { C_REDB, C_ORANGE, C_YELLOW, C_GREEN, C_LBLUE };
    static const uint8_t len[5] = { 8, 6, 4, 6, 8 };
    uint8_t i;
    for (i = 0; i < 5; i++) rect(x, (uint8_t)(y + i * h), (uint16_t)(len[i] * w), h, col[i]);
}

static uint8_t view_sq(uint8_t f, uint8_t r, uint8_t *vx, uint8_t *vy)   /* board square -> view column/row (0..7, top-left origin) */
{ *vx = flipped ? (uint8_t)(7 - f) : f; *vy = flipped ? r : (uint8_t)(7 - r); return 1; }
static uint8_t pieceset;
static uint32_t piece_img(uint8_t img) { return SPRD + CH_SET0 + (uint32_t)pieceset * CH_SET_BYTES + (uint32_t)img * CH_SPR_BYTES; }
static void spr(uint8_t i, int16_t x, int16_t y, uint32_t d, uint8_t palofs, uint8_t on)
{
    uint32_t t = SPRTAB + (uint32_t)i * 16;
    far_poke16(t, (uint16_t)x); far_poke16(t + 2, (uint16_t)y);
    far_poke16(t + 4, (uint16_t)d); far_poke16(t + 6, (uint16_t)(d >> 16));
    far_poke(t + 8, on ? 1 : 0);                      /* 4 bpp, after layer 0 */
    far_poke(t + 9, 2 | (2 << 2));                    /* 32 x 32 */
    far_poke(t + 10, palofs);
}
static void spr_at(uint8_t i, uint8_t sq, uint32_t d, uint8_t palofs, uint8_t on)
{
    uint8_t vx, vy; view_sq(FILE_OF(sq), RANK_OF(sq), &vx, &vy);
    spr(i, (int16_t)(BX + vx * SQPX - SOFF), (int16_t)(BY + vy * SQPX - SOFF), d, palofs, on);
}
#define MARK(m) (SPRD + (uint32_t)(m) * CH_SPR_BYTES)
#define S_PIECE 0                                     /* 64: one per square */
#define S_SEL 64
#define S_LAST 65
#define S_LAST2 66
#define S_CHECK 67
#define S_CURSOR 68
#define S_HINT 69
#define S_HINT2 70
#define S_DOT 72                                      /* up to 28 target dots (a queen has 27) */
#define S_CAP 100                                     /* 28: the two trays of captured pieces, 14 each */
#define NDOT 28
#define NCAP 14
#define CAP_SHOWN 12                                  /* what the panel's width holds */
static uint8_t cursor_sq = SQ(4, 1), selected = 0xFF, coords_on = 1;
static uint8_t targets[NDOT], ntargets, captured_on = 1;
static void draw_pieces(void)
{
    uint8_t s, i = 0;
    for (s = 0; s < 128; s++) {
        if (OFFBOARD(s)) { s += 7; continue; }
        if (bd[s]) spr_at((uint8_t)(S_PIECE + i), s, piece_img((uint8_t)(KIND(bd[s]) - 1 + (COLOUR(bd[s]) ? 6 : 0))), COLOUR(bd[s]) ? 2 : 1, 1);
        else far_poke(SPRTAB + (uint32_t)(S_PIECE + i) * 16 + 8, 0);
        i++;
    }
}
static void draw_marks(void)
{
    uint8_t i;
    if (selected != 0xFF) spr_at(S_SEL, selected, MARK(CH_MARK_FRAME), 3, 1); else far_poke(SPRTAB + S_SEL * 16 + 8, 0);
    if (nhist) { spr_at(S_LAST, last_from, MARK(CH_MARK_THIN), 4, 1); spr_at(S_LAST2, last_to, MARK(CH_MARK_THIN), 4, 1); }
    else { far_poke(SPRTAB + S_LAST * 16 + 8, 0); far_poke(SPRTAB + S_LAST2 * 16 + 8, 0); }
    if (!game_over && in_check(stm)) spr_at(S_CHECK, ksq[stm], MARK(CH_MARK_FRAME), 5, 1); else far_poke(SPRTAB + S_CHECK * 16 + 8, 0);
    spr_at(S_CURSOR, cursor_sq, MARK(CH_MARK_FRAME), 6, 1);
    if (hint_from != 0xFF) { spr_at(S_HINT, hint_from, MARK(CH_MARK_FRAME), 7, 1); spr_at(S_HINT2, hint_to, MARK(CH_MARK_FRAME), 7, 1); }
    else { far_poke(SPRTAB + S_HINT * 16 + 8, 0); far_poke(SPRTAB + S_HINT2 * 16 + 8, 0); }
    for (i = 0; i < NDOT; i++) { if (i < ntargets) spr_at((uint8_t)(S_DOT + i), targets[i], MARK(CH_MARK_DOT), 3, 1); else far_poke(SPRTAB + (uint32_t)(S_DOT + i) * 16 + 8, 0); }
}
static void draw_board(void)
{
    uint8_t vx, vy, f, r, c;
    rect(BX - 2, BY - 2, 8 * SQPX + 4, 8 * SQPX + 4, C_LBLUE);
    for (vy = 0; vy < 8; vy++) for (vx = 0; vx < 8; vx++) rect(BX + vx * SQPX, (uint8_t)(BY + vy * SQPX), SQPX, SQPX, ((vx + vy) & 1) ? C_DARK : C_LIGHT);
    if (!coords_on) return;
    /* as KoboChess: ranks in the left squares' top-left, files in the bottom squares' bottom-right */
    for (r = 0; r < 8; r++) { view_sq(0, r, &vx, &vy); c = ((vx + vy) & 1) ? C_LGREY : C_DGREY;
        tiny(BX + vx * SQPX + 1, (uint8_t)(BY + vy * SQPX + 1), r, c, ((vx + vy) & 1) ? C_DARK : C_LIGHT); }
    for (f = 0; f < 8; f++) { view_sq(f, 0, &vx, &vy); c = ((vx + vy) & 1) ? C_LGREY : C_DGREY;
        tiny(BX + vx * SQPX + SQPX - 4, (uint8_t)(BY + vy * SQPX + SQPX - 6), (uint8_t)(8 + f), c, ((vx + vy) & 1) ? C_DARK : C_LIGHT); }
}
static void draw_panel_static(void)
{
    rect(0, GY, SW, BBOT - GY, C_BLUE);
    bars(PX, GY + 14, 3, 3);
    text(PX + 32, GY + 13, "Chess", C_WHITE, C_BLUE, F8X2);
}
#define MOVES_Y (GY + 94)
#define MOVE_ROWS 8
static void draw_moves(void)
{
    uint16_t i, first = 0, pairs = (nhist + 1) / 2; uint8_t y = MOVES_Y, k, hl; char b[24];
    rect(PX - 2, MOVES_Y - 1, SW - PX + 2, MOVE_ROWS * 9 + 1, C_BLUE);
    if (pairs > MOVE_ROWS) first = pairs - MOVE_ROWS;
    for (i = first; i < pairs; i++, y += 9) {
        uint16_t n = i + 1; const char *p;
        hl = i + 1 == pairs;                          /* the last move: the Chooser's bar */
        k = 0;
        if (n >= 100) b[k++] = (char)('0' + n / 100);
        b[k++] = n >= 10 ? (char)('0' + (n / 10) % 10) : ' '; b[k++] = (char)('0' + n % 10); b[k++] = ' ';
        { uint8_t col = (uint8_t)(k + 6);                   /* black's column: a white move of 5 and a space */
          p = san[i * 2]; while (*p) b[k++] = *p++;
          if (i * 2 + 1 < nhist) { do b[k++] = ' '; while (k < col); p = san[i * 2 + 1]; while (*p) b[k++] = *p++; } }
        while (k < PCOLS) b[k++] = ' ';
        b[k] = 0;
        if (hl) rect(PX - 2, y - 1, SW - PX - 2, 9, C_LBLUE);
        text_fit(PX, y, b, PCOLS, hl ? C_BLUE : C_YELLOW, hl ? C_LBLUE : C_BLUE, F8);
    }
}
/* the captured pieces: what each side has taken, most valuable first, and
 * the material difference -- two trays under the move list, 12 px minis */
#define CAP_Y (MOVES_Y + MOVE_ROWS * 9 + 4)
static void draw_captured(void)
{
    uint8_t taken[2][16], n[2] = { 0, 0 }, s, i, j, k; int16_t diff = 0; uint16_t h;
    rect(PX - 2, CAP_Y, SW - PX + 2, 28, C_BLUE);
    for (i = 0; i < NCAP * 2; i++) far_poke(SPRTAB + (uint32_t)(S_CAP + i) * 16 + 8, 0);
    if (!captured_on) return;
    for (h = 0; h < nhist; h++) {
        uint8_t c = hist[h].cap, by; if (!c) continue;
        by = (uint8_t)(h & 1);                          /* white moved on even plies */
        if (n[by] < 16) taken[by][n[by]++] = KIND(c);
        diff += by == WHITE ? VAL[KIND(c)] : (int16_t)-VAL[KIND(c)];
    }
    for (s = 0; s < 2; s++) {
        for (i = 1; i < n[s]; i++) { k = taken[s][i]; for (j = i; j > 0 && VAL[taken[s][j - 1]] < VAL[k]; j--) taken[s][j] = taken[s][j - 1]; taken[s][j] = k; }
        for (i = 0; i < NCAP; i++) {
            uint32_t t = SPRTAB + (uint32_t)(S_CAP + s * NCAP + i) * 16;
            if (i >= n[s] || i >= CAP_SHOWN) continue;
            { uint32_t d = SPRD + CH_SET0 + (uint32_t)pieceset * CH_SET_BYTES + CH_MINI_OFF + ((uint32_t)(taken[s][i] - 1 + (s == WHITE ? 6 : 0))) * CH_MINI_BYTES;   /* white took black pieces */
              far_poke16(t, (uint16_t)(PX - 2 + i * 8)); far_poke16(t + 2, (uint16_t)(CAP_Y - 2 + s * 14));
              far_poke16(t + 4, (uint16_t)d); far_poke16(t + 6, (uint16_t)(d >> 16));
              far_poke(t + 8, 1); far_poke(t + 9, 1 | (1 << 2)); far_poke(t + 10, s == WHITE ? 2 : 1); }
        }
        if ((s == WHITE && diff > 0) || (s == BLACK && diff < 0)) {
            char b[6]; uint16_t v = (uint16_t)((diff < 0 ? -diff : diff) / 100); uint8_t q = 0;
            b[q++] = '+'; if (v >= 10) b[q++] = (char)('0' + v / 10); b[q++] = (char)('0' + v % 10); b[q] = 0;
            text(SW - 4 - q * 8, (uint8_t)(CAP_Y + 2 + s * 14), b, C_WHITE, C_BLUE, F8);
        }
    }
}
static uint32_t clock_ms[2]; static uint8_t clock_min; static uint32_t clock_last;
#define CLOCK_Y (BBOT - 12)
static void draw_clocks(void)
{
    uint8_t s; char b[8];
    rect(PX - 2, CLOCK_Y - 1, SW - PX + 2, 10, C_BLUE);
    if (!two_player || !clock_min) return;
    for (s = 0; s < 2; s++) {
        uint32_t t = clock_ms[s] / 1000; uint8_t on = s == stm && !game_over; uint16_t x = PX + s * 64;
        b[0] = s ? 'B' : 'W'; b[1] = ' '; put2(b + 2, (uint8_t)(t / 60)); b[4] = ':'; put2(b + 5, (uint8_t)(t % 60)); b[7] = 0;
        if (on) rect(x - 2, CLOCK_Y - 1, 60, 10, C_LBLUE);
        text(x, CLOCK_Y, b, on ? C_BLUE : C_YELLOW, on ? C_LBLUE : C_BLUE, F8);
    }
}
static void status_lines(const char *a, const char *b, uint8_t bcol)
{
    rect(PX - 2, GY + 34, SW - PX + 2, 34, C_BLUE);
    text_fit(PX, GY + 34, a, PCOLS, C_YELLOW, C_BLUE, F16);
    if (b) text_fit(PX, GY + 50, b, PCOLS, bcol, C_BLUE, F16);
}
static void draw_status(void)
{
    if (game_over) {                                  /* "Checkmate . White wins": a line each */
        char a[24]; const char *p = result_text; uint8_t k = 0;
        while (*p && !(p[0] == ' ' && p[1] == '.') && k < 23) a[k++] = *p++;
        a[k] = 0; if (*p) p += 3;
        status_lines(a, p, C_WHITE);
    } else status_lines(stm == WHITE ? "White to move" : "Black to move", in_check(stm) ? "Check!" : 0, C_LRED);
    draw_keys();
}
static void draw_engine_line(void)
{
    rect(PX - 2, GY + 70, SW - PX + 2, 18, C_BLUE);
    if (two_player) text_fit(PX, GY + 70, "Two players", PCOLS, C_LBLUE, C_BLUE, F8);
    else { text_fit(PX, GY + 70, eng_kind == 0 ? "Built-in" : eng_name, PCOLS, C_LBLUE, C_BLUE, F8);
           text_fit(PX, GY + 79, LEVELS[level].name, PCOLS, C_LBLUE, C_BLUE, F8); }
}
static void redraw(void) { draw_pieces(); draw_marks(); draw_status(); draw_moves(); draw_captured(); draw_clocks(); }
static void repaint(void)                             /* everything: after a menu has had the glass */
{
    draw_top(); draw_panel_static(); draw_board(); draw_engine_line(); redraw();
    REG(V_SPRCTL) = 1;
}

/* ---- the palette and the colour schemes ------------------------------------ */
typedef struct { uint8_t w[3][3], b[3][3]; } scheme_t;   /* ink, paper, edge for each side */
static const scheme_t SCHEMES[4] = {
    { { {0,0,0}, {255,255,255}, {110,110,110} }, { {0,0,0}, {225,225,225}, {110,110,110} } },      /* classic */
    { { {92,52,20}, {242,222,182}, {160,120,80} }, { {60,32,12}, {205,150,95}, {120,80,45} } },     /* wood */
    { { {150,20,20}, {255,225,225}, {200,110,110} }, { {20,40,150}, {205,215,255}, {110,130,200} } }, /* red and blue */
    { { {20,110,40}, {225,255,230}, {110,190,130} }, { {90,30,130}, {235,215,255}, {160,120,200} } }, /* green and purple */
};
static const char *const SCHEME_NAME[4] = { "Classic", "Wood", "Red and blue", "Green and purple" };
static uint8_t scheme;
/* The eleven tones of a side from its ink and paper (tools/mkchess.py's
 * classes): 1..5 paper to ink inside the piece; 6..8 ink at 75/50/25 %
 * over the board's mid grey (the light and dark squares averaged, so an
 * edge softens on both); 9..10 paper at 75/50 % the same way. */
static uint8_t mix(uint8_t a, uint8_t b, uint8_t pct) { return (uint8_t)(((uint16_t)a * pct + (uint16_t)b * (100 - pct)) / 100); }
static void bank_tones(uint8_t base, const uint8_t ink[3], const uint8_t paper[3])
{
    static const uint8_t SQ_MID[3] = { 153, 153, 153 };
    uint8_t c, t[3];
    static const uint8_t inside[5] = { 0, 25, 50, 75, 100 };            /* % ink */
    static const uint8_t edge_ink[3] = { 75, 50, 25 }, edge_paper[2] = { 75, 50 };
    uint8_t i;
    for (i = 0; i < 5; i++) { for (c = 0; c < 3; c++) t[c] = mix(ink[c], paper[c], inside[i]); pal((uint8_t)(base + 1 + i), t[0], t[1], t[2]); }
    for (i = 0; i < 3; i++) { for (c = 0; c < 3; c++) t[c] = mix(ink[c], SQ_MID[c], edge_ink[i]); pal((uint8_t)(base + 6 + i), t[0], t[1], t[2]); }
    for (i = 0; i < 2; i++) { for (c = 0; c < 3; c++) t[c] = mix(paper[c], SQ_MID[c], edge_paper[i]); pal((uint8_t)(base + 9 + i), t[0], t[1], t[2]); }
}
static void set_scheme(void)
{
    const scheme_t *s = &SCHEMES[scheme];
    bank_tones(16, s->w[0], s->w[1]);
    bank_tones(32, s->b[0], s->b[1]);
}
static void make_palette(void)                        /* the sprites' banks only: 0-15 are the machine's */
{
    pal(49, 255, 255, 85); pal(50, 255, 255, 85); pal(51, 255, 255, 85);       /* bank 3: selection frame, target dot -- yellow */
    pal(65, 0, 136, 255); pal(66, 0, 136, 255); pal(67, 0, 136, 255);          /* bank 4: the last move, thin */
    pal(81, 230, 40, 40); pal(82, 230, 40, 40); pal(83, 230, 40, 40);          /* bank 5: check */
    pal(97, 0, 0, 170); pal(98, 0, 0, 170); pal(99, 0, 0, 170);                /* bank 6: the cursor, the glass's blue */
    pal(113, 0, 204, 85); pal(114, 0, 204, 85); pal(115, 0, 204, 85);          /* bank 7: the hint */
    set_scheme();
}

/* ---- input: keys, the pad, the mouse ------------------------------------------ */
static uint8_t mouse_btn_last, held_last; static uint8_t held_rep, key_arrow;
static int16_t mouse_x(void) { return (int16_t)(REG(MOUSEX) | (REG(MOUSEX + 1) << 8)) >> 1; }    /* the glass is 640x480: halved to the board's */
static int16_t mouse_y(void) { return (int16_t)(REG(MOUSEX + 2) | (REG(MOUSEX + 3) << 8)) >> 1; }
#define EV_NONE 0
#define EV_UP 1
#define EV_DOWN 2
#define EV_LEFT 3
#define EV_RIGHT 4
#define EV_SELECT 5
#define EV_BACK 6
#define EV_F1 7                                        /* ..EV_F5 = 11 */
#define EV_CLICK 12                                    /* on a square: ev_sq */
#define EV_BTN 13                                      /* a key in the bottom band: ev_sq = 0..4 */
#define EV_DIGIT 14                                    /* 1-9: ev_sq = 0..8 */
static uint8_t ev_sq;
static uint8_t get_event(void)
{
    uint8_t k = key_get(), h = keys_held(), edge, mb; int16_t mx, my;
    if (k == KEY_ESC) return EV_BACK;
    if (k == KEY_ENTER) return EV_SELECT;
    if (k >= 0x80 && k <= 0x83) { key_arrow = 3; held_last = h; return (uint8_t)(EV_UP + k - 0x80); }   /* the queue's arrows; $D104 will show the same press, so its edge is swallowed */
    if (k >= KEY_F1 && k <= KEY_F1 + 4) return (uint8_t)(EV_F1 + k - KEY_F1);
    if (k >= '1' && k <= '9') { ev_sq = (uint8_t)(k - '1'); return EV_DIGIT; }
    if (k == 'u' || k == 'U') return EV_F1 + 1; if (k == 'h' || k == 'H') return EV_F1 + 2; if (k == 'f' || k == 'F') return EV_F1 + 3;
    /* the held register: keyboard arrows and space, and the pad, one source; edges, then a slow repeat */
    edge = (uint8_t)(h & ~held_last);
    if (h == held_last && h) { if (++held_rep > 18) { held_rep = 15; edge = h & 15; } } else held_rep = 0;
    held_last = h;
    if (key_arrow) { key_arrow--; edge &= (uint8_t)~15; }
    if (edge & HELD_UP) return EV_UP; if (edge & HELD_DOWN) return EV_DOWN; if (edge & HELD_LEFT) return EV_LEFT; if (edge & HELD_RIGHT) return EV_RIGHT;
    if (edge & (HELD_FIRE | HELD_A)) return EV_SELECT; if (edge & HELD_B) return EV_BACK;
    mb = REG(MOUSEX + 4);
    if ((mb & 1) && !(mouse_btn_last & 1)) {
        mouse_btn_last = mb; mx = mouse_x(); my = mouse_y();
        if (mx >= BX && mx < BX + 8 * SQPX && my >= BY && my < BY + 8 * SQPX) {
            uint8_t vx = (uint8_t)((mx - BX) / SQPX), vy = (uint8_t)((my - BY) / SQPX);
            ev_sq = flipped ? SQ(7 - vx, vy) : SQ(vx, 7 - vy); return EV_CLICK;
        }
        if (my >= BBOT && mx >= KEYS_X && mx < KEYS_X + 5 * 64) { ev_sq = (uint8_t)((mx - KEYS_X) / 64); return EV_BTN; }
        return EV_NONE;
    }
    if ((mb & 2) && !(mouse_btn_last & 2)) { mouse_btn_last = mb; return EV_BACK; }
    mouse_btn_last = mb;
    return EV_NONE;
}

/* ---- menus: the Chooser's list, over the whole glass --------------------------- */
#define MENU_Y (GY + 44)
#define MENU_H 20
static void menu_row(uint8_t i, const char *s, uint8_t on)
{
    uint8_t y = (uint8_t)(MENU_Y + i * MENU_H); char d[2];
    rect(12, y, SW - 24, MENU_H - 2, on ? C_LBLUE : C_BLUE);
    rect(18, y + 3, 12, 12, on ? C_BLUE : C_LBLUE);   /* the number box */
    d[0] = (char)('1' + i); d[1] = 0; text(20, y + 5, d, on ? C_LBLUE : C_BLUE, on ? C_BLUE : C_LBLUE, F8);
    text_fit(40, y + 1, s, 33, on ? C_BLUE : C_YELLOW, on ? C_LBLUE : C_BLUE, F16);
}
static uint8_t menu(const char *title, const char *const *items, uint8_t n, uint8_t cur)   /* index, or 255 */
{
    uint8_t i, e, r = 255;
    REG(V_SPRCTL) = 0;                                /* the pieces would sit over the list */
    rect(0, GY, SW, BBOT - GY, C_BLUE);
    bars(12, GY + 8, 5, 6);
    text(72, GY + 8, "Chess", C_WHITE, C_BLUE, F8X2);
    text(72, GY + 28, title, C_YELLOW, C_BLUE, F8);
    for (i = 0; i < n; i++) menu_row(i, items[i], i == cur);
    band_text(BBOT, n > 1 ? "1-9 or cursor keys, RETURN picks" : "RETURN picks, ESC backs out");
    for (;;) {
        e = get_event();
        if (e == EV_UP || e == EV_DOWN) {
            menu_row(cur, items[cur], 0);
            cur = (uint8_t)(e == EV_UP ? (cur + n - 1) % n : (cur + 1) % n);
            menu_row(cur, items[cur], 1);
        }
        else if (e == EV_SELECT) { r = cur; break; }
        else if (e == EV_DIGIT && ev_sq < n) { r = ev_sq; break; }
        else if (e == EV_BACK) break;
        else if (e == EV_NONE && (REG(MOUSEX + 4) & 1)) {                /* a click on a row */
            int16_t my = mouse_y();
            if (my >= MENU_Y && my < MENU_Y + n * MENU_H) { while (REG(MOUSEX + 4) & 1) wait_vblank(); mouse_btn_last = 0; r = (uint8_t)((my - MENU_Y) / MENU_H); break; }
        }
        tick_top();
        wait_vblank();
    }
    repaint();
    return r;
}

/* ---- the game ----------------------------------------------------------------- */
static void snd(uint8_t ch, uint8_t now, int8_t vol, uint8_t pitch, uint8_t dur)
{ REG(SEQ) = (uint8_t)((now ? 0x10 : 0) | ch); REG(SEQ + 1) = (uint8_t)vol; REG(SEQ + 2) = pitch; REG(SEQ + 3) = dur; }
static void snd_move(uint8_t cap) { if (cap) { snd(0, 1, -9, 20, 1); } else snd(0, 1, -6, 60, 1); }
static void snd_end(void) { snd(3, 1, -8, 53, 3); snd(3, 0, -8, 69, 3); snd(3, 0, -8, 85, 6); }

static void new_game(void)
{
    set_start(); game_over = 0; selected = 0xFF; ntargets = 0; hint_from = hint_to = 0xFF; last_from = last_to = 0;
    clock_ms[0] = clock_ms[1] = (uint32_t)clock_min * 60000UL; clock_last = ms();
    cursor_sq = human_side == BLACK ? SQ(4, 6) : SQ(4, 1);
    draw_board(); redraw(); draw_engine_line();
}
static void select_sq(uint8_t s)
{
    uint8_t n, i;
    selected = 0xFF; ntargets = 0;
    if (!bd[s] || COLOUR(bd[s]) != stm) return;
    n = count_legal(0);
    for (i = 0; i < n; i++) if (ml[0][i].from == s) { uint8_t j; for (j = 0; j < ntargets; j++) if (targets[j] == ml[0][i].to) break; if (j == ntargets && ntargets < NDOT) targets[ntargets++] = ml[0][i].to; }
    if (ntargets) selected = s;
}
static uint8_t promote_menu(void)
{
    static const char *const P[4] = { "Queen", "Rook", "Bishop", "Knight" };
    uint8_t r = menu("Promote to", P, 4, 0);
    return r == 255 ? QUEEN : (uint8_t)(QUEEN - r);
}
static uint8_t try_move(uint8_t from, uint8_t to)   /* the person's move, if legal */
{
    uint8_t n = count_legal(0), i; move_t m; uint8_t found = 0;
    for (i = 0; i < n; i++) if (ml[0][i].from == from && ml[0][i].to == to) { m = ml[0][i]; found = 1; if (!m.promo) break; }
    if (!found) return 0;
    if (m.promo) { uint8_t p = promote_menu(); for (i = 0; i < n; i++) if (ml[0][i].from == from && ml[0][i].to == to && ml[0][i].promo == p) m = ml[0][i]; }
    snd_move(m.flags & MF_CAP);
    play(&m); judge();
    if (game_over) snd_end();
    selected = 0xFF; ntargets = 0;
    return 1;
}
static void engine_turn(void)
{
    move_t m; uint8_t r;
    if (game_over || two_player || stm == human_side) return;
    message("Thinking...  (Esc interrupts)");
    draw_marks();
    if (eng_kind && !eng_up) { if (!eng_start()) { eng_kind = 0; message("engine not answering: built-in plays"); { uint8_t f = 90; while (f--) wait_vblank(); } } else eng_level(); }
    if (eng_kind && eng_up) {
        r = eng_move(&m);
        if (r == 0) { eng_stop(); eng_kind = 0; draw_engine_line(); r = think(&m); }
    } else { deadline = 0; r = think(&m); }
    if (r != 1) { if (r == 0) judge(); redraw(); return; }   /* 2 = Esc while the engine thought: m is not a move */
    snd_move(m.flags & MF_CAP);
    play(&m); judge();
    if (game_over) snd_end();
    cursor_sq = m.to;
    redraw();
}
static void hint(void)
{
    move_t m; uint8_t r, save = level;
    if (game_over || (!two_player && stm != human_side)) return;
    message("Looking for a move...");
    if (eng_kind && !eng_up) { if (eng_start()) eng_level(); else eng_kind = 0; }
    if (eng_kind && eng_up) { r = eng_move(&m); if (!r) { eng_stop(); eng_kind = 0; } }
    if (!eng_kind || !eng_up) { level = 3; r = think(&m); level = save; }
    if (r == 1) { hint_from = m.from; hint_to = m.to; cursor_sq = m.to; }
    redraw();
}
static void undo(void)
{
    if (!nhist) return;
    unmake();
    if (!two_player && nhist && stm != human_side) unmake();
    game_over = 0; selected = 0xFF; ntargets = 0; hint_from = hint_to = 0xFF;
    if (nhist) { last_from = hist[nhist - 1].m.from; last_to = hist[nhist - 1].m.to; }
    redraw();
}
static void export_pgn(void)
{
    char *o = PGNBUF; uint16_t i; uint8_t k; const char *p; char name[40];
    static const char *const res[] = { "*", "1-0", "0-1", "1/2-1/2" };
    uint8_t rr = 0;
    if (game_over) { if (starts(result_text, "Checkmate . W")) rr = 1; else if (starts(result_text, "Checkmate . B")) rr = 2; else if (starts(result_text, "White resigns")) rr = 2; else if (starts(result_text, "Black resigns")) rr = 1; else rr = 3; }
    rtc_latch();                                          /* before reading the date */
    p = "[Event \"K4510 chess\"]\n[Site \"K4510\"]\n[Date \""; while (*p) *o++ = *p++;
    { unsigned long y = REG(SYS + 0x0A) | ((unsigned long)REG(SYS + 0x0B) << 8);
      *o++ = (char)('0' + (y / 1000) % 10); *o++ = (char)('0' + (y / 100) % 10); *o++ = (char)('0' + (y / 10) % 10); *o++ = (char)('0' + y % 10); *o++ = '.';
      *o++ = (char)('0' + REG(SYS + 9) / 10); *o++ = (char)('0' + REG(SYS + 9) % 10); *o++ = '.'; *o++ = (char)('0' + REG(SYS + 8) / 10); *o++ = (char)('0' + REG(SYS + 8) % 10); }
    p = "\"]\n[White \""; while (*p) *o++ = *p++;
    if (two_player) p = "Player"; else if (human_side == WHITE) p = "Doc"; else if (eng_kind) p = eng_name; else p = "K4510 built-in";
    while (*p) *o++ = *p++;
    p = "\"]\n[Black \""; while (*p) *o++ = *p++;
    if (two_player) p = "Player"; else if (human_side == BLACK) p = "Doc"; else if (eng_kind) p = eng_name; else p = "K4510 built-in";
    while (*p) *o++ = *p++;
    p = "\"]\n[Result \""; while (*p) *o++ = *p++; p = res[rr]; while (*p) *o++ = *p++; p = "\"]\n\n"; while (*p) *o++ = *p++;
    for (i = 0; i < nhist && (uint16_t)(o - PGNBUF) < PGNMAX - 40; i++) {
        if ((i & 1) == 0) { uint16_t n = i / 2 + 1; if (n >= 100) *o++ = (char)('0' + n / 100); if (n >= 10) *o++ = (char)('0' + (n / 10) % 10); *o++ = (char)('0' + n % 10); *o++ = '.'; *o++ = ' '; }
        p = san[i]; while (*p) *o++ = *p++; *o++ = (i % 12 == 11) ? '\n' : ' ';
    }
    p = res[rr]; while (*p) *o++ = *p++; *o++ = '\n';
    k = 0; p = "/HOME/GAME-"; while (*p) name[k++] = *p++;
    { unsigned long y = REG(SYS + 0x0A) | ((unsigned long)REG(SYS + 0x0B) << 8);
      name[k++] = (char)('0' + (y / 1000) % 10); name[k++] = (char)('0' + (y / 100) % 10); name[k++] = (char)('0' + (y / 10) % 10); name[k++] = (char)('0' + y % 10);
      name[k++] = (char)('0' + REG(SYS + 9) / 10); name[k++] = (char)('0' + REG(SYS + 9) % 10); name[k++] = (char)('0' + REG(SYS + 8) / 10); name[k++] = (char)('0' + REG(SYS + 8) % 10); name[k++] = '-';
      name[k++] = (char)('0' + REG(SYS + 7) / 10); name[k++] = (char)('0' + REG(SYS + 7) % 10); name[k++] = (char)('0' + REG(SYS + 6) / 10); name[k++] = (char)('0' + REG(SYS + 6) % 10); }
    p = ".PGN"; while (*p) name[k++] = *p++; name[k] = 0;
    zp16(0xF0, (uint16_t)name); zp32(0xF2, (uint32_t)(uint16_t)PGNBUF); zp32(0xF6, (uint32_t)(uint16_t)(o - PGNBUF));
    if (rom_save()) message("could not save: MKDIR /HOME first?"); else { char b[60]; uint8_t j = 0; p = "saved "; while (*p) b[j++] = *p++; p = name; while (*p) b[j++] = *p++; b[j] = 0; message(b); }
    { uint8_t f = 120; while (f--) wait_vblank(); }
}
static void game_menu(void)
{
    static const char *const G[6] = { "New game, play White", "New game, play Black", "New game, two players", "Resign", "Export PGN", "Back" };
    uint8_t r = menu("Game", G, 6, 0);
    if (r == 0) { two_player = 0; human_side = WHITE; flipped = 0; new_game(); }
    else if (r == 1) { two_player = 0; human_side = BLACK; flipped = 1; new_game(); }
    else if (r == 2) { two_player = 1; human_side = WHITE; flipped = 0; new_game(); }
    else if (r == 3 && !game_over) { game_over = 1; result_text = stm == WHITE ? "White resigns . Black wins" : "Black resigns . White wins"; snd_end(); }
    else if (r == 4) export_pgn();
    redraw(); draw_engine_line();
}
static void options_menu(void)
{
    static const char *const O[8] = { "Piece set", "Piece colours", "Engine", "Level", "Clock (two players)", "Coordinates", "Captured pieces", "Back" };
    static const char *const E[3] = { "Built-in", "Stockfish on the Tube", "Network engine (ENGINE.CFG)" };
    static const char *const L[6] = { "Beginner", "Easy", "Casual", "Intermediate", "Strong", "Maximum" };
    static const char *const C[4] = { "Off", "5 minutes", "10 minutes", "15 minutes" };
    static const char *const Y[2] = { "Shown", "Hidden" };
    uint8_t r, s;
    for (;;) {
        r = menu("Options", O, 8, 0);
        if (r == 0) { s = menu("Piece set", CH_SET_NAME, CH_NSETS, pieceset); if (s != 255) pieceset = s; }
        else if (r == 1) { s = menu("Piece colours", SCHEME_NAME, 4, scheme); if (s != 255) { scheme = s; set_scheme(); } }
        else if (r == 2) {
            s = menu("Engine", E, 3, eng_kind);
            if (s != 255 && s != eng_kind) {
                eng_stop(); eng_kind = s;
                if (s == 1 && !(REG(TUBE) & 8)) { message("no UCI engine on this Tube"); eng_kind = 0; { uint8_t f = 90; while (f--) wait_vblank(); } }
                if (s == 2 && !eng_url[0]) { message("/APPS/CHESS/ENGINE.CFG: tcp://host:port"); eng_kind = 0; { uint8_t f = 120; while (f--) wait_vblank(); } }
                if (eng_kind) { message("starting the engine..."); if (eng_start()) eng_level(); else { message("the engine did not answer"); eng_kind = 0; { uint8_t f = 90; while (f--) wait_vblank(); } } }
            }
        }
        else if (r == 3) { s = menu("Level", L, 6, level); if (s != 255) { level = s; eng_level(); } }
        else if (r == 4) { s = menu("Clock", C, 4, clock_min == 0 ? 0 : clock_min / 5); if (s != 255) { clock_min = (uint8_t)(s * 5); clock_ms[0] = clock_ms[1] = (uint32_t)clock_min * 60000UL; } }
        else if (r == 5) { s = menu("Coordinates", Y, 2, coords_on ? 0 : 1); if (s != 255) { coords_on = !s; draw_board(); } }
        else if (r == 6) { s = menu("Captured pieces", Y, 2, captured_on ? 0 : 1); if (s != 255) captured_on = !s; }
        else break;
        redraw(); draw_engine_line();
    }
    redraw(); draw_engine_line();
}
static void tick_clock(void)
{
    uint32_t now = ms(), d = now - clock_last; clock_last = now;
    if (!two_player || !clock_min || game_over || !nhist) return;
    if (clock_ms[stm] <= d) { clock_ms[stm] = 0; game_over = 1; result_text = stm == WHITE ? "Time out . Black wins" : "Time out . White wins"; snd_end(); redraw(); return; }
    clock_ms[stm] -= d;
    if ((now / 500) != ((now - d) / 500)) draw_clocks();
}
static uint8_t ctrl_was;
static void setup(void)
{
    uint8_t i;
    ctrl_was = REG(V_CTRL);
    REG(V_CTRL) = 0; REG(V_BGCOL) = C_BLACK;           /* a pixel of colour 0 is the ground: black */
    make_palette(); fonts_near();
    dma_fill(C_BLUE, BMP, (uint32_t)SW * 240);
    { uint16_t L = V_LAYER(0); REG(L + 1) = 0; w16(L + 2, 0); w16(L + 4, 0); w16(L + 6, SW); w32(L + 8, BMP); w32(L + 12, BMP); REG(L) = 1 | (0 << 1) | (3 << 3); }
    REG(V_LAYER(1)) = 0;
    dma_fill(0, SPRTAB, 4096);
    for (i = 0; i < 128; i++) far_poke(SPRTAB + (uint32_t)i * 16 + 9, 2 | (2 << 2));
    w32(V_SPRTAB, SPRTAB); REG(V_SPRCTL) = 1;
    REG(V_CTRL) = 1 | 2 | 4;                            /* 320 x 240, doubled: the Chooser's chunky pixels */
    draw_top(); draw_panel_static(); draw_keys();
    load_engine_cfg();
}
static void first_engine(void)                        /* after the board is up: an engine may take seconds to answer */
{
    if (REG(TUBE) & 8) eng_kind = 1; else if (eng_url[0]) eng_kind = 2; else eng_kind = 0;
    if (!eng_kind) return;
    message("starting the engine...");
    if (eng_start()) eng_level(); else { eng_kind = 0; message("no engine answered: built-in plays"); { uint8_t f = 90; while (f--) wait_vblank(); } }
    draw_status(); draw_engine_line();
}

/* ---- the port: chess driven from a script -------------------------------
 * `CHESS @file` reads command lines from that file, does them, and writes the
 * answer beside it (.CMD -> .RPL): the result code on the first line, the text
 * after it.  No screen, no keyboard -- this is the mode RX's ADDRESS CHESS
 * uses through SWAP, and the position is kept in /APPS/CHESS/PORT.GAM so the next
 * call carries on where this one stopped (a swapped-in program starts fresh:
 * the file IS the memory).  Commands, one per line, case does not matter:
 *   NEW [WHITE|BLACK]   MOVE e2e4   GO   LEVEL 0-5   BOARD   FEN   STATUS
 * Anything else answers "?" and sets the result code to 1. */
#define PORTCMD  ((char *)0x0800)               /* the command file, whole */
#define PORTRPL  ((char *)0x1400)               /* the reply, built here */
#define PORTMAX  0x0B00u
static unsigned char rom_load(void) { return ((unsigned char (*)(void))0xFF89)(); }
static char port_name[40];
static char *po;                                 /* where the reply is being built */
static void po_str(const char *p) { while (*p && (uint16_t)(po - PORTRPL) < PORTMAX - 2) *po++ = *p++; }
static void po_nl(void) { *po++ = '\n'; }
static void po_sq(uint8_t sq) { *po++ = (char)('a' + FILE_OF(sq)); *po++ = (char)('1' + RANK_OF(sq)); }
static uint8_t port_state(uint8_t save)          /* the position, to and from /APPS/CHESS/PORT.GAM */
{
    static char nm[] = "/APPS/CHESS/PORT.GAM";
    uint8_t *b = (uint8_t *)PORTRPL + PORTMAX;   /* above the reply: 140 bytes of state */
    uint8_t i;
    if (save) {
        for (i = 0; i < 128; i++) b[i] = bd[i];
        b[128] = stm; b[129] = castle; b[130] = half; b[131] = (uint8_t)ep;
        b[132] = ksq[0]; b[133] = ksq[1]; b[134] = game_over; b[135] = level; b[136] = 0x4B;
        zp16(0xF0, (uint16_t)nm); zp32(0xF2, (uint32_t)(uint16_t)b); zp32(0xF6, 137);
        return rom_save() == 0;
    }
    zp16(0xF0, (uint16_t)nm); zp32(0xF2, (uint32_t)(uint16_t)b);
    if (rom_load() || b[136] != 0x4B) return 0;
    for (i = 0; i < 128; i++) bd[i] = b[i];
    stm = b[128]; castle = b[129]; half = b[130]; ep = (int8_t)b[131];
    ksq[0] = b[132]; ksq[1] = b[133]; game_over = b[134]; level = b[135];
    nhist = 0;
    return 1;
}
static const char PGLYPH[] = ".PNBRQK";
static char port_glyph(uint8_t p)                /* FEN's letter for a piece: black is lower case */
{
    char g;
    if (p == EMPTY) return '.';
    g = PGLYPH[KIND(p)];
    if (COLOUR(p) == BLACK) g = (char)(g + 32);
    return g;
}
static void port_board(void)
{
    int8_t r; uint8_t f;
    for (r = 7; r >= 0; r--) {
        for (f = 0; f < 8; f++) *po++ = port_glyph(bd[SQ(f, (uint8_t)r)]);
        po_nl();
    }
}
static void port_fen(void)
{
    int8_t r; uint8_t f, p, gap;
    for (r = 7; r >= 0; r--) {
        gap = 0;
        for (f = 0; f < 8; f++) {
            p = bd[SQ(f, (uint8_t)r)];
            if (p == EMPTY) { gap++; continue; }
            if (gap) { *po++ = (char)('0' + gap); gap = 0; }
            *po++ = port_glyph(p);
        }
        if (gap) *po++ = (char)('0' + gap);
        if (r) *po++ = '/';
    }
    *po++ = ' '; *po++ = stm == WHITE ? 'w' : 'b'; *po++ = ' ';
    if (!castle) *po++ = '-';
    else { if (castle & 1) *po++ = 'K'; if (castle & 2) *po++ = 'Q'; if (castle & 4) *po++ = 'k'; if (castle & 8) *po++ = 'q'; }
    *po++ = ' ';
    if (ep < 0) *po++ = '-'; else po_sq((uint8_t)ep);
    po_nl();
}
static uint8_t port_move(char *a)                /* e2e4 or e7e8q; 1 played */
{
    uint8_t n = count_legal(0), i;
    for (i = 0; a[i]; i++) if (a[i] >= 'A' && a[i] <= 'Z') a[i] = (char)(a[i] + 32);   /* the word arrived upper-cased */
    if (a[0] < 'a' || a[0] > 'h' || a[1] < '1' || a[1] > '8') return 0;
    for (i = 0; i < n; i++) {
        const move_t *m = &ml[0][i];
        if (FILE_OF(m->from) != a[0] - 'a' || RANK_OF(m->from) != a[1] - '1') continue;
        if (FILE_OF(m->to) != a[2] - 'a' || RANK_OF(m->to) != a[3] - '1') continue;
        if (m->promo && a[4] && "  nbrq"[m->promo] != (a[4] | 32)) continue;
        if (m->promo && !a[4] && m->promo != QUEEN) continue;
        { move_t mv; mv = *m; play(&mv); judge(); }   /* cc65: a struct is assigned, not initialised */
        return 1;
    }
    return 0;
}
static uint8_t port_go(void)                     /* the engine moves; 1 played */
{
    move_t m; uint8_t r = 0;
    if (game_over) return 0;
    if (REG(TUBE) & 8) {                          /* Stockfish on the Tube, when this machine has one */
        eng_kind = 1;
        if (eng_start()) { eng_level(); r = eng_move(&m); if (r == 2) r = 0; }
        eng_stop(); eng_kind = 0;
    }
    if (!r) { deadline = 0; r = think(&m); }
    if (r != 1) return 0;
    play(&m); judge();
    po_str("move "); po_sq(m.from); po_sq(m.to);
    if (m.promo) *po++ = "  nbrq"[m.promo];
    *po++ = ' '; po_str(san[nhist - 1]); po_nl();
    return 1;
}
static uint8_t port_word(const char **p, char *w, uint8_t n)   /* a word, upper-cased */
{
    uint8_t i = 0; char c;
    /* Written the long way on purpose: the compact form, with the character
     * declared inside the loop body, compiled to something that stored the
     * buffer size instead of the character (cc65 2.19, 2026-09-07). */
    while (**p == ' ' || **p == '\t') (*p)++;
    for (;;) {
        c = **p;
        if (!c || c == ' ' || c == '\t' || c == '\r' || c == '\n' || i >= (uint8_t)(n - 1)) break;
        (*p)++;
        if (c >= 'a' && c <= 'z') c = (char)(c - 32);
        w[i++] = c;
    }
    w[i] = 0;
    return i;
}
static uint8_t port_line(const char *p)          /* one command; the result code */
{
    char w[12]; char a[8]; uint8_t rc = 0;
    if (!port_word(&p, w, 12)) return 0;
    if (!strcmp(w, "NEW") || !strcmp(w, "RESET")) {
        port_word(&p, w, 12);
        set_start(); game_over = 0; nhist = 0;
        human_side = (w[0] == 'B') ? BLACK : WHITE; two_player = 0;
        po_str("new game\n");
    } else if (!strcmp(w, "MOVE")) {
        port_word(&p, a, 8);
        if (port_move(a)) { po_str("ok "); po_str(san[nhist - 1]); po_nl(); }
        else { po_str("illegal move\n"); rc = 1; }
    } else if (!strcmp(w, "GO")) {
        if (!port_go()) { po_str("no move\n"); rc = 1; }
    } else if (!strcmp(w, "LEVEL")) {
        port_word(&p, a, 8);
        if (a[0] >= '0' && a[0] <= '5') { level = (uint8_t)(a[0] - '0'); po_str(LEVELS[level].name); po_nl(); }
        else { po_str("level 0 to 5\n"); rc = 1; }
    } else if (!strcmp(w, "BOARD")) port_board();
    else if (!strcmp(w, "FEN")) port_fen();
    else if (!strcmp(w, "STATUS")) {
        if (game_over) po_str(result_text);
        else po_str(stm == WHITE ? "white to move" : "black to move");
        po_nl();
    } else { po_str("? "); po_str(w); po_nl(); rc = 1; }
    return rc;
}
static void port_run(const char *file)
{
    const char *p; char *e; uint16_t n; uint8_t rc = 0, i;
    for (i = 0; i < 39 && file[i] && file[i] != ' '; i++) port_name[i] = file[i];
    port_name[i] = 0;
    zp16(0xF0, (uint16_t)port_name); zp32(0xF2, (uint32_t)(uint16_t)PORTCMD);
    if (rom_load()) return;                       /* no command file: nothing to answer */
    n = (uint16_t)((uint32_t)REG(0xF6) | ((uint32_t)REG(0xF7) << 8));
    if (n > 0x0B00u) n = 0x0B00u;
    PORTCMD[n] = 0;
    if (!port_state(0)) { set_start(); game_over = 0; human_side = WHITE; two_player = 0; }
    eng_kind = 0; eng_up = 0; two_player = 0;
    po = PORTRPL + 6;
    for (p = PORTCMD; *p; ) {
        char lbuf[80]; uint8_t k = 0;
        while (*p && *p != '\n' && k < 79) { if (*p != '\r') lbuf[k++] = *p; p++; }
        lbuf[k] = 0;
        while (*p == '\n' || *p == '\r') p++;
        if ((uint16_t)(po - PORTRPL) > PORTMAX - 160) {   /* the reply is all but full: the biggest answer (BOARD, FEN) is
                                                          * under 100 bytes, and only po_str stopped at the end before --
                                                          * forty BOARDs in one .CMD wrote past it (review 2026-09-12) */
            po_str("reply full: the rest not done\n"); rc = 1; break;
        }
        if (port_line(lbuf)) rc = 1;
    }
    port_state(1);
    while ((uint16_t)(po - PORTRPL) && po[-1] == '\n') po--;      /* one value: no trailing newline */
    po_nl();
    { char *q = PORTRPL; for (i = 0; i < 5; i++) q[i] = ' '; q[0] = (char)('0' + rc); q[5] = '\n'; }
    e = port_name; while (*e) e++;
    if (e - port_name > 4 && e[-4] == '.') { e[-3] = 'R'; e[-2] = 'P'; e[-1] = 'L'; }
    else { *e++ = '.'; *e++ = 'R'; *e++ = 'P'; *e++ = 'L'; *e = 0; }
    zp16(0xF0, (uint16_t)port_name); zp32(0xF2, (uint32_t)(uint16_t)PORTRPL); zp32(0xF6, (uint32_t)(uint16_t)(po - PORTRPL));
    rom_save();
}
static unsigned char rom_args(void) { return ((unsigned char (*)(void))0xFF95)(); }
void main(void)
{
    uint8_t e;
    { uint8_t na = rom_args(); const char *a = *(const char **)0xF0;
      while (na && *a == ' ') { a++; na--; }
      if (na && *a == '@') { port_run(a + 1); return; } }      /* CHESS @file: the port, no screen */
    setup();
    two_player = 0; human_side = WHITE; flipped = 0;
    new_game();
    first_engine();
    for (;;) {
        engine_turn();
        e = get_event();
        if (e == EV_BACK) {
            if (selected != 0xFF) { selected = 0xFF; ntargets = 0; draw_marks(); }
            else if (hint_from != 0xFF) { hint_from = hint_to = 0xFF; draw_marks(); }
            else break;
        }
        else if (e == EV_UP || e == EV_DOWN || e == EV_LEFT || e == EV_RIGHT) {
            int8_t df = (e == EV_LEFT) ? -1 : (e == EV_RIGHT) ? 1 : 0, dr = (e == EV_UP) ? 1 : (e == EV_DOWN) ? -1 : 0;
            int8_t f, r;
            if (flipped) { df = (int8_t)-df; dr = (int8_t)-dr; }
            f = (int8_t)(FILE_OF(cursor_sq) + df); r = (int8_t)(RANK_OF(cursor_sq) + dr);
            if (f >= 0 && f < 8 && r >= 0 && r < 8) { cursor_sq = SQ(f, r); draw_marks(); }
        }
        else if (e == EV_SELECT || e == EV_CLICK) {
            uint8_t s = (e == EV_CLICK) ? ev_sq : cursor_sq;
            cursor_sq = s;
            if (game_over || (!two_player && stm != human_side)) { draw_marks(); continue; }
            if (selected != 0xFF && s != selected && try_move(selected, s)) { redraw(); continue; }
            select_sq(s); draw_marks();
        }
        else if (e == EV_F1 || (e == EV_BTN && ev_sq == 0)) game_menu();
        else if (e == EV_F1 + 1 || (e == EV_BTN && ev_sq == 1)) undo();
        else if (e == EV_F1 + 2 || (e == EV_BTN && ev_sq == 2)) hint();
        else if (e == EV_F1 + 3 || (e == EV_BTN && ev_sq == 3)) { flipped = !flipped; draw_board(); redraw(); }
        else if (e == EV_F1 + 4 || (e == EV_BTN && ev_sq == 4)) options_menu();
        tick_clock(); tick_top();
        wait_vblank();
    }
    eng_stop();
    REG(SEQ) = 0x80;
    REG(V_SPRCTL) = 0; REG(V_LAYER(0)) = 0; REG(V_LAYER(1)) = 0;
    REG(V_CTRL) = ctrl_was;
}
