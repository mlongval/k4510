/* The frontend's side of the host: F12 -> Host, the battery and the network
 * for the bands, the keyboard layout and lid switch on the K4510 Linux.
 * Moved out of sdl/main.c 2026-10-07; nothing here touches SDL but its clock. */
#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <signal.h>
#include <sys/wait.h>
#include <ifaddrs.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "../core/io.h"
#include "../core/ui/settings.h"
#include "../core/ui/menu.h"
#include "hostpage.h"
/* ---- the Host page (F12 -> Host, the K4510 Linux only) ------------------------
 * Name, the first real IPv4 address and the tailnet address, read from the
 * host each time the menu opens (Wi-Fi may have just come up).  "Wi-Fi /
 * network setup" runs nmtui on a spare console the way tekplay runs tek40xx:
 * the emulator keeps running and holds the picture, the VT switches away and
 * comes back when nmtui exits (openvt -s -w); the child is reaped each frame
 * so the machine never blocks. */
static pid_t host_child;
/* The host's battery, for the status band ($D53A): every ten seconds, from
 * /sys/class/power_supply -- any Linux laptop, the K4510 Linux or a desktop.
 * % in bits 0-6, bit 7 on AC or charging, $FF with no battery.  K4510_BATTERY
 * ("52", "52+") stands in for one, for a headless test.  Doc, 2026-09-12.
 *
 * And the time it has left (Doc, 2026-10-07: "the estimated time remaining"):
 * charge over current -- or energy over power, as some batteries say it --
 * to empty while it discharges, to full while it charges, nothing when it
 * does neither (a charge threshold holds it).  The current jumps with what
 * the machine is doing, so it is averaged over the polls, about a minute and
 * a half deep, and starts over when the battery changes direction.
 * K4510_BATTERY "52,95" adds 95 minutes. */
static int batt_min = -1;                        /* the estimate, minutes; -1 none */
/* F12 -> Host -> Charge to 100% once (Doc, 2026-10-07: "charge to 100% once
 * then go back to 80% ... good for travel").  The limits are the firmware's
 * and the helper, k4510-charge, sets them under sudo -n; its note of the old
 * ones (CHARGE_NOTE) is the truth, on the persistence partition, so the row
 * follows it at start.  The battery poll puts the limits back when the
 * battery is full, or is unplugged after it has charged. */
