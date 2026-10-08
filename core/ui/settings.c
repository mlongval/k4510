/* The settings registry. See settings.h. */
#include "settings.h"
#include "vicky.h"
#include "idr.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <strings.h>

/* Bumped when an old file needs interpreting differently; see settings_load. */
#define SETTINGS_VERSION     3
#define SETTINGS_VERSION_STR "3"

static const char *const smooth_names[]= { "integer", "fit to display" };
static const char *const place_names[] = { "centre", "left", "right" };
static const char *const panel_names[] = { "off", "registers" };
/* the sidebars when there are no zips to list them (core/sidebars.c): SIDEBAR_* order */
/* This array must have SIDEBAR_COUNT entries: the row below declares that many
 * labels, and settings_text indexes the array with the value, so one name
 * short is a read past the end.  MATRIX was added to eight other lists on
 * 2026-09-16 and missed here, which nothing caught: core/sidebars.c replaces
 * these labels with the zips' own names at startup (settings_set_labels), so
 * the short array is only reached when there are no zips -- and the handbook's
 * generator, which reads this line, quietly dropped matrix from the list of
 * sidebars in Chapter 1. */
static const char *const sidebar_names[]= { "border", "gradient", "registers", "halloween", "christmas", "space", "river", "dreamfall", "tetris", "antfarm", "matrix", "navidrome" };
static const char *const base_names[]  = { "4:3", "full" };   /* the canvas: the largest 4:3 in the panel, or all of it */
static const char *const date_names[]  = { "DD.MM.YYYY", "YYYY-MM-DD", "MM/DD/YYYY" };
static const char *const lid_names[]   = { "keep running", "suspend" };
static const char *const pipe_names[]  = { "off", "on", "on, shown" };
static const char *const kbd_names[]   = { "Host", "US", "US-intl", "Canada-FR", "France", "Germany", "Spain", "UK", "Italy" };   /* after "Host", kbdmaps.h's order;
                                                                                                                                       * k4510-keymap reads these names */
static const char *const cpu_names[]   = { "202.5 MHz", "162 MHz", "121.5 MHz", "81 MHz", "60 MHz",
                                           "40.5 MHz", "30 MHz", "20 MHz", "15 MHz", "10 MHz" };
static const char *const chord_names[] = { "Super+PageUp", "Ctrl+PageUp", "Alt+PageUp", "Ctrl+Alt+Del" };
static const char *const mkey_names[]  = { "F7", "F8", "F11", "Pause", "F12" };
static const char *const page_names[]  = { "CP437", "K4510" };
static const char *const hdfont_names[] = { "unscii", "Zhekov Bold", "Zhekov", "Spleen", "IBM VGA", "Atkinson Mono", "Go Mono",
                                            "Fira Mono", "Proggy Clean", "Tamzen Bold" };
const char *const hdfont_files[HDFONT_COUNT] = { NULL, "zhekov-bold", "zhekov", "spleen", "ibm-vga", "atkinson", "go-mono",
                                                 "fira-mono", "proggy", "tamzen-bold" };

static const char *const frame_names[16] = { "Black", "White", "Red", "Cyan", "Purple", "Green", "Blue", "Yellow",
                                             "Orange", "Brown", "Light red", "Dark grey", "Grey", "Light green", "Light blue", "Light grey" };
