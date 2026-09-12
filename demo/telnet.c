/* K4510: TELNET host port -- a terminal on the N: device ($D900).
 * Opens tcp://host:port on channel 0, sends what you type, and what
 * arrives goes to JIM, the terminal ($DA00): a VT100/ANSI in hardware, so
 * BBSes get their ANSI art and colours, and cursor and function keys go
 * out as VT sequences. F12 hangs up (Escape is a key the far end wants).
 * Telnet option negotiation (IAC): the server is told the terminal type
 * and the window size (JIM's columns and rows, NAWS), so a BBS lays its
 * screens out for this screen.  The type is a list, one name per TTYPE
 * SEND (RFC 1091): XTERM-COLOR, VT220, VT100, ANSI, and ANSI again once
 * the list is spent.  A Linux host takes the first -- its TERM=ansi is
 * the PC's ANSI.SYS, which garbled htop on JIM; xterm-color draws it
 * clean (test/ttypetest.sh) -- and an older system or a BBS that wants
 * ANSI keeps asking until it hears a name it knows.  Every other DO/WILL gets a
 * WONT/DONT, so plain servers, MUDs and a raw TCP echo talk too.
 * UTF-8: a Linux host's programs speak it -- Claude Code's bullets and lines
 * were noise without (Doc, 2026-09-12).  But CP437 art can be valid UTF-8 by
 * accident (C4 B3), so JIM decodes (ESC % G) only for a far end that asks the
 * terminal type and takes the first answer, XTERM-COLOR: that is a Unix host.
 * One that asks again is working down to VT220/ANSI -- a BBS or an old system --
 * and gets CP437 back (ESC % @); one that never asks never leaves CP437.  In a
 * UTF-8 session an accented letter typed goes out as UTF-8 too. */
#include "k4510.h"

#define NET      0xD900u
#define NET_CMD  (NET + 0)
#define NET_ST   (NET + 1)
#define NET_CHAN (NET + 2)
#define TERM     0xDA00u

void __fastcall__ rom_chrout(unsigned char c);
unsigned char rom_getin(void);
static unsigned char rom_args(void) { return ((unsigned char (*)(void))0xFF95)(); }

static char url[96];
static unsigned char buf[256], rep[12], sb[16], sbn, cmd;
static const char *const ttypes[] = { "XTERM-COLOR", "VT220", "VT100", "ANSI" };
static unsigned char tt[20], tti;                         /* the TTYPE IS reply, and which name is next */
static unsigned char u8;                                  /* the session is UTF-8 (JIM decoding) */
static const unsigned int cp437u[128] = {                 /* CP437 $80-$FF -> Unicode, for a typed letter */
    0x00C7,0x00FC,0x00E9,0x00E2,0x00E4,0x00E0,0x00E5,0x00E7,0x00EA,0x00EB,0x00E8,0x00EF,0x00EE,0x00EC,0x00C4,0x00C5,
    0x00C9,0x00E6,0x00C6,0x00F4,0x00F6,0x00F2,0x00FB,0x00F9,0x00FF,0x00D6,0x00DC,0x00A2,0x00A3,0x00A5,0x20A7,0x0192,
    0x00E1,0x00ED,0x00F3,0x00FA,0x00F1,0x00D1,0x00AA,0x00BA,0x00BF,0x2310,0x00AC,0x00BD,0x00BC,0x00A1,0x00AB,0x00BB,
    0x2591,0x2592,0x2593,0x2502,0x2524,0x2561,0x2562,0x2556,0x2555,0x2563,0x2551,0x2557,0x255D,0x255C,0x255B,0x2510,
    0x2514,0x2534,0x252C,0x251C,0x2500,0x253C,0x255E,0x255F,0x255A,0x2554,0x2569,0x2566,0x2560,0x2550,0x256C,0x2567,
    0x2568,0x2564,0x2565,0x2559,0x2558,0x2552,0x2553,0x256B,0x256A,0x2518,0x250C,0x2588,0x2584,0x258C,0x2590,0x2580,
    0x03B1,0x00DF,0x0393,0x03C0,0x03A3,0x03C3,0x00B5,0x03C4,0x03A6,0x0398,0x03A9,0x03B4,0x221E,0x03C6,0x03B5,0x2229,
    0x2261,0x00B1,0x2265,0x2264,0x2320,0x2321,0x00F7,0x2248,0x00B0,0x2219,0x00B7,0x221A,0x207F,0x00B2,0x25A0,0x00A0 };

