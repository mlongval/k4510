/* test/sidebartest.c -- the sidebars: the list read from the zips in
 * fs/SYSTEM/SIDEBARS (core/sidebars.c), the settings that choose one, and the
 * move of the old register panel into them (docs/SIDEBARS-PLAN.md, step 3). */
#include <stdio.h>
#include <string.h>
#include "../core/sidebars.h"
#include "../core/ui/settings.h"
#include "../sdl/savers.h"
static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("  FAIL: " __VA_ARGS__); printf("\n"); } } while (0)
static const char *cfg = "/tmp/k4510-sidebartest.cfg";
static void load(const char *text) { FILE *f = fopen(cfg, "w"); fputs(text, f); fclose(f); settings_load(cfg); }
static const char *chosen(void) { const sidebar_info *s = sidebars_info(settings_get(SET_VIDEO_SIDEBARS)); return s ? s->key : "?"; }
int main(void)
{
    static const char *order[] = { "border", "gradient", "knot", "registers", "halloween", "christmas", "space", "river", "dreamfall", "tetris", "antfarm" };

    printf("1. before any zips: the built-in names, the value is the drawing\n");
    settings_defaults();
    CHECK(settings_choices(SET_VIDEO_SIDEBARS) == SIDEBAR_COUNT, "%d built-in choices", settings_choices(SET_VIDEO_SIDEBARS));
    CHECK(sidebars_builtin(SIDEBAR_REGISTERS) == SIDEBAR_REGISTERS && sidebars_builtin(99) == SIDEBAR_BORDER, "no zips: value = builtin");

    printf("2. the zips in fs/SYSTEM/SIDEBARS\n");
    int n = sidebars_scan("fs");
    CHECK(n == SIDEBAR_COUNT, "%d sidebars read, want %d", n, SIDEBAR_COUNT);
    for (int i = 0; i < n && i < SIDEBAR_COUNT; i++) {
        const sidebar_info *s = sidebars_info(i);
        printf("     %-10s %-10s %s%s\n", s->key, s->name, s->about, s->months ? "  (seasonal)" : "");
        CHECK(!strcmp(s->key, order[i]) && s->builtin == i && s->version == 1 && s->name[0] && s->about[0], "entry %d is %s", i, s->key);
    }
    CHECK(sidebars_info(sidebars_find("halloween"))->months == 1u << 9, "halloween is October");
    CHECK(sidebars_info(sidebars_find("christmas"))->months == 1u << 11, "christmas is December");
    CHECK(sidebars_find("nosuch") == -1, "find a name not there");

    printf("3. the setting: its choices are the zips, saved by name\n");
    settings_defaults();
    CHECK(!strcmp(chosen(), "border"), "the default is border, not %s", chosen());
    load("video.sidebars = antfarm\n");
    CHECK(!strcmp(chosen(), "antfarm") && sidebars_builtin(settings_get(SET_VIDEO_SIDEBARS)) == SIDEBAR_ANTFARM, "antfarm loads (%s)", chosen());
    load("video.sidebars = nosuch\n");
    CHECK(sidebars_builtin(settings_get(SET_VIDEO_SIDEBARS)) == SIDEBAR_BORDER, "an unknown name is the border");
    { char b[32]; load("video.sidebars = tetris\n"); settings_step(SET_VIDEO_SIDEBARS, 1);
      CHECK(!strcmp(settings_text(SET_VIDEO_SIDEBARS, b, sizeof b), "antfarm"), "the next after tetris is %s", settings_text(SET_VIDEO_SIDEBARS, b, sizeof b));
      settings_step(SET_VIDEO_SIDEBARS, 1);
      CHECK(!strcmp(settings_text(SET_VIDEO_SIDEBARS, b, sizeof b), "border"), "and then round to border"); }

    printf("4. an old config's register panel becomes the Registers sidebar\n");
    load("version = 3\nvideo.panel = registers\nvideo.sidebars = antfarm\n");
    CHECK(!strcmp(chosen(), "registers") && settings_get(SET_VIDEO_PANEL) == PANEL_OFF && settings_changed(), "panel -> registers (%s)", chosen());
    CHECK(sidebars_builtin(settings_get(SET_VIDEO_SIDEBARS)) == SIDEBAR_REGISTERS, "and it draws the panel");
    settings_save(cfg);
    { FILE *f = fopen(cfg, "r"); char line[128]; int ok = 0, panel_off = 0;
      while (f && fgets(line, sizeof line, f)) { if (!strcmp(line, "video.sidebars = registers\n")) ok = 1; if (!strcmp(line, "video.panel = off\n")) panel_off = 1; }
      if (f) fclose(f);
      CHECK(ok && panel_off, "saved as video.sidebars = registers, video.panel = off"); }
    load("version = 3\nvideo.panel = off\nvideo.sidebars = river\n");
    CHECK(!strcmp(chosen(), "river") && !settings_changed(), "no panel: nothing moved");
    remove(cfg);

    printf("5. every scene draws, both sides, and fills its canvas\n");
    { static uint32_t px[120 * 540];
      for (int s = 0; s < SAVER_COUNT; s++) for (int side = 0; side < 2; side++) {
          memset(px, 0, sizeof px);
          for (int f = 0; f < 5; f++) saver_draw(s, px, 120, 120, 540, 5000 + f * 700, side);
          int blank = 0; for (int i = 0; i < 120 * 540; i++) if (!(px[i] >> 24)) blank++;
          CHECK(blank == 0, "scene %d side %d leaves %d pixels unpainted", s, side, blank);
      } }

    printf(fails ? "\n%d FAILED\n" : "\nALL OK\n", fails); return fails != 0;
}
