/* demo/dosed.h -- the editing window EDIT and PROG share (2026-10-02): the
 * text in a frame, its scroll bars, the selection, the editing keys, the
 * mouse inside it, and the Find / Change / Go To Line dialogs.  The text
 * engine under it is ed.h's (VI's), so the three editors cannot drift; the
 * furniture around it is dosui.h's.
 *
 * The window is rows wy .. wy+th-1, framed left (column 0), its vertical
 * scroll bar in the last column, its horizontal one on row wy+th.  The
 * program sets wy and th, and draws whatever is outside.
 * #include "k4510.h", "ed.h" and "dosui.h" first. */

static uint8_t wy = 2, th, tw, hoff, lasthoff = 0xFF, over, wantx;
static unsigned lasttop = 0xFFFF, lastcy = 0xFFFF, tgline = 0xFFFFu;

/* ---- the selection ---------------------------------------------------------
 * Characters, not lines: from the anchor (sely, selx) to the cursor, either
 * way round.  Shift with a moving key, a drag or a Shift-click makes one,
 * Ctrl+A takes the whole file; typing replaces it. */
static uint8_t selon, selshown;
static unsigned sely, qy1, qy2;
static uint8_t selx, qx1, qx2;
static void sel_order(void)
{
    if (sely < cy || (sely == cy && selx <= cx)) { qy1 = sely; qx1 = selx; qy2 = cy; qx2 = cx; }
    else { qy1 = cy; qx1 = cx; qy2 = sely; qx2 = selx; }
}

/* ---- drawing the window ----------------------------------------------------- */
static void text_row(uint8_t r)
{
    unsigned l = top + r, p; const uint8_t *s = ln; uint8_t c, w = 0, a = 255, b = 0;
    if (l < nlines) {
        if (l != cy) { far_get(SLOT(l), tmp, 256); s = tmp; }
        w = s[0];
        if (selon && l >= qy1 && l <= qy2) { a = l == qy1 ? qx1 : 0; b = l == qy2 ? qx2 : 255; }
    }
    cel(0, 0xB3, K_FRAME);
    for (c = 0; c < tw; c++) {
        p = (unsigned)hoff + c;
        cel((uint8_t)(c + 1), (uint8_t)(p < w ? s[1 + p] : ' '), (uint8_t)((p >= a && (p < b || b == 255)) ? K_SEL : K_TEXT));
    }
    flush((uint8_t)(wy + r), (uint8_t)(cols - 1));
}
static uint8_t vthumb(void) { return thumb(cy, nlines, vtrack(wy, (uint8_t)(wy + th - 1))); }
static void vbar(void) { vbar_at((uint8_t)(cols - 1), wy, (uint8_t)(wy + th - 1), vthumb()); }
static uint8_t hthumb(void) { return (uint8_t)((unsigned)cx * (cols - 6) / 255); }
static void hbar(void)                                /* the window's foot: └◄░░█░░►─┘ */
{
    uint8_t x, t = hthumb();
    cel(0, 0xC0, K_FRAME);
    cel(1, 0x11, K_SCROLL);
    for (x = 0; x < cols - 5; x++) cel((uint8_t)(2 + x), (uint8_t)(x == t ? 0xDB : 0xB0), K_SCROLL);
    cel((uint8_t)(cols - 3), 0x10, K_SCROLL);
    cel((uint8_t)(cols - 2), 0xC4, K_FRAME);
    cel((uint8_t)(cols - 1), 0xD9, K_FRAME);
    flush((uint8_t)(wy + th), cols);
}
static void where(char *p)                            /* "00012:034": the line and the column, as DOS EDIT */
{
    uint8_t i; unsigned long v = cy + 1;
    for (i = 0; i < 5; i++) { p[4 - i] = (char)('0' + v % 10); v /= 10; }
    p[5] = ':'; v = (unsigned long)cx + 1; for (i = 0; i < 3; i++) { p[8 - i] = (char)('0' + v % 10); v /= 10; }
    p[9] = 0;
}
/* The window's rows moved by d (2026-10-06): JIM shifts them itself -- a
 * scroll region and SU/SD, done in the host -- and the copy in far memory
 * follows; only the d rows that came in are drawn.  Redrawing every row
 * through the stream took PROG five frames a line on a long file. */
