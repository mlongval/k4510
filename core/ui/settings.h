/* The settings registry: every knob the machine's host offers, typed,
 * with a default, persisted to k4510.cfg (a key = value file beside fs/;
 * on the Pi, SD:/k4510/k4510.cfg). Adding a setting is one row in
 * settings.c's table, one id here, one menu row and one place that reads
 * the value -- that is the whole extension contract. No SDL here: the
 * host reads values and applies them (sdl/main.c, the Pi through the
 * same file). */
#ifndef K4510_SETTINGS_H
#define K4510_SETTINGS_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef enum {
    SET_VIDEO_BORDER,        /* INT  pixels of border around the picture */
    SET_VIDEO_BORDER_COLOUR, /* INT  palette index */
    SET_VIDEO_FONT,          /* ENUM the text chargen at $010000 */
    SET_VIDEO_MODE,          /* ENUM the machine's video mode: shown live, and the ROM performs a change */
    SET_VIDEO_MARGIN,        /* BOOL the one-cell gap at the top and the left (79 columns, not 80) */
    SET_VIDEO_STATUSBAR,     /* BOOL the status bands; keyed term.bands, and its row lives in the Terminal menu.
                              * The console becomes a scroll region between two bands the ROM draws.
                              * (Key renamed from video.statusbar 2026-09-02; the old name still loads.) */
    SET_VIDEO_SCANLINES,     /* ENUM a dark line between each of the machine's */
    SET_VIDEO_SMOOTH,        /* ENUM how the picture is scaled to the window */
    SET_VIDEO_FULLSCREEN,    /* BOOL desktop only */
    SET_VIDEO_VSYNC,         /* BOOL desktop only: present on the vertical blank instead of pacing by hand.
                              * OFF is the machine's own 60 Hz (see sdl/main.c); ON hands the pacing to the
                              * display, which costs frames on a host whose refresh is not 60. */
    SET_VIDEO_PLACE,         /* ENUM where the picture sits on a screen wider than 4:3: centre, left, right */
    SET_VIDEO_PANEL,         /* ENUM the side panel in the space that leaves: off, or the registers (sdl/panel.c) */
    SET_AUDIO_VOLUME,        /* INT  0-100 */
    SET_INPUT_RESET_CHORD,   /* CHORD */
    SET_INPUT_MENU_KEY,      /* ENUM which F-key opens the menu */
    SET_INPUT_MOUSE_GRAB,    /* BOOL a click on the picture confines the pointer to the window; F7 frees it */
    SET_INPUT_MOUSE_SHOW,    /* BOOL show the host mouse pointer over the picture (on: it does not vanish on the glass) */
    SET_SHELL_CPMCOM,        /* BOOL an unknown word may run a CP/M .COM */
    SET_SHELL_STARTUP,       /* BOOL run /STARTUP.BAT at power-on */
    SET_CPU_CLOCK,           /* ENUM the emulated CPU's clock: full 40.5 MHz, or less where the host cannot keep up */
    SET_CPU_AUTO,            /* BOOL measure the host at boot and set the clock from that (an explicit clock turns it off) */
    SET_CPU_MEASURED,        /* ENUM what the last measurement chose (not in the menu) */
    SET_CPU_HOST,            /* INT  the host the measurement was taken on; 0 = never (not in the menu) */
    /* The status bands.  Heights are INDEPENDENT (Doc, 2026-09-02) and reach
     * the guest as $D52D/$D52E; the ROM clamps them so the console keeps a
     * workable minimum whatever is asked for.  0 and 0 is the same thing as
     * turning the bands off, and the ROM treats it that way. */
    SET_TERM_BAND_TOP,       /* INT  rows in the top band (default 1) */
    SET_TERM_BAND_BOT,       /* INT  rows in the bottom band (default 2) */
    SET_TERM_CLOCK24,        /* BOOL 24-hour clock; off is 12-hour with AM/PM */
    SET_TERM_DATEFMT,        /* ENUM DD.MM.YYYY / YYYY-MM-DD / MM/DD/YYYY -- all ten cells wide, which is
                              * what lets the IRQ's clock painter stay a fixed-width digit poker */
    SET_INPUT_CAPS_CTRL,     /* BOOL Caps Lock is a Ctrl key (F7 -> Input); at the end so no index moves (Doc, 2026-09-12) */
    SET_INPUT_KBD_LAYOUT,    /* ENUM the machine's keyboard layout (F7 -> Input): "Host" follows the desktop / kernel keymap;
                              * any other is the emulator's own table (core/kbdmaps.h), at once, and on the K4510 Linux
                              * the consoles are set to match (k4510-keymap, also at every boot).  Doc, 2026-09-12 */
    SET_COUNT
} set_id;
typedef enum { ST_BOOL, ST_INT, ST_ENUM, ST_CHORD } set_type;
/* Fastest first.  The steps above 40.5 are multiples of it (x2 x3 x4 x5)
 * plus a round 60: the MEGA65 number was always a suggestion, and a host
 * that can do more should be allowed to.  Saved by NAME in k4510.cfg, so
 * this list may be reordered without stranding an existing config. */
