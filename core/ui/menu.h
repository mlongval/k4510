/* The F7 menu: a framed window over the live (frozen, dimmed) screen, in
 * the style of the C64 Ultimate's. Declarative tree (static tables), a
 * small state machine, drawn into the overlay only when something
 * changed. Keys arrive as K4510 key codes (io.h) from kbd_push, which
 * hands the menu every key while it is open, so the desktop keyboard
 * and the Pi's C64 keyboard both drive it. The host: freezes the
 * machine while menu_is_open(), composites the overlay, performs the
 * actions the menu asks for (menu_take_action), and saves the settings
 * when the menu closes (menu_closed_pending). */
#ifndef K4510_MENU_H
#define K4510_MENU_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
enum { ACT_NONE, ACT_RESET, ACT_POWER_CYCLE, ACT_TUBE_STOP, ACT_QUIT, ACT_SHUTDOWN, ACT_SAVE_SLOT = 16, ACT_LOAD_SLOT = 32 };   /* + slot 0-3 */
#define MENU_SLOTS 4
enum { INFO_VERSION, INFO_ROM, INFO_FS, INFO_HOST, INFO_COUNT };
void menu_open(void);
void menu_close(void);
int  menu_is_open(void);
void menu_key(uint8_t code);                  /* a K4510 key code or ASCII */
void menu_mouse(int x, int y, int buttons, int wheel);   /* machine pixels; buttons bit0 left bit1 right; wheel +up/-down.
                                                           * Hover highlights a setting, click selects, right-click steps
                                                           * back, the wheel moves the cursor.  Draws the pointer itself. */
int  menu_take_action(void);                  /* ACT_*, once */
int  menu_closed_pending(void);               /* 1 once, after a close: the host saves the settings */
void menu_info(int row, const char *text);    /* the Info page's rows, from the host */
void menu_slot(int n, const char *text);      /* what a save-state slot holds ("empty", a date), from the host */
int  menu_draw(uint8_t *overlay);             /* 1 if it drew (the overlay changed) */
void menu_dirty(void);                        /* redraw next time: the cell grid changed under it */
int  menu_key_code(void);                     /* the K4510 key code that opens the menu (from the setting) */
void menu_set_shutdown(int available);        /* K4510x only: reveal "Shut down the computer" (see menu.c) */
#ifdef __cplusplus
}
#endif
#endif
