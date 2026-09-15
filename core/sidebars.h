/* core/sidebars.h -- the sidebars the machine has: the zips in
 * /SYSTEM/SIDEBARS (docs/SIDEBAR-FORMAT.md), read once at start.
 *
 * The directory is the list (docs/SIDEBARS-PLAN.md, step 3): the Sidebars
 * setting's choices are the zips found, by their file names (antfarm for
 * ANTFARM.ZIP), and each says in its SIDEBAR.INF which of the emulator's own
 * drawings it is (draw = builtin NAME).  Nothing else lists sidebars.  With no
 * zips -- a test, a disk without /SYSTEM -- the built-in names stand, in
 * SIDEBAR_* order, and everything still works. */
#ifndef K4510_SIDEBARS_H
#define K4510_SIDEBARS_H
typedef struct {
    char key[24];            /* "antfarm": the zip's name, lower case -- what k4510.cfg saves */
    char name[48];           /* "Ant farm" */
    char about[64];
    int builtin;             /* SIDEBAR_* (core/ui/settings.h) */
    int version;
    unsigned months;         /* bit m-1 for each month of its season; 0 = any time */
} sidebar_info;

/* Read the .ZIP files in FSROOT/SYSTEM/SIDEBARS and make them the Sidebars setting's
 * choices (before settings_load, so a saved name finds its place).  A zip
 * that cannot be read, or draws nothing this emulator knows, is left out.
 * Returns how many there are. */
int  sidebars_scan(const char *fsroot);
int  sidebars_count(void);
const sidebar_info *sidebars_info(int i);   /* NULL past the end */
int  sidebars_find(const char *key);        /* the choice with this key, -1 if none */
/* What to draw for the setting's value V: SIDEBAR_*, and SIDEBAR_BORDER for
 * anything it does not know. */
int  sidebars_builtin(int v);
#endif
