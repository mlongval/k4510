/* core/idr.c: panel -> canvas -> integer display resolutions.
 *   test/idrtest        the checks
 *   test/idrtest -t     the tables in docs/design-video-foundations.md */
#include <stdio.h>
#include <string.h>
#include "../core/idr.h"

static int fails;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("  FAIL: " __VA_ARGS__); printf("\n"); } } while (0)

/* "WxH/s WxH/s ..." for a panel, to compare against a line */
static const char *listed(int pw, int ph, int base, const idr_limits *lim)
{
    static char buf[512]; idr_t r[IDR_MAX]; int n = idr_list(pw, ph, base, lim, r, IDR_MAX), o = 0;
    buf[0] = 0;
    for (int i = 0; i < n; i++) o += snprintf(buf + o, sizeof buf - (size_t) o, "%s%dx%d/%d", i ? " " : "", r[i].w, r[i].h, r[i].scale);
    return buf;
}
#define LIST(pw, ph, base, lim, want) do { const char *got = listed(pw, ph, base, lim); \
    CHECK(!strcmp(got, want), "%dx%d %s: got \"%s\", want \"%s\"", pw, ph, base == IDR_BASE_43 ? "4:3" : "full", got, want); } while (0)

/* the K/OS text limits today: TCOLS/TROWS and JIM's geometry are bytes, and
 * the console's map is $030000-$03BFFF (12288 text32 cells) */
static const char *fits(int c, int r) { return c > 255 || r > 255 ? "!" : (long) c * r > 12288 ? "*" : ""; }

static void tables(void)
{
    static const int panels[][2] = { { 1024, 768 }, { 1280, 800 }, { 1366, 768 }, { 1600, 900 }, { 1920, 1080 }, { 1920, 1200 },
                                     { 2560, 1440 }, { 2560, 1600 }, { 2880, 1800 }, { 3840, 2160 } };
    idr_limits all = idr_default_limits; all.max_pixels = 1L << 40; all.max_w = all.max_h = 0;   /* every IDR, the cap marked */
    for (int base = 0; base < 2; base++) {
        printf("\n%s base\n\n", base == IDR_BASE_43 ? "4:3" : "Full-panel");
        printf("| Panel | Canvas | IDR | x | 8x8 | 8x16 | 16x16 | 16x32 | HD |\n|---|---|---|---|---|---|---|---|---|\n");
        for (size_t p = 0; p < sizeof panels / sizeof panels[0]; p++) {
            int cw, ch; idr_t r[IDR_MAX];
            idr_canvas(panels[p][0], panels[p][1], base, &cw, &ch);
            int n = idr_list(panels[p][0], panels[p][1], base, &all, r, IDR_MAX);
            for (int i = 0; i < n; i++) {
                int over = (long) r[i].w * r[i].h > idr_default_limits.max_pixels || r[i].w > idr_default_limits.max_w || r[i].h > idr_default_limits.max_h;
                char g[4][24]; static const int cell[4][2] = { { 8, 8 }, { 8, 16 }, { 16, 16 }, { 16, 32 } };
                for (int k = 0; k < 4; k++) { int c, rr; idr_grid(r[i].w, r[i].h, cell[k][0], cell[k][1], &c, &rr); snprintf(g[k], sizeof g[k], "%dx%d%s", c, rr, fits(c, rr)); }
                if (i == 0) printf("| %dx%d | %dx%d ", panels[p][0], panels[p][1], cw, ch); else printf("| | ");
                printf("| %s%dx%d%s | %d | %s | %s | %s | %s | %s |\n", over ? "~~" : "", r[i].w, r[i].h, over ? "~~" : "", r[i].scale,
                       g[0], g[1], g[2], g[3], idr_hd_text(&r[i], NULL) ? (idr_hd_text(&r[i], NULL) == 3 ? "3x" : "yes") : !(r[i].scale & 1) ? "~~yes~~" : "");
            }
        }
    }
}

