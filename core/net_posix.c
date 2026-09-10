/* The network on a POSIX desktop: sockets for TCP and UDP, curl (a child,
 * no shell) for HTTP and HTTPS. */
#include "net_plat.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <netdb.h>
#include <poll.h>

int plat_net_ready(void) { return 1; }
unsigned plat_ticks(void) { struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts); return (unsigned)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000); }

static int connect_to(const char *host, int port, int socktype)
{
    struct addrinfo hints, *res, *ai; char ps[16]; int fd = -1;
    snprintf(ps, sizeof ps, "%d", port);
    memset(&hints, 0, sizeof hints); hints.ai_socktype = socktype;
    if (getaddrinfo(host, ps, &hints, &res)) return -1;
    for (ai = res; ai; ai = ai->ai_next) {
        fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (fd < 0) continue;
        if (!connect(fd, ai->ai_addr, ai->ai_addrlen)) break;
        close(fd); fd = -1;
    }
    freeaddrinfo(res);
    return fd;
}
int  plat_udp_open(const char *host, int port) { return connect_to(host, port, SOCK_DGRAM); }
int  plat_udp_send(int h, const void *buf, int n) { return (int) send(h, buf, (size_t) n, 0); }
int  plat_udp_recv(int h, void *buf, int max, int timeout_ms)
{
    struct pollfd pf = { h, POLLIN, 0 };
    int r = poll(&pf, 1, timeout_ms);
    if (r <= 0) return r < 0 ? -1 : 0;
    r = (int) recv(h, buf, (size_t) max, 0);
    return r < 0 ? -1 : r;
}
void plat_udp_close(int h) { close(h); }

int plat_tcp_connect(const char *host, int port)
{
    int fd = connect_to(host, port, SOCK_STREAM);
    if (fd >= 0) fcntl(fd, F_SETFL, O_NONBLOCK);
    return fd;
}
int plat_tcp_send(int h, const void *buf, int n)
{
    int done = 0;
    while (done < n) {
        ssize_t w = send(h, (const char *) buf + done, (size_t)(n - done), MSG_NOSIGNAL);
        if (w < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) { struct pollfd pf = { h, POLLOUT, 0 }; poll(&pf, 1, 1000); continue; }
        if (w <= 0) return -1;
        done += (int) w;
    }
    return done;
}
int plat_tcp_recv(int h, void *buf, int max)
{
    ssize_t r = recv(h, buf, (size_t) max, 0);
    if (r > 0) return (int) r;
    if (r == 0) return -1;
    return (errno == EAGAIN || errno == EWOULDBLOCK) ? 0 : -1;
}
int plat_tcp_avail(int h)
{
    int n = 0; struct pollfd pf = { h, POLLIN, 0 }; char b;
    if (ioctl(h, FIONREAD, &n) == 0 && n > 0) return n;
    if (poll(&pf, 1, 0) > 0 && recv(h, &b, 1, MSG_PEEK | MSG_DONTWAIT) == 0) return -1;
    return 0;
}
void plat_tcp_close(int h) { close(h); }

/* curl's whole output into memory, straight from a pipe */
int plat_http_fetch(const char *url, uint8_t **buf, uint32_t *len)
{
    int p[2]; pid_t pid; int st = 1; uint32_t cap = 65536, n = 0; uint8_t *b;
    if (pipe(p)) return 2;
    pid = fork();
    if (pid < 0) { close(p[0]); close(p[1]); return 2; }
    if (pid == 0) {
        int nul = open("/dev/null", O_RDWR);
        dup2(p[1], 1); close(p[0]); close(p[1]);
        if (nul >= 0) { dup2(nul, 0); dup2(nul, 2); if (nul > 2) close(nul); }
        /* sftp:// and scp:// need the host key accepted without a prompt (stdin
         * is /dev/null here); --insecure skips the check, as ssh's
         * StrictHostKeyChecking=no does.  HTTPS keeps its verification. */
        if (!strncasecmp(url, "sftp://", 7) || !strncasecmp(url, "scp://", 6))
            execlp("curl", "curl", "-sSL", "--max-time", "120", "--fail", "--insecure", "--", url, (char *) NULL);
        else
            execlp("curl", "curl", "-sSL", "--max-time", "120", "--fail", "--", url, (char *) NULL);
        _exit(127);
    }
    close(p[1]);
    if (!(b = malloc(cap))) { close(p[0]); waitpid(pid, &st, 0); return 2; }
    for (;;) {
        ssize_t r;
        if (n == cap) { uint8_t *nb; cap *= 2; if (!(nb = realloc(b, cap))) { free(b); close(p[0]); waitpid(pid, &st, 0); return 2; } b = nb; }
        r = read(p[0], b + n, cap - n);
        if (r <= 0) break;
        n += (uint32_t) r;
    }
    close(p[0]);
    waitpid(pid, &st, 0);
    if (!WIFEXITED(st) || WEXITSTATUS(st) != 0) { free(b);
        /* "not found" is 22 for HTTP (a 404), 19 for FTP (RETR failed) and 78
         * for FTP/SFTP (no such file): all map to status 1 so the shell falls
         * through to a local program of the same name; anything else is 2. */
        if (WIFEXITED(st)) { int c = WEXITSTATUS(st); if (c == 22 || c == 19 || c == 78) return 1; }
        return 2; }
    *buf = b; *len = n;
    return 0;
}