static void say(const char *s) { while (*s) REG(TERM) = *s++; }
static unsigned char utf8_of(unsigned char k, unsigned char *o)   /* a CP437 letter -> its UTF-8 bytes */
{
    unsigned int u = cp437u[k - 0x80];
    if (u < 0x800) { o[0] = (unsigned char)(0xC0 | (u >> 6)); o[1] = (unsigned char)(0x80 | (u & 0x3F)); return 2; }
    o[0] = (unsigned char)(0xE0 | (u >> 12)); o[1] = (unsigned char)(0x80 | ((u >> 6) & 0x3F)); o[2] = (unsigned char)(0x80 | (u & 0x3F));
    return 3;
}
static unsigned char net(unsigned char cmd) { REG(NET_CMD) = cmd; return REG(NET_ST); }
static void net_send(const unsigned char *p, unsigned int n) { w32(NET + 8, (uint16_t)p); w32(NET + 12, n); net(3); }

void main(void)
{
    unsigned char n = rom_args(), i = 0, k, iac = 0, obg = 0, odbg = 0;
    const char *p = *(const char **)0xF0;
    unsigned int got, j;                              /* j indexes the 256-byte read buffer: a byte would wrap on a full one */
    REG(TERM + 4) = 1;                                    /* JIM: defaults, home... */
    REG(TERM + 9) = 0;                                    /* ...at the console's line (run_at handed the row over; the column is 0) */
    tti = 0;                                              /* each session offers the TTYPE list from the top */
    if (!n) { say("telnet: host port  (F12 hangs up)\r\n"); return; }
    url[i++] = 't'; url[i++] = 'c'; url[i++] = 'p'; url[i++] = ':'; url[i++] = '/'; url[i++] = '/';
    while (*p && *p != ' ' && i < 90) url[i++] = *p++;
    while (*p == ' ') p++;
    if (!*p) { say("telnet: host port\r\n"); return; }
    url[i++] = ':';
    while (*p && *p != ' ' && i < 94) url[i++] = *p++;
    url[i] = 0;
    REG(NET_CHAN) = 0;
    w32(NET + 4, (uint16_t)url);
    if (REG(NET_ST) == 6) { say("telnet: no network on this host\r\n"); return; }
    if (net(1)) { say("telnet: cannot connect to "); say(url + 6); say("\r\n"); return; }
    /* A BBS lays its screens out for a black terminal -- its ANSI sets the
     * colours it wants and returns to "default" for the rest, so on the
     * machine's blue that default fights the art. Go black for the session
     * and give the machine its own colours back on the way out. */
    obg  = REG(V_BGCOL);
    odbg = REG(TERM + 0x15);                              /* JIM's DEFBG: where SGR 0 and 49 land */
    REG(TERM + 0x15) = 0;
    REG(TERM + 0x0C) = 0;                                 /* and what it paints with now */
    REG(V_BGCOL)     = 0;                                 /* the screen behind the terminal */
    REG(TERM + 4) = 2;                                    /* clear, so no blue is left around the art */
    say("connected to "); say(url + 6); say("  (F12 hangs up)\r\n");
    u8 = 0;                                               /* CP437 until the far end takes XTERM-COLOR */
    say("\033[20l");                                      /* LNM off: a far end's bare LF keeps the column (tmux moves down
                                                           * that way); the ROM console gets its LNM back on the way out */
    REG(TERM + 0x0E) = 1;                                 /* JIM's cursor */
    for (;;) {
        k = rom_getin();
        if (k == 0x9B && (REG(KBDST) & 0x40)) break;      /* F12 (the kind bit: $9B is also a letter) */
        if (k == 0x0D) { buf[0] = 13; buf[1] = 10; net_send(buf, 2); }
        else if (k >= 0x80 && !(REG(KBDST) & 0x40)) {   /* an accented letter: not through JIM, which would make it a cursor key */
            if (u8) i = utf8_of(k, buf); else { buf[0] = k; i = 1; }
            net_send(buf, i);
        }
        else if (k) {                                     /* through JIM: arrows and F-keys become VT sequences */
            REG(TERM + 3) = k;
            for (i = 0; (REG(TERM + 1) & 0x80) && i < 16; i++) buf[i] = REG(TERM + 2);
            if (i) net_send(buf, i);
        }
        w32(NET + 8, (uint16_t)buf); w32(NET + 12, sizeof buf);
        k = net(2);
        got = REG(NET + 12) | ((unsigned int)REG(NET + 13) << 8);
        for (j = 0; j < got; j++) {
            unsigned char c = buf[j];
            if (iac == 1) {                               /* after IAC */
                if (c == 255) { iac = 0; REG(TERM) = 255; continue; }
                if (c == 250) { iac = 3; sbn = 0; continue; }   /* SB: a subnegotiation follows */
                iac = (c >= 251 && c <= 254) ? 2 : 0; cmd = c; continue;
            }
            if (iac == 2) {                               /* DO/DONT/WILL/WONT + option */
                iac = 0;
                if (cmd == 253 && c == 31) {              /* DO NAWS: WILL, then the size */
                    rep[0] = 255; rep[1] = 251; rep[2] = 31; net_send(rep, 3);
                    rep[0] = 255; rep[1] = 250; rep[2] = 31; rep[3] = 0; rep[4] = REG(TERM + 5); rep[5] = 0; rep[6] = REG(TERM + 6); rep[7] = 255; rep[8] = 240;
                    net_send(rep, 9);
                } else if (cmd == 253 && c == 24) { rep[0] = 255; rep[1] = 251; rep[2] = 24; net_send(rep, 3); }   /* DO TTYPE: WILL; the server asks next */
                else if (c == 0 || c == 1 || c == 3) {   /* BINARY, ECHO, SGA: agreed, not refused -- a BBS echoes for
                                                         * us, runs character-at-a-time (a Major BBS hangs up
                                                         * without SGA) and needs 8 bits for its CP437 art */
                    rep[0] = 255; rep[2] = c;
                    rep[1] = (cmd == 251) ? 253 : (cmd == 252) ? 254 : (cmd == 253) ? 251 : 252;
                    net_send(rep, 3);
                }
                else { rep[0] = 255; rep[1] = (cmd == 251 || cmd == 252) ? 254 : 252; rep[2] = c; net_send(rep, 3); }
                continue;
            }
            if (iac == 3) {                               /* inside SB ... IAC SE */
                if (c == 255) { iac = 4; continue; }
                if (sbn < sizeof sb) sb[sbn++] = c;
                continue;
            }
            if (iac == 4) {
                if (c == 240) {                           /* SE: TTYPE SEND -> IS the next name on the list */
                    iac = 0;
                    if (sbn >= 2 && sb[0] == 24 && sb[1] == 1) {
                        const char *t = ttypes[tti];
                        unsigned char n = 4;
                        if (tti == 0) { say("\033%G"); u8 = 1; }          /* offering XTERM-COLOR: UTF-8 if it is taken */
                        else if (u8) { say("\033%@"); u8 = 0; }           /* asked again: not a Unix host, CP437 */
                        if (tti < 3) tti++;               /* the last name repeats: the list is spent */
                        tt[0] = 255; tt[1] = 250; tt[2] = 24; tt[3] = 0;
                        while (*t) tt[n++] = *t++;
                        tt[n++] = 255; tt[n++] = 240;
                        net_send(tt, n);
                    }
                    continue;
                }
                if (sbn < sizeof sb) sb[sbn++] = c; iac = 3; continue;
            }
            if (c == 255) { iac = 1; continue; }
            REG(TERM) = c;
        }
        for (i = 0; (REG(TERM + 1) & 0x80) && i < 16; i++) buf[i] = REG(TERM + 2);   /* JIM's replies (a BBS asking where the cursor is) */
        if (i) net_send(buf, i);
        if (k == 4) { say("\r\nconnection closed by the far end\r\n"); break; }
        if (!got) wait_vblank();
    }
    net(4);
    if (u8) { say("\033%@"); u8 = 0; }                     /* the machine's own screen is CP437 */
    say("\033[20h");                                      /* and LNM, as the ROM's video_init sets it */
    REG(TERM + 0x0E) = 0;
    REG(TERM + 0x15) = odbg;                              /* every exit comes through here: F12, */
    REG(TERM + 0x0C) = odbg;                              /* a far end that hung up, or a closed */
    REG(V_BGCOL)     = obg;                               /* socket -- so the colours always return */
}
