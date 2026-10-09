/* The Personality Chooser (Doc, 2026-10-09): which machine this computer is
 * -- the K4510, or one of its personalities (Commodore 64, 128, PET, Amiga
 * 500, 1200, Commander X16; ~/Projects/K4510-Personalities makes them).
 *
 * k4510-session (linux/config/includes.chroot/usr/local/bin) runs it on tty1
 * when a machine is quit, or at power-on when SPACE was held: it prints the
 * name picked and exits 0, or exits 10 for Power off and 11 for Restart.
 * Run from ~/k4510 (the fonts in data/, k4510.cfg, fs/ for the palette):
 *
 *   sdl/k4510-chooser [--last NAME] [--list FILE]
 *
 *   --last NAME   the machine used last: selected, and marked "last"
 *   --list FILE   NAME<TAB>title lines (tests); else `k4510-personality`'s list
 *
 * The K4510's look, read the K4510's way: its palette (k4510.cfg's
 * video.palette, a .PAL applied over the VIC-II sixteen, as the ROM does at
 * reset), its frame and status bands (core/ui/frame.c, the same F12 -> Video
 * -> Frame colour), the banner's five bars, and the machine's 8x8 font
 * (unscii) at a whole multiple, so it is chunky and sharp on any screen: a
 * 320x232 glass with a 12-line band above and below, 320x256 in all: 4x on
 * 1920x1080, 3x on 1024x768 (exactly its 768 lines).
 * Keyboard first: cursor keys, RETURN, 1-9; a gamepad's d-pad and A; the mouse.
 * F1 Power off and F2 Restart, each asked once more before it happens. */
#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "../core/vicky.h"
#include "../core/ui/settings.h"
#include "../core/ui/frame.h"

#define CW 320                                   /* the canvas: the glass and a band above and below */
#define CH 256                                   /* 3x is 768 lines: the 1024x768 machines, exactly */
#define BH 12                                    /* a band: 8-pixel text with 2 above and below */
#define GY BH                                    /* the glass's first line */
#define GH (CH - 2 * BH)                         /* and its height */
#define MAXM 16
#define TILE_Y (GY + 48)                               /* the first machine's line, and each one's height */
#define TILE_H 24
#define TILES 7                                  /* shown at once; more scroll */

static uint32_t cv[CW * CH];                     /* 0x00RRGGBB */
static uint8_t font[2048];
static uint32_t pal[16];
static int fg_i = 7, bg_i = 6;                   /* the console's yellow on blue, unless the .PAL's COLOR says */

typedef struct { char name[32], title[64]; } machine;
static machine m[MAXM];
static int nm;

/* --- the palette, as the machine would have it after a reset ------------- */
static int hexval(const char *s, unsigned *v)
{
    char *e; unsigned long x = strtoul(s, &e, 16);
    if (e == s) return 0;
    *v = (unsigned) x; return 1;
}
static void load_palette(void)
{
    vicky_reset();                               /* the VIC-II sixteen */
    const char *p = settings_palette();
    if (p && *p) {                               /* a machine path, under fs/ */
        char path[300]; snprintf(path, sizeof path, "fs%s%s", *p == '/' ? "" : "/", p);
        FILE *f = fopen(path, "r"); char line[160];
        while (f && fgets(line, sizeof line, f)) {
            char *h = strchr(line, '#'); if (h) *h = 0;
            char a[16], b[16], c[16], d[16]; unsigned i, r, g, bl;
            int n = sscanf(line, "%15s %15s %15s %15s", a, b, c, d);
            if (n >= 2 && !strcasecmp(a, "COLOR")) {         /* the console's colours, as PALETTE LOAD sets them */
                if (hexval(b, &i)) fg_i = i & 15;
                if (n >= 3 && hexval(c, &i)) bg_i = i & 15;
            } else if (n == 4 && hexval(a, &i) && hexval(b, &r) && hexval(c, &g) && hexval(d, &bl) && i < 256) {
                vicky_write(VR_PALIDX, (uint8_t) i); vicky_write(VR_PALR, (uint8_t) r);
                vicky_write(VR_PALG, (uint8_t) g); vicky_write(VR_PALB, (uint8_t) bl);
            }
        }
        if (f) fclose(f);
    }
    for (int i = 0; i < 16; i++) pal[i] = vicky_palette_rgb(i) & 0xFFFFFF;
}

