/* K4510 desktop frontend -- spike version.
 *
 * SDL2 window, 60 Hz. Each frame: run the 45GS10 for a frame's worth of
 * cycles, feed keys into the keyboard register, let VICKY render screen
 * RAM. The ROM (Wozmon) does everything else.
 */
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif
#include <SDL.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>   /* access(): is this K4510x? */
#include "../core/xemu/emutools_basicdefs.h"
#include "../core/xemu/cpu65.h"
#include "../core/mem.h"
#include "../core/io.h"
#include "../core/vicky.h"
#include "../core/build.h"   /* K4510_BUILD, for the frame profile */
#include "../core/audio.h"
#include "../core/opl2.h"
#include "../core/sndq.h"
#include "../core/host.h"
#include "../core/ui/settings.h"
#include "../core/hostid.h"
#include "../core/ui/menu.h"
#include "../core/ui/ui_draw.h"
#include "../core/state.h"
#include <sys/stat.h>
#include <time.h>

#define SCALE 2
#define AUDIO_RATE 48000

/* Audio: core renders into a ring per scanline; SDL drains it in its thread.
 *
 * RING_TARGET is the lead the ring is kept at -- one callback, plus a frame,
 * so a late frame does not starve the device.  RING_CAP is the other side of
 * it, and it was missing: the writers would fill to RING_MASK, 683 ms, and
 * anything that made the machine produce sound slightly faster than the
 * device consumed it walked the lead up there and stayed (four sounding
 * SIDs did exactly that, in the days the machine had them: 56 ms of lead to
 * 226 ms in 38 seconds and still climbing).  The chip is clocked either
 * way -- pitch is its own and does not move -- but past the cap the samples
 * are let go, so the lead cannot drift late however the two rates disagree. */
static int16_t ring[1 << 15]; static volatile unsigned ring_w, ring_h;
#define RING_MASK ((1 << 15) - 1)
#ifdef K4510_PI
#define RING_TARGET (1536 + 800)          /* the Pi's device runway is longer */
#else
#define RING_TARGET (1024 + 800)          /* one callback, plus a frame */
#endif
#define RING_CAP    (RING_TARGET + 800)   /* a frame of slack above the lead */
#define RING_DEPTH  ((ring_w - ring_h) & RING_MASK)
static void audio_cb(void *ud, Uint8 *stream, int len)
{
    (void)ud; int16_t *out = (int16_t *)stream; int n = len / 2;
    int gap = 0;
    for (int i = 0; i < n; i++) { if (ring_h != ring_w) out[i] = ring[ring_h++ & RING_MASK]; else { out[i] = 0; gap = 1; } }
    if (gap && io_audio_gaps != 0xFFFF) io_audio_gaps++;     /* one per callback that ran dry: what "choppy" is, counted */
}
#define CPU_HZ 40500000           /* MEGA65-class; the ceiling is ours, per the design */
/* the emulated clock is a setting (cpu.clock): full on the desktop, 20 MHz on
 * the Pi by default, where the whole machine would otherwise run at 20 fps */
static unsigned cpu_hz_now = CPU_HZ, cycles_per_line = CPU_HZ / 60 / VICKY_HEIGHT;
/* what the guest reads at SYS+$36: the wall clock, not the frame count */
static uint32_t sdl_ms_now(void) { return (uint32_t)SDL_GetTicks(); }
/* the governor steps down above this much of the frame spent inside the
 * machine: 14 ms of 16.67 leaves the frontend its texture and its present,
 * and a machine costing more than that is not holding 60 frames a second */
#define GOV_LATE_MS 14.0
/* The next step down the ladder, by clock rather than by index: the enum's
 * order is the menu's business and has been changed once already. */
static int clock_step_below(int cur)
{
    unsigned cur_hz = settings_cpu_hz_of(cur), best_hz = 0; int best = -1;
    for (int i = 0; i < CPUCLK_COUNT; i++) {
        unsigned h = settings_cpu_hz_of(i);
        if (h < cur_hz && h > best_hz) { best_hz = h; best = i; }
    }
    return best;
}
#define CYCLES_PER_LINE  cycles_per_line
/* Another core, while it owns the sound (core/sndq.h): the same top-up the
 * frame loop does, with the queued register writes performed as it passes
 * their moment.  It is the only writer of the ring while it owns it, which is
 * what makes the ring's single-producer rule hold across the handover. */
void k4510_audio_pump(void)
{
    int vol = settings_get(SET_AUDIO_VOLUME), guard = 4096;
    while (RING_DEPTH < RING_TARGET && guard--) {
        int16_t tmp[256];
        audio_drain_to(sndq_now());
        int n = audio_render(CYCLES_PER_LINE, tmp, 256);
        for (int i = 0; i < n; i++)
            if (RING_DEPTH < RING_CAP) ring[ring_w++ & RING_MASK] = (int16_t)(tmp[i] * vol / 100);
    }
}


static int load_file(const char *path, uint8_t *buf, size_t max)
{
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    size_t n = fread(buf, 1, max, f);
    fclose(f);
    return (int)n;
}

/* A PETSCII chargen (512 glyphs, both sets) rearranged into the ASCII/CP437
 * order the ROM prints in: letters, digits and punctuation from the
 * lower-case set, the box glyphs the ROM and JIM use from the graphics. */
static void petscii_to_ascii(const uint8_t *cg, uint8_t *out)
{
    const uint8_t *lo = cg + 2048;                      /* set 2: upper/lower case */
    static const struct { uint8_t ascii, glyph; } box[] = {
        { 0xC4, 0x40 }, { 0xB3, 0x5D }, { 0xDA, 0x70 }, { 0xBF, 0x6E }, { 0xC0, 0x6D }, { 0xD9, 0x7D },
        { 0xC3, 0x6B }, { 0xB4, 0x73 }, { 0xC5, 0x5B }, { 0xC1, 0x71 }, { 0xC2, 0x72 }, { 0xDB, 0xE0 },
        { 0xB0, 0x66 }, { 0xB1, 0x66 }, { 0xB2, 0x66 }, { 0xCD, 0x40 }, { 0xBA, 0x5D }, { 0xC9, 0x70 },
        { 0xBB, 0x6E }, { 0xC8, 0x6D }, { 0xBC, 0x7D }, { 0xFB, 0xBA }, { 0x10, 0x3E }, { 0x1B, 0x3C } };
    memset(out, 0, 2048);
    for (int c = 0x20; c < 0x80; c++) {
        int g;
        if (c < 0x40) g = c;                                /* punctuation and digits: same codes */
        else if (c == 0x40) g = 0;                          /* @ */
        else if (c <= 0x5A) g = c;                          /* A-Z at 65-90 in the lower-case set */
        else if (c == 0x5B) g = 0x1B; else if (c == 0x5C) g = 0x1C; else if (c == 0x5D) g = 0x1D;
        else if (c == 0x5E) g = 0x1E; else if (c == 0x5F) g = 0x64;   /* ^ as the up arrow, _ as the low bar */
        else if (c == 0x60) g = 0x27;                       /* ` as ' */
        else if (c <= 0x7A) g = c - 0x60;                   /* a-z at 1-26 */
        else if (c == 0x7B) g = 0x73; else if (c == 0x7C) g = 0x5D; else if (c == 0x7D) g = 0x6B; else g = 0x40;
        memcpy(out + c * 8, lo + g * 8, 8);
    }
    for (unsigned i = 0; i < sizeof box / sizeof box[0]; i++) memcpy(out + box[i].ascii * 8, lo + box[i].glyph * 8, 8);
}
static uint8_t font_kernel8[2048], font_menu[2048];
static const char *slot_path(int n) { static char p[32]; snprintf(p, sizeof p, "k4510-slot%d.k4s", n + 1); return p; }
static void slot_refresh(int n)                      /* the slot's row: its file's date, or "empty" */
{
    struct stat st; char b[24];
    if (stat(slot_path(n), &st)) { menu_slot(n, ""); return; }
    struct tm *tm = localtime(&st.st_mtime);
    if (tm) strftime(b, sizeof b, "%b %d %H:%M", tm); else snprintf(b, sizeof b, "%ld KB", (long)(st.st_size >> 10));
    menu_slot(n, b);
}
/* The C64 chargen lives in the machine's own filesystem, not the host's data/:
 * drop chargen.bin into /SYSTEM and the menu can wear it. 4096 bytes, PETSCII
 * order, so the same converter the open-roms chargens use rearranges it. */
static void chargen_path(char *out, int max) { snprintf(out, (size_t) max, "%s/SYSTEM/chargen.bin", fs_get_root()); }
static int chargen_present(void)
{
    char p[512]; chargen_path(p, sizeof p);
    FILE *f = fopen(p, "rb"); if (!f) return 0;
    fseek(f, 0, SEEK_END); long n = ftell(f); fclose(f);
    return n >= 2048;
}
static void apply_font(int which)
{
    static const char *paths[FONT_COUNT] = { "data/font8.bin", "data/fonts/unscii/font8-unscii.bin",
                                             "data/fonts/openroms/chargen_openroms.rom", "data/fonts/openroms/chargen_pxlfont_2.3.rom", 0 /* FONT_CHARGEN: from the guest fs */,
                                             "data/fonts/zx/bauhaus.bin", "data/fonts/zx/broadway.bin", "data/fonts/zx/computer.bin", "data/fonts/zx/cyberwire.bin",
                                             "data/fonts/zx/nlq.bin", "data/fonts/zx/benguiat.bin", "data/fonts/zx/chicago.bin", "data/fonts/zx/courier.bin",
                                             "data/fonts/zx/eurostile.bin", "data/fonts/zx/ocr-a.bin", "data/fonts/zx/pristine.bin", "data/fonts/zx/anvil.bin" };
    char cg[512]; const char *path = paths[which];
    if (which == FONT_CHARGEN) { chargen_path(cg, sizeof cg); path = cg; }
    uint8_t buf[4096], font[2048]; int n = which ? load_file(path, buf, sizeof buf) : 0;
    if (which == FONT_KERNEL8 || n < 2048) memcpy(font, font_kernel8, 2048);
    else if (n == 4096) petscii_to_ascii(buf, font);
    else memcpy(font, buf, 2048);
    /* An alternate font covers what it covers; every glyph it leaves blank is
     * taken from the kernel font instead of showing as a hole.  A PETSCII
     * chargen brings ~120 characters, a ZX font 96: without this, most of
     * CP437 -- box drawing, shading, accents -- vanished with the font swap
     * (a blank stays blank only where the kernel glyph is blank too: space). */
    if (which != FONT_KERNEL8)
        for (int c = 0; c < 256; c++) {
            uint8_t *g = font + c * 8; int ink = 0;
            for (int i = 0; i < 8; i++) if (g[i]) { ink = 1; break; }
            if (!ink) memcpy(g, font_kernel8 + c * 8, 8);
        }
    mem_load(K4510_FONT8_PHYS, font, 2048);
}