static set_desc desc[SET_COUNT] = {        /* not const: the Sidebars choices are filled in at start (settings_set_labels) */
    { "video.border",        "Border width",   ST_INT,   0, 0, 64, 4, 0, 0, SF_LIVE },
    { "video.border_colour", "Frame colour",   ST_ENUM, 11, 0, 0, 0, frame_names, 16, SF_LIVE },   /* the border and the bands; dark grey */
    { "video.mode",          "Resolution",     ST_ENUM,  0, 0, 0, 0, NULL, 0, SF_LIVE },   /* choices built at run time: settings_video_rebuild */
    { "term.bands",          "Status bands",   ST_BOOL,  0, 0, 1, 1, 0, 0, SF_LIVE },   /* two static bands frame a scrolling console */
    { "video.smoothing",     "Scaling",        ST_ENUM,  SMOOTH_INTEGER, 0, 0, 0, smooth_names, SMOOTH_COUNT, SF_LIVE },
    { "video.fullscreen",    "Full screen",    ST_BOOL,  1, 0, 1, 1, 0, 0, SF_LIVE },   /* fixed on since 2026-10-06 (settings_load) */
    /* Vertical sync, off by default -- which is the machine keeping its own
     * 60 Hz and presenting when it is ready, as it does on the Pi.  Turning it
     * on hands the pacing to the display: on a host whose refresh is not
     * exactly 60 that costs frames, and frames are sound here (a machine at
     * 51.8 fps makes 51.8 frames of sound where the device wants 60, and you
     * hear the difference).  Off, the trade is tearing.  Neither answer is
     * right for every host, which is why it is a row and not a decision. */
    { "video.vsync",         "Vertical sync",  ST_BOOL,  0, 0, 1, 1, 0, 0, SF_LIVE },
    /* A 4:3 picture on a 16:9 screen leaves a third of it empty.  Placement
     * puts the picture at one edge; the side panel fills what is left with the
     * machine's registers, the next instructions, the banks (Doc, 2026-09-09:
     * "the rest of the physical display could be used for educational
     * purposes").  A panel with the picture centred makes no sense, so the
     * panel forces left. */
    { "video.placement",     "Placement",      ST_ENUM,  PLACE_CENTRE, 0, 0, 0, place_names, PLACE_COUNT, SF_LIVE },
    { "video.panel",         "Side panel",     ST_ENUM,  PANEL_OFF, 0, 0, 0, panel_names, PANEL_COUNT, SF_LIVE },
    { "video.sidebars",      "Sidebars",       ST_ENUM,  SIDEBAR_BORDER, 0, 0, 0, sidebar_names, SIDEBAR_COUNT, SF_LIVE },
    { "audio.volume",        "Volume",         ST_INT,   80, 0, 100, 10, 0, 0, SF_LIVE },
    /* audio.chip and audio.sids lived here until 2026-09-05, when the SIDs
     * were removed.  An old k4510.cfg still carrying them is fine: unknown
     * keys are kept and ignored. */
    { "input.reset_chord",   "Reset chord",    ST_CHORD, CHORD_SUPER_PGUP, 0, 0, 0, chord_names, CHORD_COUNT, SF_LIVE },
    { "input.menu_key",      "Menu key",       ST_ENUM,  MENUKEY_F12, 0, 0, 0, mkey_names, MENUKEY_COUNT, SF_LIVE },
    { "input.mouse_grab",    "Mouse capture",  ST_BOOL,  0, 0, 1, 1, 0, 0, SF_LIVE },   /* a click captures the pointer; the menu releases it.  OFF by default since
                                                                                            2026-09-11: on a laptop the vanishing pointer startled Doc; a game turns it on */
    { "input.mouse_pointer", "Mouse pointer",  ST_BOOL,  1, 0, 1, 1, 0, 0, SF_LIVE },   /* on: the host pointer shows over the picture instead of vanishing (Doc, 2026-09-11) */
    { "shell.cpm_com",       "CP/M .COM by name", ST_BOOL, 0, 0, 1, 1, 0, 0, SF_LIVE },   /* off: typing d must not launch a Z80 program */
    { "shell.startup",       "Run STARTUP.BAT", ST_BOOL, 1, 0, 1, 1, 0, 0, SF_RESTART },  /* read at power-on: the way out of a bad one */
    /* The machine is a fantasy and its timings are suggestions.  40.5 is
     * only where a host starts before it has been measured, not a ceiling:
     * the menu offers everything the enum has, because what a host can hold
     * is a question about that host, not about this machine.  A desktop
     * measured over 120 MHz should be allowed to run there; a slow one asked
     * for 202.5 will crawl, and the setting is live, so stepping back down is
     * how you find out.  INFO reports whichever is set. */
    { "cpu.clock",           "CPU clock",      ST_ENUM,  CPUCLK_40_5, 0, 0, 0, cpu_names, CPUCLK_COUNT, SF_LIVE },
    /* ...and those defaults are only where a host starts before it has been
     * measured.  With cpu.auto on, the frontend runs core/calib.c at power-on
     * and sets cpu.clock to the highest step the host holds with margin; the
     * result is kept here with the host it was taken on, so it is paid once.
     * Choosing a clock in the menu turns auto off: an explicit setting wins. */
    { "cpu.auto",            "Auto clock",     ST_BOOL,  1, 0, 1, 1, 0, 0, SF_RESTART },
    { "cpu.measured",        "Measured clock", ST_ENUM,  CPUCLK_15, 0, 0, 0, cpu_names, CPUCLK_COUNT, 0 },
    { "cpu.host",            "Measured on",    ST_INT,   0, 0, 0x7FFFFFFF, 1, 0, 0, 0 },
    { "term.clock24",        "24-hour clock",  ST_BOOL,  1, 0, 1, 1, 0, 0, SF_LIVE },
    { "term.datefmt",        "Date format",    ST_ENUM,  DATEFMT_DMY, 0, 0, 0, date_names, DATEFMT_COUNT, SF_LIVE },
    { "input.caps_ctrl",     "Caps Lock is Ctrl", ST_BOOL, 0, 0, 1, 1, 0, 0, SF_LIVE },   /* the old Unix keyboard's Ctrl, where Caps Lock sits (Doc, 2026-09-12) */
    { "input.kbd_layout",    "Keyboard layout", ST_ENUM, 0, 0, 0, 0, kbd_names, 9, SF_LIVE },   /* the machine's; the K4510 Linux follows it */
    { "host.lid",            "Lid closed",     ST_ENUM, 0, 0, 0, 0, lid_names, 2, SF_LIVE },   /* keep running (Doc's rule of 2026-09-11), or suspend */
    { "input.keypipe",       "Key pipe",       ST_ENUM, 0, 0, 0, 0, pipe_names, 3, SF_LIVE },  /* remote typing: off (the default since 2026-10-08: remote control is off unless turned on) / on / on, shown */
    { "text.codepage",       "Code page",      ST_ENUM,  PAGE_CP437, 0, 0, 0, page_names, PAGE_COUNT, SF_LIVE },
    { "video.hdfont",        "Font",           ST_ENUM,  HDFONT_ZHEKOV, 0, 0, 0, hdfont_names, HDFONT_COUNT, SF_LIVE },
    { "video.base",          "Canvas",         ST_ENUM,  0, 0, 0, 0, base_names, 2, SF_LIVE },
    { "video.cap",           "Pixel cap",      ST_INT,   2073600, 64000, 2304000, 64000, 0, 0, 0 },
    { "host.charge_once",    "Charge to 100% once", ST_BOOL, 0, 0, 1, 1, 0, 0, SF_LIVE },   /* then back to the usual limit */
    { "video.frame_follow",  "Frame follows palette", ST_BOOL, 1, 0, 1, 1, 0, 0, SF_LIVE },   /* off: the VIC-II colour, whatever the palette */
    { "video.palette",       "Palette",        ST_INT,   0, 0, 0, 0, 0, 0, 0 },   /* text, kept beside the table: pal_name */
    { "term.battime",        "Battery time",   ST_BOOL,  0, 0, 1, 1, 0, 0, SF_LIVE },   /* h:mm left, beside the battery's % */
};
static const unsigned cpu_hz_table[CPUCLK_COUNT] = { 202500000u, 162000000u, 121500000u, 81000000u, 60000000u,
                                                     40500000u, 30000000u, 20000000u, 15000000u, 10000000u };
