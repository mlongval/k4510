/* The F7 menu. See menu.h. */
#include "menu.h"
#include "settings.h"
#include "ui_draw.h"
#include "../io.h"
#include <string.h>
#include <strings.h>
#include <stdio.h>

typedef enum { MI_SUBMENU, MI_SETTING, MI_ACTION, MI_INFO, MI_SEP, MI_SAVESLOT, MI_LOADSLOT } item_kind;
typedef struct menu_s menu_t;
typedef struct { const char *label; item_kind kind; int arg; const menu_t *sub; } item_t;
struct menu_s { const char *title; const item_t *items; int n; };

static const item_t video_items[] = {
    { "Border width",  MI_SETTING, SET_VIDEO_BORDER },
    { "Border colour", MI_SETTING, SET_VIDEO_BORDER_COLOUR },
    { "Screen font",   MI_SETTING, SET_VIDEO_FONT },
    { "Resolution",    MI_SETTING, SET_VIDEO_MODE },
    { "Scanlines",     MI_SETTING, SET_VIDEO_SCANLINES },
    { "Scaling",       MI_SETTING, SET_VIDEO_SMOOTH },
    { "Full screen",   MI_SETTING, SET_VIDEO_FULLSCREEN },
    { "Vertical sync", MI_SETTING, SET_VIDEO_VSYNC },
    { "Placement",     MI_SETTING, SET_VIDEO_PLACE },
    { "Side panel",    MI_SETTING, SET_VIDEO_PANEL },
};
/* One chip, the OPL2, so there is nothing to choose but the volume. */
static const item_t audio_items[] = { { "Volume", MI_SETTING, SET_AUDIO_VOLUME } };
/* The Terminal menu (2026-09-02).  The status bands are the console's own
 * furniture rather than a property of the picture, so they moved out of Video
 * and brought the things that belong with them: how tall each band is, and
 * what the clock in the top one says. */
