/* What a frame of pictures costs, per glass and per kind of picture
 * (docs/design-video-foundations.md, 2026-10-07).  No CPU, no ROM: VICKY's
 * registers are set by hand, as vickytest does, and every frame is drawn
 * whole (vicky_dirty forced), which is the worst case -- an idle or still
 * frame costs nothing.  Then the frontend's per-pixel work as sdl/main.c
 * does it on a frame that changed: the still-frame compare and copy of fb,
 * the palette lookup into ARGB, and the bytes a streaming texture carries.
 * The GPU's share (upload, scaling, present) is not here: it is the host's.
 *
 *   test/vidbench [frames]          default 200 frames a case
 *   test/vidbench --suggest [CFG]   this host's pixel cap: the most pixels VICKY
 *                                   should draw a frame here (k4510.cfg's
 *                                   video.cap); with CFG, written into it
 *
 * The suggestion (Doc, 2026-10-07: "a system test that can evaluate the
 * hardware's capabilities and suggest a lower cap to avoid pinning the CPU"):
 * the busiest picture -- a bitmap, a text layer and 64 sprites, every pixel
 * redrawn -- plus the frontend's two passes, timed per pixel at 1440x1080; the
 * cap is what fits in HALF a frame (8.3 ms), the other half left to the CPU
 * and the host.  Rounded down to a size a panel has, 640x480 at least,
 * 1920x1080 at most.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "../core/xemu/emutools_basicdefs.h"
#include "../core/xemu/cpu65.h"
#include "../core/mem.h"
#include "../core/io.h"
#include "../core/vicky.h"

static uint8_t fb[VICKY_WIDTH * VICKY_HEIGHT], last_fb[VICKY_WIDTH * VICKY_HEIGHT];
static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec * 1e-9; }
static void W(int r, uint8_t v) { io_write(IO_VICKY + r, v); }
static void W32(int r, uint32_t v) { for (int i = 0; i < 4; i++) W(r + i, (v >> (8 * i)) & 0xFF); }
static void W16(int r, uint16_t v) { W(r, v & 0xFF); W(r + 1, v >> 8); }
static volatile int sink;
static uint32_t rng = 12345; static uint8_t rnd(void) { rng = rng * 1103515245u + 12345u; return (uint8_t)(rng >> 16); }

#define F8   0x400000u     /* 8x8   */
#define F16  0x401000u     /* 8x16  */
#define F32  0x402000u     /* 16x32 */
#define F16W 0x408000u     /* 16x16 */
#define MAP  0x410000u     /* text32, up to 180x135 cells */
#define BMP  0x1000000u    /* 8 bpp, 1440 wide */
#define SPR  0x1200000u    /* sprite table, then the sprites' data */
static uint8_t stock16[4096], stock8[2048], hd32[16384], hd16[8192];

enum { K_TEXT, K_BITMAP, K_BUSY };
static const char *kind_name[] = { "text", "bitmap", "bitmap+text+64spr" };

static void setup(uint8_t ctrl, int kind, int cell)
{
    for (int n = 0; n < 4; n++) W(VR_LAYER(n) + VL_CTRL, 0);
    W(VR_SPRCTL, 0); W(VR_BGCOL, 6);
    W(VR_CTRL, ctrl);
    int cw = cell >= 2 ? 16 : 8, cols = VICKY_WIDTH / cw;          /* the map is wide enough for any glass */
    int text_layer = kind == K_TEXT ? 0 : 1;
    if (kind != K_TEXT) {
        W32(VR_LAYER(0) + VL_DATA, BMP); W16(VR_LAYER(0) + VL_STRIDE, VICKY_WIDTH);
        W(VR_LAYER(0) + VL_CTRL, 1 | (VL_MODE_BITMAP << 1) | (3 << 3));
    }
    if (kind != K_BITMAP) {
        uint32_t f = cell == 3 ? F32 : cell == 2 ? F16W : cell == 1 ? F16 : F8;
        W32(VR_LAYER(text_layer) + VL_DATA, f); W32(VR_LAYER(text_layer) + VL_MAP, MAP); W16(VR_LAYER(text_layer) + VL_STRIDE, (uint16_t) cols);
        W(VR_LAYER(text_layer) + VL_CTRL, (uint8_t)(1 | (VL_MODE_TEXT32 << 1) | (cell << 5)));
    }
    if (kind == K_BUSY) {
        for (int i = 0; i < 64; i++) {
            uint32_t e = SPR + (uint32_t) i * 16; int x = (i * 97) % 600, y = (i * 53) % 440;
            uint8_t a[16] = { (uint8_t) x, (uint8_t)(x >> 8), (uint8_t) y, (uint8_t)(y >> 8) };
            uint32_t d = SPR + 0x1000 + (uint32_t)(i % 8) * 1024;
            a[4] = (uint8_t) d; a[5] = (uint8_t)(d >> 8); a[6] = (uint8_t)(d >> 16); a[7] = (uint8_t)(d >> 24);
            a[8] = 1 | 2 | (3 << 4); a[9] = 2 | (2 << 2);                         /* on, 8 bpp, over everything; 32x32 */
            mem_load(e, a, 16);
        }
        W32(VR_SPRTAB, SPR); W(VR_SPRCTL, 1);
    }
}