#define CHARGE_NOTE "/home/k4510/.k4510-charge"
static int charge_charged;                       /* it has charged since the once was asked for */
static void charge_run(const char *how)
{
    pid_t pid = fork();
    if (pid == 0) {
        int fd = open("/dev/null", O_RDWR); if (fd >= 0) { dup2(fd, 1); dup2(fd, 2); }
        execlp("sudo", "sudo", "-n", "/usr/local/sbin/k4510-charge", how, (char *) NULL);
        _exit(127);
    }
    if (pid > 0) waitpid(pid, NULL, 0);          /* two writes to /sys: done at once */
}
void host_charge_apply(void)
{
    static int first = 1;
    if (access("/etc/k4510-linux", F_OK) != 0) { settings_set(SET_HOST_CHARGE_ONCE, 0); return; }   /* a desktop's battery is the desktop's */
    int noted = access(CHARGE_NOTE, F_OK) == 0, want = settings_get(SET_HOST_CHARGE_ONCE) != 0;
    if (first) { first = 0; settings_set(SET_HOST_CHARGE_ONCE, noted); return; }
    if (want == noted) return;
    charge_run(want ? "full" : "normal"); charge_charged = 0;
    settings_set(SET_HOST_CHARGE_ONCE, access(CHARGE_NOTE, F_OK) == 0);   /* what took: a battery without limits stays off */
}
static void battery_info(void)                   /* F12 -> Info -> Battery, from the same byte as the band */
{
    char t[48]; int n;
    if (io_battery == 0xFF) { snprintf(t, sizeof t, "none"); menu_info(INFO_BATT, t); return; }
    n = snprintf(t, sizeof t, "%d%%, %s", io_battery & 0x7F, (io_battery & 0x80) ? "on AC / charging" : "on battery");
    if (batt_min >= 0) n += snprintf(t + n, sizeof t - n, ", %d:%02d %s", batt_min / 60, batt_min % 60, (io_battery & 0x80) ? "to full" : "left");
    if (settings_get(SET_HOST_CHARGE_ONCE) && n < (int) sizeof t) snprintf(t + n, sizeof t - n, ", to 100%% once");
    menu_info(INFO_BATT, t);
}
static long sysval(const char *dev, const char *name)   /* a number from /sys/class/power_supply/DEV/NAME; -1 none */
{
    char path[300], buf[32]; long v = -1;
    snprintf(path, sizeof path, "/sys/class/power_supply/%s/%s", dev, name);
    FILE *f = fopen(path, "r");
    if (f) { if (fgets(buf, sizeof buf, f)) v = atol(buf); fclose(f); }
    return v;
}
void host_battery_poll(void)
{
    static Uint32 at; static int first = 1;
    io_batt_min = (batt_min >= 0 && io_battery != 0xFF && settings_get(SET_TERM_BATTTIME)) ? (uint16_t) batt_min : 0xFFFF;   /* the switch, at once */
    if (!first && SDL_GetTicks() - at < 10000) return;
    first = 0; at = SDL_GetTicks();
    const char *fake = getenv("K4510_BATTERY");
    if (fake) { int p = atoi(fake); const char *m = strchr(fake, ',');
                io_battery = (uint8_t)((p < 0 ? 0 : p > 100 ? 100 : p) | (strchr(fake, '+') ? 0x80 : 0));
                batt_min = m ? atoi(m + 1) : -1; battery_info(); return; }
    static double avg; static int dir;           /* the averaged current (or power), and which way it went: 1 down, 2 up */
    int pct = -1, ac = 0, d2 = 0, full = 0; long left = -1, rate = -1; char path[300], buf[32];
    DIR *d = opendir("/sys/class/power_supply");
    if (d) {
        struct dirent *de;
        while ((de = readdir(d))) {
            if (de->d_name[0] == '.') continue;
            snprintf(path, sizeof path, "/sys/class/power_supply/%s/type", de->d_name);
            FILE *f = fopen(path, "r"); if (!f) continue;
            if (!fgets(buf, sizeof buf, f)) buf[0] = 0; fclose(f);
            if (!strncmp(buf, "Battery", 7) && pct < 0) {
                snprintf(path, sizeof path, "/sys/class/power_supply/%s/capacity", de->d_name);
                if ((f = fopen(path, "r"))) { if (fgets(buf, sizeof buf, f)) pct = atoi(buf); fclose(f); }
                snprintf(path, sizeof path, "/sys/class/power_supply/%s/status", de->d_name);
                if ((f = fopen(path, "r"))) {
                    if (!fgets(buf, sizeof buf, f)) buf[0] = 0;
                    fclose(f);
                    if (!strncmp(buf, "Charging", 8) || !strncmp(buf, "Full", 4)) ac = 1;
                    full = !strncmp(buf, "Full", 4);
                    d2 = !strncmp(buf, "Discharging", 11) ? 1 : !strncmp(buf, "Charging", 8) ? 2 : 0;
                }
                long now = sysval(de->d_name, "charge_now"), full = sysval(de->d_name, "charge_full");
                rate = sysval(de->d_name, "current_now");
                if (now < 0 || rate < 0) { now = sysval(de->d_name, "energy_now"); full = sysval(de->d_name, "energy_full"); rate = sysval(de->d_name, "power_now"); }
                if (now >= 0 && rate > 0) left = d2 == 1 ? now : d2 == 2 && full > now ? full - now : -1;
            } else if (!strncmp(buf, "Mains", 5)) {
                snprintf(path, sizeof path, "/sys/class/power_supply/%s/online", de->d_name);
                if ((f = fopen(path, "r"))) { if (fgets(buf, sizeof buf, f) && buf[0] == '1') ac = 1; fclose(f); }
            }
        }
        closedir(d);
    }
    io_battery = pct < 0 ? 0xFF : (uint8_t)((pct > 100 ? 100 : pct) | (ac ? 0x80 : 0));
    if (settings_get(SET_HOST_CHARGE_ONCE) && pct >= 0) {                 /* the once: spent when full, or unplugged after charging */
        if (d2 == 2) charge_charged = 1;
        if (pct >= 100 || full || (d2 == 1 && charge_charged)) {
            charge_run("normal"); charge_charged = 0;
            settings_set(SET_HOST_CHARGE_ONCE, access(CHARGE_NOTE, F_OK) == 0);
        }
    }
    if (pct < 0 || left < 0 || !d2) { dir = 0; batt_min = -1; }
    else {
        if (d2 != dir) { dir = d2; avg = (double) rate; } else avg += ((double) rate - avg) / 8;   /* 10 s polls: 1/8 a step */
        double m = (double) left / avg * 60;
        batt_min = m < 1 ? 0 : m > 5999 ? -1 : (int) m;   /* past 99:59 is no estimate */
    }
    battery_info();
}
/* The network for the bottom band (io_net), every ten seconds: a cable that
 * is up wins, then Wi-Fi with its link quality (/proc/net/wireless, out of
 * 70), then anything else up but the loopback (a container's veth, a VPN).
 * K4510_NET ("wifi:77", "lan", "net", "none") stands in, for a test.
 * Doc, 2026-10-06: "a WIFI or Network indicator in one of the bars". */