static const item_t term_items[] = {
    { "Status bands",     MI_SETTING, SET_VIDEO_STATUSBAR },
    { "",                 MI_SEP },
    { "24-hour clock",    MI_SETTING, SET_TERM_CLOCK24 },
    { "Date format",      MI_SETTING, SET_TERM_DATEFMT },
};
static const item_t input_items[] = {
    { "Reset chord", MI_SETTING, SET_INPUT_RESET_CHORD },
    { "Menu key",    MI_SETTING, SET_INPUT_MENU_KEY },
    { "Mouse capture", MI_SETTING, SET_INPUT_MOUSE_GRAB },
    { "Mouse pointer", MI_SETTING, SET_INPUT_MOUSE_SHOW },
    { "Caps Lock is Ctrl", MI_SETTING, SET_INPUT_CAPS_CTRL },
    { "Keyboard layout",   MI_SETTING, SET_INPUT_KBD_LAYOUT },
    { "Key pipe",          MI_SETTING, SET_INPUT_KEYPIPE },   /* typing from outside (tools/k4510-type): off / on / on, shown */
};
static const item_t save_items[] = {
    { "Slot 1", MI_SAVESLOT, 0 }, { "Slot 2", MI_SAVESLOT, 1 }, { "Slot 3", MI_SAVESLOT, 2 }, { "Slot 4", MI_SAVESLOT, 3 },
};
static const item_t load_items[] = {
    { "Slot 1", MI_LOADSLOT, 0 }, { "Slot 2", MI_LOADSLOT, 1 }, { "Slot 3", MI_LOADSLOT, 2 }, { "Slot 4", MI_LOADSLOT, 3 },
};
static const menu_t save_menu = { "Save state", save_items, (int)(sizeof save_items / sizeof save_items[0]) };
static const menu_t load_menu = { "Load state", load_items, (int)(sizeof load_items / sizeof load_items[0]) };
static const item_t machine_items[] = {
    { "Save state",        MI_SUBMENU, 0, &save_menu },
    { "Load state",        MI_SUBMENU, 0, &load_menu },
    { "",                  MI_SEP },
    { "Reset",             MI_ACTION, ACT_RESET },
    { "Power cycle",       MI_ACTION, ACT_POWER_CYCLE },
    { "Stop the Tube",     MI_ACTION, ACT_TUBE_STOP },
    { "",                  MI_SEP },
    { "Quit the emulator", MI_ACTION, ACT_QUIT },
    { "",                  MI_SEP },
    { "CPU clock",         MI_SETTING, SET_CPU_CLOCK },   /* the ladder, 202.5 down to 10; after Reset so uitest's walk to it is unchanged */
    { "Auto clock",        MI_SETTING, SET_CPU_AUTO },    /* measured at boot (core/calib.c); choosing a clock above turns this off */
    /* The last two rows are the K4510 Linux's and nobody else's, which is why they are
     * LAST: the menu simply stops short of them everywhere else (menu_set_
     * shutdown below), so no host that cannot honour them ever draws them and
     * uitest's walk is unchanged.  On a desktop the emulator is a program and
     * quitting it is enough; on the Pi "Power off" already halts the board.
     * The K4510 Linux is the case in between -- a whole computer whose only job is to
     * be this machine -- and there, ending the session should be able to end
     * the machine, not drop you on a login prompt you did not ask for. */
    { "",                  MI_SEP },
    { "Shut down the computer", MI_ACTION, ACT_SHUTDOWN },
};
#define MACHINE_N ((int)(sizeof machine_items / sizeof machine_items[0]))
static const item_t shell_items[] = {
    { "CP/M .COM by name", MI_SETTING, SET_SHELL_CPMCOM },
    { "Run STARTUP.BAT",   MI_SETTING, SET_SHELL_STARTUP },
};
static const item_t info_items[] = {
    { "Version", MI_INFO, INFO_VERSION }, { "Build", MI_INFO, INFO_BUILD }, { "ROM", MI_INFO, INFO_ROM }, { "Files", MI_INFO, INFO_FS }, { "Host", MI_INFO, INFO_HOST },
    { "Battery", MI_INFO, INFO_BATT },
};
/* The Host category: the K4510 Linux's and nobody else's (menu_set_host), so
 * it is the LAST category and simply off the end of main_items elsewhere --
 * uitest's DOWN-counting walk is unchanged.  Wi-Fi setup runs nmtui on a
 * spare console (the same VT switch tekplay uses; sdl/main.c); the telnet
 * row types TELNET 127.0.0.1 23 at the prompt for you. */
static const item_t host_items[] = {
    { "Name",      MI_INFO, INFO_NAME }, { "Address", MI_INFO, INFO_ADDR }, { "Tailscale", MI_INFO, INFO_TS },
    { "",          MI_SEP },
    { "Lid closed",            MI_SETTING, SET_HOST_LID },   /* keep running / suspend: sdl/main.c host_lid_apply */
    { "Wi-Fi / network setup", MI_ACTION, ACT_NETSETUP },
    { "Telnet into the host",  MI_ACTION, ACT_TELNET },
};
static const menu_t host_menu    = { "Host",    host_items,    (int)(sizeof host_items / sizeof host_items[0]) };
static const menu_t video_menu   = { "Video",   video_items,   (int)(sizeof video_items / sizeof video_items[0]) };
static const menu_t audio_menu   = { "Audio",   audio_items,   (int)(sizeof audio_items / sizeof audio_items[0]) };
static const menu_t term_menu    = { "Terminal", term_items,   (int)(sizeof term_items / sizeof term_items[0]) };
static const menu_t input_menu   = { "Input",   input_items,   (int)(sizeof input_items / sizeof input_items[0]) };   /* was a literal 4: the
                                                                  * fifth row, Caps Lock is Ctrl, never showed (the Dell, 2026-09-12) */
/* The full list: the shutdown row and its separator are left out by rebuild()
 * until a host says it can honour them.  (Was a hard 8 once, and the CPU
 * clock entry never drew.) */
