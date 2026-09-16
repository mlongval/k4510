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
    SET_VIDEO_MODE,          /* ENUM the machine's video mode: shown live, and the ROM performs a change */
    SET_VIDEO_STATUSBAR,     /* BOOL the status bands; keyed term.bands, and its row lives in the Terminal menu.
                              * The console becomes a scroll region between two bands the ROM draws.
                              * (Key renamed from video.statusbar 2026-09-02; the old name still loads.) */
    SET_VIDEO_SMOOTH,        /* ENUM how the picture is scaled to the window */
    SET_VIDEO_FULLSCREEN,    /* BOOL desktop only */
    SET_VIDEO_VSYNC,         /* BOOL desktop only: present on the vertical blank instead of pacing by hand.
                              * OFF is the machine's own 60 Hz (see sdl/main.c); ON hands the pacing to the
                              * display, which costs frames on a host whose refresh is not 60. */
    SET_VIDEO_PLACE,         /* ENUM where the picture sits on a screen wider than 4:3: centre, left, right */
    SET_VIDEO_PANEL,         /* ENUM the side panel -- since 2026-09-15 only read, to move an old config's
                              * registers to the Sidebars setting, where the register panel is now a choice */
    SET_VIDEO_SIDEBARS,      /* ENUM what fills the space beside the picture: its choices are the zips in
                              * /SYSTEM/SIDEBARS (core/sidebars.c), saved by name */
    SET_AUDIO_VOLUME,        /* INT  0-100 */
    SET_INPUT_RESET_CHORD,   /* CHORD */
    SET_INPUT_MENU_KEY,      /* ENUM which F-key opens the menu */
    SET_INPUT_MOUSE_GRAB,    /* BOOL a click on the picture confines the pointer to the window; F12 frees it */
    SET_INPUT_MOUSE_SHOW,    /* BOOL show the host mouse pointer over the picture (on: it does not vanish on the glass) */
    SET_SHELL_CPMCOM,        /* BOOL an unknown word may run a CP/M .COM */
    SET_SHELL_STARTUP,       /* BOOL run /STARTUP.BAT at power-on */
    SET_CPU_CLOCK,           /* ENUM the emulated CPU's clock: full 40.5 MHz, or less where the host cannot keep up */
    SET_CPU_AUTO,            /* BOOL measure the host at boot and set the clock from that (an explicit clock turns it off) */
    SET_CPU_MEASURED,        /* ENUM what the last measurement chose (not in the menu) */
    SET_CPU_HOST,            /* INT  the host the measurement was taken on; 0 = never (not in the menu) */
    /* the status bands are one row at the top and one at the bottom, or off
     * (Doc, 2026-09-14): their heights are no longer settings */
    SET_TERM_CLOCK24,        /* BOOL 24-hour clock; off is 12-hour with AM/PM */
    SET_TERM_DATEFMT,        /* ENUM DD.MM.YYYY / YYYY-MM-DD / MM/DD/YYYY -- all ten cells wide, which is
                              * what lets the IRQ's clock painter stay a fixed-width digit poker */
    SET_INPUT_CAPS_CTRL,     /* BOOL Caps Lock is a Ctrl key (F12 -> Input); at the end so no index moves (Doc, 2026-09-12) */
    SET_INPUT_KBD_LAYOUT,    /* ENUM the machine's keyboard layout (F12 -> Input): "Host" follows the desktop / kernel keymap;
                              * any other is the emulator's own table (core/kbdmaps.h), at once, and on the K4510 Linux
                              * the consoles are set to match (k4510-keymap, also at every boot).  Doc, 2026-09-12 */
    SET_HOST_LID,            /* ENUM F12 -> Host -> Lid closed: keep running (the default: an emulator holds logind's
                              * lid lock) or suspend (it lets go, and logind suspends).  K4510 Linux only.  Doc, 2026-09-14 */
    SET_INPUT_KEYPIPE,       /* ENUM F12 -> Input -> Key pipe: off / on / on, shown -- keys typed from outside through the
                              * emulator's KEYS pipe (tools/k4510-type), and whether each is echoed on the glass for a
                              * few seconds so nobody types into the machine unseen.  Doc, 2026-09-14 */
    SET_TEXT_CODEPAGE,       /* ENUM strict CP437 (the default) or the K4510 page: F12 -> Terminal -> Code page, or
                              * CODEPAGE (JIM $DA17, which the frontend follows and saves).  Doc, 2026-09-15 */
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
/* The fastest clock the machine is allowed, for now (Doc, 2026-09-14: "lets
 * artificially (by hiding the higher options) limit the cpu speed to 60mhz
 * for now").  The steps above it stay in the ladder and in k4510.cfg's
 * vocabulary; the menu does not offer them, a saved or measured clock above
 * is brought down to it, and the machine sees a ladder that starts here
 * (SYS+$23/$27: SETUP and BENCH sweep from it).  CPUCLK_202_5 lifts it. */