unsigned settings_cpu_hz_of(int step) { if (step < 0 || step >= CPUCLK_COUNT) step = 0; return cpu_hz_table[step]; }
unsigned settings_cpu_hz(void) { return settings_cpu_hz_of(settings_get(SET_CPU_CLOCK)); }
static int value[SET_COUNT];
static int changed;
static char pal_name[64];                  /* video.palette: the one setting that is text */
const char *settings_palette(void) { return pal_name; }
void settings_set_palette(const char *path)
{
    if (!path) path = "";
    if (strcmp(pal_name, path)) { snprintf(pal_name, sizeof pal_name, "%s", path); changed = 1; }
}

/* The Resolution row's choices (settings.h): the classic screens, then this
 * panel's IDRs.  "640x480" is the 80x30 screen, as every k4510.cfg saved it. */
#define VM_MAX (VMODE_IDR0 + 2 * IDR_MAX)
static vmode_t vm[VM_MAX] = {
    { "640x480", 0, 0, 0, 1, 80, 30 }, { "640x480x60", 0, 1, 0, 0, 80, 60 }, { "640x240", 1, 0, 0, 0, 80, 30 },
    { "320x240", 2, 0, 0, 0, 40, 30 }, { "320x200", 3, 0, 0, 0, 40, 25 }, { "160x200", 4, 0, 0, 0, 20, 25 } };