static const menu_t machine_menu = { "Machine", machine_items, MACHINE_N };
static const menu_t shell_menu   = { "Shell",   shell_items,   (int)(sizeof shell_items / sizeof shell_items[0]) };
static const menu_t info_menu    = { "Info",    info_items,    (int)(sizeof info_items / sizeof info_items[0]) };
static const item_t main_items[] = {
    { "Video",   MI_SUBMENU, 0, &video_menu },
    { "Terminal",MI_SUBMENU, 0, &term_menu },
    { "Audio",   MI_SUBMENU, 0, &audio_menu },
    { "Input",   MI_SUBMENU, 0, &input_menu },
    { "Machine", MI_SUBMENU, 0, &machine_menu },
    { "Shell",   MI_SUBMENU, 0, &shell_menu },
    { "Info",    MI_SUBMENU, 0, &info_menu },
    { "Host",    MI_SUBMENU, 0, &host_menu },     /* last, and off the end until menu_set_host */
};
#define MAIN_N ((int)(sizeof main_items / sizeof main_items[0]))
/* What the menu shows is a copy of the tables above, made by rebuild(): the
 * rows the menu file hides are left out, and so are the rows only the K4510
 * Linux may offer (the shutdown row, the Host category) until it says so. */
static item_t vmain[MAIN_N];
static menu_t main_menu = { "K4510", vmain, 0 };

/* ---- state ----------------------------------------------------------------
 * Two panes: the categories on the left, the chosen one's settings on the
 * right. `cat' is the left pane's cursor and never moves on its own; the
 * stack belongs to the right pane, so a submenu (the save-state slots) opens
 * there and Escape steps back out of it before returning to the categories. */
static struct { const menu_t *m; int cur; } stack[6];
static int depth, cat, pane, open_, dirty, action, closed;
static int popup, popup_cur, popup_was;      /* an ENUM's option list, over the panes; popup_was is what to restore on Escape */
static char info[INFO_COUNT][40];
static char slot[MENU_SLOTS][24];

/* ---- the menu file and the shown copy (Doc, 2026-09-13) ---------------------
 * "Could the F7 menu be an editable text file ... if this thing is ever given
 * to kids": hidden rows disappear, no PIN, and locks for the Linux shell and
 * the consoles.  hide_* index the FULL tables, so the file's names are the
 * tables' labels; rebuild() makes the copy the rest of this file draws. */
#define ROWMAX 16
static unsigned char hide_cat[MAIN_N], hide_row[MAIN_N][ROWMAX], locks[MENU_LOCK_COUNT];
static int have_shutdown, have_host;
static item_t vrows[MAIN_N][ROWMAX];
static menu_t vmenu[MAIN_N];
static void rebuild(void)
{
    int k = 0;
    for (int c = 0; c < MAIN_N; c++) {
        const menu_t *full = main_items[c].sub; int n = 0;
        if (hide_cat[c] || (full == &host_menu && !have_host)) continue;
        for (int i = 0; i < full->n && i < ROWMAX; i++) {
            const item_t *it = &full->items[i];
            if (hide_row[c][i]) continue;
            if (full == &machine_menu && !have_shutdown && i >= MACHINE_N - 2) continue;
            if (it->kind == MI_ACTION && it->arg == ACT_TELNET && locks[MENU_LOCK_LINUX]) continue;
            if (it->kind == MI_SEP && (n == 0 || vrows[k][n - 1].kind == MI_SEP)) continue;   /* no separator first, none twice */
            vrows[k][n++] = *it;
        }
        while (n && vrows[k][n - 1].kind == MI_SEP) n--;                                   /* nor last */
        if (!n) continue;                                                                  /* nothing left: the category goes */
        vmenu[k].title = full->title; vmenu[k].items = vrows[k]; vmenu[k].n = n;
        vmain[k] = main_items[c]; vmain[k].sub = &vmenu[k];
        k++;
    }
    main_menu.n = k;
    if (cat >= k) cat = k ? k - 1 : 0;
    dirty = 1;
}