/* The keyboard when SDL sends no text.
 *
 * A printable character normally arrives as SDL_TEXTINPUT, already composed by
 * the host's layout.  On K4510x that never happens: the appliance draws
 * straight on the screen (SDL_VIDEODRIVER=kmsdrm) and SDL's own evdev keyboard
 * gives us key codes and no text at all, so the F7 menu -- which is arrow keys
 * and Enter -- worked while nothing could be TYPED (Doc, on the T480,
 * 2026-09-07: "the keyboard is unresponsive outside of the menu").
 *
 * So every printable key press is remembered as it comes, and any TEXTINPUT
 * that follows cancels it; whatever is still pending when the frame's events
 * run out is typed from the key code instead.  On a desktop the text always
 * arrives and this costs nothing; where it never arrives the keyboard works,
 * one frame late and in the American arrangement, which is the arrangement the
 * key codes are named in.  An accented layout still needs the text events. */
static uint8_t key_ascii(SDL_Keycode k, int shift)
{
    static const char plain[]   = "`1234567890-=[]\\;',./";
    static const char shifted[] = "~!@#$%^&*()_+{}|:\"<>?";
    const char *c;
    if (k >= SDLK_a && k <= SDLK_z) return (uint8_t)(shift ? k - 'a' + 'A' : k);
    if (k == SDLK_SPACE) return ' ';
    if (k >= SDLK_KP_1 && k <= SDLK_KP_9) return (uint8_t)('1' + (k - SDLK_KP_1));
    if (k == SDLK_KP_0) return '0';
    switch (k) {
    case SDLK_KP_PERIOD: return '.';  case SDLK_KP_DIVIDE: return '/';
    case SDLK_KP_MULTIPLY: return '*'; case SDLK_KP_MINUS: return '-';
    case SDLK_KP_PLUS: return '+';
    default: break;
    }
    c = strchr(plain, (int)k);
    if (k && c && *c) return (uint8_t)(shift ? shifted[c - plain] : k);
    return 0;
}

/* Unicode -> code page 437, for the half of the machine's font above ASCII.
 * Only the letters and marks a keyboard can actually produce are here; the box
 * drawing has no key.  0 means "this machine cannot show it". */
static uint8_t cp437_of(unsigned long cp)
{
    static const unsigned short u[] = {
        0x00C7,0x00FC,0x00E9,0x00E2,0x00E4,0x00E0,0x00E5,0x00E7,0x00EA,0x00EB,
        0x00E8,0x00EF,0x00EE,0x00EC,0x00C4,0x00C5,0x00C9,0x00E6,0x00C6,0x00F4,
        0x00F6,0x00F2,0x00FB,0x00F9,0x00FF,0x00D6,0x00DC,0x00A2,0x00A3,0x00A5,
        0x20A7,0x0192,0x00E1,0x00ED,0x00F3,0x00FA,0x00F1,0x00D1,0x00AA,0x00BA,
        0x00BF };
    unsigned i;
    for (i = 0; i < sizeof u / sizeof u[0]; i++) if (u[i] == cp) return (uint8_t)(0x80 + i);
    switch (cp) {                                   /* the stragglers, out of order in CP437 */
    case 0x00AC: return 0xAA;  case 0x00BD: return 0xAB;  case 0x00BC: return 0xAC;
    case 0x00A1: return 0xAD;  case 0x00AB: return 0xAE;  case 0x00BB: return 0xAF;
    case 0x00DF: return 0xE1;  case 0x00B5: return 0xE6;  case 0x00B1: return 0xF1;
    case 0x00F7: return 0xF6;  case 0x00B0: return 0xF8;  case 0x00B7: return 0xFA;
    case 0x00B2: return 0xFD;  case 0x00A0: return 0x20;  default: return 0;
    }
}

#ifndef K4510_PI
/* The mouse.  SDL hands us logical coordinates (the renderer's logical size
 * is the machine's picture, x2 with scanlines), so machine pixels are one
 * division and the border's shrink away.  geo_k/geo_b are copied from the
 * frame code each frame; the picture cannot move between them. */
static int geo_k = 1, geo_b = 0;
static int mouse_x = -1, mouse_y = -1, mouse_btn, wheel_acc, dx_acc, dy_acc;
static int to_machine(int v, int full) { int m = (v / geo_k - geo_b) * full / (full - 2 * geo_b); return m < 0 ? 0 : m >= full ? full - 1 : m; }
static void mouse_to_menu(void) { if (menu_is_open() && mouse_x >= 0) menu_mouse(mouse_x, mouse_y, mouse_btn, wheel_acc); }
/* Mouse capture: a click on the picture confines the host pointer to the
 * window (the coordinates stay absolute, so CHESS still clicks squares);
 * opening the menu, or the window losing focus, lets it go, and closing the
 * menu takes it back.  Not in the browser: SDL's pointer lock there is
 * relative-only and would stop the position registers. */
static SDL_Window *grab_win; static int grabbed, grab_wanted;
static void grab(int on)
{
#ifndef __EMSCRIPTEN__
    if (!grab_win || on == grabbed) return;
    SDL_SetWindowMouseGrab(grab_win, on ? SDL_TRUE : SDL_FALSE);
    grabbed = on;
#else
    (void) on;
#endif
}
static SDL_GameController *pad;
static void pad_open(int idx)
{
    if (pad) return;
    pad = SDL_GameControllerOpen(idx);
    if (pad) fprintf(stderr, "gamepad: %s\n", SDL_GameControllerName(pad));
}
/* the pad's part of $D104: d-pad or left stick -> the four directions,
 * A/Y/right trigger -> FIRE (space), X/left shoulder -> A (Z),
 * B/right shoulder -> B (X) -- so LODE digs left and right on the shoulders */
static uint8_t pad_held(void)
{
    uint8_t h = 0; if (!pad) return 0;
#define PB(b) SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_##b)
#define PA(a) SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_##a)
    const int dz = 10000;                            /* stick dead zone, of 32767 */
    if (PB(DPAD_UP)    || PA(LEFTY) < -dz) h |= HELD_UP;
    if (PB(DPAD_DOWN)  || PA(LEFTY) >  dz) h |= HELD_DOWN;
    if (PB(DPAD_LEFT)  || PA(LEFTX) < -dz) h |= HELD_LEFT;
    if (PB(DPAD_RIGHT) || PA(LEFTX) >  dz) h |= HELD_RIGHT;
    if (PB(A) || PB(Y) || PA(TRIGGERRIGHT) > dz) h |= HELD_FIRE;
    if (PB(X) || PB(LEFTSHOULDER))  h |= HELD_A;
    if (PB(B) || PB(RIGHTSHOULDER)) h |= HELD_B;
#undef PB
#undef PA
    return h;
}
#endif