static const char *vm_labels[VM_MAX];
static int vm_n;                                 /* 0: not built yet */
static int vm_want_div = 4, vm_want_small = 0;   /* what k4510.cfg asked for, by scale: resolved when the list is built */
static int vm_cells(int w, int h, int csz, int *cols, int *rows)   /* a grid K/OS can run: 25 rows, 132 columns */
{
    int cw = csz >= 2 ? 16 : 8, ch = csz == 0 ? 8 : csz == 3 ? 32 : 16;
    *cols = w / cw; *rows = h / ch;
    return *rows >= 25 && *cols >= 40 && *cols <= 132;
}
void settings_video_rebuild(void)
{
    int keep_div = vm_want_div, keep_small = vm_want_small;          /* the choice, by scale (settings_set keeps it) */
    vm_n = VMODE_IDR0;
    for (int i = 0; i < vicky_idr_count() && vm_n + 2 <= VM_MAX; i++) {
        const vicky_idr *d = vicky_idr_at(i);
        int wide = d->scale == 1 || d->w >= 1280, big = wide ? 3 : 1, small = wide ? 2 : 0, c, r;   /* as the ROM chooses (video_init) */
        for (int k = 0; k < 2; k++) {
            int csz = k ? small : big;
            if (!vm_cells(d->w, d->h, csz, &c, &r)) continue;
            if (k && vm_n > VMODE_IDR0 && vm[vm_n - 1].div == d->scale && vm[vm_n - 1].rows == r) continue;
            vmode_t *e = &vm[vm_n++];
            snprintf(e->label, sizeof e->label, "%dx%d %dx%d", d->w, d->h, c, r);
            e->mode = 5; e->div = (unsigned char) d->scale; e->csz = (unsigned char) csz; e->rows60 = (unsigned char) k;
            e->cols = (unsigned char) c; e->rows = (unsigned char) r;
        }
    }
    for (int i = 0; i < vm_n; i++) vm_labels[i] = vm[i].label;
    desc[SET_VIDEO_MODE].labels = vm_labels; desc[SET_VIDEO_MODE].nlabels = vm_n;
    /* the choice: the same scale and cells if this panel has them; else the
     * nearest scale it offers, the smaller cells only if asked for */
    { int best = -1, bd = 1 << 30;
      for (int i = VMODE_IDR0; i < vm_n; i++) {
          int small = vm[i].csz == 0 || vm[i].csz == 2, dd = (vm[i].div - keep_div) * (vm[i].div - keep_div) * 4 + (small != keep_small);
          if (dd < bd) { bd = dd; best = i; }
      }
      value[SET_VIDEO_MODE] = best >= 0 ? best : VMODE_640x480;
      desc[SET_VIDEO_MODE].def = value[SET_VIDEO_MODE]; }
}
static void vm_ready(void) { if (!vm_n) settings_video_rebuild(); }
const vmode_t *settings_vmode(int i) { vm_ready(); return i >= 0 && i < vm_n ? &vm[i] : NULL; }
int settings_vmode_count(void) { vm_ready(); return vm_n; }
int settings_vmode_find(int mode, int div, int csz)
{
    vm_ready();
    if (mode < 5) { for (int i = 0; i < VMODE_IDR0; i++) if (vm[i].mode == mode && (mode || vm[i].csz == csz)) return i; return -1; }
    for (int i = VMODE_IDR0; i < vm_n; i++) if (vm[i].div == div && vm[i].csz == csz) return i;
    for (int i = VMODE_IDR0; i < vm_n; i++) if (vm[i].div == div) return i;   /* the grid VICKY fell back to */
    return -1;
}
/* the old names, and the classic screens, as the scale K/OS boots in (it
 * always runs in an IDR: 640x480 comes back as /2, the games' as /4) */