static const menu_t *top(void) { return stack[depth].m; }
static void move_cur(int d)
{
    const menu_t *m = top(); int c = stack[depth].cur;
    for (int i = 0; i < m->n; i++) { c = (c + d + m->n) % m->n; if (m->items[c].kind != MI_SEP) break; }
    stack[depth].cur = c;
}
static void set_cat(int c)
{
    if (!main_menu.n) return;                        /* the file hid everything */
    cat = (c + main_menu.n) % main_menu.n;
    depth = 0; stack[0].m = main_menu.items[cat].sub; stack[0].cur = 0;
    if (stack[0].m->items[0].kind == MI_SEP) move_cur(+1);
}
void menu_open(void) { rebuild(); if (!main_menu.n) return; open_ = 1; pane = 0; popup = 0; set_cat(0); dirty = 1; }
void menu_close(void) { if (open_) { open_ = 0; closed = 1; dirty = 1; } }
int  menu_is_open(void) { return open_; }
void menu_dirty(void) { dirty = 1; }
int  menu_take_action(void) { int a = action; action = ACT_NONE; return a; }
/* The host tells us whether shutting the computer down is a thing it can do.
 * Only the K4510 Linux says yes (sdl/main.c looks for /etc/k4510-linux): on a desktop this
 * would offer to power off Doc's workstation from inside a toy computer. */
void menu_set_shutdown(int available) { have_shutdown = available; rebuild(); }
void menu_set_host(int available)     { have_host = available; rebuild(); }
int  menu_closed_pending(void) { int c = closed; closed = 0; return c; }
void menu_info(int row, const char *text) { if (row >= 0 && row < INFO_COUNT) { snprintf(info[row], sizeof info[row], "%s", text); dirty = 1; } }
void menu_slot(int n, const char *text) { if (n >= 0 && n < MENU_SLOTS) { snprintf(slot[n], sizeof slot[n], "%s", text); dirty = 1; } }
int  menu_key_code(void)
{
    static const uint8_t codes[MENUKEY_COUNT] = { KEY_F1 + 6, KEY_F1 + 7, KEY_F1 + 10, 0x9F };
    return codes[settings_get(SET_INPUT_MENU_KEY)];
}

static void enter(void)
{
    const item_t *it = &top()->items[stack[depth].cur];
    switch (it->kind) {
    case MI_SUBMENU: if (depth < 5) { depth++; stack[depth].m = it->sub; stack[depth].cur = 0; } break;
    case MI_ACTION: action = it->arg; if (it->arg != ACT_TUBE_STOP) menu_close(); break;
    case MI_SAVESLOT: action = ACT_SAVE_SLOT + it->arg; break;               /* the menu stays: the host refreshes the slot's text */
    case MI_LOADSLOT: action = ACT_LOAD_SLOT + it->arg; menu_close(); break;
    case MI_SETTING: {
        const set_desc *d = settings_desc((set_id) it->arg);
        if (d->type == ST_ENUM || d->type == ST_CHORD) { popup = 1; popup_cur = popup_was = settings_get((set_id) it->arg);
            if (popup_cur >= settings_choices((set_id) it->arg) || popup_cur < settings_first((set_id) it->arg))
                popup_cur = settings_first((set_id) it->arg); }   /* in a choice the menu does not offer */
        else settings_step((set_id) it->arg, +1);
        break; }
    default: break;
    }
}
void menu_key(uint8_t k)
{
    if (!open_) return;
    dirty = 1;
    if (popup) {
        const item_t *it = &top()->items[stack[depth].cur];
        int nch = settings_choices((set_id) it->arg), f = settings_first((set_id) it->arg), nv = nch - f;   /* offered: f..nch-1 */
        if (k == KEY_UP) popup_cur = f + (popup_cur - f + nv - 1) % nv;
        else if (k == KEY_DOWN) popup_cur = f + (popup_cur - f + 1) % nv;
        else if (k == KEY_ENTER) { settings_set((set_id) it->arg, popup_cur); popup = 0; return; }
        else if (k == KEY_ESC) { settings_set((set_id) it->arg, popup_was); popup = 0; return; }
        else return;
        settings_set((set_id) it->arg, popup_cur);   /* live: the choice takes effect as the cursor passes it, Escape puts it back */
        return;
    }
    if (k == menu_key_code()) { menu_close(); return; }
    if (!pane) {                                     /* the categories */
        if (k == KEY_ESC) { menu_close(); return; }
        if (k == KEY_UP) set_cat(cat - 1);
        else if (k == KEY_DOWN) set_cat(cat + 1);
        else if (k == KEY_HOME) set_cat(0);
        else if (k == KEY_END) set_cat(main_menu.n - 1);
        else if (k == KEY_ENTER || k == KEY_RIGHT || k == ' ') { if (top()->n) pane = 1; }
        return;
    }
    if (k == KEY_ESC || k == KEY_BS) { if (depth) depth--; else pane = 0; return; }
    if (k == KEY_UP) move_cur(-1);
    else if (k == KEY_DOWN) move_cur(+1);
    else if (k == KEY_ENTER || k == KEY_RIGHT || k == ' ') {
        const item_t *it = &top()->items[stack[depth].cur];
        if (k == KEY_RIGHT && it->kind == MI_SETTING) settings_step((set_id) it->arg, +1); else enter();
    }
    else if (k == KEY_LEFT) {
        const item_t *it = &top()->items[stack[depth].cur];
        if (it->kind == MI_SETTING) settings_step((set_id) it->arg, -1);
        else if (depth) depth--;
        else pane = 0;
    }
    else if (k == KEY_HOME) stack[depth].cur = 0;
    else if (k == KEY_END) { stack[depth].cur = top()->n - 1; if (top()->items[stack[depth].cur].kind == MI_SEP) move_cur(-1); }
}

