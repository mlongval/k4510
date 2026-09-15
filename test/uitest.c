/* The F7 menu and the settings registry: keys in, overlay and k4510.cfg out. */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "../core/xemu/emutools_basicdefs.h"
#include "../core/xemu/cpu65.h"
#include "../core/mem.h"
#include "../core/io.h"
#include "../core/ui/settings.h"
#include "../core/ui/menu.h"
#include "../core/ui/ui_draw.h"
static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("  FAIL: " __VA_ARGS__); printf("\n"); } } while (0)
static uint8_t ov[UI_W * UI_H], font[2048];
static int cell_is(int cx, int cy, int colour) { int n = 0; for (int y = 0; y < 8; y++) for (int x = 0; x < 8; x++) if (ov[(cy * 8 + y) * UI_W + cx * 8 + x] == colour) n++; return n; }
int main(void)
{
    const char *cfg = "test/uitest.cfg"; FILE *f;
    mem_init(); io_reset();
    for (int i = 0; i < 2048; i++) font[i] = (uint8_t)(i * 37);     /* any font: the test only looks at colours */
    ui_font(font);
    /* 1. the registry and its file */
    f = fopen(cfg, "w"); fputs("# my notes\nvideo.border = 12\naudio.volume=30\nfuture.thing = keep me\nvideo.smoothing = sharp\n", f); fclose(f);
    CHECK(settings_load(cfg) == 0, "load");
    CHECK(settings_get(SET_VIDEO_BORDER) == 12 && settings_get(SET_AUDIO_VOLUME) == 30 && settings_get(SET_VIDEO_SMOOTH) == SMOOTH_FIT, "values read (%d %d %d)", settings_get(SET_VIDEO_BORDER), settings_get(SET_AUDIO_VOLUME), settings_get(SET_VIDEO_SMOOTH));
    CHECK(settings_get(SET_INPUT_MENU_KEY) == MENUKEY_F12 && settings_get(SET_TEXT_CODEPAGE) == PAGE_CP437 && !settings_changed(), "defaults for the rest (F12, CP437), not dirty");
    settings_set(SET_VIDEO_BORDER, 999); CHECK(settings_get(SET_VIDEO_BORDER) == 64 && settings_changed(), "clamped, dirty");
    settings_step(SET_INPUT_RESET_CHORD, -1); CHECK(settings_get(SET_INPUT_RESET_CHORD) == CHORD_COUNT - 1, "enum wraps");
    CHECK(settings_save(cfg) == 0, "save");
    { char buf[1024] = { 0 }; f = fopen(cfg, "r"); fread(buf, 1, sizeof buf - 1, f); fclose(f);
      CHECK(strstr(buf, "# my notes") && strstr(buf, "future.thing = keep me"), "comments and unknown keys kept");
      CHECK(strstr(buf, "video.border = 64 px") == 0 && strstr(buf, "video.border = 64"), "border rewritten in place");
      CHECK(strstr(buf, "input.reset_chord = Ctrl+Alt+Del") && strstr(buf, "input.menu_key = F12"), "missing keys appended: '%s'", buf); }
    f = fopen(cfg, "w"); fputs("version = 2\ninput.menu_key = F7\n", f); fclose(f);                    /* 2 -> 3: F7 is F12 now */
    CHECK(settings_load(cfg) == 0 && settings_get(SET_INPUT_MENU_KEY) == MENUKEY_F12 && settings_changed(), "an old F7 moves to F12, and is saved");
    f = fopen(cfg, "w"); fputs("version = 3\ninput.menu_key = F7\n", f); fclose(f);
    CHECK(settings_load(cfg) == 0 && settings_get(SET_INPUT_MENU_KEY) == MENUKEY_F7 && !settings_changed(), "F7 chosen since stays F7");
    printf("1. registry: load, clamp, wrap, save with unknown keys kept, 2 -> 3\n");
    /* 2. keys reach the menu through kbd_push / kbd_push_key (the F-keys and arrows are KEY codes, since 2026-09-08 a kind of their own) */
    settings_defaults();
    kbd_push('a'); CHECK(io_read(IO_KBD) == 'a' && !menu_is_open(), "a plain key reaches the machine");
    kbd_modifiers(1, 0, 0); kbd_push_key(KEY_F1 + 11); CHECK(!menu_is_open() && io_read(IO_KBD) == KEY_F1 + 11, "Shift+F12 reaches the machine (the frontend pauses on it)");
    kbd_modifiers(0, 0, 0); kbd_push_key(KEY_F1 + 6); CHECK(!menu_is_open() && io_read(IO_KBD) == KEY_F1 + 6, "F7 is a program's key now");
    kbd_push_key(KEY_F1 + 11); CHECK(menu_is_open(), "F12 opens the menu");
    kbd_push('x'); CHECK(io_read(IO_KBD) == 0, "keys do not reach the machine while open");
    CHECK(menu_draw(ov) == 1 && menu_draw(ov) == 0, "draws once, then clean");
    { CHECK(cell_is(1, 2, UIC_FRAME) > 0, "the frame is drawn");
      CHECK(cell_is(2, 5, UIC_BAR) > 0, "the category cursor wears the bar");
      CHECK(ov[0] != 0 && ov[(UI_H - 1) * UI_W + UI_W - 1] != 0, "opaque corner to corner: the machine's picture is hidden"); }
    /* The categories, in order: Video, Terminal, Audio, Input, Machine, Shell,
     * Info.  This walk counts DOWNs, so inserting a category shifts it -- as
     * Terminal did on 2026-09-02.  Counts are from the top each time. */
    kbd_push_key(KEY_DOWN); kbd_push_key(KEY_DOWN); kbd_push(KEY_ENTER);          /* Audio */
    kbd_push_key(KEY_RIGHT); CHECK(settings_get(SET_AUDIO_VOLUME) == 90, "Right steps the volume (%d)", settings_get(SET_AUDIO_VOLUME));
    kbd_push_key(KEY_LEFT); kbd_push_key(KEY_LEFT); CHECK(settings_get(SET_AUDIO_VOLUME) == 70, "Left steps back");
    kbd_push(KEY_ESC); kbd_push_key(KEY_UP); kbd_push_key(KEY_UP); kbd_push(KEY_ENTER);   /* Video */
    for (int k = 0; k < 3; k++) kbd_push_key(KEY_DOWN);                      /* Scaling: a popup */
    kbd_push(KEY_ENTER);
    kbd_push_key(KEY_DOWN); kbd_push(KEY_ENTER);
    CHECK(settings_get(SET_VIDEO_SMOOTH) == SMOOTH_FIT, "popup chose fit to display (%d)", settings_get(SET_VIDEO_SMOOTH));
    kbd_push(KEY_ESC); kbd_push_key(KEY_DOWN); kbd_push_key(KEY_DOWN); kbd_push_key(KEY_DOWN); kbd_push_key(KEY_DOWN); kbd_push(KEY_ENTER);   /* Machine */
    kbd_push_key(KEY_DOWN); kbd_push_key(KEY_DOWN); kbd_push(KEY_ENTER);   /* past Save/Load state (the separator is skipped): Reset */
    CHECK(!menu_is_open() && menu_take_action() == ACT_RESET && menu_take_action() == ACT_NONE, "Reset acts and closes");
    CHECK(menu_closed_pending() == 1 && menu_closed_pending() == 0, "close reported once");
    kbd_push('b'); CHECK(io_read(IO_KBD) == 'b', "keys reach the machine again");
    settings_set(SET_INPUT_MENU_KEY, MENUKEY_F8); kbd_push_key(KEY_F1 + 11); CHECK(!menu_is_open(), "F12 is a plain key once the menu key moved");
    kbd_push_key(KEY_F1 + 7); CHECK(menu_is_open(), "F8 opens it"); menu_close();
    printf("2. menu: open/close, navigation, INT steps, ENUM popup, actions\n");
    /* 3. the shell toggle the ROM reads at $D521 */
    settings_defaults();
    CHECK(settings_get(SET_SHELL_CPMCOM) == 0, "CP/M .COM by name is off unless asked for");
    io_set_opts(settings_get(SET_SHELL_CPMCOM) ? SYSOPT_CPMCOM : 0);
    CHECK((io_read(IO_SYS_OPTS) & SYSOPT_CPMCOM) == 0, "and the guest sees it off");
    settings_set(SET_SHELL_CPMCOM, 1);
    io_set_opts(settings_get(SET_SHELL_CPMCOM) ? SYSOPT_CPMCOM : 0);
    CHECK((io_read(IO_SYS_OPTS) & SYSOPT_CPMCOM) != 0, "switched on, the guest sees it on");
    printf("3. the shell toggle reaches the guest at $D521\n");

    /* 4. the video mode: asked for through the same byte, and never saved below 320x240 */
    settings_defaults();
    settings_set(SET_VIDEO_MODE, VMODE_320x200);
    io_set_opts((uint8_t)((VMODE_320x200 + 1) << SYSOPT_MODE_SHIFT));
    CHECK((io_read(IO_SYS_OPTS) >> SYSOPT_MODE_SHIFT) == VMODE_320x200 + 1, "the guest is asked for the mode at $D521");
    io_set_opts(0);
    CHECK((io_read(IO_SYS_OPTS) & SYSOPT_MODE) == 0, "and the request clears");

    settings_set(SET_VIDEO_MODE, VMODE_160x200);
    CHECK(settings_get(SET_VIDEO_MODE) == VMODE_160x200, "160x200 can be chosen");
    settings_save(cfg);
    settings_defaults();
    settings_load(cfg);
    CHECK(settings_get(SET_VIDEO_MODE) == VMODE_320x240, "but 320x240 is what survives a save");
    { FILE *f = fopen(cfg, "a"); if (f) { fputs("video.mode = 160x200\n", f); fclose(f); } }
    settings_load(cfg);
    CHECK(settings_get(SET_VIDEO_MODE) == VMODE_320x240, "and a hand-edited file is clamped on the way in");
    /* the menu will not steer into 320x200 / 160x200 -- 40x25 and 20x25 are not a
     * shell -- but it still shows one when the guest (MODE 3, a game) is in it */
    settings_defaults();
    CHECK(settings_choices(SET_VIDEO_MODE) == VMODE_360x270 + 1, "the menu offers six modes, not eight");
    settings_set(SET_VIDEO_MODE, VMODE_360x270);
    settings_step(SET_VIDEO_MODE, +1);
    CHECK(settings_get(SET_VIDEO_MODE) == VMODE_640x480, "stepping past the last offered one wraps, not into 320x200");
    settings_set(SET_VIDEO_MODE, VMODE_160x200);
    CHECK(settings_get(SET_VIDEO_MODE) == VMODE_160x200, "but the guest may put the machine in one, and the row says so");
    { char b[32]; CHECK(!strcmp(settings_text(SET_VIDEO_MODE, b, sizeof b), "160x200"), "shown by name"); }
    printf("4. the mode request, and 320x240 as the floor for what is saved\n");

    /* 5. the STARTUP.BAT switch: the way out of one that wedges the machine */
    settings_defaults();
    CHECK(settings_get(SET_SHELL_STARTUP) == 1, "STARTUP.BAT runs unless told not to");
    io_set_opts(settings_get(SET_SHELL_STARTUP) ? 0 : SYSOPT_NOBOOT);
    CHECK((io_read(IO_SYS_OPTS) & SYSOPT_NOBOOT) == 0, "and the guest is not told to skip it");
    settings_set(SET_SHELL_STARTUP, 0);
    io_set_opts(settings_get(SET_SHELL_STARTUP) ? 0 : SYSOPT_NOBOOT);
    CHECK((io_read(IO_SYS_OPTS) & SYSOPT_NOBOOT) != 0, "switched off, the guest skips it at power-on");
    printf("5. the STARTUP.BAT switch reaches the guest at $D521\n");

    /* 6. the settings table is indexed by set_id, and nothing in the compiler
     * checks that the enum's order matches it.  When SET_AUDIO_CORE3 was
     * added ABOVE the (since removed) SET_AUDIO_CHIP in the enum and BELOW
     * it in the table, the two swapped in silence: the Sound chip row set a
     * boolean, and the machine stayed on the wrong chip whatever the menu
     * said.  Every id the frontend acts on by name is checked here. */
    { static const struct { set_id id; const char *key; } pairs[] = {
        { SET_VIDEO_MODE, "video.mode" }, { SET_VIDEO_SMOOTH, "video.smoothing" },
        { SET_VIDEO_STATUSBAR, "term.bands" }, { SET_VIDEO_BORDER, "video.border" },
        { SET_VIDEO_FULLSCREEN, "video.fullscreen" }, { SET_VIDEO_VSYNC, "video.vsync" },
        { SET_AUDIO_VOLUME, "audio.volume" },
        { SET_SHELL_CPMCOM, "shell.cpm_com" }, { SET_SHELL_STARTUP, "shell.startup" },
        { SET_CPU_CLOCK, "cpu.clock" }, { SET_CPU_AUTO, "cpu.auto" },
        { SET_TERM_CLOCK24, "term.clock24" }, { SET_TERM_DATEFMT, "term.datefmt" },
        { SET_INPUT_MENU_KEY, "input.menu_key" }, { SET_INPUT_MOUSE_GRAB, "input.mouse_grab" } };
      for (unsigned i = 0; i < sizeof pairs / sizeof pairs[0]; i++)
          CHECK(!strcmp(settings_key(pairs[i].id), pairs[i].key),
                "id %d is \"%s\", expected \"%s\"", (int)pairs[i].id, settings_key(pairs[i].id), pairs[i].key);
      printf("6. every set_id names the setting it is supposed to\n"); }

    /* 7. term.bands was video.statusbar until 2026-09-02.  A renamed key is a
     * silently lost setting: every config with the bands on would have come
     * back with them off.  The old name still loads. */
    { FILE *f = fopen(cfg, "w");
      fprintf(f, "version = 2\nvideo.statusbar = on\n"); fclose(f);
      settings_load(cfg);
      CHECK(settings_get(SET_VIDEO_STATUSBAR) == 1, "the old video.statusbar key no longer loads");
      printf("7. a config written before the rename still turns the bands on\n"); }

    /* 8. The shutdown row is the K4510 Linux's alone.  A desktop that could power the
     * host off from inside the Machine menu would be a nasty surprise, and the
     * row is kept off the end of the table rather than hidden mid-list, so
     * this also pins that legs 2's walk does not shift under it. */
    /* END lands on the last row of the Machine menu, which is the whole point
     * of putting the shutdown row there: by default that is "Auto clock" and
     * pressing it can only touch a setting. */
    { menu_open();
      menu_key(KEY_DOWN); menu_key(KEY_DOWN); menu_key(KEY_DOWN); menu_key(KEY_DOWN); menu_key(KEY_ENTER);   /* Machine */
      menu_key(KEY_END); menu_key(KEY_ENTER);
      CHECK(menu_take_action() != ACT_SHUTDOWN, "a plain host can shut the computer down from the menu");
      menu_close(); menu_take_action();

      menu_set_shutdown(1);
      menu_open();
      menu_key(KEY_DOWN); menu_key(KEY_DOWN); menu_key(KEY_DOWN); menu_key(KEY_DOWN); menu_key(KEY_ENTER);   /* Machine */
      menu_key(KEY_END); menu_key(KEY_ENTER);
      CHECK(menu_take_action() == ACT_SHUTDOWN, "the K4510 Linux asked for the shutdown row and did not get it");
      menu_close(); menu_take_action();
      menu_set_shutdown(0);
      printf("8. \"Shut down\" stays off the menu until the host says it can\n"); }

    /* 9. the mouse: hover selects in the right pane, a click on a category
     * picks it, the pointer is drawn into the overlay, a right click in the
     * open backs out and, at the top, closes.  Cells are 8 wide and
     * UI_H/UI_ROWS tall; the layout constants are menu.c's (LX 2, SEPX 18,
     * RX 21, TOPY 5). */
    { static uint8_t ov[640 * 480]; int ch = 480 / ui_rows();
      menu_open();
      menu_mouse(23 * 8 + 3, 6 * ch + 2, 0, 0);                    /* hover: right pane, second item */
      menu_draw(ov);
      CHECK(ov[(6 * ch + 2) * 640 + 23 * 8 + 3] != 0, "the pointer was not drawn into the overlay");
      menu_mouse(4 * 8, 7 * ch + 2, 1, 0); menu_mouse(4 * 8, 7 * ch + 2, 0, 0);   /* click the third category */
      menu_draw(ov);
      CHECK(menu_is_open(), "a category click closed the menu");
      menu_mouse(630, 470, 2, 0); menu_mouse(630, 470, 0, 0);      /* right click in the open: back to the categories, then... */
      menu_mouse(630, 470, 2, 0); menu_mouse(630, 470, 0, 0);      /* ...closes */
      CHECK(!menu_is_open(), "two right clicks in the open did not close the menu");
      menu_take_action();
      printf("9. the mouse hovers, clicks, draws its pointer, backs out\n"); }

    /* 10. The Host category (name, addresses, Wi-Fi setup, telnet) is the K4510
     * Linux's alone, kept off the END of the category list the same way. */
    { menu_open();
      menu_key(KEY_END); menu_key(KEY_ENTER);                    /* the last category: Info on a plain host */
      menu_key(KEY_END); menu_key(KEY_ENTER);                    /* an info row: nothing happens */
      CHECK(menu_take_action() == ACT_NONE, "a plain host has a Host category with actions");
      menu_close(); menu_take_action();
      menu_set_host(1);
      menu_open();
      menu_key(KEY_END); menu_key(KEY_ENTER);                    /* Host */
      menu_key(KEY_END); menu_key(KEY_ENTER);                    /* its last row: telnet */
      CHECK(menu_take_action() == ACT_TELNET, "the K4510 Linux asked for the Host category and did not get its telnet row");
      menu_close(); menu_take_action();
      menu_set_host(0);
      printf("10. the Host category stays off the menu until the host says so\n"); }

    remove(cfg);
    /* 11. The menu file, k4510-menu.cfg (Doc, 2026-09-13): hidden rows and
     * categories go, a separator left alone goes with them, names match in
     * either case, the locks read, and the written file lists every row. */
    { const char *mf = "test/uitest-menu.cfg", *mf2 = "test/uitest-menu2.cfg"; char buf[4096] = { 0 };
      f = fopen(mf, "w");
      fputs("# a parent's choices\n[K4510]\nAudio = hide\n[Video]\nBorder width = hide\nfull screen = HIDE   # either case\n"
            "[Terminal]\n24-hour clock = hide\nDate format = hide\n[Nowhere]\nX = hide\n[Locks]\nlinux = locked\n", f); fclose(f);
      CHECK(menu_file_load(mf) == 0, "the menu file loads");
      CHECK(!menu_row_shown("Audio", 0) && menu_row_shown("Video", 0), "a hidden category is gone, the others stay");
      CHECK(!menu_row_shown("Video", "Border width") && !menu_row_shown("Video", "Full screen") && menu_row_shown("Video", "Scaling"), "hidden rows are gone, either case");
      CHECK(!menu_row_shown("Terminal", "Date format") && menu_row_shown("Terminal", "Status bands"), "the Terminal rows it names are gone");
      CHECK(menu_lock(MENU_LOCK_LINUX) && !menu_lock(MENU_LOCK_CONSOLES), "the locks read");
      CHECK(menu_file_write(mf2) == 0, "the menu file is written");
      f = fopen(mf2, "r"); if (f) { fread(buf, 1, sizeof buf - 1, f); fclose(f); }
      CHECK(strstr(buf, "[Video]") && strstr(buf, "Border width") && strstr(buf, "Scaling") && strstr(buf, "[Machine]")
            && strstr(buf, "Shut down the computer") && strstr(buf, "linux    = locked") && strstr(buf, "consoles = open"), "...listing every row and the locks");
      f = fopen(mf, "w"); fclose(f);
      CHECK(menu_file_load(mf) == 0 && menu_row_shown("Audio", 0) && menu_row_shown("Video", "Border width") && !menu_lock(MENU_LOCK_LINUX), "an empty file shows everything again");
      remove(mf); remove(mf2);
      printf("11. the menu file: hidden rows and categories, either case, the locks, the full list written\n"); }
    printf(fails ? "\n%d FAILED\n" : "\nALL OK\n", fails); return fails != 0;
}
