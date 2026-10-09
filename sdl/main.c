/* K4510 desktop frontend -- spike version.
 *
 * SDL2 window, 60 Hz. Each frame: run the 45GS10 for a frame's worth of
 * cycles, feed keys into the keyboard register, let VICKY render screen
 * RAM. The ROM (Wozmon) does everything else.
 */
#include <SDL.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>   /* access(): is this the K4510 Linux? */
#include <sys/wait.h>
#include <poll.h>
#include <glob.h>
#include <ifaddrs.h>
#include <netinet/in.h>
#include <arpa/inet.h>
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
#include "../core/term.h"            /* the cursor blink, steady while typing */
#include "panel.h"
#include "png.h"                     /* the screenshots */
#include "savers.h"                  /* the sidebar-savers that paint whole scenes */
#include "sidebars.h"                /* the sidebars there are: the zips in /SYSTEM/SIDEBARS */
#include "../core/ui/settings.h"
#include "../core/hostid.h"
#include "../core/ui/menu.h"
#include "../core/ui/ui_draw.h"
#include "../core/ui/frame.h"
#include "../core/state.h"
#include <sys/stat.h>
#include <dirent.h>
#include <ctype.h>
#include <math.h>          /* the frame text's contrast */
#include <strings.h>
#include <time.h>
#include <signal.h>
#include <sys/time.h>
#include <dirent.h>        /* /sys/class/power_supply: the battery */
#if defined(__linux__)
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/vt.h>      /* VT_ACTIVATE: Ctrl+Alt+F2..F6 on the K4510 Linux; VT_GETSTATE: is ours up (vt_away) */
#include <sys/sysmacros.h> /* major, minor: which console is ours */
#endif

/* A screenshot on request -- SIGUSR1 (tools/k4510-shot) or PrtSc -- of the
 * machine's own picture: one row per line of the machine, the menu over it
 * when it is open.  Written as PNG with stored
 * (uncompressed) deflate blocks: no zlib to link, and 900 KB a frame is
 * nothing.  Doc, 2026-09-12: the Dell's KMSDRM screen could not be grabbed
 * from outside (fbdev is bypassed, the scanout is tiled). */
static volatile sig_atomic_t shot_req;
static int caps_ctrl_down;          /* F12 -> Input -> Caps Lock is Ctrl: the key is held now */
static int shot_flash;              /* frames left of the screenshot's screen invert */
static int shot_full;               /* the whole display is to be read back this frame (shot_save_full) */
/* F12 -> Input -> Key pipe, "on, shown": keys typed from outside (the KEYS
 * pipe, tools/k4510-type) are echoed in a bar at the foot of the window --
 * the last few dozen, gone four seconds after the last -- so nobody types
 * into the machine unseen (Doc, 2026-09-14).  Drawn by the frontend over
 * the picture, never into it: the machine and its screenshots are untouched. */
static char echo_txt[48]; static int echo_len; static Uint32 echo_until;
/* F12 -> Video -> Palette: the .PAL files, listed for the menu each time it
 * opens; the choice is typed at the K/OS prompt (PALETTE LOAD NAME), now or,
 * if a program is running, as soon as it has ended -- so the ROM does what
 * PALETTE always does: the entries, a COLOR line, the bars' readable pair. */
static char pal_list[MENU_PALETTES][16]; static int pal_n;
static char pal_pending[40];
static int pal_cmp(const void *a, const void *b) { return strcmp((const char *) a, (const char *) b); }
static void palettes_scan(void)
{
    char dir[600]; DIR *d; struct dirent *e; const char *nm[MENU_PALETTES]; int i;
    pal_n = 0;
    if (io_fs_hostpath("/SYSTEM/ETC/PALETTES", dir, sizeof dir) && (d = opendir(dir))) {
        while ((e = readdir(d)) && pal_n < MENU_PALETTES) {
            size_t l = strlen(e->d_name);
            if (l < 5 || l > 4 + 12 || strcasecmp(e->d_name + l - 4, ".pal")) continue;
            for (i = 0; i < (int) l - 4; i++) pal_list[pal_n][i] = (char) toupper((unsigned char) e->d_name[i]);
            pal_list[pal_n][l - 4] = 0; pal_n++;
        }
        closedir(d);
    }
    qsort(pal_list, (size_t) pal_n, sizeof pal_list[0], pal_cmp);
    for (i = 0; i < pal_n; i++) nm[i] = pal_list[i];
    menu_set_palettes(nm, pal_n);
}
/* The same bar also carries the frontend's own notices -- the volume, so far.
 * Doc, 2026-09-17: "the laptop hardware sound keys do not seem to do
 * anything."  They did: the setting had gone 80 -> 100 under his fingers.
 * But the only sign was a printf to a tty hidden behind the KMS glass, so a
 * key that worked and a key that did not looked exactly alike. */
static const char *echo_tag = "remote: ";
static void echo_note(const char *txt)
{
    snprintf(echo_txt, sizeof echo_txt, "%s", txt); echo_len = (int) strlen(echo_txt);
    echo_tag = ""; echo_until = SDL_GetTicks() + 2000;
}
static void echo_key(int k)
{
    char s[4] = { 0 };
    if (k >= 0x20 && k < 0x7F) s[0] = (char) k;
    else if (k == 0x0D || k == '\n') s[0] = 0x14;               /* CP437: a pilcrow for Enter */
    else if (k == 0x09) s[0] = 0x1A;                             /* an arrow for Tab */
    else if (k == 0x08) s[0] = 0x11;                             /* a left-pointing triangle for Backspace */
    else if (k == 0x1B) strcpy(s, "Esc");
    else if (k >= 0x80 && k <= 0x83) s[0] = "\x18\x19\x1B\x1A"[k - 0x80];   /* the arrow keys */
    else if (k >= 0x90 && k <= 0x9B) snprintf(s, sizeof s, "F%d", k - 0x8F);
    else s[0] = (char) 0xFE;                                    /* a key with no mark: a small square */
    int l = (int) strlen(s);
    if (!*echo_tag) { echo_tag = "remote: "; echo_len = 0; }     /* a notice was showing: keys do not append to it */
    if (echo_len + l > 40) { int drop = echo_len + l - 40; memmove(echo_txt, echo_txt + drop, (size_t)(echo_len - drop)); echo_len -= drop; }
    memcpy(echo_txt + echo_len, s, (size_t) l); echo_len += l;
    echo_until = SDL_GetTicks() + 4000;
}
/* Why the machine stopped, on stderr with the wall clock (the K4510 Linux keeps
 * it in ~/k4510/DIAG/emulator-*.log while the log switch is on): every way out,
 * and a heartbeat every ten seconds, so a freeze shows as the beats stopping.
 * Put in to explain an ssh session that ended on the Dell (2026-09-12). */
static void mlog(const char *what)
{
    struct timespec ts; struct tm tm;
    clock_gettime(CLOCK_REALTIME, &ts); localtime_r(&ts.tv_sec, &tm);
    fprintf(stderr, "%02d:%02d:%02d.%03ld %s\n", tm.tm_hour, tm.tm_min, tm.tm_sec, ts.tv_nsec / 1000000, what); fflush(stderr);
}
static volatile sig_atomic_t viewed;           /* the screen was read from outside: REMOTE says so for a minute */
static void shot_signal(int sig) { (void) sig; shot_req = 1; viewed = 1; }
/* The machine's text screen, as text -- SIGUSR2 (tools/k4510-screen), for
 * reading the machine from another computer without a picture (Doc,
 * 2026-09-14: the remote harness, "standardize it ... for testing and
 * remote debugging").  The text layer's own map (layer 0's MAP and STRIDE, a
 * text32 cell is four bytes, the glyph first), every row of the glass --
 * 60, 30 or 25 by VICKY's mode -- the status bands included, as CP437
 * bytes, trailing blanks trimmed, blank rows kept so a row is always the
 * same line.  Written beside and renamed, so a reader never sees half of
 * one.  The F12 menu draws over the picture, not into this map: a first line
 * says when it is open. */
/* Sidebar-savers (Doc's brainshot, 2026-09-14: "scrolling tilemaps or
 * colorcycling maps that run on the sidebars if they are not being used by
 * something else").  The first, the gradient: the border colour's hue turned
 * once round the colour wheel down the screen, in bands of four machine rows
 * (so it steps at the machine's own pixel size), rolling once every 24
 * seconds, a little darker than the border so it stays calm beside text.
 * Integer HSV: hue 0-1535 (six segments of 256), saturation and value 0-255. */
static uint32_t sb_hue_rgb(int hue, int sat, int val)
{
    hue %= 1536; if (hue < 0) hue += 1536;
    int seg = hue >> 8, f = hue & 255, r, g, b;
    int p = val * (255 - sat) / 255, q = val * (255 - sat * f / 255) / 255, u = val * (255 - sat * (255 - f) / 255) / 255;
    switch (seg) {
    case 0: r = val; g = u; b = p; break;   case 1: r = q; g = val; b = p; break;   case 2: r = p; g = val; b = u; break;
    case 3: r = p; g = q; b = val; break;   case 4: r = u; g = p; b = val; break;   default: r = val; g = p; b = q; break;
    }
    return 0xFF000000u | (uint32_t) r << 16 | (uint32_t) g << 8 | (uint32_t) b;
}
static int sb_rgb_hue(uint32_t c, int *sat, int *val)
{
    int r = (int)(c >> 16 & 255), g = (int)(c >> 8 & 255), b = (int)(c & 255);
    int mx = r > g ? (r > b ? r : b) : (g > b ? g : b), mn = r < g ? (r < b ? r : b) : (g < b ? g : b), d = mx - mn, h;
    *val = mx; *sat = mx ? d * 255 / mx : 0;
    if (!d) return 0;
    if (mx == r) h = (g - b) * 256 / d; else if (mx == g) h = 512 + (b - r) * 256 / d; else h = 1024 + (r - g) * 256 / d;
    return h < 0 ? h + 1536 : h;
}
static volatile sig_atomic_t screen_req;
static void screen_signal(int sig) { (void) sig; screen_req = 1; viewed = 1; }
/* SIGHUP -- the ssh or terminal that started us going away -- ends the run the
 * way closing the window does, so settings are saved and the Tube is stopped.
 * SDL does this for INT and TERM itself; HUP it leaves at "die now". */
static volatile sig_atomic_t hup_req;
static void hup_signal(int sig) { (void) sig; hup_req = 1; }
static void screen_save(void)
{
    uint32_t map = (uint32_t) vicky_read(0x1C) | ((uint32_t) vicky_read(0x1D) << 8) | ((uint32_t) vicky_read(0x1E) << 16) | ((uint32_t) vicky_read(0x1F) << 24);
    int stride = vicky_read(0x16) | (vicky_read(0x17) << 8), ctrl = vicky_read(0);
    /* bit 3 a 200-line field (25 rows); bit 2 lines halved and bit 1 320 wide --
     * which halves the lines too (MODE 2) -- 30; neither, the whole 480: 30
     * rows of 8x16 (layer 0's cell bits), or 60 of 8x8 for a program's own */
    int rows = (ctrl & 8) ? 25 : ((ctrl & 6) || (vicky_read(0x10) & 0x60)) ? 30 : 60, cols = stride > 0 && stride <= 180 ? stride : 80;
    if (ctrl & 0x20) rows = vicky_glass_h() / vicky_cell_h(0);   /* the HD family: 8, 16 or 32 tall */
    mkdir("shots", 0755);
    FILE *f = fopen("shots/.screen.tmp", "wb");
    if (!f) return;
    if (menu_is_open()) fputs("# the menu is open over this screen\n", f);
    for (int r = 0; r < rows; r++) {
        char line[200]; int n = 0;
        for (int c = 0; c < cols; c++) {
            uint8_t ch = mem_peek((map + (uint32_t)(r * stride + c) * 4) & 0x0FFFFFFFu);
            line[n++] = (ch < 0x20 || ch == 0x7F) ? ' ' : (char) ch;
        }
        while (n && line[n - 1] == ' ') n--;
        fwrite(line, 1, (size_t) n, f); fputc('\n', f);
    }
    fclose(f);
    rename("shots/.screen.tmp", "shots/screen.txt");
}
/* The name both pictures of one screenshot share: shots/shot-<date>-<time>-<ms> */
static char shot_base[64];
/* The frame (Doc, 2026-10-07): the border and the status bands are one colour,
 * F12 -> Video -> Frame colour.  Following the palette it is that palette
 * entry, as the border always was; not following, it is the VIC-II colour of
 * that number whatever the palette, and the bands' text is near-black or
 * near-white, whichever reads better on it for normal and protan eyes alike.
 * The band lines (vicky_band_lines) are then drawn through fpal, which is the
 * palette with the bands' two entries replaced.  frame_vic, frame_rgb and the
 * text's contrast are in core/ui/frame.c, shared with the Personality Chooser. */
static uint32_t pal[256], mpal[256], fpal[256], fmpal[256];   /* the palette, behind the menu; with the frame's colours */
static int frame_fixed;                                        /* the band lines go through fpal this frame */
static const uint32_t *row_pal(int y, int menu)                 /* the table line y of the frame is drawn through */
{
    if (frame_fixed && y >= 0 && y < VICKY_HEIGHT && vicky_band_lines()[y]) return menu ? fmpal : fpal;
    return menu ? mpal : pal;
}
static void shot_save(const uint8_t *src, const uint8_t *ov, const uint32_t *pal_unused, const uint32_t *upal) {
    const int W = vicky_out_w(), H = vicky_out_h(), ROW = 1 + W * 3;   /* the picture as drawn: the glass, or twice it with HD text */
    static uint8_t raw[VICKY_HEIGHT * (1 + VICKY_WIDTH * 3)];
    for (int y = 0; y < H; y++) {
        uint8_t *d = raw + y * ROW; *d++ = 0;                      /* filter: none */
        for (int x = 0; x < W; x++) {
            int o = ov ? ov[(y * UI_H / H) * UI_W + x * UI_W / W] : 0;
            uint32_t p = o ? upal[o] : row_pal(y, ov != NULL)[src[y * VICKY_WIDTH + x]];
            *d++ = (uint8_t)(p >> 16); *d++ = (uint8_t)(p >> 8); *d++ = (uint8_t) p;
        }
    }
    struct timeval tv; struct tm tm; char path[72];
    gettimeofday(&tv, NULL); localtime_r(&tv.tv_sec, &tm);
    mkdir("shots", 0755);
    size_t l = strftime(shot_base, sizeof shot_base, "shots/shot-%Y%m%d-%H%M%S", &tm);
    snprintf(shot_base + l, sizeof shot_base - l, "-%03ld", (long)(tv.tv_usec / 1000));
    snprintf(path, sizeof path, "%s.png", shot_base);
    png_write(path, raw, W, H);
}
/* ...and the whole display beside it, <name>-full.png: the window or screen as
 * the renderer has it -- border, sides, bars, the key echo -- read back before
 * it is shown (Doc, 2026-10-06: the border was a different grey from the bars,
 * and the machine's picture could not show it).  W x H device pixels, ARGB. */
static void shot_save_full(const uint32_t *px, int W, int H) {
    uint8_t *raw = malloc((size_t) H * (size_t)(1 + W * 3));
    char path[80];
    if (!raw) return;
    for (int y = 0; y < H; y++) {
        uint8_t *d = raw + (size_t) y * (size_t)(1 + W * 3); *d++ = 0;
        for (int x = 0; x < W; x++) { uint32_t p = px[(size_t) y * W + x]; *d++ = (uint8_t)(p >> 16); *d++ = (uint8_t)(p >> 8); *d++ = (uint8_t) p; }
    }
    snprintf(path, sizeof path, "%s-full.png", shot_base);
    png_write(path, raw, W, H);
    free(raw);
}

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
static volatile int16_t ring[1 << 15]; static volatile unsigned ring_w, ring_h;   /* the slots volatile too: that is what orders a sample's store before its index's */
#define RING_MASK ((1 << 15) - 1)
#define RING_TARGET (1024 + 800)          /* one callback, plus a frame */
#define RING_CAP    (RING_TARGET + 800)   /* a frame of slack above the lead */
#define RING_DEPTH  ((ring_w - ring_h) & RING_MASK)
/* The audio device closes in silence (2026-10-06, for time on battery): an
 * open stream keeps the sound hardware awake and interrupting fifty times a
 * second even when every sample is zero.  sound_ms is when a sample last was
 * not; ring_put notes it. */
static Uint32 sound_ms;
static inline void ring_put(int16_t v)
{
    if (RING_DEPTH < RING_CAP) { ring[ring_w & RING_MASK] = v; ring_w = ring_w + 1; }   /* the sample, THEN the index: `ring[ring_w++] = v` let gcc publish
                                                                                           * the index first, and the callback played a stale slot (review 2026-09-17) */
    if (v) sound_ms = SDL_GetTicks();
}
static int vol_machine(void);                 /* below: the volume the machine's own sound is made at */
static void audio_cb(void *ud, Uint8 *stream, int len)
{
    (void)ud; int16_t *out = (int16_t *)stream; int n = len / 2;
    int gap = 0;
    for (int i = 0; i < n; i++) { if (ring_h != ring_w) out[i] = ring[ring_h++ & RING_MASK]; else { out[i] = 0; gap = 1; } }
    if (gap && io_audio_gaps != 0xFFFF) io_audio_gaps++;     /* one per callback that ran dry: what "choppy" is, counted */
    { int mv = vol_machine(); int q = (int)((int64_t) mv * mv * mv * 32768 / 1000000);   /* the same cube as vol_gain, computed here without its shared cache (the audio thread must not touch it) */
      navi_mix(out, n, q); }                                 /* the Navidrome radio, decoded elsewhere, mixed HERE on the audio
                                                             * thread (near idle) -- not on the emulation thread, which is at
                                                             * 80% of a core running the machine and starved the radio to a
                                                             * scratch (Doc, 2026-09-17: aplay alone was clean, so it is the
                                                             * thread, not the audio path). */
}
#define CPU_HZ 40500000           /* MEGA65-class; the ceiling is ours, per the design */
/* the emulated clock is a setting (cpu.clock): full on the desktop, 20 MHz on
 * the Pi by default, where the whole machine would otherwise run at 20 fps */
static unsigned cpu_hz_now = CPU_HZ, cycles_per_line = CPU_HZ / 60 / 480;
static int frame_lines = 480;                 /* this frame's lines: 480, or an HD mode's height (vicky_glass_h) */
/* what the guest reads at SYS+$36: the wall clock, not the frame count */
static uint32_t sdl_ms_now(void) { return (uint32_t)SDL_GetTicks(); }
/* the governor steps down above GOV_LATE_MS of the frame spent inside the
 * machine: 14 ms of 16.67 leaves the frontend its texture and its present,
 * and a machine costing more than that is not holding 60 frames a second.
 * It steps back up too (2026-09-18): the rules are in core/governor.h, where
 * they can be tested without a window. */
#include "../core/governor.h"
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
static int clock_step_above(int cur)
{
    unsigned cur_hz = settings_cpu_hz_of(cur), best_hz = 0; int best = -1;
    for (int i = 0; i < CPUCLK_COUNT; i++) {
        unsigned h = settings_cpu_hz_of(i);
        if (h > cur_hz && (!best_hz || h < best_hz)) { best_hz = h; best = i; }
    }
    return best;
}
#define CYCLES_PER_LINE  cycles_per_line


static int load_file(const char *path, uint8_t *buf, size_t max)
{
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    size_t n = fread(buf, 1, max, f);
    fclose(f);
    return (int)n;
}

static uint8_t font_menu[2048];                      /* unscii-8: the machine's 8x8 font, and the menu's */
static uint8_t font_panel[4096]; static int font_panel_rows = 16;  /* unscii-16: MODE 0's font, and the side panel's */
static uint8_t font_437_8[2048], font_437_16[4096];  /* both again in strict CP437, for TELNET's BBS sessions (docs/K4510-CODEPAGE.md) */
/* The HD text fonts (vicky_hd_font, tools/mkhdfonts.py): per face, the K4510
 * page's order and CP437's; a face whose files are missing is drawn as unscii. */
static uint8_t hd_fonts[HDFONT_COUNT][PAGE_COUNT][16384], hd_fonts16[HDFONT_COUNT][PAGE_COUNT][8192]; static int hd_have[HDFONT_COUNT];
static uint8_t hd_fonts48[HDFONT_COUNT][PAGE_COUNT][36864], hd_fonts24[HDFONT_COUNT][PAGE_COUNT][18432]; static int hd_have3[HDFONT_COUNT];   /* the same at 3x: 480x360 (2026-10-07) */
/* Is another console up (Ctrl+Alt+F2...)?  SDL on KMSDRM reads the keyboard
 * and mouse through evdev, which knows nothing of consoles: switched away to
 * tty2, every key typed at its login went to the machine as well -- into the
 * Terminal screen's session on another computer (Doc, 2026-10-08: "typing
 * from the consoles is leaking through to the SDL side").  While our console
 * is not the active one, input is dropped.  Asked of the kernel (VT_GETSTATE)
 * at most every 100 ms; 0 off the K4510 Linux or without a console. */
static int vt_away(void)
{
#if defined(__linux__)
    static int fd = -2, ours, away; static Uint32 at;
    if (fd == -2) {
        struct stat st;
        fd = access("/etc/k4510-linux", F_OK) == 0 ? open("/dev/tty", O_RDONLY) : -1;
        if (fd >= 0 && (fstat(fd, &st) != 0 || major(st.st_rdev) != 4 || minor(st.st_rdev) < 1 || minor(st.st_rdev) > 63)) { close(fd); fd = -1; }
        else if (fd >= 0) ours = (int) minor(st.st_rdev);
    }
    if (fd < 0) return 0;
    if (SDL_GetTicks() - at >= 100) {
        struct vt_stat vs; at = SDL_GetTicks();
        away = ioctl(fd, VT_GETSTATE, &vs) == 0 && vs.v_active != ours;
    }
    return away;
#else
    return 0;
#endif
}
/* RADIO at the machine's prompt (2026-10-08): the remote for k4510-radio, the
 * player on the Linux beside the machine -- it starts one in the background
 * (MUSIC [playlist], STATION n, PODCAST n [m]) and steers whichever is
 * running, in the Terminal screen or not (NEXT PAUSE RESUME STOP; alone, what
 * plays).  The Navidrome sidebar, while it plays, keeps RADIO as it was. */