void host_net_poll(void)
{
    static Uint32 at; static int first = 1;
    if (!first && SDL_GetTicks() - at < 10000) return;
    first = 0; at = SDL_GetTicks();
    const char *fake = getenv("K4510_NET");
    if (fake) {
        if (!strncmp(fake, "wifi", 4)) { int q = fake[4] == ':' ? atoi(fake + 5) : 70; io_net = NET_WIFI; io_net_q = (uint8_t)(q < 0 ? 0 : q > 100 ? 100 : q); }
        else io_net = !strcmp(fake, "lan") ? NET_WIRED : !strcmp(fake, "net") ? NET_OTHER : NET_NONE;
        return;
    }
    int wired = 0, other = 0, wq = -1; char path[300], buf[64];
    DIR *d = opendir("/sys/class/net");
    if (!d) { io_net = 0xFF; return; }
    struct dirent *de;
    while ((de = readdir(d))) {
        if (de->d_name[0] == '.' || !strcmp(de->d_name, "lo")) continue;
        snprintf(path, sizeof path, "/sys/class/net/%s/operstate", de->d_name);
        FILE *f = fopen(path, "r"); if (!f) continue;
        if (!fgets(buf, sizeof buf, f)) buf[0] = 0; fclose(f);
        int up = !strncmp(buf, "up", 2);
        if (!up && strncmp(buf, "unknown", 7)) continue;                 /* a tun device says unknown while it works */
        snprintf(path, sizeof path, "/sys/class/net/%s/wireless", de->d_name);
        int wifi = access(path, F_OK) == 0;
        snprintf(path, sizeof path, "/sys/class/net/%s/device", de->d_name);
        int phys = access(path, F_OK) == 0;
        if (wifi && up) {
            FILE *w = fopen("/proc/net/wireless", "r"); int q = 0;
            if (w) { char line[256]; size_t nl = strlen(de->d_name);
                     while (fgets(line, sizeof line, w)) { char *p = line; while (*p == ' ') p++;
                         if (!strncmp(p, de->d_name, nl) && p[nl] == ':') { float lq = 0; if (sscanf(p + nl + 1, "%*s %f", &lq) == 1) q = (int)(lq * 100 / 70); } }
                     fclose(w); }
            if (q > wq) wq = q;
        } else if (phys && up) wired = 1;
        else if (up) other = 1;
    }
    closedir(d);
    io_net = wired ? NET_WIRED : wq >= 0 ? NET_WIFI : other ? NET_OTHER : NET_NONE;
    io_net_q = (uint8_t)(wq < 0 ? 0 : wq > 100 ? 100 : wq);
}
/* F12 -> Host -> Keyboard layout and F12 -> Input -> Caps Lock is Ctrl, for the
 * Linux beside the machine: its consoles at once (k4510-keymap --set writes
 * /etc/default/keyboard and reloads the kernel keymap), the machine's own
 * typing at the emulator's next start -- SDL reads that keymap once, at init.
 * The helper applies the saved pair at every boot too, before tty1, since
 * /etc lives in RAM.  Called once before the frame loop (to note what the boot
 * applied) and at each menu close.  Doc, 2026-09-12. */
static pid_t kbd_child;
void host_keymap_apply(void)
{
    static int last_layout = -1, last_caps = -1;
    int l = settings_get(SET_INPUT_KBD_LAYOUT), c = settings_get(SET_INPUT_CAPS_CTRL);
    if (kbd_child > 0 && waitpid(kbd_child, NULL, WNOHANG) == kbd_child) kbd_child = 0;
    if (last_layout < 0) { last_layout = l; last_caps = c; return; }
    if (l == last_layout && c == last_caps) return;
    if (access("/etc/k4510-linux", F_OK) != 0) { last_layout = l; last_caps = c; return; }   /* a desktop owns its keyboard */
    if (kbd_child > 0) return;                        /* one at a time; the next close tries again */
    last_layout = l; last_caps = c;
    char buf[32]; const char *name = settings_text(SET_INPUT_KBD_LAYOUT, buf, sizeof buf);   /* the RETURNED text: for an
                                                                * ENUM it is the label itself, and buf stays empty -- the
                                                                * helper got "--set '' 1" and kept "us" (the Dell, 2026-09-12) */
    pid_t pid = fork();
    if (pid == 0) {
        int fd = open("/dev/null", O_RDWR); if (fd >= 0) { dup2(fd, 1); dup2(fd, 2); }
        execlp("sudo", "sudo", "-n", "/usr/local/sbin/k4510-keymap", "--set", name, c ? "1" : "0", (char *) NULL);
        _exit(127);
    }
    if (pid > 0) kbd_child = pid;
}
/* F12 -> Host -> Lid closed.  logind's own rule (k4510-lid.conf) is to suspend
 * on the lid; "keep running", the default -- Doc's rule of 2026-09-11, when a
 * closed lid suspending the machine was the complaint -- holds logind's
 * handle-lid-switch lock for as long as this emulator lives, and "suspend"
 * lets it go.  The lock needs root (there is no polkit on the K4510 Linux),
 * so it is systemd-inhibit under sudo -n, as k4510-keymap is; what it holds
 * the lock around is a loop that ends when this process does, so a crash
 * cannot leave the lid locked.  Called before the frame loop and at each
 * menu close; atexit lets it go on a clean quit. */