/* --- drawing into the canvas ---------------------------------------------- */
static void fill(int x, int y, int w, int h, uint32_t c)
{
    for (int j = y; j < y + h; j++) for (int i = x; i < x + w; i++)
        if (i >= 0 && j >= 0 && i < CW && j < CH) cv[j * CW + i] = c;
}
static void glyph(int x, int y, unsigned char ch, uint32_t c, int sx, int sy)   /* sx, sy: 1 or 2 */
{
    for (int r = 0; r < 8; r++) {
        uint8_t bits = font[ch * 8 + r];
        for (int b = 0; b < 8; b++) if (bits & (0x80 >> b)) fill(x + b * sx, y + r * sy, sx, sy, c);
    }
}
static int text(int x, int y, const char *s, uint32_t c, int sx, int sy)      /* returns the x after it */
{
    for (; *s; s++, x += 8 * sx) glyph(x, y, (unsigned char) *s, c, sx, sy);
    return x;
}

/* The badges: 16x16, a palette index a pixel (0-9, A-F), '.' the tile showing
 * through.  Our own small pictures of each machine, not anybody's logo. */
static const char *const badge_k4510[16] = {
    "................", "22222222222222..", "22222222222222..", "22222222222222..",
    "8888888888......", "8888888888......", "8888888888......", "7777777.........",
    "7777777.........", "7777777.........", "5555555555......", "5555555555......",
    "5555555555......", "EEEEEEEEEEEEEE..", "EEEEEEEEEEEEEE..", "EEEEEEEEEEEEEE..",
};
static const char *const badge_c64[16] = {      /* the breadbin, from above */
    "................", "................", "................", "................",
    "...9999999999...", "..999999999999..", ".99999999999929.", ".9F9F9F9F9F9F99.",
    ".99F9F9F9F9F9F9.", ".9F9F9F9F9F9F99.", ".99F9F9F9F9F9F9.", ".999FFFFFFF9999.",
    ".99999999999999.", "..999999999999..", "................", "................",
};
static const char *const badge_c128[16] = {     /* the wedge, with its keypad */
    "................", "................", "................", "................",
    "................", ".FFFFFFFFFFFFFF.", ".FCFCFCFCFCFCFC.", ".FFFFFFFFFFFFFF.",
    ".FCFCFCFCFCFCFC.", ".FFFFFFFFFFFFFF.", ".FCFCFCFCFFCFCF.", ".FFFFFFFFFFFFFF.",
    ".FFCCCCCCCFFCFC.", ".FFFFFFFFFFFFFF.", ".CCCCCCCCCCCCCC.", "................",
};
static const char *const badge_pet[16] = {      /* screen, keyboard, one box */
    "..CCCCCCCCCCCC..", ".CFFFFFFFFFFFFC.", ".CF0000000000FC.", ".CF0550555000FC.",
    ".CF0000000000FC.", ".CF0555055500FC.", ".CF0000000000FC.", ".CF0550000000FC.",
    ".CFFFFFFFFFFFFC.", ".CCCCCCCCCCCCCC.", "CFFFFFFFFFFFFFFC", "CF0F0F0F0F0F0FFC",
    "CFF0F0F0F0F0F0FC", "CF0F0F0F0F0F0FFC", "CFFFFFFFFFFFFFFC", "CCCCCCCCCCCCCCCC",
};
static const char *const badge_x16[16] = {      /* an X in the banner's colours */
    "................", ".22..........22.", ".222........222.", "..888......888..",
    "...888....888...", "....777..777....", ".....777777.....", "......7777......",
    "......5555......", ".....555555.....", "....555..555....", "...EEE....EEE...",
    "..EEE......EEE..", ".EEE........EEE.", ".EE..........EE.", "................",
};
static const char *const badge_other[16] = {    /* a screen */
    "................", "..CCCCCCCCCCCC..", "..C0000000000C..", "..C0EE000000C...",
    "..C0000000000C..", "..C0EEEE00000C..", "..C0000000000C..", "..C0EEE000000C..",
    "..C0000000000C..", "..CCCCCCCCCCCC..", "......CCCC......", ".....CCCCCC.....",
    "................", "................", "................", "................",
};
static void badge(int x, int y, const char *const *b)
{
    for (int j = 0; j < 16; j++) for (int i = 0; i < 16; i++) {
        char c = b[j][i];
        if (c != '.') cv[(y + j) * CW + x + i] = pal[(c <= '9' ? c - '0' : c - 'A' + 10) & 15];
    }
}
static void ball(int x, int y, int a, int b)    /* the Amiga's: a chequered ball in colours a and b */
{
    for (int j = 0; j < 16; j++) for (int i = 0; i < 16; i++) {
        double dx = i - 7.5, dy = j - 7.5;
        if (dx * dx + dy * dy > 7.6 * 7.6) continue;
        int sq = (int) ((dx * (1.0 + dx * dx / 160.0)) + 16) / 4 + (int) ((dy * (1.0 + dy * dy / 160.0)) + 16) / 4;   /* bulged squares */
        cv[(y + j) * CW + x + i] = pal[sq & 1 ? a : b];
    }
}
static void draw_badge(int x, int y, const char *name)
{
    if (!strcmp(name, "k4510")) badge(x, y, badge_k4510);
    else if (!strcmp(name, "c64")) badge(x, y, badge_c64);
    else if (!strcmp(name, "c128")) badge(x, y, badge_c128);
    else if (!strcmp(name, "pet")) badge(x, y, badge_pet);
    else if (!strcmp(name, "a500")) ball(x, y, 2, 1);
    else if (!strcmp(name, "a1200")) ball(x, y, 14, 1);
    else if (!strcmp(name, "x16")) badge(x, y, badge_x16);
    else badge(x, y, badge_other);
}