static const char *radio_prog(void)
{
    return access("/usr/local/bin/k4510-radio", X_OK) == 0 ? "/usr/local/bin/k4510-radio" : "linux/config/includes.chroot/usr/local/bin/k4510-radio";
}
static void radio_run(const char *arg, char *reply, size_t max)   /* k4510-radio ARG, its answer into reply */
{
    char cmd[160]; FILE *p; size_t n = strlen(reply);
    snprintf(cmd, sizeof cmd, "python3 %s %s 2>&1", radio_prog(), arg);
    if (!(p = popen(cmd, "r"))) return;
    while (n + 1 < max && fgets(reply + n, (int)(max - n), p)) n = strlen(reply);
    pclose(p);
}
static void radio_cmd(const char *cmd, char *reply, size_t max)
{
    char word[16], rest[128], *av[12]; int i = 0, ac = 0;
    if (navi_playing()) { navi_command(cmd, reply, max); return; }
    while (*cmd == ' ') cmd++;
    for (; *cmd && *cmd != ' ' && i < 15; cmd++) word[i++] = (char) tolower((unsigned char) *cmd);
    word[i] = 0;
    while (*cmd == ' ') cmd++;
    snprintf(rest, sizeof rest, "%s", cmd);
    reply[0] = 0;
    if (!word[0] || !strcmp(word, "status") || !strcmp(word, "help")) {
        radio_run("now", reply, max);
        snprintf(reply + strlen(reply), max - strlen(reply),
                 "RADIO MUSIC [playlist]   RADIO STATION n   RADIO PODCAST n [m]   (your lists: /SYSTEM/ETC/RADIO.CFG)\n"
                 "RADIO NEXT / PAUSE / RESUME / STOP     -- or k4510-radio in the Terminal (Alt+2)\n");
        return;
    }
    if (!strcmp(word, "next") || !strcmp(word, "pause") || !strcmp(word, "resume") || !strcmp(word, "stop") || !strcmp(word, "off")) {
        radio_run(!strcmp(word, "off") ? "stop" : word, reply, max);
        if (!reply[0]) snprintf(reply, max, "%s\n", word);
        return;
    }
    if (strcmp(word, "music") && strcmp(word, "play") && strcmp(word, "station") && strcmp(word, "podcast")) {
        snprintf(reply, max, "RADIO: MUSIC [playlist], STATION n, PODCAST n [m], NEXT, PAUSE, RESUME, STOP\n"); return;
    }
    snprintf(reply, max, "starting %s%s%s in the background (RADIO alone says what plays)\n", word, rest[0] ? " " : "", rest);
    av[ac++] = "python3"; av[ac++] = (char *) radio_prog(); av[ac++] = "--background"; av[ac++] = !strcmp(word, "play") ? "music" : word;
    for (char *t = strtok(rest, " "); t && ac < 11; t = strtok(NULL, " ")) av[ac++] = t;
    av[ac] = NULL;
    pid_t pid = fork();
    if (pid == 0) {                                      /* twice, so the player is nobody's child here */
        setsid();
        if (fork() == 0) { execvp("python3", av); _exit(127); }
        _exit(0);
    }
    if (pid > 0) waitpid(pid, NULL, 0);
}
/* F12 -> Machine -> Save and power off (Doc, 2026-10-07, in place of a Linux
 * hibernate): the whole machine to this file, then the computer off; the next
 * start loads it and deletes it, so you are back where you were -- the
 * program, the screen, the memory.  Not in it, as with any state: the Tube's
 * co-processor, the second screen's session, network connections.  A file
 * this build cannot read (a state from another version) is set aside as
 * .old and the machine boots as usual. */
#define RESUME_FILE "k4510-resume.k4s"
/* A power off on its way (F12 -> Power off, Save and power off): the mark the
 * session loop (k4510-session) looks for when this program ends, so it waits
 * for the halt instead of showing the Personality Chooser.  F12 -> Quit
 * leaves none, and the Chooser comes up.  k4510-poweroff writes it too. */
#define HALTING_FILE "/tmp/k4510-halting"
static void halting_mark(int on)
{
    if (on) { FILE *f = fopen(HALTING_FILE, "w"); if (f) fclose(f); }
    else remove(HALTING_FILE);
}
static const char *slot_path(int n) { static char p[32]; snprintf(p, sizeof p, "k4510-slot%d.k4s", n + 1); return p; }
static void slot_refresh(int n)                      /* the slot's row: its file's date, or "empty" */
{
    struct stat st; char b[24];
    if (stat(slot_path(n), &st)) { menu_slot(n, ""); return; }
    struct tm *tm = localtime(&st.st_mtime);
    if (tm) strftime(b, sizeof b, "%b %d %H:%M", tm); else snprintf(b, sizeof b, "%ld KB", (long)(st.st_size >> 10));
    menu_slot(n, b);
}
/* The screen font: unscii, the one font since 2026-09-14 (Doc: "pick one font
 * and jettison all the rest").  8x8 at $010000 for the 240-line modes, 8x16
 * at $010800 for 640x480; the ROM points VICKY at whichever the mode wants. */
const uint16_t *term_page_table(void); void term_set_page(int k4510); int term_get_page(void); int term_page_request(void);   /* core/term.h */
/* Brainshots read and moved to PROCESSED (tools/k4510-remote ideas) go 48 hours
 * after that, at the next start (Doc, 2026-09-15); the reader keeps its own copy.
 * The move re-stamps them, so the hours count from processing, not from IDEA. */
/* What the sidebars keep across a power cycle, to STATE.DAT: every five minutes
 * and at the end (sidebars step 5).  A scene not yet started keeps nothing. */
static void sidebar_save_states(void)
{
    for (int i = 0; i < sidebars_count(); i++) {
        int b = sidebars_info(i)->builtin; uint8_t *st; size_t n;
        if (b < SIDEBAR_HALLOWEEN || !(n = saver_state(b - SIDEBAR_HALLOWEEN, &st))) continue;
        sidebars_state_write(i, st, n); free(st);
    }
}
static void prune_brainshots(const char *fsroot)
{
    char dir[600], p[900]; DIR *d; struct dirent *e; struct stat st; time_t now = time(NULL);
    snprintf(dir, sizeof dir, "%s/SYSTEM/BRAINSHOTS/PROCESSED", fsroot);
    if (!(d = opendir(dir))) return;
    while ((e = readdir(d))) {
        size_t n = strlen(e->d_name);
        if (n < 4 || e->d_name[n - 4] != '.' || (e->d_name[n - 3] | 32) != 't' || (e->d_name[n - 2] | 32) != 'x' || (e->d_name[n - 1] | 32) != 't') continue;
        snprintf(p, sizeof p, "%s/%s", dir, e->d_name);
        if (stat(p, &st) == 0 && S_ISREG(st.st_mode) && now - st.st_mtime > 48 * 3600) unlink(p);
    }
    closedir(d);
}
/* MODE 5's fonts, 16 wide: the F12 font in the page in use -- or unscii,
 * doubled -- placed where the ROM points VICKY (K4510_FONT32/16W_PHYS).  Again
 * whenever the font or the page changes (the frame loop), or a power cycle or a
 * state load puts RAM back. */
static int wide_face = -1, wide_page = -1;
static void wide_fonts(int face, int page)
{
    static uint8_t f32[16384], f16[8192];
    wide_face = face; wide_page = page;
    if (face != HDFONT_UNSCII && hd_have[face]) { memcpy(f32, hd_fonts[face][page], 16384); memcpy(f16, hd_fonts16[face][page], 8192); }
    else {
        const uint8_t *s16 = page == PAGE_K4510 ? font_panel : font_437_16, *s8 = page == PAGE_K4510 ? font_menu : font_437_8;
        for (int i = 0; i < 256 * 32; i++) { uint8_t b = s16[(i >> 5) * 16 + ((i & 31) >> 1)]; unsigned w = 0;
            for (int k = 0; k < 8; k++) if (b & (0x80 >> k)) w |= 0xC000u >> (2 * k);
            f32[i * 2] = (uint8_t)(w >> 8); f32[i * 2 + 1] = (uint8_t) w; }
        for (int i = 0; i < 256 * 16; i++) { uint8_t b = s8[(i >> 4) * 8 + ((i & 15) >> 1)]; unsigned w = 0;
            for (int k = 0; k < 8; k++) if (b & (0x80 >> k)) w |= 0xC000u >> (2 * k);
            f16[i * 2] = (uint8_t)(w >> 8); f16[i * 2 + 1] = (uint8_t) w; }
    }
    mem_load(K4510_FONT32_PHYS, f32, sizeof f32); mem_load(K4510_FONT16W_PHYS, f16, sizeof f16);
    vicky_dirty = 1;
}
static void load_fonts(void)
{
    mem_load(K4510_FONT8_PHYS, font_menu, sizeof font_menu);
    mem_load(K4510_FONT16_PHYS, font_panel, sizeof font_panel);
    mem_load(K4510_FONT8_437_PHYS, font_437_8, sizeof font_437_8);        /* zeros if the files were missing: TELNET */
    mem_load(K4510_FONT16_437_PHYS, font_437_16, sizeof font_437_16);     /* sees an empty font and stays on the machine's */
    mem_load(K4510_FONT8_K_PHYS, font_menu, sizeof font_menu);            /* the K4510 page's pair, for CODEPAGE K4510 */
    mem_load(K4510_FONT16_K_PHYS, font_panel, sizeof font_panel);
    term_set_page(settings_get(SET_TEXT_CODEPAGE));                    /* the live slots: CP437, unless the K4510 page was chosen */
    wide_face = -1;                                                    /* MODE 5's: placed again by the frame loop */
}

/* The keyboard when SDL sends no text.
 *
 * A printable character normally arrives as SDL_TEXTINPUT, already composed by
 * the host's layout.  On the K4510 Linux that never happens: the appliance draws
 * straight on the screen (SDL_VIDEODRIVER=kmsdrm) and SDL's own evdev keyboard
 * gives us key codes and no text at all, so the F12 menu -- which is arrow keys
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

/* ---- the machine's own keyboard layout (F12 -> Input -> Keyboard layout) ----
 * "Host" types what the host composed (SDL_TEXTINPUT).  Any other layout is
 * the emulator's own table, from the same XKB data the Linux consoles use
 * (tools/mkkbdmaps.py -> core/kbdmaps.h), applied to the PHYSICAL key -- so it
 * takes effect the moment it is chosen: on KMSDRM, SDL copies the kernel
 * keymap once at init and could never follow a change.  Shift, AltGr, Caps
 * Lock on letters, dead keys, and Ctrl+letter by the layout's letter.  Doc,
 * 2026-09-12: "selectable in the K4510, immediately, the host in sync". */
#include "../core/kbdmaps.h"
#include "../core/codepage.h"
static const uint8_t sdl2lnx[] = {                        /* SDL scancode -> Linux keycode, the typing keys */
    [SDL_SCANCODE_A] = 30, [SDL_SCANCODE_B] = 48, [SDL_SCANCODE_C] = 46, [SDL_SCANCODE_D] = 32, [SDL_SCANCODE_E] = 18,
    [SDL_SCANCODE_F] = 33, [SDL_SCANCODE_G] = 34, [SDL_SCANCODE_H] = 35, [SDL_SCANCODE_I] = 23, [SDL_SCANCODE_J] = 36,
    [SDL_SCANCODE_K] = 37, [SDL_SCANCODE_L] = 38, [SDL_SCANCODE_M] = 50, [SDL_SCANCODE_N] = 49, [SDL_SCANCODE_O] = 24,
    [SDL_SCANCODE_P] = 25, [SDL_SCANCODE_Q] = 16, [SDL_SCANCODE_R] = 19, [SDL_SCANCODE_S] = 31, [SDL_SCANCODE_T] = 20,
    [SDL_SCANCODE_U] = 22, [SDL_SCANCODE_V] = 47, [SDL_SCANCODE_W] = 17, [SDL_SCANCODE_X] = 45, [SDL_SCANCODE_Y] = 21,
    [SDL_SCANCODE_Z] = 44,
    [SDL_SCANCODE_1] = 2, [SDL_SCANCODE_2] = 3, [SDL_SCANCODE_3] = 4, [SDL_SCANCODE_4] = 5, [SDL_SCANCODE_5] = 6,
    [SDL_SCANCODE_6] = 7, [SDL_SCANCODE_7] = 8, [SDL_SCANCODE_8] = 9, [SDL_SCANCODE_9] = 10, [SDL_SCANCODE_0] = 11,
    [SDL_SCANCODE_MINUS] = 12, [SDL_SCANCODE_EQUALS] = 13, [SDL_SCANCODE_LEFTBRACKET] = 26, [SDL_SCANCODE_RIGHTBRACKET] = 27,
    [SDL_SCANCODE_BACKSLASH] = 43, [SDL_SCANCODE_NONUSHASH] = 43, [SDL_SCANCODE_SEMICOLON] = 39, [SDL_SCANCODE_APOSTROPHE] = 40,
    [SDL_SCANCODE_GRAVE] = 41, [SDL_SCANCODE_COMMA] = 51, [SDL_SCANCODE_PERIOD] = 52, [SDL_SCANCODE_SLASH] = 53,
    [SDL_SCANCODE_SPACE] = 57, [SDL_SCANCODE_NONUSBACKSLASH] = 86,
};
static uint32_t kbd_dead;                                 /* a dead key's accent, waiting for its letter */
static uint8_t cp437_of(unsigned long cp);
static const uint32_t *layout_entry(int layout, SDL_Scancode sc)
{
    if (layout <= 0 || layout > KBD_MAPS || sc < 0 || sc >= (int)sizeof sdl2lnx || !sdl2lnx[sc]) return NULL;
    return kbd_maps[layout - 1][sdl2lnx[sc]];
}
static void push_unicode(uint32_t u)
{
    if (u >= 0x20 && u < 0x7F) kbd_push((uint8_t) u);
    else if (u) { uint8_t b = cp437_of(u); if (b) kbd_push(b); }
}
static int layout_key(int layout, SDL_Scancode sc, SDL_Keymod m)   /* 1: the layout typed it (or knowingly nothing) */
{
    const uint32_t *e = layout_entry(layout, sc);
    if (!e) return 0;
    int shift = (m & KMOD_SHIFT) != 0, altgr = (m & (KMOD_RALT | KMOD_MODE)) != 0;
    if ((e[0] & KBD_CAPS) && (m & KMOD_CAPS)) shift = !shift;
    uint32_t v = e[(shift ? 1 : 0) + (altgr ? 2 : 0)];
    if (!v) return 1;
    if (v & KBD_DEAD) {                                   /* a dead key: wait for the letter; twice types the accent */
        uint32_t acc = v & 0xFFFF;
        if (kbd_dead) { push_unicode(kbd_dead); if (kbd_dead == acc) { kbd_dead = 0; return 1; } }
        kbd_dead = acc; return 1;
    }
    uint32_t u = v & KBD_CHAR;
    if (kbd_dead) {
        uint32_t out = 0;
        for (unsigned i = 0; i < sizeof kbd_compose / sizeof kbd_compose[0]; i++)
            if (kbd_compose[i].dead == kbd_dead && kbd_compose[i].base == u) { out = kbd_compose[i].out; break; }
        if (out && (out < 0x80 || cp437_of(out))) u = out;   /* a letter the page has no place for (CP437's A-grave): the plain letter */
        else if (!out) push_unicode(kbd_dead);            /* no such letter: the accent, then the key */
        kbd_dead = 0;
    }
    push_unicode(u);
    return 1;
}

/* Unicode -> code page 437, for the half of the machine's font above ASCII.
 * Only the letters and marks a keyboard can actually produce are here; the box
 * drawing has no key.  0 means "this machine cannot show it". */
static uint8_t cp437_of(unsigned long cp)                /* Unicode -> the K4510 code page (core/codepage.h); 0: no place */
{
    if (cp == 0x00A0) return 0x20;                     /* no-break space: a space */
    { const uint16_t *t = term_page_table();          /* the page in use: CP437, or the K4510 page (CODEPAGE) */
      for (unsigned i = 0x80; i < 0x100; i++) if (t[i] == cp) return (uint8_t) i; }
    return 0;
}

/* The mouse.  SDL hands us logical coordinates (the renderer's logical size
 * is the machine's picture), so machine pixels are one
 * division and the border's shrink away.  geo_b and the rest are copied from the
 * frame code each frame; the picture cannot move between them. */
static int geo_b = 0, geo_xd = 0, geo_yd = 0; static double geo_s = 1.0;   /* Placement: the picture's device offset and scale (1, 0, 0 when SDL maps) */
static int mouse_x = -1, mouse_y = -1, mouse_btn, wheel_acc, dx_acc, dy_acc;
static int to_machine(int v, int full) { int m = (v - geo_b) * full / (full - 2 * geo_b); return m < 0 ? 0 : m >= full ? full - 1 : m; }
/* the menu is drawn at 640x480 whatever the glass: the pointer is taken there */
/* Where the menu lies on the glass, in glass pixels: whole menu pixels and
 * centred since 2026-10-06, so not always the whole picture (set as it is drawn). */
static int menu_gx0, menu_gy0, menu_gw, menu_gh;
static int ui_mx(int x) { return menu_gw > 0 ? (x - menu_gx0) * UI_W / menu_gw : x * UI_W / vicky_glass_w(); }
static int ui_my(int y) { return menu_gh > 0 ? (y - menu_gy0) * UI_H / menu_gh : y * UI_H / vicky_glass_h(); }
static void mouse_to_menu(void) { if (menu_is_open() && mouse_x >= 0) menu_mouse(ui_mx(mouse_x), ui_my(mouse_y), mouse_btn, wheel_acc); }
/* The pointer stays on the machine (Doc, 2026-09-14: "limit mouse to k4510
 * screen only ... it doesnt go into sidebars, or above or below active screen
 * ... it can however go into side bars if the processor info sidebar is
 * present").  Full screen only -- a window on a desktop must never trap the
 * pointer -- which on the K4510 Linux is always.  confine_r is in WINDOW
 * coordinates (SDL_GetMouseState's), worked out where the picture is placed:
 * the canvas, or the whole screen when the side panel shares it.  Two
 * mechanisms, because SDL_SetWindowMouseRect is not honoured by every video
 * driver (KMSDRM draws its own cursor): SDL's rect where it works, and a warp
 * back to the edge on any motion that got out anyway. */
static int confine_on; static SDL_Rect confine_r;
static uint64_t drawn_bits;                   /* the last 64 frames, a bit each: drawn (1) or not; the pacer's measure of rest */
static Uint32 input_ms;                       /* the last key, button or motion the host saw */
/* The wait between frames (2026-10-09, the JIM timing work).  It was one
 * SDL_Delay to the deadline -- three frames at rest -- so a key waited for the
 * next frame to be read, 8 ms on average and 50 ms after two idle seconds,
 * before it reached the Terminal's pty.  Now the loop waits on SDL's queue:
 * an event ends the wait at once, is handled (a key for the Terminal is
 * written to its pty there and then), and the wait goes on to the same
 * deadline, so the machine keeps its 60 frames a second.  An event while at
 * rest ends the rest.  So do bytes from the Terminal's pty: at rest the wait
 * is cut into frames and the pty looked at between them.  Out of rest that
 * is not needed -- the next frame reads them before it draws (io_frame_start).
 * X11 and Wayland block in SDL_WaitEventTimeout.  KMSDRM (the K4510 Linux)
 * cannot -- SDL would wake every millisecond there -- so the wait is a poll()
 * on the input devices themselves (/dev/input/event*, opened read-only beside
 * SDL's own; each reader has its own queue, so ours are read and dropped) and
 * the pty: it sleeps until a key, the pointer or the session moves, and rest
 * keeps its 20 wakeups a second.  Where the devices cannot be opened, slices:
 * 4 ms, 10 ms at rest. */
