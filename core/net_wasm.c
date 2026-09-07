/* The N: device's platform half for the browser build: nothing is fitted.
 * A page cannot open raw TCP or UDP, and the HTTP the machine speaks (a
 * URL as a file name) would need a CORS proxy to be honest; so the device
 * answers "no network on this host" (status 6), exactly as a Pi without
 * an Ethernet adapter does.  See core/net_plat.h. */
#include "net_plat.h"
#include <stdlib.h>
#include <time.h>
int  plat_net_ready(void) { return 0; }
int  plat_udp_open(const char *host, int port) { (void) host; (void) port; return -1; }
int  plat_udp_send(int h, const void *buf, int n) { (void) h; (void) buf; (void) n; return -1; }
int  plat_udp_recv(int h, void *buf, int max, int timeout_ms) { (void) h; (void) buf; (void) max; (void) timeout_ms; return -1; }
void plat_udp_close(int h) { (void) h; }
int  plat_tcp_connect(const char *host, int port) { (void) host; (void) port; return -1; }
int  plat_tcp_send(int h, const void *buf, int n) { (void) h; (void) buf; (void) n; return -1; }
int  plat_tcp_recv(int h, void *buf, int max) { (void) h; (void) buf; (void) max; return -1; }
int  plat_tcp_avail(int h) { (void) h; return -1; }
void plat_tcp_close(int h) { (void) h; }
int  plat_http_fetch(const char *url, uint8_t **buf, uint32_t *len) { (void) url; *buf = NULL; *len = 0; return 1; }
unsigned plat_ticks(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (unsigned)(t.tv_sec * 1000u + t.tv_nsec / 1000000u); }