/* --- the machines --------------------------------------------------------- */
static void add(const char *name, const char *title)
{
    if (nm >= MAXM || !*name) return;
    for (int i = 0; i < nm; i++) if (!strcmp(m[i].name, name)) return;
    snprintf(m[nm].name, sizeof m[nm].name, "%s", name);
    snprintf(m[nm].title, sizeof m[nm].title, "%s", title);
    char *p = strstr(m[nm].title, " ("); if (p) *p = 0;     /* "(F12 menu)": a note for the old menu, not a name */
    nm++;
}
static void read_list(const char *file)
{
    static const char *const order[] = { "c64", "c128", "pet", "a500", "a1200", "x16" };
    machine got[MAXM]; int ng = 0; char line[160];
    FILE *f = file ? fopen(file, "r") : popen("k4510-personality 2>/dev/null", "r");
    while (f && fgets(line, sizeof line, f) && ng < MAXM) {
        char *s = line; while (*s == ' ') s++;
        char *t = strchr(s, '\t'); if (!t) continue;
        *t++ = 0; t[strcspn(t, "\r\n")] = 0;
        snprintf(got[ng].name, sizeof got[ng].name, "%.31s", s);
        snprintf(got[ng].title, sizeof got[ng].title, "%.63s", t);
        ng++;
    }
    if (f) { if (file) fclose(f); else pclose(f); }
    add("k4510", "K4510 Fantasy Computer");                   /* always, and first */
    for (unsigned o = 0; o < sizeof order / sizeof *order; o++)
        for (int i = 0; i < ng; i++) if (!strcmp(got[i].name, order[o])) add(got[i].name, got[i].title);
    for (int i = 0; i < ng; i++) add(got[i].name, got[i].title);   /* any others, as listed */
}

