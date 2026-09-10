/* The network, three ways -- each borrowed from the machine that did it first.
 *
 * The Meatloaf rule (the C64's Meatloaf cartridge): a URL is a file name.
 * A name beginning http://, https:// or tnfs:// that reaches the filesystem
 * device ($D300) for reading -- LOAD, TYPE, CP, RUN, EhBASIC's LOAD, BBC
 * BASIC's LOAD on the Tube -- is fetched and served like a file. No ROM
 * change: the ROM passes names through untouched and the host fetches.
 *
 * TNFS (the Spectranet's file protocol, the one FujiNet and Meatloaf servers
 * speak; UDP, port 16384): a tnfs://host[:port]/path is also a DIRECTORY.
 * CD tnfs://host/path puts the machine's current directory on the server:
 * DIR lists it, a bare name loads from it (so the REXX rule runs programs
 * off the internet), CD .. climbs it, CD - comes home. The server is
 * read-only from here.
 *
 * The N: device (FujiNet's network device, as the Atari and Apple saw it),
 * at $D900, for programs that want a live connection: four channels, each
 * a URL opened for reading and writing.
 *   $D900 W: command          $D901 R: status (0 ok, 1 not found / refused, 2 i/o error,
 *                                          3 bad command, 4 closed by the peer, 5 name too long, 6 not fitted)
 *   $D902 RW: channel 0-3     $D904-$D907 NAMEPTR 28-bit -> URL, NUL-terminated
 *   $D908-$D90B ADDR 28-bit   $D90C-$D90F LEN  bytes requested; updated to bytes done
 *   $D910-$D913 SIZE          after OPEN: the content length, $FFFFFFFF if unknown; after STATUS: bytes waiting
 *   commands: 1 OPEN  (tcp://host:port a connection; http://, https://, tnfs:// a file, fetched whole, then READ)
 *             2 READ  (up to LEN bytes to ADDR, what has arrived so far; LEN = done, 0 = nothing yet)
 *             3 WRITE (LEN bytes from ADDR; tcp only)
 *             4 CLOSE
 *             5 STATUS (SIZE = bytes waiting; status 4 once the peer has closed and the bytes are gone)
 *             6 GET   (the Meatloaf rule for programs: fetch the whole URL into ADDR, at most LEN; LEN = done, SIZE = total)
 * Reads never block the machine: a program polls, as it would a UART.
 * Underneath: core/net_plat.h -- sockets and curl on the desktop, Circle's
 * stack on the Pi (no TLS there: https answers 6). */
#ifndef K4510_NET_H
#define K4510_NET_H
#include <stdint.h>
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
int     net_is_url(const char *name);   /* http/https/tnfs/ftp/sftp, any case; N: prefix and user:pass@ accepted */
int     net_fetch(const char *url, uint8_t **buf, uint32_t *len);   /* the whole file into memory (caller frees); 0 ok, 1 not found, 2 error, 6 not fitted */
typedef struct { char name[64]; uint32_t size; int isdir; } net_dirent;
int     net_listdir(const char *url, net_dirent **ents, int *n);/* a tnfs:// directory, sorted (caller frees); status as above */
int     net_isdir(const char *url);                             /* 1 a directory, 0 not, -1 unreachable */
void    net_url_join(char *out, size_t max, const char *base, const char *name);   /* base URL + relative name, with . and .. */
void    net_reset(void);
uint8_t net_read(uint8_t reg);
void    net_write(uint8_t reg, uint8_t v);
#ifdef __cplusplus
}
#endif
#endif
