/* test/sidebartest.c -- the sidebars: the list read from the zips in
 * fs/SYSTEM/SIDEBARS (core/sidebars.c), the settings that choose one, and the
 * move of the old register panel into them (docs/SIDEBARS-PLAN.md, step 3). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
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

    printf("6. every scene moves: a minute on, a different picture\n");
    { static uint32_t a[120 * 540], b[120 * 540];
      for (int s = 0; s < SAVER_COUNT; s++) {
          saver_draw(s, a, 120, 120, 540, 100000, 0); saver_draw(s, b, 120, 120, 540, 160000, 0);
          CHECK(memcmp(a, b, sizeof a) != 0, "scene %d is the same a minute later", s);
      } }

    printf("7. any size, from one pixel wide to the widest, and never outside its canvas\n");
    { static const int ws[] = { 1, 3, 4, 7, 16, 61, 240 }, hs[] = { 1, 4, 9, 300, 1080 };
      for (int s = 0; s < SAVER_COUNT; s++)
          for (unsigned wi = 0; wi < sizeof ws / sizeof *ws; wi++) for (unsigned hi = 0; hi < sizeof hs / sizeof *hs; hi++) {
              int w = ws[wi], h = hs[hi], pitch = w + 16, bad = 0;
              uint32_t *buf = malloc((size_t) pitch * (h + 2) * 4), *cv = buf + pitch;   /* a guard row above and below, 16 columns right */
              for (size_t i = 0; i < (size_t) pitch * (h + 2); i++) buf[i] = 0x5A5A5A5Au;
              for (int f = 0; f < 4; f++) saver_draw(s, cv, pitch, w, h, 3000 + f * 900, f & 1);
              for (int x = 0; x < pitch; x++) bad += buf[x] != 0x5A5A5A5Au || buf[(size_t)(h + 1) * pitch + x] != 0x5A5A5A5Au;
              for (int y = 0; y < h; y++) for (int x = w; x < pitch; x++) bad += cv[(size_t) y * pitch + x] != 0x5A5A5A5Au;
              CHECK(!bad, "scene %d at %dx%d wrote %d pixels outside its canvas", s, w, h, bad);
              free(buf);
          } }

    printf("8. the budget: the widest a sidebar gets (240x1080, the HD mode on 1080 lines), mean of 100 frames\n");
    { static uint32_t px[240 * 1080]; static const char *nm[] = { "halloween", "christmas", "space", "river", "dreamfall", "tetris", "antfarm" };
      for (int s = 0; s < SAVER_COUNT; s++) {
          struct timespec t0, t1; double ms;
          for (int f = 0; f < 20; f++) saver_draw(s, px, 240, 240, 1080, 1000 + f * 16, 0);
          clock_gettime(CLOCK_MONOTONIC, &t0);
          for (int f = 0; f < 100; f++) saver_draw(s, px, 240, 240, 1080, 2000 + f * 16, 0);
          clock_gettime(CLOCK_MONOTONIC, &t1);
          ms = ((t1.tv_sec - t0.tv_sec) * 1e3 + (t1.tv_nsec - t0.tv_nsec) / 1e6) / 100;
          printf("     %-10s %.2f ms a side\n", nm[s], ms);
          CHECK(ms < 5.0, "%s takes %.2f ms a frame a side (budget 5)", nm[s], ms);
      } }

    printf("9. the contact sheet: test/out/sidebars.ppm, every scene side by side\n");
    { enum { W = 120, H = 540, G = 6 }; int sw = SAVER_COUNT * (W + G) + G; static uint32_t px[W * H];
      uint32_t *sheet = calloc((size_t) sw * (H + 2 * G), 4); FILE *o;
      for (int s = 0; s < SAVER_COUNT; s++) {
          for (int f = 0; f < 40; f++) saver_draw(s, px, W, W, H, 20000 + f * 250, 0);
          for (int y = 0; y < H; y++) memcpy(&sheet[(size_t)(y + G) * sw + G + s * (W + G)], &px[y * W], W * 4);
      }
      mkdir("test/out", 0777);
      if ((o = fopen("test/out/sidebars.ppm", "wb"))) {
          fprintf(o, "P6 %d %d 255\n", sw, H + 2 * G);
          for (size_t i = 0; i < (size_t) sw * (H + 2 * G); i++) { fputc(sheet[i] >> 16 & 255, o); fputc(sheet[i] >> 8 & 255, o); fputc(sheet[i] & 255, o); }
          fclose(o);
      } else CHECK(0, "cannot write test/out/sidebars.ppm");
      free(sheet); }

    printf(fails ? "\n%d FAILED\n" : "\nALL OK\n", fails); return fails != 0;
}