#define LX   2                       /* the category column */
#define LW   16
#define SEPX (LX + LW)
#define RX   (SEPX + 3)
#define TOPY 5
/* ---- the mouse ---------------------------------------------------------------
 * The menu is a text grid, so a pointer position is a cell.  The right pane
 * follows the pointer (hover = cursor), a click is Enter, a right click on a
 * setting steps it back and anywhere else is Escape; the categories on the
 * left are click-only, so passing over them does not throw away a submenu.
 * The pointer is drawn into the overlay: the machine has no host cursor. */
static int mx = -1, my = -1, mbtn_last;
static void popup_geom(int *px, int *py, int *w, int *h)
{
    const item_t *it = &top()->items[stack[depth].cur]; const set_desc *d = settings_desc((set_id) it->arg);
    int nch = settings_choices((set_id) it->arg), f = settings_first((set_id) it->arg);
    *w = 8; for (int i = f; i < nch; i++) if ((int) strlen(d->labels[i]) + 6 > *w) *w = (int) strlen(d->labels[i]) + 6;
    *h = nch - f + 2; *px = (UI_COLS - *w) / 2; *py = (UI_ROWS - *h) / 2;
}
void menu_mouse(int x, int y, int buttons, int wheel)
{
    int press = buttons & ~mbtn_last, ch = UI_H / UI_ROWS, col, row;
    mbtn_last = buttons;
    if (!open_) return;
    if (x != mx || y != my) { mx = x; my = y; dirty = 1; }
    col = x / 8; row = y / ch;
    if (popup) {
        const item_t *it = &top()->items[stack[depth].cur];
        int nch = settings_choices((set_id) it->arg), f = settings_first((set_id) it->arg), nv = nch - f, px, py, w, h, i;
        popup_geom(&px, &py, &w, &h);
        i = row - py - 1 + f;                                   /* the row's choice: the list starts at f */
        if (wheel) { popup_cur = f + (popup_cur - f + (wheel < 0 ? 1 : nv - 1)) % nv; settings_set((set_id) it->arg, popup_cur); dirty = 1; return; }
        if (i >= f && i < nch && col > px && col < px + w - 1) {
            if (popup_cur != i) { popup_cur = i; settings_set((set_id) it->arg, i); dirty = 1; }
            if (press & 1) { popup = 0; dirty = 1; }
        } else if (press & 3) { settings_set((set_id) it->arg, popup_was); popup = 0; dirty = 1; }
        return;
    }
    if (wheel) { dirty = 1; if (pane) move_cur(wheel < 0 ? +1 : -1); else set_cat(cat + (wheel < 0 ? 1 : -1)); return; }
    if (col >= LX && col < SEPX && row >= TOPY && row < TOPY + main_menu.n) {        /* a category: click only */
        if (press & 1) { set_cat(row - TOPY); pane = 0; dirty = 1; }
        return;
    }
    if (col > SEPX && col < UI_COLS - 1 && row >= TOPY && row < TOPY + top()->n && top()->items[row - TOPY].kind != MI_SEP) {
        const item_t *it = &top()->items[row - TOPY];
        if (!pane || stack[depth].cur != row - TOPY) { pane = 1; stack[depth].cur = row - TOPY; dirty = 1; }
        if (press & 1) { dirty = 1; enter(); }
        else if (press & 2) { dirty = 1; if (it->kind == MI_SETTING) settings_step((set_id) it->arg, -1); else if (depth) depth--; else pane = 0; }
        return;
    }
    if (press & 2) { dirty = 1; if (depth) depth--; else if (pane) pane = 0; else menu_close(); }   /* right click in the open: back */
}
static void draw_pointer(uint8_t *ov)
{
    /* an 8x12 arrow, bright with a dark edge, clipped at the overlay's edges */
    static const uint8_t arrow[12] = { 0x80, 0xC0, 0xE0, 0xF0, 0xF8, 0xFC, 0xFE, 0xF0, 0xD8, 0x98, 0x0C, 0x0C };
    if (mx < 0) return;
    for (int pass = 0; pass < 2; pass++)
        for (int y = 0; y < 12; y++) for (int x = 0; x < 8; x++) {
            if (!((arrow[y] << x) & 0x80)) continue;
            if (pass == 0) {                          /* the edge: the eight neighbours */
                for (int dy = -1; dy <= 1; dy++) for (int dx = -1; dx <= 1; dx++) {
                    int px = mx + x + dx, py = my + y + dy;
                    if (px >= 0 && px < UI_W && py >= 0 && py < UI_H) ov[py * UI_W + px] = UIC_BAR;
                }
            } else if (mx + x < UI_W && my + y < UI_H) ov[(my + y) * UI_W + mx + x] = UIC_BARTEXT;
        }
}