/* --- one frame ------------------------------------------------------------ */
enum { F_LIST, F_POWER, F_RESTART };
static int sel, top, focus = F_LIST, confirm, blink;
static const char *last = "";

static void draw(void)
{
    uint32_t frame = frame_rgb(), ftext = frame_text_rgb(frame);
    uint32_t bg = pal[bg_i], fg = pal[fg_i], hi = pal[1], bar = pal[14], dim = pal[15];
    for (int i = 0; i < CW * CH; i++) cv[i] = frame;
    fill(0, GY, CW, GH, bg);

    /* the top band: what this is, and the time, as the K4510's bands have them */
    text(8, 2, "PERSONALITIES", ftext, 1, 1);
    { char t[32]; time_t now = time(NULL); struct tm *tm = localtime(&now);
      strftime(t, sizeof t, "%H:%M %d.%m.%Y", tm); text(CW - 8 - 8 * (int) strlen(t), 2, t, ftext, 1, 1); }

    /* the banner: its five bars, 4:3:2:3:4, and the name */
    static const int bw[5] = { 64, 48, 32, 48, 64 }, bc[5] = { 2, 8, 7, 5, 14 };
    for (int i = 0; i < 5; i++) fill(8, GY + 4 + i * 8, bw[i], 8, pal[bc[i]]);
    text(88, GY + 4, "K4510", hi, 2, 2);
    text(88, GY + 28, "Choose a machine", fg, 1, 1);

    /* the machines, a tile each */
    if (sel < top) top = sel;
    if (sel >= top + TILES) top = sel - TILES + 1;
    for (int k = 0; k < TILES && top + k < nm; k++) {
        int i = top + k, y = TILE_Y + k * TILE_H, on = focus == F_LIST && i == sel;
        uint32_t tc = on ? bg : fg;
        if (on) fill(8, y, CW - 16, TILE_H - 2, bar);
        if (i < 9) { char d[2] = { (char) ('1' + i), 0 }; fill(14, y + 5, 12, 12, on ? bg : bar); text(16, y + 7, d, on ? bar : bg, 1, 1); }
        draw_badge(32, y + 3, m[i].name);
        int x = text(56, y + 3, m[i].title, on ? bg : (i == sel ? hi : tc), 1, 2);
        if (on && blink) fill(x + 2, y + 3, 8, 16, bg);              /* the K4510's cursor, blinking */
        if (!strcmp(m[i].name, last)) text(CW - 16 - 32, y + 7, "last", on ? bg : dim, 1, 1);
    }
    if (top > 0) text(CW - 16, TILE_Y - 10, "\x18", dim, 1, 1);
    if (top + TILES < nm) text(CW - 16, TILE_Y + TILES * TILE_H - 2, "\x19", dim, 1, 1);
    text(16, GY + GH - 11, "1-9 or cursor keys, RETURN to start", dim, 1, 1);

    /* the bottom band: Power off and Restart, and the question before either */
    if (confirm) {
        const char *q = confirm == F_POWER ? " Power off? RETURN yes, ESC no " : " Restart? RETURN yes, ESC no ";
        fill(8, CH - BH + 1, 8 * (int) strlen(q), BH - 2, ftext);
        text(8, CH - BH + 2, q, frame, 1, 1);
    } else {
        static const char *const it[2] = { " F1 Power off ", " F2 Restart " };
        int x = 8;
        for (int i = 0; i < 2; i++) {
            int on = focus == F_POWER + i, w = 8 * (int) strlen(it[i]);
            if (on) fill(x, CH - BH + 1, w, BH - 2, ftext);
            text(x, CH - BH + 2, it[i], on ? frame : ftext, 1, 1);
            x += w + 8;
        }
    }
}