#define CPUCLK_FASTEST CPUCLK_60
int settings_first(set_id id);                 /* the first choice the menu offers: 0, or CPUCLK_FASTEST for the clock */
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
/* One screen font since 2026-09-14 (Doc: "pick one font and jettison all the
 * rest"): unscii, 8x16 at 640x480 and 8x8 in the 240-line modes.  The host
 * loads both (sdl/main.c); there is no setting. */
/* video modes, in the ENUM's order -- the shell's MODE 0-4 */
/* hd-modes (2026-09-14): the HD family after the three classic shells, so the
 * menu's choices stay one run.  The order is the menu's, not the MODE number:
 * vmode_number[] maps (0 1 2 5 6 7 3 4). */
enum { VMODE_640x480, VMODE_640x480_60, VMODE_640x240, VMODE_320x240, VMODE_1440x1080, VMODE_720x540, VMODE_360x270,
       VMODE_320x200, VMODE_160x200, VMODE_COUNT };
/* 640x480 twice: the same screen in 8x16 cells (80x30, the default since the
 * one font of 2026-09-14) and in 8x8 (80x60, what it was before).  Both are
 * the ROM's MODE 0; the rows are SYSOPT_ROWS60 going out, and layer 0's cell
 * bit coming back, so `MODE 0 60` typed at the prompt is noticed and saved.
 * Doc, 2026-09-15: "can we have both ... in the menu". */
extern const unsigned char vmode_number[VMODE_COUNT];
#define VMODE_MENU_MAX VMODE_360x270   /* the menu offers no less than this.  320x200 and 160x200 are
                                        * for games and for a language that wants the pixels -- 40x25
                                        * and 20x25 are not a shell -- so MODE 3 and MODE 4 reach them
                                        * and the menu still SHOWS them when the guest is in one, but
                                        * you cannot steer the machine into one from the menu. */
#define VMODE_SAVE_MAX VMODE_360x270   /* and nothing past it is ever written to k4510.cfg: */
#define VMODE_SAVE_TO  VMODE_320x240   /* a game mode is saved as this */
/* (scanlines, a dark line between each of the machine's, went 2026-09-14 --
 * Doc: "a nice idea that has limited only nostalgic use") */
/* scaling, in the ENUM's order, hard pixels both (Doc, 2026-09-14: "only 2
 * modes, 1: Integer or 2: Fit to display"): a whole-number multiple, every
 * machine pixel the same size on the glass; or as large as the window takes.
 * The old names still load: sharp-fit is Integer, sharp and soft are Fit. */
enum { SMOOTH_INTEGER, SMOOTH_FIT, SMOOTH_COUNT };
enum { PLACE_CENTRE, PLACE_LEFT, PLACE_RIGHT, PLACE_COUNT };
enum { PANEL_OFF, PANEL_REGS, PANEL_COUNT };
/* the sidebars the emulator draws itself (Doc's brainshot, 2026-09-14): what
 * a zip in /SYSTEM/SIDEBARS names with draw = builtin NAME (core/sidebars.c).
 * The setting's choices are the zips; with none, these, in this order. */
enum { SIDEBAR_BORDER, SIDEBAR_GRADIENT, SIDEBAR_KNOT,
       SIDEBAR_REGISTERS,   /* the side panel, a sidebar since 2026-09-15 (Doc); before the scenes, so their numbers stay */
       SIDEBAR_HALLOWEEN, SIDEBAR_CHRISTMAS, SIDEBAR_SPACE, SIDEBAR_RIVER, SIDEBAR_DREAMFALL, SIDEBAR_TETRIS, SIDEBAR_ANTFARM,   /* sdl/savers.c, in SAVER_* order */
       SIDEBAR_COUNT };
/* the reset chords, in the CHORD's order: modifier + PageUp ("Restore") */
enum { CHORD_SUPER_PGUP, CHORD_CTRL_PGUP, CHORD_ALT_PGUP, CHORD_CTRL_ALT_DEL, CHORD_COUNT };
/* the menu keys, in the ENUM's order */
enum { MENUKEY_F7, MENUKEY_F8, MENUKEY_F11, MENUKEY_PAUSE, MENUKEY_F12, MENUKEY_COUNT };   /* F12 the default since 2026-09-15 */
enum { PAGE_CP437, PAGE_K4510, PAGE_COUNT };

const set_desc *settings_desc(set_id id);
int         settings_choices(set_id id);          /* how many of an ENUM's labels the menu may offer */
int         settings_get(set_id id);
void        settings_set(set_id id, int v);       /* clamped / wrapped to the descriptor */
void        settings_step(set_id id, int dir);    /* +1 / -1: the next value (ENUMs wrap, INTs stop) */
const char *settings_text(set_id id, char *buf, int max);   /* the value as the menu prints it */
/* An ENUM whose choices are only known at run time (the sidebars: whatever
 * zips there are).  Before settings_load, so a saved name is found. */
void        settings_set_labels(set_id id, const char *const *labels, int n, int def);
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
