#ifndef K4510_HOSTPAGE_H
#define K4510_HOSTPAGE_H
void host_battery_poll(void);   /* $D53A, every ten seconds */
int  host_remote_login(void);   /* 1 while someone is logged in from elsewhere (checked every five seconds) */
void host_net_poll(void);       /* the bottom band's network, every ten seconds */
void host_keymap_apply(void);   /* before the frame loop and at each menu close */
void host_lid_apply(void);      /* the same */
void host_charge_apply(void);   /* the same */
void host_info_refresh(void);   /* F12 -> Host: name and addresses */
void host_net_setup(void);      /* "Wi-Fi / network setup": nmtui on a spare console */
void host_reap(void);           /* once a frame */
#endif
