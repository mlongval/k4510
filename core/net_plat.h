/* What the network needs from the platform underneath core/net.c.
 * Two implementations: core/net_posix.c (desktop: sockets, curl for HTTP)
 * and pi/net_pi.cpp (Circle, every call marshalled to core 0). Handles
 * are small non-negative integers; -1 is failure. Nothing here blocks the
 * machine for longer than one network round trip. */
#ifndef K4510_NET_PLAT_H
#define K4510_NET_PLAT_H
#include <stdint.h>
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
int  plat_net_ready(void);                                    /* 0 = not fitted, or no address yet */
int  plat_udp_open(const char *host, int port);               /* a "connected" UDP socket */
int  plat_udp_send(int h, const void *buf, int n);
int  plat_udp_recv(int h, void *buf, int max, int timeout_ms);/* >0 bytes, 0 timed out, -1 error */
void plat_udp_close(int h);
int  plat_tcp_connect(const char *host, int port);
int  plat_tcp_send(int h, const void *buf, int n);            /* all of it, or -1 */
int  plat_tcp_recv(int h, void *buf, int max);                /* >0 bytes, 0 nothing yet, -1 closed */
int  plat_tcp_avail(int h);                                   /* bytes waiting, -1 closed and drained */
void plat_tcp_close(int h);
/* the whole body of an http(s) URL; 0 ok, 1 not found, 2 error, 6 unsupported (https where there is no TLS) */
int  plat_http_fetch(const char *url, uint8_t **buf, uint32_t *len);
unsigned plat_ticks(void);                                    /* milliseconds */
/* Called every ~50 ms while a transfer waits, so the frontend can keep its
 * window alive; the machine itself waits, as on a disk.  NULL = nothing. */
extern void (*plat_net_wait_hook)(void);
#ifdef __cplusplus
}
#endif
#endif