/* ---- drawing ----------------------------------------------------------------
 * The whole screen, opaque: the machine's picture is put away rather than
 * dimmed behind, so nothing of the guest shows through and the menu reads the
 * same whatever was on screen when it opened. */
int menu_draw(uint8_t *ov)
{
    if (!dirty) return 0;
    dirty = 0;
    ui_clear(ov);
    if (!open_) return 1;
    ui_fill(ov, 0, 0, UI_COLS, UI_ROWS, UIC_PANEL);
    ui_fill(ov, 0, 0, UI_COLS, 1, UIC_BAR);
    ui_text(ov, 1, 0, UIC_BARTEXT, UIC_BAR, "K4510");
    { char b[32]; const set_desc *mk = settings_desc(SET_INPUT_MENU_KEY);
      snprintf(b, sizeof b, "%s closes ", mk->labels[settings_get(SET_INPUT_MENU_KEY)]);
      ui_text(ov, UI_COLS - (int) strlen(b), 0, UIC_BARTEXT, UIC_BAR, b); }
    ui_box(ov, 1, 2, UI_COLS - 2, UI_ROWS - 5, UIC_FRAME, UIC_PANEL);
    for (int y = 3; y < UI_ROWS - 4; y++) ui_text(ov, SEPX, y, UIC_FRAME, UIC_PANEL, "\xB3");
    for (int i = 0; i < main_menu.n; i++) {
        int sel = (i == cat);
        uint8_t fg = sel ? (pane ? UIC_TITLE : UIC_BARTEXT) : UIC_TEXT;
        uint8_t bg = (sel && !pane) ? UIC_BAR : UIC_PANEL;
        ui_fill(ov, LX, TOPY + i, LW - 1, 1, bg);
        ui_text(ov, LX + 1, TOPY + i, fg, bg, main_menu.items[i].label);
    }
    { const menu_t *m = top(); char b[32];
      ui_text(ov, RX, 3, UIC_TITLE, UIC_PANEL, m->title);
      for (int i = 0; i < m->n; i++) {
          const item_t *it = &m->items[i]; int y = TOPY + i;
          int sel = pane && (i == stack[depth].cur);
          uint8_t fg = sel ? UIC_BARTEXT : UIC_TEXT, bg = sel ? UIC_BAR : UIC_PANEL;
          const char *v = 0;
          if (it->kind == MI_SEP) continue;
          ui_fill(ov, RX - 1, y, UI_COLS - RX - 1, 1, bg);
          ui_text(ov, RX, y, fg, bg, it->label);
          if (it->kind == MI_SETTING) v = settings_text((set_id) it->arg, b, sizeof b);
          else if (it->kind == MI_INFO) v = info[it->arg];
          else if (it->kind == MI_SAVESLOT || it->kind == MI_LOADSLOT) v = slot[it->arg][0] ? slot[it->arg] : "empty";
          else if (it->kind == MI_SUBMENU) v = ">";
          if (v) ui_text(ov, UI_COLS - 3 - (int) strlen(v), y, it->kind == MI_INFO && !sel ? UIC_DIM : fg, bg, v);
      } }
    { const char *legend = pane ? " Up/Down item   Left/Right change   Enter select   Esc back   or the mouse "
                               : " Up/Down category   Enter or Right for its settings   Esc closes ";
      ui_text(ov, (UI_COLS - (int) strlen(legend)) / 2, UI_ROWS - 2, UIC_DIM, UIC_PANEL, legend); }
    if (popup) {
        const item_t *it = &top()->items[stack[depth].cur]; const set_desc *d = settings_desc((set_id) it->arg);
        char title[64];
        int nch = settings_choices((set_id) top()->items[stack[depth].cur].arg);
        int w, h, px, py; popup_geom(&px, &py, &w, &h);
        ui_box(ov, px, py, w, h, UIC_FRAME, UIC_PANEL);
        snprintf(title, sizeof title, " %s ", it->label);
        ui_text(ov, px + (w - (int) strlen(title)) / 2, py, UIC_TITLE, UIC_PANEL, title);
        int f = settings_first((set_id) it->arg);           /* choices before it are not offered (the clock's cap) */
        for (int i = f; i < nch; i++) {
            int sel = (i == popup_cur), r = py + 1 + i - f; uint8_t fg = sel ? UIC_BARTEXT : UIC_TEXT, bg = sel ? UIC_BAR : UIC_PANEL;
            ui_fill(ov, px + 1, r, w - 2, 1, bg);
            ui_text(ov, px + 2, r, fg, bg, i == popup_was ? "\xFB" : " ");
            ui_text(ov, px + 4, r, fg, bg, d->labels[i]);
        }
    }
    draw_pointer(ov);
    return 1;
}