static int vm_parse(const char *v)
{
    static const struct { const char *name; int div, small; } old[] = {
        { "1440x1080 16x32", 1, 0 }, { "1440x1080 16x16", 1, 1 }, { "1440x1080", 1, 0 },
        { "720x540 16x32", 2, 0 }, { "720x540 16x16", 2, 1 }, { "720x540", 2, 0 }, { "360x270", 4, 0 },
        { "640x480", 2, 0 }, { "640x480x60", 2, 1 }, { "640x240", 2, 0 }, { "320x240", 4, 0 },
        { "320x200", 4, 0 }, { "160x200", 4, 0 } };
    if (v[0] == '/') { int d = atoi(v + 1); if (d < 1) d = 1; vm_want_div = d; vm_want_small = strstr(v, "small") != NULL; return 1; }
    for (size_t i = 0; i < sizeof old / sizeof *old; i++) if (!strcasecmp(v, old[i].name)) { vm_want_div = old[i].div; vm_want_small = old[i].small; return 1; }
    return 0;
}

const set_desc *settings_desc(set_id id) { return &desc[id]; }
/* An ENUM may have choices the menu does not offer: settings_set still accepts
 * them (the machine can be in one, and the row must say so) but stepping and
 * the popup stop short. */
int settings_choices(set_id id)
{
    if (id == SET_VIDEO_MODE) vm_ready();
    return desc[id].nlabels;
}
int settings_get(set_id id) { return value[id]; }
/* The first choice the menu offers.  For the mode it is 1440x1080 since
 * 2026-10-06 (Doc: only the even dividers of the panel's 4:3 -- 1440x1080,
 * 720x540, 360x270); 640x480, 640x240 and 320x240 are still modes the machine
 * can be in, a program's MODE 0, 1 or 2, and the row says so. */
int settings_first(set_id id)
{
    if (id == SET_CPU_CLOCK || id == SET_CPU_MEASURED) return CPUCLK_FASTEST;
    if (id == SET_VIDEO_MODE) return VMODE_IDR0;     /* the IDRs: the classic screens are programs' */
    return 0;
}
static int clampv(set_id id, int v)
{
    const set_desc *d = &desc[id];
    if (d->type == ST_ENUM || d->type == ST_CHORD) {
        if ((id == SET_CPU_CLOCK || id == SET_CPU_MEASURED) && v < settings_first(id)) v = settings_first(id);   /* a clock above the cap comes down to it */
        if (v >= d->nlabels) v = d->nlabels - 1;
        return v;
    }
    if (v < d->min) v = d->min;
    if (v > d->max) v = d->max;
    return v;
}
void settings_set(set_id id, int v)
{
    if (id == SET_VIDEO_MODE) vm_ready();
    v = clampv(id, v);
    if (id == SET_VIDEO_MODE && v >= VMODE_IDR0) {   /* an IDR: what is kept, by its scale */
        int small = vm[v].csz == 0 || vm[v].csz == 2;
        if (vm[v].div != vm_want_div || small != vm_want_small) { vm_want_div = vm[v].div; vm_want_small = small; changed = 1; }
    }
    if (value[id] != v) { value[id] = v; changed = 1; }
}
void settings_step(set_id id, int dir)
{
    const set_desc *d = &desc[id]; int v = value[id];
    if (d->type == ST_ENUM || d->type == ST_CHORD) { int n = settings_choices(id), f = settings_first(id); v += dir; if (v < f) v = n - 1; if (v >= n) v = f; }
    else if (d->type == ST_BOOL) v = !v;
    else v += dir * d->step;
    settings_set(id, v);
}
const char *settings_text(set_id id, char *buf, int max)
{
    const set_desc *d = &desc[id]; int v = value[id];
    if (id == SET_VIDEO_MODE) vm_ready();
    if (d->type == ST_ENUM || d->type == ST_CHORD) return d->labels[clampv(id, v)];
    if (d->type == ST_BOOL) return v ? "on" : "off";
    if (id == SET_AUDIO_VOLUME) snprintf(buf, (size_t) max, "%d%%", v);
    else if (id == SET_VIDEO_BORDER) snprintf(buf, (size_t) max, "%d px", v);
    else snprintf(buf, (size_t) max, "%d", v);
    return buf;
}
static const char *file_text(set_id id, char *buf, int max)   /* what goes in the file: raw numbers, enum names */
{
    const set_desc *d = &desc[id];
    /* The resolution by its scale, whatever the machine is in now: a program's
     * 160x200 is a place you cannot easily steer out of after a power cycle,
     * and K/OS always comes back in an integer display resolution. */
    if (id == SET_VIDEO_MODE) { snprintf(buf, (size_t) max, "/%d%s", vm_want_div, vm_want_small ? " small" : ""); return buf; }
    if (id == SET_VIDEO_PALETTE) return pal_name;
    if (d->type == ST_ENUM || d->type == ST_CHORD || d->type == ST_BOOL) return settings_text(id, buf, max);
    snprintf(buf, (size_t) max, "%d", value[id]); return buf;
}
void settings_set_labels(set_id id, const char *const *labels, int n, int def)
{
    if (id < 0 || id >= SET_COUNT || !labels || n <= 0) return;
    desc[id].labels = labels; desc[id].nlabels = n;
    desc[id].def = def >= 0 && def < n ? def : 0;
    value[id] = desc[id].def;
}
void settings_defaults(void)
{
    for (int i = 0; i < SET_COUNT; i++) value[i] = desc[i].def;
    pal_name[0] = 0;
    vm_want_div = 4; vm_want_small = 0; if (vm_n) settings_video_rebuild();
    changed = 0;
}
int settings_changed(void) { return changed; }