static Uint64 pace_next, pace_until;          /* the next frame's deadline (perf counter); the wait's end, later at rest */
#define PACE_SLICE_MS 4
#define PACE_REST_SLICE_MS 10
#define PACE_EV_MAX 32
static int pace_ev[PACE_EV_MAX], pace_evn = -1; static Uint32 pace_ev_at;
static void pace_ev_scan(void)                /* the input devices, again every five seconds: one plugged in is watched too */
{
    glob_t g;
    for (int i = 0; i < pace_evn; i++) close(pace_ev[i]);
    pace_evn = 0; pace_ev_at = SDL_GetTicks();
    if (glob("/dev/input/event*", 0, NULL, &g) != 0) return;
    for (size_t i = 0; i < g.gl_pathc && pace_evn < PACE_EV_MAX; i++) {
        int fd = open(g.gl_pathv[i], O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (fd >= 0) pace_ev[pace_evn++] = fd;
    }
    globfree(&g);
}
/* KMSDRM's wait: poll the devices and the pty for at most ms.  1 an input
 * device stirred, 2 the pty, 0 the time ran out, -1 no devices to watch. */
static int pace_ev_wait(Uint32 ms, int pty)
{
    struct pollfd p[PACE_EV_MAX + 1]; int n = 0, r;
    if (pace_evn < 0 || SDL_GetTicks() - pace_ev_at >= 5000) pace_ev_scan();
    if (!pace_evn) return -1;
    for (int i = 0; i < pace_evn; i++) { p[n].fd = pace_ev[i]; p[n].events = POLLIN; p[n].revents = 0; n++; }
    if (pty >= 0) { p[n].fd = pty; p[n].events = POLLIN; p[n].revents = 0; n++; }
    if ((r = poll(p, (nfds_t) n, (int) ms)) <= 0) return r < 0 ? -1 : 0;
    if (pty >= 0 && (p[n - 1].revents & POLLIN)) return 2;
    for (int i = 0; i < pace_evn; i++) {
        if (p[i].revents & (POLLERR | POLLHUP | POLLNVAL)) { pace_ev_at = 0; continue; }   /* unplugged: scanned again next time */
        if (p[i].revents & POLLIN) { char b[1536]; while (read(pace_ev[i], b, sizeof b) > 0) ; }   /* whole events: 64 of 24 bytes (96 of 16 on 32-bit); not <linux/input.h>, whose KEY_ names are io.h's */
    }
    return 1;
}
static int pace_wait(void)                    /* 1: woken early -- handle the events and call again; 0: the frame's time */
{
    static int block = -1, pty_seen;
    if (block < 0) { const char *d = SDL_GetCurrentVideoDriver();
                     block = d && (!strcmp(d, "x11") || !strcmp(d, "wayland") || !strcmp(d, "windows") || !strcmp(d, "cocoa")); }
    for (;;) {
        Uint64 now = SDL_GetPerformanceCounter(), f = SDL_GetPerformanceFrequency();
        int resting = pace_until > pace_next, fd; Uint32 ms, slice;
        if (now >= pace_until) { pty_seen = 0; return 0; }
        ms = (Uint32)((pace_until - now) * 1000 / f);
        if (!ms) { pty_seen = 0; return 0; }                  /* under a millisecond: the frame starts now (SDL_Delay floored too) */
        fd = resting && !pty_seen ? io_screen2_fd() : -1;
        if (fd >= 0) { struct pollfd p = { fd, POLLIN, 0 };
                       if (poll(&p, 1, 0) > 0) { pty_seen = 1; pace_until = pace_next; continue; } }   /* the Terminal has something to show: rest is over */
        slice = block ? ms : resting ? PACE_REST_SLICE_MS : PACE_SLICE_MS;
        if (fd >= 0 && slice > 16) slice = 16;                /* at rest on the Terminal: look at the pty once a frame */
        if (slice > ms) slice = ms;
        if (block) { if (SDL_WaitEventTimeout(NULL, (int) slice)) break; }
        else {
            int w = pace_ev_wait(fd >= 0 ? (ms < 16 ? ms : 16) : ms, fd);   /* the devices themselves, or (none) a slice of sleep */
            if (w < 0) SDL_Delay(slice);
            else if (w == 2) { pty_seen = 1; pace_until = pace_next; continue; }
            SDL_PumpEvents();
            if (SDL_PeepEvents(NULL, 1, SDL_PEEKEVENT, SDL_FIRSTEVENT, SDL_LASTEVENT) > 0) break;
        }
    }
    if (pace_until > pace_next) pace_until = pace_next;     /* someone is there: no more rest */
    return 1;
}
static int present_force = 1, frame_static, frame_sides;   /* still frames: draw the window again; nothing around the picture moves;
                                                            * only the sidebars move (they are drawn at 30 a second) */
static int confine_clamp(int *x, int *y)          /* 1 if (x,y) was outside and has been brought to the edge */
{
    int cx = *x, cy = *y;
    if (!confine_on) return 0;
    if (cx < confine_r.x) cx = confine_r.x; else if (cx >= confine_r.x + confine_r.w) cx = confine_r.x + confine_r.w - 1;
    if (cy < confine_r.y) cy = confine_r.y; else if (cy >= confine_r.y + confine_r.h) cy = confine_r.y + confine_r.h - 1;
    if (cx == *x && cy == *y) return 0;
    *x = cx; *y = cy; return 1;
}
/* The touchpad on the bare K4510 Linux (Doc's Dell, 2026-09-11: trackpoint
 * fine, touchpad dead).  With no compositor SDL reads evdev itself, and a
 * touchpad is an absolute multitouch device, so SDL reports FINGERS, not
 * mouse motion, and the machine never saw it.  Here a finger's motion warps
 * the pointer by its delta -- which comes back as an ordinary SDL_MOUSEMOTION
 * with xrel/yrel, so the path above needs nothing -- and a short tap that
 * hardly moved is a click (two fingers: the right button).  KMSDRM only: a
 * desktop's compositor already turns the touchpad into a mouse. */
static int touchpad_rel; static int tp_fingers; static Uint32 tp_down_at; static float tp_moved;
static void touchpad_event(const SDL_Event *e, SDL_Window *win)
{
    int ww, wh; SDL_GetWindowSize(win, &ww, &wh);
    switch (e->type) {
    case SDL_FINGERDOWN: if (++tp_fingers == 1) { tp_down_at = e->tfinger.timestamp; tp_moved = 0; } break;
    case SDL_FINGERMOTION: {
        float dx = e->tfinger.dx * (float) ww, dy = e->tfinger.dy * (float) wh;   /* one sweep of the pad = the window */
        tp_moved += SDL_fabsf(dx) + SDL_fabsf(dy);
        if (tp_fingers == 1) {
            int mx, my; SDL_GetMouseState(&mx, &my);
            mx += (int)(dx + (dx < 0 ? -0.5f : 0.5f)); my += (int)(dy + (dy < 0 ? -0.5f : 0.5f));
            if (mx < 0) mx = 0; if (my < 0) my = 0; if (mx >= ww) mx = ww - 1; if (my >= wh) my = wh - 1;
            confine_clamp(&mx, &my);                                    /* and on the machine's picture, full screen */
            SDL_WarpMouseInWindow(win, mx, my);
        }
        break; }
    case SDL_FINGERUP: {
        int n = tp_fingers; if (tp_fingers > 0) tp_fingers--;
        if (tp_fingers == 0 && e->tfinger.timestamp - tp_down_at < 250 && tp_moved < 8.0f) {
            SDL_Event b; SDL_zero(b); b.button.which = SDL_TOUCH_MOUSEID; b.button.clicks = 1;
            b.button.button = n >= 2 ? SDL_BUTTON_RIGHT : SDL_BUTTON_LEFT;
            b.type = SDL_MOUSEBUTTONDOWN; b.button.state = SDL_PRESSED;  SDL_PushEvent(&b);
            b.type = SDL_MOUSEBUTTONUP;   b.button.state = SDL_RELEASED; SDL_PushEvent(&b);
        }
        break; }
    default: break;
    }
}
/* Mouse capture: a click on the picture confines the host pointer to the
 * window (the coordinates stay absolute, so CHESS still clicks squares);
 * opening the menu, or the window losing focus, lets it go, and closing the
 * menu takes it back.  Not in the browser: SDL's pointer lock there is
 * relative-only and would stop the position registers. */
static SDL_Window *grab_win; static int grabbed, grab_wanted;
static void grab(int on)
{
    if (!grab_win || on == grabbed) return;
    SDL_SetWindowMouseGrab(grab_win, on ? SDL_TRUE : SDL_FALSE);
    grabbed = on;
}
/* ---- the machine, one scanline at a time ------------------------------
 * The frame used to be one loop; it is now a state machine that can stop
 * between scanlines and between instructions, so that the paused machine
 * can be stepped from the side panel (Doc, 2026-09-09: "F8 upon pausing
 * allows for single stepping ... ability to trigger logging or dumping").
 * Running, machine_frame() does exactly what the loop did. */
static uint8_t fb[VICKY_WIDTH * VICKY_HEIGHT];
static Uint64 p_cpu, p_vic, p_snd;            /* the machine's half of the frame, split three ways (PERF.TXT) */
#define PCLK() SDL_GetPerformanceCounter()
static int m_line, m_cyc, m_in_frame;         /* the next scanline; cycles already run on it by single steps; between begin and end */
/* K4510_LATLOG=file (2026-10-09, the JIM timing work): for each frame that
 * first draws bytes from the second screen's pty, a line "read_ns shown_ns"
 * -- when s2_pump read them and when the frame with them was presented, both
 * CLOCK_MONOTONIC -- so byte-to-glass latency can be measured on a real
 * machine (test/jim/latrun.sh). */
static FILE *lat_f; static unsigned long long lat_armed;
static unsigned long long mono_ns(void) { struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts); return (unsigned long long) ts.tv_sec * 1000000000ull + (unsigned long long) ts.tv_nsec; }
static void lat_shown(void) { if (lat_armed) { fprintf(lat_f, "%llu %llu\n", lat_armed, mono_ns()); fflush(lat_f); lat_armed = 0; } }
static FILE *trace_f; static unsigned trace_n;   /* T while paused: every instruction to SYSTEM/LOG/TRACE.TXT */
#define TRACE_MAX 200000                      /* about 12 MB, then it stops itself */
static void trace_toggle(void)
{
    if (trace_f) { fprintf(trace_f, "(trace off at %u lines)\n", trace_n); fclose(trace_f); trace_f = NULL; return; }
    char path[600]; snprintf(path, sizeof path, "%s/SYSTEM/LOG/TRACE.TXT", fs_get_root());
    trace_f = fopen(path, "w"); trace_n = 0;
    if (trace_f) fprintf(trace_f, "K4510 instruction trace  (PC  bytes  instruction  A X Y Z SP P)\n");
    else fprintf(stderr, "K4510: cannot write %s\n", path);
}
static int cpu_run(int cycles)                /* run the CPU for so many cycles; one instruction at a time when tracing */
{
    if (!trace_f) return cpu65_step(cycles);
    int done = 0;
    while (done < cycles && trace_f) {
        char t[48]; panel_disasm(cpu65.pc, t, sizeof t);
        fprintf(trace_f, "%-26s A=%02X X=%02X Y=%02X Z=%02X SP=%04X P=%02X\n", t, cpu65.a, cpu65.x, cpu65.y, cpu65.z, cpu65.s | cpu65.sphi, cpu65_get_pf());
        done += cpu65_step(1);
        if (++trace_n >= TRACE_MAX) { fprintf(trace_f, "(trace stopped itself at %u lines)\n", trace_n); fclose(trace_f); trace_f = NULL; }
    }
    if (done < cycles) done += cpu65_step(cycles - done);
    return done;
}
static void line_begin(void)
{
    if (m_line == 0 && !m_in_frame) {
        { int f = settings_get(SET_VIDEO_FONT), pg = settings_get(SET_TEXT_CODEPAGE) == PAGE_K4510 ? PAGE_K4510 : PAGE_CP437;   /* the HD font for this frame */
          if (f != wide_face || pg != wide_page) wide_fonts(f, pg);
          if (f != HDFONT_UNSCII && hd_have[f]) vicky_hd_font(hd_fonts[f][pg], pg == PAGE_K4510 ? font_panel : font_437_16,
                                                              hd_fonts16[f][pg], pg == PAGE_K4510 ? font_menu : font_437_8);
          else vicky_hd_font(NULL, NULL, NULL, NULL);
          if (f != HDFONT_UNSCII && hd_have3[f]) vicky_hd_font3(hd_fonts48[f][pg], hd_fonts24[f][pg]); else vicky_hd_font3(NULL, NULL);
          if (f != HDFONT_UNSCII && !hd_have[f] && hd_have3[f]) vicky_hd_font(NULL, pg == PAGE_K4510 ? font_panel : font_437_16, NULL, pg == PAGE_K4510 ? font_menu : font_437_8); }   /* the stock glyphs, for 3x alone */
        vicky_begin_frame(fb, VICKY_WIDTH); m_in_frame = 1;
        if (lat_f && io_lat_read_ns && !lat_armed) { lat_armed = io_lat_read_ns; io_lat_read_ns = 0; }   /* read before this raster: drawn by it */
        frame_lines = vicky_glass_h(); cycles_per_line = cpu_hz_now / 60 / (unsigned) frame_lines;   /* a frame is 1/60 s however many lines */
    }
    cpu65.irqLevel = vicky_irq() ? 1 : 0;
}
/* The status bands carry two things the machine itself does not know (Doc,
 * 2026-09-14): at the left of the top band, what is running -- the title
 * stack core/io.c keeps, "LOGO SQUARES.LGO > VI SQUARES.LGO" -- and at
 * the left of the bottom band, the keys typed through the key pipe.  Drawn
 * into the finished frame, not into the machine's memory, in the machine's
 * own font and each band's own colours (read from the band's first cell), so
 * they look like the band and follow its size, the font and the scaling.
 * Not when the bands are off, when a program has claimed them (VICKY's
 * BANDCTL bit 1: they are its to draw), when the console is not the picture,
 * or under the menu.  Whether there are bands at all is VICKY's to say
 * ($D0B5/$D0B7, 2026-10-01) -- this used to repeat the ROM's 40x30 rule.  echo_banded tells the old echo bar it need not draw. */
const char *io_title(void);
static int echo_banded;
static int alt_ate;                 /* an Alt+letter just went as KEY_ALT_*: its SDL_TEXTINPUT is not typed */
static void band_text(int row, int col, int maxc, const char *s, int stride, int rh, int cw, int y0)
{
    uint32_t map = (uint32_t) vicky_read(0x1C) | ((uint32_t) vicky_read(0x1D) << 8) | ((uint32_t) vicky_read(0x1E) << 16) | ((uint32_t) vicky_read(0x1F) << 24);
    uint32_t font = (uint32_t) vicky_read(0x18) | ((uint32_t) vicky_read(0x19) << 8) | ((uint32_t) vicky_read(0x1A) << 16) | ((uint32_t) vicky_read(0x1B) << 24);
    uint32_t cell = vicky_text_cell(0, row);   /* the band's own cells (option B), where VICKY draws it from */
    (void) map; (void) stride;
    uint8_t fg = mem_peek((cell + 2) & 0x0FFFFFFFu), bg = mem_peek((cell + 3) & 0x0FFFFFFFu);
    int gh = vicky_cell_h(0), gw = vicky_cell_w(0), sc = vicky_out_scale();   /* the glyphs: 8 or 16 wide; the frame drawn 1x or 2x */
    for (int i = 0; s[i] && i < maxc; i++) {
        uint8_t g = (uint8_t) s[i];
        for (int gy = 0; gy < rh; gy++) {
            int y = y0 + row * rh + gy;
            if (y < 0 || y >= vicky_glass_h()) break;
            if ((col + i + 1) * cw > vicky_glass_w()) break;
            uint32_t a = font + ((uint32_t) g * (uint32_t) gh + (uint32_t)(gy * gh / rh)) * (uint32_t)(gw / 8);
            unsigned bits = gw == 16 ? (unsigned) mem_peek(a & 0x0FFFFFFFu) << 8 | mem_peek((a + 1) & 0x0FFFFFFFu) : mem_peek(a & 0x0FFFFFFFu);
            for (int k = 0; k < sc; k++) {
                uint8_t *p = fb + (size_t)(y * sc + k) * VICKY_WIDTH + (size_t)(col + i) * (size_t) cw * (size_t) sc;
                for (int gx = 0; gx < cw * sc; gx++) p[gx] = (bits & (1u << (gw - 1 - gx * gw / (cw * sc)))) ? fg : bg;
            }
        }
    }
}
static void bands_overlay(void)
{
    echo_banded = 0;
    if (menu_is_open()) return;
    if ((vicky_read(VR_BANDCTL) & (VB_USER | VB_PROGRAM)) != VB_USER) return;   /* the user's bands, and unclaimed */
    { uint8_t oy, crows, bot; vicky_layout(&oy, &crows, &bot); if (!oy && !bot) return; }   /* VICKY lays none out */
    uint8_t ctrl = vicky_read(0), l0 = vicky_read(0x10);
    if (!(ctrl & 1) || !(l0 & 1) || ((l0 >> 1) & 3) != 3) return;       /* the console (text32) is not the picture */
    int stride = vicky_read(0x16) | (vicky_read(0x17) << 8);
    int rows = (ctrl & 8) ? 25 : ((ctrl & 6) || (l0 & 0x60)) ? 30 : 60, cols = stride > 0 && stride <= 180 ? stride : 80;
    int rh = rows == 60 ? 8 : 16, cw = vicky_glass_w() / cols, y0 = (ctrl & 8) ? 40 : 0;
    if (ctrl & 0x20) { rh = vicky_cell_h(0); rows = vicky_glass_h() / rh; y0 = -(int16_t)(vicky_read(0x14) | (vicky_read(0x15) << 8)); }   /* the HD family: rows of the mode's own cells, below the ROM's top padding */
    /* What is running, left of the clock, is JIM's now (core/term.c, the
     * bands are its own since 2026-10-05): the first screen's tab carries it. */
    if (settings_get(SET_VIDEO_STATUSBAR)) {                         /* the key pipe's echo, left of the battery */
        echo_banded = 1;
        if (echo_len && (Sint32)(echo_until - SDL_GetTicks()) > 0) {
            char buf[64]; snprintf(buf, sizeof buf, " %s%.*s", echo_tag, echo_len, echo_txt);
            band_text(rows - 1, term_band_left() - 1, cols - 26 - term_band_left(), buf, stride, rh, cw, y0);   /* after REMOTE, clear of the network and the battery */
            VICKY_TOUCH();                       /* drawn into fb: the next frame draws the band again, so it goes when the echo does */
        }
    }
}
/* The volume setting as a gain, 0..32768.  CUBIC, because ears are
 * logarithmic: the old straight line made 100 -> 50 a drop of 6 dB and each
 * ten-percent step near the top under 1 dB, which nobody can hear -- half of
 * why the volume keys "did nothing".  Cubed, a step is 2-3 dB near the top,
 * 50% is -18 dB and the bottom of the range is properly quiet.  100% is still
 * unity, so nothing gets louder than it was. */
/* On the K4510 Linux the volume is the computer's (Doc, 2026-10-08: "k4510
 * volume controls do not control music volume" -- k4510-radio plays on the
 * Linux beside the machine): F12's volume and the volume keys set ALSA's
 * Master, which every sound on the computer goes through, and the machine's
 * own sound is made at full.  On a desktop the desktop owns the volume, and the
 * setting scales the machine's sound alone, as before. */
static int vol_owned = -1;                    /* 1: the K4510 Linux, where the setting is ALSA's Master */
static int vol_machine(void)
{
    if (vol_owned < 0) vol_owned = access("/etc/k4510-linux", F_OK) == 0;
    return vol_owned ? 100 : settings_get(SET_AUDIO_VOLUME);
}
static void vol_master(int v)                 /* the setting to ALSA's Master, in the background */
{
    static pid_t last; char pc[8]; pid_t pid;
    if (last > 0) waitpid(last, NULL, WNOHANG);   /* the one before: no zombie left over */
    snprintf(pc, sizeof pc, "%d%%", v);
    if ((pid = fork()) == 0) {
        int fd = open("/dev/null", O_RDWR); if (fd >= 0) { dup2(fd, 1); dup2(fd, 2); }
        execlp("amixer", "amixer", "-q", "-M", "sset", "Master", pc, v ? "unmute" : "mute", (char *) NULL);
        _exit(127);
    }
    last = pid;
}
static int vol_gain(int vol)
{
    static int last = -1, g;
    if (vol != last) { last = vol; g = (int)((int64_t) vol * vol * vol * 32768 / 1000000); }
    return g;
}
static void line_end(int vol)                 /* the scanline's picture and sound, then on to the next */
{
    Uint64 t1 = PCLK();
    vicky_line(m_line);
    Uint64 t2 = PCLK();
    /* The audio clock the OPL2 writes are stamped with: one scanline of it,
     * whoever is rendering.  See core/sndq.h. */
    sndq_tick(1000000u / (60u * (unsigned) frame_lines));
    if (sndq_owner() == SNDQ_OWNER_CPU)
    { int16_t tmp[256]; int n = audio_render(CYCLES_PER_LINE, tmp, 256);
      for (int i = 0; i < n; i++) ring_put((int16_t)(tmp[i] * vol_gain(vol) >> 15)); }
    Uint64 t3 = PCLK();
    p_vic += t2 - t1; p_snd += t3 - t2;
    m_cyc = 0;
    if (++m_line == frame_lines) {
        m_line = 0; m_in_frame = 0;
        vicky_end_frame();
        bands_overlay();                          /* what is running, and the keys typed from outside, in the bands */
        cpu65.irqLevel = vicky_irq() ? 1 : 0;
    }
}
static void machine_insn(int vol)             /* Space: one instruction */
{
    line_begin();
    Uint64 t0 = PCLK(); m_cyc += cpu_run(1); p_cpu += PCLK() - t0;
    if (m_cyc >= (int)CYCLES_PER_LINE) line_end(vol);
}
static void machine_line(int vol)             /* L: the rest of this scanline */
{
    line_begin();
    Uint64 t0 = PCLK(); cpu_run((int)CYCLES_PER_LINE - m_cyc); p_cpu += PCLK() - t0;
    line_end(vol);
}
static void machine_frame(int vol)            /* F, and every frame while running: through to the next vblank */
{
    do machine_line(vol); while (m_line != 0);
}

/* An 8-bit sprite pointer for the host cursor: the same arrow MOUSETEST draws
 * as a VICKY sprite (demo/mousetest.c), white with a one-pixel black outline,
 * scaled up so the pixels read as chunky (Doc, 2026-09-11).  Set as the window
 * cursor; the Mouse pointer setting shows or hides it.  Its white and black
 * are the palette's entries 1 and 0, so it is an amber arrow under AMBER
 * (Doc, 2026-10-07): built again whenever the palette changes. */