enum { DATEFMT_DMY, DATEFMT_ISO, DATEFMT_MDY, DATEFMT_COUNT };
enum { CPUCLK_202_5, CPUCLK_162, CPUCLK_121_5, CPUCLK_81, CPUCLK_60,
       CPUCLK_40_5, CPUCLK_30, CPUCLK_20, CPUCLK_15, CPUCLK_10, CPUCLK_COUNT };
unsigned settings_cpu_hz(void);                /* the emulated clock, from SET_CPU_CLOCK */
unsigned settings_cpu_hz_of(int step);         /* the ladder by index, fastest first */
#define SF_LIVE     1        /* takes effect at once */
#define SF_RESTART  2        /* needs a power cycle */
typedef struct {
    const char *key;         /* "video.border" */
    const char *label;       /* what the menu shows */
    set_type type;
    int def, min, max, step;
    const char *const *labels; int nlabels;   /* ENUM / CHORD */
    unsigned flags;
} set_desc;
/* the font choices, in the ENUM's order */
/* The five built-ins, then the curated ZX Origins C64 chargens in data/fonts/zx/
 * (damieng.com/typography/zx-origins). Order is the menu order and MUST match
 * font_names[] (settings.c) and the paths[] in apply_font (sdl/main.c);
 * the non-native fonts are CP437 .bin baked by tools/mkcp437font.py. */
enum { FONT_KERNEL8, FONT_UNSCII, FONT_OPENROMS, FONT_PXLFONT, FONT_CHARGEN,
       FONT_ZX_BAUHAUS, FONT_ZX_BROADWAY, FONT_ZX_COMPUTER, FONT_ZX_CYBERWIRE,
       FONT_ZX_NLQ, FONT_ZX_BENGUIAT, FONT_ZX_CHICAGO, FONT_ZX_COURIER,
       FONT_ZX_EUROSTILE, FONT_ZX_OCRA, FONT_ZX_PRISTINE, FONT_ZX_ANVIL, FONT_COUNT };
/* video modes, in the ENUM's order -- the shell's MODE 0-4 */
enum { VMODE_640x480, VMODE_640x240, VMODE_320x240, VMODE_320x200, VMODE_160x200, VMODE_COUNT };
#define VMODE_MENU_MAX VMODE_320x240   /* the menu offers no less than this.  320x200 and 160x200 are
                                        * for games and for a language that wants the pixels -- 40x25
                                        * and 20x25 are not a shell -- so MODE 3 and MODE 4 reach them
                                        * and the menu still SHOWS them when the guest is in one, but
                                        * you cannot steer the machine into one from the menu. */
#define VMODE_SAVE_MAX VMODE_320x240   /* and nothing smaller is ever written to k4510.cfg */
/* scanline strengths, in the ENUM's order */
enum { SCAN_OFF, SCAN_LIGHT, SCAN_MEDIUM, SCAN_HEAVY, SCAN_COUNT };
/* scaling, in the ENUM's order */
enum { SMOOTH_SHARP, SMOOTH_SOFT, SMOOTH_SHARPFIT, SMOOTH_COUNT };
enum { PLACE_CENTRE, PLACE_LEFT, PLACE_RIGHT, PLACE_COUNT };
enum { PANEL_OFF, PANEL_REGS, PANEL_COUNT };
/* the reset chords, in the CHORD's order: modifier + PageUp ("Restore") */
enum { CHORD_SUPER_PGUP, CHORD_CTRL_PGUP, CHORD_ALT_PGUP, CHORD_CTRL_ALT_DEL, CHORD_COUNT };
/* the menu keys, in the ENUM's order */
enum { MENUKEY_F7, MENUKEY_F8, MENUKEY_F11, MENUKEY_PAUSE, MENUKEY_COUNT };

const set_desc *settings_desc(set_id id);
int         settings_choices(set_id id);          /* how many of an ENUM's labels the menu may offer */
int         settings_get(set_id id);
void        settings_set(set_id id, int v);       /* clamped / wrapped to the descriptor */
void        settings_label(set_id id, int idx, const char *text);  /* rename one ENUM choice: the host says what it actually found */
void        settings_step(set_id id, int dir);    /* +1 / -1: the next value (ENUMs wrap, INTs stop) */
const char *settings_text(set_id id, char *buf, int max);   /* the value as the menu prints it */
/* The key this id names.  desc[] is indexed by set_id, so the enum's order
 * and the table's order must agree, and NOTHING in the compiler checks that:
 * SET_AUDIO_CORE3 was added to the enum above SET_AUDIO_CHIP and below it in
 * the table, and the two silently swapped -- the Sound chip row set a boolean
 * and the machine stayed on reSID whatever the menu said.  test/uitest walks
 * the pairs now, which is why this accessor exists. */
const char *settings_key(set_id id);
void        settings_defaults(void);
int         settings_load(const char *path);      /* 0 ok, -1 no file (defaults stand) */
int         settings_save(const char *path);      /* rewrites the file; unknown keys and comments kept */
int         settings_changed(void);               /* since the last load/save */
#ifdef __cplusplus
}
#endif
#endif