int main(int argc, char **argv)
{
    if (argc > 1 && !strcmp(argv[1], "-t")) { tables(); return 0; }
    printf("IDR: panel -> canvas -> integer display resolutions\n");
    int cw, ch;
    idr_canvas(1920, 1080, IDR_BASE_43, &cw, &ch); CHECK(cw == 1440 && ch == 1080, "1920x1080 4:3 canvas %dx%d", cw, ch);
    idr_canvas(1280, 800, IDR_BASE_43, &cw, &ch);  CHECK(cw == 1066 && ch == 800, "1280x800 4:3 canvas floors to %dx%d", cw, ch);
    idr_canvas(1024, 1280, IDR_BASE_43, &cw, &ch); CHECK(cw == 1024 && ch == 768, "a portrait panel: the whole width, %dx%d", cw, ch);
    idr_canvas(1920, 1080, IDR_BASE_FULL, &cw, &ch); CHECK(cw == 1920 && ch == 1080, "full canvas is the panel");
    idr_canvas(3, 2, IDR_BASE_43, &cw, &ch); CHECK(cw == 0 && ch == 0, "no panel, no canvas");

    const idr_limits *d = NULL;
    LIST(1920, 1080, IDR_BASE_43, d, "1440x1080/1 720x540/2 480x360/3 360x270/4");
    LIST(1920, 1200, IDR_BASE_43, d, "1600x1200/1 800x600/2 533x400/3 400x300/4 320x240/5");
    LIST(1366, 768,  IDR_BASE_43, d, "1024x768/1 512x384/2 341x256/3");
    LIST(1280, 720,  IDR_BASE_43, d, "960x720/1 480x360/2 320x240/3");
    LIST(1280, 800,  IDR_BASE_43, d, "1066x800/1 533x400/2 355x266/3");
    LIST(2560, 1440, IDR_BASE_43, d, "960x720/2 640x480/3 480x360/4 384x288/5 320x240/6");
    LIST(3840, 2160, IDR_BASE_43, d, "1440x1080/2 960x720/3 720x540/4 576x432/5 480x360/6 411x308/7 360x270/8 320x240/9");
    LIST(3840, 2160, IDR_BASE_FULL, d, "1920x1080/2 1280x720/3 960x540/4 768x432/5 640x360/6 548x308/7 480x270/8 426x240/9 384x216/10");
    LIST(1920, 1080, IDR_BASE_FULL, d, "1920x1080/1 960x540/2 640x360/3 480x270/4 384x216/5");
    LIST(1920, 1200, IDR_BASE_FULL, d, "960x600/2 640x400/3 480x300/4 384x240/5 320x200/6");   /* 1920x1200 is over the cap */
    { idr_limits p2 = idr_default_limits; p2.pow2 = 1;
      LIST(1920, 1080, IDR_BASE_43, &p2, "1440x1080/1 720x540/2 360x270/4");
      LIST(1366, 768,  IDR_BASE_43, &p2, "1024x768/1 512x384/2");
      LIST(3840, 2160, IDR_BASE_43, &p2, "1440x1080/2 720x540/4 360x270/8"); }
    { idr_limits lo = idr_default_limits; lo.min_w = 256; lo.min_h = 192;
      LIST(1366, 768, IDR_BASE_43, &lo, "1024x768/1 512x384/2 341x256/3 256x192/4"); }
    LIST(320, 200, IDR_BASE_FULL, d, "320x200/1");
    LIST(300, 200, IDR_BASE_FULL, d, "");
    { idr_t r[2]; int n = idr_list(3840, 2160, IDR_BASE_43, NULL, r, 2); CHECK(n == 2 && r[1].w == 960, "the list stops at max (%d)", n); }

    int c, r; idr_grid(1440, 1080, 16, 32, &c, &r); CHECK(c == 90 && r == 33, "1440x1080 16x32: %dx%d", c, r);
    idr_grid(720, 540, 8, 8, &c, &r); CHECK(c == 90 && r == 67, "720x540 8x8: %dx%d", c, r);
    idr_grid(1066, 800, 16, 32, &c, &r); CHECK(c == 66 && r == 25, "1066x800 16x32: %dx%d", c, r);
    { idr_t a = { 720, 540, 2 }, b = { 480, 360, 3 }, e = { 1440, 1080, 1 }, f = { 360, 270, 4 }, k = { 1440, 1080, 2 }, m = { 960, 540, 2 };
      CHECK(idr_hd_text(&a, NULL) == 2 && idr_hd_text(&b, NULL) == 3 && !idr_hd_text(&e, NULL) && idr_hd_text(&f, NULL) == 2,
            "HD text at 2x at even scales, 3x at scales three divides (480x360 /3), none at /1");
      { idr_t g5 = { 288, 216, 5 }; CHECK(!idr_hd_text(&g5, NULL), "none at /5: neither divides it"); }
      CHECK(!idr_hd_text(&k, NULL), "HD text in 4K's 1440x1080 would draw 2880x2160: over the cap");
      CHECK(idr_hd_text(&m, NULL), "960x540 HD draws 1920x1080: at the cap, allowed"); }

    printf(fails ? "\n%d FAILED\n" : "idrtest: OK\n", fails);
    return fails != 0;
}
