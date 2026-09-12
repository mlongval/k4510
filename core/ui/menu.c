/* The F7 menu. See menu.h. */
#include "menu.h"
#include "settings.h"
#include "ui_draw.h"
#include "../io.h"
#include <string.h>
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
    { "Left/top margin", MI_SETTING, SET_VIDEO_MARGIN },
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
    { "Top band rows",    MI_SETTING, SET_TERM_BAND_TOP },
    { "Bottom band rows", MI_SETTING, SET_TERM_BAND_BOT },
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
};
static const item_t save_items[] = {
    { "Slot 1", MI_SAVESLOT, 0 }, { "Slot 2", MI_SAVESLOT, 1 }, { "Slot 3", MI_SAVESLOT, 2 }, { "Slot 4", MI_SAVESLOT, 3 },
};
static const item_t load_items[] = {
    { "Slot 1", MI_LOADSLOT, 0 }, { "Slot 2", MI_LOADSLOT, 1 }, { "Slot 3", MI_LOADSLOT, 2 }, { "Slot 4", MI_LOADSLOT, 3 },
};
static const menu_t save_menu = { "Save state", save_items, 4 };
static const menu_t load_menu = { "Load state", load_items, 4 };
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
};
/* The Host category: the K4510 Linux's and nobody else's (menu_set_host), so
 * it is the LAST category and simply off the end of main_items elsewhere --
 * uitest's DOWN-counting walk is unchanged.  Wi-Fi setup runs nmtui on a
 * spare console (the same VT switch tekplay uses; sdl/main.c); the telnet
 * row types TELNET 127.0.0.1 23 at the prompt for you. */
static const item_t host_items[] = {
    { "Name",      MI_INFO, INFO_NAME }, { "Address", MI_INFO, INFO_ADDR }, { "Tailscale", MI_INFO, INFO_TS },
    { "",          MI_SEP },
    { "Wi-Fi / network setup", MI_ACTION, ACT_NETSETUP },
    { "Telnet into the host",  MI_ACTION, ACT_TELNET },
};
static const menu_t host_menu    = { "Host",    host_items,    (int)(sizeof host_items / sizeof host_items[0]) };
static const menu_t video_menu   = { "Video",   video_items,   (int)(sizeof video_items / sizeof video_items[0]) };
static const menu_t audio_menu   = { "Audio",   audio_items,   (int)(sizeof audio_items / sizeof audio_items[0]) };
static const menu_t term_menu    = { "Terminal", term_items,   (int)(sizeof term_items / sizeof term_items[0]) };
static const menu_t input_menu   = { "Input",   input_items,   (int)(sizeof input_items / sizeof input_items[0]) };   /* was a literal 4: the
                                                                  * fifth row, Caps Lock is Ctrl, never showed (the Dell, 2026-09-12) */
/* NOT const, and not the full count: the shutdown row and its separator are
 * off the end until a host says it can honour them.  (Was a hard 8 once, and
 * the CPU clock entry never drew.) */
static menu_t machine_menu = { "Machine", machine_items, MACHINE_N - 2 };
static const menu_t shell_menu   = { "Shell",   shell_items,   2 };
static const menu_t info_menu    = { "Info",    info_items,    5 };
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
static menu_t main_menu = { "K4510", main_items, MAIN_N - 1 };

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

static const menu_t *top(void) { return stack[depth].m; }
static void move_cur(int d)
{
    const menu_t *m = top(); int c = stack[depth].cur;
    for (int i = 0; i < m->n; i++) { c = (c + d + m->n) % m->n; if (m->items[c].kind != MI_SEP) break; }
    stack[depth].cur = c;
}
static void set_cat(int c)
{
    cat = (c + main_menu.n) % main_menu.n;
    depth = 0; stack[0].m = main_items[cat].sub; stack[0].cur = 0;
    if (stack[0].m->items[0].kind == MI_SEP) move_cur(+1);
}
void menu_open(void) { open_ = 1; pane = 0; popup = 0; set_cat(0); dirty = 1; }
void menu_close(void) { if (open_) { open_ = 0; closed = 1; dirty = 1; } }
int  menu_is_open(void) { return open_; }
void menu_dirty(void) { dirty = 1; }
int  menu_take_action(void) { int a = action; action = ACT_NONE; return a; }
/* The host tells us whether shutting the computer down is a thing it can do.
 * Only the K4510 Linux says yes (sdl/main.c looks for /etc/k4510-linux): on a desktop this
 * would offer to power off Doc's workstation from inside a toy computer. */
void menu_set_shutdown(int available) { machine_menu.n = available ? MACHINE_N : MACHINE_N - 2; dirty = 1; }
void menu_set_host(int available)     { main_menu.n = available ? MAIN_N : MAIN_N - 1; dirty = 1; }
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
            if (popup_cur >= settings_choices((set_id) it->arg)) popup_cur = 0; }   /* in a choice the menu does not offer */
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
        int nch = settings_choices((set_id) it->arg);
        if (k == KEY_UP) popup_cur = (popup_cur + nch - 1) % nch;
        else if (k == KEY_DOWN) popup_cur = (popup_cur + 1) % nch;
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
    int nch = settings_choices((set_id) it->arg);
    *w = 8; for (int i = 0; i < nch; i++) if ((int) strlen(d->labels[i]) + 6 > *w) *w = (int) strlen(d->labels[i]) + 6;
    *h = nch + 2; *px = (UI_COLS - *w) / 2; *py = (UI_ROWS - *h) / 2;
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
        int nch = settings_choices((set_id) it->arg), px, py, w, h, i;
        popup_geom(&px, &py, &w, &h);
        i = row - py - 1;
        if (wheel) { popup_cur = (popup_cur + (wheel < 0 ? 1 : nch - 1)) % nch; settings_set((set_id) it->arg, popup_cur); dirty = 1; return; }
        if (i >= 0 && i < nch && col > px && col < px + w - 1) {
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
        ui_text(ov, LX + 1, TOPY + i, fg, bg, main_items[i].label);
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
        for (int i = 0; i < nch; i++) {
            int sel = (i == popup_cur); uint8_t fg = sel ? UIC_BARTEXT : UIC_TEXT, bg = sel ? UIC_BAR : UIC_PANEL;
            ui_fill(ov, px + 1, py + 1 + i, w - 2, 1, bg);
            ui_text(ov, px + 2, py + 1 + i, fg, bg, i == popup_was ? "\xFB" : " ");
            ui_text(ov, px + 4, py + 1 + i, fg, bg, d->labels[i]);
        }
    }
    draw_pointer(ov);
    return 1;
}