#define ARROW_W 8
#define ARROW_H 12
#define ARROW_SC 3                                  /* each source pixel -> ARROW_SC x ARROW_SC */
static const unsigned char retro_arrow[ARROW_H] = { 0x80,0xC0,0xE0,0xF0,0xF8,0xFC,0xFE,0xF0,0xD8,0x98,0x0C,0x0C };
static int arrow_on(int x, int y) { return (x >= 0 && x < ARROW_W && y >= 0 && y < ARROW_H && ((retro_arrow[y] << x) & 0x80)) ? 1 : 0; }
static void set_retro_cursor(void)
{
    SDL_Surface *sf = SDL_CreateRGBSurfaceWithFormat(0, ARROW_W * ARROW_SC, ARROW_H * ARROW_SC, 32, SDL_PIXELFORMAT_ARGB8888);
    static SDL_Cursor *cur;
    SDL_Cursor *old = cur;
    Uint32 fill = 0xFF000000u | vicky_palette_rgb(1), line = 0xFF000000u | vicky_palette_rgb(0);
    if (!sf) return;
    for (int sy = 0; sy < ARROW_H; sy++) for (int sx = 0; sx < ARROW_W; sx++) {
        Uint32 c = 0;                              /* transparent */
        if (arrow_on(sx, sy)) c = fill;            /* white fill: entry 1 */
        else { int edge = 0; for (int dy = -1; dy <= 1; dy++) for (int dx = -1; dx <= 1; dx++) if (arrow_on(sx + dx, sy + dy)) edge = 1;
               if (edge) c = line; }               /* black outline: entry 0 */
        for (int py = 0; py < ARROW_SC; py++) for (int px = 0; px < ARROW_SC; px++)
            ((Uint32 *)sf->pixels)[(sy * ARROW_SC + py) * (sf->pitch / 4) + sx * ARROW_SC + px] = c;
    }
    cur = SDL_CreateColorCursor(sf, 0, 0);         /* hotspot at the tip */
    SDL_FreeSurface(sf);
    if (cur) SDL_SetCursor(cur);
    if (old && old != cur) SDL_FreeCursor(old);    /* the last palette's */
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

#include "hostpage.h"                                       /* F12 -> Host, the battery and network polls */
#include <errno.h>                                             /* the key pipe's mkfifo, below */
/* While the machine waits on the network (core/net_posix.c), keep the window
 * answering: the compositor greys out a window that stops pumping events.
 * The machine itself waits, as it would on a disk; only the main thread may
 * touch SDL, and a CP/M or BBC BASIC fetch on another thread just waits. */
#include "net_plat.h"
static SDL_threadID main_tid;
static void net_wait_alive(void)
{
    static Uint32 last; Uint32 now;
    if (SDL_ThreadID() != main_tid) return;
    now = SDL_GetTicks();
    if (now - last < 50) return;
    last = now;
    SDL_PumpEvents();
}

int k4510_frontend_main(int argc, char **argv)
{
    /* --no-startup.bat: skip /STARTUP.BAT for this run only.  The F12 switch
     * does the same thing but persists, and holding a key at the banner needs
     * you to be there -- neither suits a script, or the case where a startup
     * file wedges the machine and you want one clean boot to go and fix it. */
    /* First, before anything slow: the default action of both is to KILL, so a
     * k4510-shot fired while the ROM was loading ended the emulator (review
     * 2026-09-17). */
    signal(SIGUSR1, shot_signal);                  /* tools/k4510-shot: a screenshot, from outside */
    signal(SIGUSR2, screen_signal);                /* tools/k4510-screen: the text screen, as text */
    signal(SIGHUP, hup_signal);
    /* Every exit that is not a return from here -- Xlib calls exit(1) when the
     * X server goes -- still stops the co-processor.  Idempotent, so the clean
     * path calling it too is fine. */
    atexit(io_tube_shutdown);
    io_radio_hook = radio_cmd;                     /* RADIO at the prompt: k4510-radio's remote (the Navidrome sidebar's while it plays) */
    int no_startup = 0;
    { int i, j;
      for (i = 1; i < argc; i++)
          if (!strcmp(argv[i], "--no-startup.bat") || !strcmp(argv[i], "--no-startup")) {
              no_startup = 1;
              for (j = i; j < argc - 1; j++) argv[j] = argv[j + 1];   /* out of the way of the positional arguments */
              argc--; i--;
          } }
    if (getenv("K4510_NO_STARTUP")) no_startup = 1;          /* the same thing, for a script that sets it once */
    const char *rom = (argc > 1) ? argv[1] : "rom/kernal.bin";
    { const char *lp = getenv("K4510_LATLOG"); if (lp && *lp && (lat_f = fopen(lp, "w"))) io_lat_on = 1; }
    const char *cfg = "k4510.cfg";
    if (argc > 2) fs_set_root(argv[2]);
    for (int i = 1; i < HDFONT_COUNT; i++) {                     /* optional: the HD text fonts, 16x32 and 16x16, both pages */
        char p[4][80]; static const char *const pg[2] = { "cp437", "k4510" };   /* PAGE_CP437, PAGE_K4510 */
        hd_have[i] = 1;
        for (int k = 0; k < 2; k++) {
            snprintf(p[k], sizeof p[k], "data/fonts/hd/%s-%s.bin", hdfont_files[i], pg[k]);
            snprintf(p[2 + k], sizeof p[2 + k], "data/fonts/hd/%s16-%s.bin", hdfont_files[i], pg[k]);
            if (load_file(p[k], hd_fonts[i][k], 16384) != 16384 || load_file(p[2 + k], hd_fonts16[i][k], 8192) != 8192) hd_have[i] = 0;
        }
        hd_have3[i] = 1;                                         /* and at 3x, 24x48 and 24x24 */
        for (int k = 0; k < 2; k++) {
            char a[80], b[80];
            snprintf(a, sizeof a, "data/fonts/hd/%s48-%s.bin", hdfont_files[i], pg[k]);
            snprintf(b, sizeof b, "data/fonts/hd/%s24-%s.bin", hdfont_files[i], pg[k]);
            if (load_file(a, hd_fonts48[i][k], 36864) != 36864 || load_file(b, hd_fonts24[i][k], 18432) != 18432) hd_have3[i] = 0;
        }
    }
    load_file("data/fonts/unscii/font8-cp437.bin", font_437_8, sizeof font_437_8);      /* optional: IBM's page */
    load_file("data/fonts/unscii/font16-cp437.bin", font_437_16, sizeof font_437_16);
    if (load_file("data/fonts/unscii/font8-unscii.bin", font_menu, sizeof font_menu) != sizeof font_menu ||
        load_file("data/fonts/unscii/font16-unscii.bin", font_panel, sizeof font_panel) != sizeof font_panel) {
        fprintf(stderr, "need data/fonts/unscii/font8-unscii.bin and font16-unscii.bin (run from repo root)\n");
        return 1;
    }
    ui_font(font_menu);                                  /* the menu's own font: it must draw whatever the guest did */
    sidebars_scan(argc > 2 ? argv[2] : "fs");             /* the Sidebars choices, before a saved one is looked up */
    settings_load(cfg);
    snprintf(io_palname, sizeof io_palname, "%s", settings_palette());   /* the palette the ROM loads at every reset ($D547) */
    for (int i = 0; i < sidebars_count(); i++) {           /* what the sidebars kept across the power cycle (STATE.DAT) */
        int b = sidebars_info(i)->builtin; size_t n; uint8_t *st;
        if (b >= SIDEBAR_HALLOWEEN && (st = sidebars_state_read(i, &n))) { saver_restore(b - SIDEBAR_HALLOWEEN, st, n); free(st); }
    }
    /* The F12 menu file, beside k4510.cfg and outside the machine's own disk, so
     * nobody at the machine can edit it: which rows show, and the locks.
     * Written in full when missing, so it lists what can be changed (Doc,
     * 2026-09-13: "include all options in the F12 menu file as text"). */
    { extern int io_lock_linux;                             /* core/io.c: `!` and SSH refused */
      if (menu_file_load("k4510-menu.cfg") < 0) menu_file_write("k4510-menu.cfg");
      io_lock_linux = menu_lock(MENU_LOCK_LINUX); }
    if (mem_init() != 0) { fprintf(stderr, "cannot reserve %u MB\n", K4510_PHYS_SIZE >> 20); return 1; }
    load_fonts();                                        /* the ROM points VICKY at $010000 or $010800 */
    for (int i = 0; i < MENU_SLOTS; i++) slot_refresh(i);
    menu_info(INFO_VERSION, "K4510 K/OS"); menu_info(INFO_BUILD, K4510_BUILD); menu_info(INFO_ROM, rom); menu_info(INFO_FS, argc > 2 ? argv[2] : "fs");
    prune_brainshots(argc > 2 ? argv[2] : "fs");            /* processed brainshots older than 48 hours */
    menu_info(INFO_HOST, access("/etc/k4510-linux", F_OK) == 0 ? "the K4510 Linux" : "desktop, SDL2");


    if (mem_load_rom(rom) <= 0) {
        fprintf(stderr, "cannot load ROM %s\n", rom);
        return 1;
    }
    cpu65_reset();

    io_set_ms_source(sdl_ms_now);              /* SYS+$36: the wall clock the guest can pace against */
    cpu_hz_now = settings_cpu_hz(); cycles_per_line = cpu_hz_now / 60 / (unsigned) frame_lines; io_set_cpu_khz(cpu_hz_now / 1000);
    audio_init((double)cpu_hz_now, AUDIO_RATE);
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER) != 0) { fprintf(stderr, "SDL: %s\n", SDL_GetError()); return 1; }
    main_tid = SDL_ThreadID(); plat_net_wait_hook = net_wait_alive;
    /* The panel, before the machine boots: the display's own mode, so the ROM
     * lays its console out on the right integer display resolution from the
     * first frame.  The renderer's output once the window is full screen is
     * the truth, and is checked every frame below (panel_track).  K4510_PANEL
     * =WxH stands in for it: the tests, and pictures of panels nobody here
     * owns.  docs/design-video-foundations.md. */
    vicky_set_cap(settings_get(SET_VIDEO_CAP));
    { SDL_DisplayMode dm; int pw = 1920, ph = 1080; const char *pe = getenv("K4510_PANEL");
      if (SDL_GetDesktopDisplayMode(0, &dm) == 0 && dm.w >= 320 && dm.h >= 200) { pw = dm.w; ph = dm.h; }
      if (pe) sscanf(pe, "%dx%d", &pw, &ph);
      vicky_set_panel(pw, ph, settings_get(SET_VIDEO_BASE)); settings_video_rebuild(); }
    /* the touchpad as a pointer on the bare console (see touchpad_event); K4510_TOUCHPAD=0|1 overrides */
    { const char *d = SDL_GetCurrentVideoDriver(), *o = getenv("K4510_TOUCHPAD");
      touchpad_rel = o ? atoi(o) : (d && SDL_strcasecmp(d, "KMSDRM") == 0);
      if (touchpad_rel) SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0"); }   /* ours, not SDL's absolute synthesis */
    /* A USB gamepad or joystick, if one is plugged in (now or later): SDL's
     * controller layer knows the common ones (Xbox, PlayStation, 8BitDo,
     * Logitech) by their ids and gives every one the same buttons, so the
     * machine sees one thing -- the $D104 held-keys register, OR'd with the
     * keyboard.  Hot-plug: the first pad to appear is the one; unplug it and
     * the next one to appear takes over.  Nothing to configure. */
    { int n = SDL_NumJoysticks(); for (int i = 0; i < n && !pad; i++) if (SDL_IsGameController(i)) pad_open(i); }
    /* The host's pointer is never drawn on the glass: the machine has its own
     * ($D108-$D10F and the Mouse pointer setting, 2026-09-11), and the host's
     * was only ever wrong there.  It showed up as a white arrow parked in the top
     * left corner on the K4510 Linux, where KMSDRM draws one because there is no
     * desktop to own it.  Hidden everywhere: in a window the pointer is still
     * there for the frame and the title bar, it just stops being drawn over
     * the picture.  Since 2026-09-11 this is a setting (Mouse pointer, default
     * ON so the pointer does not confusingly vanish on the glass); the frame
     * loop applies it, hidden while the pointer is captured for a game. */
    /* The K4510 Linux is a whole computer that exists to be this machine, so its menu
     * gets a row the others must not have.  The marker file is written by
     * linux/build-live.sh; on any other host this call never happens and the
     * row stays off the end of the Machine menu. */
    if (access("/etc/k4510-linux", F_OK) == 0) { menu_set_shutdown(1); menu_set_host(1); io_host_kind = 1; }
    host_info_refresh();
    /* K4510_WINDOW=WxH: the window's first size (1280x960 otherwise).  For a
     * wide window with the side panel, and for screenshots of one. */
    int win_w = 640 * SCALE, win_h = 480 * SCALE;
    { const char *wv = getenv("K4510_WINDOW"); int a, c2; if (wv && sscanf(wv, "%dx%d", &a, &c2) == 2 && a >= 320 && c2 >= 240) { win_w = a; win_h = c2; } }
    SDL_Window *win = SDL_CreateWindow("K4510", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                       win_w, win_h, SDL_WINDOW_RESIZABLE);
    grab_win = win;
    set_retro_cursor();          /* the 8-bit arrow pointer, shown per the Mouse pointer setting */
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
 * wanting it is measurable and local.  F12 -> Video -> Vertical sync, off by
 * default, which is exactly the behaviour above.  On the Pi it stays off: the
 * shim's present is the blocking one this was escaped from. */
SDL_Renderer *ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED);
    if (!ren) ren = SDL_CreateRenderer(win, -1, 0);      /* no GPU (the dummy driver, a screenshot run) */
    SDL_RenderSetLogicalSize(ren, 640, 480);
    SDL_Texture *tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING,
                                         VICKY_WIDTH, VICKY_HEIGHT);
    int smooth_applied = -1, logical_set = 0, vsync_applied = -1;
    int logical_custom = 0; SDL_Texture *ptex = NULL; int ptex_w = 0, ptex_h = 0; double panel_fps = 0;   /* Placement and the side panel */
    /* The border: one column of pixels stretched across, a single RenderCopy
     * that reaches the letterbox too; rebuilt only when the colour changes. */
    uint32_t border_lit = 0;
    SDL_Texture *btex = NULL; int btex_col = -1, btex_smooth = -1, btex_sbar = -1, btex_gh = -1; uint32_t btex_rgb = 0;

    static uint8_t ov[UI_W * UI_H];
    /* pal, mpal (the machine's colours, and half-lit behind the menu) and the
     * frame's fpal, fmpal are file-wide: the screenshots read them too */
    static uint32_t upal[UIC_COUNT];              /* the menu's own colours */
    int tex_stale = 1;                            /* the tables changed: the texture must be rebuilt */
    int fullscreen_applied = 0;
    int mode_pending = 0;                          /* (mode + 1) the ROM has been asked for, 0 = nothing */
    int mode_shown = -1, status_shown = -1, mode_req = 0, mode_wait = 0;