int k4510_frontend_main(int argc, char **argv)
{
    /* --no-startup.bat: skip /STARTUP.BAT for this run only.  The F7 switch
     * does the same thing but persists, and holding a key at the banner needs
     * you to be there -- neither suits a script, or the case where a startup
     * file wedges the machine and you want one clean boot to go and fix it. */
    int no_startup = 0;
    { int i, j;
      for (i = 1; i < argc; i++)
          if (!strcmp(argv[i], "--no-startup.bat") || !strcmp(argv[i], "--no-startup")) {
              no_startup = 1;
              for (j = i; j < argc - 1; j++) argv[j] = argv[j + 1];   /* out of the way of the positional arguments */
              argc--; i--;
          } }
    if (getenv("K4510_NO_STARTUP")) no_startup = 1;          /* the same thing, for a script that sets it once */
    /* --host-shell: let `!` at the prompt run the host's shell on the Tube.
     * K4510x turns it on (the Linux there is the user's); a plain desktop or
     * the Pi never does, and `!` there says "no host shell on this machine". */
    { int i, j;
      for (i = 1; i < argc; i++)
          if (!strcmp(argv[i], "--host-shell")) {
              io_host_shell = 1;
              for (j = i; j < argc - 1; j++) argv[j] = argv[j + 1];
              argc--; i--;
          } }
    if (getenv("K4510_HOST_SHELL")) io_host_shell = 1;
    const char *rom = (argc > 1) ? argv[1] : "rom/kernal.bin";
    const char *cfg = "k4510.cfg";
    if (argc > 2) fs_set_root(argv[2]);
    if (load_file("data/font8.bin", font_kernel8, sizeof font_kernel8) != sizeof font_kernel8) {
        fprintf(stderr, "need data/font8.bin (run from repo root)\n");
        return 1;
    }
    if (load_file("data/fonts/unscii/font8-unscii.bin", font_menu, sizeof font_menu) != sizeof font_menu) memcpy(font_menu, font_kernel8, sizeof font_menu);
    ui_font(font_menu);                                  /* the menu's own font: it must draw whatever the guest did */
    settings_load(cfg);
    if (mem_init() != 0) { fprintf(stderr, "cannot reserve %u MB\n", K4510_PHYS_SIZE >> 20); return 1; }
    settings_label(SET_VIDEO_FONT, FONT_CHARGEN, chargen_present() ? "C64 chargen" : "C64 chargen (none)");
    int font_applied = settings_get(SET_VIDEO_FONT); apply_font(font_applied);   /* the ROM points VICKY at $010000 */
    for (int i = 0; i < MENU_SLOTS; i++) slot_refresh(i);
    menu_info(INFO_VERSION, "k4510 0.3"); menu_info(INFO_ROM, rom); menu_info(INFO_FS, argc > 2 ? argv[2] : "fs");
#ifdef K4510_PI
    menu_info(INFO_HOST, "Raspberry Pi 3B+, Circle");
#else
    menu_info(INFO_HOST, "desktop, SDL2");
#endif


    if (mem_load_rom(rom) <= 0) {
        fprintf(stderr, "cannot load ROM %s\n", rom);
        return 1;
    }
    cpu65_reset();

    io_set_ms_source(sdl_ms_now);              /* SYS+$36: the wall clock the guest can pace against */
    cpu_hz_now = settings_cpu_hz(); cycles_per_line = cpu_hz_now / 60 / VICKY_HEIGHT; io_set_cpu_khz(cpu_hz_now / 1000);
    audio_init((double)cpu_hz_now, AUDIO_RATE);
#ifdef __EMSCRIPTEN__
    SDL_SetHint(SDL_HINT_EMSCRIPTEN_KEYBOARD_ELEMENT, "#canvas");   /* the keys are the canvas's once it is clicked, F-keys included */
#endif
#ifndef K4510_PI
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER) != 0) { fprintf(stderr, "SDL: %s\n", SDL_GetError()); return 1; }
#else
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) != 0) { fprintf(stderr, "SDL: %s\n", SDL_GetError()); return 1; }
#endif
    /* A USB gamepad or joystick, if one is plugged in (now or later): SDL's
     * controller layer knows the common ones (Xbox, PlayStation, 8BitDo,
     * Logitech) by their ids and gives every one the same buttons, so the
     * machine sees one thing -- the $D104 held-keys register, OR'd with the
     * keyboard.  Hot-plug: the first pad to appear is the one; unplug it and
     * the next one to appear takes over.  Nothing to configure. */
#ifndef K4510_PI
    { int n = SDL_NumJoysticks(); for (int i = 0; i < n && !pad; i++) if (SDL_IsGameController(i)) pad_open(i); }
#endif
    /* The machine has no mouse -- no pointer, nothing to click, not one byte
     * of mouse in the I/O map -- so a cursor sitting on the glass is never
     * anything but wrong.  It showed up as a white arrow parked in the top
     * left corner on K4510x, where KMSDRM draws one because there is no
     * desktop to own it.  Hidden everywhere: in a window the pointer is still
     * there for the frame and the title bar, it just stops being drawn over
     * the picture. */
    SDL_ShowCursor(SDL_DISABLE);
    /* K4510x is a whole computer that exists to be this machine, so its menu
     * gets a row the others must not have.  The marker file is written by
     * k4510x/build-live.sh; on any other host this call never happens and the
     * row stays off the end of the Machine menu. */
#ifndef K4510_PI
    if (access("/etc/k4510x", F_OK) == 0) menu_set_shutdown(1);
#endif
    SDL_Window *win = SDL_CreateWindow("K4510", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                       VICKY_WIDTH * SCALE, VICKY_HEIGHT * SCALE, SDL_WINDOW_RESIZABLE);
#ifndef K4510_PI
    grab_win = win;
#endif
/* No vsync by default, anywhere.  It was off on the Pi already, because the
 * shim blocked the present until the flip and a frame that overran by a
 * millisecond waited for the next one, stepping the machine down to 30 or 20
 * fps.  The desktop kept vsync and had no pacing of its own, so it ran at
 * whatever the compositor gave it -- 51.8 fps on hdieu's 60 Hz display, with
 * the present taking 16.9 ms of a 19.3 ms frame.  And a machine at 51.8 fps
 * makes 51.8 frames of sound a second where the device wants 60, so the chip
 * fills in the missing seventh and you hear it.  The machine is a 60 Hz design:
 * it keeps its own time below and presents when it is ready, which is what the
 * Pi has always done.
 *
 * The trade is tearing, and on a compositor it is not visible.  It is a
 * SETTING rather than a decision, because that commit said it should be one:
 * a host, a driver or a pair of eyes may want the flip, and the cost of
 * wanting it is measurable and local.  F7 -> Video -> Vertical sync, off by
 * default, which is exactly the behaviour above.  On the Pi it stays off: the
 * shim's present is the blocking one this was escaped from. */
SDL_Renderer *ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED);
    if (!ren) ren = SDL_CreateRenderer(win, -1, 0);      /* no GPU (the dummy driver, a screenshot run) */
    SDL_RenderSetLogicalSize(ren, VICKY_WIDTH, VICKY_HEIGHT);
    /* Two rows of texture per line of the machine, so scanlines cost a second
     * store rather than a second surface: with them off only the top half is
     * written and copied, so it costs nothing at all. */
    SDL_Texture *tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING,
                                         VICKY_WIDTH, VICKY_HEIGHT * 2);
    int scan_applied = -1, smooth_applied = -1, logical_tall = -1, vsync_applied = -1;
    /* The border, striped like the screen: one column of pixels, one texture
     * row per logical row, stretched across.  A single RenderCopy rather than
     * a few hundred RenderDrawLines, and it is rebuilt only when the colour or
     * the scanline setting changes. */
    uint32_t border_lit = 0, border_dim = 0;
    SDL_Texture *btex = NULL; int btex_scan = -1, btex_col = -1, btex_smooth = -1;
    const int shooting = getenv("K4510_SHOT") != NULL && getenv("K4510_SHOT_FX") == NULL;
                                     /* the guide's figures want a clean picture; K4510_SHOT_FX asks for one with the effects */

    static uint8_t fb[VICKY_WIDTH * VICKY_HEIGHT], ov[UI_W * UI_H];
    static uint32_t pal[256], dpal[256];          /* the machine's colours, full and scanline-dimmed */
    static uint32_t mpal[256], mdpal[256];        /* the same, half-lit: the picture behind the menu */
    static uint32_t upal[UIC_COUNT], udpal[UIC_COUNT];   /* the menu's own colours */
    int fullscreen_applied = 0;
    int mode_pending = 0;                          /* (mode + 1) the ROM has been asked for, 0 = nothing */
    int mode_shown = -1, margin_shown = -1, status_shown = -1, mode_req = 0, mode_wait = 0;
#define MODE_REQ_FRAMES 120                        /* two seconds for the guest to notice, then give up */

    SDL_AudioSpec want = { 0 }, have;
    want.freq = AUDIO_RATE; want.format = AUDIO_S16SYS; want.channels = 1; want.samples = 1024; want.callback = audio_cb;
#ifdef K4510_PI
    /* On the Pi the callback runs from this core's own event pump, once a
     * frame, and asks for a block at a time; a frame makes 800 samples. With
     * the ring starting empty, the level sat between zero and one frame, so
     * a 1024-sample block found 800 and 224 of silence -- BENCH counted 40
     * gaps in two seconds at a steady 60 fps. Smaller blocks, and a lead of
     * three of them before the sound starts (32 ms, which the ear does not
     * notice on a music player), so the level never touches the floor. */
    want.samples = 512;
#endif
    SDL_AudioDeviceID adev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
#ifdef K4510_PI
    for (int i = 0; i < 1536; i++) ring[ring_w++ & RING_MASK] = 0;
#endif
    if (adev) SDL_PauseAudioDevice(adev, 0); else fprintf(stderr, "no audio: %s\n", SDL_GetError());
    SDL_StartTextInput();
    /* ---- the clock: measured, not guessed (docs/CPU-CLOCK-POLICY.md) ------
     * SETUP.prg measures this host and writes the answer to k4510.cfg with
     * the host it was measured on.  With cpu.auto on, a later boot on the
     * SAME host reuses it and pays nothing; on any other host the fingerprint
     * disagrees and the machine runs at the compiled-in safe step until
     * someone runs SETUP there.  A clock chosen in the menu turns auto off:
     * an explicit setting always wins. */
    if (settings_get(SET_CPU_AUTO)) {
        if (settings_get(SET_CPU_HOST) == host_id_hash()) {
            settings_set(SET_CPU_CLOCK, settings_get(SET_CPU_MEASURED));
            io_set_clock_measured(1);
        } else {
            /* No measured clock for this host, and we do not stop to find one.
             * Doc's decision, 2026-08-27: the boot is instantaneous, always.
             * A machine nobody has measured runs at the compiled-in safe step
             * -- 40.5 MHz on the desktop, 15 on the Pi -- and SETUP.prg is how
             * it gets measured properly, from inside the machine, with sound
             * and video and the network really running.
             *
             * There used to be a second answer here: a two-phase boot probe
             * (core/calib.c) that measured the host before the shell.  It was
             * compiled but switched off, and had been since the boot was made
             * instantaneous.  Cut 2026-09-01 -- an engine that never runs is a
             * capability the machine appears to have and does not.  The design
             * is in docs/CPU-CLOCK-POLICY.md; the code is in the history. */
            io_set_clock_measured(0);
        }
    }
    int running = 1, shutdown_req = 0;
    int paused = 0;                                 /* F8: freeze the machine with the screen still showing (unless F8 is the menu key) */
    int clock_at_open = -1;                        /* the clock when the menu opened: changed on close = the user's choice */
    const int ring_log = getenv("K4510_RINGLOG") != NULL;
    /* the governor's window: how long the machine's own half of the frame has
     * been costing, and how many callbacks ran dry, since it last decided */
    Uint64 gov_t0 = 0, gov_mach = 0; unsigned gov_frames = 0, gaps_seen = 0;
    /* ---- where the frame goes -------------------------------------------
     * The Pi runs at about a tenth of the speed it was measured at on 22
     * August and nothing in the shared code is slower on the desktop, so
     * rather than guess a third time, the frame loop times itself and writes
     * the answer to SYSTEM/PERF.TXT on the machine's own filesystem. Four
     * buckets: the emulated machine, building the texture, putting it on the
     * glass, and everything else (events, the menu, settings). One counter
     * read per bucket per frame is nothing against a frame. */
    static Uint64 p_mach, p_tex, p_pres, p_tot, p_last; static unsigned p_n;
    static unsigned p_runs;                       /* windows written this run: the first truncates, the rest append */
    static Uint64 p_cpu, p_vic, p_snd;            /* the machine, split three ways */