#ifdef DOSED_SCROLL_HI
#pragma code-name (push, "HICODE")                   /* EDIT: at $E000, its main image is full (PROG's $E000 is) */
#endif
static void scroll_rows(int d)
{
    uint8_t r, n = (uint8_t)(d < 0 ? -d : d);
    uint16_t w = (uint16_t)(cols * 4);
    jc_str("\x1b["); jc_num((uint8_t)(wy + 1)); jc_put(';'); jc_num((uint8_t)(wy + th)); jc_put('r');
    jc_str("\x1b["); jc_num(n); jc_put(d > 0 ? 'S' : 'T');
    jc_str("\x1b[r"); jc_x = 0xFF;                    /* the region back to the window; the cursor has moved */
    if (d > 0) for (r = 0; r < th - n; r++) dma_copy(rowaddr((uint8_t)(wy + r + n)), rowaddr((uint8_t)(wy + r)), w);
    else for (r = (uint8_t)(th - 1); r >= n; r--) dma_copy(rowaddr((uint8_t)(wy + r - n)), rowaddr((uint8_t)(wy + r)), w);
    if (d > 0) for (r = (uint8_t)(th - n); r < th; r++) text_row(r);
    else for (r = 0; r < n; r++) text_row(r);
}
#ifdef DOSED_SCROLL_HI
#pragma code-name (pop)
#endif
/* Scroll so the cursor shows; redraw what changed; put JIM's cursor on it.
 * Answers 1 when every row was drawn (the program redraws its own then). */
static uint8_t window(void)
{
    uint8_t r, all = 0;
    if (cy < top) top = cy;
    while (cy >= top + th) top++;
    if (cx < hoff) hoff = cx;
    if (cx >= (unsigned)hoff + tw) hoff = (uint8_t)(cx - tw + 1);
    if (!full && !selon && !selshown && hoff == lasthoff && lasttop != 0xFFFF && top != lasttop
        && (top > lasttop ? top - lasttop : lasttop - top) < th / 2) {   /* a few rows: JIM moves them */
        scroll_rows((int)top - (int)lasttop);
        if (lastcy != cy && lastcy >= top && lastcy < top + th) text_row((uint8_t)(lastcy - top));
        text_row((uint8_t)(cy - top));
        vbar(); hbar();
        lasttop = top; lastcy = cy;
        return 0;
    }
    if (top != lasttop || hoff != lasthoff) full = 1;
    if (selon || selshown) { full = 1; if (selon) sel_order(); }
    selshown = selon;
    if (full) { for (r = 0; r < th; r++) text_row(r); all = 1; }
    else {
        if (lastcy != cy && lastcy >= top && lastcy < top + th) text_row((uint8_t)(lastcy - top));
        text_row((uint8_t)(cy - top));
    }
    vbar(); hbar();
    lasttop = top; lastcy = cy; lasthoff = hoff;
    return all;
}
static void window_cursor(void)
{
    cursor_shape(over ? '2' : '4');                   /* a block overwriting, an underline inserting: EDIT's */
    cursor_at((uint8_t)(1 + cx - hoff), (uint8_t)(wy + cy - top));
}