#define MODE_REQ_FRAMES 120                        /* two seconds for the guest to notice, then give up */

    SDL_AudioSpec want = { 0 }, have;
    want.freq = AUDIO_RATE; want.format = AUDIO_S16SYS; want.channels = 1; want.samples = 1024; want.callback = audio_cb;
    SDL_AudioDeviceID adev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if (adev) SDL_PauseAudioDevice(adev, 0); else fprintf(stderr, "no audio: %s\n", SDL_GetError());
    int audio_ever = adev != 0;                    /* a host with no sound is not asked again and again */
    sound_ms = SDL_GetTicks();
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
    int dbg_req = 0, dump_n = 0;                    /* while paused: 1 = step an instruction, 2 = a scanline, 3 = a frame; the last dump written */
    int clock_at_open = -1;                        /* the clock when the menu opened: changed on close = the user's choice */
    const int ring_log = getenv("K4510_RINGLOG") != NULL;
    /* the governor's window: how long the machine's own half of the frame has
     * been costing, and how many callbacks ran dry, since it last decided */
    Uint64 gov_t0 = 0, gov_mach = 0; unsigned gov_frames = 0, gaps_seen = 0; gov_state gov = { 0, 0, 0, 0 }; int gov_own = 0;
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
#define PCLK_HZ() SDL_GetPerformanceFrequency()
#define PERF_FRAMES 300
    host_keymap_apply();                          /* the first look: note the layout the boot already applied */
    host_lid_apply();                             /* the lid: keep running holds logind's lock from the start */
    host_charge_apply();                          /* Charge to 100% once: the menu follows the helper's note */
    if (access(RESUME_FILE, F_OK) == 0) {         /* Save and power off, last time: back where it was */
        int r = state_load(RESUME_FILE);
        if (r == 0) { load_fonts(); remove(RESUME_FILE); mlog("start: resumed from " RESUME_FILE); }
        else { rename(RESUME_FILE, RESUME_FILE ".old"); mlog("start: " RESUME_FILE " would not load; a fresh boot");
               if (r == -2) { host_zero(k4510_ram, K4510_PHYS_SIZE); mem_reset(); load_fonts(); mem_load_rom(rom); cpu65_reset(); } }
    }
    while (running) {
        /* The audio device closes after AUDIO_IDLE_MS of nothing but zeros --
         * no FM (the OPL2 sleeps), no radio -- and the sound hardware powers
         * down.  The first sample
         * that is not zero opens it again; the ring keeps the last frame's
         * samples while it is closed, so the sound starts with its start. */
        if (audio_ever && sndq_owner() == SNDQ_OWNER_CPU) {
#define AUDIO_IDLE_MS 5000
            int need = navi_playing() || io_tube_kind() == 6 || io_tube_kind() == 7 || SDL_GetTicks() - sound_ms < AUDIO_IDLE_MS;
            if (!need && adev) { SDL_CloseAudioDevice(adev); adev = 0; mlog("audio: closed, nothing to hear"); }
            else if (need && !adev) {
                static Uint32 tried;                       /* a device that will not open: once a second, not every frame */
                if (SDL_GetTicks() - tried >= 1000) { tried = SDL_GetTicks();
                    if ((adev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0)) != 0) { SDL_PauseAudioDevice(adev, 0); mlog("audio: open again"); } }
            }
            if (!adev) { unsigned keep = (unsigned) AUDIO_RATE / 60; if (RING_DEPTH > keep) ring_h = ring_w - keep; }   /* closed: only the last frame's, for the start of a sound */
        }
        { Uint64 c = SDL_GetPerformanceCounter();
          /* the window opens 20 s after start, so it measures the machine at
           * the prompt rather than BENCH, whose clock reads are dear on the Pi */
          if (p_last && SDL_GetTicks() > 20000) { p_tot += c - p_last; p_n++; }
          if (p_n == 1) { io_prof_reset(); io_prof_on = 1; p_mach = p_tex = p_pres = p_cpu = p_vic = p_snd = 0; }   /* the window opens: every sum starts here */
          p_last = c;
          if (p_n == PERF_FRAMES) {
              char pp[600]; snprintf(pp, sizeof pp, "%s/SYSTEM/LOG/PERF.TXT", fs_get_root());
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
                  { fprintf(pf, "\nI/O page, per frame: %.0f reads\n", (double)io_prof_reads / f);
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
        if (hup_req) { hup_req = 0; mlog("quit: SIGHUP"); running = 0; }
        uint8_t pend = 0;               /* a printable key waiting to see whether SDL sends its text */
        do {                            /* the wait for this frame's deadline is here, events handled as they come (pace_wait) */
        while (SDL_PollEvent(&e)) {
            if ((e.type == SDL_KEYDOWN || e.type == SDL_KEYUP || e.type == SDL_TEXTINPUT || e.type == SDL_TEXTEDITING
                 || e.type == SDL_MOUSEMOTION || e.type == SDL_MOUSEBUTTONDOWN || e.type == SDL_MOUSEBUTTONUP || e.type == SDL_MOUSEWHEEL
                 || e.type == SDL_FINGERDOWN || e.type == SDL_FINGERMOTION || e.type == SDL_FINGERUP)
                && vt_away()) continue;                                  /* another console's keys are not the machine's */
            if (e.type == SDL_KEYDOWN || e.type == SDL_KEYUP || e.type == SDL_TEXTINPUT || e.type == SDL_MOUSEMOTION || e.type == SDL_MOUSEBUTTONDOWN
                || e.type == SDL_MOUSEBUTTONUP || e.type == SDL_MOUSEWHEEL || e.type == SDL_JOYAXISMOTION || e.type == SDL_JOYBUTTONDOWN) input_ms = SDL_GetTicks();
            if (e.type == SDL_KEYDOWN) term_blink_restart();          /* the cursor steady while typing (core/term.c) */
            switch (e.type) {
            case SDL_QUIT: mlog("quit: SDL_QUIT (the window closed, or a SIGTERM)"); running = 0; break;
            case SDL_WINDOWEVENT:
                if (e.window.event == SDL_WINDOWEVENT_FOCUS_LOST) grab(0);   /* alt-tab always frees the pointer */
                present_force = 1;                                       /* exposed, resized, moved: draw the window again (still frames) */
                break;
            case SDL_RENDER_TARGETS_RESET: case SDL_RENDER_DEVICE_RESET: present_force = 1; break;
            case SDL_MOUSEMOTION:
                if (confine_on && !SDL_GetRelativeMouseMode()) {        /* off the picture: back to its edge; the warp's own event follows */
                    int wx, wy; SDL_GetMouseState(&wx, &wy);
                    if (confine_clamp(&wx, &wy)) { SDL_WarpMouseInWindow(win, wx, wy); break; }
                }
                mouse_x = to_machine((int)((e.motion.x - geo_xd) / geo_s), vicky_out_w()) / vicky_out_scale();   /* the picture as drawn, back to the machine's pixels */
                mouse_y = to_machine((int)((e.motion.y - geo_yd) / geo_s), vicky_out_h()) / vicky_out_scale();
                dx_acc += (int)(e.motion.xrel / geo_s); dy_acc += (int)(e.motion.yrel / geo_s);
                mouse_to_menu(); break;
            case SDL_MOUSEBUTTONDOWN: case SDL_MOUSEBUTTONUP: {
                int bit = e.button.button == SDL_BUTTON_LEFT ? 1 : e.button.button == SDL_BUTTON_RIGHT ? 2 : e.button.button == SDL_BUTTON_MIDDLE ? 4 : 0;
                if (e.type == SDL_MOUSEBUTTONDOWN && !menu_is_open() && settings_get(SET_INPUT_MOUSE_GRAB)) { grab_wanted = 1; grab(1); }
                if (e.type == SDL_MOUSEBUTTONDOWN) mouse_btn |= bit; else mouse_btn &= ~bit;
                mouse_to_menu(); break; }
            case SDL_FINGERDOWN: case SDL_FINGERMOTION: case SDL_FINGERUP:
                if (touchpad_rel) touchpad_event(&e, win);
                break;
            case SDL_MOUSEWHEEL:
                wheel_acc += e.wheel.y;
                if (menu_is_open()) { menu_mouse(mouse_x < 0 ? 0 : ui_mx(mouse_x), mouse_y < 0 ? 0 : ui_my(mouse_y), mouse_btn, e.wheel.y); wheel_acc = 0; }
                break;
            case SDL_CONTROLLERDEVICEADDED: pad_open(e.cdevice.which); break;
            case SDL_CONTROLLERDEVICEREMOVED:
                if (pad && e.cdevice.which == SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(pad))) { SDL_GameControllerClose(pad); pad = NULL; fprintf(stderr, "gamepad: unplugged\n"); }
                break;
            case SDL_CONTROLLERBUTTONDOWN:                   /* the buttons that are keys, not held state: */
                if (e.cbutton.button == SDL_CONTROLLER_BUTTON_START) kbd_push(0x0D);   /* Start = Enter (INVADER2's "press a key", a game's pause) */
                if (e.cbutton.button == SDL_CONTROLLER_BUTTON_BACK)  kbd_push(0x1B);   /* Back/Select = Esc, how every game leaves */
                break;
            case SDL_TEXTINPUT: {
                if (settings_get(SET_INPUT_KBD_LAYOUT) > 0) break;   /* the machine's own layout typed it at SDL_KEYDOWN */
                if (alt_ate) { alt_ate = 0; break; }            /* Alt+letter went as a key code at SDL_KEYDOWN: not typed too.
                                                                  * The flag lives one event: the next key down clears it, so a
                                                                  * host that sends no text for Alt+letter loses nothing */
                pend = 0;                                    /* SDL does send text here: the key code is not needed */
                /* Caps Lock as Ctrl: while it is held a letter is a Ctrl code, and
                 * SDL_KEYDOWN sends that -- the host would type the letter too.  And
                 * the host still toggles its caps state, so undo the case it flips. */
                int caps_flip = 0;
                if (settings_get(SET_INPUT_CAPS_CTRL)) {
                    if (caps_ctrl_down) break;
                    caps_flip = (SDL_GetModState() & KMOD_CAPS) != 0;
                }
                /* The host layout has already composed the character -- a dead key
                 * plus a vowel arrives here as one UTF-8 sequence.  ASCII goes
                 * straight through; anything above it is decoded and looked up in
                 * the machine's upper half, which is code page 437 (unscii).
                 * Dropping the non-ASCII bytes, as this used to, meant no accented
                 * character could ever be typed. */
                for (const char *c = e.text.text; *c; ) {
                    unsigned long cp; unsigned char ch = (unsigned char)*c;
                    if (ch < 0x80) { cp = ch; c++; }
                    else if ((ch & 0xE0) == 0xC0 && (c[1] & 0xC0) == 0x80) { cp = ((unsigned long)(ch & 0x1F) << 6) | (c[1] & 0x3F); c += 2; }
                    else if ((ch & 0xF0) == 0xE0 && (c[1] & 0xC0) == 0x80 && (c[2] & 0xC0) == 0x80)
                         { cp = ((unsigned long)(ch & 0x0F) << 12) | ((unsigned long)(c[1] & 0x3F) << 6) | (c[2] & 0x3F); c += 3; }
                    else { c++; continue; }                      /* 4-byte or malformed: nothing to type */
                    if (caps_flip && (((cp | 0x20) >= 'a' && (cp | 0x20) <= 'z')
                                      || (cp >= 0xC0 && cp <= 0xFE && cp != 0xD7 && cp != 0xDF && cp != 0xF7))) cp ^= 0x20;
                    if (cp >= 0x20 && cp < 0x7F) { kbd_push((uint8_t)cp); continue; }
                    { uint8_t b = cp437_of(cp); if (b) kbd_push(b); }
                }
                break;
            }
            case SDL_KEYDOWN: case SDL_KEYUP: {
                /* Caps Lock as Ctrl (F12 -> Input), in the emulator because on the
                 * K4510 Linux SDL reads evdev scancodes, which an XKB or console
                 * keymap option never reaches.  The key is a held Ctrl for every
                 * chord below and never reaches the machine; the lock state it
                 * still toggles is taken out of the modifiers here and out of the
                 * text in SDL_TEXTINPUT.  Doc, 2026-09-12. */
                if (settings_get(SET_INPUT_CAPS_CTRL) && e.key.keysym.scancode == SDL_SCANCODE_CAPSLOCK) {
                    SDL_Keymod cm = SDL_GetModState();
                    caps_ctrl_down = (e.type == SDL_KEYDOWN);
                    kbd_modifiers(cm & KMOD_SHIFT, (cm & KMOD_CTRL) || caps_ctrl_down, cm & KMOD_ALT);
                    break;
                }
                SDL_Keymod m = SDL_GetModState();
                if (settings_get(SET_INPUT_CAPS_CTRL)) m = (SDL_Keymod)((m & ~KMOD_CAPS) | (caps_ctrl_down ? KMOD_LCTRL : 0));
                else caps_ctrl_down = 0;                         /* the setting went off while the key was held */
                kbd_modifiers(m & KMOD_SHIFT, m & KMOD_CTRL, m & KMOD_ALT);
                if (e.type != SDL_KEYDOWN) break;
                SDL_Keycode k = e.key.keysym.sym;
                { /* the reset chord: a modifier + PageUp ("Commodore + Restore"), or Ctrl+Alt+Del */
                  int ch = settings_get(SET_INPUT_RESET_CHORD), hit = 0;
                  if (k == SDLK_PAGEUP) hit = (ch == CHORD_SUPER_PGUP && (m & KMOD_GUI)) || (ch == CHORD_CTRL_PGUP && (m & KMOD_CTRL)) || (ch == CHORD_ALT_PGUP && (m & KMOD_ALT));
                  if (k == SDLK_DELETE && ch == CHORD_CTRL_ALT_DEL && (m & KMOD_CTRL) && (m & KMOD_ALT)) hit = 1;
                  if (hit) { menu_close(); cpu65_reset(); break; } }
                { static const SDL_Keycode mk[MENUKEY_COUNT] = { SDLK_F7, SDLK_F8, SDLK_F11, SDLK_PAUSE, SDLK_F12 };   /* Shift + the menu key pauses: */
                  if (k == mk[settings_get(SET_INPUT_MENU_KEY)] && (m & KMOD_SHIFT)) {                       /* F8 did until 2026-09-15 */
                      paused = !paused; SDL_SetWindowTitle(win, paused ? "K4510  [PAUSED]" : "K4510"); break; } }
                /* PrtSc: shots/shot-*.png, paused or not.  Both keys: on the K4510
                 * Linux SDL reads evdev, where PrtSc is KEY_SYSRQ and SDL's table
                 * makes that SDLK_SYSREQ; SDLK_PRINTSCREEN is X11/Wayland's (and
                 * evdev's KEY_PRINT).  Found on the Dell with a key logger, 2026-09-12. */
                if (k == SDLK_PRINTSCREEN || k == SDLK_SYSREQ) { shot_req = 1; break; }
#if defined(__linux__)
                /* Ctrl+Alt+F1..F6 on the K4510 Linux: the other consoles.  SDL
                 * puts tty1's keyboard in K_OFF so no key leaks into the text
                 * console under us -- and that switches off the kernel's own
                 * console keys with it.  So the switch is asked for here; from
                 * tty2..6 the kernel's Ctrl+Alt+F1 brings you back (their
                 * keyboards are normal).  Doc, the Dell, 2026-09-12. */
                { static int appliance = -1;
                  if (appliance < 0) appliance = access("/etc/k4510-linux", F_OK) == 0;
                  if (appliance && (m & KMOD_CTRL) && (m & KMOD_ALT) && k >= SDLK_F1 && k <= SDLK_F6) {
                      int vt = 1 + (int)(k - SDLK_F1), fd;
                      if (vt > 1 && !menu_lock(MENU_LOCK_CONSOLES)             /* k4510-menu.cfg: consoles = locked */
                          && (fd = open("/dev/tty", O_RDWR)) >= 0) {           /* our own tty: no privilege needed */
                          if (ioctl(fd, VT_ACTIVATE, vt) < 0) perror("k4510: VT_ACTIVATE");
                          close(fd);
                      }
                      break;
                  } }
#endif
                /* Paused, the keyboard is the debugger's (the legend is on the
                 * side panel; it works without the panel too).  Space steps one
                 * instruction, L one scanline, F one frame, D writes a dump
                 * (dumps/dump-NNN.txt, the DUMP register's file, and arms the
                 * PC recorder so the next one has history), T starts or stops
                 * an instruction trace.  Anything else is swallowed: the
                 * machine is stopped and would only queue it. */
                if (paused && !menu_is_open() && !(m & (KMOD_CTRL | KMOD_ALT | KMOD_GUI))) {
                    if (k == SDLK_SPACE) dbg_req = 1;
                    else if (k == SDLK_l) dbg_req = 2;
                    else if (k == SDLK_f) dbg_req = 3;
                    else if (k == SDLK_d) { extern int dbg_rec; dbg_rec = 1; int n = dbg_dump("panel"); if (n > 0) dump_n = n; }
                    else if (k == SDLK_t) trace_toggle();
                    break;
                }
                /* The volume, without opening the menu.  A laptop's own volume
                 * keys (a ThinkPad's Fn+F1/F2/F3) arrive as these three, and
                 * Ctrl+Alt with the plus, minus and zero keys does the same
                 * where those are taken -- on the K4510 Linux there is no desktop mixer
                 * behind this program, so this is the only volume there is. */
                { int dv = 0, mute = 0;
                  if (k == SDLK_VOLUMEUP) dv = 10; else if (k == SDLK_VOLUMEDOWN) dv = -10;
                  else if (k == SDLK_AUDIOMUTE || k == SDLK_MUTE) mute = 1;   /* X11 sends one, evdev the other */
                  else if ((m & KMOD_CTRL) && (m & KMOD_ALT)) {
                      if (k == SDLK_EQUALS || k == SDLK_PLUS || k == SDLK_KP_PLUS) dv = 10;
                      else if (k == SDLK_MINUS || k == SDLK_KP_MINUS) dv = -10;
                      else if (k == SDLK_0 || k == SDLK_KP_0) mute = 1;
                      else if (k == SDLK_n) { navi_next(); echo_note("next song"); mlog("navidrome: next song"); break; }   /* the Navidrome sidebar: nothing happens when it is not playing */
                  }
                  if (dv || mute) {
                      static int was;                      /* what to come back to after a mute */
                      int v = settings_get(SET_AUDIO_VOLUME);
                      if (mute) { if (v) { was = v; v = 0; } else v = was ? was : 50; }
                      else { v += dv; if (v < 0) v = 0; if (v > 100) v = 100; }
                      settings_set(SET_AUDIO_VOLUME, v);
                      settings_save(cfg);
                      { char note[48]; int n = snprintf(note, sizeof note, "volume %3d%% ", v);   /* on the glass: ten cells, CP437 blocks */
                        for (int i = 0; i < 10; i++) note[n++] = (char)(i < v / 10 ? 0xDB : 0xB0);
                        note[n] = 0; echo_note(note); }
                      { char lg[32]; snprintf(lg, sizeof lg, "volume %d%%", v); mlog(lg); }             /* stderr, so the log has it: stdout is a tty nobody sees on the K4510 Linux */
                      break;
                  } }
                /* Alt+letter (the left Alt, alone with the letter): a key code of
                 * its own, KEY_ALT_A..Z, and nothing typed -- the menus of the
                 * new EDIT (Doc, 2026-10-02: "similar to the one on later releases
                 * of MS-DOS").  AltGr is the right Alt and still composes; a
                 * program that does not know the codes ignores them (the ROM's
                 * line editor does); JIM turns them into ESC + letter. */
                { SDL_Keymod em = (SDL_Keymod) e.key.keysym.mod;  /* the modifiers AT this key: SDL_GetModState is the
                                                                    * state after every queued event, Alt already up */
                  alt_ate = 0;
                  if ((em & KMOD_LALT) && !(em & (KMOD_CTRL | KMOD_GUI | KMOD_RALT | KMOD_MODE)) && k >= SDLK_a && k <= SDLK_z) {
                      kbd_push_key((uint8_t)(KEY_ALT_A + (k - SDLK_a))); alt_ate = 1; break; }
                  /* Alt+1 / Alt+2: JIM's two screens, K/OS and the terminal (2026-10-05).
                   * The frontend's, never the machine's or the session's. */
                  if ((em & KMOD_LALT) && !(em & (KMOD_CTRL | KMOD_GUI | KMOD_RALT | KMOD_MODE)) && (k == SDLK_1 || k == SDLK_2)) {
                      if (k == SDLK_2 && menu_lock(MENU_LOCK_LINUX)) echo_note("the terminal is locked off here");
                      else io_screen_show(k == SDLK_2);
                      alt_ate = 1; break; } }
                { int lay = settings_get(SET_INPUT_KBD_LAYOUT);   /* the machine's own layout: Ctrl by ITS letter (AZERTY's A), then typing */
                  const uint32_t *le = layout_entry(lay, e.key.keysym.scancode);
                  if (le && (m & KMOD_CTRL)) { uint32_t b = le[0] & KBD_CHAR; if (b >= 'a' && b <= 'z') { kbd_push((uint8_t)(b - 'a' + 1)); break; } }
                  if (le && !(m & (KMOD_CTRL | KMOD_LALT | KMOD_GUI)) && layout_key(lay, e.key.keysym.scancode, m)) break; }
                if ((m & KMOD_CTRL) && k >= 'a' && k <= 'z') { kbd_push((uint8_t)(k - 'a' + 1)); break; }
                if ((m & KMOD_CTRL) && k == SDLK_RIGHTBRACKET) { kbd_push(0x1D); break; }   /* Ctrl-]: TELNET hangs up, as telnet(1) */
                switch (k) {
                case SDLK_RETURN: case SDLK_KP_ENTER: kbd_push(KEY_ENTER); break;
                case SDLK_BACKSPACE: kbd_push(KEY_BS); break;
                case SDLK_TAB:       kbd_push(KEY_TAB); break;
                case SDLK_ESCAPE:    if (m & KMOD_SHIFT) { mlog("quit: Shift+Esc"); running = 0; } else kbd_push(KEY_ESC); break;
                case SDLK_UP: kbd_push_key(KEY_UP); break;     case SDLK_DOWN: kbd_push_key(KEY_DOWN); break;
                case SDLK_LEFT: kbd_push_key(KEY_LEFT); break; case SDLK_RIGHT: kbd_push_key(KEY_RIGHT); break;
                case SDLK_HOME: kbd_push_key(KEY_HOME); break; case SDLK_END: kbd_push_key(KEY_END); break;
                case SDLK_PAGEUP: kbd_push_key(KEY_PGUP); break; case SDLK_PAGEDOWN: kbd_push_key(KEY_PGDN); break;
                case SDLK_INSERT: kbd_push_key(KEY_INS); break; case SDLK_DELETE: kbd_push_key(KEY_DEL); break;
                case SDLK_PAUSE: kbd_push_key(0x9F); break;
                default:
                    if (k >= SDLK_F1 && k <= SDLK_F12) { kbd_push_key((uint8_t)(KEY_F1 + (k - SDLK_F1))); break; }
                    if (!(m & (KMOD_CTRL | KMOD_ALT | KMOD_GUI))) pend = key_ascii(k, m & KMOD_SHIFT);
                    break;
                }
                break; }
            }
        }
        if (pend) {                                          /* no text event came: type the key itself */
            static int said;
            if (!said) { said = 1; fprintf(stderr, "keyboard: SDL sends no text on the %s driver; typing from the key codes\n", SDL_GetCurrentVideoDriver()); }
            kbd_push(pend); pend = 0;
        }
        } while (running && pace_wait());
        host_poll_input();
        { static int menu_was; int m = menu_is_open();               /* the menu is the machine's outside: it frees the pointer */
          if (m && !menu_was) { grab(0); palettes_scan(); }   /* the Palette rows: what is on the disk now */
          if (!m && pal_pending[0]) { extern const char *io_title(void);
              if (!strcmp(io_title(), "K/OS")) { { char b[80]; snprintf(b, sizeof b, "palette: typed at the K/OS prompt: %.30s", pal_pending); mlog(b); } for (const char *c = pal_pending; *c; c++) kbd_push_machine((uint8_t) *c); pal_pending[0] = 0; }
              else { static Uint32 said; if (SDL_GetTicks() - said > 5000) { said = SDL_GetTicks(); char b[96]; snprintf(b, sizeof b, "palette: waiting -- %.40s is running, not the K/OS prompt", io_title()); mlog(b); } } }   /* the machine's
                   * queue, not kbd_push: with the Terminal screen up that typed PALETTE LOAD into its session (the Dell, 2026-10-07) */
          else if (!m && menu_was && grab_wanted && settings_get(SET_INPUT_MOUSE_GRAB)) grab(1);
          if (!settings_get(SET_INPUT_MOUSE_GRAB)) { grab(0); grab_wanted = 0; }
          { static uint32_t cur_gen; if (vicky_palette_gen() != cur_gen) { cur_gen = vicky_palette_gen(); set_retro_cursor(); } }   /* the arrow in the palette's colours */
          { static int cur_shown = -1; int want = (settings_get(SET_INPUT_MOUSE_SHOW) && (!grabbed || mouse_host_wanted())) ? 1 : 0;   /* the host pointer: shown per the setting, hidden while captured -- unless the program asks ($D110 bit1) */
            mouse_host_pointer(want);                                  /* $D110: so a program's own pointer is not a second one */
            if (want != cur_shown) { SDL_ShowCursor(want ? SDL_ENABLE : SDL_DISABLE); cur_shown = want; } }
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
        /* Keys from outside, typed one per frame; ~ waits 30 frames; a byte of
         * $80 or more is a KEY_* code; $1F says "the next byte is a character
         * whatever its value" (an accented letter).  Two sources, the same rules:
         *   K4510_KEYS   a script, once, at start (the screenshots use it)
         *   the key pipe a FIFO, read every frame while the machine runs:
         *                KEYS in this directory on the K4510 Linux, or the path
         *                K4510_KEYPIPE names anywhere.  tools/k4510-type writes
         *                it, so the machine can be driven from another computer
         *                over ssh, a screenshot (k4510-shot) at a time (Doc,
         *                2026-09-14: "can you run the K4510 from here? ... i mean
         *                injecting keystrokes").  0600: only its owner types.
         * The keys reach the machine's keyboard queue, as typing would; the
         * menu key's code there opens F12's menu too (tested 2026-09-14), so
         * the whole machine, settings included, can be driven this way. */
        { static const char *feed; static int feed_init, feed_wait, feed_fr;
          static int kfd = -1, klen, kpos; static char kbuf[4096];
          if (!feed_init) {
              const char *pipe = getenv("K4510_KEYPIPE");
              feed_init = 1; feed = getenv("K4510_KEYS");
              if (!pipe && access("/etc/k4510-linux", F_OK) == 0) pipe = "KEYS";
              if (pipe && *pipe) {
                  struct stat ps;
                  if (lstat(pipe, &ps) == 0 && !S_ISFIFO(ps.st_mode)) unlink(pipe);   /* a stale plain file of the name */
                  if (mkfifo(pipe, 0600) == 0 || errno == EEXIST) kfd = open(pipe, O_RDONLY | O_NONBLOCK);
              }
          }
          if (kfd >= 0 && kpos >= klen) {
              ssize_t n = read(kfd, kbuf, sizeof kbuf);
              if (n > 0) {
                  if (settings_get(SET_INPUT_KEYPIPE) == 0) klen = kpos = 0;   /* F12 says off: read, and dropped -- a writer never hangs */
                  else { klen = (int) n; kpos = 0; }
              }
          }
          if (++feed_fr >= feed_wait) {
              int k = -1, next = -1, piped = 0;
              if (feed && *feed) { k = (uint8_t)*feed++; if (*feed) next = (uint8_t)*feed; }
              else if (kpos < klen) { k = (uint8_t)kbuf[kpos++]; if (kpos < klen) next = (uint8_t)kbuf[kpos]; piped = 1; }
              int shown = piped && settings_get(SET_INPUT_KEYPIPE) == 2;
              /* Two more for testing PROG from outside (2026-09-14): $1E n sends
               * the next key with modifiers n held (1 Shift, 2 Ctrl, 4 Alt), and
               * $1D x,y,buttons,wheel,mods; sets the mouse on the glass (640x480;
               * -1 keeps a coordinate) -- tools/k4510-type --key shift-down, --click. */
              static int pmods = -1;
              if (k == '~') feed_wait = feed_fr + 30;
              else if (k == 0x1E && next >= 0) { pmods = (next - '0') & 7; if (feed && *feed) feed++; else kpos++; }
              else if (k == 0x1D) {
                  char m[48]; int mi = 0, c, v[5] = { -1, -1, 0, 0, 0 };
                  for (;;) {
                      c = (feed && *feed) ? (uint8_t)*feed++ : kpos < klen ? (uint8_t)kbuf[kpos++] : -1;
                      if (c < 0 || c == ';' || mi >= 47) break;
                      m[mi++] = (char) c;
                  }
                  m[mi] = 0;
                  if (sscanf(m, "%d,%d,%d,%d,%d", &v[0], &v[1], &v[2], &v[3], &v[4]) >= 2) {
                      if (v[0] >= 0) mouse_x = v[0];
                      if (v[1] >= 0) mouse_y = v[1];
                      mouse_btn = v[2]; wheel_acc += v[3];
                      kbd_modifiers(v[4] & 1, v[4] & 2, v[4] & 4);
                  }
              }
              else if (k == 0x1F && next >= 0) { if (pmods >= 0) kbd_push_mods((uint8_t) next, (uint8_t) pmods); else kbd_push((uint8_t) next); pmods = -1; if (shown) echo_key(next); if (feed && *feed) feed++; else kpos++; }
              else if (k >= 0x80) { if (pmods >= 0) kbd_push_key_mods((uint8_t) k, (uint8_t) pmods); else kbd_push_key((uint8_t) k); pmods = -1; if (shown) echo_key(k); }
              else if (k >= 0) { uint8_t ch = k == '\n' ? 0x0D : (uint8_t) k; if (pmods >= 0) kbd_push_mods(ch, (uint8_t) pmods); else kbd_push(ch); pmods = -1; if (shown) echo_key(k); }
          } }
        /* The machine's video mode.  Only the ROM can change it -- the console's
         * PCOLS/PROWS/stride are its -- so the menu asks through $D521 bits 5-7
         * and the ROM acts on its next key poll.  Which means the machine has to
         * be running: a frozen one would never see the request, and the point of
         * choosing a resolution in the menu is watching it happen.  So an
         * outstanding request thaws the machine until VICKY's CTRL says it took,
         * or until the wait runs out (a program that never reads a key). */
        { uint8_t c = vicky_read(VR_CTRL);
          int machine = -1;
          if (c & 1) {                                  /* bit 0 is display-enable.  Before the ROM's
                                                         * video_init runs, CTRL is 0 -- which is NOT
                                                         * 640x480, though it looks just like it. */
              int csz = (vicky_read(0x10) >> 5) & 3;    /* layer 0's cells say which grid of a pair:
                                                         * `MODE 0 60` or `MODE /2 67` typed at the
                                                         * prompt is noticed and saved, as a guest's
                                                         * CODEPAGE is (Doc, 2026-09-15) */
              if (c & 0x20) machine = settings_vmode_find(5, vicky_read(VR_SCALE), csz);   /* an IDR: by its scale (2026-10-07) */
              else {
                  uint8_t m = (uint8_t)(c & (2 | 4 | 8 | 16));
                  int mode = m == 0 ? 0 : m == 4 ? 1 : m == 2 ? 2 : m == (2 | 8) ? 3 : m == (2 | 8 | 16) ? 4 : -1;
                  if (mode >= 0) machine = settings_vmode_find(mode, 0, csz);
              }
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
        /* The mode goes out as the ROM's MODE number (the menu's order is not
         * it: vmode_number), whole in $D53C and, where it fits, in $D521's
         * three bits as before. */
        { const vmode_t *vm = settings_vmode((mode_pending ? mode_pending : settings_get(SET_VIDEO_MODE) + 1) - 1);
          int m1 = vm ? vm->mode + 1 : 0;
          io_set_mode((uint8_t) m1); io_set_mode_div(vm ? vm->div : 0);
          io_set_opts((settings_get(SET_SHELL_CPMCOM) ? SYSOPT_CPMCOM : 0)
                      | ((settings_get(SET_SHELL_STARTUP) && !no_startup) ? 0 : SYSOPT_NOBOOT)
                      | (settings_get(SET_VIDEO_STATUSBAR) ? SYSOPT_STATUS : 0)
                      | ((vm && vm->rows60) ? SYSOPT_ROWS60 : 0)   /* the smaller cells: 80x60, 90x67 */
                      | (uint8_t)((m1 <= 7 ? m1 : 0) << SYSOPT_MODE_SHIFT)
                      | (mode_pending ? SYSOPT_MODEREQ : 0)); }
        io_set_bands(1, 1,                               /* one row each, when the bands are on (Doc, 2026-09-14) */
                     (uint8_t)((settings_get(SET_TERM_CLOCK24) ? 1 : 0)
                               | (settings_get(SET_TERM_DATEFMT) << 1)));

        { int want = settings_get(SET_TEXT_CODEPAGE), req = term_page_request();   /* the code page: a guest's CODEPAGE is saved, */
          if (req >= 0) { if (req != want) { settings_set(SET_TEXT_CODEPAGE, req); settings_save(cfg); } }   /* the menu's applied */
          else if (want != term_get_page()) term_set_page(want); }
        Uint64 p_a = SDL_GetPerformanceCounter();
        int open = menu_is_open();
        if ((!open && !paused) || mode_pending) {            /* frozen while the menu is open OR paused; paused keeps the picture */
            machine_frame(vol_machine());
        } else if (paused && dbg_req) {                       /* the panel's keys: a step of the chosen size */
            int vol = vol_machine();
            if (dbg_req == 1) machine_insn(vol); else if (dbg_req == 2) machine_line(vol); else machine_frame(vol);
            dbg_req = 0;
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
            int vol = vol_machine();
            int guard = 4096;                                 /* never more than a few frames of sound ahead */
            while (RING_DEPTH < RING_TARGET && guard--) {
                int16_t tmp[256]; int n = audio_render(CYCLES_PER_LINE, tmp, 256);
                for (int i = 0; i < n; i++) ring_put((int16_t)(tmp[i] * vol_gain(vol) >> 15));
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
              if (state_load(slot_path(act - ACT_LOAD_SLOT)) == 0) load_fonts();   /* the font lives in RAM: an old slot may hold another */
              act = ACT_NONE; }
        switch (act) {
        case ACT_RESET: cpu65_reset(); break;
        case ACT_POWER_CYCLE: host_zero(k4510_ram, K4510_PHYS_SIZE); mem_reset(); /* resets the I/O too */ load_fonts(); mem_load_rom(rom); cpu65_reset();
                              io_screen2_redraw();                /* the Terminal's map was in that RAM: blank it, and its session draws all again */
                              mode_shown = -1; mode_req = 0; break;   /* forget the mode tracking: re-adopt once the ROM is back up */
        case ACT_TUBE_STOP: io_write(IO_TUBE + 3, 2); break;
        case ACT_QUIT: mlog("quit: F12 -> Quit"); running = 0; break;
        case ACT_SHUTDOWN: mlog("quit: F12 -> Power off"); halting_mark(1); shutdown_req = 1; running = 0; break;   /* acted on below, after the settings are saved and SDL has let go of the screen */
        case ACT_SAVE_OFF:                                            /* the machine to RESUME_FILE, then the same power off */
            halting_mark(1);                                          /* before the save: no Chooser while it is written */
            if (state_save(RESUME_FILE ".tmp") == 0 && rename(RESUME_FILE ".tmp", RESUME_FILE) == 0) {
                mlog("quit: F12 -> Save and power off"); shutdown_req = 1; running = 0;
            } else { remove(RESUME_FILE ".tmp"); halting_mark(0); mlog("Save and power off: the state was not written; still running"); }
            break;
        case ACT_NETSETUP: host_net_setup(); break;
        case ACT_SIDEBAR_OPTIONS: {                                   /* F12 -> Video -> Edit options: the sidebar's OPTIONS.CFG in VI */
            extern const char *io_title(void);
            int i = settings_get(SET_VIDEO_SIDEBARS); char path[80], cmd[100];
            if (sidebars_count()) sidebars_prepare(i);                /* made from the zip's copy if it is not there yet */
            sidebars_options_path(sidebars_count() ? i : -1, path, sizeof path);
            if (!strcmp(io_title(), "K/OS")) {                        /* at the prompt: typed for you */
                menu_close(); snprintf(cmd, sizeof cmd, "VI %s\r", path);
                for (const char *c = cmd; *c; c++) kbd_push((uint8_t) *c);
            } else {                                                  /* a program is running: typing into it would be wrong -- say what to type */
                snprintf(echo_txt, sizeof echo_txt, "VI %.44s", path); echo_len = (int) strlen(echo_txt); echo_tag = "remote: "; echo_until = SDL_GetTicks() + 8000;
            }
            break; }
        default:
            if (act >= ACT_PALETTE && act <= ACT_PALETTE + pal_n) {          /* F12 -> Video -> Palette */
                extern const char *io_title(void);
                if (act == ACT_PALETTE) snprintf(pal_pending, sizeof pal_pending, "PALETTE RESET\r");
                else snprintf(pal_pending, sizeof pal_pending, "PALETTE LOAD %s\r", pal_list[act - ACT_PALETTE - 1]);
                if (strcmp(io_title(), "K/OS")) {                              /* a program is running: it goes in when that ends */
                    snprintf(echo_txt, sizeof echo_txt, "palette: at the prompt, when this ends"); echo_len = (int) strlen(echo_txt); echo_tag = "F12: "; echo_until = SDL_GetTicks() + 5000;
                }
            }
            break;
        case ACT_SCREEN_KOS:  io_screen_show(0); break;            /* JIM's two screens: core/io.c, core/term.c */
        case ACT_SCREEN_TERM: if (menu_lock(MENU_LOCK_LINUX)) { echo_note("the terminal is locked off here"); break; }
                              io_screen_show(1); break;
        case ACT_TELNET: if (menu_lock(MENU_LOCK_LINUX)) break;   /* the row is hidden then; this is belt and braces */
                         { menu_close(); const char *c = "TELNET 127.0.0.1 23\r"; while (*c) kbd_push((uint8_t)*c++); } break;   /* typed at the prompt; the menu is shut first so the keys reach the machine */
        } }
        if (open && clock_at_open < 0) clock_at_open = settings_get(SET_CPU_CLOCK);
        { static int host_open_was; static Uint32 host_read_at;   /* the Host page: read at open, then every 2 s while open */
          if (open && (!host_open_was || SDL_GetTicks() - host_read_at >= 2000)) { host_info_refresh(); host_read_at = SDL_GetTicks(); }
          host_open_was = open; host_reap(); }
        /* SETUP's pixel budget ($D548): tools/k4510-vidcap beside the machine,
         * its answer read through a pipe a frame at a time, so the machine
         * runs on while the host is timed. */
        { static pid_t vc_pid; static int vc_fd = -1; static char vc_out[2048]; static size_t vc_n;
          if (io_vidcap_req && !vc_pid) {
              int pfd[2]; io_vidcap_req = 0; vc_n = 0;
              if (pipe(pfd) == 0 && (vc_pid = fork()) == 0) {
                  dup2(pfd[1], 1); dup2(pfd[1], 2); close(pfd[0]); close(pfd[1]);
                  execl("/bin/sh", "sh", "tools/k4510-vidcap", "-", (char *) NULL); _exit(127);
              }
              if (vc_pid > 0) { close(pfd[1]); vc_fd = pfd[0]; fcntl(vc_fd, F_SETFL, O_NONBLOCK); mlog("vidcap: measuring the host for SETUP"); }
              else { io_vidcap_state = 3; vc_pid = 0; }
          }
          if (vc_pid > 0) {
              ssize_t r;
              while (vc_n < sizeof vc_out - 1 && (r = read(vc_fd, vc_out + vc_n, sizeof vc_out - 1 - vc_n)) > 0) vc_n += (size_t) r;
              int st;
              if (waitpid(vc_pid, &st, WNOHANG) == vc_pid) {
                  while (vc_n < sizeof vc_out - 1 && (r = read(vc_fd, vc_out + vc_n, sizeof vc_out - 1 - vc_n)) > 0) vc_n += (size_t) r;
                  close(vc_fd); vc_fd = -1; vc_pid = 0; vc_out[vc_n] = 0;
                  const char *m = strstr(vc_out, "suggested video.cap = "); long cap = m ? atol(m + 22) : 0;
                  if (cap >= 64000) {
                      settings_set(SET_VIDEO_CAP, (int) cap); settings_save(cfg);
                      io_vidcap_k = (uint16_t)(cap / 1000); io_vidcap_state = 2;
                      char b[96]; snprintf(b, sizeof b, "vidcap: video.cap = %ld, kept for the next start", cap); mlog(b);
                  } else { io_vidcap_state = 3; mlog("vidcap: no answer from tools/k4510-vidcap"); }
              }
          } }
        if (vol_machine() == 100 && vol_owned) {                    /* the K4510 Linux: the setting is ALSA's Master, applied when it moves */
            static int vol_applied = -1; int v = settings_get(SET_AUDIO_VOLUME);
            if (v != vol_applied) { vol_applied = v; vol_master(v); }
        }
        if (io_palname_new) {                                         /* the machine loaded a palette (or PALETTE RESET): kept, */
            io_palname_new = 0; settings_set_palette(io_palname);    /* written at once -- a power cut should not lose it */
            { char b[96]; snprintf(b, sizeof b, "palette: the machine loaded '%.60s'; kept", io_palname); mlog(b); }
            if (settings_changed()) settings_save(cfg);
        }
        { static Uint32 view_until; int login = host_remote_login();   /* REMOTE at the bottom left (core/io.h io_remote) */
          if (viewed) { viewed = 0; view_until = SDL_GetTicks() + 60000; }
          io_remote = (uint8_t)((settings_get(SET_INPUT_KEYPIPE) ? REMOTE_KEYS : 0) | (login ? REMOTE_LOGIN : 0)
                                | ((Sint32)(view_until - SDL_GetTicks()) > 0 ? REMOTE_VIEW : 0)); }
        host_battery_poll();                                          /* $D53A, every ten seconds */
        host_net_poll();                                              /* the band's network, the same */
        { static Uint32 beat_at; static unsigned long loops; loops++;  /* the heartbeat: a freeze is the beats stopping */
          if (SDL_GetTicks() - beat_at >= 10000) { char b[96]; beat_at = SDL_GetTicks();
              snprintf(b, sizeof b, "alive: %lu loops, Tube %s, menu %s", loops, (io_read(IO_TUBE) & 1) ? "running" : "idle", open ? "open" : "shut");
              mlog(b); } }
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
            if (clock_at_open >= 0 && settings_get(SET_CPU_CLOCK) != clock_at_open && settings_get(SET_CPU_AUTO))
                settings_set(SET_CPU_AUTO, 0);     /* a clock chosen by hand is not to be second-guessed at the next boot */
            clock_at_open = -1;
            if (settings_changed()) settings_save(cfg);
            host_keymap_apply();                  /* layout / Caps-as-Ctrl changed: the Linux side too (K4510 Linux only) */
            host_lid_apply();                     /* Lid closed: take or let go of logind's lid lock */
            host_charge_apply();                  /* Charge to 100% once: lift or put back the battery's limits */
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
        if (!settings_get(SET_CPU_AUTO) || open || paused || io_measuring()) { gov_t0 = 0; gov_mach = 0; gov_frames = 0; gaps_seen = io_audio_gaps; gov_restart(&gov); }
        else {
            Uint64 nowc = SDL_GetPerformanceCounter(), hzc = SDL_GetPerformanceFrequency();
            if (!gov_t0) { gov_t0 = nowc; gov_mach = 0; gov_frames = 0; gaps_seen = io_audio_gaps; }
            else if (nowc - gov_t0 >= hzc * 3 && gov_frames >= 30) {
                double ms = (double)gov_mach * 1000.0 / (double)hzc / gov_frames;
                unsigned g = io_audio_gaps >= gaps_seen ? io_audio_gaps - gaps_seen : 0;   /* the guest can clear $D524 mid-window: that is not four billion gaps */
                int s = settings_get(SET_CPU_CLOCK), down = clock_step_below(s), up = clock_step_above(s);
                /* the ceiling for the way back up: what SETUP measured on this host, or
                 * the step an unmeasured machine starts at -- never more than that */
                unsigned ceiling = io_clock_measured() ? settings_cpu_hz_of(settings_get(SET_CPU_MEASURED)) : (unsigned) CPU_HZ;
                int way = gov_decide(&gov, ms, g, settings_cpu_hz_of(s), up >= 0 ? settings_cpu_hz_of(up) : 0,
                                     down >= 0 ? settings_cpu_hz_of(down) : 0, ceiling, SDL_GetTicks());
                if (way > 0) {
                    settings_set(SET_CPU_CLOCK, up); settings_save(cfg); gov_own = 1;
                    fprintf(stderr, "clock: %.1f ms a frame at %.1f MHz for thirty seconds and no gap, stepping up to %.1f\n",
                            ms, settings_cpu_hz_of(s) / 1e6, settings_cpu_hz_of(up) / 1e6);
                }
                if (way < 0) {
                    gov_own = 1;
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
            cpu_hz_now = settings_cpu_hz(); cycles_per_line = cpu_hz_now / 60 / (unsigned) frame_lines;
            io_set_cpu_khz(cpu_hz_now / 1000); audio_set_cpu_hz((double)cpu_hz_now);
            /* A new clock is a new machine to measure: open another PERF window
             * and append it.  This is how the Pi gets swept -- there is no
             * K4510_CPU_HZ on the card, only the menu. */
            p_n = 0; p_last = 0; p_tot = 0;          /* the whole-frame sum too, or every later window divides the run by PERF_FRAMES */
            gov_t0 = 0;                                  /* and a new machine to judge: the governor's window restarts */
            if (!gov_own) gov_restart(&gov);             /* somebody else's change (the menu, SETUP): the quiet count was about another clock */
            gov_own = 0;
        }
        if (settings_get(SET_VIDEO_FULLSCREEN) != fullscreen_applied) { fullscreen_applied = settings_get(SET_VIDEO_FULLSCREEN); SDL_SetWindowFullscreen(win, fullscreen_applied ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0); }
        /* panel_track: the panel is the renderer's output once full screen (a
         * monitor plugged in, a resolution changed under us), the canvas the
         * Canvas row's.  A change rebuilds VICKY's list and the Resolution row,
         * and asks K/OS to lay its console out again on the same scale. */
        { static int fs_frames, fs_was = -1; int pw = 0, ph = 0; const char *pe = getenv("K4510_PANEL");
          if (fullscreen_applied != fs_was) { fs_was = fullscreen_applied; fs_frames = 0; }   /* the switch takes a frame or two to land */
          if (fullscreen_applied && ++fs_frames > 3) SDL_GetRendererOutputSize(ren, &pw, &ph); else { pw = vicky_panel_w(); ph = vicky_panel_h(); }
          if (pe) sscanf(pe, "%dx%d", &pw, &ph);
          if (pw >= 320 && ph >= 200 && (pw != vicky_panel_w() || ph != vicky_panel_h() || settings_get(SET_VIDEO_BASE) != vicky_panel_base())) {
              vicky_set_panel(pw, ph, settings_get(SET_VIDEO_BASE)); settings_video_rebuild(); menu_dirty();
              if (mode_shown >= 0) { mode_shown = settings_get(SET_VIDEO_MODE); mode_req = mode_shown + 1; mode_wait = MODE_REQ_FRAMES; }
          } }
        /* the menu takes the machine's own row grid: 30 rows over a 240-line
         * mode or 640x480 in 8x16 cells, 60 over 640x480 in 8x8, so its lines
         * sit on the picture's lines */
        if (ui_cell_h(((vicky_read(VR_CTRL) & 6) || (vicky_read(0x10) & 0x60)) ? 16 : 8)) menu_dirty();
        menu_draw(ov);

        /* Vertical sync, live.  SDL_RenderSetVSync arrived in 2.0.18, so on
         * anything older the setting simply cannot be honoured and the
         * machine keeps its own pacing -- which is the default anyway.  Not
         * on the Pi: the shim's present is the blocking one all of this was
         * escaped from. */
        if (settings_get(SET_VIDEO_VSYNC) != vsync_applied) {
            vsync_applied = settings_get(SET_VIDEO_VSYNC);
            if (SDL_RenderSetVSync(ren, vsync_applied) != 0)
                fprintf(stderr, "vsync: this SDL or driver will not take it (%s)\n", SDL_GetError());
        }
        /* A software resolution may ask to be FITTED (VICKY's GLASSCTL bit4,
         * 2026-10-07): sharp-bilinear, the whole multiple then smoothing, for
         * as long as it is up.  K/OS and every IDR stay integer. */
        { int gc = vicky_glass_ctl(), want = settings_get(SET_VIDEO_SMOOTH);
          if ((vicky_read(VR_CTRL) & 0x20) && (gc & 3) == VG_SOFT && (gc & 0x30)) want = SMOOTH_FIT;
        if (want != smooth_applied) {
            smooth_applied = want;
            /* Hard pixels always ("soft", the linear filter, went 2026-09-14);
             * Integer is a whole-number scale, so every pixel of the
             * machine is the same size on the glass. */
            SDL_SetTextureScaleMode(tex, SDL_ScaleModeNearest);
            SDL_RenderSetIntegerScale(ren, smooth_applied == SMOOTH_INTEGER ? SDL_TRUE : SDL_FALSE);
        } }
        /* the palettes, once a frame instead of once a pixel: the machine's
         * colours, the same half-lit behind the menu, and the menu's own; the
         * 256-entry tables only when VICKY's palette changed (review 2026-09-05, 9) */
        { static uint32_t gen_done = 0xFFFFFFFFu; static int frame_done = -1;
          int fr = settings_get(SET_VIDEO_BORDER_COLOUR) & 15, fol = settings_get(SET_VIDEO_FRAME_FOLLOW) != 0;
          io_frame = (uint8_t) fr; io_frame_follow = (uint8_t) fol;            /* JIM paints the bands with them */
          if (vicky_palette_gen() != gen_done || frame_done != fr * 2 + fol) {
              gen_done = vicky_palette_gen(); frame_done = fr * 2 + fol; tex_stale = 1;
              for (int i = 0; i < 256; i++) {
                  uint32_t c = vicky_palette_rgb(i) & 0xFFFFFF;
                  pal[i] = 0xFF000000u | c; mpal[i] = 0xFF000000u | ((c >> 1) & 0x7F7F7F);
              }
              memcpy(fpal, pal, sizeof fpal); memcpy(fmpal, mpal, sizeof fmpal);
              if (!fol) {                                                      /* the bands' entries, as term.c chose them */
                  uint32_t bg = frame_vic[fr], fg = frame_text_rgb(bg);
                  int fi = fr == 1 ? 0 : 1;
                  fpal[fr] = 0xFF000000u | bg; fpal[fi] = 0xFF000000u | fg;
                  fmpal[fr] = 0xFF000000u | ((bg >> 1) & 0x7F7F7F); fmpal[fi] = 0xFF000000u | ((fg >> 1) & 0x7F7F7F);
              }
          }
          { uint8_t a, b2, c2, d2; int claimed = 0;
            int fx = !fol && vicky_bands(&a, &b2, &c2, &d2, &claimed) && !claimed;   /* a program's own bands keep its colours */
            if (fx != frame_fixed) { frame_fixed = fx; tex_stale = 1; } }
          for (int i = 0; i < UIC_COUNT; i++) { uint32_t o = upal[i]; upal[i] = 0xFF000000u | (ui_palette_rgb(i) & 0xFFFFFF); if (upal[i] != o) tex_stale = 1; }
          border_lit = 0xFF000000u | frame_rgb();
        }

        void *pixels; int pitch;
        p_a = SDL_GetPerformanceCounter();
        /* Nothing new to show -- same picture, same overlay, same tables --
         * and the texture already holds it: skip the 307,200 lookups (review
         * 2026-09-05, 9).  A still screen at the prompt is most frames. */
        const int gw = vicky_out_w(), gh = vicky_out_h();       /* the picture: the glass (640x480, or an HD mode's own size), twice it with HD text */
        int tex_same = 0;                                       /* the picture is the last frame's (still frames, below) */
        { static uint8_t last_fb[sizeof fb], last_ov[sizeof ov]; static int last_open = -1, last_gw, last_gh;
          if (!tex_stale && open == last_open && gw == last_gw && gh == last_gh
              && !memcmp(fb, last_fb, sizeof fb) && (!open || !memcmp(ov, last_ov, sizeof ov))) { tex_same = 1; goto tex_done; }
          tex_stale = 0; last_open = open; last_gw = gw; last_gh = gh;
          memcpy(last_fb, fb, sizeof fb); if (open) memcpy(last_ov, ov, sizeof ov); }
        if (SDL_LockTexture(tex, NULL, &pixels, &pitch) != 0) goto tex_done;   /* checked, as every other lock here is: pixels is garbage otherwise */
        for (int y = 0; y < gh; y++) {
            const uint8_t *src = fb + y * VICKY_WIDTH;     /* the menu is its own layer now, drawn over this (below) */
            uint32_t *d = (uint32_t *)((uint8_t *)pixels + y * pitch);
            const uint32_t *rp = row_pal(y, open);                  /* a band line: the frame's colours */
            for (int x = 0; x < gw; x++) d[x] = rp[src[x]];
        }
        SDL_UnlockTexture(tex);
tex_done:
        if (shot_req) { shot_req = 0; shot_save(fb, open ? ov : NULL, open ? mpal : pal, upal); shot_flash = 4; shot_full = 1; }   /* what the texture holds; the whole display below */
        if (screen_req) { screen_req = 0; screen_save(); }                                                         /* the text screen, as text: no flash, it is not a picture */
        p_tex += SDL_GetPerformanceCounter() - p_a;
        p_a = SDL_GetPerformanceCounter();
        /* Still frames (2026-10-06).  When the picture is the last frame's and
         * nothing around it moves -- a plain border, no side panel, no menu,
         * no key echo, the window and the settings as they were -- the window
         * already shows this frame: drawing and presenting it again was 3 ms
         * of every idle frame on the Dell.  Skipped, the display keeps what it
         * has.  A window event, a screenshot, or a second since the last full
         * frame (the sidebars' timer lives in there) draws it again. */
        int echo_on = echo_len && (Sint32)(echo_until - SDL_GetTicks()) > 0;
        int echo_vis = !echo_banded && font_panel && (echo_on || io_remote);   /* REMOTE too: with no bands to carry it (off, claimed, the menu up) it is here, always */
        { static uint32_t last_sig; static Uint32 last_full; static int captures = -1;
          int ow = 0, oh = 0; SDL_GetRendererOutputSize(ren, &ow, &oh);
          uint32_t sig = 2166136261u;
          #define SIG(v) (sig = (sig ^ (uint32_t)(v)) * 16777619u)
          SIG(gw); SIG(gh); SIG(ow); SIG(oh); SIG(open); SIG(echo_vis); SIG(border_lit); SIG(smooth_applied); SIG(fullscreen_applied);
          SIG(settings_get(SET_VIDEO_BORDER)); SIG(settings_get(SET_VIDEO_SIDEBARS)); SIG(settings_get(SET_VIDEO_PLACE)); SIG(io_tube_kind());
          #undef SIG
          if (captures < 0) captures = getenv("K4510_GLASS") || getenv("K4510_SHOT") || getenv("K4510_NOSTILL");   /* NOSTILL: every frame drawn, to measure against */
          Uint32 tn = SDL_GetTicks();
          static unsigned side_n; int side_due = (++side_n & 1) == 0;   /* the moving sidebars: every other frame, 30 a second */
          if (tex_same && (frame_static || (frame_sides && !side_due)) && sig == last_sig && !present_force && !shot_flash && !captures
              && (frame_sides || tn - last_full < 1000)) goto frame_still;
          drawn_bits |= 1;                                      /* this frame is drawn (the pacer counts them) */
          last_sig = sig; present_force = 0; last_full = tn; }
        { int b = settings_get(SET_VIDEO_BORDER) * gw / 640;     /* the border's pixels are 640-glass pixels */
          uint32_t bc = frame_rgb();
          geo_b = b;                                             /* for the mouse */
          SDL_Rect gsrc = { 0, 0, gw, gh };                      /* the glass, in the top-left of the largest texture */
          SDL_Rect dr = { b, b, gw - 2 * b, gh - 2 * b };
          /* Placement and the side panel (Doc, 2026-09-09).  Centred, the
           * picture is SDL's logical canvas and SDL scales and centres it, as
           * always.  Placed left or right, SDL's mapping is switched OFF and
           * the geometry is worked out here in device pixels: the scale the
           * picture would get anyway (floored to an integer for Integer),
           * the picture at one edge, the panel in the rest.  Working it out
           * here rather than widening SDL's canvas was forced: the software
           * renderer drew nothing right of the picture on a widened canvas
           * above 1x, and a mapping that cannot be trusted is not worth
           * arguing with. */
          /* what the sidebar setting draws (core/sidebars.c); the register panel is
           * one of the choices since 2026-09-15 (Doc: "Register becomes a choice in
           * the Sidebar") and everything below still asks panel_kind */
          /* Sidebars off for now (Doc, 2026-10-06: "turn off the sidebars for now,
           * let's just concentrate on the K4510"): the setting is out of the F12
           * menu, and whatever it says, both sides are the plain border -- no
           * gamebars, no register panel.  -DK4510_SIDEBARS=1 brings them back. */
#if K4510_SIDEBARS
          int sv = settings_get(SET_VIDEO_SIDEBARS), sbar = sidebars_builtin(sv);
#else
          int sv = sidebars_count() ? sidebars_find("border") : SIDEBAR_BORDER, sbar = SIDEBAR_BORDER;
#endif
          /* each side's sidebar and its own clock (step 5): the right can be another
           * (SIDEBARS.CFG right =), the choice can change on a timer (change =),
           * and each runs at its speed (OPTIONS.CFG speed =).  Once a second: the
           * files read again if they changed, the options handed to the scenes,
           * and every five minutes what they keep written to STATE.DAT. */
          static int shown[2] = { -1, -1 }, shown_v = -1; static Uint32 shown_at, vlast, state_at; static double vclk[2];
          { Uint32 tn = SDL_GetTicks();
            if (shown_v != sv || tn - shown_at >= 1000) {
                time_t wall = time(NULL); struct tm lt; localtime_r(&wall, &lt);
                shown_at = tn; shown_v = sv;
                sidebars_poll();
                for (int s2 = 0; s2 < 2; s2++) {
                    int i = sidebars_shown(sv, s2, (long) wall, lt.tm_mon + 1), b2 = sidebars_builtin(i);
                    shown[s2] = i;
                    if (i >= 0 && sidebars_count()) {
                        sidebars_prepare(i);
                        if (b2 >= SIDEBAR_HALLOWEEN) { const char *d = sidebars_opt(i, "day"); saver_option(b2 - SIDEBAR_HALLOWEEN, "day", d ? d : "real"); }
                    }
                }
                { int ni = sidebars_find("navidrome");           /* the radio's options, whether or not it is on the glass: RADIO at the prompt plays without the sidebar */
                  if (ni >= 0) { static const char *const nk[] = { "server", "user", "password", "play", "gain" }; sidebars_prepare(ni);
                                 for (int q = 0; q < 5; q++) saver_option(SAVER_NAVIDROME, nk[q], sidebars_opt(ni, nk[q])); } }
                navi_active(sidebars_builtin(shown[0]) == SIDEBAR_NAVIDROME || sidebars_builtin(shown[1]) == SIDEBAR_NAVIDROME);   /* it plays while it is on the glass */
                if (!state_at) state_at = tn;
                if (tn - state_at >= 300000) { state_at = tn; sidebar_save_states(); }
            }
            Uint32 dt = vlast ? tn - vlast : 0; if (dt > 250) dt = 250; vlast = tn;
            for (int s2 = 0; s2 < 2; s2++) vclk[s2] += dt * (sidebars_count() ? sidebars_speed(shown[s2]) : 1.0); }
          int sb_side[2] = { sidebars_builtin(shown[0] >= 0 ? shown[0] : sv), sidebars_builtin(shown[1] >= 0 ? shown[1] : sv) };
          if (sbar == SIDEBAR_REGISTERS) sb_side[0] = sb_side[1] = SIDEBAR_BORDER;   /* the panel is drawn on its own, below */
#if !K4510_SIDEBARS
          sb_side[0] = sb_side[1] = SIDEBAR_BORDER;
#endif
          int grad = sb_side[0] == SIDEBAR_GRADIENT || sb_side[1] == SIDEBAR_GRADIENT;
          Uint32 gclk = (Uint32) vclk[sb_side[0] == SIDEBAR_GRADIENT ? 0 : 1];   /* the gradient's clock */
          int place = settings_get(SET_VIDEO_PLACE), panel_kind = sbar == SIDEBAR_REGISTERS ? PANEL_REGS : PANEL_OFF;
          if (panel_kind != PANEL_OFF && place == PLACE_CENTRE) place = PLACE_LEFT;
          /* for the host shell's children: tek40xx places its page the same
           * way (Doc, 2026-09-09: "the tek programs should respect the
           * placement option"), and goes full screen when the machine is */
          { static int place_env = -1, fs_env = -1;
            if (place != place_env) { place_env = place; setenv("K4510_PLACEMENT", place == PLACE_LEFT ? "left" : place == PLACE_RIGHT ? "right" : "centre", 1); }
            if (fullscreen_applied != fs_env) { fs_env = fullscreen_applied; if (fullscreen_applied) setenv("TEK40XX_FULLSCREEN", "1", 1); else unsetenv("TEK40XX_FULLSCREEN"); }
            /* Same screen real estate as the machine (Doc, 2026-09-11: "the
             * same screen real estate as the k4510 screen ... to maintain the
             * illusion of a seamless machine").  A host program run through `!`
             * inherits this environment at fork time, so we keep the live
             * window rectangle in it: K4510_WINRECT for our own tek40xx (which
             * sizes AND places itself to match), and SDL_VIDEO_WINDOW_POS,
             * which SDL2 honours for ANY program's window (position only --
             * size is the program's own).  Full screen takes precedence, so
             * the rect is cleared then and TEK40XX_FULLSCREEN drives it. */
            { static char rect_env[48] = ""; char now[48] = "";
              if (!fullscreen_applied) {
                  int wx = 0, wy = 0, ww = 0, wh = 0;
                  SDL_GetWindowPosition(win, &wx, &wy); SDL_GetWindowSize(win, &ww, &wh);
                  snprintf(now, sizeof now, "%d,%d,%d,%d", wx, wy, ww, wh);
              }
              if (strcmp(now, rect_env)) {
                  strcpy(rect_env, now);
                  if (now[0]) { char pos[24]; int wx = 0, wy = 0; sscanf(now, "%d,%d", &wx, &wy);
                                snprintf(pos, sizeof pos, "%d,%d", wx, wy);
                                setenv("K4510_WINRECT", now, 1); setenv("SDL_VIDEO_WINDOW_POS", pos, 1); }
                  else        { unsetenv("K4510_WINRECT"); unsetenv("SDL_VIDEO_WINDOW_POS"); }
              } } }
          int lw = gw, canvas_h = gh, cow = 0, coh = 0;
          SDL_GetRendererOutputSize(ren, &cow, &coh);
          int custom = place != PLACE_CENTRE && cow > 0 && coh > 0;
          double sc = 1.0; int pic_x = 0, pic_y = 0, pic_w = lw, pic_h = canvas_h;
          if (custom) {
              sc = (double)cow / lw; if ((double)coh / canvas_h < sc) sc = (double)coh / canvas_h;
              /* Integer floors the scale.  The panel never shrinks the
               * picture (Doc, 2026-09-09: "the emulator screen does not need
               * to be reduced in size"); it takes what is left. */
              if (smooth_applied == SMOOTH_INTEGER) sc = (double)(int)sc;
              if (sc < 1.0) sc = 1.0;
              pic_w = (int)(lw * sc); pic_h = (int)(canvas_h * sc);
              pic_y = (coh - pic_h) / 2; pic_x = place == PLACE_RIGHT ? cow - pic_w : 0;
              dr.x = pic_x + (int)(b * sc); dr.y = pic_y + (int)(b * sc);
              dr.w = (int)((gw - 2 * b) * sc); dr.h = (int)((gh - 2 * b) * sc);
          }
          geo_s = custom ? sc : 1.0; geo_xd = custom ? pic_x : 0; geo_yd = custom ? pic_y : 0;
          { static int set_w, set_h;
            if (lw != set_w || canvas_h != set_h) { set_w = lw; set_h = canvas_h; logical_set = 0; } }   /* a new glass: a new canvas */
          if (custom != logical_custom || !logical_set) {
              logical_custom = custom; logical_set = 1;
              SDL_RenderSetLogicalSize(ren, custom ? 0 : lw, custom ? 0 : canvas_h);
          }
          /* where the pointer may go, full screen (confine_clamp, above): the
           * canvas in window coordinates -- or all of it when the side panel
           * shares the screen, which is the one place off the picture it may go */
          { SDL_Rect wr = { 0, 0, 0, 0 }; int want = fullscreen_applied || io_host_kind;
            if (want) {
                int ww = 0, wh = 0; SDL_GetWindowSize(win, &ww, &wh);
                if (custom) {
                    int x0 = pic_x, y0 = pic_y, x1 = pic_x + pic_w, y1 = pic_y + pic_h;
                    if (panel_kind != PANEL_OFF && cow - pic_w >= 64) { x0 = 0; y0 = 0; x1 = cow; y1 = coh; }
                    wr.x = x0 * ww / cow; wr.y = y0 * wh / coh; wr.w = (x1 - x0) * ww / cow; wr.h = (y1 - y0) * wh / coh;
                } else {
                    int x0, y0, x1, y1;
                    SDL_RenderLogicalToWindow(ren, 0.0f, 0.0f, &x0, &y0);
                    SDL_RenderLogicalToWindow(ren, (float) lw, (float) canvas_h, &x1, &y1);
                    wr.x = x0; wr.y = y0; wr.w = x1 - x0; wr.h = y1 - y0;
                }
            }
            { static SDL_Rect last; static int last_on = -1;
              confine_on = want && wr.w > 0 && wr.h > 0; confine_r = wr;
              if (confine_on != last_on || memcmp(&wr, &last, sizeof wr)) {
                  SDL_SetWindowMouseRect(win, confine_on ? &wr : NULL);
                  last = wr; last_on = confine_on;
                  /* the pointer starts where the video driver put it -- the
                   * top-left corner, outside the picture (the Dell, 2026-09-14)
                   * -- and the clamp above only acts on motion: bring it in now,
                   * to the middle, whenever the area is set or changes */
                  if (confine_on) { int mx, my; SDL_GetMouseState(&mx, &my);
                      if (mx < wr.x || my < wr.y || mx >= wr.x + wr.w || my >= wr.y + wr.h)
                          SDL_WarpMouseInWindow(win, wr.x + wr.w / 2, wr.y + wr.h / 2); }
              } } }
          int bcol = settings_get(SET_VIDEO_BORDER_COLOUR);
          if (!btex) {
              btex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, 1, VICKY_HEIGHT);
              if (btex) SDL_SetTextureScaleMode(btex, SDL_ScaleModeNearest);   /* the picture's filter: hard pixels */
              btex_col = -1;
          }
          /* the border colour flat -- refilled when it changes, the entry or
           * what the palette makes of it (a PALETTE LOAD in STARTUP.BAT left the
           * border the boot palette's grey beside the new one's bands: Doc,
           * 2026-10-06) -- or the gradient, refilled every frame: a column of
           * 1080 pixels */
          if (btex && (btex_col != bcol || btex_rgb != border_lit || btex_smooth != smooth_applied || btex_sbar != grad || btex_gh != gh || grad)) {
              void *bp; int bpitch;
              btex_col = bcol; btex_rgb = border_lit; btex_smooth = smooth_applied; btex_sbar = grad; btex_gh = gh;
              if (SDL_LockTexture(btex, NULL, &bp, &bpitch) == 0) {
                  if (grad) {
                      int s, v, h0 = sb_rgb_hue(border_lit, &s, &v), ph = (int)((uint64_t) gclk * 1536 / 24000 % 1536);
                      if (s < 160) s = 160;
                      v = v * 3 / 4; if (v < 80) v = 80;
                      for (int y = 0; y < VICKY_HEIGHT; y++)
                          *(uint32_t *)((uint8_t *)bp + y * bpitch) = sb_hue_rgb(h0 + ph + (y & ~3) * 1536 / (gh > 0 ? gh : 480), s, v);
                  } else
                      for (int y = 0; y < VICKY_HEIGHT; y++) *(uint32_t *)((uint8_t *)bp + y * bpitch) = border_lit;
                  SDL_UnlockTexture(btex);
              }
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
              SDL_Rect bsrc = { 0, 0, 1, gh };
              int ow = 0, oh = 0, lh = gh;
              SDL_GetRendererOutputSize(ren, &ow, &oh);
              if (ow > 0 && oh > 0) {
                  /* ASK SDL where the picture lands rather than working it out
                   * again here.  Reimplementing the rule was tried first and it
                   * was right for two of the three Scaling modes and wrong for
                   * Integer, where SDL_RenderSetIntegerScale floors the scale
                   * by its own arithmetic -- the bars came out a stripe out of
                   * step with the border, which is the exact fault this is
                   * meant to remove.  LogicalToWindow is SDL's own answer and
                   * cannot disagree with SDL. */
                  int wy0 = 0, wy1 = 0, wx = 0;
                  if (custom) { wy0 = pic_y; wy1 = pic_y + pic_h; }
                  else {
                      SDL_RenderLogicalToWindow(ren, 0.0f, 0.0f, &wx, &wy0);
                      SDL_RenderLogicalToWindow(ren, 0.0f, (float)lh, &wx, &wy1);
                  }
                  int py = wy0, ph = wy1 - wy0;
                  if (ph > 0) {
                      if (!custom) SDL_RenderSetLogicalSize(ren, 0, 0);
                      for (int y = py; y > -ph; y -= ph) { SDL_Rect d = { 0, y, ow, ph }; SDL_RenderCopy(ren, btex, &bsrc, &d); }
                      for (int y = py + ph; y < oh; y += ph) { SDL_Rect d = { 0, y, ow, ph }; SDL_RenderCopy(ren, btex, &bsrc, &d); }
                      if (!custom) SDL_RenderSetLogicalSize(ren, lw, lh);
                  } else SDL_RenderCopy(ren, btex, &bsrc, NULL);
              } else SDL_RenderCopy(ren, btex, &bsrc, NULL);
          }
          /* the scene savers (sdl/savers.c): each sidebar, the window's full
           * height, painted in machine pixels every frame and scaled to the
           * picture's pixel size; not on the side panel's side */
          if (sb_side[0] >= SIDEBAR_HALLOWEEN || sb_side[1] >= SIDEBAR_HALLOWEEN) {
              static SDL_Texture *stex[2]; static int stw[2], sth[2];
              int ow2 = 0, oh2 = 0, sx0, sy0, sx1, sy1;
              SDL_GetRendererOutputSize(ren, &ow2, &oh2);
              if (custom) { sx0 = pic_x; sx1 = pic_x + pic_w; sy0 = pic_y; sy1 = pic_y + pic_h; }
              else { SDL_RenderLogicalToWindow(ren, 0.0f, 0.0f, &sx0, &sy0); SDL_RenderLogicalToWindow(ren, (float) lw, (float) canvas_h, &sx1, &sy1); }
              double ms = (double)(sy1 - sy0) / (gh > 0 ? gh : 480);   /* device pixels a machine pixel */
              if (ms > 0.1 && ow2 > 0 && oh2 > 0) {
                  if (!custom) SDL_RenderSetLogicalSize(ren, 0, 0);
                  for (int side = 0; side < 2; side++) {
                      int a = side ? sx1 : 0, e = side ? ow2 : sx0;
                      if (panel_kind != PANEL_OFF && custom && ((place == PLACE_LEFT) == (side == 1))) continue;   /* the panel's side */
                      if (sb_side[side] < SIDEBAR_HALLOWEEN) continue;
                      int mw = (int)((e - a) / ms), mh = (int)(oh2 / ms) + 1;
                      if (mw < 8 || mh < 8) continue;
                      if (!stex[side] || stw[side] != mw || sth[side] != mh) {
                          if (stex[side]) SDL_DestroyTexture(stex[side]);
                          stex[side] = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, mw, mh);
                          if (stex[side]) SDL_SetTextureScaleMode(stex[side], SDL_ScaleModeNearest);
                          stw[side] = mw; sth[side] = mh;
                      }
                      void *sp; int spitch;
                      if (stex[side] && SDL_LockTexture(stex[side], NULL, &sp, &spitch) == 0) {
                          saver_font(font_panel, font_panel_rows);   /* the machine's glyphs, for the scenes that draw characters */
                          saver_draw(sb_side[side] - SIDEBAR_HALLOWEEN, (uint32_t *) sp, spitch / 4, mw, mh, (uint32_t) vclk[side], side);
                          SDL_UnlockTexture(stex[side]);
                          int dw = (int)(mw * ms + 0.5), dh = (int)(mh * ms + 0.5);
                          SDL_Rect d = { side ? a : e - dw, (oh2 - dh) / 2, dw, dh };   /* against the picture's edge */
                          SDL_RenderCopy(ren, stex[side], NULL, &d);
                      }
                  }
                  if (!custom) SDL_RenderSetLogicalSize(ren, lw, canvas_h);
              }
          }
          /* Sharp-bilinear (Doc, 2026-09-14: "soft" scaling back, but not for the
           * integer modes).  Fit on a picture that does not divide the screen --
           * 640x480 on 1080 lines is 2.25x -- would make some pixels two lines
           * tall and some three.  So: hard pixels to the whole multiple below
           * (2x) into a texture, then smoothing for the last bit (1.125x): even
           * pixels, and only their edges soft.  Integer, and an HD mode that
           * fits exactly, stay pixel for pixel. */
          { float esx = 1.0f, esy = 1.0f; double eff;
            if (custom) eff = sc; else { SDL_RenderGetScale(ren, &esx, &esy); eff = esy; }
            int n = (int) eff;
            static SDL_Texture *sbt; static int sbw, sbh;
            int drawn = 0;
            if (smooth_applied == SMOOTH_FIT && n >= 1 && eff - n > 0.02 && SDL_RenderTargetSupported(ren)) {
                int tw = gw * n, th = gh * n;
                if (!sbt || sbw != tw || sbh != th) {
                    if (sbt) SDL_DestroyTexture(sbt);
                    sbt = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, tw, th);
                    if (sbt) SDL_SetTextureScaleMode(sbt, SDL_ScaleModeLinear);
                    sbw = tw; sbh = th;
                }
                if (sbt) {
                    if (!custom) SDL_RenderSetLogicalSize(ren, 0, 0);
                    if (SDL_SetRenderTarget(ren, sbt) == 0) {
                        SDL_Rect whole = { 0, 0, tw, th };
                        SDL_RenderCopy(ren, tex, &gsrc, &whole);   /* hard pixels: tex is Nearest */
                        SDL_SetRenderTarget(ren, NULL);
                        if (!custom) SDL_RenderSetLogicalSize(ren, lw, canvas_h);
                        SDL_RenderCopy(ren, sbt, NULL, &dr);        /* the rest, smoothed */
                        drawn = 1;
                    } else if (!custom) SDL_RenderSetLogicalSize(ren, lw, canvas_h);
                }
            }
            if (!drawn) SDL_RenderCopy(ren, tex, &gsrc, &dr);
            /* Scanlines (VICKY's GLASSCTL bit6, 2026-10-08: MODE -c, Doc's
             * "scanline options"): every machine row keeps its top half and
             * has its bottom half dimmed, as a CRT's beam left a dark gap
             * between lines.  Only where a row is two screen lines or more --
             * at scale 1 there is no half to dim.  A 1-wide column of k
             * screen lines a row, laid over the picture: hard at a whole
             * scale, smoothed at a fitted one. */
            if (vicky_glass_ctl() & 0x40) {
                static SDL_Texture *slt; static int slh, slk;
                double rowpx = (double) dr.h * (custom ? 1.0 : esy) / gh;
                int k = (int)(rowpx + 0.5);
                if (k >= 2) {
                    if (!slt || slh != gh || slk != k) {
                        if (slt) SDL_DestroyTexture(slt);
                        slt = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, 1, gh * k);
                        slh = gh; slk = k;
                        void *lp; int lpitch;
                        if (slt && SDL_LockTexture(slt, NULL, &lp, &lpitch) == 0) {
                            for (int y = 0; y < gh * k; y++)
                                *(uint32_t *)((uint8_t *) lp + y * lpitch) = y % k >= k - k / 2 ? 0x90000000u : 0;
                            SDL_UnlockTexture(slt);
                        }
                        if (slt) SDL_SetTextureBlendMode(slt, SDL_BLENDMODE_BLEND);
                    }
                    if (slt) {
                        SDL_SetTextureScaleMode(slt, rowpx - k > 0.02 || k - rowpx > 0.02 ? SDL_ScaleModeLinear : SDL_ScaleModeNearest);
                        SDL_RenderCopy(ren, slt, NULL, &dr);
                    }
                }
            } }
          /* The F12 menu: its own 640x480 layer over the picture, scaled the
           * same way whatever the mode -- hard pixels to the whole multiple,
           * smoothing for the rest.  Stretched into the picture it was drawn
           * 1.125x at 720x540 and halved at 360x270, and its letters came out
           * ragged (Doc, 2026-09-15: "the f7 menu font in the newer mode is
           * crappy ... perhaps it should always be the same one, like in modes
           * 0, 1 and 2"). */
          if (open) {
              static SDL_Texture *mtex;
              if (!mtex && (mtex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, UI_W, UI_H)) != NULL)
                  SDL_SetTextureScaleMode(mtex, SDL_ScaleModeNearest);
              void *mp; int mpitch;
              if (mtex && SDL_LockTexture(mtex, NULL, &mp, &mpitch) == 0) {
                  for (int y = 0; y < UI_H; y++) {
                      uint32_t *d = (uint32_t *)((uint8_t *)mp + y * mpitch); const uint8_t *o = ov + y * UI_W;
                      for (int x = 0; x < UI_W; x++) d[x] = o[x] ? upal[o[x]] : 0x00000000u;
                  }
                  SDL_UnlockTexture(mtex);
              }
              /* Whole menu pixels only (Doc, 2026-10-06: the menu looked striped,
               * as if scanlines were on).  It was drawn at the picture's size:
               * 2.25x on the Dell's 1080 lines, a 2x copy smoothed up the last
               * quarter, so every fourth row came out soft.  Now the largest
               * whole multiple that fits the picture, hard pixels, centred on it
               * -- 2x there, 1280x960 inside 1440x1080. */
              float esx = 1.0f, esy = 1.0f; double eff2;
              if (custom) eff2 = sc; else { SDL_RenderGetScale(ren, &esx, &esy); eff2 = esy; }
              double me = eff2 * gh / (double) UI_H; int n = (int) me, done = 0;   /* device pixels a menu pixel */
              if (mtex && n >= 1) {
                  int px0, py0, px1, py1;                                 /* the picture, in device pixels */
                  if (custom) { px0 = pic_x; py0 = pic_y; px1 = pic_x + pic_w; py1 = pic_y + pic_h; }
                  else { SDL_RenderLogicalToWindow(ren, (float) dr.x, (float) dr.y, &px0, &py0);
                         SDL_RenderLogicalToWindow(ren, (float)(dr.x + dr.w), (float)(dr.y + dr.h), &px1, &py1); }
                  while (n > 1 && (UI_W * n > px1 - px0 || UI_H * n > py1 - py0)) n--;
                  SDL_Rect md = { px0 + (px1 - px0 - UI_W * n) / 2, py0 + (py1 - py0 - UI_H * n) / 2, UI_W * n, UI_H * n };
                  if (px1 > px0 && py1 > py0) {                          /* the same rect in glass pixels, for the mouse */
                      int mgw = vicky_glass_w(), mgh = vicky_glass_h();        /* mouse_x spans the picture in the machine's pixels */
                      menu_gx0 = (md.x - px0) * mgw / (px1 - px0); menu_gw = md.w * mgw / (px1 - px0);
                      menu_gy0 = (md.y - py0) * mgh / (py1 - py0); menu_gh = md.h * mgh / (py1 - py0);
                  }
                  if (!custom) SDL_RenderSetLogicalSize(ren, 0, 0);
                  SDL_SetTextureBlendMode(mtex, SDL_BLENDMODE_BLEND);
                  SDL_RenderCopy(ren, mtex, NULL, &md);
                  if (!custom) SDL_RenderSetLogicalSize(ren, lw, canvas_h);
                  done = 1;
              }
              if (mtex && !done) { SDL_SetTextureBlendMode(mtex, SDL_BLENDMODE_BLEND); SDL_RenderCopy(ren, mtex, NULL, &dr); }
          }
          /* the side panel: the device pixels beside the picture, at the picture's rows */
          { int pw = custom ? cow - pic_w : 0;
            if (panel_kind != PANEL_OFF && custom && pw >= 64) {
                /* the whole window's height, not the picture's: a 16:9 screen
                 * has rows to spare above and below a 4:3 picture and the
                 * panel is the one thing here that wants them */
                if (!ptex || ptex_w != pw || ptex_h != coh) {
                    if (ptex) SDL_DestroyTexture(ptex);
                    ptex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, pw, coh);
                    ptex_w = pw; ptex_h = coh;
                    if (ptex) SDL_SetTextureScaleMode(ptex, SDL_ScaleModeNearest);
                }
                if (ptex) { void *pp; int ppitch;
                    if (SDL_LockTexture(ptex, NULL, &pp, &ppitch) == 0) {
                        int g = panel_scale(pw, coh, font_panel_rows);
                        {
                            panel_info pi = { panel_fps, io_host_kind ? "on its Linux" : "on a desktop", settings_cpu_hz(),
                                              paused, m_line, trace_n, trace_f != NULL, dump_n };
                            panel_render((uint32_t *)pp, ppitch / 4, pw, coh, g, font_panel, font_panel_rows, &pi);
                        }
                        SDL_UnlockTexture(ptex);
                    }
                    SDL_Rect pd = { place == PLACE_RIGHT ? 0 : pic_w, 0, pw, coh };
                    SDL_RenderCopy(ren, ptex, NULL, &pd);
                }
            } }
          /* the key pipe's echo: a bar at the foot of the window, in device
           * pixels (out of the logical mapping, as the glass capture below
           * steps), the panel's CP437 font at 1-3x for the window's height */
          /* what moves by itself is drawn every frame; with none of it, a frame can stand still (above) */
          frame_static = sb_side[0] == SIDEBAR_BORDER && sb_side[1] == SIDEBAR_BORDER && panel_kind == PANEL_OFF && !open && !echo_vis;
          frame_sides = !frame_static && panel_kind == PANEL_OFF && !open && !echo_vis;   /* only the sidebars move */
          if (echo_vis) {   /* until four seconds after the last key; the bottom band has it when there is one */
              static SDL_Texture *etex; static int etw, eth;
              char eline[96]; int en = 0;
              if (io_remote) en = snprintf(eline, sizeof eline, " REMOTE:%s%s%s%s", io_remote & REMOTE_KEYS ? " keys" : "", io_remote & REMOTE_LOGIN ? " login" : "",
                                           io_remote & REMOTE_VIEW ? " viewed" : "", echo_on ? " |" : " ");
              if (echo_on) en += snprintf(eline + en, sizeof eline - (size_t) en, " %s%.*s ", echo_tag, echo_len, echo_txt);
              int tw = en * 8, th = font_panel_rows;
              if (!etex || etw != tw || eth != th) {
                  if (etex) SDL_DestroyTexture(etex);
                  etex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, tw, th);
                  etw = tw; eth = th;
                  if (etex) SDL_SetTextureScaleMode(etex, SDL_ScaleModeNearest);
              }
              void *ep; int epitch;
              if (etex && SDL_LockTexture(etex, NULL, &ep, &epitch) == 0) {
                  uint32_t *px = (uint32_t *) ep; int pp = epitch / 4;
                  for (int y = 0; y < th; y++) for (int x = 0; x < tw; x++) px[y * pp + x] = 0xFF202020u;
                  for (int c = 0; c < en; c++) {
                      const uint8_t *gl = font_panel + (uint8_t) eline[c] * font_panel_rows;
                      for (int y = 0; y < th; y++) for (int x = 0; x < 8; x++) if (gl[y] & (0x80 >> x)) px[y * pp + c * 8 + x] = 0xFFF0C040u;
                  }
                  SDL_UnlockTexture(etex);
                  int gw = 0, gh = 0;
                  if (!custom) SDL_RenderSetLogicalSize(ren, 0, 0);
                  SDL_GetRendererOutputSize(ren, &gw, &gh);
                  int sc = gh >= 960 ? 3 : gh >= 480 ? 2 : 1;
                  SDL_Rect d = { 8, gh - th * sc - 8, tw * sc, th * sc };
                  SDL_RenderCopy(ren, etex, NULL, &d);
                  if (!custom) SDL_RenderSetLogicalSize(ren, lw, canvas_h);
              }
          }
          /* the fps the panel shows: frames presented per wall-clock second */
          { static Uint64 t0; static unsigned n; Uint64 now = SDL_GetPerformanceCounter(); n++;
            if (!t0) t0 = now;
            else if (now - t0 >= SDL_GetPerformanceFrequency()) { panel_fps = (double)n * SDL_GetPerformanceFrequency() / (double)(now - t0); t0 = now; n = 0; } }
          if (shot_full) {                     /* the screenshot's second picture: the display, before the flash is drawn over it */
              shot_full = 0;
              int fw = 0, fh = 0; SDL_GetRendererOutputSize(ren, &fw, &fh);
              uint32_t *fp = fw > 0 && fh > 0 ? malloc((size_t) fw * fh * 4) : NULL;
              if (fp) {
                  if (!custom) SDL_RenderSetLogicalSize(ren, 0, 0);    /* device units, as K4510_GLASS reads them */
                  int rc = SDL_RenderReadPixels(ren, NULL, SDL_PIXELFORMAT_ARGB8888, fp, fw * 4);
                  if (!custom) SDL_RenderSetLogicalSize(ren, lw, canvas_h);
                  if (rc == 0) shot_save_full(fp, fw, fh);
                  free(fp);
              }
          }
          /* K4510_GLASS=file.ppm:frames -- the whole window as the renderer has it, panel and bars included */
          { static const char *glass; static int glass_fr, glass_init;
            if (!glass_init) { glass_init = 1; glass = getenv("K4510_GLASS"); if (glass) { const char *c = strrchr(glass, ':'); glass_fr = c ? atoi(c + 1) : 120; } }
            if (glass && --glass_fr == 0) {
                int gw = 0, gh = 0; SDL_GetRendererOutputSize(ren, &gw, &gh);
                uint32_t *gp = gw > 0 && gh > 0 ? malloc((size_t)gw * gh * 4) : NULL;
                char path[256]; snprintf(path, sizeof path, "%.*s", (int)(strrchr(glass, ':') ? strrchr(glass, ':') - glass : (long)strlen(glass)), glass);
                FILE *f = gp ? fopen(path, "wb") : NULL;
                /* Read in DEVICE units: with a logical size set, ReadPixels takes its
                 * rect in the mapping, and the software renderer (Xvfb) got that
                 * wrong above 1x -- the panel vanished from the shot but not from
                 * the glass.  Step out of the mapping, as the border tiling does. */
                if (!custom) SDL_RenderSetLogicalSize(ren, 0, 0);
                int grc = f ? SDL_RenderReadPixels(ren, NULL, SDL_PIXELFORMAT_ARGB8888, gp, gw * 4) : -1;
                if (!custom) SDL_RenderSetLogicalSize(ren, lw, canvas_h);
                if (f && grc == 0) {
                    fprintf(f, "P6 %d %d 255\n", gw, gh);
                    for (int i = 0; i < gw * gh; i++) { fputc((gp[i] >> 16) & 255, f); fputc((gp[i] >> 8) & 255, f); fputc(gp[i] & 255, f); }
                }
                if (f) fclose(f);
                free(gp);
                running = 0; } } }
        if (shot_flash > 0) {                /* a screenshot was taken: the screen inverts for four frames (Doc's idea) */
            static int invert_ok = -1; static SDL_BlendMode inv;
            if (invert_ok < 0) {             /* dst = 1 - dst: white drawn with this blend inverts what is under it */
                inv = SDL_ComposeCustomBlendMode(SDL_BLENDFACTOR_ONE_MINUS_DST_COLOR, SDL_BLENDFACTOR_ZERO, SDL_BLENDOPERATION_ADD,
                                                 SDL_BLENDFACTOR_ZERO, SDL_BLENDFACTOR_ONE, SDL_BLENDOPERATION_ADD);
                invert_ok = SDL_SetRenderDrawBlendMode(ren, inv) == 0;
            }
            if (invert_ok) { SDL_SetRenderDrawBlendMode(ren, inv); SDL_SetRenderDrawColor(ren, 255, 255, 255, 255); }
            else { SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND); SDL_SetRenderDrawColor(ren, 255, 255, 255, 160); }   /* a white flash instead */
            SDL_RenderFillRect(ren, NULL);
            SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_NONE);
            shot_flash--;
        }
        SDL_RenderPresent(ren);
