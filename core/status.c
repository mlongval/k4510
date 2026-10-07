/* What the host knows of itself, for the machine to print: STATUS's report
 * and MOUNT's account of where the machine's own disk is.  Linux-specific
 * (/proc, statvfs, getifaddrs, nmcli) and nothing to do with the files
 * themselves, which is why it is not in hostfs.c. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/statvfs.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include "io.h"
#include "io_int.h"
#include "mem.h"
#include "vicky.h"

/* Where the machine's disk REALLY is, for MOUNT with nothing after it.
 *
 * Doc, 2026-09-17: "I would like the mount command on k/os to show the k4510
 * mounts (just the parts that get the k4510 up) currently if no mounts are
 * mounted it just says 'no mounts'."  Which was true and useless: the most
 * interesting thing about this machine's storage -- that the system is in
 * RAM and what you save is on a disk, and which disk -- was invisible from
 * inside it.  Only the host can know, so the storage device says
 * (FS_SYSMOUNTS) and the ROM prints.
 *
 * On a K4510 Linux fs_root sits on an overlay: the lower layers are the
 * system's squashfs files (in RAM when the kernel was given `toram`), the
 * upper is wherever saved files go -- the persistence partition, or a tmpfs
 * when there is none.  Anywhere else it is a plain directory, and says so.
 * Read from /proc, so it is what IS, not what a config file hoped for. */
static int mnt_for(const char *path, char *src, size_t smax, char *type, size_t tmax, char *opts, size_t omax)
{
    FILE *f = fopen("/proc/self/mounts", "r"); char line[4096], s[512], m[512], t[64], o[3072]; size_t best = 0; int hit = 0;
    if (!f) return 0;
    while (fgets(line, sizeof line, f)) {
        size_t l;
        if (sscanf(line, "%511s %511s %63s %3071s", s, m, t, o) != 4) continue;
        l = strlen(m);
        if (strncmp(path, m, l) || (l > 1 && path[l] && path[l] != '/')) continue;
        if (l < best) continue;
        best = l; hit = 1;
        snprintf(src, smax, "%s", s); snprintf(type, tmax, "%s", t); if (opts) snprintf(opts, omax, "%s", o);
    }
    fclose(f);
    return hit;
}
static const char *dev_label(const char *dev, char *out, size_t max)   /* "K4510 (nvme0n1p4)", or just the device */
{
    DIR *d = opendir("/dev/disk/by-label"); struct dirent *e; const char *base = strrchr(dev, '/'); char *rd = realpath(dev, NULL);
    base = base ? base + 1 : dev;
    snprintf(out, max, "%s", base);
    if (d) {
        while (rd && (e = readdir(d))) {
            char p[600], *r; if (e->d_name[0] == '.') continue;
            snprintf(p, sizeof p, "/dev/disk/by-label/%.300s", e->d_name);
            if ((r = realpath(p, NULL))) { int same = !strcmp(r, rd); free(r); if (same) { snprintf(out, max, "%.40s (%.40s)", e->d_name, base); break; } }
        }
        closedir(d);
    }
    free(rd);
    return out;
}
static void size_words(double bytes, char *out, size_t max)
{
    if (bytes >= 10.0 * (1u << 30)) snprintf(out, max, "%.0f GB", bytes / (1u << 30));
    else if (bytes >= (1u << 30)) snprintf(out, max, "%.1f GB", bytes / (1u << 30));
    else snprintf(out, max, "%.0f MB", bytes / (1u << 20));
}
int status_mount_row(int idx, char *b, size_t max)
{
    char src[512], type[64], opts[3072], sz[32], lab[120]; struct statvfs sv; char *root = realpath(fs_root, NULL);
    int n = 0, ok = root && mnt_for(root, src, sizeof src, type, sizeof type, opts, sizeof opts);
    b[0] = 0;
    if (!ok) { if (idx == 0) snprintf(b, max, "/        a directory on the host: %.100s", root ? root : fs_root); free(root); return idx == 0; }
    if (strcmp(type, "overlay")) {            /* an ordinary computer: one row */
        if (idx == 0) { sz[0] = 0; if (!statvfs(root, &sv)) size_words((double) sv.f_bavail * sv.f_frsize, sz, sizeof sz);
                        snprintf(b, max, "/        a directory on the host computer (%s, %s free)", type, sz); }
        if (idx == 1) { size_t l = strlen(root); snprintf(b, max, "         %s%s", l > 68 ? "..." : "", l > 68 ? root + l - 65 : root); }   /* its own line: a path and the rest do not fit in 80 columns */
        free(root); return idx <= 1;
    }
    /* row 0: the system */
    if (idx == n++) {
        FILE *c = fopen("/proc/cmdline", "r"); char cl[2048] = "", *m; int toram; char from[160] = "";
        if (c) { if (!fgets(cl, sizeof cl, c)) cl[0] = 0; fclose(c); }
        toram = strstr(cl, " toram") != NULL;
        if ((m = strstr(cl, "live-media=/dev/"))) { char dev[128]; sscanf(m + 11, "%127s", dev); snprintf(from, sizeof from, " from %s", dev_label(dev, lab, sizeof lab)); }
        sz[0] = 0; if (!statvfs("/run/live/medium", &sv)) size_words((double)(sv.f_blocks - sv.f_bfree) * sv.f_frsize, sz, sizeof sz);
        snprintf(b, max, toram ? "/        the system: in RAM, loaded%s%s%s" : "/        the system: read%s%s%s", from, sz[0] ? ", " : "", sz);   /* under 76 columns with a long label: STATUS indents it by two */
    }
    /* row 1: what is saved -- the overlay's upper directory, and what THAT is on */
    { char *u = strstr(opts, "upperdir="), up[512] = "", usrc[512], utype[64]; int disk;
      if (u) { sscanf(u + 9, "%511[^,]", up); }
      disk = up[0] && mnt_for(up, usrc, sizeof usrc, utype, sizeof utype, NULL, 0) && !strncmp(usrc, "/dev/", 5);
      if (idx == n++) {
          if (disk) { sz[0] = 0; if (!statvfs(up, &sv)) size_words((double) sv.f_bavail * sv.f_frsize, sz, sizeof sz);
                      snprintf(b, max, "/        what you save: on disk, %s, %s free", dev_label(usrc, lab, sizeof lab), sz); }
          else snprintf(b, max, "/        what you save: in RAM only -- gone at power-off (no persistence)");
      }
      if (disk && idx == n++) snprintf(b, max, "/DISK    on disk only: never loaded at boot (see /DISK/README.TXT)");
    }
    free(root);
    return b[0] != 0;
}
/* STATUS: the whole machine at a glance.
 *
 * Doc, 2026-09-17: "a command that gives me a birds eye view of the state of
 * the K4510: memory usage ... same for disk, current resolution, current
 * network state, mounted filesystems, any other info you feel important".
 * Nearly all of that is the HOST's to know, so the report is made here, whole,
 * when line 0 is asked for, and handed over a line at a time; STATUS
 * (demo/status.c) only prints.  Lines are kept under 78 columns.
 *
 * "Memory in use" needs saying what it means on a machine with no allocator:
 * it is how much of the 256 MB holds anything at all -- 64 KB blocks with a
 * non-zero byte in them.  A cleared block counts as free, which is what a
 * program looking for room would want to know. */