/* ---- editing ------------------------------------------------------------- */
static void t_end(void) { if (tgline != 0xFFFFu) { u_end(); tgline = 0xFFFFu; } }
static void t_begin(void) { if (tgline != cy) { t_end(); u_begin(); u_line(cy); line_in(cy); tgline = cy; } }
static void go(unsigned n) { t_end(); goline(n); }
static void type_ch(uint8_t c)
{
    t_begin();
    if (over && cx < ln[0]) { ln[cx + 1] = c; cx++; dirty = 1; }
    else ins_ch(c);
    wantx = cx;
}
static void enter(void)                               /* split the line, keeping its indent */
{
    uint8_t ind = 0, i;
    t_end();
    while (ind < ln[0] && ln[ind + 1] == ' ') ind++;
    if (ind > cx) ind = cx;
    u_begin(); u_line(cy); line_in(cy); u_ins(cy + 1);
    split();
    for (i = 0; i < ind; i++) ins_ch(' ');
    u_end(); wantx = cx;
}
static void backspace(void)
{
    if (cx) { t_begin(); cx--; del_ch(); wantx = cx; return; }
    if (!cy) return;
    t_end(); line_out(cy);
    far_get(SLOT(cy - 1), tmp, 256);
    if ((unsigned)tmp[0] + ln[0] > 255) { note = "The lines would not fit on one"; return; }
    u_begin(); u_line(cy - 1); u_del(cy);
    join_prev();
    u_end(); wantx = cx;
}
static void delete_fwd(void)
{
    uint8_t i;
    if (cx < ln[0]) { t_begin(); del_ch(); return; }
    if (cy + 1 >= nlines) return;
    t_end(); line_out(cy);
    far_get(SLOT(cy + 1), tmp, 256);
    if ((unsigned)tmp[0] + ln[0] > 255) { note = "The lines would not fit on one"; return; }
    u_begin(); u_line(cy); u_del(cy + 1);
    far_get(SLOT(cy + 1), tmp, 256);
    for (i = 0; i < tmp[0]; i++) ln[ln[0] + 1 + i] = tmp[i + 1];
    ln[0] = (uint8_t)(ln[0] + tmp[0]);
    line_out(cy); close_at(cy + 1);
    u_end(); dirty = 1;
}
static uint8_t wordc(uint8_t c) { return (uint8_t)((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_'); }
static void word_left(void)
{
    if (!cx) { if (cy) { go(cy - 1); cx = ln[0]; } return; }
    while (cx && !wordc(ln[cx])) cx--;
    while (cx && wordc(ln[cx])) cx--;
}
static void word_right(void)
{
    if (cx >= ln[0]) { if (cy + 1 < nlines) { go(cy + 1); cx = 0; } return; }
    while (cx < ln[0] && wordc(ln[cx + 1])) cx++;
    while (cx < ln[0] && !wordc(ln[cx + 1])) cx++;
}
static void sel_clear(void) { if (selon) { selon = 0; full = 1; } }
static void sel_start(void) { if (!selon) { selon = 1; sely = cy; selx = cx; } }
static uint8_t sel_delete(void)
{
    unsigned i; uint8_t n, j;
    sel_order(); selon = 0; full = 1;
    t_end(); line_out(cy);
    far_get(SLOT(qy2), tmp, 256);
    if (qx2 > tmp[0]) qx2 = tmp[0];
    n = (uint8_t)(tmp[0] - qx2);
    if ((unsigned)qx1 + n > 255) { note = "The lines would not fit on one"; return 0; }
    u_begin(); u_line(qy1);
    for (i = qy2; i > qy1; i--) u_del(i);
    far_get(SLOT(qy2), tmp, 256);
    far_get(SLOT(qy1), ln, 256);
    for (j = 0; j < n; j++) ln[qx1 + 1 + j] = tmp[qx2 + 1 + j];
    ln[0] = (uint8_t)(qx1 + n);
    far_put(ln, SLOT(qy1), 256);
    for (i = qy2; i > qy1; i--) close_at(i);
    u_end();
    cy = qy1; cx = qx1; line_in(cy); dirty = 1; wantx = cx;
    return 1;
}
static uint8_t sel_copy(void)
{
    unsigned i, n; uint8_t a, b, j;
    sel_order(); line_out(cy);
    n = qy2 - qy1 + 1;
    if (n >= REGMAX) { note = "Too much to copy"; return 0; }
    for (i = 0; i < n; i++) {
        far_get(SLOT(qy1 + i), tmp, 256);
        a = i ? 0 : qx1; b = (qy1 + i == qy2) ? qx2 : tmp[0];
        if (b > tmp[0]) b = tmp[0];
        if (a > b) a = b;
        for (j = 0; j < (uint8_t)(b - a); j++) tmp[1 + j] = tmp[1 + a + j];
        tmp[0] = (uint8_t)(b - a);
        far_put(tmp, RSLOT(i), 256);
    }
    reglines = n; reglinewise = 0;
    return 1;
}
static void put_chars(void)
{
    unsigned i, n = reglines; uint8_t j, x;
    t_end(); line_out(cy);
    far_get(RSLOT(0), tmp, 256);
    if (n == 1) {
        if ((unsigned)ln[0] + tmp[0] > 255) { note = "The line would be too long"; return; }
        u_begin(); u_line(cy);
        far_get(RSLOT(0), tmp, 256);
        for (j = 0; j < tmp[0]; j++) ins_ch(tmp[1 + j]);
        line_out(cy); u_end(); wantx = cx; full = 1;
        return;
    }
    if ((unsigned)cx + tmp[0] > 255) { note = "The line would be too long"; return; }
    far_get(RSLOT(n - 1), tmp, 256);
    if ((unsigned)tmp[0] + (ln[0] - cx) > 255) { note = "The line would be too long"; return; }
    u_begin(); u_line(cy);
    tmp[0] = (uint8_t)(ln[0] - cx);
    for (j = 0; j < tmp[0]; j++) tmp[1 + j] = ln[cx + 1 + j];
    far_put(tmp, RSLOT(n), 256);
    far_get(RSLOT(0), tmp, 256);
    for (j = 0; j < tmp[0]; j++) ln[cx + 1 + j] = tmp[1 + j];
    ln[0] = (uint8_t)(cx + tmp[0]);
    line_out(cy);
    for (i = 1; i < n; i++) { u_ins(cy + i); open_at(cy + i); dma_copy(RSLOT(i), SLOT(cy + i), 256); }
    cy += n - 1; line_in(cy); x = ln[0];
    far_get(RSLOT(n), tmp, 256);
    for (j = 0; j < tmp[0]; j++) ln[x + 1 + j] = tmp[1 + j];
    ln[0] = (uint8_t)(x + tmp[0]);
    line_out(cy);
    u_end(); cx = x; wantx = cx; dirty = 1; full = 1;
}
static void indent(uint8_t out)
{
    unsigned y, ya = cy, yb = cy; uint8_t j, k, w = ed_tabw;
    if (selon) { sel_order(); ya = qy1; yb = qy2; if (yb > ya && !qx2) yb--; }
    t_end(); line_out(cy);
    u_begin();
    for (y = ya; y <= yb; y++) {
        u_line(y);
        if (out) {
            for (k = 0; k < w && k < tmp[0] && tmp[1 + k] == ' '; k++) ;
            if (!k) continue;
            for (j = 0; (uint8_t)(j + k) < tmp[0]; j++) tmp[1 + j] = tmp[1 + j + k];
            tmp[0] = (uint8_t)(tmp[0] - k);
        } else {
            if (!tmp[0] || (unsigned)tmp[0] + w > 255) continue;
            for (j = tmp[0]; j; j--) tmp[j + w] = tmp[j];
            for (j = 1; j <= w; j++) tmp[j] = ' ';
            tmp[0] = (uint8_t)(tmp[0] + w);
        }
        far_put(tmp, SLOT(y), 256);
    }
    u_end(); dirty = 1; full = 1;
    line_in(cy);
    if (selon) { sely = ya; selx = 0; goline(yb); cx = ln[0]; }
    else if (cx > ln[0]) cx = ln[0];
    wantx = cx;
}
static void cut_line(void)
{
    t_end(); line_out(cy);
    reg_take(cy, 1, 1);
    u_begin();
    if (nlines == 1) { u_line(0); ln[0] = 0; line_out(0); }
    else { u_del(cy); close_at(cy); if (cy >= nlines) cy = nlines - 1; }
    u_end();
    line_in(cy); cx = 0; dirty = 1; full = 1;
    note = "Line cut -- Ctrl+V puts it back";
}
static void select_all(void) { t_end(); sely = 0; selx = 0; go(nlines - 1); cx = ln[0]; selon = 1; full = 1; }
static void do_cut(void)
{
    if (!selon) { cut_line(); return; }
    if (sel_copy() && sel_delete()) note = "Cut -- Ctrl+V puts it back";
}
static void do_copy(void)
{
    if (selon) { if (sel_copy()) note = "Copied"; return; }
    t_end(); line_out(cy); reg_take(cy, 1, 1); note = "Line copied";
}
static void do_paste(void)
{
    if (selon && !sel_delete()) return;
    t_end(); if (reglinewise || !reglines) do_put(0); else put_chars();
}
static void do_clear(void) { if (selon) sel_delete(); else delete_fwd(); }

/* ---- the search dialogs ----------------------------------------------------- */
static char sbuf[NAMEMAX], cbuf[NAMEMAX], gbuf[8];
static void find_next(void) { t_end(); search(1); if (note[0] == 'n') note = "Match not found"; }
static void find_dlg(void)
{
    uint8_t i;
    for (i = 0; i < patlen; i++) sbuf[i] = pat[i];
    sbuf[i] = 0;
    if (!form1("Find", "Find What:", sbuf, NAMEMAX, "OK") || !sbuf[0]) return;
    for (patlen = 0; sbuf[patlen]; patlen++) pat[patlen] = sbuf[patlen];
    find_next();
}
static void change_dlg(void)
{
    unsigned l;
    dl_title = "Change"; dl_text = 0; dl_nf = 2;
    dl_lab[0] = "Find What:"; dl_buf[0] = sbuf; dl_max[0] = NAMEMAX;
    dl_lab[1] = "Change To:"; dl_buf[1] = cbuf; dl_max[1] = NAMEMAX;
    dl_btn[0] = "Change All"; dl_btn[1] = "Cancel"; dl_nb = 2;
    if (dialog() != 0 || !sbuf[0]) return;
    for (soldl = 0; sbuf[soldl]; soldl++) sold[soldl] = sbuf[soldl];
    for (snewl = 0; cbuf[snewl]; snewl++) snew[snewl] = cbuf[snewl];
    t_end(); subs = 0; line_out(cy); u_begin();
    for (l = 0; l < nlines; l++) sub_line(l, 1);
    u_end(); line_in(cy);
    if (cx > ln[0]) cx = ln[0];
    for (patlen = 0; sbuf[patlen]; patlen++) pat[patlen] = sbuf[patlen];
    full = 1;
    nb_reset(); nb_n(subs); nb_s(subs == 1 ? " change made (Ctrl+Z undoes it)" : " changes made (Ctrl+Z undoes them)"); note = nbuf;
}
static void goto_dlg(void)
{
    unsigned n = 0; uint8_t i;
    gbuf[0] = 0;
    if (!form1("Go To Line", "Line:", gbuf, sizeof gbuf, "OK")) return;
    for (i = 0; gbuf[i] >= '0' && gbuf[i] <= '9'; i++) n = n * 10 + (unsigned)(gbuf[i] - '0');
    if (n) { go(n - 1); cx = 0; wantx = 0; }
}

/* ---- the mouse in the window ------------------------------------------------ */
static uint8_t vdrag, hdrag;
static void place(uint8_t r, uint8_t c)
{
    go(top + (r - wy));
    cx = (uint8_t)(hoff + (c ? c - 1 : 0)); if (cx > ln[0]) cx = ln[0];
    wantx = cx;
}
static void vset(uint8_t r)                           /* the thumb dragged to row r of the bar */
{
    unsigned n = vtrack(wy, (uint8_t)(wy + th - 1)), y = r < wy + 1 ? 0 : r - wy - 1;
    if (y >= n) y = n - 1;
    go(n > 1 ? (unsigned)((unsigned long)y * (nlines - 1) / (n - 1)) : 0);
    if (cx > ln[0]) cx = ln[0];
}
static void hset(uint8_t c)
{
    unsigned n = cols - 5, x = c < 2 ? 0 : c - 2;
    if (x >= n) x = n - 1;
    cx = (uint8_t)((unsigned long)x * 255 / (n - 1)); if (cx > ln[0]) cx = ln[0];
    wantx = cx;
}
/* The mouse in the window: answers 1 if it was the window's (a drag, the
 * wheel, a click on the text or a scroll bar), 0 for the program to handle. */
static uint8_t window_mouse(void)
{
    uint8_t r = mrow, c = mcol, rmax = (uint8_t)(wy + th - 1), t; unsigned d;
    if (mev == 4) {                                   /* the wheel: three lines a notch */
        d = (unsigned)(mwheel < 0 ? -mwheel : mwheel) * 3;
        t_end();
        if (mwheel > 0) top = top > d ? top - d : 0;
        else { top += d; if (top + th > nlines) top = nlines > th ? nlines - th : 0; }
        if (cy < top) go(top); else if (cy >= top + th) go(top + th - 1);
        if (cx > ln[0]) cx = ln[0];
        full = 1; return 1;
    }
    if (mev == 3) { dragging = vdrag = hdrag = 0; return 1; }
    if (mev == 2) {
        if (vdrag) { vset(r); return 1; }
        if (hdrag) { hset(c); return 1; }
        if (!dragging) return 1;
        if (r == 0xFF || r < wy) go(top ? top - 1 : 0);
        else if (r > rmax) go(top + th);
        else place(r, c);
        selon = (uint8_t)(cy != sely || cx != selx);
        full = 1; return 1;
    }
    if (c == cols - 1 && r >= wy && r <= rmax) {        /* the vertical scroll bar */
        sel_clear(); t_end(); t = vthumb();
        if (r == wy) { if (cy) go(cy - 1); }
        else if (r == rmax) { if (cy + 1 < nlines) go(cy + 1); }
        else if (r - wy - 1 == t) vdrag = 1;
        else if (r - wy - 1 < t) go(cy > (unsigned)th ? cy - th : 0);
        else go(cy + th);
        if (cx > ln[0]) cx = ln[0];
        return 1;
    }
    if (r == wy + th && c >= 1 && c <= cols - 3) {       /* the horizontal one */
        sel_clear();
        if (c == 1) { if (cx) cx--; }
        else if (c == cols - 3) { if (cx < ln[0]) cx++; }
        else if (c - 2 == hthumb()) hdrag = 1;
        else hset(c);
        wantx = cx;
        return 1;
    }
    if (r >= wy && r <= rmax && c >= 1 && c < cols - 1) {
        t_end();
        if (kmod & 1) { sel_start(); place(r, c); }
        else { sel_clear(); place(r, c); sely = cy; selx = cx; }
        dragging = 1;
        return 1;
    }
    return 0;
}

/* ---- the keys that edit ------------------------------------------------------
 * Typing, Enter, Backspace, Tab, and the moving keys (with Shift: selecting).
 * Answers 1 if the key was one of them; the program has the rest. */
/* Options > Tab Width: how many spaces Tab puts (to the next stop).  The same
 * ed_tabw as VI's `set ts=N` and VI.RC; Doc, 2026-10-05: "an option to prog and
 * edit and word to change the number of spaces when I hit the tab key.  NEVER
 * use the Tab CHARACTER" -- so this only ever changes a number of spaces. */
static void tabw_dlg(void)
{
    char b[4]; uint8_t o = ed_tabw;
    if (o > 9) { b[0] = '1'; b[1] = (char)('0' + o - 10); b[2] = 0; } else { b[0] = (char)('0' + o); b[1] = 0; }
    if (form1("Tab Width", "Spaces per Tab (1-16):", b, 3, "OK")) {
        ed_tabw = 0; ed_set_tabw(b);                  /* 0 afterwards: not a number from 1 to 16 */
        if (!ed_tabw) { ed_tabw = o; note = "Tab width is 1 to 16 spaces"; }
        else { nb_reset(); nb_s("Tab stops every "); nb_n(ed_tabw); nb_s(" columns, in spaces"); note = nbuf; }
    }
    full = 1;
}
static uint8_t window_key(uint8_t k)
{
    uint8_t ctrl = (uint8_t)(kmod & 2), shift = (uint8_t)(kmod & 1);
    if (!kcode) {
        switch (k) {
        case 0x0D: if (selon && !sel_delete()) return 1; enter(); return 1;
        case 0x08: if (selon) { sel_delete(); return 1; } backspace(); return 1;
        case 0x09:
            if (shift) { indent(1); return 1; }
            if (selon) { sel_order(); if (qy2 > qy1) { indent(0); return 1; } if (!sel_delete()) return 1; }
            t_begin(); ed_tab(); wantx = cx; return 1;
        case 0x1B: sel_clear(); return 1;
        }
        if ((k >= 0x20 && k < 0x7F) || k >= 0x80) { if (selon && !sel_delete()) return 1; type_ch(k); return 1; }
        return 0;
    }
    if (k == KDEL && shift) { sel_clear(); do_cut(); return 1; }   /* DOS's own clipboard keys */
    if (k == KINS && ctrl) { do_copy(); return 1; }
    if (k == KINS && shift) { do_paste(); return 1; }
    if (k >= KUP && k <= KPGDN) {
        t_end();                                      /* a move ends a run of typing: Ctrl+Z takes back what was typed since */
        if (shift) sel_start(); else sel_clear();
    }
    switch (k) {
    case KLEFT:  if (ctrl) word_left(); else if (cx) cx--; else if (cy) { go(cy - 1); cx = ln[0]; } wantx = cx; return 1;
    case KRIGHT: if (ctrl) word_right(); else if (cx < ln[0]) cx++; else if (cy + 1 < nlines) { go(cy + 1); cx = 0; } wantx = cx; return 1;
    case KUP:    if (cy) { go(cy - 1); cx = wantx < ln[0] ? wantx : ln[0]; } return 1;
    case KDOWN:  if (cy + 1 < nlines) { go(cy + 1); cx = wantx < ln[0] ? wantx : ln[0]; } return 1;
    case KHOME:  if (ctrl) go(0); cx = 0; wantx = 0; return 1;
    case KEND:   if (ctrl) go(nlines - 1); cx = ln[0]; wantx = cx; return 1;
    case KPGUP:  go(cy > (unsigned)(th - 1) ? cy - (th - 1) : 0); cx = wantx < ln[0] ? wantx : ln[0]; return 1;
    case KPGDN:  go(cy + th - 1); cx = wantx < ln[0] ? wantx : ln[0]; return 1;
    case KINS:   over = (uint8_t)!over; return 1;
    case KDEL:   do_clear(); return 1;
    }
    return 0;
}