frame_still:
        if (lat_f) lat_shown();
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
          /* At rest (2026-10-06, for time on battery): nobody at the keys for
           * two seconds, the audio device closed, nothing moving beside the
           * picture, and hardly anything drawn (six of the last 64 frames at
           * most: the cursor's blink is allowed, an animation is not) -- and
           * the loop sleeps three frames at a time, then runs the two it owes
           * back to back (the deadline above lets it catch up).  The machine
           * still gets its 60 frames a second, in bursts: 20 wakeups a second
           * for the host instead of 60 (on KMSDRM, 100: pace_wait's slices).
           * A key wakes the wait at once and ends it (2026-10-09; it waited
           * for the next wakeup, 50 ms at most, before).  Not when the cursor's
           * blink changes within the burst: it would show up to two frames
           * late, a different late each time, and the blink limped (Doc,
           * 2026-10-09). */
          int rest = SDL_GetTicks() - input_ms > 2000 && !adev && frame_static && !open && !paused && __builtin_popcountll(drawn_bits) <= 6
                     && term_blink_due() > 3;
          drawn_bits <<= 1;
          pace_next = next;                                    /* waited for at the top of the loop, with the events (pace_wait); */
          pace_until = now >= next ? 0 : rest ? next + 2 * per : next;   /* a frame owed runs at once, as it did */
        }
        { static const char *shot; static int shot_fr, shot_init;      /* K4510_SHOT=file.ppm:frames -- a screenshot of what is on the glass */
          if (!shot_init) { shot_init = 1; shot = getenv("K4510_SHOT"); if (shot) { const char *c = strrchr(shot, ':'); shot_fr = c ? atoi(c + 1) : 120; } }
          if (shot && --shot_fr == 0) {
              char path[256]; snprintf(path, sizeof path, "%.*s", (int)(strrchr(shot, ':') ? strrchr(shot, ':') - shot : (long) strlen(shot)), shot);
              FILE *f = fopen(path, "wb");
              int sh = vicky_out_h(), sw = vicky_out_w(), mo = menu_is_open();
              if (f) { fprintf(f, "P6 %d %d 255\n", sw, sh);   /* the glass, and the menu over it when it is open (its own layer on the screen) */
                       for (int y = 0; y < sh; y++) for (int x = 0; x < sw; x++) {
                           uint8_t o = mo ? ov[(y * UI_H / sh) * UI_W + x * UI_W / sw] : 0;
                           uint32_t p = o ? upal[o] : row_pal(y, mo)[fb[y * VICKY_WIDTH + x]];
                           fputc((p >> 16) & 255, f); fputc((p >> 8) & 255, f); fputc(p & 255, f); }
                       fclose(f); }
              running = 0; } }
    }
    sidebar_save_states();                                /* the ant colony, and anything else a sidebar keeps */
    if (settings_changed()) settings_save(cfg);
    io_tube_shutdown();                                   /* a co-processor still running */
    mlog("exit: the frame loop ended normally");
    SDL_DestroyTexture(tex); SDL_DestroyRenderer(ren); SDL_DestroyWindow(win); SDL_Quit();
    /* Shut the computer down, in this order and not another: the settings are
     * already written above, SDL has given the console back, and only then do
     * we ask for the power off.  The helper syncs and unmounts the persistence
     * partition before halting, which is what makes it safe to pull the stick
     * out afterwards -- see linux/build-live.sh. */
    if (shutdown_req) execl("/usr/local/sbin/k4510-poweroff", "k4510-poweroff", (char *) NULL);
    return 0;
}

int main(int argc, char **argv) { return k4510_frontend_main(argc, argv); }