#ifdef K4510_PI
#define PCLK() ({ Uint64 v_; asm volatile("mrs %0, cntvct_el0" : "=r"(v_)); v_; })
#define PCLK_HZ() ({ Uint64 f_; asm volatile("mrs %0, cntfrq_el0" : "=r"(f_)); f_; })
#else
#define PCLK() SDL_GetPerformanceCounter()
#define PCLK_HZ() SDL_GetPerformanceFrequency()
#endif
#define PERF_FRAMES 300
    while (running) {
        { Uint64 c = SDL_GetPerformanceCounter();
          /* the window opens 20 s after start, so it measures the machine at
           * the prompt rather than BENCH, whose clock reads are dear on the Pi */
          if (p_last && SDL_GetTicks() > 20000) { p_tot += c - p_last; p_n++; }
          if (p_n == 1) { io_prof_reset(); io_prof_on = 1; p_mach = p_tex = p_pres = p_cpu = p_vic = p_snd = 0; }   /* the window opens: every sum starts here */
          p_last = c;
          if (p_n == PERF_FRAMES) {
              char pp[600]; snprintf(pp, sizeof pp, "%s/SYSTEM/PERF.TXT", fs_get_root());
              FILE *pf = fopen(pp, p_runs++ ? "a" : "w");
              if (pf) {
                  Uint64 hz = SDL_GetPerformanceFrequency();
                  double f = (double)PERF_FRAMES;
                  double tot = (double)p_tot * 1000.0 / (double)hz / f;
                  double ma  = (double)p_mach * 1000.0 / (double)hz / f;
                  double tx  = (double)p_tex  * 1000.0 / (double)hz / f;
                  double pr  = (double)p_pres * 1000.0 / (double)hz / f;
                  fprintf(pf, "K4510 frame profile\n===================\n\n");
                  fprintf(pf, "Build:   %s\n", K4510_BUILD);
                  fprintf(pf, "Clock:   %.1f MHz\n", cpu_hz_now / 1e6);
                  fprintf(pf, "Frames:  %d averaged\n\n", PERF_FRAMES);
                  fprintf(pf, "  whole frame      %8.3f ms   (%.1f fps)\n", tot, tot > 0 ? 1000.0 / tot : 0.0);
                  fprintf(pf, "  the machine      %8.3f ms   %5.1f%%\n", ma, tot > 0 ? 100.0 * ma / tot : 0.0);
                  { double ph = (double)PCLK_HZ();
                    double cu = (double)p_cpu * 1000.0 / ph / f, vi = (double)p_vic * 1000.0 / ph / f, si = (double)p_snd * 1000.0 / ph / f;
                    fprintf(pf, "      CPU steps    %8.3f ms\n", cu);
                    fprintf(pf, "      VICKY lines  %8.3f ms\n", vi);
                    fprintf(pf, "      OPL2 render  %8.3f ms\n", si); }
                  fprintf(pf, "  building texture %8.3f ms   %5.1f%%\n", tx, tot > 0 ? 100.0 * tx / tot : 0.0);
                  fprintf(pf, "  onto the glass   %8.3f ms   %5.1f%%\n", pr, tot > 0 ? 100.0 * pr / tot : 0.0);
                  fprintf(pf, "  everything else  %8.3f ms   %5.1f%%\n", tot - ma - tx - pr,
                          tot > 0 ? 100.0 * (tot - ma - tx - pr) / tot : 0.0);
                  { double ph = (double)PCLK_HZ();
                    fprintf(pf, "\nI/O page, per frame: %.0f reads, %.3f ms inside io_read\n",
                            (double)io_prof_reads / f, (double)io_prof_cycles * 1000.0 / ph / f);
                    fprintf(pf, "  hottest register groups (16-byte, reads per frame):\n");
                    for (int k = 0; k < 6; k++) {                       /* top six, by selection */
                        int best = -1; uint32_t bv = 0;
                        for (int i = 0; i < 256; i++) if (io_prof_hist[i] > bv) { bv = io_prof_hist[i]; best = i; }
                        if (best < 0 || !bv) break;
                        fprintf(pf, "    $D%02X0-$D%02XF  %10.0f\n", best, best, (double)bv / f);
                        io_prof_hist[best] = 0;
                    } }
                  { static char hp[1200]; host_perf_probe(hp, sizeof hp); fputs(hp, pf); }
                  fprintf(pf, "\n(events, the menu overlay and the settings poll are 'everything else')\n");
                  fclose(pf);
              }
              io_prof_on = 0;                    /* the window closes: stop paying for the counters */
          }
        }
        SDL_Event e;
        uint8_t pend = 0;               /* a printable key waiting to see whether SDL sends its text */
        while (SDL_PollEvent(&e)) {
            switch (e.type) {
            case SDL_QUIT: running = 0; break;
#ifndef K4510_PI
            case SDL_WINDOWEVENT:
                if (e.window.event == SDL_WINDOWEVENT_FOCUS_LOST) grab(0);   /* alt-tab always frees the pointer */
                break;
            case SDL_MOUSEMOTION:
                mouse_x = to_machine(e.motion.x, VICKY_WIDTH); mouse_y = to_machine(e.motion.y, VICKY_HEIGHT);
                dx_acc += e.motion.xrel / geo_k; dy_acc += e.motion.yrel / geo_k;
                mouse_to_menu(); break;
            case SDL_MOUSEBUTTONDOWN: case SDL_MOUSEBUTTONUP: {
                int bit = e.button.button == SDL_BUTTON_LEFT ? 1 : e.button.button == SDL_BUTTON_RIGHT ? 2 : e.button.button == SDL_BUTTON_MIDDLE ? 4 : 0;
                if (e.type == SDL_MOUSEBUTTONDOWN && !menu_is_open() && settings_get(SET_INPUT_MOUSE_GRAB)) { grab_wanted = 1; grab(1); }
                if (e.type == SDL_MOUSEBUTTONDOWN) mouse_btn |= bit; else mouse_btn &= ~bit;
                mouse_to_menu(); break; }
            case SDL_MOUSEWHEEL:
                wheel_acc += e.wheel.y;
                if (menu_is_open()) { menu_mouse(mouse_x < 0 ? 0 : mouse_x, mouse_y < 0 ? 0 : mouse_y, mouse_btn, e.wheel.y); wheel_acc = 0; }
                break;
            case SDL_CONTROLLERDEVICEADDED: pad_open(e.cdevice.which); break;
            case SDL_CONTROLLERDEVICEREMOVED:
                if (pad && e.cdevice.which == SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(pad))) { SDL_GameControllerClose(pad); pad = NULL; fprintf(stderr, "gamepad: unplugged\n"); }
                break;
            case SDL_CONTROLLERBUTTONDOWN:                   /* the buttons that are keys, not held state: */
                if (e.cbutton.button == SDL_CONTROLLER_BUTTON_START) kbd_push(0x0D);   /* Start = Enter (INVADER2's "press a key", a game's pause) */
                if (e.cbutton.button == SDL_CONTROLLER_BUTTON_BACK)  kbd_push(0x1B);   /* Back/Select = Esc, how every game leaves */
                break;
#endif
            case SDL_TEXTINPUT: {
                pend = 0;                                    /* SDL does send text here: the key code is not needed */
                /* The host layout has already composed the character -- a dead key
                 * plus a vowel arrives here as one UTF-8 sequence.  ASCII goes
                 * straight through; anything above it is decoded and looked up in
                 * the machine's upper half, which is code page 437 (data/mkfont.py).
                 * Dropping the non-ASCII bytes, as this used to, meant no accented
                 * character could ever be typed. */
                for (const char *c = e.text.text; *c; ) {
                    unsigned long cp; unsigned char ch = (unsigned char)*c;
                    if (ch < 0x80) { cp = ch; c++; }
                    else if ((ch & 0xE0) == 0xC0 && (c[1] & 0xC0) == 0x80) { cp = ((unsigned long)(ch & 0x1F) << 6) | (c[1] & 0x3F); c += 2; }
                    else if ((ch & 0xF0) == 0xE0 && (c[1] & 0xC0) == 0x80 && (c[2] & 0xC0) == 0x80)
                         { cp = ((unsigned long)(ch & 0x0F) << 12) | ((unsigned long)(c[1] & 0x3F) << 6) | (c[2] & 0x3F); c += 3; }
                    else { c++; continue; }                      /* 4-byte or malformed: nothing to type */
                    if (cp >= 0x20 && cp < 0x7F) { kbd_push((uint8_t)cp); continue; }
                    { uint8_t b = cp437_of(cp); if (b) kbd_push(b); }
                }
                break;
            }
            case SDL_KEYDOWN: case SDL_KEYUP: {
                SDL_Keymod m = SDL_GetModState();
                kbd_modifiers(m & KMOD_SHIFT, m & KMOD_CTRL, m & KMOD_ALT);
                if (e.type != SDL_KEYDOWN) break;
                SDL_Keycode k = e.key.keysym.sym;
                { /* the reset chord: a modifier + PageUp ("Commodore + Restore"), or Ctrl+Alt+Del */
                  int ch = settings_get(SET_INPUT_RESET_CHORD), hit = 0;
                  if (k == SDLK_PAGEUP) hit = (ch == CHORD_SUPER_PGUP && (m & KMOD_GUI)) || (ch == CHORD_CTRL_PGUP && (m & KMOD_CTRL)) || (ch == CHORD_ALT_PGUP && (m & KMOD_ALT));
                  if (k == SDLK_DELETE && ch == CHORD_CTRL_ALT_DEL && (m & KMOD_CTRL) && (m & KMOD_ALT)) hit = 1;
                  if (hit) { menu_close(); cpu65_reset(); break; } }
                if (k == SDLK_F8 && settings_get(SET_INPUT_MENU_KEY) != MENUKEY_F8) { paused = !paused; SDL_SetWindowTitle(win, paused ? "K4510  [PAUSED]" : "K4510"); break; }
                /* The volume, without opening the menu.  A laptop's own volume
                 * keys (a ThinkPad's Fn+F1/F2/F3) arrive as these three, and
                 * Ctrl+Alt with the plus, minus and zero keys does the same
                 * where those are taken -- on K4510x there is no desktop mixer
                 * behind this program, so this is the only volume there is. */
                { int dv = 0, mute = 0;
                  if (k == SDLK_VOLUMEUP) dv = 10; else if (k == SDLK_VOLUMEDOWN) dv = -10;
                  else if (k == SDLK_AUDIOMUTE || k == SDLK_MUTE) mute = 1;   /* X11 sends one, evdev the other */
                  else if ((m & KMOD_CTRL) && (m & KMOD_ALT)) {
                      if (k == SDLK_EQUALS || k == SDLK_PLUS || k == SDLK_KP_PLUS) dv = 10;
                      else if (k == SDLK_MINUS || k == SDLK_KP_MINUS) dv = -10;
                      else if (k == SDLK_0 || k == SDLK_KP_0) mute = 1;
                  }
                  if (dv || mute) {
                      static int was;                      /* what to come back to after a mute */
                      int v = settings_get(SET_AUDIO_VOLUME);
                      if (mute) { if (v) { was = v; v = 0; } else v = was ? was : 50; }
                      else { v += dv; if (v < 0) v = 0; if (v > 100) v = 100; }
                      settings_set(SET_AUDIO_VOLUME, v);
                      settings_save(cfg);
                      printf("volume %d%%\n", v); fflush(stdout);
                      break;
                  } }
                if ((m & KMOD_CTRL) && k >= 'a' && k <= 'z') { kbd_push((uint8_t)(k - 'a' + 1)); break; }
                switch (k) {
                case SDLK_RETURN: case SDLK_KP_ENTER: kbd_push(KEY_ENTER); break;
                case SDLK_BACKSPACE: kbd_push(KEY_BS); break;
                case SDLK_TAB:       kbd_push(KEY_TAB); break;
                case SDLK_ESCAPE:    if (m & KMOD_SHIFT) running = 0; else kbd_push(KEY_ESC); break;
                case SDLK_UP: kbd_push(KEY_UP); break;     case SDLK_DOWN: kbd_push(KEY_DOWN); break;
                case SDLK_LEFT: kbd_push(KEY_LEFT); break; case SDLK_RIGHT: kbd_push(KEY_RIGHT); break;
                case SDLK_HOME: kbd_push(KEY_HOME); break; case SDLK_END: kbd_push(KEY_END); break;
                case SDLK_PAGEUP: kbd_push(KEY_PGUP); break; case SDLK_PAGEDOWN: kbd_push(KEY_PGDN); break;
                case SDLK_INSERT: kbd_push(KEY_INS); break; case SDLK_DELETE: kbd_push(KEY_DEL); break;
                case SDLK_PAUSE: kbd_push(0x9F); break;
                default:
                    if (k >= SDLK_F1 && k <= SDLK_F12) { kbd_push((uint8_t)(KEY_F1 + (k - SDLK_F1))); break; }
                    if (!(m & (KMOD_CTRL | KMOD_ALT | KMOD_GUI))) pend = key_ascii(k, m & KMOD_SHIFT);
                    break;
                }
                break; }
            }
        }
        if (pend) {                                          /* no text event came: type the key itself */
            static int said;
            if (!said) { said = 1; fprintf(stderr, "keyboard: SDL sends no text on the %s driver; typing from the key codes\n", SDL_GetCurrentVideoDriver()); }
            kbd_push(pend);
        }
        host_poll_input();                                   /* the Pi: C64 keyboard on GPIO */