static pid_t lid_child;
static void host_lid_release(void)
{
    if (lid_child > 0) { kill(lid_child, SIGTERM); waitpid(lid_child, NULL, 0); lid_child = 0; }
}
void host_lid_apply(void)
{
    static int registered;
    int hold = settings_get(SET_HOST_LID) == 0;                 /* 0 keep running, 1 suspend */
    if (lid_child > 0 && waitpid(lid_child, NULL, WNOHANG) == lid_child) lid_child = 0;   /* it died: take it again */
    if (access("/etc/k4510-linux", F_OK) != 0) return;          /* a desktop's lid is the desktop's */
    if (!hold) { host_lid_release(); return; }
    if (lid_child > 0) return;
    char loop[96];
    snprintf(loop, sizeof loop, "while kill -0 %d 2>/dev/null; do sleep 5; done", (int) getpid());
    pid_t pid = fork();
    if (pid == 0) {
        int fd = open("/dev/null", O_RDWR); if (fd >= 0) { dup2(fd, 1); dup2(fd, 2); }
        execlp("sudo", "sudo", "-n", "systemd-inhibit", "--what=handle-lid-switch", "--mode=block",
               "--who=K4510", "--why=F12 > Host > Lid closed: keep running", "sh", "-c", loop, (char *) NULL);
        _exit(127);
    }
    if (pid > 0) { lid_child = pid; if (!registered) { atexit(host_lid_release); registered = 1; } }
}
void host_info_refresh(void)
{
    char name[64] = "?", addr[40] = "none", ts[40] = "none";
    if (gethostname(name, sizeof name) != 0) snprintf(name, sizeof name, "?");
    name[sizeof name - 1] = 0;
    struct ifaddrs *ifa0 = NULL;
    if (getifaddrs(&ifa0) == 0) {
        for (struct ifaddrs *i = ifa0; i; i = i->ifa_next) {
            if (!i->ifa_addr || i->ifa_addr->sa_family != AF_INET || !i->ifa_name) continue;
            if (strcmp(i->ifa_name, "lo") == 0) continue;
            char buf[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &((struct sockaddr_in *)i->ifa_addr)->sin_addr, buf, sizeof buf);
            if (strncmp(i->ifa_name, "tailscale", 9) == 0) { if (!strcmp(ts, "none")) snprintf(ts, sizeof ts, "%s", buf); }
            else if (!strcmp(addr, "none")) snprintf(addr, sizeof addr, "%s (%s)", buf, i->ifa_name);
        }
        freeifaddrs(ifa0);
    }
    /* No address yet: say whether the Wi-Fi is still joining.  Right after
     * boot it takes ~25 s, and "none" read as "not connected" (Doc, the
     * Dell, 2026-09-12).  NetworkManager knows; only the K4510 Linux asks. */
    if (!strcmp(addr, "none") && access("/etc/k4510-linux", F_OK) == 0) {
        FILE *p = popen("nmcli -t -f STATE general 2>/dev/null", "r");
        char st[40] = "";
        if (p) { if (!fgets(st, sizeof st, p)) st[0] = 0; pclose(p); }
        if (!strncmp(st, "connecting", 10)) snprintf(addr, sizeof addr, "connecting...");
        else if (!strncmp(st, "disconnected", 12) || !strncmp(st, "asleep", 6)) snprintf(addr, sizeof addr, "none (see Wi-Fi setup)");
    }
    if (!strcmp(ts, "none") && access("/etc/k4510-linux", F_OK) == 0 && !strncmp(addr, "connecting", 10)) snprintf(ts, sizeof ts, "waiting for the network");
    menu_info(INFO_NAME, name); menu_info(INFO_ADDR, addr); menu_info(INFO_TS, ts);
}
void host_net_setup(void)
{
    if (host_child > 0) return;                       /* one at a time */
    pid_t pid = fork();
    if (pid == 0) {
        execlp("sudo", "sudo", "-n", "openvt", "-s", "-w", "--", "env", "TERM=linux", "nmtui", (char *) NULL);
        _exit(127);
    }
    host_child = pid > 0 ? pid : 0;
}
void host_reap(void)                          /* once a frame */
{
    if (host_child > 0 && waitpid(host_child, NULL, WNOHANG) == host_child) { host_child = 0; host_info_refresh(); }
}

