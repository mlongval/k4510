/* core/sidebars.h -- the sidebars the machine has: the zips in
 * /SYSTEM/SIDEBARS (docs/SIDEBAR-FORMAT.md), read once at start.
 *
 * The directory is the list (docs/SIDEBARS-PLAN.md, step 3): the Sidebars
 * setting's choices are the zips found, by their file names (antfarm for
 * ANTFARM.ZIP), and each says in its SIDEBAR.INF which of the emulator's own
 * drawings it is (draw = builtin NAME).  Nothing else lists sidebars.  With no
 * zips -- a test, a disk without /SYSTEM -- the built-in names stand, in
 * SIDEBAR_* order, and everything still works.
 *
 * Beside the zips, what changes (step 5), all plain files:
 *     /SYSTEM/SIDEBARS/SIDEBARS.CFG      right = , change = , seasons =
 *     /SYSTEM/SIDEBARS/KEY/OPTIONS.CFG   a sidebar's own: speed, and its others
 *     /SYSTEM/SIDEBARS/KEY/STATE.DAT     what it keeps across a power cycle
 * The first two are made when a sidebar is first shown -- OPTIONS.CFG is the
 * zip's own copy -- and read again whenever they change. */
#ifndef K4510_SIDEBARS_H
#define K4510_SIDEBARS_H
#include <stddef.h>
#include <stdint.h>
typedef struct {
    char key[24];            /* "antfarm": the zip's name, lower case -- what k4510.cfg saves */
    char file[40];           /* "ANTFARM.ZIP", as it is on the disk */
    char name[48];           /* "Ant farm" */
    char about[64];
    int builtin;             /* SIDEBAR_* (core/ui/settings.h) */
    int version;
    unsigned months;         /* bit m-1 for each month of its season; 0 = any time */
    char game[16];           /* game = NAME: a GAMEBAR, that program's side art (docs/GAMEBARS.md); "" for the rest */
} sidebar_info;

/* Read the .ZIP files in FSROOT/SYSTEM/SIDEBARS and make them the Sidebars
 * setting's choices (before settings_load, so a saved name finds its place).
 * A zip that cannot be read, or draws nothing this emulator knows, is left out.
 * Returns how many there are. */
int  sidebars_scan(const char *fsroot);
int  sidebars_count(void);
const sidebar_info *sidebars_info(int i);   /* NULL past the end */
int  sidebars_find(const char *key);        /* the choice with this key, -1 if none */
/* What to draw for the setting's value V: SIDEBAR_*, and SIDEBAR_BORDER for
 * anything it does not know. */
int  sidebars_builtin(int v);

/* Sidebar I's folder and OPTIONS.CFG (and SIDEBARS.CFG), made the first time,
 * and its options read -- again only if the file changed.  0 ok. */
int  sidebars_prepare(int i);
/* Once a second or so: read again any OPTIONS.CFG or SIDEBARS.CFG that
 * changed.  1 when one did. */
int  sidebars_poll(void);
double      sidebars_speed(int i);                   /* its speed option, 0.25 to 4; 1 when not given */
const char *sidebars_opt(int i, const char *key);    /* one of its options, NULL when not given */
const char *sidebars_cfg(const char *key);           /* SIDEBARS.CFG's */
long        sidebars_seconds(const char *s);         /* "10m" 600, "1h", "1d", "90" seconds; never, real, "" 0 */
/* Which choice a side shows, for the setting's value V: V itself, or on the
 * right a sidebar SIDEBARS.CFG names, or -- change = 10m and so on -- the
 * one whose turn it is at NOW (seconds), those out of season skipped when
 * seasons = on.  A register panel is never changed away from. */
int  sidebars_shown(int v, int side, long now, int month);
void sidebars_options_path(int i, char *buf, size_t n);   /* as the machine sees it: /SYSTEM/SIDEBARS/KEY/OPTIONS.CFG */
/* STATE.DAT: what sidebar I keeps across a power cycle.  Read gives a new
 * buffer (the caller frees), or NULL -- none, or one written by another
 * version, which is set aside as STATE.OLD and not read again. */
uint8_t *sidebars_state_read(int i, size_t *len);
int      sidebars_state_write(int i, const uint8_t *buf, size_t len);   /* by way of STATE.NEW, renamed over */
#endif