const char *settings_key(set_id id) { return (id >= 0 && id < SET_COUNT) ? desc[id].key : ""; }
static int find_key(const char *k) { for (int i = 0; i < SET_COUNT; i++) if (!strcmp(desc[i].key, k)) return i; return -1; }
static int parse_value(set_id id, const char *v)
{
    const set_desc *d = &desc[id];
    if (id == SET_VIDEO_MODE) { vm_parse(v); return value[id]; }   /* resolved against this panel's list (settings_video_rebuild) */
    if (id == SET_VIDEO_PALETTE) { snprintf(pal_name, sizeof pal_name, "%s", v); return 0; }
    if (d->labels == smooth_names) {                  /* the names before 2026-09-14 */
        if (!strcasecmp(v, "sharp-fit")) return SMOOTH_INTEGER;
        if (!strcasecmp(v, "sharp") || !strcasecmp(v, "soft")) return SMOOTH_FIT;
    }
    if (d->type == ST_ENUM || d->type == ST_CHORD) { for (int i = 0; i < d->nlabels; i++) if (!strcasecmp(d->labels[i], v)) return i; return clampv(id, atoi(v)); }
    if (d->type == ST_BOOL) return (!strcasecmp(v, "on") || !strcasecmp(v, "true") || !strcasecmp(v, "yes") || atoi(v)) ? 1 : 0;
    return clampv(id, atoi(v));
}
static void trim(char *s) { size_t n = strlen(s); while (n && (s[n - 1] == '\n' || s[n - 1] == '\r' || s[n - 1] == ' ' || s[n - 1] == '\t')) s[--n] = 0; }
static int split(char *line, char **k, char **v)         /* "key = value" -> 1; comments and blanks -> 0 */
{
    char *p = line; while (*p == ' ' || *p == '\t') p++;
    if (!*p || *p == '#' || *p == ';') return 0;
    char *eq = strchr(p, '='); if (!eq) return 0;
    *eq = 0; *k = p; trim(p);
    p = eq + 1; while (*p == ' ' || *p == '\t') p++; *v = p; trim(p);
    return 1;
}
int settings_load(const char *path)
{
    FILE *f = fopen(path, "r"); char line[256]; int filever = 1;
    settings_defaults();
    if (!f) return -1;
    while (fgets(line, sizeof line, f)) {
        char *k, *v; char copy[256]; strcpy(copy, line);
        if (!split(copy, &k, &v)) continue;
        if (!strcmp(k, "version")) { filever = atoi(v); continue; }
        /* term.bands was video.statusbar until 2026-09-02, when its row moved
         * into the Terminal menu.  A renamed key is silently a lost setting --
         * every config that had the bands ON would have come back with them
         * off -- so the old name still loads.  It is not written back. */
        int id = !strcmp(k, "video.statusbar") ? (int)SET_VIDEO_STATUSBAR : find_key(k);
        if (id >= 0) value[id] = parse_value((set_id) id, v);
    }
    /* Version 1 -> 2 (2026-09-01) migrated audio.chip, which no longer
     * exists; the version line is kept so a future migration has a number
     * to compare against. */
    /* Version 2 -> 3 (2026-09-15): the menu moved from F7, a Commodore habit, to
     * F12, where most emulators keep theirs.  Every config wrote its menu key
     * down, so a default change alone would have moved nobody. */
    int migrated = 0;
    if (filever < 3 && value[SET_INPUT_MENU_KEY] == MENUKEY_F7) { value[SET_INPUT_MENU_KEY] = MENUKEY_F12; migrated = 1; }
    /* The register panel became a sidebar (2026-09-15, Doc): video.panel =
     * registers moves to the Sidebars setting, and the panel key stays off.
     * No version bump: the old key says all there is to know. */
    if (value[SET_VIDEO_PANEL] == PANEL_REGS) {
        const set_desc *d = &desc[SET_VIDEO_SIDEBARS];
        for (int i = 0; i < d->nlabels; i++) if (!strcasecmp(d->labels[i], "registers")) { value[SET_VIDEO_SIDEBARS] = i; break; }
        value[SET_VIDEO_PANEL] = PANEL_OFF; migrated = 1;
    }
    settings_video_rebuild();                     /* the resolution asked for, as this panel has it */
    /* Fixed since 2026-10-06, their rows gone from F12 (Doc: "scaling --
     * default is integer always; full screen -- on always"): whatever an older
     * file says.  Vertical sync keeps its value, unseen. */
    if (value[SET_VIDEO_SMOOTH] != SMOOTH_INTEGER || !value[SET_VIDEO_FULLSCREEN]) migrated = 1;
    value[SET_VIDEO_SMOOTH] = SMOOTH_INTEGER; value[SET_VIDEO_FULLSCREEN] = 1;
    fclose(f); changed = migrated;
    return 0;
}
int settings_save(const char *path)
{
    char *old[128]; int nold = 0, seen[SET_COUNT] = { 0 };
    FILE *f = fopen(path, "r"); char line[256];
    if (f) { while (nold < 128 && fgets(line, sizeof line, f)) { old[nold] = malloc(strlen(line) + 1); strcpy(old[nold++], line); } fclose(f); }
    f = fopen(path, "w"); if (!f) { for (int i = 0; i < nold; i++) free(old[i]); return -1; }
    if (!nold) fprintf(f, "# K4510 settings -- written by the F7 menu; edit freely, unknown keys are kept\nversion = " SETTINGS_VERSION_STR "\n");
    for (int i = 0; i < nold; i++) {                      /* the old lines, known keys rewritten in place */
        char *k, *v; char copy[256]; strcpy(copy, old[i]);
        int split_ok = split(copy, &k, &v);
        int id = split_ok ? find_key(k) : -1;
        if (id >= 0 && !seen[id]) { char b[32]; fprintf(f, "%s = %s\n", desc[id].key, file_text((set_id) id, b, sizeof b)); seen[id] = 1; }
        /* The version line is the one unknown key that is NOT passed through:
         * it has to be rewritten, or a migrated file would still say version 1
         * and be migrated again on the next boot -- undoing, every time, any
         * choice the person made after the first migration. */
        else if (split_ok && !strcmp(k, "version")) fputs("version = " SETTINGS_VERSION_STR "\n", f);
        else if (id < 0) fputs(old[i], f);
        free(old[i]);
    }
    for (int i = 0; i < SET_COUNT; i++) if (!seen[i]) { char b[32]; fprintf(f, "%s = %s\n", desc[i].key, file_text((set_id) i, b, sizeof b)); }
    fclose(f); changed = 0;
    return 0;
}