#ifndef K4510_PI
        { static int menu_was; int m = menu_is_open();               /* the menu is the machine's outside: it frees the pointer */
          if (m && !menu_was) grab(0);
          else if (!m && menu_was && grab_wanted && settings_get(SET_INPUT_MOUSE_GRAB)) grab(1);
          if (!settings_get(SET_INPUT_MOUSE_GRAB)) { grab(0); grab_wanted = 0; }
          menu_was = m; }
        {   /* $D104: which of the game keys are down right now (core/io.h) */
            const Uint8 *ks = SDL_GetKeyboardState(NULL); uint8_t held = 0;
            if (ks[SDL_SCANCODE_UP])    held |= HELD_UP;
            if (ks[SDL_SCANCODE_DOWN])  held |= HELD_DOWN;
            if (ks[SDL_SCANCODE_LEFT])  held |= HELD_LEFT;
            if (ks[SDL_SCANCODE_RIGHT]) held |= HELD_RIGHT;
            if (ks[SDL_SCANCODE_SPACE]) held |= HELD_FIRE;
            if (ks[SDL_SCANCODE_Z])     held |= HELD_A;
            if (ks[SDL_SCANCODE_X])     held |= HELD_B;
            kbd_held(held | pad_held());
            mouse_set(mouse_x < 0 ? 0 : mouse_x, mouse_y < 0 ? 0 : mouse_y, (uint8_t) mouse_btn, wheel_acc, dx_acc, dy_acc);   /* $D108-$D10F */
            wheel_acc = dx_acc = dy_acc = 0;
        }
