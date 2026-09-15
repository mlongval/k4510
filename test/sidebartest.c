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
    setenv("K4510_SAVER_DAY", "600", 1);   /* the ant farm's day by the test's clock, not the host's: the same picture every run */

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

    printf("10. OPTIONS.CFG: the zip's copy on first use, read again when it changes\n");
    char tmp[] = "/tmp/k4510-sbtXXXXXX", cmd[600], p[800];
    if (!mkdtemp(tmp)) { CHECK(0, "mkdtemp"); return 1; }
    snprintf(cmd, sizeof cmd, "mkdir -p %s/SYSTEM/SIDEBARS && cp fs/SYSTEM/SIDEBARS/*.ZIP %s/SYSTEM/SIDEBARS/", tmp, tmp);
    CHECK(system(cmd) == 0, "copy the zips");
    CHECK(sidebars_scan(tmp) == SIDEBAR_COUNT, "scan the copy");
    int af = sidebars_find("antfarm"), tt = sidebars_find("tetris");
    CHECK(sidebars_prepare(af) == 0, "prepare antfarm");
    snprintf(p, sizeof p, "%s/SYSTEM/SIDEBARS/ANTFARM/OPTIONS.CFG", tmp);
    { FILE *f = fopen(p, "r"); char l[200]; int sp = 0; while (f && fgets(l, sizeof l, f)) if (!strncmp(l, "speed = 1", 9)) sp = 1; if (f) fclose(f);
      CHECK(sp, "ANTFARM/OPTIONS.CFG is the zip's copy"); }
    CHECK(sidebars_speed(af) == 1.0 && sidebars_opt(af, "day") && !strcmp(sidebars_opt(af, "day"), "real"), "its defaults: speed 1, day real");
    { char gp[800]; snprintf(gp, sizeof gp, "%s/SYSTEM/SIDEBARS/SIDEBARS.CFG", tmp); FILE *f = fopen(gp, "r");
      CHECK(f != NULL && sidebars_cfg("change") && !strcmp(sidebars_cfg("change"), "never") && !strcmp(sidebars_cfg("right"), "same"), "SIDEBARS.CFG made, its defaults read");
      if (f) fclose(f); }
    { char gp[160]; sidebars_options_path(af, gp, sizeof gp); CHECK(!strcmp(gp, "/SYSTEM/SIDEBARS/ANTFARM/OPTIONS.CFG"), "the machine's path: %s", gp); }
    #define PUT(path, text) do { FILE *f_ = fopen(path, "w"); fputs(text, f_); fclose(f_); } while (0)
    PUT(p, "speed = 2\nday   = 30m     # half an hour\n");
    CHECK(sidebars_poll() == 1 && sidebars_speed(af) == 2.0 && !strcmp(sidebars_opt(af, "day"), "30m"), "edited: speed 2, day 30m");
    CHECK(sidebars_poll() == 0, "and not read again when nothing changed");
    PUT(p, "speed = 9\n");          sidebars_poll(); CHECK(sidebars_speed(af) == 4.0, "speed 9 is 4");
    PUT(p, "speed = 0.1\n");        sidebars_poll(); CHECK(sidebars_speed(af) == 0.25, "speed 0.1 is 0.25");
    PUT(p, "speed = quick!\n");     sidebars_poll(); CHECK(sidebars_speed(af) == 1.0, "a speed that is not a number is 1");
    CHECK(sidebars_seconds("30m") == 1800 && sidebars_seconds("1h") == 3600 && sidebars_seconds("1d") == 86400 && sidebars_seconds("90") == 90
          && sidebars_seconds("never") == 0 && sidebars_seconds("real") == 0 && sidebars_seconds(NULL) == 0, "durations");

    printf("11. SIDEBARS.CFG: the right side, changing, seasons\n");
    { char gp[800]; snprintf(gp, sizeof gp, "%s/SYSTEM/SIDEBARS/SIDEBARS.CFG", tmp);
      PUT(gp, "right = tetris\n"); sidebars_poll();
      CHECK(sidebars_shown(af, 0, 0, 7) == af && sidebars_shown(af, 1, 0, 7) == tt, "right = tetris");
      PUT(gp, "right = registers\n"); sidebars_poll();
      CHECK(sidebars_shown(af, 1, 0, 7) == af, "right = registers is refused: the panel is not a side");
      PUT(gp, "change = 10m\nseasons = on\n"); sidebars_poll();
      { unsigned seen = 0; for (long t = 0; t < 40 * 600; t += 600) seen |= 1u << sidebars_builtin(sidebars_shown(af, 0, t, 7));
        CHECK(!(seen & (1u << SIDEBAR_REGISTERS)) && !(seen & (1u << SIDEBAR_HALLOWEEN)) && !(seen & (1u << SIDEBAR_CHRISTMAS)) && __builtin_popcount(seen) == 8,
              "July, every 10 minutes: the eight that are not seasonal or the panel (%x)", seen);
        seen = 0; for (long t = 0; t < 40 * 600; t += 600) seen |= 1u << sidebars_builtin(sidebars_shown(af, 0, t, 10));
        CHECK((seen & (1u << SIDEBAR_HALLOWEEN)) && !(seen & (1u << SIDEBAR_CHRISTMAS)), "October: halloween comes round, christmas does not");
        CHECK(sidebars_shown(af, 0, 0, 7) == sidebars_shown(af, 0, 599, 7) && sidebars_shown(af, 0, 0, 7) != sidebars_shown(af, 0, 600, 7), "the next one at ten minutes, not before"); }
      PUT(gp, "change = 10m\nseasons = off\n"); sidebars_poll();
      { unsigned seen = 0; for (long t = 0; t < 40 * 600; t += 600) seen |= 1u << sidebars_builtin(sidebars_shown(af, 0, t, 7));
        CHECK((seen & (1u << SIDEBAR_HALLOWEEN)) && (seen & (1u << SIDEBAR_CHRISTMAS)), "seasons off: all of them"); }
      CHECK(sidebars_shown(sidebars_find("registers"), 0, 12345, 7) == sidebars_find("registers"), "the register panel is never changed away from");
      PUT(gp, "change = never\n"); sidebars_poll(); }

    printf("12. STATE.DAT: the colony across a power cycle\n");
    { static uint32_t x[120 * 540], y[120 * 540]; uint8_t *st, *rb; size_t sn, rn; uint32_t T = 1000 + 200 * 90;
      for (int f = 0; f < 200; f++) for (int side = 0; side < 2; side++) saver_draw(SAVER_ANTFARM, x, 120, 120, 540, 1000 + f * 90, side);
      sn = saver_state(SAVER_ANTFARM, &st);
      CHECK(sn > 0 && sidebars_state_write(af, st, sn) == 0, "saved (%zu bytes)", sn);
      saver_draw(SAVER_ANTFARM, x, 120, 120, 540, T, 0);                          /* what comes next, from the colony as saved */
      for (int f = 1; f < 120; f++) saver_draw(SAVER_ANTFARM, y, 120, 120, 540, T + f * 90, 0);   /* ... and a colony that went on */
      rb = sidebars_state_read(af, &rn);
      CHECK(rb && rn == sn && !memcmp(rb, st, sn), "read back as written");
      saver_restore(SAVER_ANTFARM, rb, rn);
      saver_draw(SAVER_ANTFARM, y, 120, 120, 540, T, 0);
      CHECK(!memcmp(x, y, sizeof x), "the restored colony draws as the saved one did");
      free(st); free(rb);
      snprintf(p, sizeof p, "%s/SYSTEM/SIDEBARS/ANTFARM/STATE.DAT", tmp);
      PUT(p, "K4510 sidebar state: antfarm 99 10\n0123456789");
      rb = sidebars_state_read(af, &rn);
      snprintf(p, sizeof p, "%s/SYSTEM/SIDEBARS/ANTFARM/STATE.OLD", tmp);
      { FILE *f = fopen(p, "r"); CHECK(!rb && f, "another version's state: not read, set aside as STATE.OLD"); if (f) fclose(f); } }
    snprintf(cmd, sizeof cmd, "rm -rf %s", tmp); CHECK(system(cmd) == 0, "clean up %s", tmp);

    printf(fails ? "\n%d FAILED\n" : "\nALL OK\n", fails); return fails != 0;
}