/* ---- the menu file: k4510-menu.cfg ------------------------------------------
 *   [K4510]              the categories         Audio = hide
 *   [Video] ...          a category's rows      Border width = hide
 *   [Locks]              linux = locked         consoles = locked
 * Names are the menu's own labels, either case; # starts a comment; anything
 * the file does not name is shown, and a line it cannot place is ignored. */
static void mtrim(char *s)
{
    char *a = s, *e; while (*a == ' ' || *a == '\t') a++;
    memmove(s, a, strlen(a) + 1);
    e = s + strlen(s); while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\n' || e[-1] == '\r')) *--e = 0;
}
static int cat_named(const char *name) { for (int c = 0; c < MAIN_N; c++) if (!strcasecmp(main_items[c].label, name)) return c; return -1; }
static int row_named(int c, const char *name)
{
    const menu_t *m = main_items[c].sub;
    for (int i = 0; i < m->n && i < ROWMAX; i++) if (m->items[i].kind != MI_SEP && !strcasecmp(m->items[i].label, name)) return i;
    return -1;
}
int menu_file_load(const char *path)
{
    FILE *f = fopen(path, "r"); char line[160]; int sec = -1;   /* -1 nowhere, -2 [K4510], -3 [Locks], >= 0 a category */
    memset(hide_cat, 0, sizeof hide_cat); memset(hide_row, 0, sizeof hide_row); memset(locks, 0, sizeof locks);
    if (!f) { rebuild(); return -1; }
    while (fgets(line, sizeof line, f)) {
        char *h = strchr(line, '#'), *eq; if (h) *h = 0;
        mtrim(line); if (!line[0]) continue;
        if (line[0] == '[') {
            char *r = strchr(line, ']'); if (r) *r = 0;
            memmove(line, line + 1, strlen(line)); mtrim(line);
            sec = !strcasecmp(line, "K4510") ? -2 : !strcasecmp(line, "Locks") ? -3 : cat_named(line);
            continue;
        }
        if (!(eq = strchr(line, '='))) continue;
        *eq = 0; { char *key = line, *val = eq + 1; mtrim(key); mtrim(val);
          int hide = !strcasecmp(val, "hide"), locked = !strcasecmp(val, "locked");
          if (sec == -2) { int c = cat_named(key); if (c >= 0) hide_cat[c] = (unsigned char) hide; }
          else if (sec == -3) { if (!strcasecmp(key, "linux")) locks[MENU_LOCK_LINUX] = (unsigned char) locked;
                                else if (!strcasecmp(key, "consoles")) locks[MENU_LOCK_CONSOLES] = (unsigned char) locked; }
          else if (sec >= 0) { int r = row_named(sec, key); if (r >= 0) hide_row[sec][r] = (unsigned char) hide; } }
    }
    fclose(f);
    rebuild();
    return 0;
}
int menu_file_write(const char *path)
{
    FILE *f = fopen(path, "w");
    if (!f) return -1;
    fputs("# K4510 -- what the F7 menu shows.  Written in full the first time the machine\n"
          "# starts; edit it, and it takes effect at the next start (or reboot).\n"
          "#   show   the row is there, as always\n"
          "#   hide   the row is gone -- and a menu with nothing left goes too\n"
          "# A hidden setting keeps the value it has in k4510.cfg; nobody can change it\n"
          "# from the menu.  Names are the menu's own, in either case.  The Host menu and\n"
          "# \"Shut down the computer\" appear only on the K4510 Linux.\n\n[K4510]\n", f);
    for (int c = 0; c < MAIN_N; c++) fprintf(f, "%-24s = %s\n", main_items[c].label, hide_cat[c] ? "hide" : "show");
    for (int c = 0; c < MAIN_N; c++) {
        const menu_t *m = main_items[c].sub;
        fprintf(f, "\n[%s]\n", m->title);
        for (int i = 0; i < m->n && i < ROWMAX; i++)
            if (m->items[i].kind != MI_SEP) fprintf(f, "%-24s = %s\n", m->items[i].label, hide_row[c][i] ? "hide" : "show");
    }
    fprintf(f, "\n[Locks]\n"
               "# locked: no ! shell, no SSH, no \"Telnet into the host\" -- the ways into Linux.\n"
               "# PAS and CC still compile.  Edit this file over ssh once it is locked.\n"
               "linux    = %s\n"
               "# locked: Ctrl+Alt+F2..F6 do nothing (the Linux consoles)\n"
               "consoles = %s\n",
            locks[MENU_LOCK_LINUX] ? "locked" : "open", locks[MENU_LOCK_CONSOLES] ? "locked" : "open");
    fclose(f);
    return 0;
}
int menu_lock(int which) { return which >= 0 && which < MENU_LOCK_COUNT ? locks[which] : 0; }
int menu_row_shown(const char *catname, const char *row)
{
    for (int k = 0; k < main_menu.n; k++) {
        const menu_t *m = main_menu.items[k].sub;
        if (strcasecmp(main_menu.items[k].label, catname)) continue;
        if (!row) return 1;
        for (int i = 0; i < m->n; i++) if (m->items[i].kind != MI_SEP && !strcasecmp(m->items[i].label, row)) return 1;
        return 0;
    }
    return 0;
}