int main(int argc, char **argv)
{
    const char *list = NULL;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--last") && i + 1 < argc) last = argv[++i];
        else if (!strcmp(argv[i], "--list") && i + 1 < argc) list = argv[++i];
        else { fprintf(stderr, "usage: k4510-chooser [--last NAME] [--list FILE]\n"); return 2; }
    }
    FILE *f = fopen("data/fonts/unscii/font8-unscii.bin", "rb");
    if (!f || fread(font, 1, sizeof font, f) != sizeof font) { fprintf(stderr, "k4510-chooser: need data/fonts/unscii/font8-unscii.bin (run from ~/k4510)\n"); return 2; }
    fclose(f);
    settings_load("k4510.cfg");
    load_palette();
    read_list(list);
    for (int i = 0; i < nm; i++) if (!strcmp(m[i].name, last)) sel = i;

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER) != 0) { fprintf(stderr, "k4510-chooser: SDL: %s\n", SDL_GetError()); return 2; }
    SDL_Window *win = SDL_CreateWindow("K4510 Personalities", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, 1024, 768, SDL_WINDOW_FULLSCREEN_DESKTOP);
    SDL_Renderer *ren = win ? SDL_CreateRenderer(win, -1, SDL_RENDERER_PRESENTVSYNC) : NULL;
    if (!ren && win) ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE);
    SDL_Texture *tex = ren ? SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, CW, CH) : NULL;
    if (!tex) { fprintf(stderr, "k4510-chooser: SDL: %s\n", SDL_GetError()); SDL_Quit(); return 2; }
    SDL_ShowCursor(SDL_DISABLE);
    for (int i = 0; i < SDL_NumJoysticks(); i++) if (SDL_IsGameController(i)) SDL_GameControllerOpen(i);

    /* SPACE still down from the splash must not start anything: ignored until let go */
    SDL_PumpEvents();
    int space_held = SDL_GetKeyboardState(NULL)[SDL_SCANCODE_SPACE];
    int result = -1;                             /* a machine's index, or 10 / 11 */
    Uint32 t0 = SDL_GetTicks();
    while (result < 0) {
        int W, H; SDL_GetRendererOutputSize(ren, &W, &H);
        int s = W / CW < H / CH ? W / CW : H / CH; if (s < 1) s = 1;
        SDL_Rect dst = { (W - CW * s) / 2, (H - CH * s) / 2, CW * s, CH * s };
        blink = (SDL_GetTicks() / 400) & 1;
        draw();
        uint32_t frame = frame_rgb();
        SDL_UpdateTexture(tex, NULL, cv, CW * 4);
        SDL_SetRenderDrawColor(ren, frame >> 16, (frame >> 8) & 255, frame & 255, 255);
        SDL_RenderClear(ren);                    /* the frame, out to the screen's edges */
        SDL_RenderCopy(ren, tex, NULL, &dst);
        SDL_RenderPresent(ren);

        SDL_Event e;
        if (!SDL_WaitEventTimeout(&e, 100)) continue;
        do {
            int key = 0;                         /* the event, as a key */
            if (e.type == SDL_QUIT) result = 0, sel = 0;       /* closed: the K4510 */
            else if (e.type == SDL_KEYUP && e.key.keysym.scancode == SDL_SCANCODE_SPACE) space_held = 0;
            else if (e.type == SDL_KEYDOWN) {
                key = e.key.keysym.sym;
                if (key == SDLK_SPACE && (space_held || e.key.repeat)) key = 0;
            } else if (e.type == SDL_CONTROLLERBUTTONDOWN) {
                switch (e.cbutton.button) {
                case SDL_CONTROLLER_BUTTON_DPAD_UP: key = SDLK_UP; break;
                case SDL_CONTROLLER_BUTTON_DPAD_DOWN: key = SDLK_DOWN; break;
                case SDL_CONTROLLER_BUTTON_DPAD_LEFT: key = SDLK_LEFT; break;
                case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: key = SDLK_RIGHT; break;
                case SDL_CONTROLLER_BUTTON_A: case SDL_CONTROLLER_BUTTON_START: key = SDLK_RETURN; break;
                case SDL_CONTROLLER_BUTTON_B: key = SDLK_ESCAPE; break;
                }
            } else if (e.type == SDL_CONTROLLERDEVICEADDED) {
                SDL_GameControllerOpen(e.cdevice.which);
            } else if ((e.type == SDL_MOUSEMOTION && (e.motion.xrel || e.motion.yrel) && SDL_GetTicks() - t0 > 500)
                       || e.type == SDL_MOUSEBUTTONDOWN) {    /* a mouse that moves: not where the pointer happens to sit at the start */
                int mx = e.type == SDL_MOUSEMOTION ? e.motion.x : e.button.x, my = e.type == SDL_MOUSEMOTION ? e.motion.y : e.button.y;
                SDL_ShowCursor(SDL_ENABLE);
                int cx = (mx - dst.x) / s, cy = (my - dst.y) / s, k = (cy - TILE_Y) / TILE_H;
                if (!confirm && cy >= TILE_Y && k < TILES && top + k < nm && cx >= 8 && cx < CW - 8) {
                    focus = F_LIST; sel = top + k;
                    if (e.type == SDL_MOUSEBUTTONDOWN) key = SDLK_RETURN;
                } else if (cy >= CH - BH && cy < CH && e.type == SDL_MOUSEBUTTONDOWN) {
                    if (confirm) key = SDLK_RETURN;
                    else if (cx >= 8 && cx < 8 + 14 * 8) { focus = F_POWER; key = SDLK_RETURN; }
                    else if (cx >= 8 + 15 * 8 && cx < 8 + 27 * 8) { focus = F_RESTART; key = SDLK_RETURN; }
                }
            }
            if (!key) continue;
            if (confirm) {                       /* the one question: RETURN yes, anything else no */
                if (key == SDLK_RETURN || key == SDLK_KP_ENTER) result = confirm == F_POWER ? 10 : 11;
                confirm = 0;
                continue;
            }
            if (key == SDLK_F1) { focus = F_POWER; confirm = F_POWER; }
            else if (key == SDLK_F2) { focus = F_RESTART; confirm = F_RESTART; }
            else if (key >= SDLK_1 && key <= SDLK_9) { if (key - SDLK_1 < nm) { focus = F_LIST; sel = key - SDLK_1; result = sel; } }
            else if (key >= SDLK_KP_1 && key <= SDLK_KP_9) { if (key - SDLK_KP_1 < nm) { focus = F_LIST; sel = key - SDLK_KP_1; result = sel; } }
            else if (key == SDLK_UP) { if (focus != F_LIST) focus = F_LIST; else if (sel > 0) sel--; else sel = nm - 1; }
            else if (key == SDLK_DOWN) { if (focus == F_LIST) { if (sel < nm - 1) sel++; else focus = F_POWER; } }
            else if (key == SDLK_LEFT) { if (focus == F_RESTART) focus = F_POWER; }
            else if (key == SDLK_RIGHT) { if (focus == F_POWER) focus = F_RESTART; }
            else if (key == SDLK_HOME) { focus = F_LIST; sel = 0; }
            else if (key == SDLK_END) { focus = F_LIST; sel = nm - 1; }
            else if (key == SDLK_RETURN || key == SDLK_KP_ENTER || key == SDLK_SPACE) {
                if (focus == F_LIST) result = sel; else confirm = focus;
            } else if (key == SDLK_ESCAPE) focus = F_LIST;
        } while (result < 0 && SDL_PollEvent(&e));
    }
    SDL_DestroyTexture(tex); SDL_DestroyRenderer(ren); SDL_DestroyWindow(win); SDL_Quit();
    if (result >= 10) return result;
    printf("%s\n", m[result].name);
    return 0;
}
