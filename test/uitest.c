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
    f = fopen(cfg, "w"); fputs("# my notes\nvideo.border = 12\naudio.volume=30\nfuture.thing = keep me\nvideo.font = unscii\n", f); fclose(f);
    CHECK(settings_load(cfg) == 0, "load");
    CHECK(settings_get(SET_VIDEO_BORDER) == 12 && settings_get(SET_AUDIO_VOLUME) == 30 && settings_get(SET_VIDEO_FONT) == FONT_UNSCII, "values read (%d %d %d)", settings_get(SET_VIDEO_BORDER), settings_get(SET_AUDIO_VOLUME), settings_get(SET_VIDEO_FONT));
    CHECK(settings_get(SET_INPUT_MENU_KEY) == MENUKEY_F7 && !settings_changed(), "defaults for the rest, not dirty");
    settings_set(SET_VIDEO_BORDER, 999); CHECK(settings_get(SET_VIDEO_BORDER) == 64 && settings_changed(), "clamped, dirty");
    settings_step(SET_INPUT_RESET_CHORD, -1); CHECK(settings_get(SET_INPUT_RESET_CHORD) == CHORD_COUNT - 1, "enum wraps");
    CHECK(settings_save(cfg) == 0, "save");
    { char buf[1024] = { 0 }; f = fopen(cfg, "r"); fread(buf, 1, sizeof buf - 1, f); fclose(f);
      CHECK(strstr(buf, "# my notes") && strstr(buf, "future.thing = keep me"), "comments and unknown keys kept");
      CHECK(strstr(buf, "video.border = 64 px") == 0 && strstr(buf, "video.border = 64"), "border rewritten in place");
      CHECK(strstr(buf, "input.reset_chord = Ctrl+Alt+Del") && strstr(buf, "input.menu_key = F7"), "missing keys appended: '%s'", buf); }
    printf("1. registry: load, clamp, wrap, save with unknown keys kept\n");
    /* 2. keys reach the menu through kbd_push */
    settings_defaults();
    kbd_push('a'); CHECK(io_read(IO_KBD) == 'a' && !menu_is_open(), "a plain key reaches the machine");
    kbd_modifiers(1, 0, 0); kbd_push(KEY_F1 + 6); CHECK(!menu_is_open() && io_read(IO_KBD) == KEY_F1 + 6, "Shift+F7 reaches the machine");
    kbd_modifiers(0, 0, 0); kbd_push(KEY_F1 + 6); CHECK(menu_is_open(), "F7 opens the menu");
    kbd_push('x'); CHECK(io_read(IO_KBD) == 0, "keys do not reach the machine while open");
    CHECK(menu_draw(ov) == 1 && menu_draw(ov) == 0, "draws once, then clean");
    { CHECK(cell_is(1, 2, UIC_FRAME) > 0, "the frame is drawn");
      CHECK(cell_is(2, 5, UIC_BAR) > 0, "the category cursor wears the bar");
      CHECK(ov[0] != 0 && ov[(UI_H - 1) * UI_W + UI_W - 1] != 0, "opaque corner to corner: the machine's picture is hidden"); }
    /* The categories, in order: Video, Terminal, Audio, Input, Machine, Shell,
     * Info.  This walk counts DOWNs, so inserting a category shifts it -- as
     * Terminal did on 2026-09-02.  Counts are from the top each time. */
    kbd_push(KEY_DOWN); kbd_push(KEY_DOWN); kbd_push(KEY_ENTER);          /* Audio */
    kbd_push(KEY_RIGHT); CHECK(settings_get(SET_AUDIO_VOLUME) == 90, "Right steps the volume (%d)", settings_get(SET_AUDIO_VOLUME));
    kbd_push(KEY_LEFT); kbd_push(KEY_LEFT); CHECK(settings_get(SET_AUDIO_VOLUME) == 70, "Left steps back");
    kbd_push(KEY_ESC); kbd_push(KEY_UP); kbd_push(KEY_UP); kbd_push(KEY_ENTER);   /* Video */
    kbd_push(KEY_DOWN); kbd_push(KEY_DOWN); kbd_push(KEY_ENTER);  /* Screen font: a popup */
    kbd_push(KEY_DOWN); kbd_push(KEY_DOWN); kbd_push(KEY_ENTER);
    CHECK(settings_get(SET_VIDEO_FONT) == FONT_OPENROMS, "popup chose open-roms (%d)", settings_get(SET_VIDEO_FONT));
    kbd_push(KEY_ESC); kbd_push(KEY_DOWN); kbd_push(KEY_DOWN); kbd_push(KEY_DOWN); kbd_push(KEY_DOWN); kbd_push(KEY_ENTER);   /* Machine */
    kbd_push(KEY_DOWN); kbd_push(KEY_DOWN); kbd_push(KEY_ENTER);   /* past Save/Load state (the separator is skipped): Reset */
    CHECK(!menu_is_open() && menu_take_action() == ACT_RESET && menu_take_action() == ACT_NONE, "Reset acts and closes");
    CHECK(menu_closed_pending() == 1 && menu_closed_pending() == 0, "close reported once");
    kbd_push('b'); CHECK(io_read(IO_KBD) == 'b', "keys reach the machine again");
    settings_set(SET_INPUT_MENU_KEY, MENUKEY_F8); kbd_push(KEY_F1 + 6); CHECK(!menu_is_open(), "F7 is a plain key once the menu key moved");
    kbd_push(KEY_F1 + 7); CHECK(menu_is_open(), "F8 opens it"); menu_close();
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
    CHECK(settings_choices(SET_VIDEO_MODE) == VMODE_320x240 + 1, "the menu offers three modes, not five");
    settings_set(SET_VIDEO_MODE, VMODE_320x240);
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
     * added ABOVE SET_AUDIO_CHIP in the enum and BELOW it in the table, the
     * two swapped in silence: the Sound chip row set a boolean, and the
     * machine stayed on reSID whatever the menu said.  Every id the frontend
     * acts on by name is checked here. */
    { static const struct { set_id id; const char *key; } pairs[] = {
        { SET_VIDEO_MODE, "video.mode" }, { SET_VIDEO_MARGIN, "video.margin" },
        { SET_VIDEO_STATUSBAR, "term.bands" }, { SET_VIDEO_BORDER, "video.border" },
        { SET_VIDEO_FULLSCREEN, "video.fullscreen" }, { SET_VIDEO_VSYNC, "video.vsync" },
        { SET_AUDIO_VOLUME, "audio.volume" }, { SET_AUDIO_SIDS, "audio.sids" },
        { SET_AUDIO_CHIP, "audio.chip" }, { SET_AUDIO_CORE3, "audio.core3" },
        { SET_SHELL_CPMCOM, "shell.cpm_com" }, { SET_SHELL_STARTUP, "shell.startup" },
        { SET_CPU_CLOCK, "cpu.clock" }, { SET_CPU_AUTO, "cpu.auto" },
        { SET_TERM_BAND_TOP, "term.band.top" }, { SET_TERM_BAND_BOT, "term.band.bottom" },
        { SET_TERM_CLOCK24, "term.clock24" }, { SET_TERM_DATEFMT, "term.datefmt" } };
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

    remove(cfg);
    printf(fails ? "\n%d FAILED\n" : "\nALL OK\n", fails); return fails != 0;
}