#endif
        { static const char *feed; static int feed_init, feed_wait, feed_fr;   /* K4510_KEYS: keys typed one per frame, ~ waits 30 */
          if (!feed_init) { feed_init = 1; feed = getenv("K4510_KEYS"); }
          if (feed && *feed && ++feed_fr >= feed_wait) { uint8_t k = (uint8_t)*feed++; if (k == '~') feed_wait = feed_fr + 30; else kbd_push(k == '\n' ? 0x0D : k); } }
        /* The machine's video mode.  Only the ROM can change it -- the console's
         * PCOLS/PROWS/stride are its -- so the menu asks through $D521 bits 5-7
         * and the ROM acts on its next key poll.  Which means the machine has to
         * be running: a frozen one would never see the request, and the point of
         * choosing a resolution in the menu is watching it happen.  So an
         * outstanding request thaws the machine until VICKY's CTRL says it took,
         * or until the wait runs out (a program that never reads a key). */
        { static const uint8_t ctrl_of[VMODE_COUNT] = { 0, 4, 2, 2 | 8, 2 | 8 | 16 };
          uint8_t c = vicky_read(VR_CTRL);
          int machine = -1;
          if (c & 1) {                                  /* bit 0 is display-enable.  Before the ROM's
                                                         * video_init runs, CTRL is 0 -- which is NOT
                                                         * 640x480, though it looks just like it. */
              uint8_t m = (uint8_t)(c & (2 | 4 | 8 | 16));
              for (int i = 0; i < VMODE_COUNT; i++) if (ctrl_of[i] == m) machine = i;
          }
          if (mode_req) {
              if (io_mode_acked()) mode_req = 0;        /* the guest says it has done it */
              else if (--mode_wait <= 0) {              /* it never will (a program that polls no keys):
                                                         * put the setting back, so the menu does not lie
                                                         * about a mode the machine is not in */
                  mode_req = 0;
                  if (machine >= 0 && machine != mode_shown) { mode_shown = machine; settings_set(SET_VIDEO_MODE, machine); menu_dirty(); }
              }
          } else if (mode_shown < 0) {
              if (machine >= 0) {                       /* the machine has booted -- straight into the saved
                                                         * mode, which $D521 bits 5-7 publish from power-on.
                                                         * A request only if it booted into something else
                                                         * (an old ROM that does not read the bits). */
                  mode_shown   = settings_get(SET_VIDEO_MODE);
                  margin_shown = settings_get(SET_VIDEO_MARGIN);
                  status_shown = settings_get(SET_VIDEO_STATUSBAR);
                  if (machine != mode_shown) { mode_req = mode_shown + 1; mode_wait = MODE_REQ_FRAMES; }
              }
          } else if (menu_is_open()) {
              /* While the menu is up, nothing is applied: the user may step
               * through modes and land back on the original, and that must
               * cost nothing.  The comparisons below run at menu close and
               * ask only whether anything is NET different. */
          } else if (settings_get(SET_VIDEO_MODE) != mode_shown) {     /* the user picked a mode */
              mode_shown = settings_get(SET_VIDEO_MODE);
              mode_req = mode_shown + 1; mode_wait = MODE_REQ_FRAMES;
          } else if (settings_get(SET_VIDEO_MARGIN) != margin_shown) { /* or turned the margin off */
              margin_shown = settings_get(SET_VIDEO_MARGIN);
              mode_req = mode_shown + 1; mode_wait = MODE_REQ_FRAMES;
          } else if (settings_get(SET_VIDEO_STATUSBAR) != status_shown) { /* or toggled the status bar */
              status_shown = settings_get(SET_VIDEO_STATUSBAR);
              mode_req = mode_shown + 1; mode_wait = MODE_REQ_FRAMES;
          } else if (machine >= 0 && machine != mode_shown) {          /* or the guest ran MODE itself */
              mode_shown = machine; settings_set(SET_VIDEO_MODE, machine); menu_dirty();
          }
          mode_pending = mode_req; }

        /* Before the machine steps, never after: the ROM reads $D521 while it
         * boots (STARTUP.BAT) and on its next key poll (the mode request), so
         * a byte written at the end of the frame would arrive one frame late
         * -- and for the boot read, a whole power-on too late. */
        io_set_opts((settings_get(SET_SHELL_CPMCOM) ? SYSOPT_CPMCOM : 0)
                    | ((settings_get(SET_SHELL_STARTUP) && !no_startup) ? 0 : SYSOPT_NOBOOT)
                    | ((settings_get(SET_VIDEO_MARGIN) && !settings_get(SET_VIDEO_STATUSBAR)) ? SYSOPT_MARGIN : 0)
                    | (settings_get(SET_VIDEO_STATUSBAR) ? SYSOPT_STATUS : 0)
                    | (uint8_t)((mode_pending ? mode_pending
                                              : settings_get(SET_VIDEO_MODE) + 1) << SYSOPT_MODE_SHIFT)
                    | (mode_pending ? SYSOPT_MODEREQ : 0));
        io_set_bands((uint8_t)settings_get(SET_TERM_BAND_TOP),
                     (uint8_t)settings_get(SET_TERM_BAND_BOT),
                     (uint8_t)((settings_get(SET_TERM_CLOCK24) ? 1 : 0)
                               | (settings_get(SET_TERM_DATEFMT) << 1)));

        Uint64 p_a = SDL_GetPerformanceCounter();
        int open = menu_is_open();
        if ((!open && !paused) || mode_pending) {            /* frozen while the menu is open OR paused; paused keeps the picture */
            int vol = settings_get(SET_AUDIO_VOLUME);
            vicky_begin_frame(fb, VICKY_WIDTH);
            for (int y = 0; y < VICKY_HEIGHT; y++) {
                Uint64 t0 = PCLK();
                cpu65.irqLevel = vicky_irq() ? 1 : 0;
                cpu65_step(CYCLES_PER_LINE);
                Uint64 t1 = PCLK();
                vicky_line(y);
                Uint64 t2 = PCLK();
                /* The audio clock the OPL2 writes are stamped with: one
                 * scanline of it, whoever is rendering.  See core/sndq.h. */
                sndq_tick(1000000u / (60u * VICKY_HEIGHT));
                if (sndq_owner() == SNDQ_OWNER_CPU)
                { int16_t tmp[256]; int n = audio_render(CYCLES_PER_LINE, tmp, 256);
                  for (int i = 0; i < n; i++) if (RING_DEPTH < RING_CAP) ring[ring_w++ & RING_MASK] = (int16_t)(tmp[i] * vol / 100); }
                Uint64 t3 = PCLK();
                p_cpu += t1 - t0; p_vic += t2 - t1; p_snd += t3 - t2;
#ifdef K4510_PI
                /* The shim serves the audio callback only when this core pumps
                 * events, and its device runway is 30 ms: a frame longer than
                 * that (28 fps at 40.5 MHz is 36 ms) ran the device dry however
                 * full our ring was.  So the pump is called a quarter-frame at
                 * a time, with the ring topped up first, and the sound holds
                 * whatever the frame rate. */
                if ((y & 127) == 127) {
                    int vol_ = settings_get(SET_AUDIO_VOLUME); int guard_ = 1024;
                    while (RING_DEPTH < RING_TARGET && guard_--) {
                        int16_t t_[256]; int n_ = audio_render(CYCLES_PER_LINE, t_, 256);
                        for (int i = 0; i < n_; i++) if (RING_DEPTH < RING_CAP) ring[ring_w++ & RING_MASK] = (int16_t)(t_[i] * vol_ / 100);
                    }
                    SDL_PumpEvents();
                }
#endif
            }
            vicky_end_frame();
            cpu65.irqLevel = vicky_irq() ? 1 : 0;
        }
        /* The sound keeps going when the CPU is late.  Audio was made only by
         * the machine's frames -- 800 samples each -- so a machine at 58 fps
         * made 46,400 a second against the 48,000 the device consumes, and any
         * shortfall at all drained the ring and gapped for ever after (a lead
         * only delayed the first gap; BENCH went from 45 gaps to 55).  Real
         * hardware does not stop its sound chip because the CPU stalled: here
         * it is clocked on without it until the ring holds a target again.
         * Pitch is the chip's own and does not move; a slow frame sustains a
         * note a fraction longer instead of cutting it.  Only after a frame the
         * machine ran -- frozen under the menu, it is silent, as before. */
        if (((!open && !paused) || mode_pending) && sndq_owner() == SNDQ_OWNER_CPU) {
            int vol = settings_get(SET_AUDIO_VOLUME);
            int guard = 4096;                                 /* never more than a few frames of sound ahead */
            while (RING_DEPTH < RING_TARGET && guard--) {
                int16_t tmp[256]; int n = audio_render(CYCLES_PER_LINE, tmp, 256);
                for (int i = 0; i < n; i++) if (RING_DEPTH < RING_CAP) ring[ring_w++ & RING_MASK] = (int16_t)(tmp[i] * vol / 100);
                /* how much of the sound the machine did not make: the honest
                 * measure of choppy, now that the ring is kept from running dry */
                if (n > 0) io_audio_fill = (io_audio_fill > 0xFFFF - n) ? 0xFFFF : (uint16_t)(io_audio_fill + n);
            }
        }
        { Uint64 d = SDL_GetPerformanceCounter() - p_a; p_mach += d; gov_mach += d; gov_frames++; }
        /* K4510_RINGLOG=1: the audio lead, every two seconds, on stderr.  A
         * lead that climbs is sound arriving later and later behind the
         * picture; one that sits at zero with the gap count rising is sound
         * the device asked for and did not get.  The two faults look alike
         * from the chair and not at all alike here. */
        if (ring_log) { static Uint32 rt; if (SDL_GetTicks() - rt >= 2000) { rt = SDL_GetTicks();
            fprintf(stderr, "ring: lead %u samples (%.0f ms), gaps %u, %.1f MHz\n", RING_DEPTH,
                    RING_DEPTH * 1000.0 / AUDIO_RATE, io_audio_gaps, settings_cpu_hz() / 1e6); } }
        /* what the menu asked for */
        { int act = menu_take_action();
          if (act >= ACT_SAVE_SLOT && act < ACT_SAVE_SLOT + MENU_SLOTS) { state_save(slot_path(act - ACT_SAVE_SLOT)); slot_refresh(act - ACT_SAVE_SLOT); act = ACT_NONE; }
          if (act >= ACT_LOAD_SLOT && act < ACT_LOAD_SLOT + MENU_SLOTS) {
              io_write(IO_TUBE + 3, 2);                    /* the co-processor is not in the file: stopped before the machine changes under it */
              if (state_load(slot_path(act - ACT_LOAD_SLOT)) == 0) font_applied = -1;   /* the font lives in RAM: the file's wins, but the setting reapplies on the next frame */
              act = ACT_NONE; }
        switch (act) {
        case ACT_RESET: cpu65_reset(); break;
        case ACT_POWER_CYCLE: host_zero(k4510_ram, K4510_PHYS_SIZE); mem_reset(); io_reset(); apply_font(font_applied); mem_load_rom(rom); cpu65_reset();
                              mode_shown = -1; mode_req = 0; break;   /* forget the mode tracking: re-adopt once the ROM is back up */
        case ACT_TUBE_STOP: io_write(IO_TUBE + 3, 2); break;
        case ACT_QUIT: running = 0; break;
        case ACT_SHUTDOWN: shutdown_req = 1; running = 0; break;   /* acted on below, after the settings are saved and SDL has let go of the screen */
        } }
        if (open && clock_at_open < 0) clock_at_open = settings_get(SET_CPU_CLOCK);
        /* SETUP has finished measuring and asks us to keep the clock it settled
         * on.  The guest chose it; we supply the two things it cannot know --
         * which host this is, and where the file lives. */
        if (io_adopt_requested()) {
            settings_set(SET_CPU_MEASURED, settings_get(SET_CPU_CLOCK));
            settings_set(SET_CPU_HOST, host_id_hash());
            settings_save(cfg);
            io_set_clock_measured(1);
            fprintf(stderr, "clock: SETUP measured this machine at %.1f MHz; kept\n", settings_cpu_hz() / 1e6);
        }
        if (menu_closed_pending()) {
            /* Sound on core 3: the handover happens here, at a menu close --
             * a moment the machine is already stopped.  menu_closed_pending()
             * is ONE-SHOT, so this has to live inside the same test as
             * everything else that acts on a close, not beside it.
             * The desktop has no second core to give the sound to and is not
             * asked: the request would spin out its whole bound for nothing,
             * which the person who opened the menu would feel. */
#ifdef K4510_PI
            { int want3 = settings_get(SET_AUDIO_CORE3) ? SNDQ_OWNER_OTHER : SNDQ_OWNER_CPU;
              if (sndq_owner() != want3) sndq_request(want3); }
#endif
            if (clock_at_open >= 0 && settings_get(SET_CPU_CLOCK) != clock_at_open && settings_get(SET_CPU_AUTO))
                settings_set(SET_CPU_AUTO, 0);     /* a clock chosen by hand is not to be second-guessed at the next boot */
            clock_at_open = -1;
            if (settings_changed()) settings_save(cfg);
        }
        /* ---- the governor -------------------------------------------------
         * The measurement is a guess about programs it has not seen, so the
         * machine watches itself and steps down when the guess was wrong.
         *
         * What it watches is how long the machine's own half of the frame
         * takes -- CPU, VICKY, the OPL2 -- against the 16.67 ms it has.  The
         * first version of this counted audio gaps instead, and the archive
         * session found it useless on Doc's laptop: the machine sat at 38
         * frames a second with the sound perfectly clean and the governor
         * content.  That is 952daa6 working as designed -- the sound was
         * deliberately decoupled from a late CPU so a slow frame sustains a
         * note instead of cutting it -- and it means the gap counter says
         * nothing at all across the whole band where a host is merely losing,
         * rather than drowning.  Reading it was reading the one meter that
         * fix insulated from the fault.
         *
         * Frame time is the direct measure, it is what SETUP measured,
         * and unlike a frames-per-second floor it does not mistake a 50 Hz
         * display for a slow machine.  Gaps stay as a second trigger, for the
         * drowning case.  The window is three seconds and restarts whenever
         * the clock changes or the menu opens -- which is also what keeps the
         * governor out of BENCH's way, since BENCH sweeps the ladder two
         * seconds a step and means to starve the sound at the top of it. */
        /* io_measuring(): SETUP is sweeping the ladder and starving the sound
         * on purpose at the top of it.  Stepping down under the program that is
         * measuring us corrupts its answer -- it did, 2026-08-27 -- so stand
         * down entirely until it says it has finished. */
        if (!settings_get(SET_CPU_AUTO) || open || io_measuring()) { gov_t0 = 0; gov_mach = 0; gov_frames = 0; gaps_seen = io_audio_gaps; }
        else {
            Uint64 nowc = SDL_GetPerformanceCounter(), hzc = SDL_GetPerformanceFrequency();
            if (!gov_t0) { gov_t0 = nowc; gov_mach = 0; gov_frames = 0; gaps_seen = io_audio_gaps; }
            else if (nowc - gov_t0 >= hzc * 3 && gov_frames >= 30) {
                double ms = (double)gov_mach * 1000.0 / (double)hzc / gov_frames;
                unsigned g = io_audio_gaps - gaps_seen;
                int s = settings_get(SET_CPU_CLOCK), down = clock_step_below(s);
                if ((ms > GOV_LATE_MS || g >= 3) && down >= 0) {
                    /* The clock, and only the clock.  cpu.measured and cpu.host
                     * belong to SETUP: they mean "this host was measured, and
                     * this is what it came to", and the banner asks for SETUP
                     * until they say so.  The governor writing them would have
                     * a three-second window impersonate a full measurement and
                     * silence that prompt -- and it wrote cpu.measured without
                     * cpu.host, so the pair said "measured on host 0", which no
                     * fingerprint can ever equal (host_id_hash never returns
                     * 0) and no boot could ever reuse.  The archive session
                     * found that in hdieu's k4510.cfg, 2026-08-27.
                     *
                     * So this is a live correction: it saves the clock, which
                     * carries on a host SETUP has never measured, and defers to
                     * SETUP's answer on one it has. */
                    settings_set(SET_CPU_CLOCK, down); settings_save(cfg);
                    fprintf(stderr, "clock: %.1f ms a frame%s at %.1f MHz, stepping down to %.1f%s\n",
                            ms, g >= 3 ? " and the sound starving" : "", settings_cpu_hz_of(s) / 1e6,
                            settings_cpu_hz_of(down) / 1e6,
                            io_clock_measured() ? " for this session (SETUP's measurement stands; re-run it if this repeats)" : "");
                }
                gov_t0 = 0;
            }
        }
        if (settings_cpu_hz() != cpu_hz_now) {
            cpu_hz_now = settings_cpu_hz(); cycles_per_line = cpu_hz_now / 60 / VICKY_HEIGHT;
            io_set_cpu_khz(cpu_hz_now / 1000); audio_set_cpu_hz((double)cpu_hz_now);
            /* A new clock is a new machine to measure: open another PERF window
             * and append it.  This is how the Pi gets swept -- there is no
             * K4510_CPU_HZ on the card, only the menu. */
            p_n = 0; p_last = 0;
            gov_t0 = 0;                                  /* and a new machine to judge: the governor's window restarts */
        }
        if (settings_get(SET_VIDEO_FONT) != font_applied) {
            font_applied = settings_get(SET_VIDEO_FONT); apply_font(font_applied);
            if (open) vicky_repaint(fb, VICKY_WIDTH);    /* frozen: nothing else would draw the new chargen */
        }
        if (settings_get(SET_VIDEO_FULLSCREEN) != fullscreen_applied) { fullscreen_applied = settings_get(SET_VIDEO_FULLSCREEN); SDL_SetWindowFullscreen(win, fullscreen_applied ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0); }
        /* the menu takes the machine's own row grid: 30 rows over a 240-line
         * mode, 60 over 640x480, so its lines sit on the picture's lines */
        if (ui_cell_h((vicky_read(VR_CTRL) & 6) ? 16 : 8)) menu_dirty();
        menu_draw(ov);

        /* Vertical sync, live.  SDL_RenderSetVSync arrived in 2.0.18, so on
         * anything older the setting simply cannot be honoured and the
         * machine keeps its own pacing -- which is the default anyway.  Not
         * on the Pi: the shim's present is the blocking one all of this was
         * escaped from. */