static char sysinfo[40][96]; static int sysinfo_n;
static void si(const char *fmt, ...) { va_list ap; if (sysinfo_n >= 40) return; va_start(ap, fmt); vsnprintf(sysinfo[sysinfo_n++], sizeof sysinfo[0], fmt, ap); va_end(ap); }
static long meminfo_kb(const char *key)
{
    FILE *f = fopen("/proc/meminfo", "r"); char l[160]; long v = -1; size_t n = strlen(key);
    if (!f) return -1;
    while (fgets(l, sizeof l, f)) if (!strncmp(l, key, n) && l[n] == ':') { v = atol(l + n + 1); break; }
    fclose(f); return v;
}
static void sysinfo_build(void)
{
    static const char *const si_tube[] = { "", "BBC BASIC", "", "CP/M", "Linux", "the chess engine" };
    char a[64], b2[64], row[160]; int sc, sr; struct statvfs sv;
    sysinfo_n = 0;
    screen_geom(&sc, &sr);
    si("THE MACHINE   K/OS %s   45GS10 at %u.%u MHz", sys_version, sys_cpu_khz / 1000, sys_cpu_khz % 1000 / 100);
    si("  display     %dx%d, text %d columns by %d rows", vicky_glass_w(), vicky_glass_h(), sc, sr);
    { int k = io_tube_kind(); const char *t = k ? (k == 6 ? "DOOM" : k >= 1 && k <= 5 ? si_tube[k] : "a program") : "idle";
      if (io_battery == 0xFF) si("  Tube        %s", t);
      else si("  Tube        %s          battery %d%%%s", t, io_battery & 0x7F, io_battery & 0x80 ? ", on mains" : ""); }
    { unsigned used = 0; for (uint32_t blk = 0; blk < K4510_PHYS_SIZE; blk += 0x10000) { const uint8_t *p = k4510_ram + blk; size_t k; for (k = 0; k < 0x10000 && !p[k]; k += 8) ; if (k < 0x10000) used++; }
      si("MEMORY        256 MB: %u.%u MB hold something, %u MB are clear", used / 16, used % 16 * 10 / 16, 256 - (used + 15) / 16);
      si("  set aside   ROM 152 KB at $FFF0000; screen $30000; bitmap 384 KB $200000;");
      si("              sprites $260000; fonts $10000; programs load at $6000"); }
    { long tot = meminfo_kb("MemTotal"), av = meminfo_kb("MemAvailable"); long rss = 0; FILE *f = fopen("/proc/self/statm", "r");
      if (f) { long x, r; if (fscanf(f, "%ld %ld", &x, &r) == 2) rss = r * (sysconf(_SC_PAGESIZE) / 1024); fclose(f); }
      if (tot > 0) { size_words((double) tot * 1024, a, sizeof a); size_words((double) av * 1024, b2, sizeof b2);
                     si("THE HOST      Linux: %s RAM, %s available; the emulator has %ld MB", a, b2, rss / 1024); }
      if (!statvfs("/run/live/medium", &sv) && sv.f_blocks) { size_words((double)(sv.f_blocks - sv.f_bfree) * sv.f_frsize, a, sizeof a); si("  in RAM      the system image, %s, copied there when it started", a); }
      { FILE *u = fopen("/proc/uptime", "r"); double up = 0; if (u) { if (fscanf(u, "%lf", &up) != 1) up = 0; fclose(u); }
        if (up >= 172800) si("  up          %d days", (int)(up / 86400));
        else if (up > 0) si("  up          %d h %02d min", (int)(up / 3600), (int)(up / 60) % 60); } }
    si("DISK");
    for (int k = 0; k < 4 && status_mount_row(k, row, sizeof row); k++) si("  %.76s", row);
    { char *root = realpath(fs_root, NULL); if (root && !statvfs(root, &sv)) { size_words((double) sv.f_blocks * sv.f_frsize, a, sizeof a); size_words((double) sv.f_bavail * sv.f_frsize, b2, sizeof b2); si("  room        %s free of %s, where your files go", b2, a); } free(root); }
    if (!fs_mount_count()) si("  mounts      none of your own");
    for (int k = 0; k < fs_mount_count() && k < 4; k++) si("  mounted     /%.30s  %.36s", fs_mount_at(k), fs_mount_src(k));
    si("NETWORK");
    { struct ifaddrs *ifa, *p; int any = 0;
      if (!getifaddrs(&ifa)) {
          for (p = ifa; p; p = p->ifa_next) {
              char ip[64];
              if (!p->ifa_addr || p->ifa_addr->sa_family != AF_INET || (p->ifa_flags & IFF_LOOPBACK) || !(p->ifa_flags & IFF_UP)) continue;
              if (!strncmp(p->ifa_name, "br-", 3) || !strncmp(p->ifa_name, "docker", 6) || !strncmp(p->ifa_name, "virbr", 5) || !strncmp(p->ifa_name, "veth", 4)) continue;   /* a desktop's container plumbing is not the machine's network */
              if (!inet_ntop(AF_INET, &((struct sockaddr_in *) p->ifa_addr)->sin_addr, ip, sizeof ip)) continue;
              si("  %-11.11s %s%s", p->ifa_name, ip, !strncmp(p->ifa_name, "tailscale", 9) ? "   (the tailnet)" : !strncmp(p->ifa_name, "wl", 2) ? "   (Wi-Fi)" : ""); any = 1;
          }
          freeifaddrs(ifa);
      }
      if (!any) si("  no network: nothing but the loopback is up");
      { FILE *n = popen("nmcli -t -f active,ssid,signal dev wifi 2>/dev/null", "r"); char l[160];      /* the network's NAME and strength; never its password */
        if (n) { while (fgets(l, sizeof l, n)) if (!strncmp(l, "yes:", 4)) { char *sig = strrchr(l, ':'); if (sig) { *sig++ = 0; si("  Wi-Fi       \"%.40s\", signal %d%%", l + 4, atoi(sig)); } break; } pclose(n); } } }
}
const char *status_row(int idx)
{
    if (idx == 0) sysinfo_build();
    return idx >= 0 && idx < sysinfo_n ? sysinfo[idx] : NULL;
}