int main(int argc, char **argv)
{
    int suggest = argc > 1 && !strcmp(argv[1], "--suggest");
    int frames = argc > 1 && !suggest ? atoi(argv[1]) : 200;
    if (mem_init()) return 1;
    mem_reset();
    /* fonts, a full map of random cells, a noisy bitmap, eight sprites' data */
    for (int i = 0; i < 2048; i++) { stock8[i] = rnd(); mem_poke(F8 + i, stock8[i]); }
    for (int i = 0; i < 4096; i++) { stock16[i] = rnd(); mem_poke(F16 + i, stock16[i]); }
    for (int i = 0; i < 16384; i++) { hd32[i] = rnd(); mem_poke(F32 + i, hd32[i]); }
    for (int i = 0; i < 8192; i++) { hd16[i] = rnd(); mem_poke(F16W + i, hd16[i]); }
    for (int i = 0; i < 180 * 135; i++) { uint8_t c[4] = { rnd(), 0, (uint8_t)(rnd() & 15), (uint8_t)(rnd() & 15) }; mem_load(MAP + (uint32_t) i * 4, c, 4); }
    { static uint8_t row[VICKY_WIDTH]; for (int y = 0; y < VICKY_HEIGHT; y++) { for (int x = 0; x < VICKY_WIDTH; x++) row[x] = (uint8_t)(16 + ((x ^ y) & 127)); mem_load(BMP + (uint32_t) y * VICKY_WIDTH, row, VICKY_WIDTH); } }
    for (int i = 0; i < 8 * 1024; i++) mem_poke(SPR + 0x1000 + (uint32_t) i, (i & 3) ? rnd() : 0);

    static const struct { const char *name; uint8_t ctrl; int hd; int cell; } glass[] = {
        { "640x480",           1,                 0, 1 },
        { "360x270",           1 | 0x20 | 6 | 16, 0, 0 },
        { "720x540",           1 | 0x20 | 6,      0, 1 },
        { "720x540 HD text",   1 | 0x20 | 6,      1, 1 },
        { "1440x1080",         1 | 0x20,          0, 3 },
    };
    static uint32_t argb[VICKY_WIDTH * VICKY_HEIGHT], pal[256];
    for (int i = 0; i < 256; i++) pal[i] = 0xFF000000u | vicky_palette_rgb(i);
    if (suggest) {
        static const long sizes[] = { 1920L * 1080, 1600L * 1200, 1440L * 1080, 1280L * 960, 1024L * 768, 800L * 600, 640L * 480 };
        double tv = 0, tf = 0; int n = 0;
        setup(1 | 0x20, 2, 3); vicky_hd_font(NULL, stock16, hd16, stock8); vicky_render(fb, VICKY_WIDTH);
        int ow = vicky_out_w(), oh = vicky_out_h();
        for (double t_end = now() + 2.0; now() < t_end; n++) {       /* two seconds of the worst picture */
            vicky_dirty = 1;
            double t0 = now(); vicky_render(fb, VICKY_WIDTH); double t1 = now(); tv += t1 - t0;
            memcpy(last_fb, fb, sizeof fb); sink += memcmp(fb, last_fb, sizeof fb);
            for (int y = 0; y < oh; y++) { const uint8_t *s = fb + y * VICKY_WIDTH; uint32_t *d = argb + y * ow; for (int x = 0; x < ow; x++) d[x] = pal[s[x]]; }
            tf += now() - t1;
        }
        double nspx = (tv + tf) * 1e9 / n / ((double) ow * oh), budget = 1e9 / 60 / 2;
        long fit = (long)(budget / nspx), cap = sizes[sizeof sizes / sizeof *sizes - 1];
        for (size_t i = 0; i < sizeof sizes / sizeof *sizes; i++) if (sizes[i] <= fit) { cap = sizes[i]; break; }
        printf("vidbench: the busiest picture costs %.2f ns a pixel here (VICKY %.2f ms + the frontend %.2f ms at %dx%d)\n",
               nspx, tv * 1000 / n, tf * 1000 / n, ow, oh);
        printf("half a frame (8.3 ms) holds %ld pixels: suggested video.cap = %ld%s\n", fit, cap,
               cap >= 1920L * 1080 ? " (the default: nothing to lower)" : "");
        if (argc > 2) {                                               /* into k4510.cfg: the line replaced, or added */
            FILE *f = fopen(argv[2], "r"); char line[256], out[16384] = ""; size_t o = 0; int done = 0;
            if (f) { while (fgets(line, sizeof line, f) && o + strlen(line) + 40 < sizeof out) {
                         if (!strncmp(line, "video.cap", 9)) { o += (size_t) snprintf(out + o, sizeof out - o, "video.cap = %ld\n", cap); done = 1; }
                         else { strcpy(out + o, line); o += strlen(line); } }
                     fclose(f); }
            if (!done) o += (size_t) snprintf(out + o, sizeof out - o, "video.cap = %ld\n", cap);
            if (!(f = fopen(argv[2], "w")) || fputs(out, f) < 0) { perror(argv[2]); return 1; }
            fclose(f); printf("written to %s: the emulator reads it at its next start\n", argv[2]);
        }
        return 0;
    }
    printf("vidbench: %d frames a case, every frame drawn whole\n", frames);
    printf("%-17s %-18s %9s %8s %9s %9s %8s %8s\n", "glass", "picture", "out px", "vicky ms", "ns/px", "cmp+cp ms", "pal ms", "tex MB");
    for (size_t g = 0; g < sizeof glass / sizeof glass[0]; g++)
        for (int kind = 0; kind < 3; kind++) {
            setup(glass[g].ctrl, kind, glass[g].cell);
            vicky_hd_font(glass[g].hd ? hd32 : NULL, stock16, hd16, stock8);
            vicky_render(fb, VICKY_WIDTH);                         /* latch the glass */
            int ow = vicky_out_w(), oh = vicky_out_h();
            double tv = 0, tc = 0, tp = 0;
            for (int f = 0; f < frames; f++) {
                vicky_dirty = 1;
                double t0 = now(); vicky_render(fb, VICKY_WIDTH); double t1 = now(); tv += t1 - t0;
                /* sdl/main.c's still-frame test: the whole of fb compared, then copied */
                /* worst case: the copy of a changed frame, and a compare that runs to the end */
                t0 = now(); memcpy(last_fb, fb, sizeof fb); sink += memcmp(fb, last_fb, sizeof fb); t1 = now(); tc += t1 - t0;
                t0 = t1;
                for (int y = 0; y < oh; y++) { const uint8_t *s = fb + y * VICKY_WIDTH; uint32_t *d = argb + y * ow; for (int x = 0; x < ow; x++) d[x] = pal[s[x]]; }
                t1 = now(); tp += t1 - t0;
            }
            double k = 1000.0 / frames;
            printf("%-17s %-18s %9d %8.2f %9.2f %9.2f %8.2f %8.2f\n", glass[g].name, kind_name[kind], ow * oh,
                   tv * k, tv * 1e9 / frames / (ow * oh), tc * k, tp * k, ow * oh * 4 / 1048576.0);
        }
    /* The frontend's per-pixel passes at sizes VICKY cannot draw yet: plain
     * memory work, so these are measured, not extrapolated. */
    static const int big[][2] = { { 1440, 1080 }, { 1600, 1200 }, { 1920, 1080 }, { 1920, 1440 }, { 2160, 1620 }, { 2400, 1800 }, { 2880, 2160 }, { 3840, 2160 } };
    printf("\nfrontend passes alone (8-bit fb -> ARGB, compare+copy), ms a frame:\n");
    for (size_t b = 0; b < sizeof big / sizeof big[0]; b++) {
        size_t n = (size_t) big[b][0] * big[b][1];
        uint8_t *a = malloc(n), *c = malloc(n); uint32_t *o = malloc(n * 4);
        for (size_t i = 0; i < n; i++) a[i] = c[i] = (uint8_t) i;
        double tp = 0, tc = 0; int fr = frames / 2 > 20 ? frames / 2 : 20;
        for (int f = 0; f < fr; f++) {
            a[f % 7] ^= 1;
            double t0 = now(); memcpy(c, a, n); sink += memcmp(a, c, n); double t1 = now(); tc += t1 - t0;
            t0 = t1; for (size_t i = 0; i < n; i++) o[i] = pal[a[i]]; t1 = now(); tp += t1 - t0;
        }
        printf("  %4dx%-4d %9zu px   cmp+cp %6.2f   palette %6.2f   texture %5.1f MB\n", big[b][0], big[b][1], n,
               tc * 1000 / fr, tp * 1000 / fr, n * 4 / 1048576.0);
        free(a); free(c); free(o);
    }
    return 0;
}