#ifndef K4510_PI
        if (settings_get(SET_VIDEO_VSYNC) != vsync_applied) {
            vsync_applied = settings_get(SET_VIDEO_VSYNC);
            if (SDL_RenderSetVSync(ren, vsync_applied) != 0)
                fprintf(stderr, "vsync: this SDL or driver will not take it (%s)\n", SDL_GetError());
        }
#endif
        if (settings_get(SET_VIDEO_SMOOTH) != smooth_applied) {
            smooth_applied = settings_get(SET_VIDEO_SMOOTH);
            /* Only "soft" is meant to be soft.  sharp-fit was picking linear
             * as well, which threw away the point of it: with integer scaling
             * every pixel of the machine is a whole number of pixels on the
             * glass, so nearest is exact -- no dropped rows, and no blur to
             * hide them with. */
            SDL_SetTextureScaleMode(tex, smooth_applied == SMOOTH_SOFT ? SDL_ScaleModeLinear : SDL_ScaleModeNearest);
            SDL_RenderSetIntegerScale(ren, smooth_applied == SMOOTH_SHARPFIT ? SDL_TRUE : SDL_FALSE);
        }
        /* Scanlines are never a figure's -- but they ARE the menu's: the point
         * of choosing them there is seeing them, so the overlay is drawn
         * through the same path the machine's picture is. */
        scan_applied = shooting ? SCAN_OFF : settings_get(SET_VIDEO_SCANLINES);

        /* the palettes, once a frame instead of once a pixel: the inner loop
         * then reads two tables and stores twice.  Four of them -- the
         * machine's colours and the menu's, each at full and at scanline
         * brightness -- so the menu dims and darkens without a branch. */
        /* Scanlines without the picture going dark.  Dimming every other line
         * costs (1 + n/4)/2 of the mean -- measured 68 -> 60 -> 52 -> 44 across
         * the four settings, which is the arithmetic exactly -- so the lit line
         * is given back what the dark line loses, and the average stays put.
         * Saturated colours clip, which is what a real tube does too. */
        { static const int num[SCAN_COUNT] = { 4, 3, 2, 1 };            /* quarters of full brightness */
          int n = num[scan_applied];
          int gain = 8 * 256 / (4 + n);                                 /* 8/(4+n) in 8.8 fixed point */
#define LIT(v)  (uint32_t)(((v) * gain >> 8) > 255 ? 255 : ((v) * gain >> 8))
#define DIM(v)  (uint32_t)((((v) * n / 4) * gain >> 8) > 255 ? 255 : (((v) * n / 4) * gain >> 8))
#define SCANDIM(c) (0xFF000000u | ((((c) >> 16 & 255) * n / 4) << 16) \
                                | ((((c) >>  8 & 255) * n / 4) <<  8) \
                                |  (((c)       & 255) * n / 4))
#define BUILD(dst, ddst, c) do { \
              int r_ = ((c) >> 16) & 255, g_ = ((c) >> 8) & 255, b_ = (c) & 255; \
              (dst)  = 0xFF000000u | (LIT(r_) << 16) | (LIT(g_) << 8) | LIT(b_); \
              (ddst) = 0xFF000000u | (DIM(r_) << 16) | (DIM(g_) << 8) | DIM(b_); \
          } while (0)
          for (int i = 0; i < 256; i++) {
              uint32_t c = vicky_palette_rgb(i), h = (c >> 1) & 0x7F7F7F;   /* h: half-lit, behind the menu */
              BUILD(pal[i], dpal[i], c);
              BUILD(mpal[i], mdpal[i], h);
          }
          for (int i = 0; i < UIC_COUNT; i++) BUILD(upal[i], udpal[i], ui_palette_rgb(i));
          /* The border is part of the picture, so it is scanlined and gained
           * with it.  It used to be a flat SDL_RenderClear at full palette
           * brightness, which left it both unstriped and brighter than the
           * average of the tube it was framing -- Doc, 2026-09-01: "scanline
           * effects do not seem to carry over to borders, looks a little
           * weird".  It was two things at once. */
          { uint32_t c = vicky_palette_rgb(settings_get(SET_VIDEO_BORDER_COLOUR));
            BUILD(border_lit, border_dim, c); }
#undef BUILD
#undef LIT
#undef DIM
        }

        void *pixels; int pitch;
        p_a = SDL_GetPerformanceCounter();
        SDL_LockTexture(tex, NULL, &pixels, &pitch);
        { int tall = scan_applied != SCAN_OFF;         /* two texture rows per line of the machine */
          for (int y = 0; y < VICKY_HEIGHT; y++) {
              const uint8_t *src = fb + y * VICKY_WIDTH, *o = ov + y * UI_W;
              uint32_t *d0 = (uint32_t *)((uint8_t *)pixels + (tall ? 2 * y : y) * pitch);
              uint32_t *d1 = tall ? (uint32_t *)((uint8_t *)pixels + (2 * y + 1) * pitch) : NULL;
              if (!open) {
                  if (tall) for (int x = 0; x < VICKY_WIDTH; x++) { uint8_t c = src[x]; d0[x] = pal[c]; d1[x] = dpal[c]; }
                  else      for (int x = 0; x < VICKY_WIDTH; x++) d0[x] = pal[src[x]];
              } else if (tall) {
                  for (int x = 0; x < VICKY_WIDTH; x++)
                      if (o[x]) { d0[x] = upal[o[x]]; d1[x] = udpal[o[x]]; }
                      else      { d0[x] = mpal[src[x]]; d1[x] = mdpal[src[x]]; }
              } else {
                  for (int x = 0; x < VICKY_WIDTH; x++) d0[x] = o[x] ? upal[o[x]] : mpal[src[x]];
              }
          } }
        SDL_UnlockTexture(tex);
        p_tex += SDL_GetPerformanceCounter() - p_a;
        p_a = SDL_GetPerformanceCounter();
        { int tall = (scan_applied != SCAN_OFF), b = settings_get(SET_VIDEO_BORDER);
          uint32_t bc = vicky_palette_rgb(settings_get(SET_VIDEO_BORDER_COLOUR));
          int k = tall ? 2 : 1;                                  /* logical units per pixel of the machine */
#ifndef K4510_PI
          geo_k = k; geo_b = b;                                  /* for the mouse */
#endif
          SDL_Rect half = { 0, 0, VICKY_WIDTH, VICKY_HEIGHT };
          SDL_Rect dr = { b * k, b * k, (VICKY_WIDTH - 2 * b) * k, (VICKY_HEIGHT - 2 * b) * k };
          if (tall != logical_tall) {                            /* 4:3 either way: 640x480, or 1280x960 */
              logical_tall = tall;
              SDL_RenderSetLogicalSize(ren, VICKY_WIDTH * k, VICKY_HEIGHT * k);
          }
          int bcol = settings_get(SET_VIDEO_BORDER_COLOUR);
          if (!btex || btex_scan != scan_applied || btex_col != bcol || btex_smooth != smooth_applied) {
              if (btex) SDL_DestroyTexture(btex);
              btex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING,
                                       1, VICKY_HEIGHT * 2);
              btex_scan = scan_applied; btex_col = bcol; btex_smooth = smooth_applied;
              if (btex) { void *bp; int bpitch;
                  /* The SAME filter as the picture (line ~760), which this used
                   * to ignore -- it was always Nearest.  With Scaling = soft the
                   * screen's scanlines are blurred by the linear filter and the
                   * border's stayed crisp, so the two halves of one picture were
                   * striped differently: Doc, 2026-09-01, "borders are weird, not
                   * the same as screen".  Measured on his shot: the screen's blue
                   * ran through every value from 146 to 191 while the border sat
                   * on exactly two, 135 and 101. */
                  SDL_SetTextureScaleMode(btex, smooth_applied == SMOOTH_SOFT ? SDL_ScaleModeLinear
                                                                             : SDL_ScaleModeNearest);
                  if (SDL_LockTexture(btex, NULL, &bp, &bpitch) == 0) {
                      for (int y = 0; y < VICKY_HEIGHT * 2; y++)
                          *(uint32_t *)((uint8_t *)bp + y * bpitch) =
                              (tall && (y & 1)) ? border_dim : border_lit;
                      SDL_UnlockTexture(btex);
                  } }
          }
          /* RenderClear is now only the floor under everything: the border
           * colour flat, in case a rounding edge shows through. */
          SDL_SetRenderDrawColor(ren, (bc >> 16) & 255, (bc >> 8) & 255, bc & 255, 255);
          SDL_RenderClear(ren);
          /* The letterbox -- the bars where the window is not 4:3 -- is part of
           * the same surface and gets the same stripes.  It used to be left
           * flat, on the reasoning that a stripe out there would only be an
           * edge artefact; on Doc's 16:10 screen it read instead as a third
           * kind of grey beside the screen and the border, 2026-09-01: "extra
           * borders (stretch) are different still".
           *
           * It cannot be reached through the logical size, which is the whole
           * difficulty: SDL_RenderSetLogicalSize clips every RenderCopy to the
           * picture's own rect, so a dst of NULL means the picture, not the
           * window, and the bars are outside it by construction.  So step out
           * of the mapping for this one draw, work out where the picture will
           * land exactly as SDL would, and tile the border texture at the
           * PICTURE's vertical scale -- which is what carries the stripe pitch
           * and phase across the seam instead of restarting them at the window
           * edge.  Then put the mapping back for the picture itself. */
          if (btex) {
              SDL_Rect bsrc = { 0, 0, 1, tall ? VICKY_HEIGHT * 2 : VICKY_HEIGHT };
              int ow = 0, oh = 0, lw = VICKY_WIDTH * k, lh = VICKY_HEIGHT * k;
              SDL_GetRendererOutputSize(ren, &ow, &oh);
              if (ow > 0 && oh > 0) {
                  /* ASK SDL where the picture lands rather than working it out
                   * again here.  Reimplementing the rule was tried first and it
                   * was right for two of the three Scaling modes and wrong for
                   * sharp-fit, where SDL_RenderSetIntegerScale floors the scale
                   * by its own arithmetic -- the bars came out a stripe out of
                   * step with the border, which is the exact fault this is
                   * meant to remove.  LogicalToWindow is SDL's own answer and
                   * cannot disagree with SDL. */
                  int wy0 = 0, wy1 = 0, wx = 0;
                  SDL_RenderLogicalToWindow(ren, 0.0f, 0.0f, &wx, &wy0);
                  SDL_RenderLogicalToWindow(ren, 0.0f, (float)lh, &wx, &wy1);
                  int py = wy0, ph = wy1 - wy0;
                  if (ph > 0) {
                      SDL_RenderSetLogicalSize(ren, 0, 0);
                      for (int y = py; y > -ph; y -= ph) { SDL_Rect d = { 0, y, ow, ph }; SDL_RenderCopy(ren, btex, &bsrc, &d); }
                      for (int y = py + ph; y < oh; y += ph) { SDL_Rect d = { 0, y, ow, ph }; SDL_RenderCopy(ren, btex, &bsrc, &d); }
                      SDL_RenderSetLogicalSize(ren, lw, lh);
                  } else SDL_RenderCopy(ren, btex, &bsrc, NULL);
              } else SDL_RenderCopy(ren, btex, &bsrc, NULL);
          }
          SDL_RenderCopy(ren, tex, tall ? NULL : &half, &dr); }
        SDL_RenderPresent(ren);
        p_pres += SDL_GetPerformanceCounter() - p_a;
        /* The hand pacer runs whether or not vsync is on, and the two cannot
         * fight, because it is a FLOOR and not a cadence: it sleeps only when
         * the frame came early.  With vsync on a 60 Hz display the present has
         * already spent the frame, so it never sleeps; on a 144 Hz display it
         * holds the machine at 60 instead of letting it run at 144, which is
         * what the design wants.  Standing it down when vsync was asked for
         * was tried first and was worse: SDL reports success for a vsync the
         * driver does not really provide (the dummy driver on a screenshot
         * run), and then nothing paced the machine at all -- 63.1 fps,
         * measured, which is sound as well as speed. */
        { /* 60 frames a second, drift-free: sleep to the next deadline; if the
           * frame overran, the deadline just moves on -- no step down to 30 */
          static Uint64 next; Uint64 now = SDL_GetPerformanceCounter(), per = SDL_GetPerformanceFrequency() / 60;
          if (!next || now > next + 4 * per) next = now;
          next += per;
          if (now < next) SDL_Delay((Uint32)((next - now) * 1000 / SDL_GetPerformanceFrequency()));
#ifdef __EMSCRIPTEN__
          else emscripten_sleep(0);                    /* a page must hand the browser its turn every frame, early or late */
#endif
        }
        { static const char *shot; static int shot_fr, shot_init;      /* K4510_SHOT=file.ppm:frames -- a screenshot of what is on the glass */
          if (!shot_init) { shot_init = 1; shot = getenv("K4510_SHOT"); if (shot) { const char *c = strrchr(shot, ':'); shot_fr = c ? atoi(c + 1) : 120; } }
          if (shot && --shot_fr == 0) {
              char path[256]; snprintf(path, sizeof path, "%.*s", (int)(strrchr(shot, ':') ? strrchr(shot, ':') - shot : (long) strlen(shot)), shot);
              FILE *f = fopen(path, "wb");
              int sh = (scan_applied != SCAN_OFF) ? VICKY_HEIGHT * 2 : VICKY_HEIGHT;   /* the tall texture is two rows a line */
              if (f) { fprintf(f, "P6 %d %d 255\n", VICKY_WIDTH, sh); SDL_LockTexture(tex, NULL, &pixels, &pitch);
                       for (int y = 0; y < sh; y++) for (int x = 0; x < VICKY_WIDTH; x++) { uint32_t p = ((uint32_t *)((uint8_t *)pixels + y * pitch))[x]; fputc((p >> 16) & 255, f); fputc((p >> 8) & 255, f); fputc(p & 255, f); }
                       SDL_UnlockTexture(tex); fclose(f); }
              running = 0; } }
    }
    if (settings_changed()) settings_save(cfg);
    SDL_DestroyTexture(tex); SDL_DestroyRenderer(ren); SDL_DestroyWindow(win); SDL_Quit();
    /* Shut the computer down, in this order and not another: the settings are
     * already written above, SDL has given the console back, and only then do
     * we ask for the power off.  The helper syncs and unmounts the persistence
     * partition before halting, which is what makes it safe to pull the stick
     * out afterwards -- see k4510x/build-live.sh. */
#ifndef K4510_PI
    if (shutdown_req) execl("/usr/local/sbin/k4510x-poweroff", "k4510x-poweroff", (char *) NULL);
#endif
    return 0;
}

#ifndef K4510_PI
int main(int argc, char **argv) { return k4510_frontend_main(argc, argv); }
#endif
